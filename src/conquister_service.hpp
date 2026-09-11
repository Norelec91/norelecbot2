#ifndef NORELECBOT_CONQUISTER_SERVICE_HPP
#define NORELECBOT_CONQUISTER_SERVICE_HPP

#include "storage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace norelecbot {

enum class ClaimStatus { taken, already_held };

struct ClaimResult {
    ClaimStatus status = ClaimStatus::taken;
    std::string previous_username;
    std::int64_t earned = 0;
};

struct LeaderboardEntry {
    std::string username;
    std::int64_t score = 0;
    std::int64_t quotes_added = 0;
};

struct Holder {
    std::string username;
    std::int64_t since = 0;
};

struct Leaderboard {
    std::vector<LeaderboardEntry> entries;
    std::optional<Holder> current;
};

struct ConquisterUser {
    std::string username;
    std::int64_t score = 0;
    std::size_t rank = 0;
    std::int64_t quotes_added = 0;
    bool in_conquister = false;
    std::int64_t since = 0;
};

[[nodiscard]] ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now
);
/* limit 0 returns every entry. */
[[nodiscard]] Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit);
/* Case-insensitive lookup; rank is 0 when the user has no score yet. */
[[nodiscard]] std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username);

}

#endif
