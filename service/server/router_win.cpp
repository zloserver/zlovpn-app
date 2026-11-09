#include "router_win.h"

#include <string>
#include <tchar.h>
#include <tlhelp32.h>

#include <vector>

#include <QProcess>

#include <core/networkUtilities.h>
#include <core/splitTunnelPolicy.h>

LONG(NTAPI *NtSuspendProcess)(HANDLE ProcessHandle) = NULL;
LONG(NTAPI *NtResumeProcess)(HANDLE ProcessHandle) = NULL;

#define STATUS_SUCCESS ((NTSTATUS) 0x00000000L)

RouterWin &RouterWin::Instance() {
    static RouterWin s;
    BOOL ok;
    ok = s.InitNtFunctions();
    if (!ok)
        qDebug() << "RouterWin::Instance failed to InitNtFunctions";

    ok = s.EnableDebugPrivilege();
    if (!ok)
        qDebug() << "RouterWin::Instance failed to EnableDebugPrivilege";

    return s;
}

bool GetAdapterGateway(IF_LUID adapterLuid, QString *outGateway) {
    std::vector<uint8_t> data{};
    ULONG addressesSize = 0;

    ULONG ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr,
                                     nullptr, &addressesSize);
    if (ret != ERROR_BUFFER_OVERFLOW) {
        qDebug() << "Failed to get adapter addresses";
        return false;
    }

    data.resize(addressesSize, 0);

    ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr,
                               (PIP_ADAPTER_ADDRESSES) data.data(), &addressesSize);
    if (ret != NO_ERROR) {
        qDebug() << "Failed to get adapter addresses";
        return false;
    }

    PIP_ADAPTER_ADDRESSES adapterAddresses = (PIP_ADAPTER_ADDRESSES) data.data();

    while (adapterAddresses && adapterAddresses->Next != nullptr) {
        if (adapterAddresses->Luid.Value == adapterLuid.Value) {
            if (adapterAddresses->FirstGatewayAddress != nullptr) {
                PIP_ADAPTER_GATEWAY_ADDRESS_LH gatewayAddress = adapterAddresses->FirstGatewayAddress;
                while (gatewayAddress != nullptr) {
                    if (gatewayAddress->Address.iSockaddrLength >= sizeof(SOCKADDR_IN) &&
                        gatewayAddress->Address.iSockaddrLength < sizeof(SOCKADDR_IN6)) {
                        auto sockAddr = reinterpret_cast<SOCKADDR_IN *>(gatewayAddress->Address.lpSockaddr);
                        const char *gatewayAddr = inet_ntoa(sockAddr->sin_addr);
                        if (outGateway) {
                            *outGateway = QString::fromUtf8(gatewayAddr);
                            return true;
                        }
                    }
                    gatewayAddress = gatewayAddress->Next;
                }
            }
        }
        adapterAddresses = adapterAddresses->Next;
    }

    return false;
}

