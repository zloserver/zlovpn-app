#ifndef CONFIGCACHE_H
#define CONFIGCACHE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

class Settings;

// What the app needs to connect when the ZloVPN API can't be reached (docs: design.md, section 7):
// last known regions and balance, and configs of servers the user actually connected to.
// Kept in one encrypted settings key, belongs to one account, wiped on logout.
class ConfigCache {
public:
    struct Server {
        QString host;
        QString url;
        QDateTime lastConnectedAt;
    };

    explicit ConfigCache(std::shared_ptr<Settings> settings);

    // Another account's cache is wiped
    void setOwner(qint64 userId);

    void saveRegions(const QJsonArray &regions);
    QJsonArray regions() const;

    void saveTimeLeft(qint64 timeLeftMs);
    // Balance estimated for now from the last known one, same units as UserInfo::timeLeft (-1 is unlimited);
    // empty when the balance was never fetched
    std::optional<qint64> estimatedTimeLeft() const;
    QDateTime timeLeftFetchedAt() const;

    // Called when the VPN reached "Connected" with this config
    void rememberServer(const QString &regionId, const QString &url);
    // Most recently connected first, without entries older than 30 days
    QList<Server> servers(const QString &regionId) const;
    bool hasServers() const;

    void clear();

    // Helpers for vpn:// strings the API hands out (Amnezia format: length + zlib + base64url)
    static QJsonObject decode(const QString &url);
    static QString hostOf(const QString &url);
    // false for configs the Xray client can't run, e.g. Hysteria2 servers put into the Amnezia format
    static bool isSupported(const QString &url);

private:
    QJsonObject load() const;
    void store(const QJsonObject &cache);

    std::shared_ptr<Settings> m_settings;
};

#endif // CONFIGCACHE_H
