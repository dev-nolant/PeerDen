#pragma once

#include "core/platform.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>

namespace tunngle {
namespace net {

class RelayConnection {
public:
    RelayConnection();
    ~RelayConnection();

    bool Connect(const std::string& relay_host, uint16_t relay_port,
                 const std::string& my_tun_ip,
                 const std::string& relay_token);
    void Disconnect();
    bool IsConnected() const { return fd_ >= 0 && registered_; }

    void Pump(uint64_t now_ms);

    bool Send(const std::string& dst_tun_ip, const uint8_t* data, size_t len);

    ssize_t ReceiveFor(const std::string& src_tun_ip, uint8_t* buf, size_t capacity);
    void SetPeerToken(const std::string& peer_tun_ip, const std::string& auth_token);

    bool HasDataFrom(const std::string& src_tun_ip) const;

private:
    struct ReplayState {
        std::deque<uint64_t> order;
        std::unordered_set<uint64_t> set;
    };

    static uint32_t IpToUint32(const std::string& ip);
    void SendRegister();
    void SendKeepalive();

    int fd_ = -1;
    std::string my_tun_ip_;
    uint32_t my_tun_ip_u32_ = 0;
    std::string relay_token_;
    bool registered_ = false;
    uint64_t last_keepalive_ms_ = 0;
    uint64_t send_counter_ = 1;
    uint32_t send_nonce_prefix_ = 1;
    std::map<std::string, std::vector<uint8_t>> peer_keys_;
    std::map<std::string, ReplayState> replay_states_;

    std::map<std::string, std::queue<std::vector<uint8_t>>> recv_buffers_;
    uint64_t send_count_ = 0;
    uint64_t recv_count_ = 0;
};

}  // namespace net
}  // namespace tunngle
