#include "authController.h"

#include "core/api/apiDiscovery.h"
#include "core/server_defs.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <logger.h>

#include <endpoints.h>

#ifdef Q_OS_IOS
#include <AmneziaVPN-Swift.h>
#include <StoreKitCallbacks.h>
#endif

#include "amnezia_application.h"

namespace {
    // docs: design.md, sections 4 and 5
    constexpr int API_TIMEOUT_MS = 20000;
    constexpr int TOKEN_REFRESH_INTERVAL_DAYS = 7;
}

static QString localizeDaysLeft(qint64 days) {
    qint64 lastDigit = days % 10;
    qint64 secondToLast = (days / 10) % 10;

    if (lastDigit == 1 && secondToLast != 1)
        return QCoreApplication::translate("DaysLocale", "%1 day left");
    if ((lastDigit == 2 || lastDigit == 3 || lastDigit == 4) && secondToLast != 1)
        return QCoreApplication::translate("DaysLocale234", "%1 days left");
    return QCoreApplication::translate("DaysLocale", "%1 days left");
}

static QString localizeHoursLeft(qint64 hours) {
    qint64 lastDigit = hours % 10;
    qint64 secondToLast = (hours / 10) % 10;

    if (lastDigit == 1 && secondToLast != 1)
        QCoreApplication::translate("DaysLocale", "%1 hour left");
    if ((lastDigit == 2 || lastDigit == 3 || lastDigit == 4) && secondToLast != 1)
        return QCoreApplication::translate("DaysLocale234", "%1 hours left");
    return QCoreApplication::translate("DaysLocale", "%1 hours left");
}

QString UserInfo::localizeTimeLeft() const {
    if (this->timeLeft == -1)
        return QString(localizeDaysLeft(100)).arg("ඞඞඞ");

    double hoursLeft = static_cast<double>(this->timeLeft) / 3600000;
    qint64 daysLeft = 0;
    if (hoursLeft >= 24) {
        daysLeft = hoursLeft / 24;
    }

    if (daysLeft > 0)
        return QString(localizeDaysLeft(daysLeft)).arg(QString::number(daysLeft));
    if (hoursLeft < 1 && hoursLeft > 0)
        return QCoreApplication::translate("DaysLocale", "less than an hour left");

    qint64 roundedHoursLeft = qRound(hoursLeft);
    return QString(localizeHoursLeft(roundedHoursLeft)).arg(QString::number(roundedHoursLeft));
}

qint64 UserInfo::monthsAvailableToAdd() const {
    if (this->timeLeft == -1)
        return 0;

    double hoursLeft = static_cast<double>(this->timeLeft) / 3600000;
    double daysLeft = 0;
    if (hoursLeft >= 24) {
        daysLeft = hoursLeft / 24;
    }

    qint64 monthsLeft = qCeil(daysLeft / 30);
    if (monthsLeft >= 6)
        return 0;
    return qMin(6 - monthsLeft, 6);
}

AuthController::AuthController(QSharedPointer<VpnConnection> vpnConnection, std::shared_ptr<Settings> settings,
                               QObject *parent) :
    QObject{parent}, m_vpnConnection(vpnConnection), m_settings(settings), m_configCache(settings) {
    connect(m_settings.get(), &Settings::settingsCleared, this, [this]() {
        m_token = "";
        m_authenticated = false;
    });

    connect(this, &AuthController::errorOccurred, this, [this](Errors errors) {
        QVariantMap map{};
        for (auto [key, value]: errors.fieldErrors.asKeyValueRange()) {
            map.insert(key, value);
        }

        emit this->errorOccurredQml(errors.errorMessage, map);
    });

    connect(this, &AuthController::apiCompatibilityChanged, this, [this]() {
        if (!m_updateRequired)
            startSession();
    });
        
    connect(this, &AuthController::notifyTransactionForwarder, this, &AuthController::notifyTransaction);

    connect(m_vpnConnection.get(), &VpnConnection::connectionStateChanged, this,
            [this](Vpn::ConnectionState state, bool getLastError) {
                m_connected = state == Vpn::ConnectionState::Connected;
            });

    m_token = m_settings->getUserToken();

    m_discovery = new ApiDiscovery(m_settings, this);
    connect(m_discovery, &ApiDiscovery::stateChanged, this, [this]() {
        if (m_discovery->state() == ApiDiscovery::State::Searching && m_spikeErrored) {
            m_spikeErrored = false;
            emit spikeUpdated();
        }
    });
    connect(m_discovery, &ApiDiscovery::found, this, [this](const QString &baseUrl) {
        leaveOfflineMode();
        m_spike = baseUrl;
        m_spikeErrored = false;
        emit spikeUpdated();
        checkApiCompatibility();
    });
    connect(m_discovery, &ApiDiscovery::failed, this, [this]() {
        if (m_vpnConnection->connectionState() == Vpn::ConnectionState::Connected || m_connected) {
            // the VPN already works (e.g. Android restarted the UI): show it with what we know instead of an error
            if (hasCachedServers()) {
                enterOfflineMode();
            } else {
                loadCachedSpike();
            }
        } else {
            m_spikeErrored = true;
            emit spikeUpdated();
            emit spikeErrorOccurred();
        }
    });

    findServer();
}

