#include "coord_client.h"
#include "core/config.h"
#include "core/platform.h"
#include "core/logger.h"
#include <cctype>
#include <cstring>
#include <thread>
#include <cstdlib>

#ifdef TUNNGLE_COORD_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <cerrno>
#include <windows.h>
#define close closesocket
#define SOCK_ERRNO WSAGetLastError()
namespace {
const char* WinSockErrorStr(int err) {
    static thread_local char buf[256];
    buf[0] = '\0';
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      nullptr, err, 0, buf, sizeof(buf), nullptr) == 0) {
        snprintf(buf, sizeof(buf), "WSA error %d", err);
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) buf[--len] = '\0';
    return buf;
}
}  // namespace
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
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#define SOCK_ERRNO errno
#endif

namespace tunngle {
namespace net {

namespace {

static std::string JsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 'u' && i + 5 < s.size()) {
                unsigned int cp = 0;
                for (int j = 0; j < 4; ++j) {
                    char c = s[i + 2 + j];
                    cp *= 16;
                    if (c >= '0' && c <= '9') cp += c - '0';
                    else if (c >= 'a' && c <= 'f') cp += c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') cp += c - 'A' + 10;
                }
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
                i += 5;
            } else if (s[i + 1] == '"') { out += '"'; i++; }
            else if (s[i + 1] == '\\') { out += '\\'; i++; }
            else if (s[i + 1] == 'n') { out += '\n'; i++; }
            else if (s[i + 1] == 'r') { out += '\r'; i++; }
            else if (s[i + 1] == 't') { out += '\t'; i++; }
            else out += s[i + 1], i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": \"";
        pos = json.find(search);
    }
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    std::string raw = json.substr(pos, end - pos);
    return JsonUnescape(raw);
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return 0;
    return std::atoi(json.c_str() + pos);
}

bool ExtractJsonBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos + 4 <= json.size() && json.compare(pos, 4, "true") == 0) return true;
    return false;
}

std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (static_cast<unsigned char>(c) >= 32 || c == '\t') out += c;
    }
    return out;
}

bool IsLocalHostName(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

std::string BuildJoinJson(const std::string& room, const std::string& nick, uint16_t udp_port,
                          bool is_public, int max_size,
                          const std::string& password, const std::string& genre,
                          const std::string& desc, const std::string& game_name,
                          const std::string& game_thumbnail, const std::string& voice_chat_url,
                          bool create_new, const std::string& auth_token) {
    std::string j;
    j += "{\"cmd\":\"join\",\"room\":\"" + JsonEscape(room) + "\",\"nick\":\"" +
         JsonEscape(nick.empty() ? "Player" : nick) + "\",\"udp_port\":" + std::to_string(udp_port) +
         ",\"public\":" + (is_public ? "true" : "false") + ",\"max_size\":" + std::to_string(max_size);
    if (create_new) j += ",\"create\":true";
    if (!auth_token.empty()) j += ",\"token\":\"" + JsonEscape(auth_token) + "\"";
    std::string client_ip = Config::Instance().GetClientIPOverride();
    if (!client_ip.empty()) j += ",\"client_ip\":\"" + JsonEscape(client_ip) + "\"";
    if (!password.empty()) j += ",\"password\":\"" + JsonEscape(password) + "\"";
    if (!genre.empty()) j += ",\"genre\":\"" + JsonEscape(genre) + "\"";
    if (!desc.empty()) j += ",\"desc\":\"" + JsonEscape(desc) + "\"";
    if (!game_name.empty()) j += ",\"game_name\":\"" + JsonEscape(game_name) + "\"";
    if (!game_thumbnail.empty()) j += ",\"game_thumbnail\":\"" + JsonEscape(game_thumbnail) + "\"";
    if (!voice_chat_url.empty()) j += ",\"voice_chat_url\":\"" + JsonEscape(voice_chat_url) + "\"";
    j += "}\n";
    return j;
}

void ParseRoomsArray(const std::string& json, std::vector<CoordRoomInfo>& out) {
    size_t key = json.find("\"rooms\"");
    if (key == std::string::npos) key = json.find("\"Rooms\"");
    if (key == std::string::npos) return;
    size_t arr = json.find('[', key);
    if (arr == std::string::npos) return;
    arr += 1;
    out.clear();
    size_t i = arr;
    while (i < json.size()) {
        size_t obj = json.find('{', i);
        if (obj == std::string::npos) break;
        size_t end = json.find('}', obj);
        if (end == std::string::npos) break;
        std::string obj_str = json.substr(obj, end - obj + 1);
        CoordRoomInfo info;
        info.name = ExtractJsonString(obj_str, "name");
        info.code = ExtractJsonString(obj_str, "code");
        info.description = ExtractJsonString(obj_str, "description");
        info.genre = ExtractJsonString(obj_str, "genre");
        info.peers = ExtractJsonInt(obj_str, "peers");
        info.max_size = ExtractJsonInt(obj_str, "max_size");
        info.has_password = ExtractJsonBool(obj_str, "has_password");
        info.game_name = ExtractJsonString(obj_str, "game_name");
        info.game_thumbnail = ExtractJsonString(obj_str, "game_thumbnail");
        info.voice_chat_url = ExtractJsonString(obj_str, "voice_chat_url");
        if (info.max_size <= 0) info.max_size = 8;
        if (!info.name.empty()) {
            out.push_back(info);
        }
        i = end + 1;
    }
}

void ParsePeersArray(const std::string& json, std::vector<CoordPeerInfo>& out,
                     const std::string& exclude_tun_ip) {
    size_t arr = json.find("\"peers\":[");
    if (arr == std::string::npos) return;
    arr += 9;
    out.clear();
    size_t i = arr;
    while (i < json.size()) {
        size_t obj = json.find('{', i);
        if (obj == std::string::npos) break;
        size_t end = json.find('}', obj);
        if (end == std::string::npos) break;
        std::string obj_str = json.substr(obj, end - obj + 1);
        CoordPeerInfo info;
        info.ip = ExtractJsonString(obj_str, "ip");
        info.port = static_cast<uint16_t>(ExtractJsonInt(obj_str, "port"));
        info.tun_ip = ExtractJsonString(obj_str, "tun_ip");
        info.nick = ExtractJsonString(obj_str, "nick");
        info.auth_token = ExtractJsonString(obj_str, "auth_token");
        if (!info.tun_ip.empty() && info.tun_ip != exclude_tun_ip) {
            out.push_back(info);
        }
        i = end + 1;
    }
}

}  // namespace

