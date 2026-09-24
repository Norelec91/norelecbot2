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

bool valid_raid_target(std::string_view target) {
    const bool one_name = !target.empty() && target != "@" &&
                          target.find_first_of(" \t\r\n", 0) == std::string_view::npos &&
                          target.find('@', target.starts_with('@') ? 1 : 0) == std::string_view::npos;
    return one_name;
}

/* The name after "We ", including its optional Telegram @, or nothing if invalid. */
std::string_view raid_target(std::string_view message) {
    if (!message.starts_with(raid_trigger) || message == conquister_trigger) {
        return {};
    }
    const std::string_view target = message.substr(raid_trigger.size());
    return valid_raid_target(target) ? target : std::string_view{};
}

struct ParsedInvestment {
    std::string_view target;
    std::int64_t amount = 0;
};

std::optional<ParsedInvestment> investment_target(std::string_view message) {
    if (!message.starts_with(raid_trigger)) {
        return std::nullopt;
    }
    const std::string_view rest = message.substr(raid_trigger.size());
    const std::size_t separator = rest.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos || !valid_raid_target(rest.substr(0, separator))) {
        return std::nullopt;
    }
    if (const auto amount = text::parse_int64(rest.substr(separator))) {
        return ParsedInvestment{rest.substr(0, separator), *amount};
    }
    return std::nullopt;
}

struct ParsedEmoji {
    std::string_view target;
    std::string_view emoji;
    /* A slot, written after the emoji: only for his own name. */
    std::optional<std::int64_t> position;
};

/* "We name emoji [position]": one emoji hung on his own name, carried to a player, or brought back to
   @TheConquister37. */
std::optional<ParsedEmoji> emoji_target(std::string_view message) {
    if (!message.starts_with(raid_trigger)) {
        return std::nullopt;
    }
    const std::string_view rest = message.substr(raid_trigger.size());
    const std::size_t separator = rest.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos || !valid_raid_target(rest.substr(0, separator))) {
        return std::nullopt;
    }
    std::string_view emoji = text::trim(rest.substr(separator));
    std::optional<std::int64_t> position;
    if (const std::size_t space = emoji.find_last_of(" \t"); space != std::string_view::npos) {
        position = text::parse_int64(emoji.substr(space + 1));
        if (position) {
            emoji = text::trim(emoji.substr(0, space));
        }
    }
    if (text::parse_int64(emoji) || !text::emoji_count(emoji)) {
        return std::nullopt;
    }
    return ParsedEmoji{rest.substr(0, separator), emoji, position};
}

struct ParsedMove {
    std::string_view target;
    std::int64_t from = 0;
    std::int64_t to = 0;
};

