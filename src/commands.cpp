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
#include <vector>

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

struct ParsedAmount {
    std::string_view target;
    std::int64_t amount = 0;
};

std::optional<ParsedAmount> amount_target(std::string_view message) {
    if (!message.starts_with(raid_trigger)) {
        return std::nullopt;
    }
    const std::string_view rest = message.substr(raid_trigger.size());
    const std::size_t separator = rest.find_first_of(" \t\r\n");
    if (separator == std::string_view::npos || !valid_raid_target(rest.substr(0, separator))) {
        return std::nullopt;
    }
    if (const auto amount = text::parse_int64(rest.substr(separator))) {
        return ParsedAmount{rest.substr(0, separator), *amount};
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

/* "/avventura" is another way to write "We @TheConquister37", with whatever follows it. */
constexpr std::string_view adventure_command = "/avventura";

std::string expand_adventure(std::string_view message) {
    const ParsedCommand command = parse_command(message);
    if (command.name != adventure_command) {
        return std::string{message};
    }
    return command.argument.empty() ? std::string{conquister_trigger}
                                    : std::format("{} {}", conquister_trigger, command.argument);
}

/* What is actually paid: the list price, or nothing for whoever turned the debug switch on. */
int price(const CommandContext &context, int cost) {
    return debug_on(context.storage, std::string{context.player_key}) ? 0 : cost;
}

/* A name with its title, if a 💩 hit him lately, and in front of it what he has on him. */
std::string with_furniture(std::string_view name, std::string_view furniture, bool smeared) {
    const std::string titled = smeared ? std::format("{} lo smerdato", name) : std::string{name};
    return furniture.empty() ? titled : std::format("{} {}", furniture, titled);
}

/* Everything that changes how the names read: the emoji beside them, and who was hit by a 💩. */
struct Looks {
    Authors furniture;
    std::vector<std::string> smeared;
};

/* The name as it is shown: the real one, plus whatever he hung beside it. */
std::string dressed(const Looks &looks, std::string_view key, std::string_view username) {
    const auto mine = std::ranges::find_if(looks.furniture, [key](const Authors::value_type &entry) {
        return text::equals_ignore_case(entry.first, key);
    });
    const bool smeared = std::ranges::any_of(looks.smeared, [key](const std::string &player) {
        return text::equals_ignore_case(player, key);
    });
    return with_furniture(username,
                          mine == looks.furniture.end() ? std::string_view{} : std::string_view{mine->second}, smeared);
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

Looks looks_of(Storage &storage) {
    return Looks{.furniture = furniture_all(storage), .smeared = smeared_all(storage, seconds_now())};
}

/* A count of palle, in the singular when there is just one. */
std::string palle(std::int64_t count) {
    return std::format("{} {}", count, count == 1 || count == -1 ? "palla" : "palle");
}

std::string format_wait(std::int64_t seconds) {
    /* Exact, largest unit first, the empty ones left out: "1 ora e 5 secondi", "23 ore, 59 minuti e 30 secondi". */
    const std::int64_t days = seconds / 86400;
    const std::int64_t hours = seconds % 86400 / 3600;
    const std::int64_t minutes = seconds % 3600 / 60;
    const std::int64_t rest = seconds % 60;
    std::vector<std::string> parts;
    if (days > 0) {
        parts.push_back(std::format("{} giorn{}", days, days == 1 ? "o" : "i"));
    }
    if (hours > 0) {
        parts.push_back(std::format("{} or{}", hours, hours == 1 ? "a" : "e"));
    }
    if (minutes > 0) {
        parts.push_back(std::format("{} minut{}", minutes, minutes == 1 ? "o" : "i"));
    }
    if (rest > 0 || parts.empty()) {
        parts.push_back(std::format("{} second{}", rest, rest == 1 ? "o" : "i"));
    }
    std::string text = parts.front();
    for (std::size_t index = 1; index < parts.size(); ++index) {
        text += (index + 1 == parts.size() ? " e " : ", ") + parts[index];
    }
    return text;
}

/* What whoever a 🧊 froze is told when he tries for the place or to set off. */
std::string frozen_reply(std::string_view username, std::int64_t seconds) {
    return std::format("{} una 🧊 ti ha congelato: non puoi entrare in {} né partire per altri {}.", username,
                       conquister_place, format_wait(seconds));
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
        .travel_divisor = context.config.travel_divisor,
        .signs = context.config.zodiac_signs,
        .furniture_limit = static_cast<std::size_t>(context.config.furniture_limit),
        .smeared_seconds = context.config.smeared_seconds,
        .rocket_percent = context.config.rocket_percent,
        .pleading_percent = context.config.pleading_percent,
        .bomb_dud_percent = context.config.bomb_dud_percent,
        .dog_percent = context.config.dog_percent,
        .mailbox_percent = context.config.mailbox_percent,
        .ninja_percent = context.config.ninja_percent,
        .alarm_percent = context.config.alarm_percent,
        .pirate_percent = context.config.pirate_percent,
        .pregnancy_seconds = context.config.pregnancy_seconds,
        .child_stage_seconds = context.config.child_stage_seconds,
        .hen_per_minute = context.config.hen_per_minute,
        .adult_per_second = context.config.adult_per_second,
        .mating_percent = context.config.mating_percent,
        .dino_percent = context.config.dino_percent,
        .salt_percent = context.config.salt_percent,
        .frozen_seconds = context.config.frozen_seconds,
        .fire_percent = context.config.fire_percent,
    };
}

std::string handle_raid(const CommandContext &context, std::string_view target, std::int64_t gift = 0,
                        std::string_view gift_emoji = {}, bool intact = false) {
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
        gift_emoji,
        intact
    );
    switch (result.status) {
    case RaidStatus::already_travelling:
        return std::format("{} sei già in viaggio, torni tra {}.", username, format_wait(result.seconds));
    case RaidStatus::holding_place:
        /* Journeys are measured between planets, and the place is not on the map. */
        return std::format("{} razzie e consegne partono da casa tua: esci prima da {} con We {}.",
                           username, conquister_place, home);
    case RaidStatus::unknown_target:
        return std::format("{} non conosco nessun giocatore di nome {}.", username, target);
    case RaidStatus::left_place:
        return std::format(
            "{} torni da {} in {} con {}{}.",
            username,
            conquister_place,
            home,
            palle(result.earned),
            hold_note(context, username, result.lightning, result.zodiac_percent, seconds_now())
        );
    case RaidStatus::coming_home:
        return std::format(
            "{} lasci perdere e torni in {}: arrivi tra {}.",
            username,
            home,
            format_wait(result.seconds)
        );
    case RaidStatus::home_already:
        return std::format("{} sei già in {}!", username, home);
    case RaidStatus::insufficient_score:
        return std::format("{} hai solo {} a disposizione.", username, palle(result.score));
    case RaidStatus::invalid_amount:
        return std::format("{} indica un numero di palle maggiore di zero.", username);
    case RaidStatus::no_such_emoji:
        return std::format("{} non hai {}, né con te né in casa.", username, gift_emoji);
    case RaidStatus::no_room:
        return std::format("{} {} non ha posti liberi per {}.", username, target, gift_emoji);
    case RaidStatus::hands_full:
        return std::format("{} hai già {} emoji con te: per portare {} serve un posto libero. Rimetti qualcosa in casa "
                           "con {}store <emoji>.", username, carried_limit, gift_emoji,
                           context.user_id == 0 ? "!" : "/");
    case RaidStatus::frozen:
        return frozen_reply(username, result.seconds);
    case RaidStatus::started:
        break;
    }
    if (!gift_emoji.empty()) {
        return std::format(
            "{} parti per {} con {} da {}: arrivi tra {}. Casa tua resta scoperta.",
            username,
            result.target,
            gift_emoji,
            intact ? "regalare" : "consegnare",
            format_wait(result.seconds)
        );
    }
    if (gift > 0) {
        return std::format(
            "{} parti per {} con {} da consegnare: arrivi tra {}. Casa tua resta scoperta.",
            username,
            result.target,
            palle(gift),
            format_wait(result.seconds)
        );
    }
    return std::format(
        "{} parti per {}: arrivi tra {}. Casa tua resta scoperta.",
        username,
        result.target,
        format_wait(result.seconds)
    );
}

/* His own name as he writes it after "We": with the mention on Telegram, bare on IRC. */
std::string own_name(const CommandContext &context) {
    return std::format("{}{}", context.user_id != 0 ? "@" : "", context.username);
}

/* "/buy emoji [position]" is another way to write "We yourname emoji [position]". Bare, it is left to
   its command, which says how to use it. */
constexpr std::string_view buy_command = "/buy";

/* "/back" is another way to write "We yourname", and "/move from to" of "We yourname from to": only
   two slots, so that nothing else after it is ever taken for a purchase. */
constexpr std::string_view back_command = "/back";
constexpr std::string_view move_command = "/move";

bool two_slots(std::string_view argument) {
    const std::size_t space = argument.find_first_of(" \t");
    return space != std::string_view::npos && text::parse_int64(argument.substr(0, space)) &&
        text::parse_int64(text::trim(argument.substr(space + 1)));
}

std::string expand_buy(const CommandContext &context, std::string message) {
    const ParsedCommand command = parse_command(message);
    if (context.username.empty()) {
        return message;
    }
    if (command.name == back_command) {
        return std::format("{}{}", raid_trigger, own_name(context));
    }
    if (command.name == move_command && two_slots(command.argument)) {
        return std::format("{}{} {}", raid_trigger, own_name(context), command.argument);
    }
    if (command.name != buy_command || command.argument.empty()) {
        return message;
    }
    return std::format("{}{} {}", raid_trigger, own_name(context), command.argument);
}

/* The three things a ride to somebody can be, each with its command: "/raid name" robs him, "/give
   name emoji|palle" makes him a present, "/throw name emoji" lands it on him. Each is a "We name ..."
   line underneath, which the verb then holds to its own meaning. */
enum class Verb { none, raid, give, throw_, burn };

struct Verbed {
    Verb verb = Verb::none;
    std::string message;
};

/* Bare, a verb is left to its command, which says how to use it. */
Verbed expand_verb(std::string message) {
    const ParsedCommand command = parse_command(message);
    const Verb verb = command.name == "/raid" ? Verb::raid
        : command.name == "/give"             ? Verb::give
        : command.name == "/throw"            ? Verb::throw_
        : command.name == "/burn"             ? Verb::burn
                                              : Verb::none;
    if (verb == Verb::none || command.argument.empty()) {
        return {Verb::none, std::move(message)};
    }
    /* What is burnt goes back where it came from, which is @TheConquister37. */
    if (verb == Verb::burn) {
        return {verb, std::format("{} {}", conquister_trigger, command.argument)};
    }
    return {verb, std::format("{}{}", raid_trigger, command.argument)};
}

/* The line that comes first when a line meant for home took him out of @TheConquister37. */
std::string departure_line(const CommandContext &context, const Departure &departure, std::int64_t now) {
    if (!departure.left) {
        return {};
    }
    return std::format(
        "{} torni da {} in {} con {}{}.\n",
        context.username,
        conquister_place,
        own_name(context),
        palle(departure.earned),
        hold_note(context, std::string{context.username}, departure.lightning, departure.zodiac_percent, now)
    );
}

/* On the road only the way back is open: says so, and how to take it, or when it ends for whoever is on it. */
std::string on_the_road(const CommandContext &context, std::string_view what) {
    if (const std::optional<std::int64_t> left = returning_in(context.storage, std::string{context.player_key},
                                                               seconds_now())) {
        return std::format("{} sei sulla via del ritorno: {} Rientri tra {}.", context.username, what,
                           format_wait(*left));
    }
    return std::format("{} sei in viaggio: {} Per tornare indietro scrivi {}back.", context.username, what,
                       context.user_id == 0 ? "!" : "/");
}

/* A pile of poo is not handed over or burnt: it is thrown, at the place or at a player. */
/* A 💣 that was a dud: what it took from the thrower himself, or that he had nothing on him. */
std::string dud_reply(std::string_view thrower, std::string_view blown, std::string_view after = {}) {
    return blown.empty()
        ? std::format("{} la bomba era difettosa: ti esplode in mano, ma non avevi niente con te da perdere.{}",
                      thrower, after)
        : std::format("{} la bomba era difettosa: ti esplode in mano e si porta via {}!{}", thrower, blown, after);
}

std::string poo_throw(std::string_view thrower, std::string_view target) {
    return std::format("{}, tiri una palla di cacca a {}, bravo hai fatto centro, l'hai completamente smerdato!",
                       thrower, target);
}

/* The place itself, written with or without the mention that reaches it on Telegram. */
bool names_the_place(std::string_view target) {
    const std::string_view name = target.starts_with('@') ? target.substr(1) : target;
    return text::equals_ignore_case(name, conquister_place.substr(1));
}

/* "/burn numero": the palle leave the game. */
std::string handle_burn(const CommandContext &context, std::int64_t amount) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const BurnResult result = palle_burn(context.storage, std::string{context.player_key}, amount);
    switch (result.status) {
    case BurnStatus::invalid_amount:
        return std::format("{} indica un numero di palle maggiore di zero.", context.username);
    case BurnStatus::insufficient_score:
        return std::format("{} hai solo {} a disposizione.", context.username, palle(result.score));
    case BurnStatus::travelling:
        return on_the_road(context, "si brucia da casa tua o da @TheConquister37.");
    case BurnStatus::burned:
        break;
    }
    return std::format(
        "{} hai bruciato {}: {} dal gioco. Te ne {} {}.",
        context.username,
        palle(result.amount),
        result.amount == 1 ? "è uscita" : "sono uscite",
        result.score == 1 ? "resta" : "restano",
        result.score
    );
}

