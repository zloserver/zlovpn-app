#include "connectionController.h"

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
#include <QGuiApplication>
#else
#include <QApplication>
#endif
#include <QTimer>
#include <QtConcurrent>

#include "core/controllers/vpnConfigurationController.h"
#include "core/enums/apiEnums.h"
#include "version.h"

#include "amnezia_application.h"

ConnectionController::ConnectionController(const QSharedPointer<ImportController> importController,
                                           const QSharedPointer<AuthController> authController,
                                           const QSharedPointer<RegionsModel> regionsModel,
                                           const QSharedPointer<ServersModel> &serversModel,
                                           const QSharedPointer<ContainersModel> &containersModel,
                                           const QSharedPointer<ClientManagementModel> &clientManagementModel,
                                           const QSharedPointer<VpnConnection> &vpnConnection,
                                           const std::shared_ptr<Settings> &settings, QObject *parent) :
    QObject(parent), m_importController(importController), m_authController(authController),
    m_regionsModel(regionsModel), m_serversModel(serversModel), m_containersModel(containersModel),
    m_clientManagementModel(clientManagementModel), m_vpnConnection(vpnConnection), m_settings(settings) {
    connect(m_vpnConnection.get(), &VpnConnection::connectionStateChanged, this,
            &ConnectionController::onConnectionStateChanged);
    connect(this, &ConnectionController::connectToVpn, m_vpnConnection.get(), &VpnConnection::connectToVpn,
            Qt::QueuedConnection);
    connect(this, &ConnectionController::disconnectFromVpn, m_vpnConnection.get(), &VpnConnection::disconnectFromVpn,
            Qt::QueuedConnection);
    connect(m_vpnConnection.get(), &VpnConnection::splitTunnelingFailed, this, [this]() {
        emit splitTunnelingFailed();
        closeConnection();
    });

    connect(this, &ConnectionController::configFromApiUpdated, this, &ConnectionController::continueConnection);

    connect(m_importController.get(), qOverload<const QString &, bool>(&ImportController::importErrorOccurred), this,
            [this](const QString &errorMessage, bool goToPageHome) {
                if (m_vpnConnection->connectionState() != Vpn::ConnectionState::Preparing)
                    return;

                emit connectionErrorOccurred(errorMessage);
                emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
            });

    connect(m_importController.get(), qOverload<ErrorCode, bool>(&ImportController::importErrorOccurred), this,
            [this](const ErrorCode errorCode, bool goToHome) {
                if (m_vpnConnection->connectionState() != Vpn::ConnectionState::Preparing)
                    return;

                emit connectionErrorOccurred(errorCode);
                emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
            });

    connect(m_importController.get(), &ImportController::importFinished, this, [this]() {
        m_serversModel->setDefaultServerIndex(0);
        m_serversModel->setProcessedServerIndex(0);
        continueConnection();
    });

    connect(m_authController.get(), &AuthController::tokenUpdated, this, [this]() {
        if (!m_authController->isAuthenticated()) {
            closeConnection();
        }
    });

    connect(m_authController.get(), &AuthController::regionsUpdated, this, [this]() {
        // The home page asks for regions before the session is checked. Auto-connecting on those would race the
        // token refresh with the old token (401 and a logout), so wait for regions loaded with a checked session.
        if (!m_authController->isAuthenticated() && !m_authController->isOfflineMode())
            return;

        if (m_isFirstStart && m_settings->isAutoConnect()) {
            openConnection();
        }

        m_isFirstStart = false;
    });

    m_state = Vpn::ConnectionState::Disconnected;
}

void ConnectionController::openConnection() {
    // #if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    //     if (!Utils::processIsRunning(Utils::executable(SERVICE_NAME, false), true))
    //     {
    //         emit connectionErrorOccurred(ErrorCode::AmneziaServiceNotRunning);
    //         return;
    //     }
    // #endif

    /*int serverIndex = m_serversModel->getDefaultServerIndex();
    QJsonObject serverConfig = m_serversModel->getServerConfig(serverIndex);
    auto configVersion = serverConfig.value(config_key::configVersion).toInt();*/

    emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Preparing);

    m_serversModel->clearServers();
    m_cachedCandidates.clear();
    m_pendingUrl.clear();

    QString selectedRegionId = m_regionsModel->getSelectedRegionId();
    if (selectedRegionId.isEmpty()) {
        emit connectionErrorOccurred(tr("Region not selected"));
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Disconnected);
        return;
    }

    if (m_authController->isOfflineMode()) {
        connectFromCache(selectedRegionId, false);
        return;
    }

    m_apiAttempts = 0;
    requestConfig(selectedRegionId);
}

