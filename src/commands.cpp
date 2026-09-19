#include "commands.hpp"

#include "game.hpp"
#include "mishaps.hpp"
#include "position.hpp"
#include "quiz.hpp"
#include "text.hpp"
#include "virus.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <cctype>
#include <chrono>
#include <exception>
#include <format>
#include <limits>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t leaderboard_size = 10;
constexpr std::string_view internal_error_reply = "Errore interno: riprova tra poco.";

using CommandHandler = std::string (*)(const CommandContext &context, std::string_view argument);

struct CommandDefinition {
    std::string_view name;
    CommandHandler handler;
};

struct ParsedCommand {
    std::string name;
    std::string_view argument;
};

/* The name in "We @someone", or in "We someone" where nobody writes the @; nothing otherwise. */
std::string_view raid_target(std::string_view message) {
    if (message == conquister_trigger) {
        return {};
    }
    std::string_view target;
    if (message.starts_with(raid_trigger)) {
        target = message.substr(raid_trigger.size());
    } else if (message.starts_with("We ")) {
        target = message.substr(3);
    } else {
        return {};
    }
    const bool one_name = !target.empty() && target.find_first_of(" \t\r\n@") == std::string_view::npos;
    return one_name ? target : std::string_view{};
}

ParsedCommand parse_command(std::string_view message) {
    const std::size_t length = std::min(message.find_first_of(" \t\r\n"), message.size());
    std::string name{message.substr(0, length)};
    if (const std::size_t suffix = name.find('@'); suffix != std::string::npos) {
        name.resize(suffix);
    }
    std::ranges::transform(name, name.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return {std::move(name), text::trim(message.substr(length))};
}

/* What made this hold worth more or less than the seconds it lasted. */
std::string hold_note(
    const CommandContext &context,
    const std::string &holder,
    std::int64_t boost_multiplier,
    int zodiac_percent,
    std::int64_t now
) {
    std::string note;
    if (boost_multiplier > 0) {
        note += std::format(" col boost x{}", boost_multiplier);
    }
    if (zodiac_percent != 100) {
        const zodiac::Sign sign = zodiac::sign_of(holder, context.config.zodiac_signs);
        note += std::format(
            "{} {} {} nel giorno di {} (x{}.{:02})",
            note.empty() ? "" : " e",
            sign.symbol,
            sign.name,
            zodiac::element_name(zodiac::element_of_day(now)),
            zodiac_percent / 100,
            zodiac_percent % 100
        );
    }
    return note;
}

std::int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string format_wait(std::int64_t seconds) {
    const std::int64_t minutes = seconds / 60;
    const std::int64_t rest = seconds % 60;
    if (minutes == 0) {
        return std::format("{} second{}", rest, rest == 1 ? "o" : "i");
    }
    if (rest == 0) {
        return std::format("{} minut{}", minutes, minutes == 1 ? "o" : "i");
    }
    return std::format(
        "{} minut{} e {} second{}",
        minutes,
        minutes == 1 ? "o" : "i",
        rest,
        rest == 1 ? "o" : "i"
    );
}

std::string missing_username_reply() {
    return std::format("Imposta uno username Telegram per giocare a {}.", conquister_place);
}

// The claim is already saved: a missing or unreadable quote must not turn the reply into an error.
std::optional<std::string> optional_random_quote(Storage &storage) {
    try {
        return quote_random(storage);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

/* A few players have a balloon nobody else can pop: the owner asked for it, for them alone. */
bool is_shielded(const CommandContext &context, const std::string &username) {
    return std::ranges::any_of(context.config.shield_users, [&username](const std::string &shielded) {
        return text::equals_ignore_case(shielded, username);
    });
}

/* What a failed attempt cost the one who made it. */
std::string failed_attempt_toll(const ClaimResult &result) {
    if (result.attack_cost > 0 && result.penalty_seconds > 0) {
        return std::format(
            " e ti costa {} palle e {} di penalità",
            result.attack_cost,
            format_wait(result.penalty_seconds)
        );
    }
    if (result.attack_cost > 0) {
        return std::format(" e ti costa {} palle", result.attack_cost);
    }
    if (result.penalty_seconds > 0) {
        return std::format(" e prendi {} di penalità", format_wait(result.penalty_seconds));
    }
    return {};
}

/* What the owner configured, before anybody shuffled it. */
Rules configured(const AppConfig &config) {
    return Rules{
        .quote_cost = config.quote_cost,
        .balloon_cost = config.balloon_cost,
        .boost_cost = config.boost_cost,
        .boost_multiplier = config.boost_multiplier,
        .raid_share = config.raid_share,
        .travel_divisor = config.travel_divisor,
        .attack_cost = config.attack_cost,
        .cooldown_seconds = config.cooldown_seconds,
    };
}

Rules rules_of(const CommandContext &context) {
    return rules_now(context.storage, configured(context.config));
}

RaidRules raid_rules(const CommandContext &context) {
    const Rules rules = rules_of(context);
    return {
        .travel_divisor = static_cast<int>(rules.travel_divisor),
        .loot_share = static_cast<int>(rules.raid_share),
        .attack_cost = static_cast<int>(rules.attack_cost),
        .signs = context.config.zodiac_signs,
        .shadowed = context.config.shadowed,
    };
}

/* Written without the @, a name nobody plays under is somebody talking, not a raid. */
std::optional<std::string> handle_raid(const CommandContext &context, std::string_view target, bool tagged) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    /* His own place is named after him, with the mention only where it reaches him. */
    const std::string home =
        std::format("{}{}", context.user_id != 0 ? "@" : "", username);
    const RaidResult result =
        raid_start(context.storage, context.user_id, username, target, seconds_now(), raid_rules(context));
    if (result.status == RaidStatus::unknown_target && !tagged) {
        return std::nullopt;
    }
    switch (result.status) {
    case RaidStatus::already_travelling:
        return std::format("🚀 {} sei già in viaggio, torni tra {}.", username, format_wait(result.seconds));
    case RaidStatus::holding_place:
        return std::format("🚀 {} sei in {} e da lì non si parte.", username, conquister_place);
    case RaidStatus::unknown_target:
        return std::format("🚀 {} non conosco nessun giocatore di nome {}.", username, target);
    case RaidStatus::left_place:
        return std::format(
            "🪐 {} sei tornato da {} in {} con {} palle{}.",
            username,
            conquister_place,
            home,
            result.earned,
            hold_note(context, username, result.boost_multiplier, result.zodiac_percent, seconds_now())
        );
    case RaidStatus::coming_home:
        return std::format(
            "🚀 {} lasci perdere e torni in {}: arrivi tra {}.",
            username,
            home,
            format_wait(result.seconds)
        );
    case RaidStatus::home_already:
        return std::format("🪐 {} sei già in {}!", username, home);
    case RaidStatus::started:
        break;
    }
    if (result.seconds <= position::shortest_travel) {
        return std::format(
            "🚚 {} ritira di persona da {}: è in zona, ci arriva in {}. {} resta scoperto.",
            username,
            result.target,
            format_wait(result.seconds),
            username
        );
    }
    return raid_started_reply(username, result.target, result.seconds);
}

std::string handle_claim(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const std::int64_t now = seconds_now();
    const ClaimResult result =
        conquister_claim(
            context.storage,
            context.user_id,
            username,
            now,
            ClaimRules{
                .cooldown_seconds = static_cast<int>(rules_of(context).cooldown_seconds),
                .attack_cost = static_cast<int>(rules_of(context).attack_cost),
                .ignores_shield = is_shielded(context, username),
                .signs = context.config.zodiac_signs,
            }
        );
    /* A name that came from IRC must not be written as a mention: on Telegram it would tag a stranger. */
    const std::string_view mention = result.previous_user_id != 0 ? "@" : "";
    if (result.status == ClaimStatus::travelling) {
        return std::format(
            "🚀 {} sei per strada: non puoi entrare in {} prima di tornare in {}{}, tra {}.",
            username,
            conquister_place,
            context.user_id != 0 ? "@" : "",
            username,
            format_wait(result.travel_seconds)
        );
    }
    if (result.status == ClaimStatus::cooldown) {
        return std::format("⏳ {} hai ancora {} di penalità.", username, format_wait(result.penalty_seconds));
    }
    if (result.status == ClaimStatus::already_held) {
        return std::format("{} sei già in {}!", username, conquister_place);
    }
    if (result.status == ClaimStatus::defended) {
        const std::string toll = failed_attempt_toll(result);
        if (result.shield_seconds > 0) {
            return std::format(
                "🎈 {} il palloncino di {} ha resistito{}. Resiste ancora per {}.",
                username,
                result.previous_username,
                toll,
                format_wait(result.shield_seconds)
            );
        }
        return std::format(
            "🎈 {} il palloncino di {} ha resistito{}. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato.",
            username,
            result.previous_username,
            toll,
            result.next_chance
        );
    }
    std::string reply;
    if (result.balloon_popped) {
        reply = std::format(
            "💥 {} hai bucato il palloncino di {}{}!\n",
            username,
            mention,
            result.previous_username
        );
    }
    if (!result.previous_username.empty()) {
        reply += std::format(
            "{0} hai cacciato {4}{1} da {2}.\n{1} hai guadagnato {3} palle{5}!\n",
            username,
            result.previous_username,
            conquister_place,
            result.earned,
            mention,
            hold_note(context, result.previous_username, result.boost_multiplier, result.zodiac_percent, now)
        );
    }
    reply += std::format("🪐 {} sei in {}!", username, conquister_place);
    if (const std::optional<std::string> quote = optional_random_quote(context.storage)) {
        reply += std::format("\n\n{}", *quote);
    }
    return reply;
}

std::string handle_leaderboard(const CommandContext &context, std::string_view) {
    const Leaderboard leaderboard = conquister_leaderboard(context.storage, leaderboard_size);
    if (leaderboard.entries.empty()) {
        return std::format(
            "Classifica vuota. Scrivi \"{}\" per entrare in {}!",
            conquister_trigger,
            conquister_place
        );
    }
    std::string reply = std::format(
        "🏆 Classifica {}\nOggi è giorno di {}.\n",
        conquister_place,
        zodiac::element_name(zodiac::element_of_day(seconds_now()))
    );
    for (std::size_t position = 1; const LeaderboardEntry &entry : leaderboard.entries) {
        reply += std::format(
            "\n{}) {} {} — {} palle",
            position++,
            zodiac::sign_of(entry.username, context.config.zodiac_signs).symbol,
            entry.username,
            entry.score
        );
        if (entry.quotes_added > 0) {
            reply += std::format(
                " — 📜 {} {}",
                entry.quotes_added,
                entry.quotes_added == 1 ? "citazione" : "citazioni"
            );
        }
    }
    if (leaderboard.current) {
        reply += std::format("\n\n🪐 In {} ora: {}", conquister_place, leaderboard.current->username);
    }
    return reply;
}

void tell_only_him(const CommandContext &context, std::string_view text) {
    if (context.whisper) {
        context.whisper(context.username, text);
    }
}

std::string_view virus_refusal(VirusStatus status) {
    switch (status) {
    case VirusStatus::not_running:
        return "🦠 Non c'è nessuna epidemia in corso.";
    case VirusStatus::not_playing:
        return "🦠 Non sei in partita: questa epidemia è cominciata senza di te.";
    case VirusStatus::dead:
        return "⚰️ Sei morto. Da lì si guarda e basta.";
    case VirusStatus::wrong_role:
        return "🦠 Non è una cosa che puoi fare tu.";
    case VirusStatus::no_vaccine:
        return "💉 Non hai nessuna fiala.";
    case VirusStatus::unknown_target:
        return "🦠 Quel nome non è in partita.";
    case VirusStatus::target_dead:
        return "⚰️ Quello è già morto.";
    case VirusStatus::oneself:
        return "🦠 Su te stesso no.";
    case VirusStatus::already_running:
        return "🦠 C'è già un'epidemia in corso.";
    case VirusStatus::too_few_players:
        return "🦠 Servono almeno due giocatori.";
    case VirusStatus::too_soon:
    case VirusStatus::done:
        break;
    }
    return {};
}

std::optional<std::string> handle_virus_move(
    const CommandContext &context,
    std::string_view argument,
    VirusAction action
) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const std::string_view target = argument.starts_with('@') ? argument.substr(1) : argument;
    const VirusOutcome outcome = virus_move(
        context.storage,
        username,
        target,
        action,
        seconds_now(),
        context.config.virus_cooldown_seconds
    );
    if (outcome.status == VirusStatus::too_soon) {
        tell_only_him(context, std::format("⏳ Puoi muoverti di nuovo tra {}.", format_wait(outcome.wait_seconds)));
        return std::nullopt;
    }
    if (outcome.status != VirusStatus::done) {
        /* Whispered: said out loud, a refusal would tell everybody what he is. */
        tell_only_him(context, virus_refusal(outcome.status));
        return std::nullopt;
    }

    std::string reply;
    switch (action) {
    case VirusAction::infect:
        reply = outcome.target_was_doronzo
            ? std::format("🦠 {} ha provato a infettare {}, che era già un doronzo.", username, outcome.target)
            : std::format("🦠 {} ha infettato {}: adesso è un doronzo.", username, outcome.target);
        break;
    case VirusAction::shoot:
        reply = std::format(
            "🔫 {} ha sparato a {}, che era {} e perde {} palle.",
            username,
            outcome.target,
            outcome.target_was_doronzo ? "un doronzo" : "sano",
            outcome.palle_lost
        );
        break;
    case VirusAction::cure:
        reply = outcome.target_was_doronzo
            ? std::format("💉 {} ha vaccinato {}: è tornato sano.", username, outcome.target)
            : std::format("💉 {} ha vaccinato {}, che stava benissimo.", username, outcome.target);
        break;
    }
    if (!outcome.vaccinated.empty()) {
        reply += "\n💉 Una fiala di vaccino è stata consegnata a un sano.";
        if (context.whisper) {
            context.whisper(
                outcome.vaccinated,
                "💉 Hai ricevuto una fiala: /vaccina <nome> riporta sano un doronzo."
            );
        }
    }
    if (outcome.over) {
        reply += outcome.doronzi_won
            ? "\n🏁 Non è rimasto nessun sano: VINCONO I DORONZI."
            : "\n🏁 Non è rimasto nessun doronzo: VINCONO I SANI.";
    } else {
        reply += std::format("\n🦠 In piedi: {} contagiati e {} sani.", outcome.doronzi_left, outcome.healthy_left);
    }
    return reply;
}

std::string handle_infect(const CommandContext &context, std::string_view argument) {
    return handle_virus_move(context, argument, VirusAction::infect).value_or(std::string{});
}

std::string handle_shoot(const CommandContext &context, std::string_view argument) {
    return handle_virus_move(context, argument, VirusAction::shoot).value_or(std::string{});
}

std::string handle_cure(const CommandContext &context, std::string_view argument) {
    return handle_virus_move(context, argument, VirusAction::cure).value_or(std::string{});
}

std::string handle_role(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::optional<VirusPlayer> role = virus_role(context.storage, std::string{context.username});
    if (!role) {
        tell_only_him(context, "🦠 Non sei in partita.");
        return {};
    }
    std::string said = role->doronzo
        ? "🦠 Sei un DORONZO. Infetti con /infetta <nome>."
        : "💉 Sei SANO. Spari con /spara <nome>, ma un colpo addosso a un sano lo uccide lo stesso.";
    if (!role->alive) {
        said = "⚰️ Sei morto. Da lì si guarda e basta.";
    } else if (role->vaccines > 0) {
        said += std::format(" Hai {} fiale di vaccino: /vaccina <nome>.", role->vaccines);
    }
    tell_only_him(context, said);
    return {};
}

std::string handle_virus(const CommandContext &context, std::string_view argument) {
    if (text::equals_ignore_case(text::trim(argument), "start")) {
        if (!context.owner) {
            return "Solo il proprietario può far scoppiare l'epidemia.";
        }
        const VirusStart start = virus_start(context.storage, seconds_now());
        if (start.status != VirusStatus::done) {
            return std::string{virus_refusal(start.status)};
        }
        if (context.whisper) {
            for (const auto &[name, doronzo] : start.roles) {
                context.whisper(
                    name,
                    doronzo
                        ? "🦠 Sei un DORONZO. Infetti con /infetta <nome>, una mossa ogni dieci minuti."
                        : "💉 Sei SANO. Spari con /spara <nome>, una mossa ogni dieci minuti: attento a chi colpisci."
                );
            }
        }
        return std::format(
            "🦠 IL VIRUS DORONZO STA COLPENDO TUTTI I GIOCATORI!\n"
            "{} in gioco, e nessuno sa chi è cosa. I contagiati infettano con /infetta <nome>, i sani "
            "sparano con /spara <nome>: una mossa ogni dieci minuti.\n"
            "Chi spara a un sano lo ammazza lo stesso, e chi muore perde metà delle palle. "
            "Ogni quattro contagi un sano riceve una fiala.\n"
            "Il ruolo ve l'ho scritto in privato: chi non l'ha ricevuto scriva /ruolo al bot in privato.",
            start.players
        );
    }
    const VirusReport report = virus_report(context.storage);
    if (!report.running) {
        return "🦠 Nessuna epidemia in corso.";
    }
    std::string reply = std::format(
        "🦠 Epidemia in corso: {} in piedi, {} a terra, {} contagi finora.",
        report.alive,
        report.dead,
        report.infections
    );
    if (!report.fallen.empty()) {
        reply += "\n⚰️ Caduti: ";
        for (std::size_t index = 0; index < report.fallen.size(); ++index) {
            reply += index == 0 ? "" : ", ";
            reply += report.fallen[index];
        }
    }
    return reply;
}

std::string handle_free(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string_view asked = argument.starts_with('@') ? argument.substr(1) : argument;
    if (asked.empty()) {
        return "🔓 Uso: /libera <nome>.";
    }
    const FreeingResult freeing = set_free(context.storage, asked);
    if (!freeing.known) {
        return std::format("🔓 {} non è riprogrammato.", asked);
    }
    return freeing.worked
        ? std::format("🔓 {} ha liberato {}: Kio non lo controlla più.", context.username, asked)
        : std::format(
              "🤖 {} ha provato a liberare {}, ma la pratica non è andata a buon fine.",
              context.username,
              asked
          );
}

std::string handle_report(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const DisputeResult opened = dispute_open(context.storage, username, seconds_now());
    switch (opened.status) {
    case DisputeStatus::nothing_to_report:
        return std::format("🧾 {} non c'è niente da segnalare: nessuno ti ha preso niente.", username);
    case DisputeStatus::too_late:
        return std::format("🧾 {} sono passati più di 7 giorni: la segnalazione non si può più aprire.", username);
    case DisputeStatus::already_open:
        return std::format("🧾 {} la segnalazione è già aperta.", username);
    case DisputeStatus::done:
        break;
    }
    return std::format(
        "🧾 {} ha aperto una segnalazione per le {} palle che {} gli ha preso.\n"
        "{}: decidi tu con /reso mie oppure /reso tue, cioè a spese di chi torna la roba.",
        username,
        opened.palle,
        opened.seller,
        opened.seller
    );
}

std::string handle_return(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    /* The seller decides who pays the postage; left to himself unless he says otherwise. */
    const bool on_seller = !text::equals_ignore_case(text::trim(argument), "tue");
    const ReturnResult giving = loot_return(context.storage, username, seconds_now(), on_seller);
    switch (giving.status) {
    case ReturnStatus::nothing_to_return:
        return std::format("📦 {} non hai niente da restituire.", username);
    case ReturnStatus::too_late:
        return std::format(
            "📦 {} sono passati più di 14 giorni da quando hai preso quelle {} palle a {}: niente reso.",
            username,
            giving.palle,
            giving.victim
        );
    case ReturnStatus::done:
        break;
    }
    if (giving.overturned) {
        return std::format(
            "🎧 Il supporto clienti ha riesaminato il caso: {} viene rimborsato di {} palle e {} tiene "
            "comunque la roba. Nessuno paga la spedizione.",
            giving.victim,
            giving.palle,
            username
        );
    }
    return std::format(
        "📦 {} ha restituito {} palle a {}. Spese di spedizione del reso a carico {}: {} palle. "
        "Gliene restano {}.",
        username,
        giving.palle,
        giving.victim,
        on_seller ? "del venditore" : "dell'acquirente",
        giving.postage,
        giving.score
    );
}

std::string handle_profile(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const std::string_view asked = argument.empty() ? std::string_view{username} : argument;
    const std::string_view name = asked.starts_with('@') ? asked.substr(1) : asked;
    const std::int64_t now = seconds_now();
    const std::optional<PlayerCard> card =
        player_card(context.storage, username, name, now, static_cast<int>(rules_of(context).travel_divisor));
    if (!card) {
        return std::format("{} non conosco nessun giocatore di nome {}.", username, name);
    }
    const bool mine = text::equals_ignore_case(card->username, username);
    const zodiac::Sign sign = zodiac::sign_of(card->username, context.config.zodiac_signs);
    const int percent = zodiac::percent_for(card->username, now, context.config.zodiac_signs);

    std::string reply = std::format("🪐 {} {} {}", card->username, sign.symbol, sign.name);
    reply += std::format(
        "\n💰 {} palle{}{}",
        card->score,
        card->rank != 0 ? std::format(" — {}° posto", card->rank) : "",
        card->quotes_added > 0 ? std::format(" — 📜 {}", card->quotes_added) : ""
    );
    reply += card->simpatia < simpatia_threshold
        ? std::format("\n🚨 simpatia {}: ANTIPATICO, denunciato alla GDF", card->simpatia)
        : std::format("\n😀 simpatia {}", card->simpatia);
    reply += std::format(
        "\n🔮 oggi è giorno di {}: {}",
        zodiac::element_name(zodiac::element_of_day(now)),
        percent == 100 ? "nessun effetto" : std::format("x{}.{:02}", percent / 100, percent % 100)
    );
    if (card->in_conquister) {
        reply += std::format("\n🪐 in {} da {}", conquister_place, format_wait(card->held_seconds));
    }
    if (card->travelling) {
        reply += card->carrying
            ? std::format("\n🚀 sta tornando da {}, arriva tra {}", card->travel_target, format_wait(card->travel_seconds))
            : std::format("\n🚀 in viaggio verso {}, arriva tra {}", card->travel_target, format_wait(card->travel_seconds));
    }
    if (!mine) {
        reply += std::format("\n🗺️ dista {} di viaggio da te", format_wait(card->distance_seconds));
        return reply;
    }
    /* Only to himself: what an attacker is supposed to find out the hard way. */
    if (card->shield_seconds > 0) {
        reply += std::format("\n🎈 palloncino blindato, resiste ancora per {}", format_wait(card->shield_seconds));
    } else if (card->balloon_attempts >= 0) {
        const int chance = (card->balloon_attempts + 1) * 25;
        reply += std::format(
            "\n🎈 palloncino: ha respinto {} attacchi, il prossimo lo buca al {}%",
            card->balloon_attempts,
            chance
        );
    } else {
        reply += "\n🎈 nessun palloncino";
    }
    if (card->boost_multiplier > 0) {
        reply += std::format("\n⚡ boost x{} pronto", card->boost_multiplier);
    }
    if (card->cooldown_seconds > 0) {
        reply += std::format("\n⏳ penalità: ancora {}", format_wait(card->cooldown_seconds));
    }
    return reply;
}

std::string handle_zimbelli(const CommandContext &context, std::string_view) {
    const std::vector<LeaderboardEntry> zimbelli = conquister_negatives(context.storage, leaderboard_size);
    if (zimbelli.empty()) {
        return std::format("Nessuno zimbello: nessuno è sotto zero in {}.", conquister_place);
    }
    std::string reply = "🤡 Zimbelli\n";
    for (std::size_t position = 1; const LeaderboardEntry &entry : zimbelli) {
        reply += std::format(
            "\n{}) {} {} — {} palle sotto zero",
            position++,
            zodiac::sign_of(entry.username, context.config.zodiac_signs).symbol,
            entry.username,
            -entry.score
        );
    }
    return reply;
}

std::string handle_add_quote(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const auto cost = static_cast<int>(rules_of(context).quote_cost);
    if (argument.empty()) {
        return std::format("Uso: /addquote <testo>. Costa {} palle.", cost);
    }
    const std::string username{context.username};
    /* Nobody gets tagged by a quote read out months later. */
    const std::string quote = text::strip_mentions(argument);
    const bool shadowed = std::ranges::any_of(context.config.shadowed, [&username](const std::string &name) {
        return text::equals_ignore_case(name, username);
    });
    /* Compared without punctuation, or "F.R.O.D.E." would walk straight past. */
    const std::string squeezed = text::squeeze(quote);
    const auto banned = std::ranges::find_if(context.config.quote_banned, [&squeezed](const std::string &piece) {
        return text::contains_ignore_case(squeezed, text::squeeze(piece));
    });
    if (!shadowed && banned != context.config.quote_banned.end()) {
        return std::format("{} questa citazione non si può aggiungere: nessun addebito.", username);
    }
    const QuoteAddResult result = shadowed
        ? quote_pretend(context.storage, username, cost)
        : quote_add(context.storage, username, quote, cost, seconds_now());
    if (result.status == QuoteAddStatus::insufficient_score) {
        return std::format(
            "{} ti servono {} palle per aggiungere una citazione (ne hai {}).",
            username,
            cost,
            result.available_score
        );
    }
    if (result.status == QuoteAddStatus::duplicate) {
        return "Citazione già presente o non salvabile: nessun addebito.";
    }
    return std::format(
        "{} hai aggiunto la citazione spendendo {} palle!\n\n{}",
        username,
        cost,
        quote
    );
}

std::string handle_buy_balloon(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const auto cost = static_cast<int>(rules_of(context).balloon_cost);
    const std::int64_t shield_seconds = is_shielded(context, username) ? context.config.shield_seconds : 0;
    const BalloonResult result = balloon_buy(context.storage, username, cost, seconds_now(), shield_seconds);
    if (result.status == BalloonStatus::already_owned) {
        if (result.shield_seconds > 0) {
            return std::format(
                "{} hai già un palloncino, resiste ancora per {}.",
                username,
                format_wait(result.shield_seconds)
            );
        }
        return std::format("{} hai già un palloncino.", username);
    }
    if (result.status == BalloonStatus::has_boost) {
        return std::format("{} hai un boost attivo: il palloncino puoi comprarlo dopo.", username);
    }
    if (result.status == BalloonStatus::insufficient_score) {
        return std::format(
            "{} ti servono {} palle per un palloncino (ne hai {}).",
            username,
            cost,
            result.available_score
        );
    }
    if (result.shield_seconds > 0) {
        return std::format(
            "🎈 {} hai comprato un palloncino spendendo {} palle! "
            "Nessuno può bucarlo: difende la tua posizione in {} per {}.",
            username,
            cost,
            conquister_place,
            format_wait(result.shield_seconds)
        );
    }
    return std::format(
        "🎈 {} hai comprato un palloncino spendendo {} palle! Ora puoi difendere la tua posizione in {}.",
        username,
        cost,
        conquister_place
    );
}

std::string handle_quotes(const CommandContext &context, std::string_view argument) {
    if (!context.owner) {
        return "Solo il proprietario può vedere le citazioni.";
    }
    const std::optional<std::int64_t> requested = text::parse_int64(argument);
    const int page = requested && *requested > 0 && *requested <= std::numeric_limits<std::int32_t>::max()
        ? static_cast<int>(*requested)
        : 1;
    const QuotePage quotes = quote_page_load(context.storage, page);
    if (quotes.total == 0) {
        return "Nessuna citazione in collezione.";
    }
    std::string reply = std::format(
        "📜 Citazioni {}-{} di {} (pagina {}/{}):",
        quotes.first_number,
        quotes.first_number + quotes.items.size() - 1,
        quotes.total,
        quotes.page,
        quotes.pages
    );
    for (std::size_t index = 0; index < quotes.items.size(); ++index) {
        const std::string &quote = quotes.items[index];
        const bool truncated = text::utf8_prefix_bytes(quote, 80) < quote.size();
        const std::string_view shown = truncated
            ? std::string_view{quote}.substr(0, text::utf8_prefix_bytes(quote, 77))
            : std::string_view{quote};
        const std::string &author = quotes.authors[index];
        reply += std::format(
            "\n{}) {}{}{}",
            quotes.first_number + index,
            shown,
            truncated ? "…" : "",
            author.empty() ? "" : std::format(" — {}", author)
        );
    }
    if (quotes.pages > 1) {
        reply += "\n\nUsa /quotes <pagina> per le altre pagine.";
    }
    return reply;
}

std::string handle_delete_quote(const CommandContext &context, std::string_view argument) {
    if (!context.owner) {
        return "Solo il proprietario può eliminare le citazioni.";
    }
    if (argument.empty()) {
        return "Uso: /delquote <numero da /quotes | testo esatto>.";
    }
    const std::optional<std::string> removed = quote_delete(context.storage, argument);
    return removed ? std::format("Citazione eliminata: {}", *removed) : "Citazione non trovata.";
}

std::string handle_buy_boost(const CommandContext &context, std::string_view) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const Rules rules = rules_of(context);
    const auto cost = static_cast<int>(rules.boost_cost);
    const BoostResult result =
        boost_buy(context.storage, username, cost, rules.boost_multiplier, seconds_now());
    if (result.status == BoostStatus::already_owned) {
        return std::format("{} hai già un boost x{} pronto.", username, result.multiplier);
    }
    if (result.status == BoostStatus::has_balloon) {
        return std::format("{} hai un palloncino: il boost puoi comprarlo dopo.", username);
    }
    if (result.status == BoostStatus::insufficient_score) {
        return std::format(
            "{} ti servono {} palle per un boost (ne hai {}).",
            username,
            cost,
            result.available_score
        );
    }
    return std::format(
        "⚡ {} hai comprato un boost spendendo {} palle! Il tuo prossimo possesso di {} vale x{}, "
        "fino a quando ti spodestano.",
        username,
        cost,
        conquister_place,
        result.multiplier
    );
}

