#pragma once

#include "tunnel.h"
#include "core/platform.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_set>

namespace tunngle {
namespace net {

class UdpTunnel : public Tunnel {
public:
    UdpTunnel();
    ~UdpTunnel() override;

    bool Connect(const std::string& peer_addr, uint16_t peer_port, uint16_t local_port = 0,
                 const std::string& auth_token = {});

    void Disconnect() override;

    bool IsConnected() const override { return fd_ >= 0 && handshake_state_ == kConnected; }

    bool IsConnecting() const override { return fd_ >= 0 && handshake_state_ == kHelloSent; }

    bool TimedOut() const override { return timed_out_; }

    int GetPingMs() const override { return last_rtt_ms_; }

    void Pump(uint64_t now_ms) override;

    ssize_t Send(const uint8_t* data, size_t len) override;

    ssize_t Receive(uint8_t* buf, size_t capacity) override;

    std::string GetPeerAddress() const { return peer_addr_; }
    uint16_t GetPeerPort() const { return peer_port_; }

private:
    enum HandshakeState { kNone, kHelloSent, kConnected };

    bool SendHello();
    bool SendAck(uint64_t challenge_nonce);
    bool SendPingPong(uint8_t type, uint64_t timestamp_ms);
    uint64_t MakeNonce() const;
    bool IsReplayNonce(uint64_t id) const;
    void RememberNonce(uint64_t id);

    int fd_ = -1;
    std::string peer_addr_;
    uint16_t peer_port_ = 0;
    std::string auth_token_;
    uint64_t last_hello_nonce_ = 0;
    uint8_t crypto_key_[32] = {0};
    uint32_t send_nonce_prefix_ = 0;
    uint64_t send_counter_ = 1;
    std::deque<uint64_t> recent_nonce_order_;
    std::unordered_set<uint64_t> recent_nonce_set_;
    HandshakeState handshake_state_ = kNone;
    uint64_t connect_started_ms_ = 0;
    uint64_t last_hello_ms_ = 0;
    uint64_t last_ping_sent_ms_ = 0;
    int last_rtt_ms_ = -1;
    bool timed_out_ = false;
};

}  // namespace net
}  // namespace tunngle