void ConnectionController::requestConfig(const QString &regionId) {
    m_apiAttempts++;

    ServerStringRequest *request = new ServerStringRequest;
    connect(request, &ServerStringRequest::errorOccurred, this, [this, request, regionId](const Errors errors) {
        request->deleteLater();

        // no answer from the API is not a reason to stay offline when we have configs that worked before
        bool apiUnreachable = errors.kind == Errors::Kind::Network || errors.kind == Errors::Kind::Server;
        if (apiUnreachable && !m_authController->configCache().servers(regionId).isEmpty()) {
            connectFromCache(regionId, true);
            return;
        }

        emit connectionErrorOccurred(errors.errorMessage);
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
    });
    connect(request, &ServerStringRequest::stringArrived, this,
            [this, request, regionId](const QString connectionString) {
                request->deleteLater();

                // the API picks a random server of the region and may pick one the Xray client can't run
                if (!ConfigCache::isSupported(connectionString)) {
                    qWarning() << "Unsupported server config for region" << regionId << ", attempt" << m_apiAttempts;
                    if (m_apiAttempts < MAX_API_ATTEMPTS) {
                        requestConfig(regionId);
                        return;
                    }
                    emit connectionErrorOccurred(tr("The servers of this region are not supported by the app"));
                    emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
                    return;
                }

                connectWithConfig(regionId, connectionString);
            });

    m_authController->getServerConnectionString(regionId, request);
}

void ConnectionController::connectFromCache(const QString &regionId, bool announce) {
    m_cachedCandidates = m_authController->configCache().servers(regionId);
    if (m_cachedCandidates.isEmpty()) {
        emit connectionErrorOccurred(tr("No saved settings for this region"));
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
        return;
    }

    if (announce)
        emit connectionNoticeOccurred(tr("No connection to the ZloVPN server, connecting with saved settings"));

    m_cachedRegionId = regionId;
    connectNextCachedServer();
}

bool ConnectionController::connectNextCachedServer() {
    if (m_cachedCandidates.isEmpty())
        return false;

    ConfigCache::Server server = m_cachedCandidates.takeFirst();
    qInfo() << "Connecting with a saved config of" << server.host << "for region" << m_cachedRegionId;
    m_serversModel->clearServers();
    connectWithConfig(m_cachedRegionId, server.url);
    return true;
}

void ConnectionController::connectWithConfig(const QString &regionId, const QString &url) {
    m_pendingRegionId = regionId;
    m_pendingUrl = url;

    if (!m_importController->extractConfigFromData(url)) {
        emit connectionErrorOccurred(tr("Failed to import config"));
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Error, false);
        return;
    }

    m_importController->importConfig();
}

void ConnectionController::closeConnection() {
    m_cachedCandidates.clear();
    emit disconnectFromVpn();
}

ErrorCode ConnectionController::getLastConnectionError() { return m_vpnConnection->lastError(); }

void ConnectionController::onConnectionStateChanged(Vpn::ConnectionState state, bool getLastError) {
    m_state = state;

    m_isConnected = false;
    m_connectionStateText = tr("Connecting...");
    switch (state) {
        case Vpn::ConnectionState::Connected: {
            amnApp->resetNetworkConnections();
            // the config worked: remember it for connecting without the API
            if (!m_pendingUrl.isEmpty()) {
                m_authController->configCache().rememberServer(m_pendingRegionId, m_pendingUrl);
                qInfo() << "Saved config of" << ConfigCache::hostOf(m_pendingUrl) << "for region" << m_pendingRegionId;
                m_pendingUrl.clear();
            }
            m_cachedCandidates.clear();
            m_isConnectionInProgress = false;
            m_isConnected = true;
            m_connectionStateText = tr("Connected");
            break;
        }
        case Vpn::ConnectionState::Connecting: {
            m_isConnectionInProgress = true;
            break;
        }
        case Vpn::ConnectionState::Reconnecting: {
            m_isConnectionInProgress = true;
            m_connectionStateText = tr("Reconnecting...");
            break;
        }
        case Vpn::ConnectionState::Disconnected: {
            amnApp->resetNetworkConnections();
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            break;
        }
        case Vpn::ConnectionState::Disconnecting: {
            m_isConnectionInProgress = true;
            m_connectionStateText = tr("Disconnecting...");
            break;
        }
        case Vpn::ConnectionState::Preparing: {
            m_isConnectionInProgress = true;
            m_connectionStateText = tr("Preparing...");
            break;
        }
        case Vpn::ConnectionState::Error: {
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            // a saved server of the region failed: try the next saved one before reporting
            if (!m_cachedCandidates.isEmpty()) {
                QTimer::singleShot(0, this, [this]() {
                    emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Preparing);
                    connectNextCachedServer();
                });
                break;
            }
            if (getLastError)
                emit connectionErrorOccurred(getLastConnectionError());
            break;
        }
        case Vpn::ConnectionState::Unknown: {
            m_isConnectionInProgress = false;
            m_connectionStateText = tr("Connect");
            if (getLastError)
                emit connectionErrorOccurred(getLastConnectionError());
            break;
        }
    }
    emit connectionStateChanged();
}