/* How commands are typed where he is: a slash on Telegram, a bang on IRC, where the slash belongs to the client. */
std::string_view command_prefix(const CommandContext &context) {
    return context.user_id == 0 ? "!" : "/";
}

/* A share of a chance, said as it is or as the most it can be. */
std::string capped(std::int64_t percent) {
    return percent >= 100 ? std::string{"100%, il massimo"} : std::format("{}%", percent);
}

/* The sentence as it starts a line. */
std::string capitalized(std::string sentence) {
    if (!sentence.empty()) {
        sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
    }
    return sentence;
}

/* What the copies of an emoji do for him once one of them has moved, on him or in the house: the bonus
   of all those that are where they work, added up. */
std::string bonus_note(const AppConfig &config, std::string_view slash, std::string_view emoji,
                       std::string_view gear, std::string_view house, bool with_him) {
    const Power *power = power_of(emoji);
    if (power == nullptr) {
        return "non dà nessun bonus";
    }
    const auto is = [power](const Power &other) { return is_power(power->emoji, other); };
    const auto count = [power](std::string_view stored) {
        return static_cast<std::int64_t>(std::ranges::count_if(furniture_slots(stored), [power](const std::string &slot) {
            return !slot.empty() && is_power(slot, *power);
        }));
    };
    if (is(power::balloon)) {
        return with_him ? std::format("con te difende te dove sei: casa tua quando ci sei, {} quando lo tieni",
                                      conquister_place)
                        : std::string{"in casa difende la casa anche quando sei fuori"};
    }
    if (power->kind == PowerKind::thrown) {
        return with_him ? std::format("non dà nessun bonus: quando la lanci con {}throw parte da qui", slash)
                        : std::string{"non dà nessun bonus: aspetta in casa finché non la lanci"};
    }
    if (power->kind == PowerKind::home) {
        const std::int64_t kept = count(house);
        std::string effect;
        if (is(power::pleading)) {
            effect = std::format("chi ti razzia ruba il {} in meno", capped(kept * config.pleading_percent));
        } else if (is(power::dog)) {
            effect = std::format("{} di fermare una razzia", capped(kept * config.dog_percent));
        } else if (is(power::mailbox)) {
            effect = std::format("{} di rispedire al mittente quello che ti lanciano a casa",
                                 capped(kept * config.mailbox_percent));
        } else if (is(power::alarm)) {
            effect = std::format("tolgono {} punti alla probabilità che un 🥷 passi di nascosto",
                                 kept * config.alarm_percent);
        } else if (is(power::hen)) {
            effect = std::format("fanno {} all'ora", palle(kept * config.hen_per_minute * 60));
        } else if (is(power::dino)) {
            effect = std::format("{} di mangiare a chi ti razzia un'emoji che ha con sé",
                                 capped(kept * config.dino_percent));
        } else if (is(power::salt)) {
            effect = std::format("chi torna a razziarti entro {} ti dà il {} delle sue palle",
                                 format_wait(salt_seconds), capped(kept * config.salt_percent));
        }
        if (with_him) {
            return kept > 0 ? std::format("con te non fa niente, funziona solo in casa, dove ne hai ancora {}: {}", kept,
                                          effect)
                            : std::string{"con te non fa niente: funziona solo in casa"};
        }
        return std::format("ora ne hai {} in casa: {}", kept, effect);
    }
    const std::int64_t on = count(gear);
    if (on == 0) {
        return "non ne hai più con te, nessun bonus";
    }
    std::string effect;
    if (is(power::bolt)) {
        effect = std::format("+{}% di palle in {}", on * config.lightning_percent, conquister_place);
    } else if (is(power::rocket)) {
        effect = std::format("viaggi il {}% più veloce", on * config.rocket_percent);
    } else if (is(power::ninja)) {
        effect = std::format("{} di passare oltre 🎈 e 🐶 senza toccarli", capped(on * config.ninja_percent));
    } else if (is(power::pirate)) {
        effect = std::format("{} di rubare un'emoji da casa di chi razzi", capped(on * config.pirate_percent));
    } else if (is(power::fire)) {
        effect = std::format("il gelo di una 🧊 dura il {} in meno", capped(on * config.fire_percent));
    } else if (is(power::hourglass)) {
        effect = std::format("la penalità dopo un tentativo fallito in {} è il {} più corta", conquister_place,
                             capped(on * config.hourglass_percent));
    } else if (is(power::lobster)) {
        std::vector<std::string> places;
        const std::vector<std::string> slots = furniture_slots(gear);
        for (std::size_t slot = 0; slot < slots.size(); ++slot) {
            if (!slots[slot].empty() && is_power(slots[slot], power::lobster)) {
                places.push_back(std::to_string(slot + 1));
            }
        }
        std::string listed = places.front();
        for (std::size_t index = 1; index < places.size(); ++index) {
            listed += (index + 1 == places.size() ? " e " : ", ") + places[index];
        }
        effect = std::format("in {} {} {} {} di chi cacci", conquister_place, places.size() == 1 ? "copia" : "copiano",
                             places.size() == 1 ? "il posto" : "i posti", listed);
    }
    return std::format("ora ne hai {} con te: {}", on, effect);
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
        return departure + std::format("{} i posti della casa vanno da 1 a {}.", username, limit);
    case FurnitureMoveStatus::same_position:
        return departure + std::format("{} il posto di partenza e quello di arrivo sono lo stesso.", username);
    case FurnitureMoveStatus::not_home:
        return on_the_road(context, "le emoji si spostano da casa tua.");
    case FurnitureMoveStatus::empty_slot:
        return departure + std::format("{} nel posto {} della casa non c'è nessuna emoji.", username, move.from);
    case FurnitureMoveStatus::swapped:
        return departure + std::format("{} hai scambiato {} e {}: ora {} è nel posto {} e {} nel posto {}. Casa: {}",
                           username, result.moved, result.swapped,
                           result.moved, move.to, result.swapped, move.from, result.shown);
    case FurnitureMoveStatus::moved:
        break;
    }
    return departure + std::format("{} hai spostato {} dal posto {} al posto {}. Casa: {}",
                       username, result.moved, move.from, move.to, result.shown);
}

