#ifndef APIDISCOVERY_H
#define APIDISCOVERY_H

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class Settings;

// Finds the base URL of the ZloVPN API: Cactus (the site, then the mirrors), then the address found last time.
// Searches again on request and when the system reports that the network is back.
class ApiDiscovery : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Searching, Found, Failed };
    Q_ENUM(State)

    explicit ApiDiscovery(std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    State state() const { return m_state; }
    QString baseUrl() const { return m_baseUrl; }

public slots:
    // Does nothing while a search is already running
    void start();

signals:
    void stateChanged();
    void found(const QString &baseUrl);
    void failed();

private:
    void probe(const QString &baseUrl, std::function<void(bool ok)> done);
    void tryLastFoundUrl();
    void finish(const QString &baseUrl);
    void fail();
    void setState(State state);
    void watchNetwork();

    std::shared_ptr<Settings> m_settings;
    State m_state = State::Idle;
    QString m_baseUrl;
};

#endif // APIDISCOVERY_H
