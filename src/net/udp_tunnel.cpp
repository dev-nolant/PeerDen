#include "udp_tunnel.h"
#include "tunnel_crypto.h"
#include "core/logger.h"
#include <chrono>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <cerrno>
#define close closesocket
#define SOCK_ERRNO WSAGetLastError()
namespace { struct WsaInit { WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); } } wsa; }
#define F_GETFL 0
#define F_SETFL 0
#define O_NONBLOCK 0
inline int fcntl(int fd, int cmd, int arg) {
    (void)cmd; (void)arg;
    u_long mode = 1;
    return ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode) == 0 ? 0 : -1;
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#define SOCK_ERRNO errno
#endif

namespace tunngle {
namespace net {

namespace {

constexpr uint8_t HELLO_TYPE = 0x01;
constexpr uint8_t ACK_TYPE = 0x02;
constexpr uint8_t PING_TYPE = 0x03;
constexpr uint8_t PONG_TYPE = 0x04;
constexpr size_t HANDSHAKE_LEN = 32;
constexpr size_t HANDSHAKE_MIN_LEN = HANDSHAKE_LEN;
constexpr size_t PING_PONG_LEN = 16;  // "TUNNGLE" + type + 8-byte timestamp
constexpr uint64_t HELLO_RETRY_MS = 500;
constexpr uint64_t HELLO_TIMEOUT_MS = 10000;
constexpr uint64_t PING_INTERVAL_MS = 2000;
constexpr size_t REPLAY_WINDOW_SIZE = 4096;

uint16_t HostToNet16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

bool IsHandshakePacket(const uint8_t* payload, size_t len) {
    if (len < HANDSHAKE_MIN_LEN) return false;
    if (std::memcmp(payload, "TUNNGLE", 7) != 0) return false;
    return payload[7] == HELLO_TYPE || payload[7] == ACK_TYPE;
}

bool IsPingPongPacket(const uint8_t* payload, size_t len) {
    if (len != PING_PONG_LEN) return false;
    return std::memcmp(payload, "TUNNGLE", 7) == 0 &&
           (payload[7] == PING_TYPE || payload[7] == PONG_TYPE);
}

void WriteTimestamp(uint8_t* buf, uint64_t ts_ms) {
    buf[0] = (ts_ms >> 56) & 0xFF;
    buf[1] = (ts_ms >> 48) & 0xFF;
    buf[2] = (ts_ms >> 40) & 0xFF;
    buf[3] = (ts_ms >> 32) & 0xFF;
    buf[4] = (ts_ms >> 24) & 0xFF;
    buf[5] = (ts_ms >> 16) & 0xFF;
    buf[6] = (ts_ms >> 8) & 0xFF;
    buf[7] = ts_ms & 0xFF;
}

uint64_t ReadTimestamp(const uint8_t* buf) {
    return (static_cast<uint64_t>(buf[0]) << 56) |
           (static_cast<uint64_t>(buf[1]) << 48) |
           (static_cast<uint64_t>(buf[2]) << 40) |
           (static_cast<uint64_t>(buf[3]) << 32) |
           (static_cast<uint64_t>(buf[4]) << 24) |
           (static_cast<uint64_t>(buf[5]) << 16) |
           (static_cast<uint64_t>(buf[6]) << 8) |
           static_cast<uint64_t>(buf[7]);
}

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
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

UdpTunnel::UdpTunnel() = default;

UdpTunnel::~UdpTunnel() {
    Disconnect();
}

uint64_t UdpTunnel::MakeNonce() const {
    return static_cast<uint64_t>(NowMs()) ^ (static_cast<uint64_t>(send_counter_) << 32);
}

bool UdpTunnel::IsReplayNonce(uint64_t id) const {
    return recent_nonce_set_.find(id) != recent_nonce_set_.end();
}

void UdpTunnel::RememberNonce(uint64_t id) {
    if (recent_nonce_set_.find(id) != recent_nonce_set_.end()) return;
    recent_nonce_set_.insert(id);
    recent_nonce_order_.push_back(id);
    while (recent_nonce_order_.size() > REPLAY_WINDOW_SIZE) {
        uint64_t old = recent_nonce_order_.front();
        recent_nonce_order_.pop_front();
        recent_nonce_set_.erase(old);
    }
}

bool UdpTunnel::SendHello() {
    if (fd_ < 0) return false;
    last_hello_nonce_ = MakeNonce();
    uint8_t proof[16];
    if (!MakeHandshakeProof(crypto_key_, HELLO_TYPE, last_hello_nonce_, proof)) return false;
    const size_t payload_len = HANDSHAKE_LEN;
    uint8_t buf[2 + HANDSHAKE_LEN];
    buf[0] = (payload_len >> 8) & 0xFF;
    buf[1] = payload_len & 0xFF;
    std::memcpy(buf + 2, "TUNNGLE", 7);
    buf[9] = HELLO_TYPE;
    WriteTimestamp(buf + 10, last_hello_nonce_);
    std::memcpy(buf + 18, proof, 16);
#ifdef _WIN32
    return send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(buf), static_cast<int>(2 + payload_len), 0) == static_cast<ssize_t>(2 + payload_len);
#else
    return send(fd_, buf, 2 + payload_len, 0) == static_cast<ssize_t>(2 + payload_len);
#endif
}

bool UdpTunnel::SendAck(uint64_t challenge_nonce) {
    if (fd_ < 0) return false;
    uint8_t proof[16];
    if (!MakeHandshakeProof(crypto_key_, ACK_TYPE, challenge_nonce, proof)) return false;
    const size_t payload_len = HANDSHAKE_LEN;
    uint8_t buf[2 + HANDSHAKE_LEN];
    buf[0] = (payload_len >> 8) & 0xFF;
    buf[1] = payload_len & 0xFF;
    std::memcpy(buf + 2, "TUNNGLE", 7);
    buf[9] = ACK_TYPE;
    WriteTimestamp(buf + 10, challenge_nonce);
    std::memcpy(buf + 18, proof, 16);
#ifdef _WIN32
    return send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(buf), static_cast<int>(2 + payload_len), 0) == static_cast<ssize_t>(2 + payload_len);
#else
    return send(fd_, buf, 2 + payload_len, 0) == static_cast<ssize_t>(2 + payload_len);
#endif
}

bool UdpTunnel::SendPingPong(uint8_t type, uint64_t timestamp_ms) {
    if (fd_ < 0) return false;
    uint8_t payload[PING_PONG_LEN];
    std::memcpy(payload, "TUNNGLE", 7);
    payload[7] = type;
    WriteTimestamp(payload + 8, timestamp_ms);
    uint8_t buf[2 + PING_PONG_LEN];
    buf[0] = (PING_PONG_LEN >> 8) & 0xFF;
    buf[1] = PING_PONG_LEN & 0xFF;
    std::memcpy(buf + 2, payload, PING_PONG_LEN);
#ifdef _WIN32
    return send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(buf), sizeof(buf), 0) == static_cast<ssize_t>(sizeof(buf));
#else
    return send(fd_, buf, sizeof(buf), 0) == static_cast<ssize_t>(sizeof(buf));
#endif
}

