#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tunngle {
namespace net {

struct FriendInfo {
    std::string user_id;
    std::string username;
    std::string display_name;
};

struct AuthResult {
    bool ok = false;
    std::string error;
    std::string token;
    std::string user_id;
    std::string username;
    std::string display_name;
};

struct ProfileResult {
    bool ok = false;
    std::string user_id;
    std::string username;
    std::string email;
    std::string display_name;
    std::string created_at;
    std::string last_login;
};

struct GameInfo {
    int id = 0;
    std::string name;
    std::string slug;
    std::string released;
    float rating = 0.0f;
    int metacritic = 0;
    int playtime = 0;
    std::string thumbnail;
    std::vector<std::string> genres;
    std::string genre_str;
};

struct GenreInfo {
    int id = 0;
    std::string name;
    std::string slug;
    int count = 0;
    std::string image;
};

struct ChatLobbyMessage {
    std::string nick;
    std::string text;
    std::string created_at;
};

class ApiClient {
public:
    explicit ApiClient(const std::string& base_url = "http://127.0.0.1:8080");

    void SetBaseURL(const std::string& url);

    // Auth
    AuthResult Register(const std::string& username, const std::string& password,
                        const std::string& email = "", const std::string& display_name = "");
    AuthResult Login(const std::string& username, const std::string& password);
    ProfileResult GetProfile();
    bool UpdateProfile(const std::string& display_name, const std::string& email);

    // Friends
    std::vector<FriendInfo> GetFriends();
    bool AddFriend(const std::string& username);
    bool RemoveFriend(const std::string& username);

    // Favorites
    std::vector<std::string> GetFavorites();
    bool AddFavorite(const std::string& room_name);
    bool RemoveFavorite(const std::string& room_name);

    // Games
    std::vector<GameInfo> SearchGames(const std::string& query);
    void SearchGamesAsync(const std::string& query);
    bool PollSearchResults(std::vector<GameInfo>& out);
    bool IsSearchingGames() const;
    std::vector<GenreInfo> GetGameGenres();
    void PrefetchGenresAsync();

    // Chat lobbies (General, LFG, Trading)
    std::vector<ChatLobbyMessage> GetChatLobbyMessages(const std::string& lobby_id);
    bool PostChatLobbyMessage(const std::string& lobby_id, const std::string& nick, const std::string& text);

    // Session
    bool IsLoggedIn() const;
    std::string GetToken() const;
    void RestoreSession(const std::string& token, const std::string& user_id,
                       const std::string& username, const std::string& display_name);
    bool ValidateSession();
    bool SessionExpired() const;
    void ClearExpiredFlag();
    std::string GetUsername() const;
    std::string GetDisplayName() const;
    std::string GetUserID() const;
    void Logout();

    // Cached data (call Refresh* to update from server)
    void RefreshFriends();
    void RefreshFavorites();
    const std::vector<FriendInfo>& CachedFriends() const { return cached_friends_; }
    const std::vector<std::string>& CachedFavorites() const { return cached_favorites_; }
    std::string LastError() const { return last_error_; }

private:
    std::string base_url_;
    std::string token_;
    std::string user_id_;
    std::string username_;
    std::string display_name_;

    std::vector<FriendInfo> cached_friends_;
    std::vector<std::string> cached_favorites_;
    std::string last_error_;
    bool session_expired_ = false;

    void HandleUnauthorized();

    mutable std::mutex mu_;

    // Light API cache for games/genres (5-10 min TTL)
    mutable std::vector<GenreInfo> cached_genres_;
    mutable std::chrono::steady_clock::time_point genres_expiry_;
    mutable std::unordered_map<std::string, std::pair<std::vector<GameInfo>, std::chrono::steady_clock::time_point>> search_cache_;
    static constexpr int kGenresTTLSec = 300;
    static constexpr int kSearchTTLSec = 300;

    // Async search state
    mutable std::mutex async_search_mu_;
    std::string async_search_requested_;   // Latest query user asked for
    std::string async_search_completed_for_;  // Query we have results for
    std::vector<GameInfo> async_search_results_;
    std::atomic<bool> async_search_in_progress_{false};
};

}  // namespace net
}  // namespace tunngle