constexpr std::array commands{
    CommandDefinition{"/leaderboard", handle_leaderboard},
    CommandDefinition{"/zimbelli", handle_zimbelli},
    CommandDefinition{"/profilo", handle_profile},
    CommandDefinition{"/reso", handle_return},
    CommandDefinition{"/segnala", handle_report},
    CommandDefinition{"/libera", handle_free},
    CommandDefinition{"/virus", handle_virus},
    CommandDefinition{"/ruolo", handle_role},
    CommandDefinition{"/infetta", handle_infect},
    CommandDefinition{"/spara", handle_shoot},
    CommandDefinition{"/vaccina", handle_cure},
    CommandDefinition{"/addquote", handle_add_quote},
    CommandDefinition{"/buyballoon", handle_buy_balloon},
    CommandDefinition{"/buyboost", handle_buy_boost},
    CommandDefinition{"/quotes", handle_quotes},
    CommandDefinition{"/delquote", handle_delete_quote},
};

const CommandDefinition *find_command(std::string_view name) {
    const auto found = std::ranges::find_if(commands, [name](const CommandDefinition &definition) {
        return definition.name == name;
    });
    return found != commands.end() ? &*found : nullptr;
}

}

bool command_is_for_bot(std::string_view text) {
    const std::string_view message = text::trim(text);
    return message == conquister_trigger || !raid_target(message).empty() ||
           (!message.empty() && find_command(parse_command(message).name) != nullptr);
}