/* "We name from to": two slots on his own name whose emoji change places. */
std::optional<ParsedMove> slot_move(std::string_view message) {
    if (!message.starts_with(raid_trigger)) {
        return std::nullopt;
    }
    const std::string_view rest = message.substr(raid_trigger.size());
    const std::size_t separator = rest.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos || !valid_raid_target(rest.substr(0, separator))) {
        return std::nullopt;
    }
    const std::string_view slots = text::trim(rest.substr(separator));
    const std::size_t space = slots.find_first_of(" \t");
    if (space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<std::int64_t> from = text::parse_int64(slots.substr(0, space));
    const std::optional<std::int64_t> to = text::parse_int64(slots.substr(space + 1));
    if (!from || !to) {
        return std::nullopt;
    }
    return ParsedMove{rest.substr(0, separator), *from, *to};
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

/* What is actually paid: the list price, or nothing for whoever turned the debug switch on. */
int price(const CommandContext &context, int cost) {
    return debug_on(context.storage, std::string{context.player_key}) ? 0 : cost;
}

/* The name as it is shown: the real one, plus whatever he hung beside it. */
std::string dressed(const Authors &furniture, std::string_view key, std::string_view username) {
    const auto mine = std::ranges::find_if(furniture, [key](const Authors::value_type &entry) {
        return text::equals_ignore_case(entry.first, key);
    });
    if (mine == furniture.end() || mine->second.empty()) {
        return std::string{username};
    }
    return std::format("{} ({})", username, mine->second);
}

/* A percentage as a multiplier: 150 is x1.5, 200 is x2, 75 is x0.75. */
std::string multiplier_text(std::int64_t percent) {
    std::string decimals = std::format("{:02}", percent % 100);
    while (!decimals.empty() && decimals.back() == '0') {
        decimals.pop_back();
    }
    return decimals.empty() ? std::format("x{}", percent / 100) : std::format("x{}.{}", percent / 100, decimals);
}

std::string hold_note(
    const CommandContext &context,
    const std::string &holder,
    std::int64_t lightning,
    int zodiac_percent,
    std::int64_t now
) {
    std::string note;
    if (lightning > 100) {
        note += std::format(" col ⚡ {}", multiplier_text(lightning));
    }
    if (zodiac_percent != 100) {
        const zodiac::Sign sign = zodiac::sign_of(holder, context.config.zodiac_signs);
        note += std::format(
            "{} {} {} nel giorno di {} ({})",
            note.empty() ? "" : " e",
            sign.symbol,
            sign.name,
            zodiac::element_name(zodiac::element_of_day(now)),
            multiplier_text(zodiac_percent)
        );
    }
    return note;
}

std::int64_t seconds_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

/* A count of palle, in the singular when there is just one. */
std::string palle(std::int64_t count) {
    return std::format("{} {}", count, count == 1 || count == -1 ? "palla" : "palle");
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

/* What a failed attempt cost the one who made it. */
std::string failed_attempt_toll(const ClaimResult &result) {
    if (result.penalty_seconds > 0) {
        return std::format(" e prendi {} di penalità", format_wait(result.penalty_seconds));
    }
    return {};
}

RaidRules raid_rules(const CommandContext &context) {
    return {
        .loot_divisor = context.config.loot_divisor,
        .loot_share = context.config.raid_share,
        .travel_divisor = context.config.travel_divisor,
        .signs = context.config.zodiac_signs,
        .furniture_limit = static_cast<std::size_t>(context.config.furniture_limit),
    };
}

std::string handle_raid(const CommandContext &context, std::string_view target, std::int64_t gift = 0,
                        std::string_view gift_emoji = {}) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    /* His own place is named after him, with the mention only where it reaches him. */
    const std::string home =
        std::format("{}{}", context.user_id != 0 ? "@" : "", username);
    const bool telegram_target = target.starts_with('@');
    const std::string_view name = telegram_target ? target.substr(1) : target;
    const RaidResult result = raid_start(
        context.storage,
        context.user_id,
        std::string{context.player_key},
        name,
        seconds_now(),
        raid_rules(context),
        telegram_target ? RaidTargetKind::telegram : RaidTargetKind::irc,
        gift,
        gift_emoji
    );
    switch (result.status) {
    case RaidStatus::already_travelling:
        return std::format("🚀 {} sei già in viaggio, torni tra {}.", username, format_wait(result.seconds));
    case RaidStatus::holding_place:
        /* Journeys are measured between planets, and the place is not on the map. */
        return std::format("🚀 {} razzie e consegne partono dal tuo pianeta: esci prima da {} con We {}.",
                           username, conquister_place, home);
    case RaidStatus::unknown_target:
        return std::format("🚀 {} non conosco nessun giocatore di nome {}.", username, target);
    case RaidStatus::left_place:
        return std::format(
            "🪐 {} torni da {} in {} con {}{}.",
            username,
            conquister_place,
            home,
            palle(result.earned),
            hold_note(context, username, result.lightning, result.zodiac_percent, seconds_now())
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
    case RaidStatus::insufficient_score:
        return std::format("🎁 {} hai solo {} a disposizione.", username, palle(result.score));
    case RaidStatus::invalid_amount:
        return std::format("🎁 {} indica un numero di palle maggiore di zero.", username);
    case RaidStatus::no_such_emoji:
        return std::format("🎁 {} non hai {} appesa al nome.", username, gift_emoji);
    case RaidStatus::no_room:
        return std::format("🎁 {} {} non ha posti liberi per {}.", username, target, gift_emoji);
    case RaidStatus::started:
        break;
    }
    if (!gift_emoji.empty()) {
        return std::format(
            "🚀 {} parti per {} con {} da consegnare: arrivi tra {}. Il tuo pianeta resta scoperto.",
            username,
            result.target,
            gift_emoji,
            format_wait(result.seconds)
        );
    }
    if (gift > 0) {
        return std::format(
            "🚀 {} parti per {} con {} da consegnare: arrivi tra {}. Il tuo pianeta resta scoperto.",
            username,
            result.target,
            palle(gift),
            format_wait(result.seconds)
        );
    }
    return std::format(
        "🚀 {} parti per {}: arrivi tra {}. Il tuo pianeta resta scoperto.",
        username,
        result.target,
        format_wait(result.seconds)
    );
}

/* His own name as he writes it after "We": with the mention on Telegram, bare on IRC. */
std::string own_name(const CommandContext &context) {
    return std::format("{}{}", context.user_id != 0 ? "@" : "", context.username);
}

/* The line that comes first when a line meant for home took him out of @TheConquister37. */
std::string departure_line(const CommandContext &context, const Departure &departure, std::int64_t now) {
    if (!departure.left) {
        return {};
    }
    return std::format(
        "🪐 {} torni da {} in {} con {}{}.\n",
        context.username,
        conquister_place,
        own_name(context),
        palle(departure.earned),
        hold_note(context, std::string{context.username}, departure.lightning, departure.zodiac_percent, now)
    );
}

/* On the road only the way back is open: says so, and how to take it, or when it ends for whoever is on it. */
std::string on_the_road(const CommandContext &context, std::string_view emoji, std::string_view what) {
    if (const std::optional<std::int64_t> left = returning_in(context.storage, std::string{context.player_key},
                                                               seconds_now())) {
        return std::format("{} {} sei sulla via del ritorno: {} Rientri tra {}.",
                           emoji, context.username, what, format_wait(*left));
    }
    return std::format("{} {} sei in viaggio: {} Per tornare indietro scrivi We {}.",
                       emoji, context.username, what, own_name(context));
}

/* The place itself, written with or without the mention that reaches it on Telegram. */
bool names_the_place(std::string_view target) {
    const std::string_view name = target.starts_with('@') ? target.substr(1) : target;
    return text::equals_ignore_case(name, conquister_place.substr(1));
}

/* "We @TheConquister37 numero": the palle go back where they were earned, which is out of the game. */
std::string handle_burn(const CommandContext &context, std::int64_t amount) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const BurnResult result = palle_burn(context.storage, std::string{context.player_key}, amount);
    switch (result.status) {
    case BurnStatus::invalid_amount:
        return std::format("🔥 {} indica un numero di palle maggiore di zero.", context.username);
    case BurnStatus::insufficient_score:
        return std::format("🔥 {} hai solo {} a disposizione.", context.username, palle(result.score));
    case BurnStatus::travelling:
        return on_the_road(context, "🔥", "si brucia dal tuo pianeta o da @TheConquister37.");
    case BurnStatus::burned:
        break;
    }
    return std::format(
        "🔥 {} hai riportato {} in {}: {} dal gioco. Te ne {} {}.",
        context.username,
        palle(result.amount),
        conquister_place,
        result.amount == 1 ? "è uscita" : "sono uscite",
        result.score == 1 ? "resta" : "restano",
        result.score
    );
}

/* How commands are typed where he is: a slash on Telegram, a bang on IRC, where the slash belongs to the client. */
std::string_view command_prefix(const CommandContext &context) {
    return context.user_id == 0 ? "!" : "/";
}

/* "We yourname from to": the emoji in one slot goes to another, swapping with what hangs there. */
std::string handle_furniture_move(const CommandContext &context, const ParsedMove &move) {
    const std::string username{context.username};
    const auto limit = static_cast<std::size_t>(context.config.furniture_limit);
    const std::int64_t now = seconds_now();
    const FurnitureMoveResult result = furniture_move(context.storage, std::string{context.player_key}, move.from,
                                                      move.to, limit, now, context.config.zodiac_signs);
    const std::string departure = departure_line(context, result.departure, now);
    switch (result.status) {
    case FurnitureMoveStatus::invalid_position:
        return departure + std::format("🛋️ {} i posti vanno da 1 a {}.", username, limit);
    case FurnitureMoveStatus::same_position:
        return departure + std::format("🛋️ {} il posto di partenza e quello di arrivo sono lo stesso.", username);
    case FurnitureMoveStatus::not_home:
        return on_the_road(context, "🛋️", "le emoji si spostano dal tuo pianeta.");
    case FurnitureMoveStatus::empty_slot:
        return departure + std::format("🛋️ {} nel posto {} non c'è nessuna emoji.", username, move.from);
    case FurnitureMoveStatus::swapped:
        return departure + std::format("🛋️ {} ({}) hai scambiato {} e {}: ora {} è nel posto {} e {} nel posto {}.",
                           username, result.shown, result.moved, result.swapped,
                           result.moved, move.to, result.swapped, move.from);
    case FurnitureMoveStatus::moved:
        break;
    }
    return departure + std::format("🛋️ {} ({}) hai spostato {} dal posto {} al posto {}.",
                       username, result.shown, result.moved, move.from, move.to);
}

/* "We @TheConquister37 emoji": the first copy on his name goes back to the place, out of the game. */
std::string handle_emoji_burn(const CommandContext &context, std::string_view emoji) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    switch (furniture_burn(context.storage, std::string{context.player_key}, std::string{emoji}, seconds_now()).status) {
    case FurnitureBurnStatus::travelling:
        return on_the_road(context, "🔥", "si brucia dal tuo pianeta o da @TheConquister37.");
    case FurnitureBurnStatus::not_owned:
        return std::format("🔥 {} non hai {} appesa al nome.", context.username, emoji);
    case FurnitureBurnStatus::burned:
        break;
    }
    if (emoji == "💩") {
        return std::format("{}, tiri una palla di cacca a {}, bravo hai fatto centro, l'hai completamente smerdato!",
                           own_name(context), conquister_place);
    }
    return std::format("🔥 {} hai riportato {} in {}: è uscita dal gioco.", context.username, emoji, conquister_place);
}

