#include "api_client.h"

#include <httplib.h>

#include <algorithm>
#include <sstream>
#include <thread>

namespace tunngle {
namespace net {

namespace {

struct JsonVal {
    std::string raw;
    std::string GetString(const std::string& key) const {
        std::string needle = "\"" + key + "\"";
        auto pos = raw.find(needle);
        if (pos == std::string::npos) return "";
        pos = raw.find(':', pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t')) pos++;
        if (pos >= raw.size()) return "";
        if (raw[pos] == '"') {
            pos++;
            auto end = raw.find('"', pos);
            if (end == std::string::npos) return "";
            return raw.substr(pos, end - pos);
        }
        auto end = raw.find_first_of(",}\n", pos);
        return raw.substr(pos, end - pos);
    }

    std::vector<std::string> GetStringArray(const std::string& key) const {
        std::vector<std::string> out;
        std::string needle = "\"" + key + "\"";
        auto pos = raw.find(needle);
        if (pos == std::string::npos) return out;
        pos = raw.find('[', pos);
        if (pos == std::string::npos) return out;
        auto end = raw.find(']', pos);
        if (end == std::string::npos) return out;
        std::string arr = raw.substr(pos + 1, end - pos - 1);
        size_t i = 0;
        while (i < arr.size()) {
            auto q1 = arr.find('"', i);
            if (q1 == std::string::npos) break;
            auto q2 = arr.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            out.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
            i = q2 + 1;
        }
        return out;
    }

    std::vector<JsonVal> GetObjectArray(const std::string& key) const {
        std::vector<JsonVal> out;
        std::string needle = "\"" + key + "\"";
        auto pos = raw.find(needle);
        if (pos == std::string::npos) return out;
        pos = raw.find('[', pos);
        if (pos == std::string::npos) return out;
        pos++;
        int depth = 0;
        size_t obj_start = 0;
        for (size_t i = pos; i < raw.size(); i++) {
            if (raw[i] == '{') {
                if (depth == 0) obj_start = i;
                depth++;
            } else if (raw[i] == '}') {
                depth--;
                if (depth == 0) {
                    out.push_back({raw.substr(obj_start, i - obj_start + 1)});
                }
            } else if (raw[i] == ']' && depth == 0) {
                break;
            }
        }
        return out;
    }
};

std::string MakeJson(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::ostringstream ss;
    ss << "{";
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) ss << ",";
        ss << "\"" << fields[i].first << "\":\"" << fields[i].second << "\"";
    }
    ss << "}";
    return ss.str();
}

}  // namespace

ApiClient::ApiClient(const std::string& base_url) : base_url_(base_url) {}

void ApiClient::SetBaseURL(const std::string& url) {
    std::lock_guard<std::mutex> lk(mu_);
    base_url_ = url;
}

bool ApiClient::IsLoggedIn() const {
    std::lock_guard<std::mutex> lk(mu_);
    return !token_.empty();
}

std::string ApiClient::GetToken() const {
    std::lock_guard<std::mutex> lk(mu_);
    return token_;
}

void ApiClient::RestoreSession(const std::string& token, const std::string& user_id,
                               const std::string& username, const std::string& display_name) {
    std::lock_guard<std::mutex> lk(mu_);
    token_ = token;
    user_id_ = user_id;
    username_ = username;
    display_name_ = display_name;
}

bool ApiClient::ValidateSession() {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return false;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Get("/api/auth/profile", hdrs);
    if (!r || r->status != 200) {
        HandleUnauthorized();
        return false;
    }
    JsonVal j{r->body};
    std::lock_guard<std::mutex> lk(mu_);
    display_name_ = j.GetString("display_name");
    username_ = j.GetString("username");
    user_id_ = j.GetString("user_id");
    return true;
}

std::string ApiClient::GetUsername() const {
    std::lock_guard<std::mutex> lk(mu_);
    return username_;
}

std::string ApiClient::GetDisplayName() const {
    std::lock_guard<std::mutex> lk(mu_);
    return display_name_;
}

std::string ApiClient::GetUserID() const {
    std::lock_guard<std::mutex> lk(mu_);
    return user_id_;
}

void ApiClient::Logout() {
    std::lock_guard<std::mutex> lk(mu_);
    token_.clear();
    user_id_.clear();
    username_.clear();
    display_name_.clear();
    cached_friends_.clear();
    cached_favorites_.clear();
    session_expired_ = false;
}

void ApiClient::HandleUnauthorized() {
    std::lock_guard<std::mutex> lk(mu_);
    token_.clear();
    session_expired_ = true;
    last_error_ = "Session expired, please sign in again";
}

bool ApiClient::SessionExpired() const {
    std::lock_guard<std::mutex> lk(mu_);
    return session_expired_;
}

void ApiClient::ClearExpiredFlag() {
    std::lock_guard<std::mutex> lk(mu_);
    session_expired_ = false;
}

AuthResult ApiClient::Register(const std::string& username, const std::string& password,
                                const std::string& email, const std::string& display_name) {
    AuthResult res;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    std::vector<std::pair<std::string, std::string>> fields = {
        {"username", username}, {"password", password}
    };
    if (!email.empty()) fields.push_back({"email", email});
    if (!display_name.empty()) fields.push_back({"display_name", display_name});

    auto r = cli.Post("/api/auth/register", MakeJson(fields), "application/json");
    if (!r) {
        res.error = "Connection failed";
        last_error_ = res.error;
        return res;
    }
    JsonVal j{r->body};
    if (r->status != 200) {
        res.error = j.GetString("error");
        if (res.error.empty()) res.error = "Registration failed";
        last_error_ = res.error;
        return res;
    }

    res.ok = true;
    res.token = j.GetString("token");
    res.user_id = j.GetString("user_id");
    res.username = j.GetString("username");
    res.display_name = j.GetString("display_name");

    std::lock_guard<std::mutex> lk(mu_);
    token_ = res.token;
    user_id_ = res.user_id;
    username_ = res.username;
    display_name_ = res.display_name;
    last_error_.clear();
    return res;
}

AuthResult ApiClient::Login(const std::string& username, const std::string& password) {
    AuthResult res;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    auto r = cli.Post("/api/auth/login",
                      MakeJson({{"username", username}, {"password", password}}),
                      "application/json");
    if (!r) {
        res.error = "Connection failed";
        last_error_ = res.error;
        return res;
    }
    JsonVal j{r->body};
    if (r->status != 200) {
        res.error = j.GetString("error");
        if (res.error.empty()) res.error = "Login failed";
        last_error_ = res.error;
        return res;
    }

    res.ok = true;
    res.token = j.GetString("token");
    res.user_id = j.GetString("user_id");
    res.username = j.GetString("username");
    res.display_name = j.GetString("display_name");

    std::lock_guard<std::mutex> lk(mu_);
    token_ = res.token;
    user_id_ = res.user_id;
    username_ = res.username;
    display_name_ = res.display_name;
    last_error_.clear();
    return res;
}

ProfileResult ApiClient::GetProfile() {
    ProfileResult res;
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return res;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Get("/api/auth/profile", hdrs);
    if (r && r->status == 401) { HandleUnauthorized(); return res; }
    if (!r || r->status != 200) return res;
    JsonVal j{r->body};
    res.ok = true;
    res.user_id = j.GetString("user_id");
    res.username = j.GetString("username");
    res.email = j.GetString("email");
    res.display_name = j.GetString("display_name");
    res.created_at = j.GetString("created_at");
    res.last_login = j.GetString("last_login");
    return res;
}

bool ApiClient::UpdateProfile(const std::string& display_name, const std::string& email) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return false;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Put("/api/auth/profile",
                     hdrs,
                     MakeJson({{"display_name", display_name}, {"email", email}}),
                     "application/json");
    if (r && r->status == 401) { HandleUnauthorized(); return false; }
    return r && r->status == 200;
}

