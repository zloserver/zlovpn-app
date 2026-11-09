#include "splitTunnelPolicy.h"

namespace SplitTunnelPolicy {

    const QStringList &localNetworks() {
        static const QStringList networks = localUnicastNetworks() + QStringList{
                "224.0.0.0/4",
                "255.255.255.255/32",
        };
        return networks;
    }

    const QStringList &localUnicastNetworks() {
        static const QStringList networks = {
                "10.0.0.0/8",
                "172.16.0.0/12",
                "192.168.0.0/16",
                "169.254.0.0/16",
        };
        return networks;
    }

    const QStringList &localDomains() {
        // lan: routers (OpenWrt, Keenetic...); home.arpa: RFC 8375; home: ISP routers, ICANN will never delegate it;
        // internal: reserved by ICANN for private use
        static const QStringList domains = {"lan", "home.arpa", "home", "internal"};
        return domains;
    }

    QList<QHostAddress> localDnsServers(const QList<QHostAddress> &servers) {
        QList<QHostAddress> local;
        for (const QHostAddress &server : servers) {
            if (server.protocol() != QAbstractSocket::IPv4Protocol || local.contains(server))
                continue;
            for (const QString &network : localUnicastNetworks()) {
                if (server.isInSubnet(QHostAddress::parseSubnet(network))) {
                    local.append(server);
                    break;
                }
            }
        }
        return local;
    }

} // namespace SplitTunnelPolicy