/* "We nome numero" toward somebody else: the palle travel with him and change hands when he arrives. */
std::string handle_gift(const CommandContext &context, const ParsedInvestment &request) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    if (request.amount <= 0) {
        return std::format("🎁 {} indica un numero di palle maggiore di zero.", context.username);
    }
    return handle_raid(context, request.target, request.amount);
}

/* A change with its sign, except nothing, which has none. */
std::string signed_amount(std::int64_t value) {
    return value == 0 ? std::string{"0"} : std::format("{:+}", value);
}

/* Nothing when the named player is somebody else: those palle are a delivery, not a deposit. */
std::optional<std::string> handle_investment(const CommandContext &context, const ParsedInvestment &request) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const bool telegram = request.target.starts_with('@');
    const std::string_view name = telegram ? request.target.substr(1) : request.target;
    const std::int64_t now = seconds_now();
    const InvestmentResult result = investment_deposit(context.storage, std::string{context.player_key}, name,
        telegram ? RaidTargetKind::telegram : RaidTargetKind::irc, request.amount, now,
        context.config.zodiac_signs, context.config.lightning_percent);
    /* From @TheConquister37 the deposit is made on the way home: first what the hold was worth. */
    const std::string departure = departure_line(context, result.departure, now);
    switch (result.status) {
    case InvestmentStatus::deposited: {
        const std::string_view horoscope = result.zodiac_percent == 125 ? "favorevole" :
            result.zodiac_percent == 75 ? "sfavorevole" : "neutro";
        return departure + std::format("🏦 {} hai investito {}. "
                                       "Rendimento di oggi: {}% (oroscopo {}). "
                                       "Saldo disponibile: {}.",
                                       context.username, palle(result.amount), signed_amount(result.daily_rate),
                                       horoscope, palle(result.score));
    }
    case InvestmentStatus::not_self:
        return std::nullopt;
    case InvestmentStatus::not_home:
        return on_the_road(context, "🏦", "si investe dal tuo pianeta.");
    case InvestmentStatus::invalid_amount:
        return std::format("🏦 {} indica un numero di palle maggiore di zero.", context.username);
    case InvestmentStatus::insufficient_score:
        return departure + std::format("🏦 {} hai solo {} a disposizione.", context.username, palle(result.score));
    default:
        return std::string{internal_error_reply};
    }
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
            std::string{context.player_key},
            now,
            ClaimRules{
                .cooldown_seconds = context.config.cooldown_seconds,
                .signs = context.config.zodiac_signs,
                .lightning = context.config.lightning_percent,
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
        return std::format("🪐 {} sei già in {}!", username, conquister_place);
    }
    const Authors furniture = furniture_all(context.storage);
    if (result.status == ClaimStatus::defended) {
        const std::string toll = failed_attempt_toll(result);
        return std::format(
            "🎈 {} il palloncino di {} ha resistito{}. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato.",
            dressed(furniture, context.player_key, username),
            dressed(furniture, result.previous_key, result.previous_username),
            toll,
            result.next_chance
        );
    }
    std::string reply;
    if (result.balloon_popped) {
        reply = std::format(
            "💥 {} hai bucato il palloncino di {}{}!\n",
            dressed(furniture, context.player_key, username),
            mention,
            dressed(furniture, result.previous_key, result.previous_username)
        );
    }
    if (!result.previous_username.empty()) {
        reply += std::format(
            "{0} hai cacciato {4}{1} da {2}.\n{1} hai guadagnato {3}{5}!\n",
            dressed(furniture, context.player_key, username),
            dressed(furniture, result.previous_key, result.previous_username),
            conquister_place,
            palle(result.earned),
            mention,
            hold_note(context, result.previous_username, result.lightning, result.zodiac_percent, now)
        );
    }
    reply += std::format("🪐 {} sei in {}!", dressed(furniture, context.player_key, username), conquister_place);
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
    const Authors furniture = furniture_all(context.storage);
    std::string reply = std::format(
        "🏆 Classifica {}\nOggi è giorno di {}.\n",
        conquister_place,
        zodiac::element_name(zodiac::element_of_day(seconds_now()))
    );
    for (std::size_t position = 1; const LeaderboardEntry &entry : leaderboard.entries) {
        reply += std::format(
            "\n{}) {} {} — {}",
            position++,
            zodiac::sign_of(entry.username, context.config.zodiac_signs).symbol,
            dressed(furniture, entry.player_key, entry.username),
            palle(entry.score)
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
        reply += std::format(
            "\n\n🪐 In {} ora: {}",
            conquister_place,
            dressed(furniture, leaderboard.current_key, leaderboard.current->username)
        );
    }
    return reply;
}