/* "We @TheConquister37 emoji", or "/throw @TheConquister37 emoji", with one that is thrown: it lands on whoever
   holds the place. "/burn emoji" destroys the first copy he has there and then: even one meant to be thrown
   hits nobody. */
std::string handle_emoji_burn(const CommandContext &context, std::string_view emoji, bool destroy = false) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const FurnitureBurnResult burnt = furniture_burn(context.storage, std::string{context.player_key}, std::string{emoji},
                                                     seconds_now(), raid_rules(context), destroy);
    switch (burnt.status) {
    case FurnitureBurnStatus::travelling:
        return on_the_road(context, "si brucia da casa tua o da @TheConquister37.");
    case FurnitureBurnStatus::not_owned:
        return std::format("{} non hai {}, né con te né in casa.", context.username, emoji);
    case FurnitureBurnStatus::child:
        return std::format("{} i bambini non si bruciano: {} resta in casa finché non se ne va da solo.",
                           context.username, emoji);
    case FurnitureBurnStatus::burned:
        break;
    }
    if (destroy) {
        return std::format("{} hai bruciato {}: è uscita dal gioco.",
                           with_furniture(context.username, burnt.shown, false), emoji);
    }
    if (burnt.reset) {
        return std::format("{0} ha sganciato la bomba nucleare su {1}: il gioco riparte da zero. Tutti senza palle e "
                           "con il solo 🎈 di partenza, {1} è vuoto e nessuno è in viaggio.", context.username, conquister_place);
    }
    if (is_power(emoji, power::poo)) {
        /* Whoever holds the place takes it full in the face. */
        return poo_throw(own_name(context), burnt.hit.empty() ? std::string{conquister_place}
            : std::format("{}{} in {}", burnt.hit_on_telegram ? "@" : "", burnt.hit, conquister_place));
    }
    if (is_power(emoji, power::ice) && !burnt.hit.empty()) {
        if (burnt.froze == 0) {
            return std::format("{} il 🔥 di {}{} scioglie subito la tua 🧊 in {}.", context.username,
                               burnt.hit_on_telegram ? "@" : "", burnt.hit, conquister_place);
        }
        return std::format("{} congeli {}{} in {}: per {} non può partire e, se lo cacciano, non può rientrare.",
                           context.username, burnt.hit_on_telegram ? "@" : "", burnt.hit, conquister_place,
                           format_wait(burnt.froze));
    }
    if (burnt.expecting > 0) {
        return std::format("{} la tua 💦 è arrivata addosso a {}{} in {}: tra {} si vedrà.", context.username,
                           burnt.hit_on_telegram ? "@" : "", burnt.hit, conquister_place,
                           format_wait(burnt.expecting));
    }
    if (is_power(emoji, power::bomb) && !burnt.hit.empty()) {
        std::string blown;
        for (const std::string &taken : burnt.blown) {
            blown += taken;
        }
        if (burnt.backfired) {
            return dud_reply(context.username, blown);
        }
        const std::string holder = with_furniture(std::format("{}{}", burnt.hit_on_telegram ? "@" : "", burnt.hit),
                                                  burnt.hit_furniture, false);
        return blown.empty()
            ? std::format("{} la tua bomba esplode addosso a {} in {} ma non trova niente da portarsi via.",
                          context.username, holder, conquister_place)
            : std::format("{} la tua bomba esplode addosso a {} in {} e si porta via {}!", context.username, holder,
                          conquister_place, blown);
    }
    /* Nobody there to hit, or only himself. */
    return std::format("{} lanci {} in {}, ma non colpisci nessuno: è uscita dal gioco.", context.username, emoji,
                       conquister_place);
}

