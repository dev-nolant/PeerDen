#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace tunngle {
namespace net {

struct CoordPeerInfo {
    std::string ip;
    uint16_t port = 0;
    std::string tun_ip;
    std::string nick;
    std::string auth_token;
};

struct CoordRoomInfo {
    std::string name;
    std::string code;
    std::string description;
    std::string genre;
    int peers = 0;
    int max_size = 8;
    bool has_password = false;
    std::string game_name;
    std::string game_thumbnail;
    std::string voice_chat_url;
};

struct CoordChatEntry {
    std::string nick;
    std::string text;
};

class CoordClient {
public:
    CoordClient();
    ~CoordClient();

    bool Connect(const std::string& host, uint16_t port, bool use_tls = false,
                 const std::string& trusted_cert_file = {});

    bool Join(const std::string& room, const std::string& nick, uint16_t udp_port,
              bool is_public = true, int max_size = 8,
              const std::string& password = {},
              const std::string& genre = {},
              const std::string& desc = {},
              const std::string& game_name = {},
              const std::string& game_thumbnail = {},
              const std::string& voice_chat_url = {},
              bool create_new = false);

    void Leave();

    void Disconnect();

    bool IsConnected() const { return fd_ >= 0; }
    bool IsInRoom() const { return in_room_; }

    std::string GetAssignedTunIP() const { return assigned_tun_ip_; }
    std::string GetRelayAddr() const { return relay_addr_; }
    std::string GetRelayToken() const { return relay_token_; }
    const std::vector<CoordPeerInfo>& GetPeers() const { return peers_; }

    void RequestRoomList();

    const std::vector<CoordRoomInfo>& GetRoomList() const { return room_list_; }

    void SetCurrentRoomInfo(const CoordRoomInfo& info) { current_room_info_ = info; }
    const CoordRoomInfo& GetCurrentRoomInfo() const { return current_room_info_; }

    void SendChat(const std::string& text);

    const std::vector<CoordChatEntry>& GetChatMessages() const { return chat_messages_; }

    void Pump();

    std::string GetLastError() const { return last_error_; }
    void SetLastError(const std::string& msg) { last_error_ = msg; }
    void ClearLastError() { last_error_ = ""; }

private:
    void ReadLoop();
    void ProcessMessage(const std::string& line);
    int RawSend(const void* buf, size_t len);
    int RawRecv(void* buf, size_t len, bool* would_block = nullptr);

    int fd_ = -1;
#ifdef TUNNGLE_COORD_TLS
    void* ssl_ = nullptr;
#endif
    bool in_room_ = false;
    std::string assigned_tun_ip_;
    std::string relay_addr_;
    std::string relay_token_;
    std::vector<CoordPeerInfo> peers_;
    std::vector<CoordRoomInfo> room_list_;
    CoordRoomInfo current_room_info_;
    std::vector<CoordChatEntry> chat_messages_;
    std::mutex mutex_;
    std::vector<std::string> message_queue_;
    bool read_thread_running_ = false;
    mutable std::string last_error_;
};

}  // namespace net
}  // namespace tunngle