std::string handle_add_quote(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const int cost = price(context, context.config.quote_cost);
    if (argument.empty()) {
        return std::format("Uso: {}addquote <testo>. Costa {}.", command_prefix(context), palle(cost));
    }
    const std::string username{context.username};
    /* Nobody gets tagged by a quote read out months later. */
    const std::string quote = text::strip_mentions(argument);
    const auto banned = std::ranges::find_if(context.config.quote_banned, [&quote](const std::string &piece) {
        return text::contains_ignore_case(quote, piece);
    });
    if (banned != context.config.quote_banned.end()) {
        return std::format("{} questa citazione non si può aggiungere: nessun addebito.", username);
    }
    const QuoteAddResult result = quote_add(context.storage, std::string{context.player_key}, quote, cost);
    if (result.status == QuoteAddStatus::insufficient_score) {
        return std::format(
            "{} ti servono {} per aggiungere una citazione (ne hai {}).",
            username,
            palle(cost),
            result.available_score
        );
    }
    if (result.status == QuoteAddStatus::duplicate) {
        return "Citazione già presente o non salvabile: nessun addebito.";
    }
    return std::format(
        "{} hai aggiunto la citazione spendendo {}!\n\n{}",
        username,
        palle(cost),
        quote
    );
}

/* "We yourname emoji [position]" at home: one emoji in one slot, the first empty one when no slot is named. */
std::string handle_furniture(const CommandContext &context, std::string_view wanted,
                             std::optional<std::int64_t> slot) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string username{context.username};
    const int cost = price(context, context.config.furniture_cost);
    const auto limit = static_cast<std::size_t>(context.config.furniture_limit);
    if (slot && (*slot < 1 || static_cast<std::uint64_t>(*slot) > limit)) {
        return std::format("🛋️ {} i posti vanno da 1 a {}: nessun addebito.", username, limit);
    }
    const std::int64_t position = slot.value_or(0);
    const std::string emoji{wanted};
    const std::int64_t now = seconds_now();
    const FurnitureResult result = furniture_buy(context.storage, std::string{context.player_key}, emoji, position,
                                                 cost, limit, now, context.config.zodiac_signs,
                                                 context.config.furniture_inflation);
    const std::string departure = departure_line(context, result.departure, now);
    switch (result.status) {
    case FurnitureStatus::not_home:
        return on_the_road(context, "🛋️", "le emoji si appendono al nome dal tuo pianeta.");
    case FurnitureStatus::full:
        return departure + std::format(
            "🛋️ {} hai già tutti i {} posti pieni: scegli quale sostituire con We {} <emoji> <posizione>. "
            "Nessun addebito.",
            username,
            limit,
            own_name(context)
        );
    case FurnitureStatus::invalid_position:
        return departure + std::format("🛋️ {} i posti vanno da 1 a {}: nessun addebito.", username, limit);
    case FurnitureStatus::already_there:
        return departure + std::format("🛋️ {} nel posto {} c'è già {}: nessun addebito.", username, result.position, result.replaced);
    case FurnitureStatus::insufficient_score:
        if (result.copies > 0) {
            return departure + std::format("🛋️ {} ti servono {} per {} (ce ne sono già {} in giro, ne hai {}).",
                               username, palle(result.charged), emoji, result.copies, result.available_score);
        }
        return departure + std::format("🛋️ {} ti servono {} per {} (ne hai {}).",
                           username, palle(result.charged), emoji, result.available_score);
    case FurnitureStatus::bought:
        break;
    }
    std::string reply = std::format(
        "🛋️ {} ({}) hai speso {}: {} nel posto {}",
        username,
        result.shown,
        palle(result.charged),
        emoji,
        result.position
    );
    if (!result.replaced.empty()) {
        reply += std::format(", al posto di {}", result.replaced);
    }
    if (result.copies > 0) {
        reply += std::format(" (ce n'{} già {} in giro, prezzo {})",
                             result.copies == 1 ? "era" : "erano",
                             result.copies,
                             multiplier_text(cost > 0 ? result.charged * 100 / cost : 100));
    }
    return departure + reply + ".";
}

