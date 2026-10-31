#include "apiDiscovery.h"

#include <Cactus/Cactus.hpp>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "amnezia_application.h"
#include "settings.h"

namespace {
    constexpr int PROBE_TIMEOUT_MS = 5000;
}

ApiDiscovery::ApiDiscovery(std::shared_ptr<Settings> settings, QObject *parent) :
    QObject{parent}, m_settings(settings) {
    watchNetwork();
}

void ApiDiscovery::start() {
    if (m_state == State::Searching)
        return;
    setState(State::Searching);

#ifdef ZLOVPN_DEV_BUILD
    // Local testing against dev-setup/fake-api.py: the API address comes from the environment instead of Cactus
    const QString devApiUrl = qEnvironmentVariable("ZLOVPN_DEV_API_URL");
    if (!devApiUrl.isEmpty()) {
        probe(devApiUrl, [this, devApiUrl](bool ok) { ok ? finish(devApiUrl) : fail(); });
        return;
    }
#endif

    ICactus::GetInstance().Spike(
            this, [this](QString baseUrl) { finish(baseUrl); }, [this]() { tryLastFoundUrl(); });
}

void ApiDiscovery::tryLastFoundUrl() {
    // Cactus gave up; the address that worked last time may still answer
    const QString lastFound = m_settings->getSpike();
    if (lastFound.isEmpty()) {
        fail();
        return;
    }
    probe(lastFound, [this, lastFound](bool ok) { ok ? finish(lastFound) : fail(); });
}

void ApiDiscovery::probe(const QString &baseUrl, std::function<void(bool ok)> done) {
    QNetworkRequest request(QUrl(baseUrl + "/api/status"));
    request.setRawHeader("User-Agent", "ZloVpn");
    request.setTransferTimeout(PROBE_TIMEOUT_MS);

    QNetworkReply *reply = amnApp->manager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError);
    });
}

void ApiDiscovery::finish(const QString &baseUrl) {
    m_settings->setSpike(baseUrl);
    m_baseUrl = baseUrl;
    setState(State::Found);
    emit found(baseUrl);
}

void ApiDiscovery::fail() {
    setState(State::Failed);
    emit failed();
}

void ApiDiscovery::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void ApiDiscovery::watchNetwork() {
    // search again by itself when the network comes back instead of waiting for "Try again"
    if (!QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability))
        return;

    connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability reachability) {
                if (m_state == State::Failed && reachability == QNetworkInformation::Reachability::Online)
                    start();
            });
}