/* "We nome numero" toward somebody else: the palle travel with him and change hands when he arrives. */
std::string handle_gift(const CommandContext &context, const ParsedAmount &request) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    if (request.amount <= 0) {
        return std::format("{} indica un numero di palle maggiore di zero.", context.username);
    }
    return handle_raid(context, request.target, request.amount);
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
                .ninja = context.config.ninja_percent,
                .hourglass = context.config.hourglass_percent,
            }
        );
    /* A name that came from IRC must not be written as a mention: on Telegram it would tag a stranger. */
    const std::string_view mention = result.previous_user_id != 0 ? "@" : "";
    if (result.status == ClaimStatus::frozen) {
        return frozen_reply(username, result.penalty_seconds);
    }
    if (result.status == ClaimStatus::travelling) {
        return std::format(
            "{} sei per strada: non puoi entrare in {} prima di tornare in {}{}, tra {}.",
            username,
            conquister_place,
            context.user_id != 0 ? "@" : "",
            username,
            format_wait(result.travel_seconds)
        );
    }
    if (result.status == ClaimStatus::cooldown) {
        return std::format("{} hai ancora {} di penalità.", username, format_wait(result.penalty_seconds));
    }
    if (result.status == ClaimStatus::already_held) {
        return std::format("{} sei già in {}!", username, conquister_place);
    }
    const Looks furniture = looks_of(context.storage);
    if (result.status == ClaimStatus::defended) {
        const std::string toll = failed_attempt_toll(result);
        return std::format(
            "{} il palloncino di {} ha resistito{}. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato.",
            dressed(furniture, context.player_key, username),
            dressed(furniture, result.previous_key, result.previous_username),
            toll,
            result.next_chance
        );
    }
    std::string reply;
    /* The kicked holder named, with the mention on the name, after what he has on him. */
    const std::string kicked =
        dressed(furniture, result.previous_key, std::format("{}{}", mention, result.previous_username));
    if (result.sneaked) {
        reply = std::format("{} scivoli di nascosto oltre il palloncino di {}!\n",
                            dressed(furniture, context.player_key, username), kicked);
    }
    if (result.balloon_popped) {
        reply = std::format("{} hai bucato il palloncino di {}!\n", dressed(furniture, context.player_key, username),
                            kicked);
    }
    if (!result.previous_username.empty()) {
        reply += std::format(
            "{0} hai cacciato {4} da {2}.\n{1} hai guadagnato {3}{5}!\n",
            dressed(furniture, context.player_key, username),
            dressed(furniture, result.previous_key, result.previous_username),
            conquister_place,
            palle(result.earned),
            kicked,
            hold_note(context, result.previous_username, result.lightning, result.zodiac_percent, now)
        );
    }
    reply += std::format("{} sei in {}!", dressed(furniture, context.player_key, username), conquister_place);
    if (!result.lobsters_became.empty()) {
        std::string became;
        for (const std::string &emoji : result.lobsters_became) {
            became += emoji;
        }
        reply += std::format("\nLe tue aragoste diventano {} finché resti qui.", became);
    }
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
    const Looks furniture = looks_of(context.storage);
    std::string reply = std::format(
        "Classifica {}\nOggi è giorno di {}.\n",
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
                " — {} {}",
                entry.quotes_added,
                entry.quotes_added == 1 ? "citazione" : "citazioni"
            );
        }
    }
    if (leaderboard.current) {
        reply += std::format(
            "\n\nIn {} ora: {}",
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
    /* The ☢️ and the 🎈 have prices of their own, which do not grow with the copies in the game. */
    const std::optional<int> fixed = is_power(wanted, power::nuke) ? std::optional{context.config.nuke_cost}
        : is_power(wanted, power::balloon) ? std::optional{context.config.balloon_cost} : std::nullopt;
    const int cost = price(context, fixed.value_or(context.config.furniture_cost));
    const int inflation = fixed ? 0 : context.config.furniture_inflation;
    const auto limit = static_cast<std::size_t>(context.config.furniture_limit);
    if (slot && (*slot < 1 || static_cast<std::uint64_t>(*slot) > limit)) {
        return std::format("{} i posti della casa vanno da 1 a {}: nessun addebito.", username, limit);
    }
    const std::int64_t position = slot.value_or(0);
    const std::string emoji{wanted};
    const std::int64_t now = seconds_now();
    const FurnitureResult result = furniture_buy(context.storage, std::string{context.player_key}, emoji, position,
                                                 cost, limit, now, context.config.zodiac_signs, inflation);
    const std::string departure = departure_line(context, result.departure, now);
    switch (result.status) {
    case FurnitureStatus::not_home:
        return on_the_road(context, "le emoji si comprano da casa tua.");
    case FurnitureStatus::full:
        return departure + std::format(
            "{} hai già tutti i {} posti della casa pieni: scegli quale sostituire con {}buy <emoji> <posto>. "
            "Nessun addebito.",
            username,
            limit,
            command_prefix(context)
        );
    case FurnitureStatus::invalid_position:
        return departure + std::format("{} i posti della casa vanno da 1 a {}: nessun addebito.", username, limit);
    case FurnitureStatus::already_there:
        return departure + std::format("{} nel posto {} c'è già {}: nessun addebito.", username, result.position, result.replaced);
    case FurnitureStatus::child_there:
        return departure + std::format("{} nel posto {} c'è {}, che non si tocca: se ne andrà da solo. Nessun addebito.",
                                       username, result.position, result.replaced);
    case FurnitureStatus::insufficient_score:
        if (result.copies > 0) {
            return departure + std::format("{} ti servono {} per {} (ce ne sono già {} in giro, ne hai {}).",
                               username, palle(result.charged), emoji, result.copies, result.available_score);
        }
        return departure + std::format("{} ti servono {} per {} (ne hai {}).",
                           username, palle(result.charged), emoji, result.available_score);
    case FurnitureStatus::bought:
        break;
    }
    std::string reply = std::format("{} hai speso {}: {} ", with_furniture(username, result.shown, false),
                                    palle(result.charged), emoji);
    reply += result.with_him ? std::string{"ora è con te"} : std::format("in casa nel posto {}", result.position);
    if (!result.replaced.empty()) {
        reply += std::format(", al posto di {}", result.replaced);
    }
    if (result.copies > 0) {
        reply += std::format(" (ce n'{} già {} in giro, prezzo {})",
                             result.copies == 1 ? "era" : "erano",
                             result.copies,
                             multiplier_text(cost > 0 ? result.charged * 100 / cost : 100));
    }
    reply += ".";
    const Power *power = power_of(emoji);
    if (power == nullptr) {
        return departure + reply;
    }
    /* What works on him and ended up in the house does nothing there: how to take it along. */
    if (!result.with_him && power->kind == PowerKind::carried && !is_power(emoji, power::balloon)) {
        const bool room = std::ranges::count_if(furniture_slots(result.shown), [](const std::string &taken) {
            return !taken.empty();
        }) < static_cast<std::ptrdiff_t>(carried_limit);
        return departure + reply + std::format(" In casa non fa niente: per averlo con te scrivi {}take {}{}.",
                                               command_prefix(context), emoji, room ? "" : " <emoji da rimettere in casa>");
    }
    return departure + reply + " " +
        capitalized(bonus_note(context.config, command_prefix(context), emoji, result.shown, result.house,
                               result.with_him)) + ".";
}

/* Every line the game understands, one example each, written the way the asker has to write it. */
std::string handle_help(const CommandContext &context, std::string_view) {
    const bool irc = context.user_id == 0;
    const std::string_view other = irc ? "giocatore" : "@giocatore";
    const std::string_view slash = command_prefix(context);
    std::string help = "Come si gioca\n\n";
    const auto line = [&help](std::string_view example, std::string_view meaning) {
        help += std::format("{} — {}\n", example, meaning);
    };
    /* The one We line the help still shows: the others all have a command of their own. */
    line(std::format("We {} (o {}avventura)", conquister_place, slash),
         std::format("entri in {}: 1 palla al secondo finché lo tieni", conquister_place));
    line(std::format("{}raid {}", slash, other), "parti per razziarlo");
    line(std::format("{}give {} 500", slash, other), "gli porti 500 palle");
    line(std::format("{}give {} 🍕", slash, other), "gli regali una 🍕; anche una 💣 arriva intatta e la potrà usare lui");
    line(std::format("{}throw {} 💣", slash, other), "gli lanci una 💣, che gli esplode addosso");
    line(std::format("{}buy ⚡", slash), std::format("compri ⚡, da casa tua: va con te se hai posto (ne puoi avere {}), "
                                                    "altrimenti in casa; quelle senza bonus vanno in casa", carried_limit));
    line(std::format("{}buy 🍕 3", slash), "la compri e la metti nel posto 3 della casa");
    line(std::format("{}take ⚡", slash), "prendi ⚡ dalla casa e lo porti con te, dove dà il suo bonus");
    line(std::format("{}take ⚡ 🚀", slash), "prendi ⚡ e rimetti in casa 🚀 al suo posto");
    line(std::format("{}store ⚡", slash), "rimetti ⚡ in casa");
    line(std::format("{}move 1 2", slash), "sposti l'emoji dal posto 1 al posto 2 della casa");
    line(std::format("{}back", slash), std::format("torni a casa tua, da {} o dal viaggio", conquister_place));
    line(std::format("{}burn 500", slash), "bruci 500 palle");
    line(std::format("{}burn 🍕", slash), std::format("bruci una 🍕; una 💣 brucia senza colpire chi è in {}",
                                                     conquister_place));
    help += std::format("\nAccanto al nome si vede quello che hai con te; la casa la vedi con {}house. Razzie, regali e "
                        "lanci partono solo da casa tua, e quello che porti a qualcuno viaggia con te. In viaggio si "
                        "può solo tornare indietro, con {}back.\n", slash, slash);
    help += std::format("\n{0}leaderboard — classifica\n{0}profile [nome] — il tuo profilo o quello di un altro\n"
                        "{0}house [nome] — la tua casa posto per posto, o quella di un altro\n"
                        "{0}emoji — cosa fa ogni emoji con un potere, dove sta e cosa la può colpire\n"
                        "{0}addquote <testo> — aggiungi una citazione\n"
                        "{0}link <nome> — collega account Telegram e nick IRC Azzurra registrato",
                        slash);
    return help;
}

/* What one emoji with a power does, in the players' words and with the numbers of this game; nothing
   for one this list has not been told about, which the tests refuse. */
std::string power_help(const Power &power, const AppConfig &config) {
    const auto is = [&power](const Power &other) { return is_power(power.emoji, other); };
    if (is(power::pleading)) {
        return std::format("chi ti razzia ruba il {}% in meno per ognuna", config.pleading_percent);
    }
    if (is(power::dog)) {
        return std::format("ognuno ha il {}% di fermare una razzia: il ladro torna a mani vuote", config.dog_percent);
    }
    if (is(power::mailbox)) {
        return std::format("ognuna ha il {}% di rispedire al mittente quello che ti lanciano a casa",
                           config.mailbox_percent);
    }
    if (is(power::alarm)) {
        return std::format("ognuno toglie {} punti alla probabilità che un 🥷 passi di nascosto", config.alarm_percent);
    }
    if (is(power::hen)) {
        return std::format("ognuna fa {} al minuto, anche quando sei fuori", palle(config.hen_per_minute));
    }
    if (is(power::dino)) {
        return std::format("ognuno ha il {}% di mangiare a chi ti razzia un'emoji che ha con sé", config.dino_percent);
    }
    if (is(power::salt)) {
        return std::format("se la stessa persona arriva a razziarti di nuovo entro {}, ti dà il {}% delle sue palle "
                           "per ognuno, fino a tutte", format_wait(salt_seconds), config.salt_percent);
    }
    if (is(power::bolt)) {
        return std::format("+{}% di palle in {} per ognuno", config.lightning_percent, conquister_place);
    }
    if (is(power::rocket)) {
        return std::format("viaggi il {}% più veloce per ognuno", config.rocket_percent);
    }
    if (is(power::lobster)) {
        return std::format("in {} diventa l'emoji che chi hai cacciato ha con sé nello stesso posto", conquister_place);
    }
    if (is(power::balloon)) {
        return std::format("il palloncino: con te difende te, casa tua quando ci sei e {} quando lo tieni; in casa "
                           "difende la casa anche quando sei fuori; bucato torna nuovo; uno nuovo costa {}",
                           conquister_place, palle(config.balloon_cost));
    }
    if (is(power::ninja)) {
        return std::format("ognuno ha il {}% di farti passare oltre 🎈 e 🐶 senza toccarli", config.ninja_percent);
    }
    if (is(power::pirate)) {
        return std::format("ognuna ha il {}% di rubare un'emoji da casa di chi razzi", config.pirate_percent);
    }
    if (is(power::poo)) {
        return std::format("chi la prende è \"lo smerdato\" per {}", format_wait(config.smeared_seconds));
    }
    if (is(power::bomb)) {
        return std::format("distrugge un'emoji con un potere dove esplode; il {}% sono difettose e scoppiano in mano",
                           config.bomb_dud_percent);
    }
    if (is(power::seed)) {
        return std::format("dopo {} nasce un bambino sul nome di chi la riceve; a casa sua solo se lui è in casa",
                           format_wait(config.pregnancy_seconds));
    }
    if (is(power::nuke)) {
        return std::format("costa {}: azzera un giocatore, o tutto il gioco se lanciata a {}", palle(config.nuke_cost),
                           conquister_place);
    }
    if (is(power::ice)) {
        return std::format("chi la prende resta congelato per {}: non può entrare in {} né partire",
                           format_wait(config.frozen_seconds), conquister_place);
    }
    if (is(power::hourglass)) {
        return std::format("ognuna accorcia del {}% la penalità dopo un tentativo fallito di entrare in {}",
                           config.hourglass_percent, conquister_place);
    }
    if (is(power::fire)) {
        return std::format("ognuno accorcia del {}% il tempo che resti congelato da una 🧊", config.fire_percent);
    }
    return {};
}

/* "/emoji": every emoji with a power, by where it is, with what can and cannot hit it. */
std::string handle_emoji_help(const CommandContext &context, std::string_view) {
    const auto list = [&context](PowerKind kind) {
        std::string lines;
        for (const Power &power : powers) {
            if (power.kind == kind) {
                std::string looks{power.emoji};
                for (const std::string_view also : power.also) {
                    if (!also.empty()) {
                        looks += std::format(" {}", also);
                    }
                }
                lines += std::format("{} {}{}\n", looks, power_help(power, context.config),
                                     power.untouchable ? ". Invincibile: né bombe né furti" : "");
            }
        }
        return lines;
    };
    std::string help = "Emoji con un potere\n\n";
    const std::string_view slash = command_prefix(context);
    help += std::format("Hai due posti per le emoji: con te ({} posti), accanto al nome, e la casa ({} posti), con "
                        "{}house. Con {}take le prendi dalla casa, con {}store le rimetti in casa. Un'emoji dove non "
                        "funziona non fa niente.\n\n", carried_limit, context.config.furniture_limit, slash, slash,
                        slash);
    help += "Funzionano in casa, anche quando sei fuori. Colpibili da una 💣 lanciata a casa tua e rubabili da una "
            "🏴‍☠️.\n" + list(PowerKind::home);
    help += std::format("\nFunzionano con te, dove sei tu. Colpibili da una 💣 lanciata su di te in {0} e da un 🦖 "
                        "di chi razzi. Quelle che ti regalano mentre sei fuori vanno in casa.\n", conquister_place) +
        list(PowerKind::carried);
    help += std::format("\nSi lanciano: {0}throw nome emoji su casa sua, We {1} emoji su chi è dentro. Si consumano e "
                        "partono con te. 🎈 e 🐶 non le fermano, solo la 📮.\n", slash, conquister_place) +
        list(PowerKind::thrown);
    help += std::format("\nI bambini (👶 👦 👧 👨 👩 👴 👵) nascono da una 💦 nella casa, crescono di un'età ogni {}, da "
                        "adulti fanno {} al secondo e poi se ne vanno. Sono intoccabili: restano in casa, si possono "
                        "solo spostare, mai togliere.\n", format_wait(context.config.child_stage_seconds),
                        palle(context.config.adult_per_second));
    help += "\nTutte le altre emoji sono decorative: non fanno niente, le bombe non le toccano, ma una 🏴‍☠️ può "
            "rubarle dalla casa.";
    return help;
}

/* The player a command asks about: the sender when nobody is named, else the one named as on that
   platform. Nothing, and what to say instead, when there is no such player. */
std::optional<Profile> asked_about(const CommandContext &context, std::string_view argument, std::int64_t now,
                                   std::string &refusal) {
    const std::string_view wanted = text::trim(argument);
    if (wanted.empty()) {
        if (context.username.empty()) {
            refusal = missing_username_reply();
            return std::nullopt;
        }
        return player_profile_of(context.storage, std::string{context.player_key}, now);
    }
    const bool telegram = wanted.starts_with('@');
    std::optional<Profile> found = player_profile(context.storage, telegram ? wanted.substr(1) : wanted,
                                                  telegram ? RaidTargetKind::telegram : RaidTargetKind::irc, now);
    if (!found) {
        refusal = std::format("{} non conosco nessun giocatore di nome {}.", context.username, wanted);
    }
    return found;
}

/* "/house [name]": what is in his house, slot by slot with their numbers. */
std::string handle_house(const CommandContext &context, std::string_view argument) {
    std::string refusal;
    const std::optional<Profile> found = asked_about(context, argument, seconds_now(), refusal);
    if (!found) {
        return refusal;
    }
    const std::vector<std::string> slots = furniture_slots(found->house);
    const std::size_t shown = std::max(slots.size(), static_cast<std::size_t>(context.config.furniture_limit));
    const auto used = std::ranges::count_if(slots, [](const std::string &slot) { return !slot.empty(); });
    std::string reply = std::format("Casa di {} ({}/{}):\n", found->name, used, context.config.furniture_limit);
    for (std::size_t slot = 0; slot < shown; ++slot) {
        reply += std::format("{}{} {}", slot == 0 ? "" : "  ", slot + 1,
                             slot < slots.size() && !slots[slot].empty() ? slots[slot] : std::string{"·"});
    }
    return reply;
}

/* "/profile [name]": his own card, or the one of the player named as on that platform. */
std::string handle_profile(const CommandContext &context, std::string_view argument) {
    const std::int64_t now = seconds_now();
    std::string refusal;
    const std::optional<Profile> found = asked_about(context, argument, now, refusal);
    if (!found) {
        return refusal;
    }
    const Profile &profile = *found;
    /* The bare name at the top, then what he has on him: the house has /house. His home below is written like
       everywhere else. */
    std::string card = std::format("{}\n", with_furniture(profile.name, {}, profile.smeared));
    card += std::format("Con te: {}\n", profile.furniture.empty() ? std::string{"niente"} : profile.furniture);
    card += profile.rank == 0 ? std::string{"nessuna palla ancora\n"}
                              : std::format("{}, {}° su {} in classifica\n", palle(profile.score), profile.rank,
                                            profile.players);
    const zodiac::Sign sign = zodiac::sign_of(profile.name, context.config.zodiac_signs);
    const int percent = zodiac::percent_for(profile.name, now, context.config.zodiac_signs);
    card += std::format("{} {}: oggi è giorno di {}, {}\n", sign.symbol, sign.name,
                        zodiac::element_name(zodiac::element_of_day(now)),
                        multiplier_text(percent));
    if (profile.frozen_for > 0) {
        card += std::format("congelato da una 🧊 ancora per {}\n", format_wait(profile.frozen_for));
    }
    if (profile.hens > 0 && context.config.hen_per_minute > 0) {
        card += std::format("{} 🐔: {} all'ora\n", profile.hens, palle(profile.hens * context.config.hen_per_minute * 60));
    }
    switch (profile.place) {
    case Whereabouts::home:
        card += std::format("a casa, in {}{}\n", profile.on_telegram ? "@" : "", profile.name);
        break;
    case Whereabouts::conquister:
        card += std::format("in {} da {}{}\n", conquister_place, format_wait(std::max<std::int64_t>(now - profile.since, 0)),
                            profile.lightning_percent > 100 ? std::format(" col ⚡ {}", multiplier_text(profile.lightning_percent))
                                                            : std::string{});
        break;
    case Whereabouts::road:
        card += std::format("{} {}: rientra tra {}\n", profile.returning ? "sulla via del ritorno da" : "in viaggio verso",
                            profile.heading, format_wait(profile.home_in));
        break;
    }
    if (profile.quotes_added > 0) {
        card += std::format("{} citazion{}\n", profile.quotes_added, profile.quotes_added == 1 ? "e" : "i");
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
        "Citazioni {}-{} di {} (pagina {}/{}):",
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
    return on ? "Debug acceso per te: i tuoi acquisti non costano niente. Gli altri pagano."
              : "Debug spento: i tuoi acquisti tornano a costare.";
}

/* "/buy" with nothing after it: what an emoji is bought with is the line that expands it. */
std::string handle_buy(const CommandContext &context, std::string_view) {
    return std::format("Uso: {0}buy <emoji> [posto], per esempio {0}buy 🍕 o {0}buy 🍕 3.", command_prefix(context));
}

/* How each verb is used, for whoever writes it bare or with the wrong thing after it. */
std::string verb_usage(const CommandContext &context, Verb verb) {
    const std::string_view slash = command_prefix(context);
    const std::string_view other = context.user_id == 0 ? "giocatore" : "@giocatore";
    switch (verb) {
    case Verb::raid:
        return std::format("Uso: {0}raid <giocatore>, per esempio {0}raid {1}: parti per razziarlo.", slash, other);
    case Verb::give:
        return std::format("Uso: {0}give <giocatore> <emoji o palle>, per esempio {0}give {1} 🍕 o {0}give {1} 500: "
                           "gliele regali, anche una 💣, che arriva intatta.", slash, other);
    case Verb::throw_:
        return std::format("Uso: {0}throw <giocatore> <emoji da lanciare>, per esempio {0}throw {1} 💣: gli esplode "
                           "addosso.", slash, other);
    case Verb::burn:
        return std::format("Uso: {0}burn <emoji o palle>, per esempio {0}burn 🍕 o {0}burn 500: escono dal gioco, "
                           "e anche una 💣 brucia senza colpire nessuno.", slash);
    case Verb::none:
        break;
    }
    return {};
}

std::string handle_raid_usage(const CommandContext &context, std::string_view) {
    return verb_usage(context, Verb::raid);
}

std::string handle_give(const CommandContext &context, std::string_view) {
    return verb_usage(context, Verb::give);
}

std::string handle_throw(const CommandContext &context, std::string_view) {
    return verb_usage(context, Verb::throw_);
}

std::string handle_burn_usage(const CommandContext &context, std::string_view) {
    return verb_usage(context, Verb::burn);
}

/* The emoji written after a command, one by one, spaces or not between them; nothing if anything else is there. */
std::optional<std::vector<std::string>> emoji_list(std::string_view argument) {
    std::string joined;
    for (const char character : argument) {
        if (character != ' ' && character != '\t') {
            joined += character;
        }
    }
    if (joined.empty()) {
        return std::nullopt;
    }
    return text::emoji_split(joined);
}

/* "/take emoji [emoji]": one from the house goes on him, the second, if named, back in the house to make room. */
std::string handle_take(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string_view slash = command_prefix(context);
    const std::optional<std::vector<std::string>> named = emoji_list(argument);
    if (!named || named->size() > 2) {
        return std::format("Uso: {0}take <emoji>, per esempio {0}take ⚡: prendi l'emoji dalla casa e la porti con te. "
                           "Con {1} emoji già con te, {0}take ⚡ 🚀 mette ⚡ al posto di 🚀, che torna in casa.",
                           slash, carried_limit);
    }
    const std::string &emoji = named->front();
    const std::string swap = named->size() == 2 ? named->back() : std::string{};
    const std::int64_t now = seconds_now();
    const GearResult result = gear_take(context.storage, std::string{context.player_key}, emoji, swap, now,
                                        context.config.zodiac_signs);
    const std::string departure = departure_line(context, result.departure, now);
    const std::string_view username = context.username;
    switch (result.status) {
    case GearStatus::not_home:
        return on_the_road(context, "le emoji si prendono da casa tua.");
    case GearStatus::not_owned:
        return departure + std::format("{} non hai {} in casa.", username, emoji);
    case GearStatus::already_on:
        return departure + std::format("{} {} è già con te.", username, emoji);
    case GearStatus::child:
        return departure + std::format("{} i bambini restano in casa.", username);
    case GearStatus::full:
        return departure + std::format("{} hai già {} emoji con te ({}): scrivi {}take {} <emoji> per mettere {} al "
                                       "posto di una di quelle, che torna in casa.", username, carried_limit,
                                       result.shown, slash, emoji, emoji);
    case GearStatus::swap_missing:
        return departure + std::format("{} non hai {} con te.", username, swap);
    default:
        break;
    }
    const std::string note = bonus_note(context.config, slash, emoji, result.shown, result.house, true);
    if (result.status == GearStatus::swapped) {
        return departure + std::format("{} {} ora è con te al posto di {}, che torna in casa: {}.",
                                       with_furniture(username, result.shown, false), emoji, result.swapped, note);
    }
    return departure + std::format("{} {} ora è con te: {}.", with_furniture(username, result.shown, false), emoji, note);
}

/* "/store emoji": one he has on him goes back in the house. */
std::string handle_store(const CommandContext &context, std::string_view argument) {
    if (context.username.empty()) {
        return missing_username_reply();
    }
    const std::string_view slash = command_prefix(context);
    const std::optional<std::vector<std::string>> named = emoji_list(argument);
    if (!named || named->size() != 1) {
        return std::format("Uso: {0}store <emoji>, per esempio {0}store ⚡: rimetti in casa un'emoji che hai con te.",
                           slash);
    }
    const std::string &emoji = named->front();
    const std::int64_t now = seconds_now();
    const GearResult result = gear_store(context.storage, std::string{context.player_key}, emoji,
                                         static_cast<std::size_t>(context.config.furniture_limit), now,
                                         context.config.zodiac_signs);
    const std::string departure = departure_line(context, result.departure, now);
    const std::string_view username = context.username;
    switch (result.status) {
    case GearStatus::not_home:
        return on_the_road(context, "le emoji si rimettono in casa da casa tua.");
    case GearStatus::not_on:
        return departure + std::format("{} non hai {} con te.", username, emoji);
    case GearStatus::house_full:
        return departure + std::format("{} la casa è piena: per fare posto brucia qualcosa con {}burn <emoji>.",
                                       username, slash);
    default:
        break;
    }
    return departure + std::format("{} {} ora è in casa: {}.", with_furniture(username, result.shown, false), emoji,
                                   bonus_note(context.config, slash, emoji, result.shown, result.house, false));
}

/* "/back" and a good "/move" are expanded before they get here: only what cannot be is left. */
std::string handle_home(const CommandContext &, std::string_view) {
    return missing_username_reply();
}

std::string handle_move_usage(const CommandContext &context, std::string_view) {
    return std::format("Uso: {0}move <da> <a>, per esempio {0}move 1 2: sposti l'emoji dal posto 1 al posto 2 della casa.",
                       command_prefix(context));
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
    CommandDefinition{"/emoji", handle_emoji_help},
    CommandDefinition{"/profile", handle_profile},
    CommandDefinition{"/addquote", handle_add_quote},
    CommandDefinition{"/link", handle_link},
    CommandDefinition{"/quotes", handle_quotes},
    CommandDefinition{"/delquote", handle_delete_quote},
    CommandDefinition{"/debug", handle_debug},
    CommandDefinition{"/buy", handle_buy},
    CommandDefinition{"/raid", handle_raid_usage},
    CommandDefinition{"/give", handle_give},
    CommandDefinition{"/throw", handle_throw},
    CommandDefinition{"/burn", handle_burn_usage},
    CommandDefinition{"/back", handle_home},
    CommandDefinition{"/house", handle_house},
    CommandDefinition{"/move", handle_move_usage},
    CommandDefinition{"/take", handle_take},
    CommandDefinition{"/store", handle_store},
};

const CommandDefinition *find_command(std::string_view name) {
    const auto found = std::ranges::find_if(commands, [name](const CommandDefinition &definition) {
        return definition.name == name;
    });
    return found != commands.end() ? &*found : nullptr;
}

}

bool command_is_for_bot(std::string_view text) {
    const std::string expanded = expand_adventure(text::trim(text));
    const std::string_view message = expanded;
    return message == conquister_trigger || !raid_target(message).empty() || amount_target(message).has_value() ||
           emoji_target(message).has_value() || slot_move(message).has_value() ||
           (!message.empty() && find_command(parse_command(message).name) != nullptr);
}

std::optional<std::string> raid_event_reply(const RaidEvent &event, const AppConfig &config) {
    const std::string_view mention = event.target_on_telegram ? "@" : "";
    /* His planet, with the mention where it reaches him, as everywhere else. */
    const std::string home = std::format("{}{}", event.raider_on_telegram ? "@" : "", event.raider);
    /* The bare name for whoever is spoken to, the dressed one when somebody is named. */
    const std::string raider = with_furniture(event.raider, event.raider_emoji, event.raider_smeared);
    const std::string target = with_furniture(event.target, event.target_emoji, event.target_smeared);
    /* The same with the mention, which goes on the name, after what he has on him. */
    const std::string named =
        with_furniture(std::format("{}{}", mention, event.target), event.target_emoji, event.target_smeared);
    if (event.kind == RaidEvent::Kind::gone) {
        return std::format("{} {} ha vissuto la sua vita e se n'è andato: il posto è di nuovo libero.",
                           named, event.gift_emoji);
    }
    if (event.kind == RaidEvent::Kind::born) {
        const bool boy = event.gift != 0;
        std::string news = std::format("{} {} {}: ", named,
                                       boy ? "è nato un maschio" : "è nata una femmina", event.gift_emoji);
        news += event.raider.empty() ? std::string{"i genitori sono il 👨 e la 👩 di casa."}
                                     : std::format("il padre è {}{}.", event.raider_on_telegram ? "@" : "", event.raider);
        if (!event.blown.empty()) {
            news += std::format(" Non c'era un posto libero: ha preso quello di {}.", event.blown.front());
        }
        return news;
    }
    if (event.kind == RaidEvent::Kind::returned) {
        if (!event.gift_emoji.empty()) {
            return std::format("{} torni in {} con {} ancora in tasca.", raider, home, event.gift_emoji);
        }
        if (event.gift > 0) {
            return std::format("{} torni in {} con {} ancora in tasca.", raider, home,
                               event.gift == 1 ? std::string{"la tua palla"} : std::format("le tue {} palle", event.gift));
        }
        if (event.loot > 0) {
            return std::format("{} torni in {} con {}.", raider, home, palle(event.loot));
        }
        /* Empty-handed too: he needs to know he is home and can leave again. */
        return std::format("{} sei tornato in {}.", raider, home);
    }
    if (event.kind == RaidEvent::Kind::delivered && !event.gift_emoji.empty()) {
        if (event.no_room) {
            return std::format("{} {} non ha più posto per {}: te la riporti a casa tua. Torni in {} tra {}.",
                               raider, named, event.gift_emoji, home, format_wait(event.seconds));
        }
        /* What went on him rather than in his house: what it does for him there. */
        const std::string kept = event.gift_with_him
            ? std::format("\n{}{} {} ora è con te: {}.", mention, event.target, event.gift_emoji,
                          bonus_note(config, "/", event.gift_emoji, event.target_emoji, {}, true))
            : std::string{};
        /* A present arrives as it is, whatever it would do if thrown. */
        if (event.intact) {
            return std::format("{} hai regalato {} a {}! Torni in {} tra {}.{}",
                               raider, event.gift_emoji, named, home, format_wait(event.seconds), kept);
        }
        if (event.sent_back) {
            std::string blown;
            for (const std::string &emoji : event.blown) {
                blown += emoji;
            }
            const std::string what = is_power(event.gift_emoji, power::ice)
                ? (event.froze == 0 ? std::string{"il tuo 🔥 la scioglie subito"}
                                    : std::format("resti congelato per {}", format_wait(event.froze)))
                : is_power(event.gift_emoji, power::seed)
                ? std::format("tra {} si vedrà, e sarà tutto tuo", format_wait(event.expecting))
                : is_power(event.gift_emoji, power::poo) ? "ora lo smerdato sei tu"
                : is_power(event.gift_emoji, power::nuke) ? "riparti da zero, senza palle e con il solo 🎈 di partenza"
                : blown.empty() ? "esplode a casa tua ma non trova niente da portarsi via"
                : std::format("esplode a casa tua e si porta via {}", blown);
            return std::format("La cassetta di {}{} rispedisce {} al mittente: {} {}! Torni in {} tra {}.", mention,
                               event.target, event.gift_emoji, event.raider, what, home, format_wait(event.seconds));
        }
        if (is_power(event.gift_emoji, power::poo)) {
            return poo_throw(home, std::format("{}{}", mention, event.target));
        }
        if (event.nobody_home) {
            return std::format("{} a casa di {} non c'è nessuno: la {} te la riporti a casa. Torni in {} tra {}.",
                               raider, named, event.gift_emoji, home, format_wait(event.seconds));
        }
        if (is_power(event.gift_emoji, power::ice) && !event.sent_back) {
            if (event.froze == 0) {
                return std::format("{} il 🔥 di {}{} scioglie subito la tua 🧊: non resta congelato. Torni in {} tra {}.",
                                   raider, mention, event.target, home, format_wait(event.seconds));
            }
            return std::format("{} congeli {}{}{}: per {} non può entrare in {} né partire. Torni in {} tra {}.", raider,
                               mention, event.target, event.melted ? ", ma il suo 🔥 accorcia il gelo" : "",
                               format_wait(event.froze), conquister_place, home, format_wait(event.seconds));
        }
        if (event.expecting > 0 && !event.sent_back) {
            return std::format("{} la tua 💦 è arrivata a casa di {}{}: tra {} si vedrà. Torni in {} tra {}.", raider,
                               mention, event.target, format_wait(event.expecting), home, format_wait(event.seconds));
        }
        if (is_power(event.gift_emoji, power::nuke)) {
            return std::format("{} ha sganciato la bomba nucleare su casa di {}{}: riparte da zero, senza palle e "
                               "con il solo 🎈 di partenza. Torni in {} tra {}.", raider, mention, event.target, home,
                               format_wait(event.seconds));
        }
        if (is_power(event.gift_emoji, power::bomb)) {
            std::string blown;
            for (const std::string &emoji : event.blown) {
                blown += emoji;
            }
            if (event.backfired) {
                return dud_reply(raider, blown, std::format(" Torni in {} tra {}.", home, format_wait(event.seconds)));
            }
            return blown.empty()
                ? std::format("{} la tua bomba esplode in casa di {} ma non trova niente da portarsi via. "
                              "Torni in {} tra {}.", raider, named, home, format_wait(event.seconds))
                : std::format("{} la tua bomba esplode in casa di {} e si porta via {}! Torni in {} tra {}.",
                              raider, named, blown, home, format_wait(event.seconds));
        }
        return std::format("{} hai consegnato {} a {}! Torni in {} tra {}.{}",
                           raider, event.gift_emoji, named, home, format_wait(event.seconds), kept);
    }
    if (event.kind == RaidEvent::Kind::delivered) {
        return std::format(
            "{} hai consegnato {} a {}! Torni in {} tra {}.",
            raider,
            palle(event.gift),
            named,
            home,
            format_wait(event.seconds)
        );
    }
    std::string alarm = event.alarmed
        ? std::format("L'allarme di {}{} ti scopre: devi vedertela con le sue difese.\n", mention, event.target)
        : std::string{};
    if (!event.eaten.empty()) {
        alarm += std::format("Il 🦖 di {}{} ti mangia {}.\n", mention, event.target, event.eaten);
    }
    if (event.salted > 0) {
        alarm += std::format("Ci torni troppo presto: il 🧂 di {}{} ti costa {}, che vanno a lui.\n", mention,
                             event.target, palle(event.salted));
    }
    if (event.balloon_held) {
        return alarm + std::format(
            "{} il palloncino di {} ha resistito. "
            "Ora il palloncino ha il {}% di probabilità di essere bucato. Torni in {} tra {}.",
            raider,
            named,
            event.next_chance,
            home,
            format_wait(event.seconds)
        );
    }
    if (event.intercepted) {
        return alarm + std::format("{} il cane di {} ti ha intercettato: niente bottino. Torni in {} tra {}.", raider,
                           named, home, format_wait(event.seconds));
    }
    std::string reply = alarm;
    reply += event.sneaked
        ? std::format("{} scivoli di nascosto oltre le difese di {} e rubi {}", raider, named,
                      palle(event.loot))
        : event.balloon_popped
        ? std::format("{} hai bucato il palloncino di {} e rubato {}", raider, named, palle(event.loot))
        : std::format("{} hai rubato {} a {}", raider, palle(event.loot), named);
    if (event.undefended) {
        reply += ", che non era a casa";
    }
    reply += std::format("! Torni in {} tra {}.", home, format_wait(event.seconds));
    if (!event.boarded.empty()) {
        reply += std::format("\nArrembaggio: ti porti via anche {} da casa sua, e ora è con te: {}.", event.boarded,
                             bonus_note(config, "/", event.boarded, event.raider_emoji, {}, true));
    }
    if (event.spared > 0) {
        reply += std::format("\n{} ti ha impietosito: gli rubi {} invece di {} ({}% in meno).", target,
                             palle(event.loot), palle(event.spared + event.loot), event.pleaded_percent);
    }
    return reply;
}

std::optional<std::string> command_dispatch(const CommandContext &context, std::string_view text) {
    try {
        const Verbed verbed = expand_verb(expand_buy(context, expand_adventure(text::trim(text))));
        const std::string_view message = verbed.message;
        const Verb verb = verbed.verb;
        CommandContext bound = context;
        std::string bound_key;
        const auto remember_sender = [&] {
            if (!context.username.empty()) {
                bound_key = player_seen(context.storage, context.user_id, std::string{context.username},
                                        context.account_name,
                                        /* No room at all is how nobody is handed one. */
                                        context.config.starter_balloon
                                            ? static_cast<std::size_t>(context.config.furniture_limit) : 0);
                bound.player_key = bound_key;
            }
        };
        /* Whether a name written after "We" is the sender himself. */
        const auto names_me = [&](std::string_view target) {
            const bool telegram = target.starts_with('@');
            return !bound.player_key.empty() &&
                names_player(context.storage, bound_key, telegram ? target.substr(1) : target,
                             telegram ? RaidTargetKind::telegram : RaidTargetKind::irc);
        };
        if (message == conquister_trigger) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            if (verb != Verb::none) {
                return verb_usage(context, verb);
            }
            remember_sender();
            return handle_claim(bound, {});
        }
        if (const auto transfer = amount_target(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            if (verb == Verb::raid || verb == Verb::throw_) {
                return verb_usage(context, verb);
            }
            remember_sender();
            /* Nothing is brought to the place any more: palle are burnt with /burn. */
            if (names_the_place(transfer->target)) {
                if (verb != Verb::burn) {
                    return std::format("{} a {} non si portano palle: per bruciarle scrivi {}burn {}.",
                                       context.username, conquister_place, command_prefix(context), transfer->amount);
                }
                return handle_burn(bound, transfer->amount);
            }
            if (names_me(transfer->target)) {
                return std::format("{} non puoi portare palle a te stesso.", context.username);
            }
            return handle_gift(bound, *transfer);
        }
        if (const auto move = slot_move(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            if (verb != Verb::none) {
                return verb_usage(context, verb);
            }
            remember_sender();
            if (context.username.empty()) {
                return missing_username_reply();
            }
            if (!names_me(move->target)) {
                return std::format("{} puoi spostare solo le emoji sul tuo nome.", context.username);
            }
            return handle_furniture_move(bound, *move);
        }
        if (const auto carried = emoji_target(message)) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            if (verb == Verb::raid) {
                return verb_usage(context, verb);
            }
            remember_sender();
            if (text::emoji_count(carried->emoji) != std::optional<std::size_t>{1}) {
                return std::format("{} una emoji per volta.", context.username);
            }
            const Power *power = power_of(carried->emoji);
            if (verb == Verb::throw_ && (power == nullptr || power->kind != PowerKind::thrown)) {
                return std::format("{} {} non si lancia: per regalarla scrivi {}give {} {}.", context.username,
                                   carried->emoji, command_prefix(context), carried->target, carried->emoji);
            }
            if (names_me(carried->target)) {
                if (verb != Verb::none) {
                    return std::format("{} non puoi {} a te stesso.", context.username,
                                       verb == Verb::give ? "regalare" : "lanciare");
                }
                return handle_furniture(bound, carried->emoji, carried->position);
            }
            /* The slot is his choice only on his own name: elsewhere the emoji takes the first free one. */
            if (carried->position) {
                return std::format("{} la posizione si sceglie solo sul tuo nome: scrivi We {} {}.",
                                   context.username, carried->target, carried->emoji);
            }
            /* At the place an emoji is only thrown, at whoever holds it; anything else is burnt with /burn. */
            if (names_the_place(carried->target)) {
                if (verb == Verb::burn) {
                    return handle_emoji_burn(bound, carried->emoji, true);
                }
                if (verb == Verb::give || power == nullptr || power->kind != PowerKind::thrown) {
                    return std::format("{} a {} non si porta niente: per bruciare {} scrivi {}burn {}.",
                                       context.username, conquister_place, carried->emoji, command_prefix(context),
                                       carried->emoji);
                }
                return handle_emoji_burn(bound, carried->emoji);
            }
            return handle_raid(bound, carried->target, 0, carried->emoji, verb == Verb::give);
        }
        if (const std::string_view target = raid_target(message); !target.empty()) {
            if (!context.claims_allowed) {
                return std::nullopt;
            }
            if (verb != Verb::none && verb != Verb::raid) {
                return verb_usage(context, verb);
            }
            remember_sender();
            if (verb == Verb::raid && names_me(target)) {
                return std::format("{} non puoi razziare te stesso.", context.username);
            }
            return handle_raid(bound, target);
        }
        if (verb != Verb::none) {
            return verb_usage(context, verb);
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