int RouterWin::routeAddList(const QString &gwE, const QStringList &ips, bool autoDetectGateway) {
    //    qDebug().noquote() << QString("ROUTE ADD List: IPs size:%1, GW: %2")
    //                          .arg(ips.size())
    //                          .arg(gw);

    //    qDebug().noquote() << QString("ROUTE ADD List: IPs:\n%1")
    //                          .arg(ips.join("\n"));

    if (ips.isEmpty()) {
        qCritical() << "Tried to add a route to WAN with an empty ip list";
        return false;
    }

    QString gw = gwE;
    if (autoDetectGateway) {
        MIB_IPFORWARDROW ipfrow;
        // Set iface for route
        IPAddr dwGwAddr = inet_addr(ips.first().toStdString().c_str());
        if (GetBestInterface(dwGwAddr, &ipfrow.dwForwardIfIndex) != NO_ERROR) {
            qDebug() << "Router::routeAddList : GetBestInterface failed";
            return false;
        }

        MIB_IPINTERFACE_ROW interfaceRow{};
        interfaceRow.Family = AF_INET;
        interfaceRow.InterfaceIndex = ipfrow.dwForwardIfIndex;

        if (GetIpInterfaceEntry(&interfaceRow) != NO_ERROR) {
            qDebug() << "Router::routeAddList: GetIpInterfaceEntry failed";
            return false;
        }

        QString adapterGateway;
        if (!GetAdapterGateway(interfaceRow.InterfaceLuid, &adapterGateway)) {
            qDebug() << "Router::routeAddList: failed to get adapter gateway";
            return false;
        }

        gw = adapterGateway;
    }

    if (!NetworkUtilities::checkIPv4Format(gw)) {
        qCritical().noquote() << "Trying to add invalid route, gw: " << gw;
        return 0;
    }

    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;


    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Malloc failed. Out of memory.";
            return 0;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "getIpForwardTable failed.";
        if (pIpForwardTable)
            free(pIpForwardTable);
        return 0;
    }

    MIB_IPFORWARDROW ipfrow;
    // Set iface for route
    IPAddr dwGwAddr = inet_addr(gw.toStdString().c_str());
    if (GetBestInterface(dwGwAddr, &ipfrow.dwForwardIfIndex) != NO_ERROR) {
        qDebug() << "Router::routeAddList : GetBestInterface failed";
        return false;
    }

    int success_count = 0;

    ipfrow.dwForwardPolicy = 0;
    ipfrow.dwForwardAge = INFINITE;

    ipfrow.dwForwardNextHop = inet_addr(gw.toStdString().c_str());
    ipfrow.dwForwardType = MIB_IPROUTE_TYPE_INDIRECT; /* XXX - next hop != final dest */
    ipfrow.dwForwardProto = MIB_IPPROTO_NETMGMT; /* XXX - MIB_PROTO_NETMGMT */

    // Get TAP iface metric to set it for new routes
    MIB_IPINTERFACE_ROW tap_iface;
    InitializeIpInterfaceEntry(&tap_iface);
    tap_iface.InterfaceIndex = ipfrow.dwForwardIfIndex;
    tap_iface.Family = AF_INET;
    dwStatus = GetIpInterfaceEntry(&tap_iface);
    if (dwStatus == NO_ERROR) {
        ipfrow.dwForwardMetric1 = tap_iface.Metric + 1;
    } else {
        qDebug() << "Router::routeAddList: failed GetIpInterfaceEntry(), Error:" << dwStatus;
        ipfrow.dwForwardMetric1 = 256;
    }
    ipfrow.dwForwardMetric2 = 0;
    ipfrow.dwForwardMetric3 = 0;
    ipfrow.dwForwardMetric4 = 0;
    ipfrow.dwForwardMetric5 = 0;

    for (int i = 0; i < ips.size(); ++i) {
        QString ipWithMask = ips.at(i);
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipWithMask);

        if (!NetworkUtilities::checkIPv4Format(ip)) {
            qCritical().noquote() << "Critical, trying to add invalid route, ip: " << ip;
            continue;
        }

        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipWithMask);

        // address
        ipfrow.dwForwardDest = inet_addr(ip.toStdString().c_str());

        // mask
        in_addr maskAddr;
        inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr);
        ipfrow.dwForwardMask = maskAddr.S_un.S_addr;

        dwStatus = CreateIpForwardEntry(&ipfrow);
        if (dwStatus == NO_ERROR) {
            m_ipForwardRows.insert(ip, ipfrow);
            success_count++;
        } else if (dwStatus == ERROR_OBJECT_ALREADY_EXISTS) {
            m_ipForwardRows.insert(ip, ipfrow);
            success_count++;
            qDebug() << "Router::routeAdd: warning, route already exist:" << ip << gw;
        } else {
            qDebug() << "Router::routeAdd: failed CreateIpForwardEntry(), Error:" << ip << gw << dwStatus;
        }
    }

    // Free resources
    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::routeAddList finished, success: " << success_count << "/" << ips.size();

    if (m_ipForwardRows.size() > 500)
        suspendWcmSvc(true);

    return success_count;
}

bool RouterWin::clearSavedRoutes() {
    if (m_ipForwardRows.isEmpty())
        return true;

    qDebug() << "RouterWin::clearSavedRoutes forward rows size:" << m_ipForwardRows.size();

    // Declare and initialize variables
    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;

    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Router::clearSavedRoutes : Malloc failed. Out of memory";
            return false;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "Router::clearSavedRoutes : getIpForwardTable failed";
        if (pIpForwardTable)
            free(pIpForwardTable);

        return false;
    }

    int removed_count = 0;
    for (auto i = m_ipForwardRows.begin(); i != m_ipForwardRows.end(); ++i) {
        dwStatus = DeleteIpForwardEntry(&i.value());

        if (dwStatus != ERROR_SUCCESS) {
            qDebug() << "Router::clearSavedRoutes : Could not delete old row" << i.key();
        } else
            removed_count++;
    }

    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::clearSavedRoutes : removed routes:" << removed_count << "of" << m_ipForwardRows.size();
    m_ipForwardRows.clear();

    suspendWcmSvc(false);

    return true;
}