std::string game_opened_reply(const GameOpened &opened) {
    switch (opened.kind) {
    case Game::race:
        return std::format("🏁 CORSA: pronti, via! Il primo che scrive si prende {} palle.", opened.pot);
    case Game::guess:
        return std::format(
            "🔢 INDOVINA: penso un numero fra 1 e 100. Chi lo scrive per primo si prende {} palle.",
            opened.pot
        );
    case Game::auction:
        return "🔨 ASTA: si batte un palloncino. Si offre scrivendo un numero, vince l'offerta più alta.";
    case Game::forbidden:
        return "🤐 PAROLA PROIBITA: ne ho scelta una e non ve la dico. Chi la scrive paga un decimo di quello che ha.";
    case Game::sequence:
        return std::format(
            "🧠 MEMORIA: il numero è {}. Il primo che me lo ripete si prende {} palle.",
            opened.secret,
            opened.pot
        );
    case Game::longest:
        return std::format(
            "📏 PAROLA PIÙ LUNGA: chi scrive la parola più lunga entro la chiusura si prende {} palle.",
            opened.pot
        );
    case Game::silence:
        return std::format(
            "🤫 SILENZIO: se nessuno scrive fino alla chiusura, {} palle a testa per tutti. Il primo che parla paga.",
            opened.pot
        );
    case Game::quiz:
        return std::format(
            "❓ DOMANDA: {} Chi risponde per primo si prende {} palle.",
            questions.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::anagram: {
        std::string scrambled{anagrams.at(static_cast<std::size_t>(opened.secret))};
        std::ranges::sort(scrambled);
        return std::format(
            "🔤 ANAGRAMMA: {} — chi trova la parola si prende {} palle.",
            scrambled,
            opened.pot
        );
    }
    case Game::chain:
        return std::format(
            "🔗 CATENA: si scrive a turno, ogni messaggio comincia con la lettera con cui finisce il "
            "precedente. Si parte dalla {}. Chi sbaglia paga un decimo.",
            static_cast<char>(opened.secret)
        );
    case Game::counting:
        return std::format(
            "🔢 CONTA: arriviamo a 20 uno alla volta. Chi sbaglia numero paga un decimo, chi dice 20 "
            "si prende {} palle.",
            opened.pot
        );
    case Game::whois:
        return std::format(
            "🕵️ IDENTIKIT: penso a un giocatore di questo gruppo. Chi scrive il suo nome si prende {} palle.",
            opened.pot
        );
    case Game::mirror:
        return std::format(
            "🪞 SPECCHIO: la parola è {}. Chi la scrive al contrario si prende {} palle.",
            mirrors.at(static_cast<std::size_t>(opened.secret)),
            opened.pot
        );
    case Game::rhyme:
        return std::format(
            "🎤 RIMA: trovate una parola che rimi con {}. La prima vale {} palle.",
            rhymes.at(static_cast<std::size_t>(opened.secret)),
            opened.pot
        );
    case Game::target:
        return std::format(
            "🎯 MIRINO: scrivete un messaggio lungo esattamente {} caratteri. Chi lo centra si prende {} palle.",
            opened.secret,
            opened.pot
        );
    case Game::closest:
        return std::format(
            "👁️ A OCCHIO: penso un numero fra 1 e 1000. Alla chiusura {} palle a chi ci è andato più vicino.",
            opened.pot
        );
    case Game::cards:
        return std::format(
            "🃏 CARTA ALTA: si pesca scrivendo, una carta a testa. La più alta alla chiusura vale {} palle.",
            opened.pot
        );
    case Game::maths:
        return std::format(
            "🧮 CALCOLO: quanto fa {}? Il primo che lo dice si prende {} palle.",
            opened.target,
            opened.pot
        );
    case Game::countdown:
        return std::format(
            "⏳ ROVESCIO: da 20 a 1, uno alla volta. Chi sbaglia numero paga un decimo, chi dice 1 si "
            "prende {} palle.",
            opened.pot
        );
    case Game::capital:
        return std::format(
            "🌍 CAPITALE: qual è la capitale del {}? Vale {} palle.",
            capitals.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::emoji:
        return std::format(
            "😀 EMOJI: {} — chi scrive la parola si prende {} palle.",
            emojis.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::acrostic:
        return std::format(
            "🅰️ ACROSTICO: una frase di almeno quattro parole che cominciano tutte per {}. Vale {} palle.",
            static_cast<char>(opened.secret),
            opened.pot
        );
    case Game::novowels:
        return std::format(
            "🚫 VOCALI: scrivete almeno dieci lettere senza usarne una sola. {} palle a chi ci riesce.",
            opened.pot
        );
    case Game::palindrome:
        return std::format(
            "🔄 PALINDROMO: una frase che si legge uguale da destra, almeno cinque lettere. {} palle.",
            opened.pot
        );
    case Game::shortest:
        return std::format(
            "🐜 CORTA: vince la parola più corta scritta entro la chiusura, minimo due lettere: {} palle.",
            opened.pot
        );
    case Game::river:
        return std::format(
            "📜 FIUME: vince il messaggio con più parole alla chiusura. In palio {} palle.",
            opened.pot
        );
    case Game::sum:
        return std::format(
            "➕ SOMMA: si aggiungono numeri fino ad arrivare esattamente a {}. Chi sfora paga un decimo, "
            "chi centra si prende {} palle.",
            opened.secret,
            opened.pot
        );
    case Game::year:
        return std::format(
            "📅 ANNO: in che anno è successo {}? Vale {} palle.",
            years.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::proverb:
        return std::format(
            "📖 PROVERBIO: «{}...» — chi lo finisce si prende {} palle.",
            proverbs.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::roulette:
        return std::format(
            "🎡 ROULETTE: scrivete un numero da 1 a 10, la ruota gira subito. Se esce il vostro, {} palle.",
            opened.pot
        );
    case Game::hangman:
        return std::format(
            "🪢 IMPICCATO: {} — una lettera alla volta, e chi indovina la parola si prende {} palle.",
            std::string(std::string_view{mirrors.at(static_cast<std::size_t>(opened.secret))}.size(), '-'),
            opened.pot
        );
    case Game::colour:
        return std::format(
            "🎨 COLORE: sto pensando a un colore. Chi lo indovina si prende {} palle.",
            opened.pot
        );
    case Game::animal:
        return std::format(
            "🐘 ANIMALE: {}. Chi è? Vale {} palle.",
            animals.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::copy:
        return std::format(
            "⌨️ COPIA: {} — il primo che lo riscrive identico si prende {} palle.",
            opened.target,
            opened.pot
        );
    case Game::alphabet:
        return std::format(
            "🔠 ALFABETO: da A a J, ogni messaggio comincia con la lettera dopo. Chi sbaglia paga un "
            "decimo, chi chiude si prende {} palle.",
            opened.pot
        );
    case Game::hotcold:
        return std::format(
            "🌡️ CALDO: numero fra 1 e 100, vi dico solo se scotta. {} palle a chi lo trova.",
            opened.pot
        );
    case Game::letters:
        return std::format(
            "🔡 LETTERE: una parola che contenga tutte e tre le lettere {}. Vale {} palle.",
            triples.at(static_cast<std::size_t>(opened.secret)),
            opened.pot
        );
    case Game::song:
        return std::format(
            "🎵 CANZONE: «{}...» — chi la continua si prende {} palle.",
            songs.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::city:
        return std::format(
            "🏙️ CITTÀ: una città italiana che comincia per {}. Vale {} palle.",
            static_cast<char>(opened.secret),
            opened.pot
        );
    case Game::dish:
        return std::format(
            "🍝 RICETTA: cosa ci vuole per forza nella {}? {} palle a chi lo dice.",
            dishes.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::hidden:
        return std::format(
            "🧩 NASCOSTA: {} — c'è una parola là dentro. Chi la scrive si prende {} palle.",
            opened.target,
            opened.pot
        );
    case Game::syllable:
        return std::format(
            "🔤 SILLABA: una parola di almeno cinque lettere che contenga {}. Vale {} palle.",
            syllables.at(static_cast<std::size_t>(opened.secret)),
            opened.pot
        );
    case Game::film:
        return std::format(
            "🎬 FILM: {}. Che film è? {} palle.",
            films.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::series:
        return std::format(
            "🧠 SERIE: {}, e poi? Il primo che lo dice si prende {} palle.",
            opened.target,
            opened.pot
        );
    case Game::coin:
        return std::format(
            "🪙 MONETA: scrivete testa o croce, la lancio subito. Se indovinate, {} palle.",
            opened.pot
        );
    case Game::trafficlight:
        return std::format(
            "🚦 SEMAFORO: ho scelto un colore e non ve lo dico. Il primo che scrive prende {} palle "
            "se è verde e paga un decimo se è rosso.",
            opened.pot
        );
    case Game::ends:
        return std::format(
            "🔡 ESTREMI: una parola di almeno quattro lettere fatta così: {}. Vale {} palle.",
            opened.target,
            opened.pot
        );
    case Game::slot:
        return std::format(
            "🎰 SLOT: una tirata a testa scrivendo. Tre uguali fanno {} palle, due uguali un quinto.",
            opened.pot
        );
    case Game::stopwatch:
        return std::format(
            "🕐 CRONOMETRO: scrivete qualcosa fra esattamente trenta secondi, non prima e non dopo. {} palle.",
            opened.pot
        );
    case Game::order:
        return std::format(
            "🔀 ORDINE: {} — riscrivetele in ordine alfabetico in un messaggio solo. {} palle.",
            opened.target,
            opened.pot
        );
    case Game::sealed:
        return std::format(
            "🕶️ ASTA CIECA: un'offerta a testa, nessuno vede quelle degli altri. Alla chiusura la più "
            "alta si prende {} palle e paga quanto ha offerto.",
            opened.pot
        );
    case Game::unique:
        return std::format(
            "🎯 NUMERO UNICO: un numero da 1 a 50 a testa. Vince il più basso scelto da una persona "
            "sola: {} palle.",
            opened.pot
        );
    case Game::average:
        return std::format(
            "📊 MEDIA: un numero da 0 a 100 a testa. Vince chi si avvicina ai due terzi della media "
            "di tutti: {} palle.",
            opened.pot
        );
    case Game::pyramid:
        return std::format(
            "🏗️ PIRAMIDE: un'offerta a testa. C'è un tetto che non vi dico: chi lo supera è fuori, "
            "chi resta sotto e ha offerto di più prende {} palle.",
            opened.pot
        );
    case Game::potato:
        return "🥔 PATATA BOLLENTE: chi scrive se la ritrova in mano. La miccia è già accesa e chi la tiene quando scoppia paga un decimo.";
    case Game::chairs:
        return std::format(
            "🪑 SEDIE: si entra scrivendo. Ogni venticinque secondi esce chi ha scritto di meno, "
            "l'ultimo rimasto si prende {} palle.",
            opened.pot
        );
    case Game::russian:
        return std::format(
            "🔫 ROULETTE RUSSA: ogni messaggio è un colpo, uno su sei parte. Chi è ancora in piedi "
            "alla chiusura si divide {} palle.",
            opened.pot
        );
    case Game::climb:
        return std::format(
            "🧗 SCALATA: si entra con cento palle, ogni messaggio le moltiplica per una volta e mezza, "
            "ma una volta su quattro si precipita. Alla chiusura si incassa, fino a {} palle.",
            opened.pot
        );
    case Game::bank:
        return std::format(
            "🃏 BANCO: ogni messaggio pesca una carta, si arriva a 21 senza sballare. Il punto più "
            "alto alla chiusura vale {} palle.",
            opened.pot
        );
    case Game::collect:
        return std::format(
            "🤝 COLLETTA: mettete insieme esattamente {} palle, un versamento a testa. Se ci arrivate "
            "tornano indietro tutte più {} palle da dividere, se sforate restano al banco.",
            opened.secret,
            opened.pot
        );
    case Game::trial:
        return std::format(
            "⚖️ PROCESSO: sul banco degli imputati c'è {}. Scrivete colpevole o innocente: se passa "
            "la condanna paga un decimo a chi lo ha accusato, se è assolto sono gli accusatori a "
            "pagargli cento palle a testa.",
            opened.target
        );
    case Game::bounty:
        return std::format(
            "💰 TAGLIA: {} palle sulla testa di {}. Il primo che ne scrive il nome le incassa, e le "
            "paga lui. A meno che non scriva pago e se la compri a metà prezzo.",
            opened.secret,
            opened.target
        );
    case Game::siege:
        return std::format(
            "🏰 ASSEDIO: {} è chiuso dentro @TheConquister37. Servono {} colpi per buttarlo giù, e "
            "ogni suo messaggio ne respinge uno. Chi dà l'ultimo colpo prende {} palle.",
            opened.target,
            opened.secret,
            opened.pot
        );
    case Game::market:
        return "📈 BORSA: il titolo parte da 100 e si muove a ogni messaggio. Scrivete compro o vendo una volta sola, alla chiusura si fanno i conti.";
    case Game::wager:
        return "🎲 SCOMMESSA: puntate un numero di palle. Alla chiusura si somma tutto: chi ha puntato pari vince se il totale è pari, e viceversa.";
    case Game::relay:
        return std::format(
            "🏃 STAFFETTA: sei passaggi senza fermarsi più di quindici secondi, e mai due di fila "
            "dalla stessa persona. Se arrivate in fondo {} palle da dividere.",
            opened.pot
        );
    case Game::hostage:
        return std::format(
            "🧨 OSTAGGIO: Kio tiene {} e chiede {} palle di riscatto. Versate scrivendo numeri: se "
            "alla chiusura mancano, l'ostaggio paga un decimo di tutto quello che ha.",
            opened.target,
            opened.secret
        );
    case Game::legacy:
        return std::format(
            "📜 EREDITÀ: {} palle senza padrone. Scrivete quanto ne chiedete: prende tutto chi ne ha "
            "chieste meno di chiunque altro.",
            opened.secret
        );
    case Game::customs:
        return std::format(
            "🛃 DOGANA: controllo su ogni messaggio. Chi passa più volte senza farsi trovare merce "
            "addosso si prende {} palle, ogni sequestro ne costa cento.",
            opened.pot
        );
    case Game::marathon:
        return std::format(
            "🏃‍♂️ MARATONA: {} parole da coprire tutti insieme. Chi scrive quella che taglia il "
            "traguardo si prende {} palle.",
            opened.secret,
            opened.pot
        );
    case Game::stars:
        return std::format(
            "♈ ZODIACO: oggi la casa è {}. Scrivete una volta sola: chi ha il segno giusto si prende "
            "metà piatto, chi ha quello opposto lascia un ventesimo di quello che ha.",
            zodiac::element_name(static_cast<zodiac::Element>(opened.secret))
        );
    case Game::fraud:
        return std::format(
            "🚨 FRODE: dichiarate quante palle avete. Chi dichiara meno di un decimo del vero paga la "
            "differenza alla Guardia di Finanza, la dichiarazione più alta e onesta prende {} palle.",
            opened.pot
        );
    case Game::scheme:
        return "💹 SCHEMA: si entra con duecento palle, che vanno divise fra chi è entrato prima. Gli ultimi due della catena restano con il cerino in mano.";
    case Game::refund:
        return std::format(
            "📦 RESO: {} ha aperto una segnalazione. Scrivete rimborso o truffa: il supporto clienti "
            "decide a sorte, ma più voti ci sono da una parte più è probabile. In ballo {} palle.",
            opened.target,
            opened.pot / 2
        );
    case Game::spy:
        return std::format(
            "🕵️‍♀️ SPIA: fra voi c'è una spia che non sa di esserlo. Fate un nome: se la prendete vi "
            "dividete {} palle, se scappa se le tiene tutte lei.",
            opened.pot
        );
    case Game::plot:
        return "🗡️ CONGIURA: scrivete il nome di chi volete far cadere. Il più nominato lascia un decimo di quello che ha a chi lo ha nominato.";
    case Game::dowry:
        return std::format(
            "💍 DOTE: scrivete sposo e un nome. Se quello risponde di sì vi dividete {} palle.",
            opened.pot
        );
    case Game::bingo:
        return std::format(
            "🎱 TOMBOLA: una cartella a testa scrivendo qualsiasi cosa, cinque numeri fino a 30. "
            "Estraggo ogni venti secondi, cartella piena {} palle.",
            opened.pot
        );
    case Game::horses:
        return std::format(
            "🐎 IPPODROMO: quattro cavalli, scrivete il numero del vostro. Corrono da soli, chi ha "
            "puntato sul vincente prende {} palle.",
            opened.pot
        );
    case Game::quake:
        return "🌋 TERREMOTO: la terra trema ogni trenta secondi. Chi non si fa sentire in tempo perde un ventesimo di quello che ha.";
    case Game::war:
        return std::format(
            "⚔️ GUERRA: vi divido in due schieramenti man mano che scrivete. Alla chiusura il "
            "fronte più forte si divide {} palle, l'altro ne lascia cento a testa.",
            opened.pot
        );
    case Game::deposit:
        return "🏦 BANCA: scrivete quante palle depositate. Alla chiusura tornano con il dieci per cento di interessi, salvo fallimento.";
    case Game::talent:
        return std::format(
            "🎤 TALENTO: vince il messaggio con più parole diverse fra loro. In palio {} palle.",
            opened.pot
        );
    case Game::treasure:
        return std::format(
            "🏹 CACCIA AL TESORO: ho nascosto una parola fra queste: {}. Ogni tentativo sbagliato "
            "costa venti palle, quello giusto ne vale {}.",
            std::string{mirrors.at(0)} + ", " + std::string{mirrors.at(2)} + ", " +
                std::string{mirrors.at(4)} + ", " + std::string{mirrors.at(6)} + ", " +
                std::string{mirrors.at(8)} + ", e altre",
            opened.pot
        );
    case Game::domino:
        return std::format(
            "🁣 DOMINO: si parte dal {}. Ogni numero deve cominciare con la cifra con cui finisce il "
            "precedente. Chi sbaglia paga un decimo.",
            opened.secret
        );
    case Game::tunnel:
        return std::format(
            "🚇 TUNNEL: ogni messaggio deve contenere una parola più lunga della precedente. Se "
            "arrivate a otto vi dividete {} palle.",
            opened.pot
        );
    case Game::dutch:
        return std::format(
            "📉 RIBASSO: il piatto vale {} palle e il prezzo scende di mille ogni venti secondi. "
            "Scrivete prendo per comprarlo al prezzo di adesso.",
            opened.pot
        );
    case Game::riddle:
        return std::format(
            "🗿 ENIGMA: ho in mente una parola e do un indizio ogni quaranta secondi. Chi indovina "
            "subito prende {} palle, dopo il secondo indizio la metà, dopo il terzo un terzo.",
            opened.pot
        );
    case Game::navy:
        return std::format(
            "🚢 BATTAGLIA NAVALE: venticinque caselle, una nave. Ogni colpo a vuoto costa venti "
            "palle, centrarla ne vale {}.",
            opened.pot
        );
    case Game::election:
        return "🗳️ ELEZIONI: scrivete il nome di chi volete sindaco. Il più votato mette una tassa di cinquanta palle su chi ha votato.";
    case Game::tug:
        return std::format(
            "🪢 TIRO ALLA FUNE: chi ha il nome dalla A alla M tira da una parte, gli altri "
            "dall'altra. Ogni messaggio è uno strattone, il lato dove sta la fune alla chiusura si "
            "divide {} palle.",
            opened.pot
        );
    case Game::jenga:
        return std::format(
            "🏯 TORRE: ogni messaggio sfila un pezzo. Una volta su otto la torre viene giù e chi "
            "l'ha toccata paga un decimo, chi resta in piedi si divide {} palle.",
            opened.pot
        );
    case Game::whispers:
        return std::format(
            "📞 TELEFONO SENZA FILI: si parte da {}. Ogni messaggio deve contenere la parola "
            "precedente con una sola lettera cambiata. Cinque passaggi e si divide {} palle.",
            opened.target,
            opened.pot
        );
    case Game::smuggle:
        return "🕶️ CONTRABBANDO: dichiarate quante palle mettete nel carico. Alla chiusura la dogana ne controlla uno a caso: quello lo perde, gli altri raddoppiano.";
    case Game::insurance:
        return "☂️ ASSICURAZIONE: scrivete assicuro per versare cento palle di premio. Una volta su tre succede il disastro e gli assicurati ne prendono cinquecento, altrimenti il premio resta alla compagnia.";
    case Game::strike:
        return "🪧 SCIOPERO: finché nessuno parla la cassa cresce di cinquecento palle ogni venti secondi. Il primo che scrive fa saltare tutto e paga un decimo.";
    case Game::contest:
        return std::format(
            "📝 CONCORSO PUBBLICO: tre domande, una ogni quarantacinque secondi, un punto a testa "
            "per ogni risposta giusta. Prima domanda: {} In palio {} palle.",
            questions.at(static_cast<std::size_t>(opened.secret)).asked,
            opened.pot
        );
    case Game::cadastre:
        return std::format(
            "🏚️ CATASTO: prendete un lotto da 1 a 20 scrivendone il numero. I lotti chiesti da una "
            "persona sola rendono, quelli contesi non rendono niente. In ballo {} palle.",
            opened.pot
        );
    case Game::pilgrimage:
        return std::format(
            "⛪ PELLEGRINAGGIO: {} parole di cammino da fare insieme, ma ogni venti secondi il "
            "sentiero ne toglie dieci. Se arrivate, {} palle da dividere.",
            opened.secret,
            opened.pot
        );
    case Game::apocalypse:
        return std::format(
            "☄️ APOCALISSE: scrivete per salire sull'arca. Ogni venticinque secondi ne salvo uno a "
            "caso: i salvati si dividono {} palle, chi resta giù perde un quinto di tutto.",
            opened.pot
        );
    }
    return {};
}

std::string game_closed_reply(const GameClosed &closed) {
    switch (closed.kind) {
    case Game::race:
        return "🏁 Nessuno si è mosso in tempo: la corsa si chiude senza vincitore.";
    case Game::guess:
        return std::format("🔢 Tempo scaduto: il numero era {}. Nessuno l'ha preso.", closed.secret);
    case Game::auction:
        return closed.winner.empty()
            ? std::string{"🔨 Asta deserta: il palloncino resta invenduto."}
            : std::format(
                  "🔨 Aggiudicato a {} per {} palle: il palloncino è suo.",
                  closed.winner,
                  closed.pot
              );
    case Game::forbidden:
        return "🤐 Nessuno ha detto la parola proibita. Stavolta.";
    case Game::sequence:
        return std::format("🧠 Tempo scaduto: nessuno si ricordava che fosse {}.", closed.secret);
    case Game::longest:
        return closed.winner.empty()
            ? std::string{"📏 Nessuna parola degna di nota."}
            : std::format(
                  "📏 Ha vinto {} con una parola di {} lettere: {} palle.",
                  closed.winner,
                  closed.secret,
                  closed.pot
              );
    case Game::silence:
        return closed.secret > 0
            ? std::format("🤫 Silenzio rispettato: {} palle a testa per tutti e {}.", closed.pot, closed.secret)
            : std::string{"🤫 Silenzio rotto, niente per nessuno."};
    case Game::quiz:
        return std::format(
            "❓ Tempo scaduto, la risposta era: {}.",
            questions.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::anagram:
        return std::format(
            "🔤 Tempo scaduto, la parola era: {}.",
            anagrams.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::chain:
        return "🔗 La catena si è fermata da sola.";
    case Game::counting:
        return "🔢 Non siamo arrivati a 20. Come al solito.";
    case Game::whois:
        return closed.winner.empty() ? std::string{"🕵️ Nessuno l'ha indovinato."} : std::string{};
    case Game::mirror:
        return std::format(
            "🪞 Tempo scaduto: nessuno ha retto lo specchio. La parola era {}.",
            mirrors.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::rhyme:
        return std::format(
            "🎤 Tempo scaduto: nessuna rima con {}.",
            rhymes.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::target:
        return std::format("🎯 Nessuno ha centrato i {} caratteri.", closed.secret);
    case Game::closest:
        return closed.winner.empty()
            ? std::format("👁️ Il numero era {} e non l'ha cercato nessuno.", closed.secret)
            : std::format(
                  "👁️ Il numero era {}: ci è andato più vicino {} e si prende {} palle.",
                  closed.secret,
                  closed.winner,
                  closed.pot
              );
    case Game::cards:
        return closed.winner.empty()
            ? std::string{"🃏 Nessuno ha pescato: il mazzo resta chiuso."}
            : std::format(
                  "🃏 Carta più alta a {} con un {}: {} palle.",
                  closed.winner,
                  closed.secret,
                  closed.pot
              );
    case Game::maths:
        return std::format("🧮 Tempo scaduto: faceva {}.", closed.secret);
    case Game::countdown:
        return "⏳ Non siamo arrivati a 1. Il conto alla rovescia si è perso per strada.";
    case Game::capital:
        return std::format(
            "🌍 Tempo scaduto, la capitale era {}.",
            capitals.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::emoji:
        return std::format(
            "😀 Tempo scaduto, la parola era {}.",
            emojis.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::acrostic:
        return "🅰️ Nessuno ha messo in fila quattro parole con la stessa lettera.";
    case Game::novowels:
        return "🚫 Le vocali hanno resistito: nessuno è riuscito a farne a meno.";
    case Game::palindrome:
        return "🔄 Nessun palindromo. Il tempo scorre in una direzione sola.";
    case Game::shortest:
        return closed.winner.empty()
            ? std::string{"🐜 Nessuna parola abbastanza corta."}
            : std::format(
                  "🐜 Ha vinto {} con una parola di {} lettere: {} palle.",
                  closed.winner,
                  closed.secret,
                  closed.pot
              );
    case Game::river:
        return closed.winner.empty()
            ? std::string{"📜 Nessuno ha avuto niente da dire."}
            : std::format(
                  "📜 Ha vinto {} con {} parole in un fiato: {} palle.",
                  closed.winner,
                  closed.secret,
                  closed.pot
              );
    case Game::sum:
        return std::format("➕ Tempo scaduto: a {} non ci è arrivato nessuno.", closed.secret);
    case Game::year:
        return std::format(
            "📅 Tempo scaduto, era il {}.",
            years.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::proverb:
        return std::format(
            "📖 Tempo scaduto: «{}».",
            proverbs.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::roulette:
        return "🎡 La ruota si ferma da sola: nessuno ha puntato.";
    case Game::hangman:
        return std::format(
            "🪢 Tempo scaduto, la parola era {}.",
            mirrors.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::colour:
        return std::format(
            "🎨 Tempo scaduto: pensavo al {}.",
            colours.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::animal:
        return std::format(
            "🐘 Tempo scaduto, era {}.",
            animals.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::copy:
        return "⌨️ Nessuno ha ricopiato niente. Comprensibile.";
    case Game::alphabet:
        return "🔠 L'alfabeto si è fermato prima della J.";
    case Game::hotcold:
        return std::format("🌡️ Tempo scaduto: il numero era {} e si è raffreddato.", closed.secret);
    case Game::letters:
        return std::format(
            "🔡 Tempo scaduto: nessuna parola con {} dentro.",
            triples.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::song:
        return std::format(
            "🎵 Tempo scaduto: faceva {}.",
            songs.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::city:
        return std::format("🏙️ Nessuna città che cominci per {}. Strano paese.", static_cast<char>(closed.secret));
    case Game::dish:
        return std::format(
            "🍝 Tempo scaduto: ci voleva {}.",
            dishes.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::hidden:
        return std::format(
            "🧩 Tempo scaduto: la parola nascosta era {}.",
            mirrors.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::syllable:
        return std::format(
            "🔤 Tempo scaduto: nessuna parola con {} dentro.",
            syllables.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::film:
        return std::format(
            "🎬 Tempo scaduto: era {}.",
            films.at(static_cast<std::size_t>(closed.secret)).answer
        );
    case Game::series:
        return std::format("🧠 Tempo scaduto: veniva {}.", closed.secret);
    case Game::coin:
        return "🪙 La moneta è rimasta in tasca: nessuno ha chiamato.";
    case Game::trafficlight:
        return closed.secret == 1
            ? std::string{"🚦 Era verde, e nessuno è passato."}
            : std::string{"🚦 Era rosso, e per una volta avete fatto bene a stare fermi."};
    case Game::ends:
        return "🔡 Nessuna parola con quelle due lettere agli estremi.";
    case Game::slot:
        return "🎰 La slot si spegne: nessuna combinazione buona.";
    case Game::stopwatch:
        return "🕐 Trenta secondi passati senza che nessuno li azzeccasse.";
    case Game::order:
        return "🔀 Nessuno le ha messe in ordine. L'alfabeto aspetta.";
    case Game::sealed:
        return closed.winner.empty()
            ? std::string{"🕶️ Asta cieca deserta o finita in parità: il piatto resta lì."}
            : std::format(
                  "🕶️ Ha vinto {} offrendo {}: {} palle in cassa, meno l'offerta.",
                  closed.winner,
                  closed.detail,
                  closed.pot
              );
    case Game::unique:
        return closed.winner.empty()
            ? std::string{"🎯 Nessun numero solitario: avete pensato tutti la stessa cosa."}
            : std::format(
                  "🎯 Il numero unico più basso era {} ed è di {}: {} palle.",
                  closed.detail,
                  closed.winner,
                  closed.pot
              );
    case Game::average:
        return closed.winner.empty()
            ? std::string{"📊 Nessun numero, nessuna media."}
            : std::format(
                  "📊 I due terzi della media facevano {}: più vicino {}, che si prende {} palle.",
                  closed.secret,
                  closed.winner,
                  closed.pot
              );
    case Game::pyramid:
        return closed.winner.empty()
            ? std::format("🏗️ Il tetto era {} e lo avete superato tutti.", closed.secret)
            : std::format(
                  "🏗️ Il tetto era {}: {} si è fermato a {} e prende {} palle.",
                  closed.secret,
                  closed.winner,
                  closed.detail,
                  closed.pot
              );
    case Game::potato:
        return "🥔 La patata si è raffreddata da sola: nessuno l'ha toccata.";
    case Game::chairs:
        return "🪑 La musica si ferma: le sedie restano vuote.";
    case Game::russian:
        return closed.table.empty()
            ? std::string{"🔫 Nessuno ha avuto il fegato di premere."}
            : std::format("🔫 Sopravvissuti: {} palle a testa per chi è rimasto in piedi.", closed.secret);
    case Game::climb:
        return closed.secret == 0
            ? std::string{"🧗 Parete deserta: nessuno ha provato a salire."}
            : std::format("🧗 Scendono in {} con le palle in tasca.", closed.secret);
    case Game::bank:
        return closed.winner.empty()
            ? std::string{"🃏 Sballati tutti: il banco ringrazia."}
            : std::format("🃏 {} chiude a {} e si prende {} palle.", closed.winner, closed.secret, closed.pot);
    case Game::collect:
        return std::format(
            "🤝 La colletta si chiude a {} su {}.",
            closed.detail,
            closed.secret
        );
    case Game::trial:
        if (closed.winner.empty()) {
            return "⚖️ Processo senza giuria: il fascicolo torna in archivio.";
        }
        return closed.detail == "colpevole"
            ? std::format("⚖️ {} è colpevole e paga {} palle a chi lo ha accusato.", closed.winner, closed.secret)
            : std::format("⚖️ {} è assolto: gli accusatori gli versano {} palle.", closed.winner, closed.secret);
    case Game::bounty:
        return "💰 La taglia scade: nessuno ha fatto il nome, nessuno ha pagato.";
    case Game::siege:
        return "🏰 L'assedio si scioglie da solo. Le mura reggono.";
    case Game::market:
        return std::format("📈 Chiusura a {}: i conti sono fatti.", closed.secret);
    case Game::wager:
        return std::format("🎲 Totale {}, quindi {}. Pagati e incassati.", closed.secret, closed.detail);
    case Game::relay:
        return "🏃 Il testimone è caduto per terra. Niente per nessuno.";
    case Game::hostage:
        return closed.winner.empty()
            ? std::string{"🧨 Nessun ostaggio, nessun riscatto."}
            : std::format(
                  "🧨 Riscatto fermo a {} su {}: {} paga un decimo di tutto.",
                  closed.detail,
                  closed.secret,
                  closed.winner
              );
    case Game::legacy:
        return closed.winner.empty()
            ? std::string{"📜 Eredità contesa o nessuno si è fatto avanti: resta al notaio."}
            : std::format(
                  "📜 {} aveva chiesto solo {} e si prende tutte e {} le palle.",
                  closed.winner,
                  closed.detail,
                  closed.secret
              );
    case Game::customs:
        return closed.winner.empty()
            ? std::string{"🛃 Dogana chiusa: nessuno è passato."}
            : std::format("🛃 {} è passato {} volte e si prende {} palle.", closed.winner, closed.secret, closed.pot);
    case Game::marathon:
        return "🏃‍♂️ La maratona finisce senza traguardo. Restate a metà strada.";
    case Game::stars:
        return std::format(
            "♈ La casa di {} si chiude.",
            zodiac::element_name(static_cast<zodiac::Element>(closed.secret))
        );
    case Game::fraud:
        return closed.winner.empty()
            ? std::string{"🚨 Nessuna dichiarazione onesta. Il fascicolo passa alla Guardia di Finanza."}
            : std::format(
                  "🚨 {} ha dichiarato {} palle senza barare e si prende {} palle.",
                  closed.winner,
                  closed.secret,
                  closed.pot
              );
    case Game::scheme:
        return closed.winner.empty()
            ? std::string{"💹 Schema chiuso senza iscritti. Per una volta non ci è cascato nessuno."}
            : std::format(
                  "💹 La catena si ferma a {}: chi è entrato per ultimo, {}, resta con il cerino.",
                  closed.secret,
                  closed.winner
              );
    case Game::refund:
        if (closed.winner.empty()) {
            return "📦 Segnalazione chiusa senza pareri. Il supporto clienti archivia.";
        }
        return closed.detail == "rimborsato"
            ? std::format("📦 Il supporto clienti rimborsa {}: {} palle.", closed.winner, closed.secret)
            : std::format("📦 Segnalazione respinta: {} perde {} palle.", closed.winner, closed.secret);
    case Game::spy:
        if (closed.winner.empty()) {
            return "🕵️‍♀️ Nessuna spia, nessun sospetto.";
        }
        return closed.detail == "presa"
            ? std::format("🕵️‍♀️ La spia era {}: {} palle a testa a chi l'ha riconosciuta.", closed.winner, closed.secret)
            : std::format("🕵️‍♀️ La spia era {} e se ne va con {} palle.", closed.winner, closed.pot);
    case Game::plot:
        return closed.winner.empty()
            ? std::string{"🗡️ Congiura sciolta: troppi nomi e nessun accordo."}
            : std::format("🗡️ {} cade: {} palle ai congiurati.", closed.winner, closed.secret);
    case Game::dowry:
        return "💍 Nessuno ha detto sì. La dote resta in famiglia.";
    case Game::bingo:
        return "🎱 Tombola chiusa senza cartella piena.";
    case Game::horses:
        return closed.winner.empty()
            ? std::format("🐎 Vince il cavallo {} e non ci aveva puntato nessuno.", closed.secret)
            : std::format("🐎 Vince il cavallo {}: {} palle a chi ci credeva.", closed.secret, closed.pot);
    case Game::quake:
        return "🌋 La terra si ferma. Chi è rimasto in piedi, è rimasto in piedi.";
    case Game::war:
        return closed.winner.empty()
            ? std::string{"⚔️ Guerra finita in parità: tutti a casa come prima."}
            : std::format("⚔️ Vince il fronte {}: il piatto si divide fra i suoi.", closed.secret);
    case Game::deposit:
        return closed.detail == "fallita"
            ? std::format("🏦 La banca è fallita con {} depositi dentro. Sportelli chiusi.", closed.secret)
            : std::format("🏦 La banca regge: {} depositi restituiti con gli interessi.", closed.secret);
    case Game::talent:
        return closed.winner.empty()
            ? std::string{"🎤 Palco vuoto: nessuno si è esibito."}
            : std::format("🎤 Vince {} con {} parole diverse: {} palle.", closed.winner, closed.secret, closed.pot);
    case Game::treasure:
        return std::format(
            "🏹 Tempo scaduto: il tesoro era sotto {}.",
            mirrors.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::domino:
        return "🁣 Le tessere restano sul tavolo. Nessuno le rimette a posto.";
    case Game::tunnel:
        return "🚇 La galleria si ferma a metà. Torneranno domani con le pale.";
    case Game::dutch:
        return "📉 Prezzo sceso fino in fondo e nessuno ha detto prendo.";
    case Game::riddle:
        return std::format(
            "🗿 Tempo scaduto: la parola era {}.",
            mirrors.at(static_cast<std::size_t>(closed.secret))
        );
    case Game::navy:
        return std::format("🚢 La nave era alla casella {} e se ne va indisturbata.", closed.secret);
    case Game::election:
        return closed.winner.empty()
            ? std::string{"🗳️ Elezioni nulle: nessuna maggioranza."}
            : std::format("🗳️ {} è sindaco e incassa {} palle di tasse.", closed.winner, closed.secret);
    case Game::tug:
        return closed.winner.empty()
            ? std::string{"🪢 La fune resta in mezzo: pari e patta."}
            : std::format("🪢 Vince il lato {}: il piatto va a loro.", closed.detail);
    case Game::jenga:
        return closed.secret == 0
            ? std::string{"🏯 Nessuno ha osato toccare la torre."}
            : std::format("🏯 La torre resta in piedi dopo {} pezzi: piatto diviso fra i coraggiosi.", closed.secret);
    case Game::whispers:
        return "📞 La linea è caduta prima del quinto passaggio.";
    case Game::smuggle:
        return closed.winner.empty()
            ? std::string{"🕶️ Camion vuoto: la dogana non ha trovato niente da controllare."}
            : std::format("🕶️ Controllato il carico di {}: {} palle sequestrate, gli altri raddoppiano.", closed.winner, closed.secret);
    case Game::insurance:
        return closed.detail == "disastro"
            ? std::format("☂️ Disastro! I {} assicurati incassano cinquecento palle a testa.", closed.secret)
            : std::format("☂️ Nessun disastro: la compagnia si tiene i premi di {} assicurati.", closed.secret);
    case Game::strike:
        return closed.secret == 0
            ? std::string{"🪧 Sciopero finito senza cassa."}
            : std::format("🪧 Sciopero riuscito: {} palle in cassa, {} palle a testa per tutti.", closed.secret, closed.detail);
    case Game::contest:
        return closed.winner.empty()
            ? std::string{"📝 Concorso annullato per mancanza di idonei."}
            : std::format("📝 Vince {} con {} punti e si prende {} palle.", closed.winner, closed.secret, closed.pot);
    case Game::cadastre:
        return closed.secret == 0
            ? std::string{"🏚️ Tutti i lotti contesi: il catasto non registra niente."}
            : std::format("🏚️ {} lotti assegnati, {} palle di rendita a testa.", closed.secret, closed.detail);
    case Game::pilgrimage:
        return "⛪ Il pellegrinaggio si ferma per strada. Si riprova l'anno prossimo.";
    case Game::apocalypse:
        return closed.secret == 0
            ? std::string{"☄️ Arca vuota: non si è salvato nessuno."}
            : std::format("☄️ {} salvati si dividono il piatto, chi è rimasto giù perde un quinto.", closed.secret);
    }
    return {};
}

std::string game_ticked_reply(const GameTicked &ticked) {
    if (ticked.kind == Game::potato) {
        return ticked.player.empty()
            ? std::string{"🥔 La patata è scoppiata senza che nessuno la toccasse."}
            : std::format(
                  "💥 La patata è scoppiata in mano a {}, che paga {} palle.",
                  ticked.player,
                  ticked.palle
              );
    }
    if (ticked.kind == Game::strike) {
        return std::format("🪧 Lo sciopero tiene: in cassa {} palle.", ticked.number);
    }
    if (ticked.kind == Game::contest) {
        return std::format("📝 Domanda {}: {}", ticked.number, ticked.detail);
    }
    if (ticked.kind == Game::pilgrimage) {
        return std::format("⛪ Il sentiero riporta indietro: {} passi.", ticked.number);
    }
    if (ticked.kind == Game::apocalypse) {
        return std::format("☄️ {} è stato salvato. Restano giù in {}.", ticked.player, ticked.number);
    }
    if (ticked.kind == Game::dutch) {
        return std::format("📉 Il prezzo scende a {}.", ticked.number);
    }
    if (ticked.kind == Game::riddle) {
        return std::format("🗿 Indizio {}: {}. Ora vale {} palle.", ticked.number, ticked.detail, ticked.palle);
    }
    if (ticked.kind == Game::bingo) {
        return ticked.decided
            ? std::format("🎱 TOMBOLA! {} ha la cartella piena e si prende {} palle.", ticked.player, ticked.palle)
            : std::format("🎱 Esce il {}.", ticked.number);
    }
    if (ticked.kind == Game::horses) {
        return std::format("🐎 Il cavallo {} avanza: è a {} lunghezze.", ticked.number, ticked.palle);
    }
    if (ticked.kind == Game::quake) {
        return std::format(
            "🌋 Scossa: {} giocatori non si erano fatti sentire e lasciano {} palle fra le macerie.",
            ticked.number,
            ticked.palle
        );
    }
    if (ticked.kind == Game::chairs) {
        return ticked.decided
            ? std::format(
                  "🪑 {} si alza al giro {}. Resta seduto solo {}, che si prende {} palle.",
                  ticked.player,
                  ticked.number,
                  ticked.detail,
                  ticked.palle
              )
            : std::format("🪑 Giro {}: {} resta in piedi ed è fuori.", ticked.number, ticked.player);
    }
    return {};
}

std::string happening_reply(const HappeningResult &what) {
    switch (what.what) {
    case Happening::earthquake:
        return std::format(
            "🌍 TERREMOTO: la mappa si è rimescolata, tutti e {} i giocatori si svegliano da un'altra parte.",
            what.players
        );
    case Happening::amnesty:
        return std::format("🕊️ CONDONO: {} penalità cancellate, tutti liberi di riprovarci subito.", what.players);
    case Happening::rain:
        return std::format("🌧️ PIOGGIA DI PALLE: {} palle a testa per tutti e {}.", what.palle, what.players);
    case Happening::inflation:
        return std::format("📉 INFLAZIONE: un decimo delle palle di tutti e {} è andato in fumo.", what.players);
    case Happening::black_market:
        return std::format("🎈 MERCATO NERO: un palloncino è comparso in casa di {}, senza ricevuta.", what.player);
    case Happening::pedlar:
        return std::format(
            "🐛 Un negoziante ha venduto a {} un bruco che non aveva chiesto: {} palle andate, conto in rosso.",
            what.player,
            what.palle
        );
    case Happening::ministry:
        return what.players == 1
            ? std::format(
                  "🇮🇹 Il Ministero del Made in Italy ha certificato le palle di {}: valgono {} in più.",
                  what.player,
                  what.palle
              )
            : std::format(
                  "🇮🇹 Il Ministero del Made in Italy ha sequestrato {} palle contraffatte a {}.",
                  what.palle,
                  what.player
              );
    case Happening::twinning:
        return std::format(
            "👯 GEMELLAGGIO: {} e un altro si sono scambiati il portafoglio senza accorgersene.",
            what.player
        );
    case Happening::mirror:
        return std::format("🪞 SPECCHIO: la classifica si è ribaltata, tutti e {} al contrario.", what.players);
    case Happening::luxury_tax:
        return std::format(
            "💎 TASSA SUL LUSSO: {} palle prelevate a {} giocatori sopra la media.",
            what.palle,
            what.players
        );
    case Happening::daylight_saving:
        return std::format("🕐 ORA LEGALE: un'ora regalata a tutti, {} palle a testa.", what.palle);
    case Happening::strike:
        return std::format(
            "🚏 SCIOPERO DEI TRASPORTI: {} viaggi in corso sono finiti tutti insieme, dove capitava.",
            what.players
        );
    case Happening::rounding:
        return std::format("🧾 ARROTONDAMENTO: le palle di tutti e {} finiscono in tre zeri.", what.players);
    case Happening::famine:
        return std::format(
            "🍽️ CARITATEVOLE CARESTIA per {}: palloncino e boost spariti, resta solo la buona volontà.",
            what.player
        );
    }
    return {};
}

std::string mishap_reply(const MishapResult &mishap) {
    return std::vformat(mishaps.at(mishap.which).text, std::make_format_args(mishap.player));
}

std::string raid_started_reply(const std::string &raider, const std::string &target, std::int64_t seconds) {
    return std::format(
        "🚀 {} parti per {}: arrivi tra {}. {} resta scoperto.",
        raider,
        target,
        format_wait(seconds),
        raider
    );
}

std::string raid_event_reply(const RaidEvent &event, const zodiac::Overrides &signs) {
    const std::string_view mention = event.target_on_telegram ? "@" : "";
    const std::string home = std::format("{}{}", event.raider_on_telegram ? "@" : "", event.raider);
    if (event.kind == RaidEvent::Kind::returned) {
        if (event.loot > 0) {
            return std::format("🪐 {} sei tornato in {} con {} palle.", event.raider, home, event.loot);
        }
        return std::format("🪐 {} sei tornato in {} a mani vuote.", event.raider, home);
    }
    if (event.kind == RaidEvent::Kind::defended) {
        return std::format(
            "🎈 {} il palloncino di {} ha resistito{}. Torni in {} a mani vuote tra {}.",
            event.raider,
            event.target,
            event.cost > 0 ? std::format(" e ti costa {} palle", event.cost) : "",
            home,
            format_wait(event.seconds)
        );
    }
    std::string reply = std::format(
        "💰 {} hai rubato {} palle a {}{}",
        event.raider,
        event.loot,
        mention,
        event.target
    );
    if (event.undefended) {
        reply += ", che era in giro";
    } else if (event.balloon_popped) {
        reply += ", bucandogli il palloncino";
    }
    if (event.raider_percent != event.target_percent) {
        const zodiac::Sign raider = zodiac::sign_of(event.raider, signs);
        const zodiac::Sign target = zodiac::sign_of(event.target, signs);
        reply += std::format(
            " ({} {} contro {} {}: {}/{})",
            raider.symbol,
            raider.name,
            target.symbol,
            target.name,
            event.raider_percent,
            event.target_percent
        );
    }
    reply += std::format("! Consegna prevista in {} tra {}.", home, format_wait(event.seconds));
    if (event.denounced) {
        reply += std::format(
            "\n🚨 {} la tua simpatia è scesa a {}: SEI ANTIPATICO. Ti denuncio alla Guardia di Finanza "
            "per evasione fiscale.",
            event.raider,
            event.simpatia
        );
    }
    return reply;
}

namespace {

/* Whether the message says that one word, on its own and not inside another. */
bool says_the_word(std::string_view message, std::string_view word) {
    if (word.empty()) {
        return false;
    }
    std::size_t at = 0;
    while ((at = message.find(word, at)) != std::string_view::npos) {
        const bool before = at == 0 || std::isalpha(static_cast<unsigned char>(message[at - 1])) == 0;
        const std::size_t after_at = at + word.size();
        const bool after = after_at >= message.size() ||
                           std::isalpha(static_cast<unsigned char>(message[after_at])) == 0;
        if (before && after) {
            return true;
        }
        at = after_at;
    }
    return false;
}

/* One word, and everything happens at once. */
std::optional<std::string> cascade_reply(const CommandContext &context, std::string_view lowered) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const auto word = std::ranges::find_if(context.config.cascade_words, [lowered](const std::string &said) {
        return says_the_word(lowered, text::to_lower_copy(said));
    });
    if (word == context.config.cascade_words.end()) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const std::vector<FlipperResult> chain = cascade(
        context.storage,
        username,
        context.config.cascade_most,
        seconds_now(),
        context.config.boost_multiplier
    );
    if (chain.empty()) {
        return std::nullopt;
    }
    std::string reply = std::format("🌀 {} ha detto \"{}\" e si è messo in moto tutto:", username, *word);
    for (const FlipperResult &hit : chain) {
        reply += "\n";
        reply += std::vformat(flippers.at(hit.which).text, std::make_format_args(username));
    }
    reply += std::format("\n💰 Alla fine gliene restano {}.", chain.back().score);
    return reply;
}

/* Words that carry luck, good or bad, for whoever says them. */
std::optional<std::string> lucky_word_reply(const CommandContext &context, std::string_view lowered) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const auto said = std::ranges::find_if(context.config.lucky_words, [lowered](const std::string &word) {
        return says_the_word(lowered, text::to_lower_copy(word));
    });
    if (said == context.config.lucky_words.end()) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const LuckyResult luck = lucky_word_said(context.storage, username, context.config.lucky_swing);
    if (luck.palle == 0) {
        return std::format("🎲 {} ha detto \"{}\" e non è successo niente.", username, *said);
    }
    return std::format(
        "🎲 {} ha detto \"{}\" e {} {} palle: ora ne ha {}.",
        username,
        *said,
        luck.palle > 0 ? "ne guadagna" : "ne perde",
        luck.palle > 0 ? luck.palle : -luck.palle,
        luck.score
    );
}

/* The pinball table under the chat: now and then a message hits a bumper. */
/* A word said twice in the same message. */
bool repeated_word(std::string_view message) {
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < message.size()) {
        const std::size_t end = std::min(message.find(' ', at), message.size());
        if (end > at + 2) {
            words.push_back(message.substr(at, end - at));
        }
        at = end + 1;
    }
    std::ranges::sort(words);
    return std::ranges::adjacent_find(words) != words.end();
}

/* The largest number written in a message, or -1 when there is none. */
std::int64_t number_said(std::string_view message) {
    std::int64_t found = -1;
    std::int64_t current = 0;
    bool reading = false;
    for (const char character : message) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            current = std::min<std::int64_t>(current * 10 + (character - '0'), 1000000000);
            reading = true;
        } else if (reading) {
            found = std::max(found, current);
            current = 0;
            reading = false;
        }
    }
    return reading ? std::max(found, current) : found;
}

/* The seventeenth of the month, which around here is nobody's friend. */
bool is_the_seventeenth(std::int64_t now) {
    const std::chrono::sys_seconds instant{std::chrono::seconds{now}};
    const std::chrono::year_month_day today{std::chrono::floor<std::chrono::days>(instant)};
    return static_cast<unsigned>(today.day()) == 17;
}

std::optional<std::string> flipper_reply(
    const CommandContext &context,
    std::string_view lowered,
    std::int64_t now
) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    /* A word off the list is a target hit; otherwise it is down to the odds. */
    const bool said_one = std::ranges::any_of(context.config.flipper_words, [lowered](const std::string &word) {
        return says_the_word(lowered, text::to_lower_copy(word));
    });
    const std::string username{context.username};
    const std::optional<FlipperResult> first = flipper_hit(
        context.storage,
        username,
        said_one ? 1 : context.config.flipper_odds,
        now,
        context.config.boost_multiplier
    );
    if (!first) {
        return std::nullopt;
    }

    /* The ball bounces on, and three old superstitions decide how far. */
    int bounces = context.config.flipper_chain - 1;
    std::string extra;
    if (repeated_word(lowered)) {
        bounces += context.config.flipper_chain;
        extra += "\n🔁 ECO: la parola ripetuta vale doppio.";
    }
    if (is_last(context.storage, username)) {
        bounces += 1;
        extra += "\n🍀 FORTUNA DEL PRINCIPIANTE: un colpo in più all'ultimo della classe.";
    }
    if (is_the_seventeenth(now)) {
        bounces += 1;
        extra += "\n🔮 VENERDÌ 17: un colpo in più, e non è un regalo.";
    }
    std::vector<FlipperResult> chain{*first};
    const std::vector<FlipperResult> rest =
        cascade(context.storage, username, bounces, now, context.config.boost_multiplier);
    chain.insert(chain.end(), rest.begin(), rest.end());

    std::string said;
    for (const FlipperResult &hit : chain) {
        said += said.empty() ? "" : "\n";
        said += std::vformat(flippers.at(hit.which).text, std::make_format_args(username));
    }
    said += extra;
    /* Whatever happened to him happens, one place up, to the man above. */
    const std::int64_t moved = std::accumulate(
        chain.begin(),
        chain.end(),
        std::int64_t{0},
        [](std::int64_t so_far, const FlipperResult &hit) { return so_far + hit.palle; }
    );
    if (moved != 0) {
        if (const std::string above = domino(context.storage, username, moved); !above.empty()) {
            said += std::format("\n🁣 EFFETTO DOMINO: anche {} si vede muovere {} palle.", above, moved);
        }
    }
    said += std::format("\n💰 Totale: {} palle.", chain.back().score);
    return said;
}

/* The game under way, which answers to whatever gets written. */
std::optional<std::string> game_reply(const CommandContext &context, std::string_view message, std::int64_t now) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const std::optional<GamePlayed> played = game_play(context.storage, username, message, now);
    if (!played) {
        return std::nullopt;
    }
    switch (played->kind) {
    case Game::race:
        return std::format("🏁 {} è arrivato primo e si prende {} palle.", played->player, played->palle);
    case Game::guess:
        return std::format(
            "🔢 {} ha indovinato: era {}. Si prende {} palle.",
            played->player,
            played->number,
            played->palle
        );
    case Game::auction:
        return std::format("🔨 {} offre {}. Qualcuno dà di più?", played->player, played->number);
    case Game::forbidden:
        return std::format(
            "🤐 {} ha detto la parola proibita e paga {} palle.",
            played->player,
            played->palle
        );
    case Game::sequence:
        return std::format("🧠 {} se l'è ricordato: {} palle.", played->player, played->palle);
    case Game::longest:
        return std::format("📏 {} passa in testa con {} lettere.", played->player, played->number);
    case Game::silence:
        return std::format("🤫 {} ha parlato per primo e paga {} palle.", played->player, played->palle);
    case Game::quiz:
        return std::format("❓ {} ha risposto giusto e si prende {} palle.", played->player, played->palle);
    case Game::anagram:
        return std::format("🔤 {} ha sciolto l'anagramma e si prende {} palle.", played->player, played->palle);
    case Game::chain:
        return played->palle > 0
            ? std::format(
                  "🔗 {} ha sbagliato lettera e paga {} palle. Catena rotta.",
                  played->player,
                  played->palle
              )
            : std::format("🔗 {} tiene la catena: la prossima comincia per {}.", played->player,
                          static_cast<char>(played->number));
    case Game::counting:
        return played->palle > 0 && played->number >= 20
            ? std::format("🔢 {} ha detto 20 e si prende {} palle.", played->player, played->palle)
            : (played->palle > 0
                   ? std::format("🔢 {} ha sbagliato numero e paga {} palle.", played->player, played->palle)
                   : std::format("🔢 {}. Avanti il prossimo.", played->number));
    case Game::whois:
        return std::format("🕵️ {} l'ha indovinato e si prende {} palle.", played->player, played->palle);
    case Game::mirror:
        return std::format("🪞 {} l'ha letta al contrario e si prende {} palle.", played->player, played->palle);
    case Game::rhyme:
        return std::format("🎤 {} ha trovato la rima e si prende {} palle.", played->player, played->palle);
    case Game::target:
        return std::format(
            "🎯 {} ha centrato i {} caratteri e si prende {} palle.",
            played->player,
            played->number,
            played->palle
        );
    case Game::closest:
        return std::format("👁️ {} dice {} ed è il più vicino per ora.", played->player, played->number);
    case Game::cards:
        return std::format("🃏 {} pesca un {}.", played->player, played->number);
    case Game::maths:
        return std::format("🧮 {} ha fatto il conto: {}. Si prende {} palle.", played->player, played->number, played->palle);
    case Game::countdown:
        return played->palle > 0 && played->number <= 1
            ? std::format("⏳ {} ha detto 1 e si prende {} palle.", played->player, played->palle)
            : (played->palle > 0
                   ? std::format("⏳ {} ha sbagliato numero e paga {} palle.", played->player, played->palle)
                   : std::format("⏳ {}. Si scende.", played->number));
    case Game::capital:
        return std::format("🌍 {} conosce la geografia e si prende {} palle.", played->player, played->palle);
    case Game::emoji:
        return std::format("😀 {} ha letto le faccine e si prende {} palle.", played->player, played->palle);
    case Game::acrostic:
        return std::format(
            "🅰️ {} ne ha allineate {} di fila e si prende {} palle.",
            played->player,
            played->number,
            played->palle
        );
    case Game::novowels:
        return std::format("🚫 {} ha scritto {} lettere senza vocali: {} palle.", played->player, played->number, played->palle);
    case Game::palindrome:
        return std::format("🔄 {} ha scritto {} e si legge uguale: {} palle.", played->player, played->detail, played->palle);
    case Game::shortest:
        return std::format("🐜 {} scende a {} lettere.", played->player, played->number);
    case Game::river:
        return std::format("📜 {} passa in testa con {} parole.", played->player, played->number);
    case Game::sum:
        if (played->detail == "sforato") {
            return std::format(
                "➕ {} sfora con {} e paga {} palle.",
                played->player,
                played->number,
                played->palle
            );
        }
        if (played->detail == "centrato") {
            return std::format("➕ {} centra la somma e si prende {} palle.", played->player, played->palle);
        }
        return std::format("➕ Siamo a {}.", played->number);
    case Game::year:
        return std::format("📅 {} si ricordava l'anno e si prende {} palle.", played->player, played->palle);
    case Game::proverb:
        return std::format("📖 {} l'ha finito come la nonna e si prende {} palle.", played->player, played->palle);
    case Game::roulette:
        return played->palle > 0
            ? std::format("🎡 Esce il {}! {} si prende {} palle.", played->number, played->player, played->palle)
            : std::format("🎡 Esce il {}: {} guarda la ruota fermarsi altrove.", played->number, played->player);
    case Game::hangman:
        return played->palle > 0
            ? std::format("🪢 {} ha detto {} e si prende {} palle.", played->player, played->detail, played->palle)
            : std::format("🪢 {}: {}", played->player, played->detail);
    case Game::colour:
        return std::format("🎨 {} ha indovinato il colore e si prende {} palle.", played->player, played->palle);
    case Game::animal:
        return std::format("🐘 {} ha riconosciuto la bestia e si prende {} palle.", played->player, played->palle);
    case Game::copy:
        return std::format("⌨️ {} ha copiato {} senza sbagliare: {} palle.", played->player, played->detail, played->palle);
    case Game::alphabet:
        return played->palle > 0 && played->number >= static_cast<std::int64_t>('j')
            ? std::format("🔠 {} ha chiuso l'alfabeto e si prende {} palle.", played->player, played->palle)
            : (played->palle > 0
                   ? std::format("🔠 {} ha sbagliato lettera e paga {} palle.", played->player, played->palle)
                   : std::format("🔠 {} passa alla {}.", played->player, static_cast<char>(played->number)));
    case Game::hotcold:
        return played->palle > 0
            ? std::format("🌡️ {} ha trovato il {} e si prende {} palle.", played->player, played->number, played->palle)
            : std::format("🌡️ {} dice {}: {}.", played->player, played->number, played->detail);
    case Game::letters:
        return std::format("🔡 {} ha trovato {} e si prende {} palle.", played->player, played->detail, played->palle);
    case Game::song:
        return std::format("🎵 {} la sapeva a memoria e si prende {} palle.", played->player, played->palle);
    case Game::city:
        return std::format("🏙️ {} dice {} e si prende {} palle.", played->player, played->detail, played->palle);
    case Game::dish:
        return std::format("🍝 {} sa cosa ci va dentro e si prende {} palle.", played->player, played->palle);
    case Game::hidden:
        return std::format("🧩 {} ha trovato {} lì in mezzo: {} palle.", played->player, played->detail, played->palle);
    case Game::syllable:
        return std::format("🔤 {} tira fuori {} e si prende {} palle.", played->player, played->detail, played->palle);
    case Game::film:
        return std::format("🎬 {} ha riconosciuto il film e si prende {} palle.", played->player, played->palle);
    case Game::series:
        return std::format("🧠 {} ha capito come va avanti: {}. Si prende {} palle.", played->player, played->number, played->palle);
    case Game::coin:
        return played->palle > 0
            ? std::format("🪙 Esce {}: {} si prende {} palle.", played->detail, played->player, played->palle)
            : std::format("🪙 Esce {}: {} aveva detto l'altra.", played->detail, played->player);
    case Game::trafficlight:
        return played->number == 1
            ? std::format("🚦 Verde! {} passa e si prende {} palle.", played->player, played->palle)
            : std::format("🚦 Rosso. {} è passato lo stesso e paga {} palle.", played->player, played->palle);
    case Game::ends:
        return std::format("🔡 {} ha trovato {} e si prende {} palle.", played->player, played->detail, played->palle);
    case Game::slot:
        return played->decided
            ? std::format("🎰 {} — tre uguali! {} si prende {} palle.", played->detail, played->player, played->palle)
            : (played->palle > 0
                   ? std::format("🎰 {} — due uguali: {} palle a {}.", played->detail, played->palle, played->player)
                   : std::format("🎰 {} — niente per {}.", played->detail, played->player));
    case Game::stopwatch:
        return std::format("🕐 {} è arrivato al secondo giusto e si prende {} palle.", played->player, played->palle);
    case Game::order:
        return std::format("🔀 {} le ha messe in fila e si prende {} palle.", played->player, played->palle);
    case Game::sealed:
    case Game::unique:
    case Game::average:
    case Game::pyramid:
        return std::format("🤐 Offerta di {} registrata. Nessuno la vede fino alla chiusura.", played->player);
    case Game::potato:
        return std::format("🥔 La patata passa a {}. Scotta.", played->player);
    case Game::chairs:
        return std::format("🪑 {} si siede: giro {}.", played->player, played->number);
    case Game::russian:
        return played->detail == "click"
            ? std::format("🔫 Click. {} è ancora qui.", played->player)
            : std::format("🔫 BANG! {} esce di scena e paga {} palle.", played->player, played->palle);
    case Game::climb:
        if (played->detail == "dentro") {
            return std::format("🧗 {} entra mettendo cento palle sulla parete.", played->player);
        }
        if (played->detail == "caduto") {
            return std::format("🧗 {} è precipitato e lascia lì {} palle.", played->player, played->palle);
        }
        return std::format("🧗 {} sale: {} palle appese al chiodo.", played->player, played->number);
    case Game::bank:
        return played->detail == "sballato"
            ? std::format("🃏 {} pesca un {} e sballa.", played->player, played->number)
            : std::format("🃏 {} pesca un {} ed è a {}.", played->player, played->number, played->palle);
    case Game::collect:
        return std::format(
            "🤝 {} versa {} palle: siamo a {}.",
            played->player,
            played->palle,
            played->number
        );
    case Game::trial:
        return std::format("⚖️ {} vota {}. Voti raccolti: {}.", played->player, played->detail, played->number);
    case Game::bounty:
        return played->detail == "pagata"
            ? std::format("💰 {} si è comprato la taglia per {} palle.", played->player, played->palle)
            : std::format(
                  "💰 {} ha consegnato {} e incassa {} palle.",
                  played->player,
                  played->detail,
                  played->palle
              );
    case Game::siege:
        if (played->detail == "difende") {
            return std::format("🏰 {} respinge: si torna a {} colpi.", played->player, played->number);
        }
        if (played->detail == "caduto") {
            return std::format("🏰 Le mura cedono! {} entra e si prende {} palle.", played->player, played->palle);
        }
        return std::format("🏰 {} colpisce: {} colpi a segno.", played->player, played->number);
    case Game::market:
        if (played->detail == "prezzo") {
            return std::format("📈 Il titolo si muove: {}.", played->number);
        }
        return std::format("📈 {} {} a {}.", played->player, played->detail, played->number);
    case Game::wager:
        return std::format("🎲 {} punta {} palle.", played->player, played->number);
    case Game::relay:
        return played->decided
            ? std::format("🏃 Sesto passaggio! {} palle a testa per chi ha corso.", played->palle)
            : std::format("🏃 {} passa il testimone: {} su 6.", played->player, played->number);
    case Game::hostage:
        return played->decided
            ? std::format("🧨 Riscatto pagato: {} è libero.", played->detail)
            : std::format("🧨 {} versa {} palle: siamo a {}.", played->player, played->palle, played->number);
    case Game::legacy:
        return std::format("📜 La richiesta di {} è agli atti.", played->player);
    case Game::customs:
        return played->palle > 0
            ? std::format(
                  "🛃 Sequestro: {} aveva {} addosso e paga {} palle.",
                  played->player,
                  played->detail,
                  played->palle
              )
            : std::format("🛃 {} passa il controllo: {} volte.", played->player, played->number);
    case Game::marathon:
        return played->decided
            ? std::format("🏃‍♂️ {} taglia il traguardo e si prende {} palle.", played->player, played->palle)
            : std::format("🏃‍♂️ {} parole coperte.", played->number);
    case Game::stars:
        if (played->number > 0) {
            return std::format("♈ {} è {} ed è in casa: {} palle.", played->player, played->detail, played->palle);
        }
        if (played->number < 0) {
            return std::format("♈ {} è {}, casa opposta: lascia {} palle.", played->player, played->detail, played->palle);
        }
        return std::format("♈ {} è {}: oggi né carne né pesce.", played->player, played->detail);
    case Game::fraud:
        return std::format("🚨 {} dichiara {} palle. Agli atti.", played->player, played->number);
    case Game::scheme:
        return std::format(
            "💹 {} entra per {} e a chi c'era già vanno {} palle a testa.",
            played->player,
            played->number,
            played->palle
        );
    case Game::refund:
        return std::format("📦 {} dice {}. Segnalazioni: {}.", played->player, played->detail, played->number);
    case Game::spy:
        return std::format("🕵️‍♀️ {} fa il nome di {}.", played->player, played->detail);
    case Game::plot:
        return std::format("🗡️ {} ha messo una croce su un nome.", played->player);
    case Game::dowry:
        return played->decided
            ? std::format("💍 {} e {} si dividono la dote: {} palle a testa.", played->player, played->detail, played->palle)
            : std::format("💍 {} chiede la mano di {}. Si aspetta risposta.", played->player, played->detail);
    case Game::bingo:
        return std::format("🎱 Cartella a {}.", played->player);
    case Game::horses:
        return std::format("🐎 {} punta sul cavallo {}.", played->player, played->number);
    case Game::quake:
        return std::format("🌋 {} si fa sentire e resta in piedi.", played->player);
    case Game::war:
        return std::format("⚔️ {} combatte per il fronte {}: forza {}.", played->player, played->number, played->palle);
    case Game::deposit:
        return std::format("🏦 {} deposita {} palle.", played->player, played->palle);
    case Game::talent:
        return std::format("🎤 {} passa in testa con {} parole diverse.", played->player, played->number);
    case Game::treasure:
        return played->decided
            ? std::format("🏹 {} ha trovato {} e si prende {} palle.", played->player, played->detail, played->palle)
            : std::format("🏹 {} scava sotto {}: niente, e venti palle in meno.", played->player, played->detail);
    case Game::domino:
        return played->decided
            ? std::format("🁣 {} attacca male la tessera e paga {} palle.", played->player, played->palle)
            : std::format("🁣 {} tessere in fila, si prosegue dal {}.", played->palle, played->number);
    case Game::tunnel:
        return played->decided
            ? std::format("🚇 Otto metri di galleria! {} palle a testa a chi ha scavato.", played->palle)
            : std::format("🚇 {} scava: {} su 8.", played->player, played->number);
    case Game::dutch:
        return std::format(
            "📉 {} compra a {} e porta a casa {} palle di differenza.",
            played->player,
            played->number,
            played->palle
        );
    case Game::riddle:
        return std::format("🗿 {} ha sciolto l'enigma: era {}. {} palle.", played->player, played->detail, played->palle);
    case Game::navy:
        return played->detail == "colpito"
            ? std::format("🚢 Colpito e affondato! {} centra la {} e prende {} palle.", played->player, played->number, played->palle)
            : std::format("🚢 Acqua alla {}: {} palle in meno per {}.", played->number, played->palle, played->player);
    case Game::election:
        return std::format("🗳️ {} vota {}.", played->player, played->detail);
    case Game::tug:
        return std::format("🪢 {} strattona: la fune è a {}.", played->player, played->palle);
    case Game::jenga:
        return played->detail == "crollo"
            ? std::format("🏯 La torre viene giù addosso a {}, che paga {} palle.", played->player, played->palle)
            : std::format("🏯 {} sfila il pezzo numero {}. Regge.", played->player, played->number);
    case Game::whispers:
        return played->decided
            ? std::format("📞 Cinque passaggi: {} palle a testa a chi era in linea.", played->palle)
            : std::format("📞 {} sente {}: {} su 5.", played->player, played->detail, played->number);
    case Game::smuggle:
        return std::format("🕶️ {} carica {} palle nel camion.", played->player, played->palle);
    case Game::insurance:
        return std::format("☂️ {} si assicura per cento palle.", played->player);
    case Game::strike:
        return std::format(
            "🪧 {} ha rotto lo sciopero a {} palle di cassa e paga {} palle.",
            played->player,
            played->number,
            played->palle
        );
    case Game::contest:
        return std::format("📝 Risposta esatta di {}: {} punti.", played->player, played->number);
    case Game::cadastre:
        return std::format("🏚️ {} mette gli occhi sul lotto {}.", played->player, played->number);
    case Game::pilgrimage:
        return played->decided
            ? std::format("⛪ Arrivati! {} palle a testa ai pellegrini.", played->palle)
            : std::format("⛪ {} passi fatti.", played->number);
    case Game::apocalypse:
        return std::format("☄️ {} sale sull'arca. A bordo in {}.", played->player, played->number);
    }
    return std::nullopt;
}

/* Il nome di un gioco, detto in mezzo a qualsiasi frase, apre quel gioco. Uno alla volta. */
std::optional<std::string> game_called(const CommandContext &context, std::string_view message, std::int64_t now) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const std::optional<Game> wanted = game_named(message);
    if (!wanted) {
        return std::nullopt;
    }
    const std::optional<GameOpened> opened = game_open(
        context.storage,
        now,
        context.config.game_open_seconds,
        context.config.game_pot,
        wanted
    );
    if (!opened) {
        return std::nullopt;
    }
    return game_opened_reply(*opened);
}

/* Morra cinese e pari o dispari: si gioca dicendo la parola, e si chiude subito. */
std::optional<std::string> hand_reply(const CommandContext &context, std::string_view lowered) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const std::array<std::pair<std::string_view, Hand>, 3> hands{{
        {"sasso", Hand::rock},
        {"carta", Hand::paper},
        {"forbice", Hand::scissors},
    }};
    for (const auto &[word, hand] : hands) {
        if (!says_the_word(lowered, word)) {
            continue;
        }
        const HandResult played = play_hand(context.storage, username, hand);
        static constexpr std::array names{"sasso", "carta", "forbice"};
        const std::string_view mine = names.at(static_cast<std::size_t>(played.theirs));
        if (played.outcome == 0) {
            return std::format("✊ {} contro {}: pareggio, tutto fermo.", word, mine);
        }
        return played.outcome > 0
            ? std::format("✊ {} batte {}: {} vince {} palle.", word, mine, username, played.palle)
            : std::format("✊ {} batte {}: {} perde {} palle.", mine, word, username, played.palle);
    }

    const bool even = says_the_word(lowered, "pari");
    if (even || says_the_word(lowered, "dispari")) {
        const std::int64_t said = std::max<std::int64_t>(number_said(lowered), 0);
        const HandResult played = play_parity(context.storage, username, even, said);
        return played.outcome > 0
            ? std::format("🤞 {} ha detto {} e ha vinto {} palle.", username, even ? "pari" : "dispari", played.palle)
            : std::format("🤞 {} ha detto {} e ha perso {} palle.", username, even ? "pari" : "dispari", played.palle);
    }
    return std::nullopt;
}

/* Whoever asks for a duel gets one, against whoever the bot picks. */
std::optional<std::string> duel_reply(const CommandContext &context, std::string_view lowered) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    if (!says_the_word(lowered, "duello")) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const DuelResult fight = duel(context.storage, username, seconds_now());
    if (!fight.fought) {
        return std::nullopt;
    }
    return fight.won
        ? std::format(
              "⚔️ {} ha sfidato a duello {} e ha vinto: {} palle passano di mano.",
              username,
              fight.other,
              fight.palle
          )
        : std::format(
              "⚔️ {} ha sfidato a duello {} e ha perso: {} palle passano di mano.",
              username,
              fight.other,
              fight.palle
          );
}

/* While the draw is open, talking is buying. */
std::optional<std::string> lottery_reply(const CommandContext &context, std::int64_t now) {
    if (context.username.empty() || !context.claims_allowed || context.config.lottery_min_seconds <= 0) {
        return std::nullopt;
    }
    const std::string username{context.username};
    const std::optional<std::int64_t> tickets =
        lottery_buy(context.storage, username, context.config.lottery_ticket, now);
    if (!tickets || *tickets != 1) {
        return std::nullopt;
    }
    return std::format(
        "🎟️ {} ha comprato un biglietto della lotteria per {} palle.",
        username,
        context.config.lottery_ticket
    );
}

/* The words that work the small spells, in the order they are tried. */
std::optional<std::string> spell_reply(const CommandContext &context, std::string_view lowered) {
    if (context.username.empty() || !context.claims_allowed) {
        return std::nullopt;
    }
    const AppConfig &config = context.config;
    const std::array<std::pair<const std::vector<std::string> *, Spell>, 7> spells{{
        {&config.magic_words, Spell::multiply},
        {&config.bet_words, Spell::bet},
        {&config.alms_words, Spell::alms},
        {&config.charisma_words, Spell::charisma},
        {&config.taunt_words, Spell::taunt},
        {&config.sixseven_words, Spell::sixseven},
        {&config.blessing_words, Spell::blessing},
    }};
    for (const auto &[words, spell] : spells) {
        const auto said = std::ranges::find_if(*words, [lowered](const std::string &word) {
            return says_the_word(lowered, text::to_lower_copy(word));
        });
        if (said == words->end()) {
            continue;
        }
        const std::string username{context.username};
        const std::optional<SpellResult> cast =
            spell_cast(context.storage, username, spell, seconds_now());
        if (!cast) {
            return std::nullopt;
        }
        switch (spell) {
        case Spell::multiply:
            return std::format(
                "🥤 {} ha detto \"{}\" · MOUNTAIN DEW: le palle si moltiplicano per {} e diventano {}.",
                username,
                *said,
                cast->multiplier,
                cast->score
            );
        case Spell::bet:
            return cast->palle > 0
                ? std::format(
                      "💥 {} ha detto \"{}\" · 360 NOSCOPE: le palle raddoppiano a {}.",
                      username,
                      *said,
                      cast->score
                  )
                : std::format(
                      "🎯 {} ha detto \"{}\" · HITMARKER mancato: le palle si dimezzano a {}.",
                      username,
                      *said,
                      cast->score
                  );
        case Spell::alms:
            return std::format(
                "📢 {} ha detto \"{}\" e ha passato {} palle a {}, che era l'ultimo di tutti.",
                username,
                *said,
                cast->palle,
                cast->other
            );
        case Spell::charisma:
            return std::format(
                "🔺 {} ha detto \"{}\" · ILLUMINATI CONFIRMED: simpatia {}.",
                username,
                *said,
                cast->palle
            );
        case Spell::taunt:
            return std::format(
                "💀 {} ha detto \"{}\" · GET REKT: simpatia {}, e adesso è il bersaglio designato.",
                username,
                *said,
                cast->palle
            );
        case Spell::sixseven:
            return std::format(
                "6️⃣7️⃣ {} ha detto \"{}\": le sue palle finiscono in 67. Adesso ne ha {}.",
                username,
                *said,
                cast->score
            );
        case Spell::blessing:
            return std::format(
                "⛪ {} ha detto \"{}\" e arriva la benedizione: debiti cancellati, penalità rimessa, "
                "simpatia di nuovo a {}. Ne ha {}.",
                username,
                *said,
                cast->multiplier,
                cast->score
            );
        }
    }
    return std::nullopt;
}

}

std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text) {
    try {
        const std::string_view message = text::trim(text);
        /* Kio decides for whoever he has taken over, until somebody gets him out. */
        if (!context.username.empty() && command_is_for_bot(message) &&
            !message.starts_with("/libera") && is_reprogrammed(context.storage, std::string{context.username})) {
            return std::format("🤖 {} è riprogrammato: decide Kio per lui.", context.username);
        }
        if (message == conquister_trigger) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            return handle_claim(context, {});
        }
        if (const std::string_view target = raid_target(message); !target.empty()) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            return handle_raid(context, target, message.starts_with(raid_trigger));
        }
        if (message.empty()) {
            return std::nullopt;
        }
        const ParsedCommand command = parse_command(message);
        const CommandDefinition *definition = find_command(command.name);
        if (definition == nullptr) {
            const std::string lowered = text::to_lower_copy(message);
            const std::optional<std::string> ticket = lottery_reply(context, seconds_now());
            const auto with_ticket = [&ticket](std::optional<std::string> said) {
                if (ticket && said) {
                    return std::optional<std::string>{*ticket + "\n" + *said};
                }
                return said ? said : ticket;
            };
            if (const std::optional<std::string> played = game_reply(context, message, seconds_now())) {
                return with_ticket(played);
            }
            if (const std::optional<std::string> called = game_called(context, message, seconds_now())) {
                return with_ticket(called);
            }
            if (const std::optional<std::string> hand = hand_reply(context, lowered)) {
                return with_ticket(hand);
            }
            if (const std::optional<std::string> fight = duel_reply(context, lowered)) {
                return with_ticket(fight);
            }
            if (const std::optional<std::string> spell = spell_reply(context, lowered)) {
                return with_ticket(spell);
            }
            if (const std::optional<std::string> everything = cascade_reply(context, lowered)) {
                return with_ticket(everything);
            }
            if (const std::optional<std::string> luck = lucky_word_reply(context, lowered)) {
                return with_ticket(luck);
            }
            return with_ticket(flipper_reply(context, lowered, seconds_now()));
        }
        /* A handler with nothing to say out loud has already said it in private. */
        std::string reply = definition->handler(context, command.argument);
        if (reply.empty()) {
            return std::nullopt;
        }
        return reply;
    } catch (const std::exception &) {
        return std::string{internal_error_reply};
    }
}

}