void AuthController::findServer() { m_discovery->start(); }

void AuthController::loadCachedSpike() {
    m_spike = m_settings->getSpike();
    m_spikeErrored = false;
    emit spikeUpdated();
}

bool AuthController::isAuthenticated() { return m_authenticated; }

bool AuthController::hasToken() { return !getToken().isEmpty(); }

QString AuthController::getToken() { return m_token; }

void AuthController::setToken(const QString &token) {
    m_token = token;
    m_settings->setUserToken(token);

    bool previousState = m_authenticated;
    m_authenticated = !m_token.isEmpty();
    bool authenticationStateChanged = previousState != m_authenticated;

    if (m_authenticated) {
        refreshUserInfo();
        refreshServers();
        
#ifdef Q_OS_IOS
        if (!m_storeKitListening) {
            ZloVPN::createStoreKitListener();
            m_storeKitListening = true;
        }
#endif
    }

    emit tokenUpdated(authenticationStateChanged);
}

void AuthController::setUnauthenticated() {
    m_refreshingToken = false;
    m_authenticated = false;
    m_settings->setUserTokenRefreshedAt(QDateTime());
    // saved configs hold this account's VPN keys
    m_configCache.clear();
    leaveOfflineMode();
    setToken("");
}

void AuthController::startSession() {
    if (!hasToken()) {
        setUnauthenticated();
        return;
    }

    // Every refresh replaces the token on the server, and a lost answer would leave us with a deleted one,
    // so refresh only when the token is getting old: it lives 30 days on the server
    const QDateTime refreshedAt = m_settings->getUserTokenRefreshedAt();
    if (refreshedAt.isValid() && refreshedAt.addDays(TOKEN_REFRESH_INTERVAL_DAYS) > QDateTime::currentDateTimeUtc()) {
        setToken(m_token);
        return;
    }

    refreshToken();
}

void AuthController::refreshToken() {
    m_refreshingToken = true;

    QNetworkRequest request = createNetworkRequest(REFRESH_ENDPOINT, true);
    QNetworkReply *reply = amnApp->manager()->post(request, QByteArray());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();
        Response response = parseNetworkReply(data, *reply);
        if (!response.isOk()) {
            m_refreshingToken = false;
            if (response.errors->kind == Errors::Kind::Unauthorized) {
                setUnauthenticated();
            } else {
                // no answer or a server failure says nothing about the token: keep it and carry on,
                // a really dead token gets 401 on the next request
                qWarning() << "Token refresh failed with status" << response.statusCode << ", keeping the token";
                setToken(m_token);
            }
            emit tokenRefreshFinished();
            return;
        }

        QJsonDocument document = QJsonDocument::fromJson(data);
        QString token = document.object()["token"].toString();
        m_settings->setUserTokenRefreshedAt(QDateTime::currentDateTimeUtc());
        m_authenticated = true;
        setToken(token);

        emit loginSuccessfull();

        m_refreshingToken = false;
        emit tokenRefreshFinished();
    });
}

void AuthController::login(const QString &login, const QString &password) {
    QJsonObject body{};
    body["login"] = login;
    body["password"] = password;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(LOGIN_ENDPOINT, false, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QString token = document.object()["token"].toString();
            m_settings->setUserTokenRefreshedAt(QDateTime::currentDateTimeUtc());
            setToken(token);
            m_authenticated = true;

            emit loginSuccessfull();
        });
    });
}

