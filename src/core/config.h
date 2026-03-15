#pragma once

#include <string>
#include <fstream>

namespace tunngle {

class Config {
public:
    static Config& Instance();

    void Load();
    void Save();
    void ForceSave();

    bool GetAutoConnectTun() const { return auto_connect_tun_; }
    void SetAutoConnectTun(bool v);

    std::string GetLocalTunIP() const { return local_tun_ip_; }
    void SetLocalTunIP(const std::string& v);

    std::string GetPeerAddress() const { return peer_address_; }
    void SetPeerAddress(const std::string& v);

    uint16_t GetPeerPort() const { return peer_port_; }
    void SetPeerPort(uint16_t v);

    uint16_t GetLocalPort() const { return local_port_; }
    void SetLocalPort(uint16_t v);

    uint32_t GetTunUnit() const { return tun_unit_; }
    void SetTunUnit(uint32_t v);

    std::string GetCoordServer() const { return coord_server_; }
    void SetCoordServer(const std::string& v);

    std::string GetApiServer() const { return api_server_; }
    void SetApiServer(const std::string& v);

    std::string GetClientIPOverride() const { return client_ip_override_; }
    void SetClientIPOverride(const std::string& v);

    std::string GetNickname() const { return nickname_; }
    void SetNickname(const std::string& v);

    std::string GetAuthToken() const { return auth_token_; }
    void SetAuthToken(const std::string& v);
    std::string GetAuthUserID() const { return auth_user_id_; }
    void SetAuthUserID(const std::string& v);
    std::string GetAuthUsername() const { return auth_username_; }
    void SetAuthUsername(const std::string& v);
    std::string GetAuthDisplayName() const { return auth_display_name_; }
    void SetAuthDisplayName(const std::string& v);
    void ClearAuth();

    std::string GetConfigPath() const { return config_path_; }
    std::string GetConfigDir() const;

    bool EnsureConfigDirExists() const;

    void SetConfigDir(const std::string& dir);

    void ParseArgs(int argc, char** argv);

private:
    Config();
    std::string GetDefaultConfigPath() const;

    std::string config_path_;
    bool auto_connect_tun_ = true;
    std::string local_tun_ip_ = "7.0.0.1";
    std::string peer_address_;
    uint16_t peer_port_ = 11155;
    uint16_t local_port_ = 11155;
    uint32_t tun_unit_ = 0;
    std::string coord_server_ = "tls://trolley.proxy.rlwy.net:16885";
    std::string api_server_ = "https://tunngle-reborn-priv-production.up.railway.app";
    std::string client_ip_override_;
    std::string nickname_ = "Player";
    std::string auth_token_;
    std::string auth_user_id_;
    std::string auth_username_;
    std::string auth_display_name_;
    bool dirty_ = false;
};

}  // namespace tunngle