void ConnectionController::onCurrentContainerUpdated() {
    if (m_isConnected || m_isConnectionInProgress) {
        emit reconnectWithUpdatedContainer(tr("Settings updated successfully, reconnnection..."));
        openConnection();
    } else {
        emit reconnectWithUpdatedContainer(tr("Settings updated successfully"));
    }
}

void ConnectionController::onTranslationsUpdated() {
    // get translated text of current state
    onConnectionStateChanged(getCurrentConnectionState());
}

Vpn::ConnectionState ConnectionController::getCurrentConnectionState() { return m_state; }

QString ConnectionController::connectionStateText() const { return m_connectionStateText; }

void ConnectionController::toggleConnection() {
    qInfo() << "Connect button pressed in state" << m_state << "in progress" << m_isConnectionInProgress << "offline mode"
            << m_authController->isOfflineMode();
    if (m_state == Vpn::ConnectionState::Preparing) {
        emit preparingConfig();
        return;
    }

    if (isConnectionInProgress()) {
        closeConnection();
    } else if (isConnected()) {
        closeConnection();
    } else {
        openConnection();
    }
}

bool ConnectionController::isConnectionInProgress() const { return m_isConnectionInProgress; }

bool ConnectionController::isConnected() const { return m_isConnected; }

bool ConnectionController::isProtocolConfigExists(const QJsonObject &containerConfig, const DockerContainer container) {
    for (Proto protocol: ContainerProps::protocolsForContainer(container)) {
        QString protocolConfig = containerConfig.value(ProtocolProps::protoToString(protocol))
                                         .toObject()
                                         .value(config_key::last_config)
                                         .toString();

        if (protocolConfig.isEmpty()) {
            return false;
        }
    }
    return true;
}

void ConnectionController::continueConnection() {
    int serverIndex = m_serversModel->getDefaultServerIndex();
    QJsonObject serverConfig = m_serversModel->getServerConfig(serverIndex);
    auto configVersion = serverConfig.value(config_key::configVersion).toInt();

    if (!m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        emit noInstalledContainers();
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Disconnected);
        return;
    }

    DockerContainer container = qvariant_cast<DockerContainer>(
            m_serversModel->data(serverIndex, ServersModel::Roles::DefaultContainerRole));

    if (!m_containersModel->isSupportedByCurrentPlatform(container)) {
        emit connectionErrorOccurred(tr("The selected protocol is not supported on the current platform"));
        return;
    }

    if (container == DockerContainer::None) {
        emit connectionErrorOccurred(tr("VPN Protocols is not installed.\n Please install VPN container at first"));
        return;
    }

    QSharedPointer<ServerController> serverController(new ServerController(m_settings));
    VpnConfigurationsController vpnConfigurationController(m_settings, serverController);

    QJsonObject containerConfig = m_containersModel->getContainerConfig(container);
    ServerCredentials credentials = m_serversModel->getServerCredentials(serverIndex);
    ErrorCode errorCode = updateProtocolConfig(container, credentials, containerConfig, serverController);
    if (errorCode != ErrorCode::NoError) {
        emit connectionErrorOccurred(errorCode);
        return;
    }

    auto dns = m_serversModel->getDnsPair(serverIndex);

    auto vpnConfiguration =
            vpnConfigurationController.createVpnConfiguration(dns, serverConfig, containerConfig, container, errorCode);
    if (errorCode != ErrorCode::NoError) {
        emit connectionErrorOccurred(tr("unable to create configuration"));
        return;
    }

    emit connectToVpn(serverIndex, credentials, container, vpnConfiguration);
}

ErrorCode ConnectionController::updateProtocolConfig(const DockerContainer container,
                                                     const ServerCredentials &credentials, QJsonObject &containerConfig,
                                                     QSharedPointer<ServerController> serverController) {
    QFutureWatcher<ErrorCode> watcher;

    if (serverController.isNull()) {
        serverController.reset(new ServerController(m_settings));
    }

    QFuture<ErrorCode> future =
            QtConcurrent::run([this, container, &credentials, &containerConfig, &serverController]() {
                ErrorCode errorCode = ErrorCode::NoError;
                if (!isProtocolConfigExists(containerConfig, container)) {
                    VpnConfigurationsController vpnConfigurationController(m_settings, serverController);
                    errorCode = vpnConfigurationController.createProtocolConfigForContainer(credentials, container,
                                                                                            containerConfig);
                    if (errorCode != ErrorCode::NoError) {
                        return errorCode;
                    }
                    m_serversModel->updateContainerConfig(container, containerConfig);

                    errorCode = m_clientManagementModel->appendClient(
                            container, credentials, containerConfig,
                            QString("Admin [%1]").arg(QSysInfo::prettyProductName()), serverController);
                    if (errorCode != ErrorCode::NoError) {
                        return errorCode;
                    }
                }
                return errorCode;
            });

    QEventLoop wait;
    connect(&watcher, &QFutureWatcher<ErrorCode>::finished, &wait, &QEventLoop::quit);
    watcher.setFuture(future);
    wait.exec();

    return watcher.result();
}
