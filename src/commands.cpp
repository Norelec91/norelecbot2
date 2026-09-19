#include "commands.hpp"

#include "game.hpp"
#include "mishaps.hpp"
#include "position.hpp"
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
