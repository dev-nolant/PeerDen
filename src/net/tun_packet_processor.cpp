#include "tun_packet_processor.h"
#include "peer_tunnel_manager.h"
#include "tun_adapter.h"
#include "tunnel.h"
#include "core/logger.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace tunngle {
namespace net {

namespace {

constexpr uint8_t IPPROTO_ICMP = 1;

constexpr uint8_t ICMP_ECHO_REQUEST = 8;
constexpr uint8_t ICMP_ECHO_REPLY = 0;

uint16_t NetToHost16(uint16_t v) {
    return (static_cast<uint16_t>(v) >> 8) | (static_cast<uint16_t>(v) << 8);
}

uint16_t HostToNet16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

uint16_t InternetChecksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
    }
    if (len & 1) {
        sum += static_cast<uint16_t>(data[len - 1]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void RecalcIPChecksum(uint8_t* buf, size_t ihl) {
    buf[10] = 0;
    buf[11] = 0;
    uint16_t cksum = InternetChecksum(buf, ihl);
    buf[10] = static_cast<uint8_t>((cksum >> 8) & 0xFF);
    buf[11] = static_cast<uint8_t>(cksum & 0xFF);
}

bool IsForUs(const uint8_t* buf, size_t n, const std::string& local_ip) {
    if (n < 20) return false;
    uint8_t dst[4] = {buf[16], buf[17], buf[18], buf[19]};
    unsigned int a, b, c, d;
    if (std::sscanf(local_ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    return dst[0] == a && dst[1] == b && dst[2] == c && dst[3] == d;
}

std::string DstIPToString(const uint8_t* buf, size_t n) {
    if (n < 20) return "";
    char out[32];
    snprintf(out, sizeof(out), "%u.%u.%u.%u", buf[16], buf[17], buf[18], buf[19]);
    return out;
}

std::string SrcIPToString(const uint8_t* buf, size_t n) {
    if (n < 20) return "";
    char out[32];
    snprintf(out, sizeof(out), "%u.%u.%u.%u", buf[12], buf[13], buf[14], buf[15]);
    return out;
}

bool IsValidIPv4Packet(const uint8_t* buf, size_t n, size_t* total_len_out) {
    if (n < 20) return false;
    uint8_t version = static_cast<uint8_t>(buf[0] >> 4);
    if (version != 4) return false;
    uint8_t ihl = static_cast<uint8_t>((buf[0] & 0x0F) * 4);
    if (ihl < 20 || n < ihl) return false;
    size_t total_len = (static_cast<size_t>(buf[2]) << 8) | buf[3];
    if (total_len < ihl || total_len > n) return false;
    if (total_len_out) *total_len_out = total_len;
    return true;
}

bool IsKnownPeerIP(const std::string& ip, const std::vector<std::string>& peer_ips) {
    for (const auto& p : peer_ips) {
        if (p == ip) return true;
    }
    return false;
}

}  // namespace

void ProcessTunPackets(TunAdapter* tun, Tunnel* tunnel, const std::string& local_ip) {
    if (!tun || !tun->IsOpen()) return;

    const std::string my_ip = local_ip.empty() ? "7.0.0.1" : local_ip;

    constexpr int max_per_frame = 32;
    for (int i = 0; i < max_per_frame; ++i) {
        uint8_t buf[2048];
        ssize_t n = tun->Read(buf, sizeof(buf));
        if (n <= 0) break;

        if (n < 20) continue;

        if (IsForUs(buf, static_cast<size_t>(n), my_ip)) {
            uint8_t ver_ihl = buf[0];
            uint8_t ihl = (ver_ihl & 0x0F) * 4;
            if (ihl < 20 || static_cast<size_t>(n) < ihl + 8) continue;

            uint8_t protocol = buf[9];
            if (protocol != IPPROTO_ICMP) continue;

            uint8_t* icmp = buf + ihl;
            if (icmp[0] != ICMP_ECHO_REQUEST || icmp[1] != 0) continue;

            std::swap_ranges(buf + 12, buf + 16, buf + 16);

            icmp[0] = ICMP_ECHO_REPLY;
            icmp[1] = 0;
            icmp[2] = 0;
            icmp[3] = 0;
            uint16_t cksum = InternetChecksum(icmp, static_cast<size_t>(n) - ihl);
            icmp[2] = static_cast<uint8_t>((cksum >> 8) & 0xFF);
            icmp[3] = static_cast<uint8_t>(cksum & 0xFF);

            RecalcIPChecksum(buf, ihl);

            tun->Write(buf, static_cast<size_t>(n));
        } else if (tunnel && tunnel->IsConnected()) {
            tunnel->Send(buf, static_cast<size_t>(n));
        }
    }
}

void ProcessTunnelPackets(TunAdapter* tun, Tunnel* tunnel) {
    if (!tun || !tun->IsOpen() || !tunnel) return;
    if (!tunnel->IsConnected() && !tunnel->IsConnecting()) return;

    constexpr int max_per_frame = 32;
    for (int i = 0; i < max_per_frame; ++i) {
        uint8_t buf[2048];
        ssize_t n = tunnel->Receive(buf, sizeof(buf));
        if (n <= 0) break;

        ssize_t written = tun->Write(buf, static_cast<size_t>(n));
        if (written > 0) {
            LOG_DEBUG("Tunnel receive %zd bytes -> TUN", written);
        }
    }
}

void ProcessTunPackets(TunAdapter* tun, PeerTunnelManager* mgr, const std::string& local_ip) {
    if (!tun || !tun->IsOpen() || !mgr) return;

    const std::string my_ip = local_ip.empty() ? "7.0.0.1" : local_ip;

    constexpr int max_per_frame = 32;
    for (int i = 0; i < max_per_frame; ++i) {
        uint8_t buf[2048];
        ssize_t n = tun->Read(buf, sizeof(buf));
        if (n <= 0) break;

        if (n < 20) continue;

        if (IsForUs(buf, static_cast<size_t>(n), my_ip)) {
            uint8_t ver_ihl = buf[0];
            uint8_t ihl = (ver_ihl & 0x0F) * 4;
            if (ihl < 20 || static_cast<size_t>(n) < ihl + 8) continue;

            uint8_t protocol = buf[9];
            if (protocol != IPPROTO_ICMP) continue;

            uint8_t* icmp = buf + ihl;
            if (icmp[0] != ICMP_ECHO_REQUEST || icmp[1] != 0) continue;

            std::swap_ranges(buf + 12, buf + 16, buf + 16);

            icmp[0] = ICMP_ECHO_REPLY;
            icmp[1] = 0;
            icmp[2] = 0;
            icmp[3] = 0;
            uint16_t cksum = InternetChecksum(icmp, static_cast<size_t>(n) - ihl);
            icmp[2] = static_cast<uint8_t>((cksum >> 8) & 0xFF);
            icmp[3] = static_cast<uint8_t>(cksum & 0xFF);

            RecalcIPChecksum(buf, ihl);

            tun->Write(buf, static_cast<size_t>(n));
        } else {
            std::string dst_ip = DstIPToString(buf, static_cast<size_t>(n));
            Tunnel* tunnel = mgr->GetTunnelFor(dst_ip);
            if (tunnel && tunnel->IsConnected()) {
                tunnel->Send(buf, static_cast<size_t>(n));
            }
        }
    }
}

void ProcessTunnelPackets(TunAdapter* tun, PeerTunnelManager* mgr, const std::string& local_ip) {
    if (!tun || !tun->IsOpen() || !mgr) return;

    const std::string my_ip = local_ip.empty() ? "7.0.0.1" : local_ip;
    std::vector<std::string> peer_ips = mgr->GetPeerTunIPs();
    for (const std::string& peer_ip : peer_ips) {
        Tunnel* tunnel = mgr->GetTunnelFor(peer_ip);
        if (!tunnel) continue;
        if (!tunnel->IsConnected() && !tunnel->IsConnecting()) continue;

        constexpr int max_per_frame = 32;
        for (int i = 0; i < max_per_frame; ++i) {
            uint8_t buf[2048];
            ssize_t n = tunnel->Receive(buf, sizeof(buf));
            if (n <= 0) break;

            size_t total_len = 0;
            if (!IsValidIPv4Packet(buf, static_cast<size_t>(n), &total_len)) continue;

            std::string src_ip = SrcIPToString(buf, total_len);
            if (src_ip != peer_ip) continue;

            std::string dst_ip = DstIPToString(buf, total_len);
            if (dst_ip != my_ip && !IsKnownPeerIP(dst_ip, peer_ips)) continue;

            ssize_t written = tun->Write(buf, total_len);
            if (written > 0) {
                LOG_DEBUG("Tunnel receive %zd bytes -> TUN", written);
            }
        }
    }
}

}  // namespace net
}  // namespace tunngle