int RouterWin::routeDeleteList(const QString &gw, const QStringList &ips) {
    //    qDebug().noquote() << QString("ROUTE DELETE List: IPs size:%1, GW: %2")
    //                          .arg(ips.size())
    //                          .arg(gw);

    //    qDebug().noquote() << QString("ROUTE DELETE List: IPs:\n%1")
    //                          .arg(ips.join("\n"));

    PMIB_IPFORWARDTABLE pIpForwardTable = NULL;
    DWORD dwSize = 0;
    BOOL bOrder = FALSE;
    DWORD dwStatus = 0;
    ULONG gw_addr = inet_addr(gw.toStdString().c_str());

    // Find out how big our buffer needs to be.
    dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    if (dwStatus == ERROR_INSUFFICIENT_BUFFER) {
        // Allocate the memory for the table
        if (!(pIpForwardTable = (PMIB_IPFORWARDTABLE) malloc(dwSize))) {
            qDebug() << "Malloc failed. Out of memory.";
            return 0;
        }
        // Now get the table.
        dwStatus = GetIpForwardTable(pIpForwardTable, &dwSize, bOrder);
    }

    if (dwStatus != ERROR_SUCCESS) {
        qDebug() << "getIpForwardTable failed.";
        if (pIpForwardTable)
            free(pIpForwardTable);
        return 0;
    }

    int success_count = 0;

    QMap<ULONG, QPair<QString, ULONG>> ipMap;
    for (int i = 0; i < ips.size(); ++i) {
        QString ipMask = ips.at(i);
        if (ipMask.isEmpty())
            continue;
        QString ip = NetworkUtilities::ipAddressFromIpWithSubnet(ipMask);
        QString mask = NetworkUtilities::netMaskFromIpWithSubnet(ipMask);

        if (ip.isEmpty())
            continue;

        in_addr maskAddr;
        inet_pton(AF_INET, mask.toStdString().c_str(), &maskAddr);

        ipMap.insert(inet_addr(ip.toStdString().c_str()), qMakePair(ipMask, maskAddr.S_un.S_addr));
    }

    for (int i = 0; i < (int) pIpForwardTable->dwNumEntries; i++) {
        MIB_IPFORWARDROW ipfrow = pIpForwardTable->table[i];

        if (ipMap.contains(ipfrow.dwForwardDest) && ipMap.value(ipfrow.dwForwardDest).second == ipfrow.dwForwardMask &&
            ipfrow.dwForwardNextHop == gw_addr) {
            dwStatus = DeleteIpForwardEntry(&pIpForwardTable->table[i]);
            if (dwStatus == ERROR_SUCCESS) {
                m_ipForwardRows.remove(ipMap.value(ipfrow.dwForwardDest).first);
                success_count++;
            }
        }
    }

    // Free resources
    if (pIpForwardTable)
        free(pIpForwardTable);

    qDebug() << "Router::routeDeleteList finished, success: " << success_count << "/" << ips.size();

    return success_count;
}

void RouterWin::flushDns() {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);

    // Qt 6 doesn't split a command line passed to start(): "ipconfig /flushdns" was taken for the program name
    p.start(QStringLiteral("ipconfig"), QStringList() << QStringLiteral("/flushdns"));
    p.waitForFinished();
    // qDebug().noquote() << "OUTPUT ipconfig /flushdns: " + p.readAll();
}

void RouterWin::resetIpStack() {
    //    {
    //        QProcess p;
    //        QString command = QString("ipconfig /release");
    //        p.start(command);
    //    }
    {
        QProcess p;
        QString command = QString("netsh int ip reset");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command = QString("netsh winsock reset");
        p.start(command);
        p.waitForFinished();
    }
}

void RouterWin::suspendWcmSvc(bool suspend) {
    if (suspend == m_suspended)
        return;

    // Solve Windows bug (routes > 1000)
    DWORD wcmSvcPid = GetServicePid(std::wstring(L"wcmSvc").c_str());

    // ListProcessThreads(wcmSvcPid);
    BOOL ok = SuspendProcess(suspend, wcmSvcPid);
    if (ok) {
        m_suspended = suspend;
    }

    qDebug() << "RouterWin::routeAddList" << (ok ? "succeed to" : "failed to")
             << (suspend ? "suspend wcmSvc" : "resume wcmSvc");
}

