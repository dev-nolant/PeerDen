#include "relay_connection.h"
#include "tunnel_crypto.h"
#include "core/logger.h"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#define SOCK_ERRNO WSAGetLastError()
inline int relay_set_nonblock(SOCKET s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#define SOCK_ERRNO errno
inline int relay_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return (flags >= 0) ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}
#endif

namespace tunngle {
namespace net {

namespace {
constexpr uint8_t RELAY_MAGIC[7] = {'T', 'U', 'N', 'N', 'G', 'L', 'E'};
constexpr uint8_t REGISTER_TYPE = 0x10;
constexpr uint8_t KEEPALIVE_TYPE = 0x11;
constexpr uint64_t KEEPALIVE_INTERVAL_MS = 15000;
constexpr size_t IP_HEADER_LEN = 4;
constexpr size_t REPLAY_WINDOW_SIZE = 4096;

uint16_t HostToNet16(uint16_t v) {
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

std::string Uint32ToIp(uint32_t ip) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return buf;
}

uint64_t MixNonceId(uint32_t prefix, uint64_t counter) {
    uint64_t x = (static_cast<uint64_t>(prefix) << 32) ^ counter;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
}  // namespace

RelayConnection::RelayConnection() = default;

RelayConnection::~RelayConnection() { Disconnect(); }

uint32_t RelayConnection::IpToUint32(const std::string& ip) {
    unsigned a, b, c, d;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
    return (a << 24) | (b << 16) | (c << 8) | d;
}

bool RelayConnection::Connect(const std::string& relay_host, uint16_t relay_port,
                               const std::string& my_tun_ip,
                               const std::string& relay_token) {
    if (fd_ >= 0) Disconnect();

    my_tun_ip_ = my_tun_ip;
    my_tun_ip_u32_ = IpToUint32(my_tun_ip);
    if (my_tun_ip_u32_ == 0) {
        LOG_ERROR("Relay: invalid tun IP %s", my_tun_ip.c_str());
        return false;
    }

    struct addrinfo hints{}, *res = nullptr;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", relay_port);
    if (getaddrinfo(relay_host.c_str(), port_str, &hints, &res) != 0 || !res) {
        LOG_ERROR("Relay: cannot resolve %s:%u", relay_host.c_str(), relay_port);
        return false;
    }

#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    fd_ = (s == INVALID_SOCKET) ? -1 : static_cast<int>(s);
#else
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (fd_ < 0) {
        freeaddrinfo(res);
        LOG_ERROR("Relay: socket failed");
        return false;
    }

#ifdef _WIN32
    if (connect(static_cast<SOCKET>(fd_), res->ai_addr, static_cast<int>(res->ai_addrlen)) < 0) {
#else
    if (connect(fd_, res->ai_addr, res->ai_addrlen) < 0) {
#endif
        LOG_ERROR("Relay: connect failed");
        freeaddrinfo(res);
        close(fd_);
        fd_ = -1;
        return false;
    }
    freeaddrinfo(res);

    relay_token_ = relay_token;
    send_nonce_prefix_ = RandomNoncePrefix();
    if (send_nonce_prefix_ == 0) send_nonce_prefix_ = 1;
    send_counter_ = 1;

#ifdef _WIN32
    relay_set_nonblock(static_cast<SOCKET>(fd_));
#else
    relay_set_nonblock(fd_);
#endif

    SendRegister();
    registered_ = true;
    last_keepalive_ms_ = 0;
    LOG_INFO("Relay: connected as %s", my_tun_ip.c_str());
    return true;
}

void RelayConnection::Disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    registered_ = false;
    recv_buffers_.clear();
    peer_keys_.clear();
    replay_states_.clear();
}

void RelayConnection::SendRegister() {
    if (relay_token_.empty()) return;

    const size_t token_len = relay_token_.size();
    if (token_len > 65535 - (12 + 2)) return;

    uint8_t pkt[65535];
    std::memcpy(pkt, RELAY_MAGIC, 7);
    pkt[7] = REGISTER_TYPE;
    pkt[8] = static_cast<uint8_t>((my_tun_ip_u32_ >> 24) & 0xFF);
    pkt[9] = static_cast<uint8_t>((my_tun_ip_u32_ >> 16) & 0xFF);
    pkt[10] = static_cast<uint8_t>((my_tun_ip_u32_ >> 8) & 0xFF);
    pkt[11] = static_cast<uint8_t>(my_tun_ip_u32_ & 0xFF);
    const size_t header_len = 12;
    const uint16_t token_len_u16 = static_cast<uint16_t>(token_len);
    pkt[header_len] = static_cast<uint8_t>((token_len_u16 >> 8) & 0xFF);
    pkt[header_len + 1] = static_cast<uint8_t>(token_len_u16 & 0xFF);
    std::memcpy(pkt + header_len + 2, relay_token_.data(), token_len);
#ifdef _WIN32
    send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(pkt), static_cast<int>(header_len+2+token_len), 0);
#else
    send(fd_, pkt, header_len + 2 + token_len, 0);
#endif
}

void RelayConnection::SendKeepalive() {
    uint8_t pkt[8];
    std::memcpy(pkt, RELAY_MAGIC, 7);
    pkt[7] = KEEPALIVE_TYPE;
#ifdef _WIN32
    send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(pkt), sizeof(pkt), 0);
#else
    send(fd_, pkt, sizeof(pkt), 0);
#endif
}

void RelayConnection::SetPeerToken(const std::string& peer_tun_ip, const std::string& auth_token) {
    uint8_t key[32];
    if (!InitTunnelCryptoKey(auth_token, key)) return;
    peer_keys_[peer_tun_ip] = std::vector<uint8_t>(key, key + 32);
    replay_states_[peer_tun_ip] = ReplayState{};
}

void RelayConnection::Pump(uint64_t now_ms) {
    if (fd_ < 0) return;

    // Keepalive
    if (now_ms - last_keepalive_ms_ >= KEEPALIVE_INTERVAL_MS) {
        last_keepalive_ms_ = now_ms;
        SendKeepalive();
    }

    for (int i = 0; i < 64; ++i) {
        uint8_t raw[65535];
        ssize_t n;
#ifdef _WIN32
        int ret = recv(static_cast<SOCKET>(fd_), reinterpret_cast<char*>(raw), sizeof(raw), 0);
        if (ret == SOCKET_ERROR) break;
        n = ret;
#else
        n = recv(fd_, raw, sizeof(raw), 0);
#endif
        if (n <= 0) break;
        if (static_cast<size_t>(n) < IP_HEADER_LEN + 1) continue;

        uint32_t src_u32 = (static_cast<uint32_t>(raw[0]) << 24) |
                           (static_cast<uint32_t>(raw[1]) << 16) |
                           (static_cast<uint32_t>(raw[2]) << 8) |
                           static_cast<uint32_t>(raw[3]);
        std::string src_ip = Uint32ToIp(src_u32);

        size_t payload_len = static_cast<size_t>(n) - IP_HEADER_LEN;
        recv_count_++;
        if (recv_count_ <= 5 || recv_count_ % 100 == 0) {
            LOG_INFO("Relay: recv %zu bytes from %s (pkt #%llu)",
                     payload_len, src_ip.c_str(), static_cast<unsigned long long>(recv_count_));
        }

        auto key_it = peer_keys_.find(src_ip);
        if (key_it == peer_keys_.end() || key_it->second.size() != 32) continue;
        uint32_t recv_prefix = 0;
        uint64_t recv_counter = 0;
        if (!ExtractTunnelNonce(raw + IP_HEADER_LEN, payload_len, &recv_prefix, &recv_counter)) continue;
        uint64_t nonce_id = MixNonceId(recv_prefix, recv_counter);
        auto& replay = replay_states_[src_ip];
        if (replay.set.find(nonce_id) != replay.set.end()) continue;
        std::vector<uint8_t> plain;
        if (!DecryptTunnelPayload(key_it->second.data(), raw + IP_HEADER_LEN, payload_len, plain)) continue;
        replay.set.insert(nonce_id);
        replay.order.push_back(nonce_id);
        while (replay.order.size() > REPLAY_WINDOW_SIZE) {
            uint64_t old = replay.order.front();
            replay.order.pop_front();
            replay.set.erase(old);
        }
        recv_buffers_[src_ip].push(std::move(plain));
    }
}

bool RelayConnection::Send(const std::string& dst_tun_ip, const uint8_t* data, size_t len) {
    if (fd_ < 0 || !registered_) return false;
    if (len + IP_HEADER_LEN > 65535) return false;

    uint32_t dst = IpToUint32(dst_tun_ip);
    if (dst == 0) return false;
    auto key_it = peer_keys_.find(dst_tun_ip);
    if (key_it == peer_keys_.end() || key_it->second.size() != 32) return false;

    std::vector<uint8_t> sealed;
    if (!EncryptTunnelPayload(key_it->second.data(), send_nonce_prefix_, send_counter_++, data, len, sealed)) return false;
    if (sealed.size() + IP_HEADER_LEN > 65535) return false;

    uint8_t pkt[65535];
    pkt[0] = static_cast<uint8_t>((dst >> 24) & 0xFF);
    pkt[1] = static_cast<uint8_t>((dst >> 16) & 0xFF);
    pkt[2] = static_cast<uint8_t>((dst >> 8) & 0xFF);
    pkt[3] = static_cast<uint8_t>(dst & 0xFF);
    std::memcpy(pkt + IP_HEADER_LEN, sealed.data(), sealed.size());

    send_count_++;
    if (send_count_ <= 5 || send_count_ % 100 == 0) {
        LOG_INFO("Relay: send %zu bytes to %s (pkt #%llu)",
                 len, dst_tun_ip.c_str(), static_cast<unsigned long long>(send_count_));
    }

#ifdef _WIN32
    int ret = send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(pkt),
                   static_cast<int>(sealed.size() + IP_HEADER_LEN), 0);
    return ret != SOCKET_ERROR;
#else
    return send(fd_, pkt, sealed.size() + IP_HEADER_LEN, 0) > 0;
#endif
}

ssize_t RelayConnection::ReceiveFor(const std::string& src_tun_ip, uint8_t* buf, size_t capacity) {
    auto it = recv_buffers_.find(src_tun_ip);
    if (it == recv_buffers_.end() || it->second.empty()) return 0;

    const auto& pkt = it->second.front();
    if (pkt.size() > capacity) {
        it->second.pop();
        return -1;
    }
    std::memcpy(buf, pkt.data(), pkt.size());
    ssize_t sz = static_cast<ssize_t>(pkt.size());
    it->second.pop();
    return sz;
}

bool RelayConnection::HasDataFrom(const std::string& src_tun_ip) const {
    auto it = recv_buffers_.find(src_tun_ip);
    return it != recv_buffers_.end() && !it->second.empty();
}

}  // namespace net
}  // namespace tunngle
