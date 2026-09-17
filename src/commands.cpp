#include "commands.hpp"

#include "game.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <array>
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

/* The name in "We @someone", or nothing when the message is not a raid. */
std::string_view raid_target(std::string_view message) {
    if (!message.starts_with(raid_trigger) || message == conquister_trigger) {
        return {};
    }
    const std::string_view target = message.substr(raid_trigger.size());
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

RaidRules raid_rules(const CommandContext &context) {
    return {
        .travel_divisor = context.config.travel_divisor,
        .loot_share = context.config.raid_share,
        .attack_cost = context.config.attack_cost,
        .signs = context.config.zodiac_signs,
    };
}

std::string handle_raid(const CommandContext &context, std::string_view target) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    /* His own place is named after him, with the mention only where it reaches him. */
    const std::string home =
        std::format("{}{}", context.user_id != 0 ? "@" : "", username);
    const RaidResult result =
        raid_start(context.storage, context.user_id, username, target, seconds_now(), raid_rules(context));
    switch (result.status) {
    case RaidStatus::already_travelling:
        return std::format("🚀 {} sei già in viaggio, torni tra {}.", username, format_wait(result.seconds));
    case RaidStatus::holding_place:
        return std::format("🚀 {} sei in {} e da lì non si parte.", username, conquister_place);
    case RaidStatus::unknown_target:
        return std::format("🚀 {} non conosco nessun giocatore di nome {}.", username, target);
    case RaidStatus::left_place:
        return std::format(
            "🏠 {} lasci {} e torni in {}: hai guadagnato {} palle{}.",
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
        return std::format("🏠 {} sei già in {}.", username, home);
    case RaidStatus::started:
        break;
    }
    return std::format(
        "🚀 {} parti per {}: arrivi tra {}. {} resta scoperto.",
        username,
        result.target,
        format_wait(result.seconds),
        home
    );
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
                .cooldown_seconds = context.config.cooldown_seconds,
                .attack_cost = context.config.attack_cost,
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
                "🎈 {} il palloncino di {}{} ha resistito{}. Resiste ancora per {}.",
                username,
                mention,
                result.previous_username,
                toll,
                format_wait(result.shield_seconds)
            );
        }
        return std::format(
            "🎈 {} il palloncino di {}{} ha resistito{}. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato.",
            username,
            mention,
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

std::string handle_add_quote(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const int cost = context.config.quote_cost;
    if (argument.empty()) {
        return std::format("Uso: /addquote <testo>. Costa {} palle.", cost);
    }
    const std::string username{context.username};
    /* Nobody gets tagged by a quote read out months later. */
    const std::string quote = text::strip_mentions(argument);
    const QuoteAddResult result = quote_add(context.storage, username, quote, cost);
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
    const int cost = context.config.balloon_cost;
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
    for (std::size_t number = quotes.first_number; const std::string &quote : quotes.items) {
        const bool truncated = text::utf8_prefix_bytes(quote, 80) < quote.size();
        const std::string_view shown = truncated
            ? std::string_view{quote}.substr(0, text::utf8_prefix_bytes(quote, 77))
            : std::string_view{quote};
        reply += std::format("\n{}) {}{}", number++, shown, truncated ? "…" : "");
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
    const int cost = context.config.boost_cost;
    const BoostResult result =
        boost_buy(context.storage, username, cost, context.config.boost_multiplier, seconds_now());
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

std::string raid_event_reply(const RaidEvent &event, const zodiac::Overrides &signs) {
    const std::string_view mention = event.target_on_telegram ? "@" : "";
    const std::string home = std::format("{}{}", event.raider_on_telegram ? "@" : "", event.raider);
    if (event.kind == RaidEvent::Kind::returned) {
        if (event.loot > 0) {
            return std::format("🏠 {} sei tornato in {} con {} palle.", event.raider, home, event.loot);
        }
        return std::format("🏠 {} sei tornato in {} a mani vuote.", event.raider, home);
    }
    if (event.kind == RaidEvent::Kind::defended) {
        return std::format(
            "🎈 {} il palloncino di {}{} ha resistito{}. Torni in {} a mani vuote tra {}.",
            event.raider,
            mention,
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
    reply += std::format("! Torni in {} tra {}.", home, format_wait(event.seconds));
    return reply;
}

std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text) {
    try {
        const std::string_view message = text::trim(text);
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
            return handle_raid(context, target);
        }
        if (message.empty()) {
            return std::nullopt;
        }
        const ParsedCommand command = parse_command(message);
        const CommandDefinition *definition = find_command(command.name);
        if (definition == nullptr) {
            return std::nullopt;
        }
        return definition->handler(context, command.argument);
    } catch (const std::exception &) {
        return std::string{internal_error_reply};
    }
}

}
