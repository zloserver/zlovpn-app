#ifndef SPLITTUNNELPOLICY_H
#define SPLITTUNNELPOLICY_H

#include <QStringList>

// "Exclude local networks" (docs: design.md, section 8). The networks are merged into the "all except" split
// tunneling of the connection config, so every platform takes them from there.
namespace SplitTunnelPolicy {

    // Private, link-local, multicast and broadcast IPv4 networks: the router, printers, NAS, mDNS/SSDP discovery.
    // The same list the macOS kill switch lets through (deploy/data/macos/Resources/pf/amn.300.allowLAN.conf).
    // IPv6 needs nothing: the tunnels either carry only global addresses or block IPv6 altogether.
    const QStringList &localNetworks();

    // Unicast part of localNetworks(), for routes through the default gateway on desktop
    // (multicast and broadcast are not routed through a gateway)
    const QStringList &localUnicastNetworks();

} // namespace SplitTunnelPolicy

#endif // SPLITTUNNELPOLICY_H