DWORD RouterWin::GetServicePid(LPCWSTR serviceName) {
    const auto hScm = OpenSCManagerW(nullptr, nullptr, NULL);
    const auto hSc = OpenServiceW(hScm, serviceName, SERVICE_QUERY_STATUS);

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD bytesNeeded = 0;
    QueryServiceStatusEx(hSc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &bytesNeeded);

    CloseServiceHandle(hSc);
    CloseServiceHandle(hScm);

    return ssp.dwProcessId;
}

BOOL RouterWin::ListProcessThreads(DWORD dwOwnerPID) {
    HANDLE hThreadSnap = INVALID_HANDLE_VALUE;
    THREADENTRY32 te32;

    // Take a snapshot of all running threads
    hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE)
        return (FALSE);

    // Fill in the size of the structure before using it.
    te32.dwSize = sizeof(THREADENTRY32);

    // Retrieve information about the first thread,
    // and exit if unsuccessful
    if (!Thread32First(hThreadSnap, &te32)) {
        // printError( TEXT("Thread32First") ); // show cause of failure
        CloseHandle(hThreadSnap); // clean the snapshot object
        return (FALSE);
    }

    // Now walk the thread list of the system,
    // and display information about each thread
    // associated with the specified process
    // HANDLE threadHandle;
    do {
        if (te32.th32OwnerProcessID == dwOwnerPID) {
            HANDLE threadHandle = OpenThread(PROCESS_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
            qDebug() << "OpenThread GetLastError:" << te32.th32ThreadID << GetLastError() << threadHandle;
            ULONG64 cycles = 0;
            BOOL ok = QueryThreadCycleTime(threadHandle, &cycles);
            qDebug() << "QueryThreadCycleTime GetLastError:" << ok << GetLastError();

            qDebug() << "Thread cycles:" << te32.th32ThreadID << cycles;
            //      _tprintf( TEXT("\n\n     THREAD ID      = 0x%08X"), te32.th32ThreadID );
            //      _tprintf( TEXT("\n     Base priority  = %d"), te32.tpBasePri );
            //      _tprintf( TEXT("\n     Delta priority = %d"), te32.tpDeltaPri );
            //      _tprintf( TEXT("\n"));

            CloseHandle(threadHandle);
        }
    } while (Thread32Next(hThreadSnap, &te32));

    CloseHandle(hThreadSnap);
    return (TRUE);
}

BOOL RouterWin::EnableDebugPrivilege(VOID) {
    HANDLE hToken = NULL;
    TOKEN_PRIVILEGES priv;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
        return FALSE;

    if (!LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &priv.Privileges[0].Luid))
        return FALSE;

    priv.PrivilegeCount = 1;
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    return AdjustTokenPrivileges(hToken, FALSE, &priv, sizeof(priv), NULL, NULL);
}

BOOL RouterWin::InitNtFunctions(VOID) {
    HMODULE hModule;

    hModule = GetModuleHandleW(L"ntdll.dll");
    if (hModule == NULL)
        return FALSE;

    // NtSuspendProcess = (decltype(NtSuspendProcess))GetProcAddress(hModule, "NtSuspendThread");
    NtSuspendProcess = (decltype(NtSuspendProcess)) GetProcAddress(hModule, "NtSuspendProcess");
    if (NtSuspendProcess == NULL)
        return FALSE;

    // NtResumeProcess = (decltype(NtResumeProcess))GetProcAddress(hModule, "NtResumeThread");
    NtResumeProcess = (decltype(NtResumeProcess)) GetProcAddress(hModule, "NtResumeProcess");
    if (NtResumeProcess == NULL)
        return FALSE;

    return TRUE;
}

BOOL RouterWin::SuspendProcess(BOOL fSuspend, DWORD dwProcessId) {
    HANDLE pHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, dwProcessId);
    if (pHandle == NULL)
        return false;

    bool ok = ((fSuspend ? NtSuspendProcess : NtResumeProcess)(pHandle) == STATUS_SUCCESS);
    CloseHandle(pHandle);

    return ok;
}

bool RouterWin::updateResolvers(const QString &ifname, const QList<QHostAddress> &resolvers) {
    return m_dnsUtil->updateResolvers(ifname, resolvers);
}