void AuthController::recoverAccount(const QString &email) {
    QJsonObject body{};
    body["email"] = email;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(RECOVERY_ENDPOINT, false, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                emit errorOccurred(*response.errors);
                return;
            }

            emit recoveryEmailSent();
        });
    });
}

void AuthController::changePassword(const QString &currentPassword, const QString &newPassword) {
    QJsonObject body{};
    body["currentPassword"] = currentPassword;
    body["newPassword"] = newPassword;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(PASSWORD_CHANGE_ENDPOINT, true, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                emit errorOccurred(*response.errors);
                return;
            }

            emit passwordChanged();
        });
    });
}

void AuthController::changeEmail(const QString &newEmail) {
    QJsonObject body{};
    body["newEmail"] = newEmail;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(EMAIL_CHANGE_ENDPOINT, true, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            emit emailChanged();
        });
    });
}

void AuthController::activatePromocode(const QString &promocode) {
    QJsonObject body{};
    body["code"] = promocode;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(ACTIVATE_PROMOCODE_ENDPOINT, true, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            refreshUserInfo();
            emit promocodeActivated();
        });
    });
}

void AuthController::checkApiCompatibility() {
    runNetworkRequest([this]() {
        QNetworkRequest request = createNetworkRequest(API_COMPAT_ENDPOINT, false);
        QNetworkReply *reply = amnApp->manager()->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);

            if (!response.isOk()) {
                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QJsonObject object = document.object();

            uint32_t breakingHash = object["breakingHash"].toInteger();
            int minVersion = object["minVersion"].toInt();
            qDebug() << "breakingHash: " << breakingHash << " minVeresion: " << minVersion;

            if (minVersion != API_VERSION || breakingHash != BREAKING_HASH) {
                m_updateRequired = true;
            } else {
                m_updateRequired = false;
            }
            emit apiCompatibilityChanged();
        });
    });
}

void AuthController::registerUser(const QString &email, const QString &username, const QString &password) {
    QJsonObject body{};
    body["email"] = email;
    body["username"] = username;
    body["password"] = password;

    runNetworkRequest([this, body]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(REGISTER_ENDPOINT, false, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply, true);
            if (!response.isOk()) {
                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QString token = document.object()["token"].toString();
            m_settings->setUserTokenRefreshedAt(QDateTime::currentDateTimeUtc());
            setToken(token);
            m_authenticated = true;

            emit registerSuccessfull();
        });
    });
}

void AuthController::logout() { setUnauthenticated(); }

void AuthController::refreshUserInfo() {
    runNetworkRequest([this]() {
        QNetworkRequest request = createNetworkRequest(ME_ENDPOINT, true);
        QNetworkReply *reply = amnApp->manager()->get(request);

        connect(reply, &QNetworkReply::finished, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);

            QJsonObject object = document.object();
            QJsonObject userObject = object["user"].toObject();

            UserInfo info{};
            info.username = userObject["username"].toString();
            info.timeLeft = userObject["timeLeft"].toInteger();
            info.email = userObject["email"].toString();
            info.userId = userObject.contains("id") ? userObject["id"].toInteger() : -1;
            info.isValid = true;

            m_userInfo = info;
            if (info.userId != -1)
                m_configCache.setOwner(info.userId);
            m_configCache.saveTimeLeft(info.timeLeft);

            emit userInfoUpdated();
        });
    });
}

static RegionInfo parseRegionInfo(QJsonObject obj) {
    return RegionInfo{.id = obj["id"].toString(),
                      .countryCode = obj["countryCode"].toString(),
                      .countryName = obj["countryName"].toString()};
}

void AuthController::refreshServers() {
    runNetworkRequest([this]() {
        QNetworkRequest request = createNetworkRequest(SERVERS_ENDPOINT, true);
        QNetworkReply *reply = amnApp->manager()->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QJsonArray serverArray = document.array();

            QList<RegionInfo> servers{};

            for (QJsonValue value: serverArray) {
                QJsonObject serverObject = value.toObject();

                RegionInfo info = parseRegionInfo(serverObject);
                servers.append(info);
            }

            m_regions = servers;
            m_configCache.saveRegions(serverArray);

            emit regionsUpdated();
        });
    });
}

