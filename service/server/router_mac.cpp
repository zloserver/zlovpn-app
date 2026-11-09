#include "router_mac.h"
#include "helper_route_mac.h"

#include <QProcess>
#include <QScopeGuard>
#include <QThread>

#include <core/networkUtilities.h>
#include <core/splitTunnelPolicy.h>

#include "version.h"

RouterMac &RouterMac::Instance()
{
    static RouterMac s;
    return s;
}

bool RouterMac::routeAdd(const QString &ipWithSubnet, const QString &gw)
{
    QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipWithSubnet);
    QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipWithSubnet);

#ifdef MZ_DEBUG
    qDebug().noquote() << "RouterMac::routeAdd: " << ipWithSubnet << gw;
#endif

    if (!NetworkUtilities::checkIPv4Format(ip) || !NetworkUtilities::checkIPv4Format(gw)) {
        qCritical().noquote() << "Critical, trying to add invalid route: " << ip << gw;
        return false;
    }

    QString cmd;
    if (mask == "255.255.255.255") {
        cmd = QString("route add -host %1 %2").arg(ip).arg(gw);
    }
    else {
        cmd = QString("route add -net %1 %2 %3").arg(ip).arg(gw).arg(mask);
    }

    QStringList parts = cmd.split(" ");

    int argc = parts.size();
    char **argv = new char*[argc];

    for (int i = 0; i < argc; i++) {
        argv[i] = new char[parts.at(i).toStdString().length() + 1];
        strcpy(argv[i], parts.at(i).toStdString().c_str());
    }

    // TODO refactor
    mainRouteIface(argc, argv);
    m_addedRoutes.append({ipWithSubnet, gw});

    for (int i = 0; i < argc; i++) {
        delete [] argv[i];
    }
    delete[] argv;
    return true;
}

int RouterMac::routeAddList(const QString &gw, const QStringList &ips)
{
    int cnt = 0;
    for (const QString &ip: ips) {
        if (routeAdd(ip, gw)) cnt++;
    }
    return cnt;
}

bool RouterMac::clearSavedRoutes()
{
    int cnt = 0;
    for (const Route &r: m_addedRoutes) {
        if (routeDelete(r.dst, r.gw)) cnt++;
    }
    bool ret = (cnt == m_addedRoutes.count());
    m_addedRoutes.clear();
    return ret;
}

bool RouterMac::routeDelete(const QString &ipWithSubnet, const QString &gw)
{
    QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipWithSubnet);
    QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipWithSubnet);

#ifdef MZ_DEBUG
    qDebug().noquote() << "RouterMac::routeDelete: " << ipWithSubnet << gw;
#endif

    if (!NetworkUtilities::checkIPv4Format(ip) || !NetworkUtilities::checkIPv4Format(gw)) {
        qCritical().noquote() << "Critical, trying to remove invalid route: " << ip << gw;
        return false;
    }

    if (ipWithSubnet == "0.0.0.0/0") {
        qDebug().noquote() << "Warning, trying to remove default route, skipping: " << ip << gw;
        return true;
    }

    QString cmd;
    if (mask == "255.255.255.255") {
        cmd = QString("route delete -host %1 %2").arg(ip).arg(gw);
    }
    else {
        cmd = QString("route delete -net %1 %2 %3").arg(ip).arg(gw).arg(mask);
    }

    QStringList parts = cmd.split(" ");

    int argc = parts.size();
    char **argv = new char*[argc];

    for (int i = 0; i < argc; i++) {
        argv[i] = new char[parts.at(i).toStdString().length() + 1];
        strcpy(argv[i], parts.at(i).toStdString().c_str());
    }

    mainRouteIface(argc, argv);

    for (int i = 0; i < argc; i++) {
        delete [] argv[i];
    }
    delete[] argv;
    return true;
}

