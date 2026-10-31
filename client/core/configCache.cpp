#include "configCache.h"

#include <QJsonDocument>
#include <QSet>

#include "settings.h"

namespace {
    constexpr int CACHE_VERSION = 1;
    constexpr int SERVERS_PER_REGION = 3;
    // a server may change its keys, so very old configs are not worth trying
    constexpr int SERVER_TTL_DAYS = 30;

    // outbound protocols the bundled Xray core can run
    const QSet<QString> SUPPORTED_XRAY_PROTOCOLS = {
        "vless", "vmess", "trojan", "shadowsocks", "socks", "http", "freedom", "blackhole", "dns",
    };
}

ConfigCache::ConfigCache(std::shared_ptr<Settings> settings) : m_settings(settings) {}

QJsonObject ConfigCache::load() const {
    QJsonObject cache = QJsonDocument::fromJson(m_settings->getConfigCache()).object();
    if (cache.value("version").toInt() != CACHE_VERSION)
        return {};
    return cache;
}

void ConfigCache::store(const QJsonObject &cache) {
    QJsonObject versioned = cache;
    versioned["version"] = CACHE_VERSION;
    m_settings->setConfigCache(QJsonDocument(versioned).toJson(QJsonDocument::Compact));
}

void ConfigCache::setOwner(qint64 userId) {
    QJsonObject cache = load();
    if (cache.contains("userId")) {
        if (cache.value("userId").toInteger() == userId)
            return;
        cache = {};
    }
    cache["userId"] = userId;
    store(cache);
}

void ConfigCache::saveRegions(const QJsonArray &regions) {
    QJsonObject cache = load();
    cache["regions"] = regions;
    store(cache);
}

QJsonArray ConfigCache::regions() const { return load().value("regions").toArray(); }

void ConfigCache::saveTimeLeft(qint64 timeLeftMs) {
    QJsonObject cache = load();
    cache["userInfo"] = QJsonObject{
            {"timeLeft", timeLeftMs},
            {"fetchedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    };
    store(cache);
}

std::optional<qint64> ConfigCache::estimatedTimeLeft() const {
    QJsonObject userInfo = load().value("userInfo").toObject();
    if (userInfo.isEmpty())
        return std::nullopt;

    qint64 timeLeft = userInfo.value("timeLeft").toInteger();
    if (timeLeft < 0) // unlimited
        return timeLeft;

    qint64 elapsed = timeLeftFetchedAt().msecsTo(QDateTime::currentDateTimeUtc());
    return qMax<qint64>(0, timeLeft - elapsed);
}

QDateTime ConfigCache::timeLeftFetchedAt() const {
    return QDateTime::fromString(load().value("userInfo").toObject().value("fetchedAt").toString(), Qt::ISODate);
}

void ConfigCache::rememberServer(const QString &regionId, const QString &url) {
    QJsonObject cache = load();
    QJsonObject servers = cache.value("servers").toObject();
    const QString host = hostOf(url);

    QJsonArray updated{QJsonObject{
            {"host", host},
            {"url", url},
            {"lastConnectedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    }};
    for (const QJsonValue &server : servers.value(regionId).toArray()) {
        if (updated.size() >= SERVERS_PER_REGION)
            break;
        if (server.toObject().value("host").toString() != host)
            updated.append(server);
    }

    servers[regionId] = updated;
    cache["servers"] = servers;
    store(cache);
}

QList<ConfigCache::Server> ConfigCache::servers(const QString &regionId) const {
    QList<Server> result;
    const QDateTime oldest = QDateTime::currentDateTimeUtc().addDays(-SERVER_TTL_DAYS);

    for (const QJsonValue &value : load().value("servers").toObject().value(regionId).toArray()) {
        QJsonObject server = value.toObject();
        QDateTime lastConnectedAt = QDateTime::fromString(server.value("lastConnectedAt").toString(), Qt::ISODate);
        if (!lastConnectedAt.isValid() || lastConnectedAt < oldest)
            continue;
        result.append(Server{server.value("host").toString(), server.value("url").toString(), lastConnectedAt});
    }
    return result;
}

bool ConfigCache::hasServers() const {
    const QJsonObject servers = load().value("servers").toObject();
    for (auto it = servers.constBegin(); it != servers.constEnd(); ++it) {
        if (!this->servers(it.key()).isEmpty())
            return true;
    }
    return false;
}

void ConfigCache::clear() { m_settings->setConfigCache({}); }

QJsonObject ConfigCache::decode(const QString &url) {
    if (!url.startsWith("vpn://"))
        return {};
    QByteArray packed = QByteArray::fromBase64(url.mid(6).toUtf8(),
                                               QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QJsonDocument::fromJson(qUncompress(packed)).object();
}

QString ConfigCache::hostOf(const QString &url) { return decode(url).value("hostName").toString(); }

bool ConfigCache::isSupported(const QString &url) {
    const QJsonObject config = decode(url);
    if (config.isEmpty()) // not ours to judge, the regular import reports broken strings
        return true;

    for (const QJsonValue &container : config.value("containers").toArray()) {
        const QJsonObject xray = container.toObject().value("xray").toObject();
        if (xray.isEmpty())
            continue;

        const QJsonObject lastConfig = QJsonDocument::fromJson(xray.value("last_config").toString().toUtf8()).object();
        for (const QJsonValue &outbound : lastConfig.value("outbounds").toArray()) {
            if (!SUPPORTED_XRAY_PROTOCOLS.contains(outbound.toObject().value("protocol").toString()))
                return false;
        }
    }
    return true;
}