void AuthController::enterOfflineMode() {
    if (!hasCachedServers())
        return;

    // only regions we can actually connect to without the API
    QList<RegionInfo> regions{};
    for (const QJsonValue &value : m_configCache.regions()) {
        RegionInfo region = parseRegionInfo(value.toObject());
        if (!m_configCache.servers(region.id).isEmpty())
            regions.append(region);
    }
    if (regions.isEmpty())
        return;

    bool selectedRegionKnown = false;
    for (const RegionInfo &region : regions)
        selectedRegionKnown |= region.id == m_settings->getSelectedRegionId();
    if (!selectedRegionKnown)
        m_settings->setSelectedRegionId(regions.first().id);

    m_userInfo = UserInfo{};
    if (std::optional<qint64> timeLeft = m_configCache.estimatedTimeLeft()) {
        m_userInfo.timeLeft = *timeLeft;
        m_userInfo.isValid = true;
    }
    m_regions = regions;
    m_offlineMode = true;

    qInfo() << "Offline mode: the API can't be reached, using" << regions.size() << "saved regions";
    emit offlineModeChanged();
    emit userInfoUpdated();
    emit regionsUpdated();
}

void AuthController::leaveOfflineMode() {
    if (!m_offlineMode)
        return;
    m_offlineMode = false;
    emit offlineModeChanged();
}

QString AuthController::offlineDataDate() const {
    QDateTime fetchedAt = m_configCache.timeLeftFetchedAt();
    return fetchedAt.isValid() ? fetchedAt.toLocalTime().toString("dd.MM") : QString();
}

void AuthController::getServerConnectionString(const QString &serverId, ServerStringRequest *stringRequest) {
    QJsonObject body{};
    body["port"] = 10833;

    runNetworkRequest([this, body, serverId, stringRequest]() {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();

        QNetworkRequest request = createNetworkRequest(CONNECTION_STRING_ENDPOINT + "/" + serverId, true, &bytes);
        QNetworkReply *reply = amnApp->manager()->post(request, bytes);

        connect(reply, &QNetworkReply::finished, stringRequest, [this, reply, stringRequest]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit stringRequest->errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QString connectionUrl = document.object()["url"].toString();
            emit stringRequest->stringArrived(connectionUrl);
        });
    });
}

void AuthController::openPaymentLink() {
    runNetworkRequest([this]() {
        QNetworkRequest request = createNetworkRequest(PAYMENT_ENDPOINT, true);
        QNetworkReply *reply = amnApp->manager()->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QString paymentUrl = document.object()["url"].toString();
            if (!QDesktopServices::openUrl(m_spike + paymentUrl)) {
                Errors errors{};
                errors.errorMessage = tr("Failed to open payment page");
                emit errorOccurred(errors);
            } else {
                emit paymentLinkOpened();
            }
        });
    });
}

bool AuthController::isSpikeReady() { return !m_spike.isEmpty(); }

QString AuthController::getSpikeUrl() { return m_spike; }

void AuthController::openAccountSettings() {
    runNetworkRequest([this]() {
        QNetworkRequest request = createNetworkRequest(MOBILE_WEB_TOKEN, true, nullptr);
        QNetworkReply *reply = amnApp->manager()->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }

                emit errorOccurred(*response.errors);
                return;
            }

            QJsonDocument document = QJsonDocument::fromJson(data);
            QString url = document.object()["url"].toString();
            if (!QDesktopServices::openUrl(url)) {
                Errors errors{};
                errors.errorMessage = tr("Failed to open account settings");
                emit errorOccurred(errors);
            } else {
                emit accountSettingsOpened();
            }
        });
    });
}

void AuthController::appleBuyMonth() {
#ifdef Q_OS_IOS
    if (m_userInfo.userId == -1) {
        refreshUserInfo();
        
        connect(this, &AuthController::userInfoUpdated, this, [this]() {
            if (m_userInfo.userId == -1) {
                Errors errors{};
                errors.errorMessage = tr("Failed to initiate payment");
                emit errorOccurred(errors);
            } else {
                runAppleBuyMonth(m_userInfo.userId);
            }
        }, Qt::ConnectionType::SingleShotConnection);
    } else {
        runAppleBuyMonth(m_userInfo.userId);
    }
#endif
}