bool RouterMac::routeDeleteList(const QString &gw, const QStringList &ips)
{
    int cnt = 0;
    for (const QString &ip: ips) {
        if (routeDelete(ip, gw)) cnt++;
    }
    return cnt;
}

bool RouterMac::createTun(const QString &dev, const QString &subnet) {
    qDebug().noquote() << "createTun start";

    QProcess process;
    QStringList commands;

    commands << "ifconfig" << dev << "inet" << subnet << subnet << "up";
    process.start("sudo", commands);
    if (!process.waitForStarted(1000))
    {
        qDebug().noquote() << "Could not start activate tun device!\n";
        return false;
    }
    else if (!process.waitForFinished(2000))
    {
        qDebug().noquote() << "Could not activate tun device!\n";
        return false;
    }
    commands.clear();

    return true;
}

bool RouterMac::updateResolvers(const QString& ifname, const QList<QHostAddress>& resolvers)
{
    return m_dnsUtil->updateResolvers(ifname, resolvers);
}


bool RouterMac::deleteTun(const QString &dev)
{
    qDebug().noquote() << "deleteTun start";

    return true;
}

void RouterMac::flushDns()
{
    // sudo killall -HUP mDNSResponder
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);

    p.start("killall", QStringList() << "-HUP" << "mDNSResponder");
    p.waitForFinished();
    qDebug().noquote() << "OUTPUT killall -HUP mDNSResponder: " + p.readAll();
}

namespace {

    // A "service" with nothing but a DNS entity: configd turns its match domains into supplemental resolvers
    // (scutil --dns). Accepted without IPv4, see get_dns_changes() in configd's Plugins/IPMonitor/ip_plugin.c
    QString localDomainsKey() {
        return QStringLiteral("State:/Network/Service/%1-LocalDomains/DNS").arg(QString(APPLICATION_NAME).remove(' '));
    }

    CFArrayRef createCFStringArray(const QStringList &strings) {
        CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, strings.size(), &kCFTypeArrayCallBacks);
        for (const QString &string : strings) {
            CFStringRef cfString = string.toCFString();
            CFArrayAppendValue(array, cfString);
            CFRelease(cfString);
        }
        return array;
    }

    // DNS servers of the primary service as DHCP handed them out (State:). The VPN connection replaces the DNS of
    // every service in Setup: (DnsUtilsMacos::updateResolvers), the State: ones stay intact.
    QList<QHostAddress> primaryServiceDns(SCDynamicStoreRef store, QString &interfaceName) {
        QList<QHostAddress> servers;
        auto global = static_cast<CFDictionaryRef>(SCDynamicStoreCopyValue(store, CFSTR("State:/Network/Global/IPv4")));
        if (!global)
            return servers;
        auto globalGuard = qScopeGuard([&] { CFRelease(global); });
        if (CFGetTypeID(global) != CFDictionaryGetTypeID())
            return servers;

        auto interface = static_cast<CFStringRef>(CFDictionaryGetValue(global, kSCDynamicStorePropNetPrimaryInterface));
        if (interface && CFGetTypeID(interface) == CFStringGetTypeID())
            interfaceName = QString::fromCFString(interface);
        auto serviceId = static_cast<CFStringRef>(CFDictionaryGetValue(global, kSCDynamicStorePropNetPrimaryService));
        if (!serviceId || CFGetTypeID(serviceId) != CFStringGetTypeID())
            return servers;

        CFStringRef dnsKey = SCDynamicStoreKeyCreateNetworkServiceEntity(kCFAllocatorDefault, kSCDynamicStoreDomainState,
                                                                         serviceId, kSCEntNetDNS);
        auto dns = static_cast<CFDictionaryRef>(SCDynamicStoreCopyValue(store, dnsKey));
        CFRelease(dnsKey);
        if (!dns)
            return servers;
        auto dnsGuard = qScopeGuard([&] { CFRelease(dns); });
        if (CFGetTypeID(dns) != CFDictionaryGetTypeID())
            return servers;

        auto addresses = static_cast<CFArrayRef>(CFDictionaryGetValue(dns, kSCPropNetDNSServerAddresses));
        if (!addresses || CFGetTypeID(addresses) != CFArrayGetTypeID())
            return servers;
        for (CFIndex i = 0; i < CFArrayGetCount(addresses); i++) {
            auto address = static_cast<CFStringRef>(CFArrayGetValueAtIndex(addresses, i));
            if (CFGetTypeID(address) == CFStringGetTypeID())
                servers.append(QHostAddress(QString::fromCFString(address)));
        }
        return servers;
    }

} // namespace

