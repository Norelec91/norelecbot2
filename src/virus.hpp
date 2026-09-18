#ifndef NORELECBOT_VIRUS_HPP
#define NORELECBOT_VIRUS_HPP

#include "storage.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace norelecbot {

/* One doronzo for every so many players, and never fewer than one. */
inline constexpr std::size_t virus_healthy_per_doronzo = 4;
/* A vaccine is handed out every so many infections. */
inline constexpr std::int64_t virus_infections_per_vaccine = 4;

enum class VirusAction { infect, shoot, cure };

enum class VirusStatus {
    done,
    not_running,
    already_running,
    too_few_players,
    not_playing,
    dead,
    too_soon,
    wrong_role,
    no_vaccine,
    unknown_target,
    target_dead,
    oneself,
};

/* What is left to say after a move: who it hit, what he turned out to be, and how it ended. */
struct VirusOutcome {
    VirusStatus status = VirusStatus::done;
    std::string target;
    /* What the target was before the move, for the ones that reveal it. */
    bool target_was_doronzo = false;
    bool target_died = false;
    std::int64_t palle_lost = 0;
    std::int64_t wait_seconds = 0;
    /* A vaccine handed out by this move, and who got it. */
    std::string vaccinated;
    /* Set when the move ended the game. */
    bool over = false;
    bool doronzi_won = false;
    std::size_t doronzi_left = 0;
    std::size_t healthy_left = 0;
};

struct VirusStart {
    VirusStatus status = VirusStatus::done;
    std::size_t players = 0;
    std::size_t doronzi = 0;
    /* Everyone in the game, so each can be told in private what he is. */
    std::vector<std::pair<std::string, bool>> roles;
};

struct VirusReport {
    bool running = false;
    std::size_t alive = 0;
    std::size_t dead = 0;
    std::int64_t infections = 0;
    std::vector<std::string> fallen;
};

[[nodiscard]] VirusStart virus_start(Storage &storage, std::int64_t now);
[[nodiscard]] VirusOutcome virus_move(
    Storage &storage,
    const std::string &username,
    std::string_view target,
    VirusAction action,
    std::int64_t now,
    int cooldown_seconds
);
[[nodiscard]] VirusReport virus_report(Storage &storage);
/* What a player is, for the message only he reads; nothing when he is not in the game. */
[[nodiscard]] std::optional<VirusPlayer> virus_role(Storage &storage, const std::string &username);

}

#endif
