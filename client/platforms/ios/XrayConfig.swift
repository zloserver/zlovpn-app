import Foundation

struct XrayConfig: Decodable {
    let dns1: String?
    let dns2: String?
    let config: String
    // 0 - everything through the VPN, 1 - only splitTunnelSites, 2 - everything except splitTunnelSites
    let splitTunnelType: Int?
    let splitTunnelSites: [String]?
}