std::vector<FriendInfo> ApiClient::GetFriends() {
    std::vector<FriendInfo> out;
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return out;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Get("/api/friends", hdrs);
    if (r && r->status == 401) { HandleUnauthorized(); return out; }
    if (!r || r->status != 200) return out;
    JsonVal j{r->body};
    for (const auto& obj : j.GetObjectArray("friends")) {
        FriendInfo f;
        f.user_id = obj.GetString("user_id");
        f.username = obj.GetString("username");
        f.display_name = obj.GetString("display_name");
        out.push_back(f);
    }
    return out;
}

bool ApiClient::AddFriend(const std::string& username) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) {
        last_error_ = "Sign in to add friends";
        return false;
    }
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Post(("/api/friends/" + username).c_str(), hdrs, "", "application/json");
    if (r && r->status == 200) {
        RefreshFriends();
        return true;
    }
    if (r && r->status == 401) {
        HandleUnauthorized();
        return false;
    }
    if (r) {
        JsonVal j{r->body};
        last_error_ = j.GetString("error");
    } else {
        last_error_ = "Could not reach server";
    }
    return false;
}

bool ApiClient::RemoveFriend(const std::string& username) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return false;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Delete(("/api/friends/" + username).c_str(), hdrs);
    if (r && r->status == 401) { HandleUnauthorized(); return false; }
    if (r && r->status == 200) {
        RefreshFriends();
        return true;
    }
    return false;
}

