#ifndef AUTHCONTROLLER_H
#define AUTHCONTROLLER_H

#include <QNetworkAccessManager>
#include <QObject>
#include <QSharedPointer>
#include <QMutex>
#include <optional>
#include "core/configCache.h"
#include "errorParser.h"
#include "settings.h"
#include "vpnconnection.h"

class ApiDiscovery;

struct Response {
    int statusCode;
    std::optional<Errors> errors{};

    static bool isOk(int statusCode) { return (statusCode / 100) == 2; }
    [[nodiscard]] bool isOk() const { return isOk(statusCode) && !errors.has_value(); }
};

struct UserInfo {
    Q_GADGET

    Q_PROPERTY(QString username MEMBER username)
    Q_PROPERTY(QString email MEMBER email)
    Q_PROPERTY(qint64 timeLeft MEMBER timeLeft)
    Q_PROPERTY(QString localizedTimeLeft READ localizeTimeLeft)
    Q_PROPERTY(bool isValid MEMBER isValid)
    Q_PROPERTY(qint64 userId MEMBER userId)

public slots:
    QString localizeTimeLeft() const;
    qint64 monthsAvailableToAdd() const;

public:
    bool operator==(const UserInfo &other) const { return username == other.username && timeLeft == other.timeLeft; }

    bool operator!=(const UserInfo &other) const { return !(*this == other); }

    QString username;
    QString email;
    qint64 timeLeft;
    qint64 userId;
    bool isValid{false};
};

struct RegionInfo {
    Q_GADGET

    Q_PROPERTY(QString id MEMBER id)
    Q_PROPERTY(QString countryCode MEMBER countryCode)
    Q_PROPERTY(QString countryName MEMBER countryName)

public:
    bool operator==(const RegionInfo &other) const {
        return id == other.id && countryCode == other.countryCode && countryName == other.countryName;
    }

    bool operator!=(const RegionInfo &other) const { return !(*this == other); }

    QString id;
    QString countryCode;
    QString countryName;
};

class ServerStringRequest : public QObject {
    Q_OBJECT

public:
    explicit ServerStringRequest(QObject *parent = nullptr) : QObject{parent} {};

signals:
    void stringArrived(const QString connectionString);
    void errorOccurred(const Errors errors);
};

class NotifyTransactionRequest : public QObject {
    Q_OBJECT
    
public:
    explicit NotifyTransactionRequest(QObject* parent = nullptr) : QObject{parent} {};
    
signals:
    void complete();
    void errorOccurred(const Errors errors);
};

class AuthController : public QObject {
    Q_OBJECT
    Q_PROPERTY(UserInfo userInfo MEMBER m_userInfo NOTIFY userInfoUpdated);
    Q_PROPERTY(QList<RegionInfo> regionInfo MEMBER m_regions NOTIFY regionsUpdated);
    Q_PROPERTY(bool spikeReady READ isSpikeReady NOTIFY spikeUpdated);
    Q_PROPERTY(bool spikeErrored MEMBER m_spikeErrored NOTIFY spikeUpdated);
    Q_PROPERTY(bool updateRequired MEMBER m_updateRequired NOTIFY apiCompatibilityChanged);
    // the API can't be reached and the app runs on ConfigCache (docs: design.md, section 7)
    Q_PROPERTY(bool offlineMode READ isOfflineMode NOTIFY offlineModeChanged);
    Q_PROPERTY(QString offlineDataDate READ offlineDataDate NOTIFY offlineModeChanged);

public:
    explicit AuthController(QSharedPointer<VpnConnection> vpnConnection, std::shared_ptr<Settings> settings,
                            QObject *parent = nullptr);

    const QList<RegionInfo> &getRegions() const { return m_regions; }
    ConfigCache &configCache() { return m_configCache; }

public slots:
    bool isAuthenticated();
    bool hasToken();
    QString getToken();
    void setToken(const QString &token);
    void setUnauthenticated();

    void refreshToken();
    void login(const QString &login, const QString &password);
    void registerUser(const QString &email, const QString &username, const QString &password);
    void recoverAccount(const QString &email);
    void changePassword(const QString &currentPassword, const QString &newPassword);
    void changeEmail(const QString &newEmail);
    void activatePromocode(const QString &promocode);
    void checkApiCompatibility();
    void findServer();
    void logout();

    bool isOfflineMode() const { return m_offlineMode; }
    bool hasCachedServers() const { return m_configCache.hasServers(); }
    // home screen with saved regions and configs while the API can't be reached
    void enterOfflineMode();
    QString offlineDataDate() const;

    void refreshUserInfo();
    void refreshServers();

    void getServerConnectionString(const QString &serverId, ServerStringRequest *stringRequest);
    void openPaymentLink();

    bool isSpikeReady();
    QString getSpikeUrl();
  
    void openAccountSettings();

    void appleBuyMonth();

    void notifyTransaction(const QString& signedPayload, NotifyTransactionRequest* request);

signals:
    void apiCompatibilityChanged();
    void spikeErrorOccurred();
    void errorOccurred(const Errors errors);
    void errorOccurredQml(const QString errorMessage, const QVariantMap fieldErrors);
    void tokenUpdated(bool authenticationStateChanged);
    void loginSuccessfull();
    void registerSuccessfull();
    void recoveryEmailSent();
    void passwordChanged();
    void emailChanged();
    void spikeUpdated();
    void offlineModeChanged();

    void userInfoUpdated();
    void regionsUpdated();
    void paymentLinkOpened();

    void promocodeActivated();

    void tokenRefreshFinished();
  
    void accountSettingsOpened();
    
    void appleMonthBought();
    
    void notifyTransactionForwarder(const QString& signedPayload, NotifyTransactionRequest* request);

private:
    QNetworkRequest createNetworkRequest(const QString &endpoint, bool needsAuthorization = false,
                                         const QByteArray *array = nullptr);
    Response parseNetworkReply(QByteArray &data, QNetworkReply &reply, bool formError = false);

    void loadCachedSpike();
    // after the API check: use the stored token, refreshing it if it is getting old
    void startSession();
    void leaveOfflineMode();

    void runNetworkRequest(std::function<void()> run);
    
    void runAppleBuyMonth(qint64 userId);

    std::shared_ptr<Settings> m_settings;

    QSharedPointer<VpnConnection> m_vpnConnection;

    ApiDiscovery *m_discovery{};
    ConfigCache m_configCache;
    bool m_offlineMode{};

    bool m_refreshingToken{};

    QString m_token{};
    UserInfo m_userInfo{};
    QList<RegionInfo> m_regions{};
    QString m_spike{};
    bool m_authenticated{};
    bool m_spikeErrored{};
    bool m_updateRequired{};
    bool m_connected{};
    
#ifdef Q_OS_IOS
    bool m_storeKitListening{};
#endif
};

#endif // AUTHCONTROLLER_H