bool UdpTunnel::Connect(const std::string& peer_addr, uint16_t peer_port, uint16_t local_port,
                        const std::string& auth_token) {
    if (fd_ >= 0) Disconnect();
    timed_out_ = false;
    auth_token_ = auth_token;
    if (auth_token_.empty()) {
        LOG_ERROR("UDP tunnel: missing auth token");
        return false;
    }
    if (!InitTunnelCryptoKey(auth_token_, crypto_key_)) {
        LOG_ERROR("UDP tunnel: crypto unavailable");
        return false;
    }
    send_nonce_prefix_ = RandomNoncePrefix();
    if (send_nonce_prefix_ == 0) send_nonce_prefix_ = 1;
    send_counter_ = 1;

#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    fd_ = (s == INVALID_SOCKET) ? -1 : static_cast<int>(s);
#else
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (fd_ < 0) {
        LOG_ERROR("UDP tunnel: socket failed: %s", strerror(SOCK_ERRNO));
        return false;
    }

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

#ifdef _WIN32
#define FD_CAST (static_cast<SOCKET>(fd_))
#else
#define FD_CAST fd_
#endif

    if (local_port != 0) {
        struct sockaddr_in bind_addr;
        std::memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        bind_addr.sin_port = HostToNet16(local_port);
        if (bind(FD_CAST, reinterpret_cast<struct sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            LOG_ERROR("UDP tunnel: bind failed: %s", strerror(SOCK_ERRNO));
            close(fd_);
            fd_ = -1;
            return false;
        }
    }

    struct sockaddr_in peer;
    std::memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_port = HostToNet16(peer_port);
    if (inet_pton(AF_INET, peer_addr.c_str(), &peer.sin_addr) <= 0) {
        LOG_ERROR("UDP tunnel: invalid peer address: %s", peer_addr.c_str());
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (connect(FD_CAST, reinterpret_cast<struct sockaddr*>(&peer), sizeof(peer)) < 0) {
        LOG_ERROR("UDP tunnel: connect failed: %s", strerror(SOCK_ERRNO));
        close(fd_);
        fd_ = -1;
        return false;
    }

#undef FD_CAST

    peer_addr_ = peer_addr;
    peer_port_ = peer_port;
    handshake_state_ = kHelloSent;
    connect_started_ms_ = NowMs();
    last_hello_ms_ = connect_started_ms_;

    if (!SendHello()) {
        LOG_ERROR("UDP tunnel: failed to send HELLO");
        Disconnect();
        return false;
    }
    LOG_INFO("UDP tunnel connecting (handshake...)");
    return true;
}

void UdpTunnel::Disconnect() {
    if (fd_ >= 0) {
        LOG_INFO("UDP tunnel disconnected");
        close(fd_);
        fd_ = -1;
    }
    peer_addr_.clear();
    peer_port_ = 0;
    auth_token_.clear();
    last_hello_nonce_ = 0;
    std::memset(crypto_key_, 0, sizeof(crypto_key_));
    send_nonce_prefix_ = 0;
    send_counter_ = 1;
    recent_nonce_order_.clear();
    recent_nonce_set_.clear();
    handshake_state_ = kNone;
}

void UdpTunnel::Pump(uint64_t now_ms) {
    if (fd_ < 0) return;
    if (handshake_state_ == kHelloSent) {
        if (now_ms - connect_started_ms_ > HELLO_TIMEOUT_MS) {
            LOG_ERROR("UDP tunnel handshake timeout");
            timed_out_ = true;
            Disconnect();
            return;
        }
        if (now_ms - last_hello_ms_ < HELLO_RETRY_MS) return;
        last_hello_ms_ = now_ms;
        SendHello();
        return;
    }
    if (handshake_state_ == kConnected && now_ms - last_ping_sent_ms_ >= PING_INTERVAL_MS) {
        last_ping_sent_ms_ = now_ms;
        SendPingPong(PING_TYPE, now_ms);
    }
}

ssize_t UdpTunnel::Send(const uint8_t* data, size_t len) {
    if (fd_ < 0 || handshake_state_ != kConnected) return -1;
    if (len > 65535) return -1;

    std::vector<uint8_t> sealed;
    if (!EncryptTunnelPayload(crypto_key_, send_nonce_prefix_, send_counter_++, data, len, sealed)) return -1;
    if (sealed.size() > 65535) return -1;
    uint8_t buf[65535 + 2];
    buf[0] = (sealed.size() >> 8) & 0xFF;
    buf[1] = sealed.size() & 0xFF;
    std::memcpy(buf + 2, sealed.data(), sealed.size());

#ifdef _WIN32
    int ret = send(static_cast<SOCKET>(fd_), reinterpret_cast<const char*>(buf), static_cast<int>(sealed.size() + 2), 0);
    return (ret == SOCKET_ERROR) ? -1 : ret;
#else
    return send(fd_, buf, sealed.size() + 2, 0);
#endif
}

ssize_t UdpTunnel::Receive(uint8_t* buf, size_t capacity) {
    if (fd_ < 0) return -1;
    if (capacity < 2) return -1;

    uint8_t raw[65535 + 2];
    ssize_t n;
#ifdef _WIN32
    int ret = recv(static_cast<SOCKET>(fd_), reinterpret_cast<char*>(raw), sizeof(raw), 0);
    if (ret == SOCKET_ERROR) {
        return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
    }
    n = ret;
#else
    n = recv(fd_, raw, sizeof(raw), 0);
#endif
    if (n <= 0) return n;
    if (n < 2) return 0;

    size_t payload_len = (static_cast<size_t>(raw[0]) << 8) | raw[1];
    if (payload_len > static_cast<size_t>(n) - 2) return 0;

    const uint8_t* payload = raw + 2;

    if (IsHandshakePacket(payload, payload_len)) {
        if (payload_len < HANDSHAKE_MIN_LEN) return 0;
        uint8_t type = payload[7];
        uint64_t nonce = ReadTimestamp(payload + 8);
        const uint8_t* proof = payload + 16;

        if (type == HELLO_TYPE) {
            if (!VerifyHandshakeProof(crypto_key_, HELLO_TYPE, nonce, proof)) return 0;
            SendAck(nonce);
            if (handshake_state_ != kConnected) {
                handshake_state_ = kConnected;
                LOG_INFO("UDP tunnel connected (LAN link up)");
            }
        } else if (type == ACK_TYPE) {
            if (handshake_state_ == kHelloSent && nonce == last_hello_nonce_ &&
                VerifyHandshakeProof(crypto_key_, ACK_TYPE, nonce, proof)) {
                handshake_state_ = kConnected;
                LOG_INFO("UDP tunnel connected");
            }
        }
        return 0;  // Control packet, no data to forward
    }
    if (IsPingPongPacket(payload, payload_len)) {
        uint8_t type = payload[7];
        if (type == PING_TYPE) {
            SendPingPong(PONG_TYPE, ReadTimestamp(payload + 8));
        } else if (type == PONG_TYPE) {
            uint64_t sent_ms = ReadTimestamp(payload + 8);
            int rtt = static_cast<int>(NowMs() - sent_ms);
            if (rtt >= 0 && rtt < 60000) last_rtt_ms_ = rtt;
        }
        return 0;  // Control packet, no data to forward
    }

    if (handshake_state_ != kConnected) return 0;  // Drop data until handshaken

    std::vector<uint8_t> plain;
    uint32_t recv_prefix = 0;
    uint64_t recv_counter = 0;
    if (!ExtractTunnelNonce(payload, payload_len, &recv_prefix, &recv_counter)) return 0;
    uint64_t nonce_id = MixNonceId(recv_prefix, recv_counter);
    if (IsReplayNonce(nonce_id)) return 0;
    if (!DecryptTunnelPayload(crypto_key_, payload, payload_len, plain)) return 0;
    RememberNonce(nonce_id);
    if (plain.size() > capacity) return -1;
    std::memcpy(buf, plain.data(), plain.size());
    return static_cast<ssize_t>(plain.size());
}

}  // namespace net
}  // namespace tunngle