std::vector<std::string> ApiClient::GetFavorites() {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return {};
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Get("/api/favorites", hdrs);
    if (r && r->status == 401) { HandleUnauthorized(); return {}; }
    if (!r || r->status != 200) return {};
    JsonVal j{r->body};
    return j.GetStringArray("favorites");
}

bool ApiClient::AddFavorite(const std::string& room_name) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return false;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Post("/api/favorites", hdrs,
                      MakeJson({{"room_name", room_name}}), "application/json");
    if (r && r->status == 401) { HandleUnauthorized(); return false; }
    if (r && r->status == 200) {
        RefreshFavorites();
        return true;
    }
    return false;
}

bool ApiClient::RemoveFavorite(const std::string& room_name) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    if (tok.empty()) return false;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    httplib::Headers hdrs = {{"Authorization", "Bearer " + tok}};
    auto r = cli.Delete(("/api/favorites/" + room_name).c_str(), hdrs);
    if (r && r->status == 401) { HandleUnauthorized(); return false; }
    if (r && r->status == 200) {
        RefreshFavorites();
        return true;
    }
    return false;
}

void ApiClient::RefreshFriends() {
    auto f = GetFriends();
    std::lock_guard<std::mutex> lk(mu_);
    cached_friends_ = std::move(f);
}

void ApiClient::RefreshFavorites() {
    auto f = GetFavorites();
    std::lock_guard<std::mutex> lk(mu_);
    cached_favorites_ = std::move(f);
}

namespace {
int FindMatchingBrace(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); i++) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}') { depth--; if (depth == 0) return static_cast<int>(i); }
    }
    return -1;
}