void RouterWin::StopRoutingIpv6() {
    {
        QProcess p;
        QString command = QString("interface ipv6 add route fc00::/7 "
                                  "interface={NetworkInterface.IPv6LoopbackInterfaceIndex} metric=0 store=active");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command = QString("interface ipv6 add route 2000::/4 "
                                  "interface={NetworkInterface.IPv6LoopbackInterfaceIndex} metric=0 store=active");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command = QString("interface ipv6 add route 3000::/4 "
                                  "interface={NetworkInterface.IPv6LoopbackInterfaceIndex} metric=0 store=active");
        p.start(command);
        p.waitForFinished();
    }
}

void RouterWin::StartRoutingIpv6() {
    {
        QProcess p;
        QString command =
                QString("interface ipv6 delete route fc00::/7 interface={NetworkInterface.IPv6LoopbackInterfaceIndex}");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command =
                QString("interface ipv6 delete route 2000::/4 interface={NetworkInterface.IPv6LoopbackInterfaceIndex}");
        p.start(command);
        p.waitForFinished();
    }
    {
        QProcess p;
        QString command =
                QString("interface ipv6 delete route 3000::/4 interface={NetworkInterface.IPv6LoopbackInterfaceIndex}");
        p.start(command);
        p.waitForFinished();
    }
}

namespace {

    // An NRPT rule, as Add-DnsClientNrptRule and OpenVPN's interactive service write it
    const wchar_t kNrptRulesKey[] = L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters\\DnsPolicyConfig";
    const wchar_t kNrptRuleName[] = L"ZloVPN-LocalDomains";
    const wchar_t kNrptRuleKey[] =
            L"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\Parameters\\DnsPolicyConfig\\ZloVPN-LocalDomains";
    const wchar_t kNrptGroupPolicyKey[] = L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient\\DnsPolicyConfig";