namespace {
#ifdef TUNNGLE_COORD_TLS
bool InitOpenSSLOnce() {
    static bool inited = false;
    if (!inited) {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        inited = true;
    }
    return true;
}
#endif
}  // namespace

CoordClient::CoordClient() = default;

CoordClient::~CoordClient() {
    Disconnect();
}

int CoordClient::RawSend(const void* buf, size_t len) {
#ifdef TUNNGLE_COORD_TLS
    if (ssl_) {
        int n = SSL_write(static_cast<SSL*>(ssl_), buf, static_cast<int>(len));
        return (n > 0) ? n : -1;
    }
#endif
#ifdef _WIN32
    return send(static_cast<SOCKET>(fd_), static_cast<const char*>(buf), static_cast<int>(len), 0);
#else
    return static_cast<int>(send(fd_, buf, len, 0));
#endif
}

int CoordClient::RawRecv(void* buf, size_t len, bool* would_block) {
#ifdef TUNNGLE_COORD_TLS
    if (ssl_) {
        SSL* s = static_cast<SSL*>(ssl_);
        int n = SSL_read(s, buf, static_cast<int>(len));
        if (n > 0) return n;
        if (n == 0) return 0;  // EOF
        int err = SSL_get_error(s, n);
        if (would_block && (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)) {
            *would_block = true;
        }
        return -1;
    }
#endif
#ifdef _WIN32
    int n = recv(static_cast<SOCKET>(fd_), static_cast<char*>(buf), static_cast<int>(len), 0);
    if (n == SOCKET_ERROR && would_block && WSAGetLastError() == WSAEWOULDBLOCK) {
        *would_block = true;
    }
    return (n == SOCKET_ERROR) ? -1 : n;
#else
    ssize_t n = recv(fd_, buf, len, 0);
    if (n < 0 && would_block && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        *would_block = true;
    }
    return (n < 0) ? -1 : static_cast<int>(n);
#endif
}

bool CoordClient::Connect(const std::string& host, uint16_t port, bool use_tls,
                         const std::string& trusted_cert_file) {
    if (fd_ >= 0) Disconnect();

    std::string host_clean = host;
    while (!host_clean.empty() && (host_clean.front() == ' ' || host_clean.front() == '\t'))
        host_clean.erase(0, 1);
    while (!host_clean.empty() && (host_clean.back() == ' ' || host_clean.back() == '\t'))
        host_clean.pop_back();
    if (host_clean.size() >= 6) {
        std::string scheme(host_clean.begin(), host_clean.begin() + 6);
        for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (scheme == "tls://") {
            use_tls = true;
            host_clean = host_clean.substr(6);
        }
    }

    const bool local_target = IsLocalHostName(host_clean);
    if (!use_tls && !local_target) {
        LOG_ERROR("Coord: refusing insecure non-TLS connection to %s:%u", host_clean.c_str(), port);
        return false;
    }
    size_t colon = host_clean.rfind(':');
    if (colon != std::string::npos && colon > 0) {
        std::string after = host_clean.substr(colon + 1);
        bool all_digits = !after.empty();
        for (char c : after) if (!std::isdigit(static_cast<unsigned char>(c))) { all_digits = false; break; }
        bool has_dot = (host_clean.find('.') != std::string::npos && host_clean.find('.') < colon);
        if (all_digits && has_dot) {
            host_clean = host_clean.substr(0, colon);
        }
    }

    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);
    if (getaddrinfo(host_clean.c_str(), port_str, &hints, &res) != 0) {
        LOG_ERROR("Coord: getaddrinfo failed for %s:%u", host_clean.c_str(), port);
        if (host_clean.find('.') == std::string::npos && host_clean != "localhost") {
            LOG_ERROR("Coord: Use full address: host:port or tls://host:port (e.g. tls://shinkansen.proxy.rlwy.net:44048)");
        }
        return false;
    }