/* Every line the game understands, one example each, written the way the asker has to write it. */
std::string handle_help(const CommandContext &context, std::string_view) {
    const bool irc = context.user_id == 0;
    const std::string me = context.username.empty() ? std::string{irc ? "tuonick" : "@tuonome"}
                                                    : own_name(context);
    const std::string_view other = irc ? "giocatore" : "@giocatore";
    const std::string_view slash = command_prefix(context);
    std::string help = "📖 Come si gioca\n\n";
    const auto line = [&help](std::string_view example, std::string_view meaning) {
        help += std::format("{} — {}\n", example, meaning);
    };
    line(std::format("We {}", conquister_place), "entri nel posto: 1 palla al secondo finché lo tieni");
    line(std::format("We {}", me), "torni sul tuo pianeta, dal posto, dal viaggio o ritiri l'investimento");
    line(std::format("We {} 1000", me), "investi 1000 palle");
    line(std::format("We {} 🍕", me), "appendi 🍕 al nome nel primo posto libero, dal tuo pianeta");
    line(std::format("We {} 🍕 3", me), "appendi 🍕 nel posto 3");
    line(std::format("We {} 1 2", me), "sposti l'emoji dal posto 1 al posto 2");
    line(std::format("We {}", other), "parti per razziarlo");
    line(std::format("We {} 500", other), "gli porti 500 palle");
    line(std::format("We {} 🍕", other), "gli porti una 🍕");
    line(std::format("We {} 500", conquister_place), "bruci 500 palle");
    line(std::format("We {} 🍕", conquister_place), "bruci una 🍕");
    help += std::format("\nDa {0} le righe col tuo nome ti riportano prima sul tuo pianeta. "
                        "Razzie e consegne partono solo dal tuo pianeta: da {0} esci prima con We {1}. "
                        "In viaggio si può solo tornare indietro: We {1}.\n", conquister_place, me);
    help += std::format("\n{0}leaderboard — classifica\n{0}profile [nome] — il tuo profilo o quello di un altro\n"
                        "{0}addquote <testo> — aggiungi una citazione\n"
                        "{0}link <nome> — collega account Telegram e nick IRC Azzurra registrato",
                        slash);
    return help;
}