    // DNS servers of the main adapter: the one with an IPv4 gateway and the lowest metric. The VPN adapter (tun3) has
    // no gateway, and the connection sets the tunnel DNS on it only, so these are as DHCP or the user set them.
    QList<QHostAddress> mainAdapterDns(QString &adapterName) {
        std::vector<BYTE> buffer;
        ULONG size = 15000;
        ULONG result = ERROR_BUFFER_OVERFLOW;
        for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; attempt++) {
            buffer.resize(size);
            result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                                          nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), &size);
        }
        if (result != NO_ERROR) {
            qWarning() << "GetAdaptersAddresses failed:" << result;
            return {};
        }

        // the WireGuard adapter's on-link default route shows up as gateway 0.0.0.0: a tunnel, not a router
        auto hasRouter = [](PIP_ADAPTER_ADDRESSES adapter) {
            for (auto gateway = adapter->FirstGatewayAddress; gateway; gateway = gateway->Next) {
                const sockaddr *address = gateway->Address.lpSockaddr;
                if (address && address->sa_family == AF_INET
                    && reinterpret_cast<const sockaddr_in *>(address)->sin_addr.s_addr != 0) {
                    return true;
                }
            }
            return false;
        };

        PIP_ADAPTER_ADDRESSES main = nullptr;
        for (auto adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); adapter; adapter = adapter->Next) {
            if (adapter->OperStatus == IfOperStatusUp && hasRouter(adapter)
                && (!main || adapter->Ipv4Metric < main->Ipv4Metric)) {
                main = adapter;
            }
        }
        if (!main) {
            return {};
        }

        adapterName = QString::fromWCharArray(main->FriendlyName);
        QList<QHostAddress> servers;
        for (auto dns = main->FirstDnsServerAddress; dns; dns = dns->Next) {
            if (dns->Address.lpSockaddr && dns->Address.lpSockaddr->sa_family == AF_INET) {
                servers.append(QHostAddress(dns->Address.lpSockaddr));
            }
        }
        return servers;
    }

    bool writeNrptRule(const QStringList &domains, const QStringList &servers) {
        // The rules key may hold rules of Windows and other software, so only our rule is volatile: it is gone after
        // a reboot even if the service crashed. A volatile parent would forbid non-volatile rules under it.
        HKEY rules = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kNrptRulesKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                         KEY_CREATE_SUB_KEY, nullptr, &rules, nullptr);
        if (status != ERROR_SUCCESS) {
            qWarning() << "Failed to open the NRPT rules:" << status;
            return false;
        }
        HKEY rule = nullptr;
        status = RegCreateKeyExW(rules, kNrptRuleName, 0, nullptr, REG_OPTION_VOLATILE, KEY_SET_VALUE, nullptr, &rule,
                                 nullptr);
        RegCloseKey(rules);
        if (status != ERROR_SUCCESS) {
            qWarning() << "Failed to create the NRPT rule:" << status;
            return false;
        }

        // ".lan" matches every name under lan; REG_MULTI_SZ ends with an empty string
        QString names;
        for (const QString &domain : domains) {
            names += QStringLiteral(".") + domain + QChar(u'\0');
        }
        names += QChar(u'\0');
        const QString genericServers = servers.join(';');
        const DWORD version = 2;
        const DWORD configOptions = 8; // use GenericDNSServers
        const wchar_t emptyString[] = L"";

        auto setValue = [&](const wchar_t *name, DWORD type, const void *data, size_t bytes) {
            if (status == ERROR_SUCCESS) {
                status = RegSetValueExW(rule, name, 0, type, static_cast<const BYTE *>(data), static_cast<DWORD>(bytes));
            }
        };
        setValue(L"Version", REG_DWORD, &version, sizeof(version));
        setValue(L"Name", REG_MULTI_SZ, names.utf16(), names.size() * sizeof(wchar_t));
        setValue(L"GenericDNSServers", REG_SZ, genericServers.utf16(), (genericServers.size() + 1) * sizeof(wchar_t));
        setValue(L"ConfigOptions", REG_DWORD, &configOptions, sizeof(configOptions));
        setValue(L"IPSECCARestriction", REG_SZ, emptyString, sizeof(emptyString));
        RegCloseKey(rule);

        if (status != ERROR_SUCCESS) {
            qWarning() << "Failed to write the NRPT rule:" << status;
        }
        return status == ERROR_SUCCESS;
    }

    // true when there was a rule
    bool deleteNrptRule() {
        const LSTATUS status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, kNrptRuleKey);
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            qWarning() << "Failed to delete the NRPT rule:" << status;
        }
        return status == ERROR_SUCCESS;
    }

    // Once Group Policy has created this key Windows reads NRPT rules only from it, even after the last policy rule
    // is gone (OpenVPN's interactive service writes its rules there in that case)
    bool hasGroupPolicyNrpt() {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kNrptGroupPolicyKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
            return false;
        }
        RegCloseKey(key);
        return true;
    }

    // The DNS client rereads its parameters, NRPT rules included (OpenVPN's interactive service does the same)
    void notifyDnsClient() {
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager) {
            qWarning() << "Failed to open the service manager:" << GetLastError();
            return;
        }
        SC_HANDLE dnscache = OpenServiceW(manager, L"Dnscache", SERVICE_PAUSE_CONTINUE);
        if (dnscache) {
            SERVICE_STATUS status;
            if (!ControlService(dnscache, SERVICE_CONTROL_PARAMCHANGE, &status)) {
                qWarning() << "Failed to notify the DNS client:" << GetLastError();
            }
            CloseServiceHandle(dnscache);
        }
        CloseServiceHandle(manager);
    }

} // namespace

QList<QHostAddress> RouterWin::setLocalDomainsResolver(bool enabled) {
    // drop the previous rule: the main adapter or its DNS may have changed since
    bool changed = deleteNrptRule();

    QList<QHostAddress> servers;
    if (enabled) {
        QString adapterName;
        const QList<QHostAddress> dns = mainAdapterDns(adapterName);
        servers = SplitTunnelPolicy::localDnsServers(dns);
        QStringList serverList;
        for (const QHostAddress &server : servers) {
            serverList.append(server.toString());
        }

        if (servers.isEmpty()) {
            qInfo() << "No local DNS server on the main adapter" << adapterName << dns
                    << "- local domains stay with the tunnel DNS";
        } else if (writeNrptRule(SplitTunnelPolicy::localDomains(), serverList)) {
            qInfo() << "Local domains" << SplitTunnelPolicy::localDomains() << "resolve via" << serverList << "of"
                    << adapterName;
            if (hasGroupPolicyNrpt()) {
                qWarning() << "NRPT comes from Group Policy here, Windows ignores the local domains rule";
            }
            changed = true;
        } else {
            deleteNrptRule();
            servers.clear();
        }
    } else if (changed) {
        qInfo() << "Local domains resolver removed";
    }

    if (changed) {
        notifyDnsClient();
        // the tunnel DNS may have answered "no such name" for router.lan while connecting
        flushDns();
    }
    return servers;
}
