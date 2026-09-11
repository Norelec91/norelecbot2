#include "conquister_service.hpp"

#include "logging.hpp"
#include "text.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace norelecbot {
namespace {

bool ranks_before(const ScoreEntry &first, const ScoreEntry &second) {
    if (first.score != second.score) {
        return first.score > second.score;
    }
    return first.username < second.username;
}

}

ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now
) {
    StorageTransaction transaction{storage, StorageDocuments::state};
    ClaimResult result;
    const std::string holder{transaction.holder()};
    if (!holder.empty() && holder == username) {
        result.status = ClaimStatus::already_held;
        return result;
    }
    if (!holder.empty()) {
        result.previous_username = holder;
        const std::int64_t since = transaction.holder_since(now);
        result.earned = now > since ? now - since : 0;
        const std::int64_t score = transaction.score(holder);
        if (score > 0 && result.earned > std::numeric_limits<std::int64_t>::max() - score) {
            log_error("Could not update Conquister score");
            throw StorageError("Conquister score overflow");
        }
        transaction.set_score(holder, score + result.earned);
    }
    transaction.set_holder(user_id, username, now);
    transaction.commit();
    return result;
}

Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit) {
    const StorageTransaction transaction{storage, StorageDocuments::state};
    std::vector<ScoreEntry> scores = transaction.scores();
    std::ranges::sort(scores, ranks_before);
    if (limit != 0 && scores.size() > limit) {
        scores.resize(limit);
    }
    Leaderboard leaderboard;
    leaderboard.entries.reserve(scores.size());
    for (ScoreEntry &entry : scores) {
        const std::int64_t quotes_added = transaction.quotes_added(entry.username);
        leaderboard.entries.push_back({std::move(entry.username), entry.score, quotes_added});
    }
    if (const std::string_view holder = transaction.holder(); !holder.empty()) {
        leaderboard.current = Holder{std::string{holder}, transaction.holder_since(0)};
    }
    return leaderboard;
}

std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username) {
    const StorageTransaction transaction{storage, StorageDocuments::state};
    ConquisterUser user;
    if (const std::string_view holder = transaction.holder();
        !holder.empty() && text::equals_ignore_case(holder, username)) {
        user.username = holder;
        user.in_conquister = true;
        user.since = transaction.holder_since(0);
    }
    const std::optional<std::string> score_name = transaction.find_score(username);
    const std::optional<std::string> quotes_name = transaction.find_quotes_added(username);
    if (!user.in_conquister) {
        if (score_name) {
            user.username = *score_name;
        } else if (quotes_name) {
            user.username = *quotes_name;
        } else {
            return std::nullopt;
        }
    }
    if (score_name) {
        const ScoreEntry self{*score_name, transaction.score(*score_name)};
        const auto ahead = std::ranges::count_if(transaction.scores(), [&self](const ScoreEntry &other) {
            return ranks_before(other, self);
        });
        user.score = self.score;
        user.rank = static_cast<std::size_t>(ahead) + 1;
    }
    if (quotes_name) {
        user.quotes_added = transaction.quotes_added(*quotes_name);
    }
    return user;
}

}