/* "/profile [name]": his own card, or the one of the player named as on that platform. */
std::string handle_profile(const CommandContext &context, std::string_view argument) {
    const std::string_view wanted = text::trim(argument);
    const std::int64_t now = seconds_now();
    std::optional<Profile> found;
    if (wanted.empty()) {
        if (context.username.empty()) {
            return missing_username_reply();
        }
        found = player_profile_of(context.storage, std::string{context.player_key}, now, context.config.zodiac_signs,
                                  context.config.lightning_percent);
    } else {
        const bool telegram = wanted.starts_with('@');
        found = player_profile(context.storage, telegram ? wanted.substr(1) : wanted,
                               telegram ? RaidTargetKind::telegram : RaidTargetKind::irc, now,
                               context.config.zodiac_signs, context.config.lightning_percent);
        if (!found) {
            return std::format("👤 {} non conosco nessun giocatore di nome {}.", context.username, wanted);
        }
    }
    const Profile &profile = *found;
    /* The bare name at the top; his home below is written like everywhere else. */
    std::string card = profile.furniture.empty() ? std::format("👤 {}\n", profile.name)
                                                 : std::format("👤 {} ({})\n", profile.name, profile.furniture);
    card += profile.rank == 0 ? std::string{"💰 nessuna palla ancora\n"}
                              : std::format("💰 {}, {}° su {} in classifica\n", palle(profile.score), profile.rank,
                                            profile.players);
    const zodiac::Sign sign = zodiac::sign_of(profile.name, context.config.zodiac_signs);
    const int percent = zodiac::percent_for(profile.name, now, context.config.zodiac_signs);
    card += std::format("{} {}: oggi è giorno di {}, {}\n", sign.symbol, sign.name,
                        zodiac::element_name(zodiac::element_of_day(now)),
                        multiplier_text(percent));
    switch (profile.place) {
    case ProfilePlace::home:
        card += std::format("🪐 in {}{}\n", profile.on_telegram ? "@" : "", profile.name);
        break;
    case ProfilePlace::conquister:
        card += std::format("🪐 in {} da {}{}\n", conquister_place, format_wait(std::max<std::int64_t>(now - profile.since, 0)),
                            profile.lightning_percent > 100 ? std::format(" col ⚡ {}", multiplier_text(profile.lightning_percent))
                                                            : std::string{});
        break;
    case ProfilePlace::road:
        card += std::format("🚀 {} {}: rientra tra {}\n", profile.returning ? "sulla via del ritorno da" : "in viaggio verso",
                            profile.heading, format_wait(profile.home_in));
        break;
    }
    if (profile.invested > 0) {
        card += std::format("🏦 {} investit{}, ora ne {} {}\n", palle(profile.invested),
                            profile.invested == 1 ? "a" : "e", profile.investment_value == 1 ? "vale" : "valgono",
                            profile.investment_value);
    }
    if (profile.quotes_added > 0) {
        card += std::format("📜 {} citazion{}\n", profile.quotes_added, profile.quotes_added == 1 ? "e" : "i");
    }
    card.pop_back();
    return card;
}

/* The owner is an admin with more powers, so he never has to be listed twice. */
bool trusted(const CommandContext &context) {
    return context.owner || context.admin;
}

std::string handle_quotes(const CommandContext &context, std::string_view argument) {
    if (!trusted(context)) {
        return "Solo gli amministratori possono vedere le citazioni.";
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
        reply += std::format("\n\nUsa {}quotes <pagina> per le altre pagine.", command_prefix(context));
    }
    return reply;
}

std::string handle_debug(const CommandContext &context, std::string_view argument) {
    if (!context.owner) {
        return "Solo il proprietario può accendere il debug.";
    }
    const std::string_view wanted = text::trim(argument);
    if (wanted != "0" && wanted != "1") {
        return std::format(
            "Uso: {0}debug 1 per accendere, {0}debug 0 per spegnere. Per te adesso è {1}.",
            command_prefix(context),
            debug_on(context.storage, std::string{context.player_key}) ? "acceso" : "spento"
        );
    }
    const bool on = wanted == "1";
    debug_set(context.storage, std::string{context.player_key}, on);
    return on ? "🔧 Debug acceso per te: i tuoi acquisti non costano niente. Gli altri pagano."
              : "🔧 Debug spento: i tuoi acquisti tornano a costare.";
}