#ifdef _WIN32
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    fd_ = (s == INVALID_SOCKET) ? -1 : static_cast<int>(s);
#else
    fd_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
#endif
    if (fd_ < 0) {
        LOG_ERROR("Coord: socket failed: %s", strerror(SOCK_ERRNO));
        freeaddrinfo(res);
        return false;
    }

#ifdef _WIN32
    if (connect(static_cast<SOCKET>(fd_), res->ai_addr, static_cast<int>(res->ai_addrlen)) == SOCKET_ERROR) {
        int err = SOCK_ERRNO;
        LOG_ERROR("Coord: connect failed: %s (code %d)", WinSockErrorStr(err), err);
        close(fd_);
        fd_ = -1;
        freeaddrinfo(res);
        return false;
    }
#else
    if (connect(fd_, res->ai_addr, res->ai_addrlen) < 0) {
        LOG_ERROR("Coord: connect failed: %s", strerror(SOCK_ERRNO));
        close(fd_);
        fd_ = -1;
        freeaddrinfo(res);
        return false;
    }
#endif
    freeaddrinfo(res);

#ifdef TUNNGLE_COORD_TLS
    if (use_tls) {
        if (!InitOpenSSLOnce()) {
            LOG_ERROR("Coord: OpenSSL init failed");
            close(fd_);
            fd_ = -1;
            return false;
        }
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            LOG_ERROR("Coord: SSL_CTX_new failed");
            close(fd_);
            fd_ = -1;
            return false;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        if (!trusted_cert_file.empty()) {
            if (SSL_CTX_load_verify_locations(ctx, trusted_cert_file.c_str(), nullptr) != 1) {
                LOG_ERROR("Coord: failed to load trusted cert file");
                SSL_CTX_free(ctx);
                close(fd_);
                fd_ = -1;
                return false;
            }
        }
        SSL* ssl = SSL_new(ctx);
        SSL_CTX_free(ctx);
        if (!ssl) {
            LOG_ERROR("Coord: SSL_new failed");
            close(fd_);
            fd_ = -1;
            return false;
        }
        SSL_set_fd(ssl, fd_);
        SSL_set_tlsext_host_name(ssl, host_clean.c_str());
        if (SSL_connect(ssl) <= 0) {
            LOG_ERROR("Coord: TLS handshake failed");
            SSL_free(ssl);
            close(fd_);
            fd_ = -1;
            return false;
        }
        ssl_ = ssl;
    }
#else
    if (use_tls) {
        LOG_ERROR("Coord: TLS requested but not built with OpenSSL. Use tls:// prefix only when server has TLS.");
        close(fd_);
        fd_ = -1;
        return false;
    }
#endif

#ifndef _WIN32
#ifdef SO_NOSIGPIPE
    int opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
#endif

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

    read_thread_running_ = true;
    std::thread(&CoordClient::ReadLoop, this).detach();

        LOG_INFO("Coord: connected to coordination server");
    return true;
}

bool CoordClient::Join(const std::string& room, const std::string& nick, uint16_t udp_port,
                       bool is_public, int max_size,
                       const std::string& password, const std::string& genre,
                       const std::string& desc, const std::string& game_name,
                       const std::string& game_thumbnail, const std::string& voice_chat_url,
                       bool create_new) {
    if (fd_ < 0) return false;

    std::string auth_token = Config::Instance().GetAuthToken();
    std::string line = BuildJoinJson(room, nick, udp_port, is_public, max_size,
                                     password, genre, desc, game_name, game_thumbnail, voice_chat_url, create_new, auth_token);
    int n = RawSend(line.data(), line.size());
    if (n != static_cast<int>(line.size())) {
        LOG_ERROR("Coord: send JOIN failed");
        return false;
    }
    last_error_.clear();
    return true;
}

void CoordClient::RequestRoomList() {
    if (fd_ < 0) return;
    const char* msg = "{\"cmd\":\"list\"}\n";
    RawSend(msg, strlen(msg));
}