std::vector<std::string> ExtractStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return out;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return out;
    auto end = json.find(']', pos);
    if (end == std::string::npos) return out;
    std::string arr = json.substr(pos + 1, end - pos - 1);
    size_t i = 0;
    while (i < arr.size()) {
        auto q1 = arr.find('"', i);
        if (q1 == std::string::npos) break;
        auto q2 = arr.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        out.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

float ExtractFloat(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return 0.0f;
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    return static_cast<float>(std::atof(json.c_str() + pos));
}

std::vector<GameInfo> FetchAndParseGames(const std::string& base_url, const std::string& query) {
    std::vector<GameInfo> out;
    httplib::Client cli(base_url);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(10);
    std::string path = "/api/games/search?q=" + query;
    auto res = cli.Get(path);
    if (!res || res->status != 200) return out;

    const std::string& body = res->body;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t obj = body.find('{', pos);
        if (obj == std::string::npos) break;
        int close = FindMatchingBrace(body, obj);
        if (close < 0) break;
        std::string chunk = body.substr(obj, close - obj + 1);
        JsonVal v{chunk};
        GameInfo g;
        g.name = v.GetString("name");
        if (g.name.empty()) { pos = close + 1; continue; }
        std::string id_s = v.GetString("id");
        g.id = id_s.empty() ? 0 : std::atoi(id_s.c_str());
        g.slug = v.GetString("slug");
        g.released = v.GetString("released");
        g.rating = ExtractFloat(chunk, "rating");
        std::string mc = v.GetString("metacritic");
        g.metacritic = mc.empty() ? 0 : std::atoi(mc.c_str());
        std::string pt = v.GetString("playtime");
        g.playtime = pt.empty() ? 0 : std::atoi(pt.c_str());
        g.thumbnail = v.GetString("thumbnail");
        g.genres = ExtractStringArray(chunk, "genres");
        for (size_t i = 0; i < g.genres.size(); i++) {
            if (i > 0) g.genre_str += ", ";
            g.genre_str += g.genres[i];
        }
        if (g.genre_str.empty()) g.genre_str = "Unknown";
        out.push_back(g);
        pos = close + 1;
    }
    return out;
}
}  // namespace

std::vector<GameInfo> ApiClient::SearchGames(const std::string& query) {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = search_cache_.find(query);
        if (it != search_cache_.end() &&
            now - it->second.second < std::chrono::seconds(kSearchTTLSec)) {
            return it->second.first;
        }
    }
    std::vector<GameInfo> out = FetchAndParseGames(base_url_, query);
    {
        std::lock_guard<std::mutex> lock(mu_);
        search_cache_[query] = {out, std::chrono::steady_clock::now()};
    }
    return out;
}

void ApiClient::SearchGamesAsync(const std::string& query) {
    auto now = std::chrono::steady_clock::now();
    std::vector<GameInfo> cached;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = search_cache_.find(query);
        if (it != search_cache_.end() &&
            now - it->second.second < std::chrono::seconds(kSearchTTLSec)) {
            cached = it->second.first;
        }
    }
    if (!cached.empty()) {
        std::lock_guard<std::mutex> lock(async_search_mu_);
        async_search_requested_ = query;
        async_search_completed_for_ = query;
        async_search_results_ = std::move(cached);
        async_search_in_progress_.store(false);
        return;
    }
    std::string base_url;
    {
        std::lock_guard<std::mutex> lock(mu_);
        base_url = base_url_;
    }
    {
        std::lock_guard<std::mutex> lock(async_search_mu_);
        async_search_requested_ = query;
        async_search_completed_for_.clear();
        async_search_results_.clear();
    }
    async_search_in_progress_.store(true);
    std::thread([this, base_url, query]() {
        std::vector<GameInfo> results = FetchAndParseGames(base_url, query);
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!results.empty()) {
                search_cache_[query] = {results, std::chrono::steady_clock::now()};
            }
        }
        {
            std::lock_guard<std::mutex> lock(async_search_mu_);
            if (async_search_requested_ == query) {
                async_search_completed_for_ = query;
                async_search_results_ = std::move(results);
            }
        }
        async_search_in_progress_.store(false);
    }).detach();
}

bool ApiClient::PollSearchResults(std::vector<GameInfo>& out) {
    std::lock_guard<std::mutex> lock(async_search_mu_);
    if (async_search_completed_for_.empty() || async_search_completed_for_ != async_search_requested_) {
        return false;
    }
    out = std::move(async_search_results_);
    async_search_completed_for_.clear();
    async_search_results_.clear();
    return true;
}

bool ApiClient::IsSearchingGames() const {
    return async_search_in_progress_.load();
}