void AuthController::notifyTransaction(const QString &signedPayload, NotifyTransactionRequest* transactionRequest) {
#ifdef Q_OS_IOS
    QJsonObject body{};
    body["signedPayload"] = signedPayload;

    runNetworkRequest([this, body, transactionRequest] {
        QJsonDocument doc{body};
        QByteArray bytes = doc.toJson();
        
        QNetworkRequest request = createNetworkRequest(APPLE_TRANSACTION_ENDPOINT, true, &bytes);
        QNetworkReply* reply = amnApp->manager()->post(request, bytes);
        
        connect(reply, &QNetworkReply::finished, this, [this, reply, transactionRequest]() {
            QByteArray data = reply->readAll();
            reply->deleteLater();
            Response response = parseNetworkReply(data, *reply);
            
            if (!response.isOk()) {
                if (response.statusCode == 401) {
                    setUnauthenticated();
                }
                
                emit transactionRequest->errorOccurred(*response.errors);
                return;
            }
            
            emit transactionRequest->complete();
        });
    });
#endif
}

#ifdef Q_OS_IOS
using CompletionCallback = void(void*, bool);

static void runAppleBuyMonthCallback(AuthController* self, bool result, const char* signedPayload, void* userData, CompletionCallback* callback) {
    if (!result) {
        Errors errors{};
        errors.errorMessage = self->tr("Failed to complete payment");
        emit self->errorOccurred(errors);
        return;
    } else {
        NotifyTransactionRequest* request = new NotifyTransactionRequest();
        
        request->connect(request, &NotifyTransactionRequest::complete, self, [self, userData, callback, request]() {
            emit self->appleMonthBought();
            if (callback) callback(userData, true);
            self->refreshUserInfo();
            delete request;
        });
        
        request->connect(request, &NotifyTransactionRequest::errorOccurred, self, [self, userData, callback, request](const Errors errors) {
            emit self->errorOccurred(errors);
            if (callback) callback(userData, false);
            delete request;
        });
        
        emit self->notifyTransactionForwarder(QString::fromUtf8(signedPayload), request);
    }
}
#endif

void AuthController::runAppleBuyMonth(qint64 userId) {
#ifdef Q_OS_IOS
    ZloVPN::purchaseMonth(userId, this, reinterpret_cast<void*>(&runAppleBuyMonthCallback));
#endif
}


QNetworkRequest AuthController::createNetworkRequest(const QString &endpoint, bool needsAuthorization,
                                                     const QByteArray *array) {
    QNetworkRequest request(QUrl(m_spike + endpoint));
    request.setRawHeader("User-Agent", "ZloVpn");
    request.setTransferTimeout(API_TIMEOUT_MS);

    if (needsAuthorization) {
        request.setRawHeader("Authorization", QString("Bearer " + m_token).toUtf8());
    }

    if (array) {
        request.setRawHeader("Content-Type", "application/json");
        request.setRawHeader("Content-Length", QByteArray::number(array->size()));
    }

    return request;
}

void AuthController::runNetworkRequest(std::function<void()> run) {
    if (m_refreshingToken) {
        connect(
                this, &AuthController::tokenRefreshFinished, this, [this, run]() { run(); },
                Qt::ConnectionType::SingleShotConnection);
    } else {
        run();
    }
}

Response AuthController::parseNetworkReply(QByteArray &data, QNetworkReply &reply, bool formError) {
    auto httpStatus = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply.error() != QNetworkReply::NoError || !Response::isOk(httpStatus)) {
        // a transport error after a 2xx status line still means the answer never fully arrived
        auto errors = ErrorParser::parse(data, Response::isOk(httpStatus) ? 0 : httpStatus, formError);
        return Response{.statusCode = httpStatus, .errors = errors};
    }

    return Response{.statusCode = httpStatus, .errors = std::nullopt};
}

#ifdef Q_OS_IOS
void notifyTransaction(std::string signedPayload, void* userData, NotifyTransactionCallback* callback) {
    NotifyTransactionRequest* request = new NotifyTransactionRequest();
    
    request->connect(request, &NotifyTransactionRequest::complete, [userData, callback, request]() {
        if (callback) callback(userData, true);
        delete request;
    });
    
    request->connect(request, &NotifyTransactionRequest::errorOccurred, [userData, callback, request]() {
        if (callback) callback(userData, false);
        delete request;
    });
    
    amnApp->m_authController->notifyTransaction(QString::fromStdString(signedPayload), request);
}
#endif