void CoordClient::Leave() {
    if (fd_ < 0) return;
    const char* msg = "{\"cmd\":\"leave\"}\n";
    RawSend(msg, strlen(msg));
    in_room_ = false;
    chat_messages_.clear();
}

void CoordClient::SendChat(const std::string& text) {
    if (fd_ < 0 || !in_room_ || text.empty()) return;
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (char c : text) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (static_cast<unsigned char>(c) >= 32 || c == '\t') escaped += c;
    }
    if (escaped.size() > 512) escaped.resize(512);
    char buf[1024];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"chat\",\"text\":\"%s\"}\n", escaped.c_str());
    RawSend(buf, strlen(buf));
}

void CoordClient::Disconnect() {
    if (fd_ >= 0) {
        read_thread_running_ = false;
#ifdef TUNNGLE_COORD_TLS
        if (ssl_) {
            SSL* s = static_cast<SSL*>(ssl_);
            SSL_shutdown(s);
            SSL_free(s);
            ssl_ = nullptr;
        }
#endif
        close(fd_);
        fd_ = -1;
    }
    in_room_ = false;
    assigned_tun_ip_.clear();
    relay_addr_.clear();
    peers_.clear();
    room_list_.clear();
    chat_messages_.clear();
    current_room_info_ = CoordRoomInfo{};
    last_error_.clear();
}

void CoordClient::ReadLoop() {
    std::string buf;
    buf.reserve(4096);
    while (read_thread_running_ && fd_ >= 0) {
        char c;
        bool would_block = false;
        int n = RawRecv(&c, 1, &would_block);
        if (n < 0 && would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (n > 0) {
            if (c == '\n') {
                if (!buf.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    message_queue_.push_back(buf);
                    buf.clear();
                }
            } else {
                buf += c;
                if (buf.size() > 65536) buf.clear();
            }
        } else if (n == 0) {  // connection closed
            std::lock_guard<std::mutex> lock(mutex_);
            message_queue_.push_back("{\"cmd\":\"_disconnected\"}");
            break;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            message_queue_.push_back("{\"cmd\":\"_disconnected\"}");
            break;
        }
    }
}

void CoordClient::ProcessMessage(const std::string& line) {
    std::string cmd = ExtractJsonString(line, "cmd");
    if (cmd == "assigned") {
        assigned_tun_ip_ = ExtractJsonString(line, "tun_ip");
        relay_addr_ = ExtractJsonString(line, "relay_addr");
        relay_token_ = ExtractJsonString(line, "relay_token");
        std::string room_code = ExtractJsonString(line, "room_code");
        std::string room_name = ExtractJsonString(line, "room_name");
        std::string room_desc = ExtractJsonString(line, "room_description");
        std::string game_name = ExtractJsonString(line, "game_name");
        std::string game_thumbnail = ExtractJsonString(line, "game_thumbnail");
        std::string voice_chat_url = ExtractJsonString(line, "voice_chat_url");
        if (!room_code.empty()) current_room_info_.code = room_code;
        if (!room_name.empty()) current_room_info_.name = room_name;
        if (!room_desc.empty()) current_room_info_.description = room_desc;
        if (!game_name.empty()) current_room_info_.game_name = game_name;
        if (!game_thumbnail.empty()) current_room_info_.game_thumbnail = game_thumbnail;
        if (!voice_chat_url.empty()) current_room_info_.voice_chat_url = voice_chat_url;
        in_room_ = true;
        last_error_.clear();
        LOG_INFO("Coord: assigned TUN IP %s", assigned_tun_ip_.c_str());
        if (!relay_addr_.empty()) {
            LOG_INFO("Coord: relay available");
        }
    } else if (cmd == "peers") {
        ParsePeersArray(line, peers_, assigned_tun_ip_);
    } else if (cmd == "rooms") {
        ParseRoomsArray(line, room_list_);
    } else if (cmd == "chat") {
        CoordChatEntry e;
        e.nick = ExtractJsonString(line, "nick");
        e.text = ExtractJsonString(line, "text");
        if (!e.text.empty()) {
            chat_messages_.push_back(e);
            if (chat_messages_.size() > 100) {
                chat_messages_.erase(chat_messages_.begin());
            }
        }
    } else if (cmd == "error") {
        std::string msg = ExtractJsonString(line, "msg");
        last_error_ = msg;
        in_room_ = false;
        assigned_tun_ip_.clear();
        peers_.clear();
        LOG_ERROR("Coord: server error: %s", msg.c_str());
    } else if (cmd == "_disconnected") {
        Disconnect();
        LOG_INFO("Coord: server disconnected");
    }
}

void CoordClient::Pump() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(message_queue_);
    }
    for (const auto& line : batch) {
        ProcessMessage(line);
    }
}

}  // namespace net
}  // namespace tunngle
