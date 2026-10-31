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

} // namespace SplitTunnelPolicy