// A temporary key of the dynamic store: configd drops it by itself when the service quits or crashes, so unlike
// /etc/resolver files nothing is left behind
QList<QHostAddress> RouterMac::setLocalDomainsResolver(bool enabled)
{
    if (!m_localDomainsStore) {
        m_localDomainsStore = SCDynamicStoreCreate(kCFAllocatorDefault, CFSTR("ZloVPN local domains"), nullptr, nullptr);
        if (!m_localDomainsStore) {
            qCritical() << "Failed to open the dynamic store for local domains:" << SCErrorString(SCError());
            return {};
        }
    }
    CFStringRef key = localDomainsKey().toCFString();
    auto keyGuard = qScopeGuard([&] { CFRelease(key); });

    // drop the previous rule: the main interface or its DNS may have changed since
    bool changed = SCDynamicStoreRemoveValue(m_localDomainsStore, key);

    QList<QHostAddress> servers;
    if (enabled) {
        QString interfaceName;
        const QList<QHostAddress> dns = primaryServiceDns(m_localDomainsStore, interfaceName);
        servers = SplitTunnelPolicy::localDnsServers(dns);
#ifdef ZLOVPN_DEV_BUILD
        // local testing on a network whose router doesn't serve DNS
        if (qEnvironmentVariableIsSet("ZLOVPN_DEV_LOCAL_DNS"))
            servers = {QHostAddress(qEnvironmentVariable("ZLOVPN_DEV_LOCAL_DNS"))};
#endif

        QStringList serverList;
        for (const QHostAddress &server : servers)
            serverList.append(server.toString());

        if (servers.isEmpty()) {
            qInfo() << "No local DNS server on the main interface" << interfaceName << dns
                    << "- local domains stay with the tunnel DNS";
        } else {
            CFMutableDictionaryRef config = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                      &kCFTypeDictionaryValueCallBacks);
            CFArrayRef addresses = createCFStringArray(serverList);
            CFArrayRef domains = createCFStringArray(SplitTunnelPolicy::localDomains());
            const int noSearch = 1;
            CFNumberRef noSearchValue = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &noSearch);
            CFDictionarySetValue(config, kSCPropNetDNSServerAddresses, addresses);
            CFDictionarySetValue(config, kSCPropNetDNSSupplementalMatchDomains, domains);
            // keeps the domains out of the search list, or "ping nas" would also try nas.lan, nas.home.arpa...
            CFDictionarySetValue(config, CFSTR("SupplementalMatchDomainsNoSearch"), noSearchValue);

            const bool added = SCDynamicStoreAddTemporaryValue(m_localDomainsStore, key, config);
            CFRelease(noSearchValue);
            CFRelease(domains);
            CFRelease(addresses);
            CFRelease(config);

            if (added) {
                qInfo() << "Local domains" << SplitTunnelPolicy::localDomains() << "resolve via" << serverList << "of"
                        << interfaceName;
                changed = true;
            } else {
                qCritical() << "Failed to set the local domains resolver:" << SCErrorString(SCError());
                servers.clear();
            }
        }
    } else if (changed) {
        qInfo() << "Local domains resolver removed";
    }

    // the tunnel DNS may have answered "no such name" for router.lan while connecting
    if (changed)
        flushDns();
    return servers;
}