void ApiClient::PrefetchGenresAsync() {
    std::thread([this]() { (void)GetGameGenres(); }).detach();
}

namespace {
std::vector<JsonVal> ParseGenresArray(const std::string& body) {
    std::vector<JsonVal> out;
    size_t arr_start = body.find('[');
    if (arr_start == std::string::npos) return out;
    size_t pos = arr_start + 1;
    int depth = 0;
    size_t obj_start = 0;
    for (size_t i = pos; i < body.size(); i++) {
        if (body[i] == '{') {
            if (depth == 0) obj_start = i;
            depth++;
        } else if (body[i] == '}') {
            depth--;
            if (depth == 0) {
                out.push_back({body.substr(obj_start, i - obj_start + 1)});
            }
        } else if (body[i] == ']' && depth == 0) {
            break;
        }
    }
    return out;
}
}  // namespace

std::vector<GenreInfo> ApiClient::GetGameGenres() {
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!cached_genres_.empty() &&
            now - genres_expiry_ < std::chrono::seconds(kGenresTTLSec)) {
            return cached_genres_;
        }
    }
    std::vector<GenreInfo> out;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(10);
    auto res = cli.Get("/api/games/genres");
    if (!res || res->status != 200) return out;

    const std::string& body = res->body;
    std::vector<JsonVal> arr;
    if (!body.empty() && body[0] == '[') {
        arr = ParseGenresArray(body);
    } else if (body.find("\"results\"") != std::string::npos) {
        JsonVal root{body};
        arr = root.GetObjectArray("results");
    }
    for (const auto& v : arr) {
        GenreInfo g;
        g.name = v.GetString("name");
        if (g.name.empty()) continue;
        std::string id_s = v.GetString("id");
        g.id = id_s.empty() ? 0 : std::atoi(id_s.c_str());
        g.slug = v.GetString("slug");
        std::string cnt = v.GetString("count");
        g.count = cnt.empty() ? 0 : std::atoi(cnt.c_str());
        g.image = v.GetString("image");
        out.push_back(g);
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        cached_genres_ = out;
        genres_expiry_ = std::chrono::steady_clock::now();
    }
    return out;
}

std::vector<ChatLobbyMessage> ApiClient::GetChatLobbyMessages(const std::string& lobby_id) {
    std::vector<ChatLobbyMessage> out;
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    std::string path = "/api/chat-lobbies/" + lobby_id + "/messages";
    auto res = cli.Get(path);
    if (!res || res->status != 200) return out;

    const std::string& body = res->body;
    JsonVal root{body};
    auto arr = root.GetObjectArray("messages");
    for (const auto& v : arr) {
        ChatLobbyMessage m;
        m.nick = v.GetString("nick");
        m.text = v.GetString("text");
        m.created_at = v.GetString("created_at");
        if (!m.nick.empty() || !m.text.empty()) {
            out.push_back(m);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

bool ApiClient::PostChatLobbyMessage(const std::string& lobby_id, const std::string& nick,
                                     const std::string& text) {
    std::string tok;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tok = token_;
    }
    httplib::Client cli(base_url_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    std::string path = "/api/chat-lobbies/" + lobby_id + "/messages";
    std::ostringstream ss;
    ss << "{\"nick\":\"";
    for (char c : nick) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c >= 32 || c == '\t') ss << c;
    }
    ss << "\",\"text\":\"";
    for (char c : text) {
        if (c == '"') ss << "\\\"";
        else if (c == '\\') ss << "\\\\";
        else if (c == '\n') ss << "\\n";
        else if (c >= 32 || c == '\t') ss << c;
    }
    ss << "\"}";
    httplib::Headers hdrs;
    if (!tok.empty()) hdrs.emplace("Authorization", "Bearer " + tok);
    auto res = cli.Post(path, hdrs, ss.str(), "application/json");
    if (res && res->status == 401) { HandleUnauthorized(); return false; }
    return res && res->status == 200;
}

}  // namespace net
}  // namespace tunngle