std::string handle_delete_quote(const CommandContext &context, std::string_view argument) {
    if (!trusted(context)) {
        return "Solo gli amministratori possono eliminare le citazioni.";
    }
    if (argument.empty()) {
        return std::format("Uso: {0}delquote <numero da {0}quotes | testo esatto>.", command_prefix(context));
    }
    const std::optional<std::string> removed = quote_delete(context.storage, argument);
    return removed ? std::format("Citazione eliminata: {}", *removed) : "Citazione non trovata.";
}

std::string handle_link(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const bool telegram = context.user_id != 0;
    const bool valid = telegram ? !argument.empty() && !argument.starts_with('@')
                                : argument.starts_with('@') && argument.size() > 1;
    if (!valid || argument.find_first_of(" \t\r\n") != std::string_view::npos) {
        return telegram ? "Uso: /link <nick IRC>. L'utente IRC deve confermare con !link @tuoUsername."
                        : "Uso: !link @usernameTelegram. L'utente Telegram deve confermare con /link tuoNick.";
    }
    const std::string_view other = telegram ? argument : argument.substr(1);
    switch (player_link(context.storage, context.user_id, std::string{context.username}, other,
                        context.account_name)) {
    case LinkStatus::unknown_account:
        return "Non conosco ancora quell'account: deve prima usare un comando del gioco.";
    case LinkStatus::self:
        return "Questi account sono già collegati.";
    case LinkStatus::already_linked:
        return "Uno dei due account è già collegato a un altro account: nessun cambiamento.";
    case LinkStatus::pending:
        return "Richiesta registrata. L'altro account deve confermare con /link (su Telegram) o !link (su IRC).";
    case LinkStatus::conflict:
        return "Entrambi gli account hanno già beni: serve una fusione manuale, nessun bene è stato spostato.";
    case LinkStatus::linked:
        return "Account collegati: ora condividono lo stesso giocatore.";
    }
    return {};
}

constexpr std::array commands{
    CommandDefinition{"/leaderboard", handle_leaderboard},
    CommandDefinition{"/help", handle_help},
    CommandDefinition{"/profile", handle_profile},
    CommandDefinition{"/addquote", handle_add_quote},
    CommandDefinition{"/link", handle_link},
    CommandDefinition{"/quotes", handle_quotes},
    CommandDefinition{"/delquote", handle_delete_quote},
    CommandDefinition{"/debug", handle_debug},
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
    return message == conquister_trigger || !raid_target(message).empty() || investment_target(message).has_value() ||
           emoji_target(message).has_value() || slot_move(message).has_value() ||
           (!message.empty() && find_command(parse_command(message).name) != nullptr);
}

std::optional<std::string> raid_event_reply(const RaidEvent &event) {
    const std::string_view mention = event.target_on_telegram ? "@" : "";
    /* His planet, with the mention where it reaches him, as everywhere else. */
    const std::string home = std::format("{}{}", event.raider_on_telegram ? "@" : "", event.raider);
    /* The bare name for whoever is spoken to, the dressed one when somebody is named. */
    const auto with_emoji = [](std::string_view name, std::string_view emoji) {
        return emoji.empty() ? std::string{name} : std::format("{} ({})", name, emoji);
    };
    const std::string raider = with_emoji(event.raider, event.raider_emoji);
    const std::string target = with_emoji(event.target, event.target_emoji);
    if (event.kind == RaidEvent::Kind::returned) {
        if (!event.gift_emoji.empty()) {
            return std::format("🎁 {} torni in {} con {} ancora in tasca.", raider, home, event.gift_emoji);
        }
        if (event.gift > 0) {
            return std::format("🎁 {} torni in {} con {} ancora in tasca.", raider, home,
                               event.gift == 1 ? std::string{"la tua palla"} : std::format("le tue {} palle", event.gift));
        }
        if (event.loot > 0) {
            return std::format("🪐 {} torni in {} con {}.", raider, home, palle(event.loot));
        }
        /* Coming home with nothing is not news. */
        return std::nullopt;
    }
    if (event.kind == RaidEvent::Kind::delivered && !event.gift_emoji.empty()) {
        if (event.no_room) {
            return std::format("🎁 {} {}{} non ha più posto per {}: te la riporti sul tuo pianeta. Torni in {} tra {}.",
                               raider, mention, target, event.gift_emoji, home, format_wait(event.seconds));
        }
        return std::format("🎁 {} hai consegnato {} a {}{}! Torni in {} tra {}.",
                           raider, event.gift_emoji, mention, target, home, format_wait(event.seconds));
    }
    if (event.kind == RaidEvent::Kind::delivered) {
        return std::format(
            "🎁 {} hai consegnato {} a {}{}! Torni in {} tra {}.",
            raider,
            palle(event.gift),
            mention,
            target,
            home,
            format_wait(event.seconds)
        );
    }
    if (event.balloon_held) {
        return std::format(
            "🎈 {} il palloncino di {}{} ha resistito: niente bottino. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato. Torni in {} tra {}.",
            raider,
            mention,
            target,
            event.next_chance,
            home,
            format_wait(event.seconds)
        );
    }
    std::string reply = event.balloon_popped
        ? std::format("💰 {} hai bucato il palloncino di {}{} e rubato {}", raider, mention, target, palle(event.loot))
        : std::format("💰 {} hai rubato {} a {}{}", raider, palle(event.loot), mention, target);
    if (event.undefended) {
        reply += ", che non era sul suo pianeta";
    }
    reply += std::format("! Torni in {} tra {}.", home, format_wait(event.seconds));
    return reply;
}

std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text) {
    try {
        const std::string_view message = text::trim(text);
        CommandContext bound = context;
        std::string bound_key;
        const auto remember_sender = [&] {
            if (!context.username.empty()) {
                bound_key = player_seen(context.storage, context.user_id, std::string{context.username},
                                        context.account_name);
                bound.player_key = bound_key;
            }
        };
        if (message == conquister_trigger) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            remember_sender();
            return handle_claim(bound, {});
        }
        if (const auto investment = investment_target(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            remember_sender();
            if (names_the_place(investment->target)) {
                return handle_burn(bound, investment->amount);
            }
            if (std::optional<std::string> reply = handle_investment(bound, *investment)) {
                return reply;
            }
            return handle_gift(bound, *investment);
        }
        if (const auto move = slot_move(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            remember_sender();
            if (context.username.empty()) {
                return missing_username_reply();
            }
            const bool telegram = move->target.starts_with('@');
            const std::string_view name = telegram ? move->target.substr(1) : move->target;
            if (bound.player_key.empty() || !names_player(context.storage, bound_key, name,
                    telegram ? RaidTargetKind::telegram : RaidTargetKind::irc)) {
                return std::format("🛋️ {} puoi spostare solo le emoji sul tuo nome.", context.username);
            }
            return handle_furniture_move(bound, *move);
        }
        if (const auto carried = emoji_target(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            remember_sender();
            if (text::emoji_count(carried->emoji) != std::optional<std::size_t>{1}) {
                return std::format("🛋️ {} una emoji per volta.", context.username);
            }
            const bool telegram = carried->target.starts_with('@');
            const std::string_view name = telegram ? carried->target.substr(1) : carried->target;
            if (!bound.player_key.empty() && names_player(context.storage, bound_key, name,
                    telegram ? RaidTargetKind::telegram : RaidTargetKind::irc)) {
                return handle_furniture(bound, carried->emoji, carried->position);
            }
            /* The slot is his choice only on his own name: elsewhere the emoji takes the first free one. */
            if (carried->position) {
                return std::format("🛋️ {} la posizione si sceglie solo sul tuo nome: scrivi We {} {}.",
                                   context.username, carried->target, carried->emoji);
            }
            if (names_the_place(carried->target)) {
                return handle_emoji_burn(bound, carried->emoji);
            }
            return handle_raid(bound, carried->target, 0, carried->emoji);
        }
        if (const std::string_view target = raid_target(message); !target.empty()) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            remember_sender();
            if (!bound.player_key.empty()) {
                const bool telegram = target.starts_with('@');
                const InvestmentResult investment = investment_withdraw(context.storage, bound_key,
                    telegram ? target.substr(1) : target,
                    telegram ? RaidTargetKind::telegram : RaidTargetKind::irc, seconds_now(),
                    context.config.zodiac_signs, context.config.lightning_percent);
                const std::string departure = departure_line(bound, investment.departure, seconds_now());
                if (investment.status == InvestmentStatus::withdrawn) {
                    return departure + std::format("🏦 {} hai ritirato {} (rendimento: {} {}). Saldo: {}.",
                                                   context.username, palle(investment.amount),
                                                   signed_amount(investment.interest),
                                                   investment.interest == 1 || investment.interest == -1 ? "palla" : "palle",
                                                   palle(investment.score));
                }
                if (investment.status == InvestmentStatus::balance_limit) {
                    return departure + std::format("🏦 {} il saldo è troppo alto per ritirare l'investimento: contatta il proprietario del bot.",
                                                   context.username);
                }
            }
            return handle_raid(bound, target);
        }
        if (message.empty()) {
            return std::nullopt;
        }
        const ParsedCommand command = parse_command(message);
        const CommandDefinition *definition = find_command(command.name);
        if (definition == nullptr) {
            return std::nullopt;
        }
        remember_sender();
        return definition->handler(bound, command.argument);
    } catch (const std::exception &) {
        return std::string{internal_error_reply};
    }
}

}
