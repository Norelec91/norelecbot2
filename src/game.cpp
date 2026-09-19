#include "game.hpp"

#include "logging.hpp"
#include "mishaps.hpp"
#include "quiz.hpp"
#include "forge.hpp"
#include "position.hpp"
#include "text.hpp"
#include "zodiac.hpp"

#include <algorithm>
#include <iterator>
#include <cctype>
#include <cmath>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>

namespace norelecbot {
namespace {

constexpr std::size_t quotes_page_size = 30;

/* Higher score first, then username in byte order. */
constexpr auto ranks_before = [](const auto &first, const auto &second) {
    return first.second != second.second ? first.second > second.second : first.first < second.first;
};

std::int64_t counter(const Counters &counters, const std::string &username) {
    const auto found = counters.find(username);
    return found != counters.end() ? found->second : 0;
}

const Counters::value_type *find_ignore_case(const Counters &counters, std::string_view username) {
    const auto found = std::ranges::find_if(counters, [username](const Counters::value_type &entry) {
        return text::equals_ignore_case(entry.first, username);
    });
    return found != counters.end() ? &*found : nullptr;
}

/* Four attempts at most: the first has one chance in four, the fourth is certain. */
constexpr std::size_t balloon_attempts = 4;

Counters::iterator find_entry(Counters &counters, const std::string &username) {
    return std::ranges::find_if(counters, [&username](const Counters::value_type &entry) {
        return entry.first == username;
    });
}

}

namespace {

/* A balloon that holds costs the attacker, even when it puts him in the red. */
std::int64_t charge_attacker(ConquisterState &state, const std::string &username, int attack_cost) {
    if (attack_cost <= 0) {
        return 0;
    }
    state.scores[username] = counter(state.scores, username) - attack_cost;
    return attack_cost;
}

/* What a hold was worth: the seconds it lasted, times the boost he had bought, times the house of the
   day. Both the boost and the hold are spent by this. */
struct Settlement {
    std::int64_t earned = 0;
    std::int64_t boost_multiplier = 0;
    int zodiac_percent = 100;
};

Settlement settle_hold(
    ConquisterState &state,
    const std::string &holder,
    std::int64_t since,
    std::int64_t now,
    zodiac::Overrides signs
) {
    Settlement settled;
    settled.earned = now > since ? now - since : 0;
    if (const auto boost = find_entry(state.boosts, holder); boost != state.boosts.end()) {
        settled.boost_multiplier = boost->second;
        if (settled.earned > std::numeric_limits<std::int64_t>::max() / settled.boost_multiplier) {
            log_error("Could not multiply the Conquister score");
            throw StorageError("Conquister score overflow");
        }
        settled.earned *= settled.boost_multiplier;
        state.boosts.erase(holder);
    }
    settled.zodiac_percent = zodiac::percent_for(holder, now, signs);
    settled.earned = settled.earned / 100 * settled.zodiac_percent +
                     settled.earned % 100 * settled.zodiac_percent / 100;
    std::int64_t &score = state.scores[holder];
    if (score > 0 && settled.earned > std::numeric_limits<std::int64_t>::max() - score) {
        log_error("Could not update Conquister score");
        throw StorageError("Conquister score overflow");
    }
    score += settled.earned;
    return settled;
}

/* Everybody starts well liked; robbing people spends it, and a day of quiet gives a point back. */
constexpr std::int64_t simpatia_full = 20;
constexpr std::int64_t simpatia_day = 86400;

std::int64_t simpatia_now(ConquisterState &state, const std::string &username, std::int64_t now) {
    const auto found = find_entry(state.simpatia, username);
    if (found == state.simpatia.end()) {
        return simpatia_full;
    }
    const std::int64_t seen = counter(state.simpatia_seen, username);
    const std::int64_t days = seen > 0 && now > seen ? (now - seen) / simpatia_day : 0;
    const std::int64_t value = std::min(simpatia_full, found->second + days);
    if (days > 0) {
        found->second = value;
        state.simpatia_seen[username] = seen + (days * simpatia_day);
    }
    return value;
}

/* Returns what it is worth afterwards. */
std::int64_t simpatia_change(
    ConquisterState &state,
    const std::string &username,
    std::int64_t by,
    std::int64_t now
) {
    const std::int64_t value = std::min(simpatia_full, simpatia_now(state, username, now) + by);
    state.simpatia[username] = value;
    state.simpatia_seen[username] = now;
    return value;
}

/* Whoever is on the road has left his base, and everything in it, unguarded. */
bool is_away(const ConquisterState &state, const std::string &username) {
    return std::ranges::any_of(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
}

Raid *raid_of(ConquisterState &state, const std::string &username) {
    const auto found = std::ranges::find_if(state.raids, [&username](const Raid &raid) {
        return raid.raider == username;
    });
    return found != state.raids.end() ? &*found : nullptr;
}

/* The id is drawn the first time the bot sees a player and never changes: it is where he lives. */
std::int64_t player_id(StorageSession &session, ConquisterState &state, const std::string &username) {
    if (const auto found = find_entry(state.ids, username); found != state.ids.end()) {
        return found->second;
    }
    std::int64_t drawn = 0;
    do {
        drawn = static_cast<std::int64_t>(session.random_index(static_cast<std::size_t>(position::ids)));
    } while (std::ranges::any_of(state.ids, [drawn](const Counters::value_type &entry) {
        return entry.second == drawn;
    }));
    state.ids[username] = drawn;
    return drawn;
}

/* Kept so that a message about this player can reach them where they play. */
void remember_telegram(ConquisterState &state, const std::string &username, std::int64_t user_id) {
    if (user_id != 0 && counter(state.telegram_ids, username) != user_id) {
        state.telegram_ids[username] = user_id;
    }
}

/* The name as it is written on file, whatever spelling the message used. */
std::optional<std::string> known_player(const ConquisterState &state, std::string_view name) {
    for (const Counters *counters : {&state.scores, &state.quotes_added, &state.ids}) {
        if (const Counters::value_type *found = find_ignore_case(*counters, name); found != nullptr) {
            return found->first;
        }
    }
    if (state.current && text::equals_ignore_case(state.current->username, name)) {
        return state.current->username;
    }
    return std::nullopt;
}

}

ClaimResult conquister_claim(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::int64_t now,
    const ClaimRules &rules
) {
    const ClaimResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        remember_telegram(state, username, user_id);
        ClaimResult outcome;
        /* A place is held by standing in it, not from the road. */
        if (const Raid *travelling = raid_of(state, username); travelling != nullptr) {
            outcome.status = ClaimStatus::travelling;
            outcome.travel_seconds = std::max<std::int64_t>(travelling->back - now, 0);
            return outcome;
        }
        if (const auto penalty = find_entry(state.cooldowns, username); penalty != state.cooldowns.end()) {
            if (penalty->second > now) {
                outcome.status = ClaimStatus::cooldown;
                outcome.penalty_seconds = penalty->second - now;
                return outcome;
            }
            state.cooldowns.erase(username);
        }
        if (state.current && state.current->username == username) {
            outcome.status = ClaimStatus::already_held;
            return outcome;
        }
        if (state.current && !state.current->username.empty()) {
            const std::string holder = state.current->username;
            outcome.previous_user_id = state.current->user_id;
            if (const auto shield = find_entry(state.shields, holder); shield != state.shields.end()) {
                if (shield->second > now && rules.ignores_shield) {
                    state.shields.erase(holder);
                    outcome.balloon_popped = true;
                } else if (shield->second > now) {
                    if (rules.cooldown_seconds > 0) {
                        state.cooldowns[username] = now + rules.cooldown_seconds;
                        outcome.penalty_seconds = rules.cooldown_seconds;
                    }
                    outcome.attack_cost = charge_attacker(state, username, rules.attack_cost);
                    outcome.status = ClaimStatus::defended;
                    outcome.previous_username = holder;
                    outcome.shield_seconds = shield->second - now;
                    return outcome;
                }
                state.shields.erase(holder);
            }
            if (const auto balloon = find_entry(state.balloons, holder); balloon != state.balloons.end()) {
                const std::int64_t attempt = balloon->second + 1;
                if (static_cast<std::int64_t>(session.random_index(balloon_attempts)) >= attempt) {
                    balloon->second = attempt;
                    if (rules.cooldown_seconds > 0) {
                        state.cooldowns[username] = now + rules.cooldown_seconds;
                        outcome.penalty_seconds = rules.cooldown_seconds;
                    }
                    outcome.attack_cost = charge_attacker(state, username, rules.attack_cost);
                    outcome.status = ClaimStatus::defended;
                    outcome.previous_username = holder;
                    outcome.next_chance =
                        static_cast<int>((attempt + 1) * 100 / static_cast<std::int64_t>(balloon_attempts));
                    return outcome;
                }
                state.balloons.erase(holder);
                outcome.balloon_popped = true;
            }
            outcome.previous_username = holder;
            const Settlement settled = settle_hold(state, holder, state.current->since, now, rules.signs);
            outcome.earned = settled.earned;
            outcome.boost_multiplier = settled.boost_multiplier;
            outcome.zodiac_percent = settled.zodiac_percent;
        }
        state.current = Holder{user_id, username, now};
        return outcome;
    });

    switch (result.status) {
    case ClaimStatus::taken:
        log_info(
            "claim taken user={} previous={} earned={} balloon_popped={} boost={}",
            username,
            result.previous_username,
            result.earned,
            result.balloon_popped ? 1 : 0,
            result.boost_multiplier
        );
        break;
    case ClaimStatus::defended:
        log_info(
            "claim defended user={} holder={} next_chance={} penalty={} shield={} cost={}",
            username,
            result.previous_username,
            result.next_chance,
            result.penalty_seconds,
            result.shield_seconds,
            result.attack_cost
        );
        break;
    case ClaimStatus::cooldown:
        log_info("claim blocked user={} wait={}", username, result.penalty_seconds);
        break;
    case ClaimStatus::travelling:
        log_info("claim refused user={} travelling={}", username, result.travel_seconds);
        break;
    case ClaimStatus::already_held:
        break;
    }
    return result;
}

DisputeResult dispute_open(Storage &storage, const std::string &username, std::int64_t now) {
    const DisputeResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        DisputeResult opened;
        /* Whoever is holding palle taken from him is the one he has a case against. */
        const auto took = std::ranges::find_if(state.loot_from, [&username](const auto &entry) {
            return text::equals_ignore_case(entry.second, username);
        });
        if (took == state.loot_from.end()) {
            opened.status = DisputeStatus::nothing_to_report;
            return opened;
        }
        opened.seller = took->first;
        opened.palle = counter(state.loot_amount, opened.seller);
        if (state.dispute_buyer.find(opened.seller) != state.dispute_buyer.end()) {
            opened.status = DisputeStatus::already_open;
            return opened;
        }
        if (counter(state.loot_when, opened.seller) + dispute_window_seconds < now) {
            opened.status = DisputeStatus::too_late;
            return opened;
        }
        state.dispute_buyer[opened.seller] = username;
        state.dispute_amount[opened.seller] = opened.palle;
        return opened;
    });

    if (result.status == DisputeStatus::done) {
        log_info("dispute opened by={} against={} palle={}", username, result.seller, result.palle);
    }
    return result;
}

ReturnResult loot_return(
    Storage &storage,
    const std::string &username,
    std::int64_t now,
    bool postage_on_seller
) {
    const ReturnResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        ReturnResult giving;
        const auto from = state.loot_from.find(username);
        const std::int64_t palle = counter(state.loot_amount, username);
        if (from == state.loot_from.end() || palle <= 0) {
            giving.status = ReturnStatus::nothing_to_return;
            return giving;
        }
        giving.victim = from->second;
        giving.palle = palle;
        giving.disputed = state.dispute_buyer.find(username) != state.dispute_buyer.end();
        if (counter(state.loot_when, username) + return_window_seconds < now) {
            giving.status = ReturnStatus::too_late;
            return giving;
        }
        /* One case in three the support refunds the buyer and lets the seller keep the palle. */
        giving.overturned =
            giving.disputed && session.random_index(support_overturns_one_in) == 0;
        if (giving.overturned) {
            state.scores[giving.victim] = counter(state.scores, giving.victim) + palle;
        } else {
            giving.postage = palle / return_postage_share;
            const std::int64_t from_seller = palle + (postage_on_seller ? giving.postage : 0);
            const std::int64_t to_buyer = palle - (postage_on_seller ? 0 : giving.postage);
            state.scores[username] = counter(state.scores, username) - from_seller;
            state.scores[giving.victim] = counter(state.scores, giving.victim) + to_buyer;
        }
        giving.score = counter(state.scores, username);
        state.loot_from.erase(username);
        state.loot_amount.erase(username);
        state.loot_when.erase(username);
        state.dispute_buyer.erase(username);
        state.dispute_amount.erase(username);
        return giving;
    });

    if (result.status == ReturnStatus::done) {
        log_info(
            "loot returned user={} to={} palle={} postage={}",
            username,
            result.victim,
            result.palle,
            result.postage
        );
    }
    return result;
}

std::optional<ReprogramResult> reprogram(Storage &storage, std::int64_t now) {
    const std::optional<ReprogramResult> result =
        storage.transaction([now](StorageSession &session) -> std::optional<ReprogramResult> {
            ConquisterState &state = session.state();
            std::vector<std::string> free_men;
            for (const Counters::value_type &entry : state.scores) {
                if (find_entry(state.reprogrammed, entry.first) == state.reprogrammed.end()) {
                    free_men.push_back(entry.first);
                }
            }
            if (free_men.empty()) {
                return std::nullopt;
            }
            ReprogramResult taken;
            taken.player = free_men[session.random_index(free_men.size())];
            state.reprogrammed[taken.player] = now;
            return taken;
        });

    if (result) {
        log_info("reprogrammed user={}", result->player);
    }
    return result;
}

bool is_reprogrammed(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        ConquisterState &state = session.state();
        return find_entry(state.reprogrammed, username) != state.reprogrammed.end();
    });
}

FreeingResult set_free(Storage &storage, std::string_view username) {
    const FreeingResult result = storage.transaction([username](StorageSession &session) {
        ConquisterState &state = session.state();
        FreeingResult freeing;
        const Counters::value_type *held = find_ignore_case(state.reprogrammed, username);
        if (held == nullptr) {
            return freeing;
        }
        freeing.known = true;
        /* The paperwork does not always go through. */
        freeing.worked = session.random_index(freeing_fails_one_in) != 0;
        if (freeing.worked) {
            state.reprogrammed.erase(held->first);
        }
        return freeing;
    });

    if (result.known) {
        log_info("freeing user={} worked={}", username, result.worked ? 1 : 0);
    }
    return result;
}

namespace {

constexpr std::array forbidden_words{"palla", "gioco", "oggi", "grazie", "niente", "tutto"};

std::int64_t number_in(std::string_view message) {
    std::int64_t found = -1;
    std::int64_t current = 0;
    bool reading = false;
    for (const char character : message) {
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            current = current * 10 + (character - '0');
            if (current > 1000000000) {
                current = 1000000000;
            }
            reading = true;
        } else if (reading) {
            found = std::max(found, current);
            current = 0;
            reading = false;
        }
    }
    return reading ? std::max(found, current) : found;
}

/* Characters as they are read, not as they are stored: an accent is one, not two. */
std::int64_t letters_in(std::string_view message) {
    return std::ranges::count_if(message, [](const char character) {
        return (static_cast<unsigned char>(character) & 0xC0U) != 0x80U;
    });
}

/* Dentro challenge ogni giocatore ha le sue caselle: "b:alice" è l'offerta di alice. */
std::string player_key(std::string_view prefix, std::string_view username) {
    return std::format("{}:{}", prefix, username);
}

std::vector<std::pair<std::string, std::int64_t>> players_with(
    const Counters &counters,
    std::string_view prefix
) {
    const std::string head = std::format("{}:", prefix);
    std::vector<std::pair<std::string, std::int64_t>> found;
    for (const Counters::value_type &entry : counters) {
        if (entry.first.starts_with(head)) {
            found.emplace_back(entry.first.substr(head.size()), entry.second);
        }
    }
    return found;
}

/* Every word of a message, lowercased, for the games that look at one word at a time. */
std::vector<std::string> words_in(std::string_view message) {
    /* L'apostrofo separa come uno spazio: in l'impiccato la parola è impiccato. */
    std::string plain{message};
    for (std::size_t at = 0; at < plain.size(); ++at) {
        if (plain.at(at) == '\'') {
            plain.at(at) = ' ';
        } else if (plain.compare(at, 3, "\u2019") == 0) {
            plain.replace(at, 3, "   ");
            at += 2;
        }
    }
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < plain.size()) {
        const std::size_t end = std::min(plain.find(' ', at), plain.size());
        std::string word{std::string_view{plain}.substr(at, end - at)};
        while (!word.empty() && std::ispunct(static_cast<unsigned char>(word.back())) != 0) {
            word.pop_back();
        }
        while (!word.empty() && std::ispunct(static_cast<unsigned char>(word.front())) != 0) {
            word.erase(word.begin());
        }
        if (!word.empty()) {
            words.push_back(text::to_lower_copy(word));
        }
        at = end + 1;
    }
    return words;
}

/* The word that calls each game, the one its announcement shouts. */
/* Letters only, lowercased: what is left of a message once the noise is gone. */
std::string bare_letters(std::string_view message) {
    std::string bare;
    for (const char character : message) {
        if (std::isalpha(static_cast<unsigned char>(character)) != 0) {
            bare.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return bare;
}

bool is_a_vowel(char letter) {
    return std::string_view{"aeiou"}.find(static_cast<char>(std::tolower(static_cast<unsigned char>(letter)))) !=
           std::string_view::npos;
}

/* The hidden word as the group sees it, with a dash where a letter is still missing. */
std::string uncovered(std::string_view word, std::int64_t mask) {
    std::string shown;
    for (std::size_t at = 0; at < word.size(); ++at) {
        shown.push_back((mask & (std::int64_t{1} << at)) != 0 ? word.at(at) : '-');
    }
    return shown;
}

/* La dimensione la conta il compilatore: una riga in meno o in più non lascia buchi. */
constexpr auto game_names = std::to_array<std::pair<std::string_view, Game>>({
    {"corsa", Game::race},
    {"indovina", Game::guess},
    {"asta", Game::auction},
    {"proibita", Game::forbidden},
    {"memoria", Game::sequence},
    {"lunga", Game::longest},
    {"silenzio", Game::silence},
    {"domanda", Game::quiz},
    {"anagramma", Game::anagram},
    {"catena", Game::chain},
    {"conta", Game::counting},
    {"identikit", Game::whois},
    {"specchio", Game::mirror},
    {"rima", Game::rhyme},
    {"mirino", Game::target},
    {"occhio", Game::closest},
    {"carta", Game::cards},
    {"calcolo", Game::maths},
    {"rovescio", Game::countdown},
    {"capitale", Game::capital},
    {"emoji", Game::emoji},
    {"acrostico", Game::acrostic},
    {"vocali", Game::novowels},
    {"palindromo", Game::palindrome},
    {"corta", Game::shortest},
    {"fiume", Game::river},
    {"somma", Game::sum},
    {"anno", Game::year},
    {"proverbio", Game::proverb},
    {"roulette", Game::roulette},
    {"impiccato", Game::hangman},
    {"colore", Game::colour},
    {"animale", Game::animal},
    {"copia", Game::copy},
    {"alfabeto", Game::alphabet},
    {"caldo", Game::hotcold},
    {"lettere", Game::letters},
    {"canzone", Game::song},
    {"città", Game::city},
    {"citta", Game::city},
    {"ricetta", Game::dish},
    {"nascosta", Game::hidden},
    {"sillaba", Game::syllable},
    {"film", Game::film},
    {"serie", Game::series},
    {"moneta", Game::coin},
    {"semaforo", Game::trafficlight},
    {"estremi", Game::ends},
    {"slot", Game::slot},
    {"cronometro", Game::stopwatch},
    {"ordine", Game::order},
    {"astacieca", Game::sealed},
    {"unico", Game::unique},
    {"media", Game::average},
    {"piramide", Game::pyramid},
    {"patata", Game::potato},
    {"sedie", Game::chairs},
    {"russa", Game::russian},
    {"scalata", Game::climb},
    {"banco", Game::bank},
    {"colletta", Game::collect},
    {"processo", Game::trial},
    {"taglia", Game::bounty},
    {"assedio", Game::siege},
    {"borsa", Game::market},
    {"scommessa", Game::wager},
    {"staffetta", Game::relay},
    {"ostaggio", Game::hostage},
    {"eredità", Game::legacy},
    {"eredita", Game::legacy},
    {"dogana", Game::customs},
    {"maratona", Game::marathon},
    {"zodiaco", Game::stars},
    {"frode", Game::fraud},
    {"schema", Game::scheme},
    {"reso", Game::refund},
    {"spia", Game::spy},
    {"congiura", Game::plot},
    {"dote", Game::dowry},
    {"tombola", Game::bingo},
    {"ippodromo", Game::horses},
    {"terremoto", Game::quake},
    {"guerra", Game::war},
    {"banca", Game::deposit},
    {"talento", Game::talent},
    {"caccia", Game::treasure},
    {"domino", Game::domino},
    {"tunnel", Game::tunnel},
    {"ribasso", Game::dutch},
    {"enigma", Game::riddle},
    {"navale", Game::navy},
    {"elezioni", Game::election},
    {"fune", Game::tug},
    {"torre", Game::jenga},
    {"telefono", Game::whispers},
    {"contrabbando", Game::smuggle},
    {"assicurazione", Game::insurance},
    {"sciopero", Game::strike},
    {"concorso", Game::contest},
    {"catasto", Game::cadastre},
    {"pellegrinaggio", Game::pilgrimage},
    {"apocalisse", Game::apocalypse},
    {"battuta", Game::whosaid},
    {"mezza", Game::halfquote},
    {"vera", Game::truequote},
});

}


std::optional<Game> game_named(std::string_view message) {
    /* A whole word, not a piece of one: chi scrive corsaro non ha chiamato la corsa. */
    for (const std::string &word : words_in(message)) {
        const auto named = std::ranges::find(game_names, word, &std::pair<std::string_view, Game>::first);
        if (named != game_names.end()) {
            return named->second;
        }
    }
    return std::nullopt;
}

namespace {

/* Quello che uno script forgiato vede del gioco vero, e quello che gli si lascia fare. */
LuaBotView forged_view(StorageSession &session, ConquisterState &state, std::int64_t pot) {
    LuaBotView view;
    view.pot = pot;
    view.score = [&state](const std::string &who) { return counter(state.scores, who); };
    view.players = [&state]() {
        std::vector<std::string> names;
        for (const Counters::value_type &entry : state.scores) {
            if (names.size() >= 40) {
                break;
            }
            names.push_back(entry.first);
        }
        return names;
    };
    view.random = [&session](std::int64_t count) {
        return static_cast<std::int64_t>(session.random_index(static_cast<std::size_t>(std::max(count, std::int64_t{1}))));
    };
    view.get = [&state](const std::string &key) { return counter(state.challenge, "f:" + key); };
    view.set = [&state](const std::string &key, std::int64_t value) { state.challenge["f:" + key] = value; };
    view.name = [&state](const std::string &key) {
        const auto found = state.challenge_who.find("f:" + key);
        return found == state.challenge_who.end() ? std::string{} : found->second;
    };
    view.setname = [&state](const std::string &key, const std::string &value) {
        state.challenge_who["f:" + key] = value;
    };
    return view;
}

/* Com'è andata, scritta nel registro della forgia quando la partita finisce. */
void record_forged(const ConquisterState &state, const std::string &keyword, bool decided, std::int64_t now) {
    ForgeVerdict verdict;
    verdict.keyword = keyword;
    verdict.family = forge_family_of(keyword);
    verdict.messages = counter(state.challenge, "fx:msgs");
    verdict.palle = counter(state.challenge, "fx:palle");
    verdict.decided = decided;
    verdict.at = now;
    const std::int64_t opened = counter(state.challenge, "fx:open");
    const std::int64_t first = counter(state.challenge, "fx:first");
    verdict.first_move = first > 0 && opened > 0 ? first - opened : 0;
    verdict.players = std::ranges::count_if(state.challenge, [](const Counters::value_type &entry) {
        return entry.first.starts_with("fx:p:");
    });
    forge_record(verdict);
}

/* Quello che lo script ha combinato, tradotto in palle e in una riga da scrivere in chat. */
struct ForgedOutcome {
    bool ok = false;
    bool decided = false;
    std::string text;
    std::int64_t palle = 0;
};

ForgedOutcome run_forged(
    StorageSession &session,
    ConquisterState &state,
    const std::string &keyword,
    std::string_view moment,
    const std::vector<LuaArg> &args
) {
    ForgedOutcome outcome;
    const std::optional<std::string> source = forge_source(keyword);
    if (!source) {
        return outcome;
    }
    const std::int64_t pot = counter(state.challenge, "pot");
    const LuaBotView view = forged_view(session, state, pot);
    const LuaRun run = lua_invoke(*source, moment, view, args, forge_limits());
    if (!run.ok) {
        forge_condemn(keyword, run.error);
        return outcome;
    }
    outcome.ok = true;
    for (const LuaEffect &effect : run.effects) {
        if (effect.kind == LuaEffect::Kind::pay || effect.kind == LuaEffect::Kind::take) {
            state.challenge["fx:palle"] = counter(state.challenge, "fx:palle") + effect.palle;
        }
        switch (effect.kind) {
        case LuaEffect::Kind::pay:
            state.scores[effect.who] = counter(state.scores, effect.who) + effect.palle;
            outcome.palle += effect.palle;
            break;
        case LuaEffect::Kind::take:
            state.scores[effect.who] = counter(state.scores, effect.who) - effect.palle;
            outcome.palle += effect.palle;
            break;
        case LuaEffect::Kind::say:
            outcome.text.append(outcome.text.empty() ? "" : " · ").append(effect.text);
            break;
        case LuaEffect::Kind::close:
            outcome.decided = true;
            break;
        }
    }
    return outcome;
}

}

std::optional<GameOpened> game_open(
    Storage &storage,
    std::int64_t now,
    std::int64_t open_for,
    std::int64_t pot,
    std::optional<Game> wanted,
    const std::string &forged_keyword
) {
    const std::optional<GameOpened> result =
        storage.transaction([&](StorageSession &session) -> std::optional<GameOpened> {
            ConquisterState &state = session.state();
            if (counter(state.challenge, "closes") > now) {
                return std::nullopt;
            }
            GameOpened opened;
            if (wanted) {
                opened.kind = *wanted;
                opened.target = *wanted == Game::forged ? forged_keyword : std::string{};
            } else {
                /* Prima una monetina fra scritti a mano e forgiati, così i 103 non affogano. */
                const std::optional<std::string> mine = session.random_index(2) == 0
                    ? forge_any([&session](std::int64_t count) {
                          return static_cast<std::int64_t>(
                              session.random_index(static_cast<std::size_t>(std::max(count, std::int64_t{1})))
                          );
                      })
                    : std::nullopt;
                if (mine) {
                    opened.kind = Game::forged;
                    opened.target = *mine;
                    state.challenge_who["forged"] = *mine;
                } else {
                    opened.kind = static_cast<Game>(session.random_index(103));
                }
            }
            opened.closes = now + open_for;
            opened.pot = pot;
            switch (opened.kind) {
            case Game::guess:
                opened.secret = 1 + static_cast<std::int64_t>(session.random_index(100));
                break;
            case Game::forbidden:
                opened.secret = static_cast<std::int64_t>(session.random_index(forbidden_words.size()));
                break;
            case Game::sequence:
                opened.secret = 100 + static_cast<std::int64_t>(session.random_index(900));
                break;
            case Game::quiz:
                opened.secret = static_cast<std::int64_t>(session.random_index(questions.size()));
                break;
            case Game::anagram:
                opened.secret = static_cast<std::int64_t>(session.random_index(anagrams.size()));
                break;
            case Game::chain:
                /* The letter everybody has to start from, drawn among the easy ones. */
                opened.secret = static_cast<std::int64_t>(static_cast<unsigned char>(
                    std::string_view{"abcdelmnoprst"}.at(session.random_index(13))
                ));
                state.challenge["letter"] = opened.secret;
                break;
            case Game::counting:
                opened.secret = 1;
                state.challenge["count"] = 0;
                break;
            case Game::whois: {
                if (state.scores.empty()) {
                    return std::nullopt;
                }
                const std::size_t which = session.random_index(state.scores.size());
                opened.target =
                    std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(which))->first;
                state.challenge_who["target"] = opened.target;
                break;
            }
            case Game::mirror:
                opened.secret = static_cast<std::int64_t>(session.random_index(mirrors.size()));
                break;
            case Game::rhyme:
                opened.secret = static_cast<std::int64_t>(session.random_index(rhymes.size()));
                break;
            case Game::target:
                /* Short enough to be written on purpose, long enough to have to count. */
                opened.secret = 20 + static_cast<std::int64_t>(session.random_index(40));
                break;
            case Game::closest:
                opened.secret = 1 + static_cast<std::int64_t>(session.random_index(1000));
                break;
            case Game::maths: {
                const std::int64_t first = 11 + static_cast<std::int64_t>(session.random_index(89));
                const std::int64_t second = 11 + static_cast<std::int64_t>(session.random_index(89));
                opened.target = std::format("{} + {}", first, second);
                opened.secret = first + second;
                break;
            }
            case Game::countdown:
                opened.secret = 20;
                state.challenge["count"] = 21;
                break;
            case Game::capital:
                opened.secret = static_cast<std::int64_t>(session.random_index(capitals.size()));
                break;
            case Game::emoji:
                opened.secret = static_cast<std::int64_t>(session.random_index(emojis.size()));
                break;
            case Game::acrostic:
                opened.secret = static_cast<std::int64_t>(static_cast<unsigned char>(
                    std::string_view{"bcdfglmprstv"}.at(session.random_index(12))
                ));
                break;
            case Game::palindrome:
            case Game::novowels:
                break;
            case Game::sum:
                opened.secret = 30 + static_cast<std::int64_t>(session.random_index(31));
                state.challenge["count"] = 0;
                break;
            case Game::year:
                opened.secret = static_cast<std::int64_t>(session.random_index(years.size()));
                break;
            case Game::proverb:
                opened.secret = static_cast<std::int64_t>(session.random_index(proverbs.size()));
                break;
            case Game::hangman:
                opened.secret = static_cast<std::int64_t>(session.random_index(mirrors.size()));
                state.challenge["mask"] = 0;
                break;
            case Game::colour:
                opened.secret = static_cast<std::int64_t>(session.random_index(colours.size()));
                break;
            case Game::animal:
                opened.secret = static_cast<std::int64_t>(session.random_index(animals.size()));
                break;
            case Game::copy: {
                /* Six characters nobody writes by accident. */
                constexpr std::string_view alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
                for (int at = 0; at < 6; ++at) {
                    opened.target.push_back(alphabet.at(session.random_index(alphabet.size())));
                }
                state.challenge_who["target"] = opened.target;
                break;
            }
            case Game::alphabet:
                opened.secret = static_cast<std::int64_t>('a');
                state.challenge["letter"] = opened.secret;
                break;
            case Game::hotcold:
                opened.secret = 1 + static_cast<std::int64_t>(session.random_index(100));
                break;
            case Game::letters:
                opened.secret = static_cast<std::int64_t>(session.random_index(triples.size()));
                break;
            case Game::song:
                opened.secret = static_cast<std::int64_t>(session.random_index(songs.size()));
                break;
            case Game::city:
                /* A letter with more than one city behind it. */
                opened.secret = static_cast<std::int64_t>(static_cast<unsigned char>(
                    std::string_view{"bcfgmnprtv"}.at(session.random_index(10))
                ));
                break;
            case Game::dish:
                opened.secret = static_cast<std::int64_t>(session.random_index(dishes.size()));
                break;
            case Game::hidden: {
                opened.secret = static_cast<std::int64_t>(session.random_index(mirrors.size()));
                const std::string_view word = mirrors.at(static_cast<std::size_t>(opened.secret));
                constexpr std::string_view noise = "bcdfghlmnpqrstvz";
                for (int at = 0; at < 4; ++at) {
                    opened.target.push_back(noise.at(session.random_index(noise.size())));
                }
                opened.target.append(word);
                for (int at = 0; at < 4; ++at) {
                    opened.target.push_back(noise.at(session.random_index(noise.size())));
                }
                break;
            }
            case Game::syllable:
                opened.secret = static_cast<std::int64_t>(session.random_index(syllables.size()));
                break;
            case Game::film:
                opened.secret = static_cast<std::int64_t>(session.random_index(films.size()));
                break;
            case Game::series: {
                const std::int64_t start = 1 + static_cast<std::int64_t>(session.random_index(9));
                const std::int64_t step = 2 + static_cast<std::int64_t>(session.random_index(8));
                opened.target = std::format(
                    "{}, {}, {}, {}",
                    start,
                    start + step,
                    start + (2 * step),
                    start + (3 * step)
                );
                opened.secret = start + (4 * step);
                break;
            }
            case Game::trafficlight:
                /* Green pays, red charges, and only the bot knows which one it is. */
                opened.secret = static_cast<std::int64_t>(session.random_index(2));
                break;
            case Game::ends: {
                constexpr std::string_view heads = "bcfmprstv";
                constexpr std::string_view tails = "aeiono";
                opened.secret = static_cast<std::int64_t>(static_cast<unsigned char>(
                    heads.at(session.random_index(heads.size()))
                ));
                state.challenge["letter"] = opened.secret;
                state.challenge["last"] = static_cast<std::int64_t>(static_cast<unsigned char>(
                    tails.at(session.random_index(tails.size()))
                ));
                opened.target = std::format(
                    "{}...{}",
                    static_cast<char>(opened.secret),
                    static_cast<char>(counter(state.challenge, "last"))
                );
                break;
            }
            case Game::stopwatch:
                /* Thirty seconds from now, give or take two. */
                opened.secret = now + 30;
                break;
            case Game::order: {
                std::vector<std::string> three;
                while (three.size() < 3) {
                    const std::string word{mirrors.at(session.random_index(mirrors.size()))};
                    if (std::ranges::find(three, word) == three.end()) {
                        three.push_back(word);
                    }
                }
                opened.target = std::format("{}, {}, {}", three.at(0), three.at(1), three.at(2));
                std::ranges::sort(three);
                state.challenge_who["target"] =
                    std::format("{} {} {}", three.at(0), three.at(1), three.at(2));
                break;
            }
            case Game::pyramid:
                /* Il tetto è segreto: chi lo supera ha alzato troppo. */
                opened.secret = 20 + static_cast<std::int64_t>(session.random_index(61));
                break;
            case Game::potato:
                /* La miccia dura fra quaranta secondi e due minuti. */
                state.challenge["fuse"] = now + 40 + static_cast<std::int64_t>(session.random_index(81));
                break;
            case Game::chairs:
                state.challenge["round"] = 1;
                state.challenge["deadline"] = now + 25;
                break;
            case Game::collect:
                opened.secret = 300 + static_cast<std::int64_t>(session.random_index(601));
                state.challenge["count"] = 0;
                break;
            case Game::trial:
            case Game::bounty:
            case Game::hostage: {
                /* Serve qualcuno su cui puntare il dito. */
                if (state.scores.empty()) {
                    return std::nullopt;
                }
                const std::size_t which = session.random_index(state.scores.size());
                opened.target =
                    std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(which))->first;
                state.challenge_who["target"] = opened.target;
                if (opened.kind == Game::hostage) {
                    opened.secret = 200 + static_cast<std::int64_t>(session.random_index(801));
                    state.challenge["count"] = 0;
                }
                if (opened.kind == Game::bounty) {
                    opened.secret = 500 + static_cast<std::int64_t>(session.random_index(1501));
                }
                break;
            }
            case Game::siege: {
                if (!state.current) {
                    return std::nullopt;
                }
                opened.target = state.current->username;
                state.challenge_who["target"] = opened.target;
                /* Quanti colpi servono per buttarlo giù. */
                opened.secret = 5 + static_cast<std::int64_t>(session.random_index(6));
                break;
            }
            case Game::market:
                opened.secret = 100;
                state.challenge["price"] = 100;
                break;
            case Game::wager:
                break;
            case Game::relay:
                state.challenge["deadline"] = now + 15;
                state.challenge["count"] = 0;
                break;
            case Game::legacy:
                opened.secret = 1000 + static_cast<std::int64_t>(session.random_index(4001));
                break;
            case Game::marathon:
                opened.secret = 60 + static_cast<std::int64_t>(session.random_index(141));
                state.challenge["count"] = 0;
                break;
            case Game::race:
            case Game::auction:
            case Game::longest:
            case Game::silence:
            case Game::cards:
            case Game::roulette:
            case Game::shortest:
            case Game::river:
            case Game::coin:
            case Game::slot:
            case Game::sealed:
            case Game::unique:
            case Game::average:
            case Game::russian:
            case Game::climb:
            case Game::bank:
            case Game::customs:
                break;
            case Game::stars:
                /* La casa del giorno decide chi è in favore. */
                opened.secret = static_cast<std::int64_t>(zodiac::element_of_day(now));
                break;
            case Game::fraud:
                break;
            case Game::scheme:
                state.challenge["count"] = 0;
                break;
            case Game::refund:
            case Game::spy: {
                if (state.scores.empty()) {
                    return std::nullopt;
                }
                const std::size_t which = session.random_index(state.scores.size());
                const std::string &chosen =
                    std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(which))->first;
                state.challenge_who["target"] = chosen;
                /* Nel gioco della spia il nome non si dice: lo sa solo il bot. */
                opened.target = opened.kind == Game::refund ? chosen : std::string{};
                break;
            }
            case Game::plot:
            case Game::dowry:
                break;
            case Game::bingo:
                state.challenge["deadline"] = now + 20;
                state.challenge["drawn"] = 0;
                break;
            case Game::horses:
                state.challenge["deadline"] = now + 15;
                state.challenge["h1"] = 0;
                state.challenge["h2"] = 0;
                state.challenge["h3"] = 0;
                state.challenge["h4"] = 0;
                break;
            case Game::quake:
                state.challenge["deadline"] = now + 30;
                break;
            case Game::war:
                state.challenge["w1"] = 0;
                state.challenge["w2"] = 0;
                break;
            case Game::deposit:
            case Game::talent:
            case Game::election:
                break;
            case Game::treasure:
            case Game::riddle:
                opened.secret = static_cast<std::int64_t>(session.random_index(mirrors.size()));
                if (opened.kind == Game::riddle) {
                    state.challenge["deadline"] = now + 40;
                    state.challenge["clues"] = 1;
                }
                break;
            case Game::domino:
                opened.secret = static_cast<std::int64_t>(session.random_index(10));
                state.challenge["letter"] = opened.secret;
                break;
            case Game::tunnel:
                state.challenge["bid"] = 0;
                state.challenge["count"] = 0;
                break;
            case Game::dutch:
                opened.secret = pot;
                state.challenge["price"] = pot;
                state.challenge["deadline"] = now + 20;
                break;
            case Game::navy:
                opened.secret = 1 + static_cast<std::int64_t>(session.random_index(25));
                break;
            case Game::tug:
                state.challenge["rope"] = 0;
                break;
            case Game::jenga:
            case Game::smuggle:
            case Game::insurance:
            case Game::cadastre:
                break;
            case Game::whispers: {
                const std::string_view first = mirrors.at(session.random_index(mirrors.size()));
                opened.target = first;
                state.challenge_who["word"] = opened.target;
                state.challenge["count"] = 0;
                break;
            }
            case Game::strike:
                state.challenge["deadline"] = now + 20;
                state.challenge["count"] = 0;
                break;
            case Game::contest:
                opened.secret = static_cast<std::int64_t>(session.random_index(questions.size()));
                state.challenge["deadline"] = now + 45;
                state.challenge["round"] = 1;
                break;
            case Game::pilgrimage:
                opened.secret = 80 + static_cast<std::int64_t>(session.random_index(121));
                state.challenge["count"] = 0;
                state.challenge["deadline"] = now + 20;
                break;
            case Game::apocalypse:
                state.challenge["deadline"] = now + 25;
                break;
            case Game::forged:
                break;
            case Game::whosaid:
            case Game::truequote: {
                /* Serve una citazione di cui si sappia chi l'ha detta. */
                const Authors &authors = state.quote_authors;
                if (authors.empty()) {
                    return std::nullopt;
                }
                const auto written =
                    std::next(authors.begin(), static_cast<std::ptrdiff_t>(session.random_index(authors.size())));
                opened.target = written->first;
                state.challenge_who["target"] = written->second;
                if (opened.kind == Game::truequote) {
                    /* Metà delle volte l'autore che mostro è quello sbagliato. */
                    const bool honest = session.random_index(2) == 0;
                    std::string shown = written->second;
                    if (!honest && !state.scores.empty()) {
                        const auto other = std::next(
                            state.scores.begin(),
                            static_cast<std::ptrdiff_t>(session.random_index(state.scores.size()))
                        );
                        shown = other->first;
                    }
                    state.challenge_who["shown"] = shown;
                    opened.detail = shown;
                    opened.secret = shown == written->second ? 1 : 0;
                }
                break;
            }
            case Game::halfquote: {
                const Quotes &quotes = session.quotes();
                if (quotes.empty()) {
                    return std::nullopt;
                }
                /* Non tutte le citazioni si prestano: qualcuna è troppo corta per tagliarla a metà. */
                for (int look = 0; look < 10 && opened.target.empty(); ++look) {
                    const std::vector<std::string> words =
                        words_in(quotes.at(session.random_index(quotes.size())));
                    if (words.size() < 6 || words.back().size() < 4) {
                        continue;
                    }
                    const std::size_t half = words.size() / 2;
                    for (std::size_t at = 0; at < half; ++at) {
                        opened.target.append(at == 0 ? "" : " ").append(words.at(at));
                    }
                    state.challenge_who["target"] = words.back();
                }
                if (opened.target.empty()) {
                    return std::nullopt;
                }
                break;
            }
            }
            if (opened.kind == Game::forged && opened.target.empty()) {
                return std::nullopt;
            }
            state.challenge["kind"] = static_cast<std::int64_t>(opened.kind);
            state.challenge["closes"] = opened.closes;
            state.challenge["secret"] = opened.secret;
            state.challenge["pot"] = opened.pot;
            /* Where the best is the smallest, the bid starts out of reach and comes down. */
            state.challenge["bid"] =
                (opened.kind == Game::closest || opened.kind == Game::shortest) ? 1000000 : 0;
            state.challenge_who.erase("leader");
            if (opened.kind == Game::forged) {
                /* Lo stato del gioco precedente non deve restare fra i piedi di questo. */
                std::vector<std::string> leftovers;
                for (const Counters::value_type &entry : state.challenge) {
                    if (entry.first.starts_with("f:")) {
                        leftovers.push_back(entry.first);
                    }
                }
                for (const Authors::value_type &entry : state.challenge_who) {
                    if (entry.first.starts_with("f:")) {
                        leftovers.push_back(entry.first);
                    }
                }
                for (const std::string &key : leftovers) {
                    state.challenge.erase(key);
                    state.challenge_who.erase(key);
                }
                state.challenge["pot"] = pot;
                state.challenge["deadline"] = now + 25;
                state.challenge["fx:open"] = now;
                state.challenge_who["forged"] = opened.target;
                const ForgedOutcome born = run_forged(session, state, opened.target, "open", {});
                if (!born.ok) {
                    state.challenge.clear();
                    state.challenge_who.clear();
                    return std::nullopt;
                }
                opened.detail = born.text;
            }
            return opened;
        });

    if (result) {
        log_info("game opened kind={} closes={}", static_cast<int>(result->kind), result->closes);
    }
    return result;
}

std::optional<GamePlayed> game_play(
    Storage &storage,
    const std::string &username,
    std::string_view message,
    std::int64_t now
) {
    return storage.transaction([&](StorageSession &session) -> std::optional<GamePlayed> {
        ConquisterState &state = session.state();
        if (counter(state.challenge, "closes") <= now) {
            return std::nullopt;
        }
        GamePlayed played;
        played.kind = static_cast<Game>(counter(state.challenge, "kind"));
        const std::int64_t secret = counter(state.challenge, "secret");
        switch (played.kind) {
        case Game::race:
            /* First to say anything takes it. */
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        case Game::guess: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            played.number = said;
            if (said != secret) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::auction: {
            const std::int64_t said = number_in(message);
            if (said <= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            played.number = said;
            played.player = username;
            state.challenge["bid"] = said;
            state.challenge_who["leader"] = username;
            break;
        }
        case Game::sequence: {
            if (number_in(message) != secret) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = secret;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            state.challenge["closes"] = 0;
            break;
        }
        case Game::longest: {
            std::size_t longest = 0;
            std::size_t at = 0;
            while (at < message.size()) {
                const std::size_t end = std::min(message.find(' ', at), message.size());
                longest = std::max(longest, end - at);
                at = end + 1;
            }
            if (static_cast<std::int64_t>(longest) <= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            played.player = username;
            played.number = static_cast<std::int64_t>(longest);
            state.challenge["bid"] = played.number;
            state.challenge_who["leader"] = username;
            break;
        }
        case Game::silence:
            /* Broken, but the closing round still has to say so. */
            played.decided = false;
            played.player = username;
            played.palle = counter(state.scores, username) / 10;
            state.scores[username] = counter(state.scores, username) - played.palle;
            state.challenge_who["leader"] = username;
            break;
        case Game::quiz: {
            if (!text::contains_ignore_case(message, questions.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::anagram: {
            if (!text::contains_ignore_case(message, anagrams.at(static_cast<std::size_t>(secret)))) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::chain: {
            const std::string_view trimmed = text::trim(message);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            const std::int64_t wanted = counter(state.challenge, "letter");
            const std::int64_t first = std::tolower(static_cast<unsigned char>(trimmed.front()));
            played.player = username;
            played.number = wanted;
            if (first != wanted) {
                played.decided = true;
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            /* The next word has to start where this one ended. */
            const std::size_t end = std::min(trimmed.find(' '), trimmed.size());
            state.challenge["letter"] =
                std::tolower(static_cast<unsigned char>(trimmed.at(end - 1)));
            state.challenge["bid"] = counter(state.challenge, "bid") + 1;
            played.number = counter(state.challenge, "letter");
            break;
        }
        case Game::counting: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            const std::int64_t wanted = counter(state.challenge, "count") + 1;
            played.player = username;
            played.number = wanted;
            if (said != wanted) {
                played.decided = true;
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            state.challenge["count"] = wanted;
            if (wanted >= 20) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::whois: {
            const auto target = state.challenge_who.find("target");
            if (target == state.challenge_who.end() || !text::contains_ignore_case(message, target->second)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::mirror: {
            std::string backwards{mirrors.at(static_cast<std::size_t>(secret))};
            std::ranges::reverse(backwards);
            if (!text::contains_ignore_case(message, backwards)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::rhyme: {
            const std::string_view asked = rhymes.at(static_cast<std::size_t>(secret));
            const std::string_view ending = asked.substr(asked.size() - 3);
            const std::vector<std::string> words = words_in(message);
            const bool rhymed = std::ranges::any_of(words, [&](const std::string &word) {
                return word.size() > 3 && word != asked && word.ends_with(ending);
            });
            if (!rhymed) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::target: {
            const std::int64_t letters = letters_in(text::trim(message));
            if (letters != secret) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = letters;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::closest: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            const std::int64_t distance = said > secret ? said - secret : secret - said;
            if (distance >= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            played.player = username;
            played.number = said;
            state.challenge["bid"] = distance;
            state.challenge_who["leader"] = username;
            break;
        }
        case Game::cards: {
            /* One card each: the deck remembers who has already had his. */
            const std::string drawn = "drew:" + username;
            if (state.challenge.find(drawn) != state.challenge.end()) {
                return std::nullopt;
            }
            const std::int64_t card = 1 + static_cast<std::int64_t>(session.random_index(10));
            state.challenge[drawn] = card;
            played.player = username;
            played.number = card;
            if (card > counter(state.challenge, "bid")) {
                state.challenge["bid"] = card;
                state.challenge_who["leader"] = username;
            }
            break;
        }
        case Game::maths: {
            if (number_in(message) != secret) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = secret;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::countdown: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            const std::int64_t wanted = counter(state.challenge, "count") - 1;
            played.player = username;
            played.number = wanted;
            if (said != wanted) {
                played.decided = true;
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            state.challenge["count"] = wanted;
            if (wanted <= 1) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::capital: {
            if (!text::contains_ignore_case(message, capitals.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::emoji: {
            if (!text::contains_ignore_case(message, emojis.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::acrostic: {
            const std::vector<std::string> words = words_in(message);
            const bool all_of_them = words.size() >= 4 && std::ranges::all_of(words, [&](const std::string &word) {
                return !word.empty() && static_cast<std::int64_t>(word.front()) == secret;
            });
            if (!all_of_them) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = static_cast<std::int64_t>(words.size());
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::novowels: {
            const std::string bare = bare_letters(message);
            if (bare.size() < 10 || std::ranges::any_of(bare, is_a_vowel)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = static_cast<std::int64_t>(bare.size());
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::palindrome: {
            const std::string bare = bare_letters(message);
            if (bare.size() < 5 || !std::ranges::equal(bare, bare | std::views::reverse)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = bare;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::shortest: {
            const std::vector<std::string> words = words_in(message);
            std::size_t shortest = 0;
            for (const std::string &word : words) {
                if (word.size() >= 2 && (shortest == 0 || word.size() < shortest)) {
                    shortest = word.size();
                }
            }
            if (shortest == 0 || static_cast<std::int64_t>(shortest) >= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            played.player = username;
            played.number = static_cast<std::int64_t>(shortest);
            state.challenge["bid"] = played.number;
            state.challenge_who["leader"] = username;
            break;
        }
        case Game::river: {
            const std::int64_t howmany = static_cast<std::int64_t>(words_in(message).size());
            if (howmany <= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            played.player = username;
            played.number = howmany;
            state.challenge["bid"] = howmany;
            state.challenge_who["leader"] = username;
            break;
        }
        case Game::sum: {
            const std::int64_t said = number_in(message);
            if (said <= 0) {
                return std::nullopt;
            }
            const std::int64_t total = counter(state.challenge, "count") + said;
            played.player = username;
            played.number = total;
            if (total > secret) {
                played.decided = true;
                played.detail = "sforato";
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            state.challenge["count"] = total;
            if (total == secret) {
                played.decided = true;
                played.detail = "centrato";
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::year: {
            const std::string_view answer = years.at(static_cast<std::size_t>(secret)).answer;
            if (!text::contains_ignore_case(message, answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::proverb: {
            if (!text::contains_ignore_case(message, proverbs.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::roulette: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 10) {
                return std::nullopt;
            }
            const std::int64_t drawn = 1 + static_cast<std::int64_t>(session.random_index(10));
            played.decided = true;
            played.player = username;
            played.number = drawn;
            if (said == drawn) {
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::hangman: {
            const std::string_view word = mirrors.at(static_cast<std::size_t>(secret));
            if (text::contains_ignore_case(message, word)) {
                played.decided = true;
                played.player = username;
                played.detail = word;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            const std::string bare = bare_letters(message);
            if (bare.size() != 1) {
                return std::nullopt;
            }
            std::int64_t mask = counter(state.challenge, "mask");
            bool anything = false;
            for (std::size_t at = 0; at < word.size(); ++at) {
                if (word.at(at) == bare.front() && (mask & (std::int64_t{1} << at)) == 0) {
                    mask |= std::int64_t{1} << at;
                    anything = true;
                }
            }
            if (!anything) {
                return std::nullopt;
            }
            state.challenge["mask"] = mask;
            played.player = username;
            played.detail = uncovered(word, mask);
            break;
        }
        case Game::colour: {
            if (!text::contains_ignore_case(message, colours.at(static_cast<std::size_t>(secret)))) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::animal: {
            if (!text::contains_ignore_case(message, animals.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::copy: {
            const auto target = state.challenge_who.find("target");
            if (target == state.challenge_who.end() || message.find(target->second) == std::string_view::npos) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = target->second;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::alphabet: {
            const std::vector<std::string> words = words_in(message);
            if (words.empty()) {
                return std::nullopt;
            }
            const std::int64_t wanted = counter(state.challenge, "letter");
            played.player = username;
            played.number = wanted;
            if (static_cast<std::int64_t>(words.front().front()) != wanted) {
                played.decided = true;
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            if (wanted >= static_cast<std::int64_t>('j')) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            state.challenge["letter"] = wanted + 1;
            played.number = wanted + 1;
            break;
        }
        case Game::hotcold: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 100) {
                return std::nullopt;
            }
            played.player = username;
            played.number = said;
            if (said == secret) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            const std::int64_t distance = said > secret ? said - secret : secret - said;
            played.detail = distance <= 3 ? "fuochissimo" : (distance <= 10 ? "caldo" : (distance <= 25 ? "tiepido" : "gelo"));
            break;
        }
        case Game::letters: {
            const std::string_view wanted = triples.at(static_cast<std::size_t>(secret));
            const std::vector<std::string> words = words_in(message);
            const auto has_them_all = [&wanted](const std::string &word) {
                return word.size() >= 4 && std::ranges::all_of(wanted, [&word](const char letter) {
                    return word.find(letter) != std::string::npos;
                });
            };
            const auto found = std::ranges::find_if(words, has_them_all);
            if (found == words.end()) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = *found;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::song: {
            if (!text::contains_ignore_case(message, songs.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::city: {
            const std::vector<std::string> words = words_in(message);
            const auto named = std::ranges::find_if(words, [&](const std::string &word) {
                return !word.empty() && static_cast<std::int64_t>(word.front()) == secret &&
                       std::ranges::find(cities, word) != cities.end();
            });
            if (named == words.end()) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = *named;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::dish: {
            if (!text::contains_ignore_case(message, dishes.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::hidden: {
            if (!text::contains_ignore_case(message, mirrors.at(static_cast<std::size_t>(secret)))) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = mirrors.at(static_cast<std::size_t>(secret));
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::syllable: {
            const std::string_view piece = syllables.at(static_cast<std::size_t>(secret));
            const std::vector<std::string> words = words_in(message);
            const auto found = std::ranges::find_if(words, [&piece](const std::string &word) {
                return word.size() >= 5 && word.find(piece) != std::string::npos;
            });
            if (found == words.end()) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = *found;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::film: {
            if (!text::contains_ignore_case(message, films.at(static_cast<std::size_t>(secret)).answer)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::series: {
            if (number_in(message) != secret) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = secret;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::coin: {
            const bool heads = text::contains_ignore_case(message, "testa");
            const bool tails = text::contains_ignore_case(message, "croce");
            if (heads == tails) {
                return std::nullopt;
            }
            const bool drawn = session.random_index(2) == 0;
            played.decided = true;
            played.player = username;
            played.detail = drawn ? "testa" : "croce";
            if (drawn == heads) {
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::trafficlight: {
            played.decided = true;
            played.player = username;
            played.number = secret;
            if (secret == 1) {
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            played.palle = counter(state.scores, username) / 10;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        case Game::ends: {
            const auto first = static_cast<char>(counter(state.challenge, "letter"));
            const auto last = static_cast<char>(counter(state.challenge, "last"));
            const std::vector<std::string> words = words_in(message);
            const auto found = std::ranges::find_if(words, [first, last](const std::string &word) {
                return word.size() >= 4 && word.front() == first && word.back() == last;
            });
            if (found == words.end()) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = *found;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::slot: {
            /* One pull each, like the cards. */
            const std::string pulled = "drew:" + username;
            if (state.challenge.find(pulled) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[pulled] = 1;
            const std::int64_t first = 1 + static_cast<std::int64_t>(session.random_index(5));
            const std::int64_t second = 1 + static_cast<std::int64_t>(session.random_index(5));
            const std::int64_t third = 1 + static_cast<std::int64_t>(session.random_index(5));
            played.player = username;
            played.detail = std::format("{} {} {}", first, second, third);
            if (first == second && second == third) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            if (first == second || second == third || first == third) {
                played.palle = counter(state.challenge, "pot") / 5;
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::stopwatch: {
            const std::int64_t late = now > secret ? now - secret : secret - now;
            if (late > 2) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.number = late;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::order: {
            const auto target = state.challenge_who.find("target");
            if (target == state.challenge_who.end()) {
                return std::nullopt;
            }
            const std::vector<std::string> wanted = words_in(target->second);
            const std::string lowered = text::to_lower_copy(message);
            std::size_t at = 0;
            const bool in_order = std::ranges::all_of(wanted, [&](const std::string &word) {
                const std::size_t found = lowered.find(word, at);
                if (found == std::string::npos) {
                    return false;
                }
                at = found + word.size();
                return true;
            });
            if (!in_order) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::sealed: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            break;
        }
        case Game::unique: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 50) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            break;
        }
        case Game::average: {
            const std::int64_t said = number_in(message);
            if (said < 0 || said > 100) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            break;
        }
        case Game::pyramid: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            break;
        }
        case Game::potato: {
            /* Chi parla si ritrova la patata in mano. */
            const auto holder = state.challenge_who.find("leader");
            if (holder != state.challenge_who.end() && holder->second == username) {
                return std::nullopt;
            }
            state.challenge_who["leader"] = username;
            played.player = username;
            played.number = counter(state.challenge, "fuse") - now;
            break;
        }
        case Game::chairs: {
            const std::string seat = player_key("in", username);
            const bool first_time = state.challenge.find(seat) == state.challenge.end();
            state.challenge[seat] = 1;
            const std::string seen = player_key("n", username);
            state.challenge[seen] = counter(state.challenge, seen) + 1;
            if (!first_time) {
                return std::nullopt;
            }
            played.player = username;
            played.number = counter(state.challenge, "round");
            break;
        }
        case Game::russian: {
            if (state.challenge.find(player_key("out", username)) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[player_key("in", username)] = 1;
            if (session.random_index(6) != 0) {
                played.player = username;
                played.detail = "click";
                break;
            }
            state.challenge[player_key("out", username)] = 1;
            state.challenge.erase(player_key("in", username));
            played.player = username;
            played.detail = "bang";
            played.palle = counter(state.scores, username) / 10;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        case Game::climb: {
            if (state.challenge.find(player_key("out", username)) != state.challenge.end()) {
                return std::nullopt;
            }
            const std::string stake = player_key("s", username);
            if (state.challenge.find(stake) == state.challenge.end()) {
                /* Si entra mettendo cento palle sul tavolo. */
                state.challenge[stake] = 100;
                state.scores[username] = counter(state.scores, username) - 100;
                played.player = username;
                played.number = 100;
                played.detail = "dentro";
                break;
            }
            if (session.random_index(4) == 0) {
                state.challenge[player_key("out", username)] = 1;
                played.player = username;
                played.palle = counter(state.challenge, stake);
                state.challenge.erase(stake);
                played.detail = "caduto";
                break;
            }
            state.challenge[stake] = counter(state.challenge, stake) * 3 / 2;
            played.player = username;
            played.number = counter(state.challenge, stake);
            played.detail = "sale";
            break;
        }
        case Game::bank: {
            if (state.challenge.find(player_key("out", username)) != state.challenge.end()) {
                return std::nullopt;
            }
            const std::string hand = player_key("s", username);
            const std::int64_t card = 1 + static_cast<std::int64_t>(session.random_index(11));
            const std::int64_t total = counter(state.challenge, hand) + card;
            state.challenge[hand] = total;
            played.player = username;
            played.number = card;
            if (total > 21) {
                state.challenge[player_key("out", username)] = 1;
                state.challenge.erase(hand);
                played.detail = "sballato";
                break;
            }
            played.palle = total;
            played.detail = "carta";
            break;
        }
        case Game::collect: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            state.scores[username] = counter(state.scores, username) - said;
            const std::int64_t total = counter(state.challenge, "count") + said;
            state.challenge["count"] = total;
            played.player = username;
            played.number = total;
            played.palle = said;
            break;
        }
        case Game::trial: {
            const auto accused = state.challenge_who.find("target");
            if (accused == state.challenge_who.end() || accused->second == username) {
                return std::nullopt;
            }
            const bool guilty = text::contains_ignore_case(message, "colpevole");
            const bool clear = text::contains_ignore_case(message, "innocente");
            if (guilty == clear) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = guilty ? 1 : 0;
            played.player = username;
            played.detail = guilty ? "colpevole" : "innocente";
            played.number = static_cast<std::int64_t>(players_with(state.challenge, "v").size());
            break;
        }
        case Game::bounty: {
            const auto hunted = state.challenge_who.find("target");
            if (hunted == state.challenge_who.end()) {
                return std::nullopt;
            }
            if (username == hunted->second) {
                if (!text::contains_ignore_case(message, "pago")) {
                    return std::nullopt;
                }
                /* Chi ha la taglia sulla testa può comprarsela, a metà prezzo. */
                played.decided = true;
                played.player = username;
                played.detail = "pagata";
                played.palle = secret / 2;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            if (!text::contains_ignore_case(message, hunted->second)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = hunted->second;
            played.palle = secret;
            state.scores[username] = counter(state.scores, username) + played.palle;
            state.scores[hunted->second] = counter(state.scores, hunted->second) - played.palle;
            break;
        }
        case Game::siege: {
            const auto held = state.challenge_who.find("target");
            if (held == state.challenge_who.end()) {
                return std::nullopt;
            }
            if (username == held->second) {
                /* Il difensore rimanda indietro un colpo per volta. */
                state.challenge["count"] = std::max(std::int64_t{0}, counter(state.challenge, "count") - 1);
                played.player = username;
                played.detail = "difende";
                played.number = counter(state.challenge, "count");
                break;
            }
            const std::int64_t hits = counter(state.challenge, "count") + 1;
            state.challenge["count"] = hits;
            played.player = username;
            played.detail = "colpo";
            played.number = hits;
            if (hits >= secret) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                state.scores[held->second] =
                    counter(state.scores, held->second) - counter(state.scores, held->second) / 10;
                played.detail = "caduto";
            }
            break;
        }
        case Game::market: {
            const bool buying = text::contains_ignore_case(message, "compro");
            const bool selling = text::contains_ignore_case(message, "vendo");
            const std::string key = player_key("b", username);
            if (buying == selling) {
                /* Ogni altro messaggio muove il prezzo e basta. */
                const std::int64_t move = static_cast<std::int64_t>(session.random_index(21)) - 10;
                state.challenge["price"] = std::max(std::int64_t{1}, counter(state.challenge, "price") + move);
                played.player = username;
                played.detail = "prezzo";
                played.number = counter(state.challenge, "price");
                break;
            }
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = buying ? counter(state.challenge, "price") : -counter(state.challenge, "price");
            played.player = username;
            played.detail = buying ? "compra" : "vende";
            played.number = counter(state.challenge, "price");
            break;
        }
        case Game::wager: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            state.challenge["count"] = counter(state.challenge, "count") + said;
            played.player = username;
            played.number = said;
            break;
        }
        case Game::relay: {
            const std::int64_t deadline = counter(state.challenge, "deadline");
            if (deadline != 0 && now > deadline) {
                return std::nullopt;
            }
            const auto last = state.challenge_who.find("leader");
            if (last != state.challenge_who.end() && last->second == username) {
                return std::nullopt;
            }
            state.challenge_who["leader"] = username;
            state.challenge[player_key("in", username)] = 1;
            const std::int64_t passes = counter(state.challenge, "count") + 1;
            state.challenge["count"] = passes;
            state.challenge["deadline"] = now + 15;
            played.player = username;
            played.number = passes;
            if (passes >= 6) {
                played.decided = true;
                const std::vector<std::pair<std::string, std::int64_t>> runners =
                    players_with(state.challenge, "in");
                const std::int64_t share =
                    counter(state.challenge, "pot") / static_cast<std::int64_t>(runners.size());
                for (const std::pair<std::string, std::int64_t> &runner : runners) {
                    state.scores[runner.first] = counter(state.scores, runner.first) + share;
                }
                played.palle = share;
            }
            break;
        }
        case Game::hostage: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const auto held = state.challenge_who.find("target");
            if (held == state.challenge_who.end() || held->second == username) {
                return std::nullopt;
            }
            state.scores[username] = counter(state.scores, username) - said;
            const std::int64_t paid = counter(state.challenge, "count") + said;
            state.challenge["count"] = paid;
            state.challenge[player_key("b", username)] =
                counter(state.challenge, player_key("b", username)) + said;
            played.player = username;
            played.number = paid;
            played.palle = said;
            if (paid >= secret) {
                played.decided = true;
                played.detail = held->second;
            }
            break;
        }
        case Game::legacy: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            break;
        }
        case Game::customs: {
            const std::string_view smuggled = forbidden_words.at(
                static_cast<std::size_t>(session.random_index(forbidden_words.size()))
            );
            played.player = username;
            if (text::contains_ignore_case(message, smuggled)) {
                played.detail = std::string{smuggled};
                played.palle = 100;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            const std::string key = player_key("b", username);
            state.challenge[key] = counter(state.challenge, key) + 1;
            played.number = counter(state.challenge, key);
            played.detail = "passa";
            break;
        }
        case Game::marathon: {
            const std::int64_t words = static_cast<std::int64_t>(words_in(message).size());
            if (words < 1) {
                return std::nullopt;
            }
            const std::int64_t covered = counter(state.challenge, "count") + words;
            state.challenge["count"] = covered;
            played.player = username;
            played.number = covered;
            if (covered >= secret) {
                played.decided = true;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
            }
            break;
        }
        case Game::stars: {
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            const zodiac::Sign sign = zodiac::sign_of(username);
            const auto house = static_cast<zodiac::Element>(secret);
            state.challenge[key] = 1;
            played.player = username;
            played.detail = sign.name;
            if (sign.element == house) {
                played.palle = counter(state.challenge, "pot") / 2;
                state.scores[username] = counter(state.scores, username) + played.palle;
                played.number = 1;
                break;
            }
            if (zodiac::opposed(sign.element, house)) {
                played.palle = counter(state.scores, username) / 20;
                state.scores[username] = counter(state.scores, username) - played.palle;
                played.number = -1;
                break;
            }
            played.number = 0;
            break;
        }
        case Game::fraud: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            played.number = said;
            break;
        }
        case Game::scheme: {
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            /* Si entra pagando duecento palle, che vanno a chi è entrato prima. */
            const std::int64_t place = counter(state.challenge, "count") + 1;
            state.challenge["count"] = place;
            state.challenge[key] = place;
            state.scores[username] = counter(state.scores, username) - 200;
            const std::vector<std::pair<std::string, std::int64_t>> before =
                players_with(state.challenge, "b");
            const std::int64_t share = before.size() > 1
                ? 200 / static_cast<std::int64_t>(before.size() - 1)
                : 0;
            for (const std::pair<std::string, std::int64_t> &sooner : before) {
                if (sooner.second < place) {
                    state.scores[sooner.first] = counter(state.scores, sooner.first) + share;
                }
            }
            played.player = username;
            played.number = place;
            played.palle = share;
            break;
        }
        case Game::refund: {
            const auto buyer = state.challenge_who.find("target");
            if (buyer == state.challenge_who.end() || buyer->second == username) {
                return std::nullopt;
            }
            const bool yes = text::contains_ignore_case(message, "rimborso");
            const bool no = text::contains_ignore_case(message, "truffa");
            if (yes == no) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = yes ? 1 : 0;
            played.player = username;
            played.detail = yes ? "rimborso" : "truffa";
            played.number = static_cast<std::int64_t>(players_with(state.challenge, "v").size());
            break;
        }
        case Game::spy: {
            const std::vector<std::string> words = words_in(message);
            const auto named = std::ranges::find_if(words, [&state](const std::string &word) {
                return state.scores.find(word) != state.scores.end();
            });
            if (named == words.end()) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 1;
            state.challenge_who[player_key("says", username)] = *named;
            played.player = username;
            played.detail = *named;
            break;
        }
        case Game::plot: {
            const std::vector<std::string> words = words_in(message);
            const auto named = std::ranges::find_if(words, [&state, &username](const std::string &word) {
                return word != username && state.scores.find(word) != state.scores.end();
            });
            if (named == words.end()) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 1;
            state.challenge_who[player_key("says", username)] = *named;
            state.challenge[player_key("t", *named)] = counter(state.challenge, player_key("t", *named)) + 1;
            played.player = username;
            played.detail = "segnato";
            break;
        }
        case Game::dowry: {
            const std::vector<std::string> words = words_in(message);
            const auto asked = std::ranges::find_if(words, [&state, &username](const std::string &word) {
                return word != username && state.scores.find(word) != state.scores.end();
            });
            const auto waiting = state.challenge_who.find("leader");
            if (waiting != state.challenge_who.end()) {
                const auto wanted = state.challenge_who.find("target");
                const bool yes = text::contains_ignore_case(message, "sì") ||
                                 text::contains_ignore_case(message, "si") ||
                                 text::contains_ignore_case(message, "accetto");
                if (wanted != state.challenge_who.end() && wanted->second == username && yes) {
                    played.decided = true;
                    played.player = username;
                    played.detail = waiting->second;
                    played.palle = counter(state.challenge, "pot") / 2;
                    state.scores[username] = counter(state.scores, username) + played.palle;
                    state.scores[waiting->second] = counter(state.scores, waiting->second) + played.palle;
                    break;
                }
            }
            if (asked == words.end() || !text::contains_ignore_case(message, "sposo")) {
                return std::nullopt;
            }
            state.challenge_who["leader"] = username;
            state.challenge_who["target"] = *asked;
            played.player = username;
            played.detail = *asked;
            break;
        }
        case Game::bingo: {
            const std::string key = player_key("c", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            /* Una cartella a testa: cinque numeri fra 1 e 30, in un solo intero. */
            std::int64_t card = 0;
            for (int at = 0; at < 5; ++at) {
                card = (card * 31) + 1 + static_cast<std::int64_t>(session.random_index(30));
            }
            state.challenge[key] = card;
            played.player = username;
            played.number = card;
            played.detail = "cartella";
            break;
        }
        case Game::horses: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 4) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            played.number = said;
            break;
        }
        case Game::quake: {
            state.challenge[player_key("seen", username)] = now;
            played.player = username;
            played.detail = "in piedi";
            break;
        }
        case Game::war: {
            const std::string key = player_key("s", username);
            std::int64_t side = counter(state.challenge, key);
            if (side == 0) {
                side = (counter(state.challenge, "w1") <= counter(state.challenge, "w2")) ? 1 : 2;
                state.challenge[key] = side;
            }
            const std::string lane = side == 1 ? "w1" : "w2";
            state.challenge[lane] = counter(state.challenge, lane) + 1;
            played.player = username;
            played.number = side;
            played.palle = counter(state.challenge, lane);
            break;
        }
        case Game::deposit: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            state.scores[username] = counter(state.scores, username) - said;
            played.player = username;
            played.palle = said;
            break;
        }
        case Game::talent: {
            const std::vector<std::string> words = words_in(message);
            std::vector<std::string> kept = words;
            std::ranges::sort(kept);
            const auto last = std::ranges::unique(kept);
            kept.erase(last.begin(), last.end());
            const auto score = static_cast<std::int64_t>(kept.size());
            if (score <= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            state.challenge["bid"] = score;
            state.challenge_who["leader"] = username;
            played.player = username;
            played.number = score;
            break;
        }
        case Game::treasure: {
            const std::string_view chest = mirrors.at(static_cast<std::size_t>(secret));
            const std::vector<std::string> words = words_in(message);
            if (std::ranges::find(words, chest) != words.end()) {
                played.decided = true;
                played.player = username;
                played.detail = chest;
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            const auto wrong = std::ranges::find_if(words, [](const std::string &word) {
                return std::ranges::find(mirrors, word) != mirrors.end();
            });
            if (wrong == words.end()) {
                return std::nullopt;
            }
            played.player = username;
            played.detail = *wrong;
            played.palle = 20;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        case Game::domino: {
            const std::int64_t said = number_in(message);
            if (said < 0) {
                return std::nullopt;
            }
            const std::int64_t wanted = counter(state.challenge, "letter");
            played.player = username;
            played.number = wanted;
            if (said / 10 % 10 != wanted && said % 10 != wanted && said != wanted) {
                played.decided = true;
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            state.challenge["letter"] = said % 10;
            state.challenge["count"] = counter(state.challenge, "count") + 1;
            played.number = said % 10;
            played.palle = counter(state.challenge, "count");
            break;
        }
        case Game::tunnel: {
            const std::vector<std::string> words = words_in(message);
            const std::size_t longest = std::accumulate(
                words.begin(),
                words.end(),
                std::size_t{0},
                [](std::size_t so_far, const std::string &word) {
                    return std::max(so_far, word.size());
                }
            );
            if (static_cast<std::int64_t>(longest) <= counter(state.challenge, "bid")) {
                return std::nullopt;
            }
            state.challenge["bid"] = static_cast<std::int64_t>(longest);
            state.challenge[player_key("in", username)] = 1;
            const std::int64_t steps = counter(state.challenge, "count") + 1;
            state.challenge["count"] = steps;
            played.player = username;
            played.number = steps;
            if (steps >= 8) {
                played.decided = true;
                const std::vector<std::pair<std::string, std::int64_t>> diggers =
                    players_with(state.challenge, "in");
                const std::int64_t share =
                    counter(state.challenge, "pot") / static_cast<std::int64_t>(diggers.size());
                for (const std::pair<std::string, std::int64_t> &digger : diggers) {
                    state.scores[digger.first] = counter(state.scores, digger.first) + share;
                }
                played.palle = share;
            }
            break;
        }
        case Game::dutch: {
            if (!text::contains_ignore_case(message, "prendo")) {
                return std::nullopt;
            }
            const std::int64_t price = counter(state.challenge, "price");
            played.decided = true;
            played.player = username;
            played.number = price;
            played.palle = counter(state.challenge, "pot") - price;
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::riddle: {
            const std::string_view answer = mirrors.at(static_cast<std::size_t>(secret));
            if (!text::contains_ignore_case(message, answer)) {
                return std::nullopt;
            }
            const std::int64_t clues = std::max(std::int64_t{1}, counter(state.challenge, "clues"));
            played.decided = true;
            played.player = username;
            played.detail = answer;
            played.palle = counter(state.challenge, "pot") / clues;
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::navy: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 25) {
                return std::nullopt;
            }
            const std::string key = std::format("shot{}", said);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 1;
            played.player = username;
            played.number = said;
            if (said == secret) {
                played.decided = true;
                played.detail = "colpito";
                played.palle = counter(state.challenge, "pot");
                state.scores[username] = counter(state.scores, username) + played.palle;
                break;
            }
            played.detail = "acqua";
            played.palle = 20;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        case Game::election: {
            const std::vector<std::string> words = words_in(message);
            const auto voted = std::ranges::find_if(words, [&state](const std::string &word) {
                return state.scores.find(word) != state.scores.end();
            });
            if (voted == words.end()) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 1;
            state.challenge[player_key("t", *voted)] =
                counter(state.challenge, player_key("t", *voted)) + 1;
            played.player = username;
            played.detail = *voted;
            break;
        }
        case Game::tug: {
            /* Da che parte della fune stai lo dice la prima lettera del nome. */
            const std::int64_t side = !username.empty() && std::tolower(static_cast<unsigned char>(username.front())) <= 'm' ? 1 : 2;
            state.challenge[player_key("s", username)] = side;
            const std::int64_t rope = counter(state.challenge, "rope") + (side == 1 ? 1 : -1);
            state.challenge["rope"] = rope;
            played.player = username;
            played.number = side;
            played.palle = rope;
            break;
        }
        case Game::jenga: {
            state.challenge[player_key("in", username)] = 1;
            const std::int64_t pulled = counter(state.challenge, "count") + 1;
            state.challenge["count"] = pulled;
            played.player = username;
            played.number = pulled;
            if (session.random_index(8) == 0) {
                played.decided = true;
                played.detail = "crollo";
                played.palle = counter(state.scores, username) / 10;
                state.scores[username] = counter(state.scores, username) - played.palle;
                break;
            }
            played.detail = "regge";
            break;
        }
        case Game::whispers: {
            const auto word = state.challenge_who.find("word");
            if (word == state.challenge_who.end()) {
                return std::nullopt;
            }
            const std::vector<std::string> words = words_in(message);
            const auto said = std::ranges::find_if(words, [&word](const std::string &candidate) {
                if (candidate.size() != word->second.size() || candidate == word->second) {
                    return false;
                }
                int different = 0;
                for (std::size_t at = 0; at < candidate.size(); ++at) {
                    different += candidate.at(at) == word->second.at(at) ? 0 : 1;
                }
                return different == 1;
            });
            if (said == words.end()) {
                return std::nullopt;
            }
            state.challenge_who["word"] = *said;
            state.challenge[player_key("in", username)] = 1;
            const std::int64_t passes = counter(state.challenge, "count") + 1;
            state.challenge["count"] = passes;
            played.player = username;
            played.detail = *said;
            played.number = passes;
            if (passes >= 5) {
                played.decided = true;
                const std::vector<std::pair<std::string, std::int64_t>> line =
                    players_with(state.challenge, "in");
                const std::int64_t share =
                    counter(state.challenge, "pot") / static_cast<std::int64_t>(line.size());
                for (const std::pair<std::string, std::int64_t> &one : line) {
                    state.scores[one.first] = counter(state.scores, one.first) + share;
                }
                played.palle = share;
            }
            break;
        }
        case Game::smuggle: {
            const std::int64_t said = number_in(message);
            if (said < 1) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            state.scores[username] = counter(state.scores, username) - said;
            played.player = username;
            played.palle = said;
            break;
        }
        case Game::insurance: {
            if (!text::contains_ignore_case(message, "assicuro")) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 100;
            state.scores[username] = counter(state.scores, username) - 100;
            played.player = username;
            played.palle = 100;
            break;
        }
        case Game::strike: {
            played.decided = true;
            played.player = username;
            played.number = counter(state.challenge, "count");
            played.palle = counter(state.scores, username) / 10;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        case Game::contest: {
            const std::size_t asked = static_cast<std::size_t>(counter(state.challenge, "secret"));
            if (!text::contains_ignore_case(message, questions.at(asked).answer)) {
                return std::nullopt;
            }
            const std::string key = player_key("p", username);
            const std::string round = player_key("r", username);
            if (counter(state.challenge, round) == counter(state.challenge, "round")) {
                return std::nullopt;
            }
            state.challenge[round] = counter(state.challenge, "round");
            state.challenge[key] = counter(state.challenge, key) + 1;
            played.player = username;
            played.number = counter(state.challenge, key);
            break;
        }
        case Game::cadastre: {
            const std::int64_t said = number_in(message);
            if (said < 1 || said > 20) {
                return std::nullopt;
            }
            const std::string key = player_key("b", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = said;
            played.player = username;
            played.number = said;
            break;
        }
        case Game::pilgrimage: {
            const auto steps = static_cast<std::int64_t>(words_in(message).size());
            if (steps < 1) {
                return std::nullopt;
            }
            const std::int64_t walked = counter(state.challenge, "count") + steps;
            state.challenge["count"] = walked;
            state.challenge[player_key("in", username)] = 1;
            played.player = username;
            played.number = walked;
            if (walked >= secret) {
                played.decided = true;
                const std::vector<std::pair<std::string, std::int64_t>> pilgrims =
                    players_with(state.challenge, "in");
                const std::int64_t share =
                    counter(state.challenge, "pot") / static_cast<std::int64_t>(pilgrims.size());
                for (const std::pair<std::string, std::int64_t> &pilgrim : pilgrims) {
                    state.scores[pilgrim.first] = counter(state.scores, pilgrim.first) + share;
                }
                played.palle = share;
            }
            break;
        }
        case Game::apocalypse: {
            const std::string key = player_key("in", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = 1;
            played.player = username;
            played.number = static_cast<std::int64_t>(players_with(state.challenge, "in").size());
            break;
        }
        case Game::whosaid: {
            const auto author = state.challenge_who.find("target");
            if (author == state.challenge_who.end() ||
                !text::contains_ignore_case(message, author->second)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = author->second;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::halfquote: {
            const auto ending = state.challenge_who.find("target");
            if (ending == state.challenge_who.end() ||
                !text::contains_ignore_case(message, ending->second)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.detail = ending->second;
            played.palle = counter(state.challenge, "pot");
            state.scores[username] = counter(state.scores, username) + played.palle;
            break;
        }
        case Game::truequote: {
            const bool yes = text::contains_ignore_case(message, "vera");
            const bool no = text::contains_ignore_case(message, "falsa");
            if (yes == no) {
                return std::nullopt;
            }
            const std::string key = player_key("v", username);
            if (state.challenge.find(key) != state.challenge.end()) {
                return std::nullopt;
            }
            state.challenge[key] = yes ? 1 : 0;
            played.player = username;
            played.detail = yes ? "vera" : "falsa";
            break;
        }
        case Game::forged: {
            const auto which = state.challenge_who.find("forged");
            if (which == state.challenge_who.end()) {
                return std::nullopt;
            }
            const std::string keyword = which->second;
            state.challenge["fx:msgs"] = counter(state.challenge, "fx:msgs") + 1;
            state.challenge["fx:p:" + username] = 1;
            if (counter(state.challenge, "fx:first") == 0) {
                state.challenge["fx:first"] = now;
            }
            const ForgedOutcome moved =
                run_forged(session, state, keyword, "message", {username, std::string{message}});
            if (!moved.ok) {
                state.challenge.clear();
                state.challenge_who.clear();
                return std::nullopt;
            }
            if (moved.text.empty() && !moved.decided) {
                return std::nullopt;
            }
            played.player = username;
            played.detail = moved.text;
            played.palle = moved.palle;
            played.decided = moved.decided;
            if (played.decided) {
                record_forged(state, keyword, true, now);
            }
            break;
        }
        case Game::forbidden: {
            const std::string_view word = forbidden_words.at(static_cast<std::size_t>(secret));
            if (!text::contains_ignore_case(message, word)) {
                return std::nullopt;
            }
            played.decided = true;
            played.player = username;
            played.palle = counter(state.scores, username) / 10;
            state.scores[username] = counter(state.scores, username) - played.palle;
            break;
        }
        }
        if (played.decided) {
            /* Settled there and then: nothing is left for the closing round to find. */
            state.challenge.clear();
            state.challenge_who.clear();
        }
        return played;
    });
}

std::optional<GameClosed> game_close(Storage &storage, std::int64_t now) {
    const std::optional<GameClosed> result =
        storage.transaction([now](StorageSession &session) -> std::optional<GameClosed> {
            ConquisterState &state = session.state();
            const std::int64_t closes = counter(state.challenge, "closes");
            if (closes == 0 || closes > now) {
                return std::nullopt;
            }
            GameClosed closed;
            closed.kind = static_cast<Game>(counter(state.challenge, "kind"));
            closed.pot = counter(state.challenge, "pot");
            closed.secret = counter(state.challenge, "secret");
            if (closed.kind == Game::forged) {
                const auto which = state.challenge_who.find("forged");
                if (which != state.challenge_who.end()) {
                    closed.winner = which->second;
                    const ForgedOutcome last = run_forged(session, state, which->second, "close", {});
                    closed.detail = last.text;
                    record_forged(state, which->second, false, now);
                }
            }
            if (closed.kind == Game::truequote) {
                closed.table = players_with(state.challenge, "v");
                const auto author = state.challenge_who.find("target");
                const auto shown = state.challenge_who.find("shown");
                if (author != state.challenge_who.end() && shown != state.challenge_who.end()) {
                    closed.winner = author->second;
                    closed.detail = shown->second;
                    std::vector<std::string> right;
                    for (const std::pair<std::string, std::int64_t> &vote : closed.table) {
                        if ((vote.second == 1) == (closed.secret == 1)) {
                            right.push_back(vote.first);
                        }
                    }
                    if (!right.empty()) {
                        const std::int64_t share =
                            closed.pot / static_cast<std::int64_t>(right.size());
                        for (const std::string &one : right) {
                            state.scores[one] = counter(state.scores, one) + share;
                        }
                    }
                }
            }
            if (closed.kind == Game::tug) {
                closed.table = players_with(state.challenge, "s");
                const std::int64_t rope = counter(state.challenge, "rope");
                closed.secret = rope;
                if (rope != 0 && !closed.table.empty()) {
                    const std::int64_t winning = rope > 0 ? 1 : 2;
                    std::vector<std::string> pullers;
                    for (const std::pair<std::string, std::int64_t> &puller : closed.table) {
                        if (puller.second == winning) {
                            pullers.push_back(puller.first);
                        }
                    }
                    if (!pullers.empty()) {
                        const std::int64_t share =
                            closed.pot / static_cast<std::int64_t>(pullers.size());
                        for (const std::string &puller : pullers) {
                            state.scores[puller] = counter(state.scores, puller) + share;
                        }
                        closed.winner = pullers.front();
                        closed.detail = winning == 1 ? "a-m" : "n-z";
                    }
                }
            }
            if (closed.kind == Game::jenga) {
                closed.table = players_with(state.challenge, "in");
                if (!closed.table.empty()) {
                    const std::int64_t share =
                        closed.pot / static_cast<std::int64_t>(closed.table.size());
                    for (const std::pair<std::string, std::int64_t> &player : closed.table) {
                        state.scores[player.first] = counter(state.scores, player.first) + share;
                    }
                    closed.secret = counter(state.challenge, "count");
                }
            }
            if (closed.kind == Game::smuggle) {
                closed.table = players_with(state.challenge, "b");
                if (!closed.table.empty()) {
                    const std::size_t checked = session.random_index(closed.table.size());
                    const std::pair<std::string, std::int64_t> &caught =
                        closed.table.at(checked);
                    closed.winner = caught.first;
                    closed.secret = caught.second;
                    for (const std::pair<std::string, std::int64_t> &load : closed.table) {
                        if (load.first != caught.first) {
                            state.scores[load.first] = counter(state.scores, load.first) + (load.second * 2);
                        }
                    }
                }
            }
            if (closed.kind == Game::insurance) {
                closed.table = players_with(state.challenge, "b");
                const bool disaster = session.random_index(3) == 0;
                closed.detail = disaster ? "disastro" : "sereno";
                closed.secret = static_cast<std::int64_t>(closed.table.size());
                if (disaster) {
                    for (const std::pair<std::string, std::int64_t> &insured : closed.table) {
                        state.scores[insured.first] = counter(state.scores, insured.first) + 500;
                    }
                }
            }
            if (closed.kind == Game::strike) {
                /* Nessuno ha parlato fino alla fine: la cassa si divide fra tutti. */
                const std::int64_t kitty = counter(state.challenge, "count");
                closed.secret = kitty;
                if (kitty > 0 && !state.scores.empty()) {
                    const std::int64_t share = kitty / static_cast<std::int64_t>(state.scores.size());
                    for (Counters::value_type &entry : state.scores) {
                        entry.second += share;
                    }
                    closed.detail = std::format("{}", share);
                }
            }
            if (closed.kind == Game::contest) {
                closed.table = players_with(state.challenge, "p");
                std::int64_t best = 0;
                int howmany = 0;
                for (const std::pair<std::string, std::int64_t> &player : closed.table) {
                    if (player.second > best) {
                        best = player.second;
                        howmany = 1;
                        closed.winner = player.first;
                    } else if (player.second == best) {
                        ++howmany;
                    }
                }
                if (best == 0 || howmany > 1) {
                    closed.winner.clear();
                } else {
                    closed.secret = best;
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::cadastre) {
                closed.table = players_with(state.challenge, "b");
                Counters howmany;
                for (const std::pair<std::string, std::int64_t> &claim : closed.table) {
                    howmany[std::format("{}", claim.second)] += 1;
                }
                std::vector<std::string> owners;
                for (const std::pair<std::string, std::int64_t> &claim : closed.table) {
                    if (counter(howmany, std::format("{}", claim.second)) == 1) {
                        owners.push_back(claim.first);
                    }
                }
                closed.secret = static_cast<std::int64_t>(owners.size());
                if (!owners.empty()) {
                    const std::int64_t rent = closed.pot / static_cast<std::int64_t>(owners.size());
                    for (const std::string &owner : owners) {
                        state.scores[owner] = counter(state.scores, owner) + rent;
                    }
                    closed.winner = owners.front();
                    closed.detail = std::format("{}", rent);
                }
            }
            if (closed.kind == Game::apocalypse) {
                closed.table = players_with(state.challenge, "in");
                const std::vector<std::pair<std::string, std::int64_t>> saved =
                    players_with(state.challenge, "safe");
                closed.secret = static_cast<std::int64_t>(saved.size());
                if (!saved.empty()) {
                    const std::int64_t share =
                        closed.pot / static_cast<std::int64_t>(saved.size());
                    for (const std::pair<std::string, std::int64_t> &one : saved) {
                        state.scores[one.first] = counter(state.scores, one.first) + share;
                    }
                }
                for (const std::pair<std::string, std::int64_t> &left : closed.table) {
                    const std::int64_t lost = counter(state.scores, left.first) / 5;
                    state.scores[left.first] = counter(state.scores, left.first) - lost;
                    closed.winner = left.first;
                }
            }
            if (closed.kind == Game::war) {
                const std::int64_t first = counter(state.challenge, "w1");
                const std::int64_t second = counter(state.challenge, "w2");
                closed.table = players_with(state.challenge, "s");
                if (first != second && !closed.table.empty()) {
                    const std::int64_t winning = first > second ? 1 : 2;
                    closed.secret = winning;
                    std::vector<std::string> victors;
                    for (const std::pair<std::string, std::int64_t> &soldier : closed.table) {
                        if (soldier.second == winning) {
                            victors.push_back(soldier.first);
                        } else {
                            state.scores[soldier.first] = counter(state.scores, soldier.first) - 100;
                        }
                    }
                    if (!victors.empty()) {
                        const std::int64_t share =
                            closed.pot / static_cast<std::int64_t>(victors.size());
                        for (const std::string &victor : victors) {
                            state.scores[victor] = counter(state.scores, victor) + share;
                        }
                        closed.winner = victors.front();
                    }
                }
            }
            if (closed.kind == Game::deposit) {
                closed.table = players_with(state.challenge, "b");
                /* Una volta su cinque la banca chiude gli sportelli e si tiene tutto. */
                const bool failed = session.random_index(5) == 0;
                closed.detail = failed ? "fallita" : "solida";
                if (!failed) {
                    for (const std::pair<std::string, std::int64_t> &saved : closed.table) {
                        state.scores[saved.first] =
                            counter(state.scores, saved.first) + saved.second + (saved.second / 10);
                    }
                }
                closed.secret = static_cast<std::int64_t>(closed.table.size());
            }
            if (closed.kind == Game::talent) {
                const auto leader = state.challenge_who.find("leader");
                if (leader != state.challenge_who.end()) {
                    closed.winner = leader->second;
                    closed.secret = counter(state.challenge, "bid");
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::election) {
                closed.table = players_with(state.challenge, "t");
                std::int64_t most = 0;
                int howmany = 0;
                for (const std::pair<std::string, std::int64_t> &votes : closed.table) {
                    if (votes.second > most) {
                        most = votes.second;
                        howmany = 1;
                        closed.winner = votes.first;
                    } else if (votes.second == most) {
                        ++howmany;
                    }
                }
                if (most == 0 || howmany > 1) {
                    closed.winner.clear();
                } else {
                    /* Il sindaco mette una tassa di cinquanta palle su ogni elettore. */
                    const std::vector<std::pair<std::string, std::int64_t>> voters =
                        players_with(state.challenge, "v");
                    for (const std::pair<std::string, std::int64_t> &voter : voters) {
                        state.scores[voter.first] = counter(state.scores, voter.first) - 50;
                    }
                    closed.secret = 50 * static_cast<std::int64_t>(voters.size());
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.secret;
                }
            }
            if (closed.kind == Game::fraud) {
                closed.table = players_with(state.challenge, "b");
                std::int64_t best = -1;
                for (const std::pair<std::string, std::int64_t> &declared : closed.table) {
                    const std::int64_t real = counter(state.scores, declared.first);
                    if (declared.second * 10 < real) {
                        /* Dichiarato meno di un decimo: evasione, e la multa è il doppio. */
                        const std::int64_t fine = (real / 10) - declared.second;
                        state.scores[declared.first] = counter(state.scores, declared.first) - fine;
                        continue;
                    }
                    if (declared.second > best) {
                        best = declared.second;
                        closed.winner = declared.first;
                    }
                }
                if (best < 0) {
                    closed.winner.clear();
                } else {
                    closed.secret = best;
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::scheme) {
                closed.table = players_with(state.challenge, "b");
                closed.secret = static_cast<std::int64_t>(closed.table.size());
                for (const std::pair<std::string, std::int64_t> &joined : closed.table) {
                    if (joined.second > closed.secret - 2) {
                        closed.winner = joined.first;
                    }
                }
            }
            if (closed.kind == Game::refund) {
                const auto buyer = state.challenge_who.find("target");
                closed.table = players_with(state.challenge, "v");
                if (buyer != state.challenge_who.end() && !closed.table.empty()) {
                    closed.winner = buyer->second;
                    const std::int64_t yes = std::accumulate(
                        closed.table.begin(),
                        closed.table.end(),
                        std::int64_t{0},
                        [](std::int64_t sum, const std::pair<std::string, std::int64_t> &vote) {
                            return sum + vote.second;
                        }
                    );
                    /* Il supporto clienti ha l'ultima parola, e la tira a sorte pesata sui voti. */
                    const auto jurors = static_cast<std::int64_t>(closed.table.size());
                    const bool refunded =
                        static_cast<std::int64_t>(session.random_index(static_cast<std::size_t>(jurors))) < yes;
                    closed.detail = refunded ? "rimborsato" : "respinto";
                    closed.secret = closed.pot / 2;
                    if (refunded) {
                        state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.secret;
                    } else {
                        state.scores[closed.winner] = counter(state.scores, closed.winner) - closed.secret;
                    }
                }
            }
            if (closed.kind == Game::spy) {
                const auto spy = state.challenge_who.find("target");
                closed.table = players_with(state.challenge, "v");
                if (spy != state.challenge_who.end()) {
                    closed.winner = spy->second;
                    std::vector<std::string> right;
                    for (const std::pair<std::string, std::int64_t> &vote : closed.table) {
                        const auto said = state.challenge_who.find(player_key("says", vote.first));
                        if (said != state.challenge_who.end() && said->second == spy->second) {
                            right.push_back(vote.first);
                        }
                    }
                    if (right.empty()) {
                        closed.detail = "scappata";
                        state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                    } else {
                        closed.detail = "presa";
                        const std::int64_t share =
                            closed.pot / static_cast<std::int64_t>(right.size());
                        for (const std::string &hunter : right) {
                            state.scores[hunter] = counter(state.scores, hunter) + share;
                        }
                        closed.secret = share;
                    }
                }
            }
            if (closed.kind == Game::plot) {
                closed.table = players_with(state.challenge, "t");
                std::int64_t most = 0;
                int howmany = 0;
                for (const std::pair<std::string, std::int64_t> &marked : closed.table) {
                    if (marked.second > most) {
                        most = marked.second;
                        howmany = 1;
                        closed.winner = marked.first;
                    } else if (marked.second == most) {
                        ++howmany;
                    }
                }
                if (most == 0 || howmany > 1) {
                    closed.winner.clear();
                } else {
                    const std::int64_t taken = counter(state.scores, closed.winner) / 10;
                    state.scores[closed.winner] = counter(state.scores, closed.winner) - taken;
                    closed.secret = taken;
                    std::vector<std::string> plotters;
                    for (const std::pair<std::string, std::int64_t> &vote : players_with(state.challenge, "v")) {
                        const auto said = state.challenge_who.find(player_key("says", vote.first));
                        if (said != state.challenge_who.end() && said->second == closed.winner) {
                            plotters.push_back(vote.first);
                        }
                    }
                    if (!plotters.empty()) {
                        const std::int64_t share =
                            taken / static_cast<std::int64_t>(plotters.size());
                        for (const std::string &plotter : plotters) {
                            state.scores[plotter] = counter(state.scores, plotter) + share;
                        }
                    }
                }
            }
            if (closed.kind == Game::horses) {
                closed.table = players_with(state.challenge, "b");
                std::int64_t ahead = 0;
                for (int horse = 1; horse <= 4; ++horse) {
                    const std::int64_t run = counter(state.challenge, std::format("h{}", horse));
                    if (run > ahead) {
                        ahead = run;
                        closed.secret = horse;
                    }
                }
                for (const std::pair<std::string, std::int64_t> &bet : closed.table) {
                    if (bet.second == closed.secret) {
                        state.scores[bet.first] = counter(state.scores, bet.first) + closed.pot;
                        closed.winner = bet.first;
                    }
                }
            }
            if (closed.kind == Game::trial) {
                const auto accused = state.challenge_who.find("target");
                closed.table = players_with(state.challenge, "v");
                if (accused != state.challenge_who.end() && !closed.table.empty()) {
                    closed.winner = accused->second;
                    const std::int64_t against = std::accumulate(
                        closed.table.begin(),
                        closed.table.end(),
                        std::int64_t{0},
                        [](std::int64_t sum, const std::pair<std::string, std::int64_t> &vote) {
                            return sum + vote.second;
                        }
                    );
                    const auto jurors = static_cast<std::int64_t>(closed.table.size());
                    if (against * 2 > jurors) {
                        const std::int64_t fine = counter(state.scores, closed.winner) / 10;
                        state.scores[closed.winner] = counter(state.scores, closed.winner) - fine;
                        const std::int64_t share = against > 0 ? fine / against : 0;
                        for (const std::pair<std::string, std::int64_t> &vote : closed.table) {
                            if (vote.second == 1) {
                                state.scores[vote.first] = counter(state.scores, vote.first) + share;
                            }
                        }
                        closed.secret = fine;
                        closed.detail = "colpevole";
                    } else {
                        /* Assolto: chi lo accusava paga cento palle a testa. */
                        for (const std::pair<std::string, std::int64_t> &vote : closed.table) {
                            if (vote.second == 1) {
                                state.scores[vote.first] = counter(state.scores, vote.first) - 100;
                                state.scores[closed.winner] = counter(state.scores, closed.winner) + 100;
                            }
                        }
                        closed.secret = against * 100;
                        closed.detail = "assolto";
                    }
                }
            }
            if (closed.kind == Game::market) {
                closed.table = players_with(state.challenge, "b");
                closed.secret = counter(state.challenge, "price");
                for (const std::pair<std::string, std::int64_t> &position : closed.table) {
                    /* Comprato è positivo, venduto è negativo: il segno dice da che parte stava. */
                    const std::int64_t gain = position.second > 0
                        ? closed.secret - position.second
                        : -closed.secret - position.second;
                    state.scores[position.first] = counter(state.scores, position.first) + (gain * 10);
                }
            }
            if (closed.kind == Game::wager) {
                closed.table = players_with(state.challenge, "b");
                const std::int64_t total = counter(state.challenge, "count");
                closed.secret = total;
                closed.detail = total % 2 == 0 ? "pari" : "dispari";
                for (const std::pair<std::string, std::int64_t> &bet : closed.table) {
                    const bool even = bet.second % 2 == 0;
                    const std::int64_t change = even == (total % 2 == 0) ? bet.second : -bet.second;
                    state.scores[bet.first] = counter(state.scores, bet.first) + change;
                }
            }
            if (closed.kind == Game::hostage) {
                const auto held = state.challenge_who.find("target");
                closed.table = players_with(state.challenge, "b");
                const std::int64_t paid = counter(state.challenge, "count");
                closed.detail = std::format("{}", paid);
                if (held != state.challenge_who.end()) {
                    closed.winner = held->second;
                    if (paid < closed.secret) {
                        const std::int64_t taken = counter(state.scores, closed.winner) / 10;
                        state.scores[closed.winner] = counter(state.scores, closed.winner) - taken;
                    }
                }
            }
            if (closed.kind == Game::legacy) {
                closed.table = players_with(state.challenge, "b");
                std::int64_t least = -1;
                int howmany = 0;
                for (const std::pair<std::string, std::int64_t> &claim : closed.table) {
                    if (least < 0 || claim.second < least) {
                        least = claim.second;
                        howmany = 1;
                        closed.winner = claim.first;
                    } else if (claim.second == least) {
                        ++howmany;
                    }
                }
                if (least < 0 || howmany > 1) {
                    closed.winner.clear();
                } else {
                    closed.detail = std::format("{}", least);
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.secret;
                }
            }
            if (closed.kind == Game::customs) {
                closed.table = players_with(state.challenge, "b");
                std::int64_t best = 0;
                for (const std::pair<std::string, std::int64_t> &runs : closed.table) {
                    if (runs.second > best) {
                        best = runs.second;
                        closed.winner = runs.first;
                    }
                }
                if (best > 0) {
                    closed.secret = best;
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::sealed || closed.kind == Game::pyramid) {
                closed.table = players_with(state.challenge, "b");
                std::int64_t best = -1;
                int howmany = 0;
                for (const std::pair<std::string, std::int64_t> &bid : closed.table) {
                    if (closed.kind == Game::pyramid && bid.second > closed.secret) {
                        continue;
                    }
                    if (bid.second > best) {
                        best = bid.second;
                        howmany = 1;
                        closed.winner = bid.first;
                    } else if (bid.second == best) {
                        ++howmany;
                    }
                }
                if (best < 0 || howmany > 1) {
                    closed.winner.clear();
                } else {
                    closed.detail = std::format("{}", best);
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                    if (closed.kind == Game::sealed) {
                        state.scores[closed.winner] = counter(state.scores, closed.winner) - best;
                    }
                }
            }
            if (closed.kind == Game::unique) {
                closed.table = players_with(state.challenge, "b");
                Counters howmany;
                for (const std::pair<std::string, std::int64_t> &bid : closed.table) {
                    howmany[std::format("{}", bid.second)] += 1;
                }
                std::int64_t best = -1;
                for (const std::pair<std::string, std::int64_t> &bid : closed.table) {
                    if (counter(howmany, std::format("{}", bid.second)) != 1) {
                        continue;
                    }
                    if (best < 0 || bid.second < best) {
                        best = bid.second;
                        closed.winner = bid.first;
                    }
                }
                if (best < 0) {
                    closed.winner.clear();
                } else {
                    closed.detail = std::format("{}", best);
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::average) {
                closed.table = players_with(state.challenge, "b");
                if (!closed.table.empty()) {
                    const std::int64_t total = std::accumulate(
                        closed.table.begin(),
                        closed.table.end(),
                        std::int64_t{0},
                        [](std::int64_t sum, const std::pair<std::string, std::int64_t> &bid) {
                            return sum + bid.second;
                        }
                    );
                    const std::int64_t wanted =
                        total * 2 / (3 * static_cast<std::int64_t>(closed.table.size()));
                    closed.secret = wanted;
                    std::int64_t best = -1;
                    for (const std::pair<std::string, std::int64_t> &bid : closed.table) {
                        const std::int64_t away = bid.second > wanted ? bid.second - wanted : wanted - bid.second;
                        if (best < 0 || away < best) {
                            best = away;
                            closed.winner = bid.first;
                        }
                    }
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::russian || closed.kind == Game::climb || closed.kind == Game::bank) {
                closed.table = players_with(state.challenge, closed.kind == Game::russian ? "in" : "s");
                if (closed.kind == Game::bank) {
                    std::int64_t best = -1;
                    for (const std::pair<std::string, std::int64_t> &hand : closed.table) {
                        if (hand.second <= 21 && hand.second > best) {
                            best = hand.second;
                            closed.winner = hand.first;
                        }
                    }
                    if (best < 0) {
                        closed.winner.clear();
                    } else {
                        closed.secret = best;
                        state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                    }
                }
                if (closed.kind == Game::russian && !closed.table.empty()) {
                    /* Chi è ancora vivo si divide il piatto. */
                    const std::int64_t share =
                        closed.pot / static_cast<std::int64_t>(closed.table.size());
                    for (const std::pair<std::string, std::int64_t> &alive : closed.table) {
                        state.scores[alive.first] = counter(state.scores, alive.first) + share;
                    }
                    closed.secret = share;
                }
                if (closed.kind == Game::climb) {
                    for (const std::pair<std::string, std::int64_t> &stake : closed.table) {
                        const std::int64_t paid = std::min(stake.second, closed.pot);
                        state.scores[stake.first] = counter(state.scores, stake.first) + paid;
                    }
                    closed.secret = static_cast<std::int64_t>(closed.table.size());
                }
            }
            if (closed.kind == Game::collect) {
                closed.table = players_with(state.challenge, "b");
                const std::int64_t total = counter(state.challenge, "count");
                closed.detail = std::format("{}", total);
                if (total == closed.secret && !closed.table.empty()) {
                    const std::int64_t share =
                        closed.pot / static_cast<std::int64_t>(closed.table.size());
                    for (const std::pair<std::string, std::int64_t> &given : closed.table) {
                        state.scores[given.first] =
                            counter(state.scores, given.first) + given.second + share;
                    }
                } else if (total < closed.secret) {
                    /* Non ci siamo arrivati: ognuno si riprende quello che aveva messo. */
                    for (const std::pair<std::string, std::int64_t> &given : closed.table) {
                        state.scores[given.first] = counter(state.scores, given.first) + given.second;
                    }
                }
            }
            if (closed.kind == Game::shortest || closed.kind == Game::river) {
                const auto leader = state.challenge_who.find("leader");
                if (leader != state.challenge_who.end()) {
                    closed.winner = leader->second;
                    closed.secret = counter(state.challenge, "bid");
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::longest) {
                const auto leader = state.challenge_who.find("leader");
                if (leader != state.challenge_who.end()) {
                    closed.winner = leader->second;
                    closed.secret = counter(state.challenge, "bid");
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                }
            }
            if (closed.kind == Game::silence && state.challenge_who.find("leader") == state.challenge_who.end()) {
                /* Nobody spoke: everybody is paid for the quiet. */
                for (Counters::value_type &entry : state.scores) {
                    entry.second += closed.pot;
                }
                closed.secret = static_cast<std::int64_t>(state.scores.size());
            }
            if (closed.kind == Game::closest || closed.kind == Game::cards) {
                const auto leader = state.challenge_who.find("leader");
                if (leader != state.challenge_who.end()) {
                    closed.winner = leader->second;
                    state.scores[closed.winner] = counter(state.scores, closed.winner) + closed.pot;
                    if (closed.kind == Game::cards) {
                        closed.secret = counter(state.challenge, "bid");
                    }
                }
            }
            if (closed.kind == Game::auction) {
                const auto leader = state.challenge_who.find("leader");
                if (leader != state.challenge_who.end()) {
                    closed.winner = leader->second;
                    const std::int64_t bid = counter(state.challenge, "bid");
                    state.scores[closed.winner] = counter(state.scores, closed.winner) - bid;
                    if (find_entry(state.balloons, closed.winner) == state.balloons.end()) {
                        state.balloons[closed.winner] = 0;
                    }
                    closed.pot = bid;
                }
            }
            state.challenge.clear();
            state.challenge_who.clear();
            return closed;
        });

    if (result) {
        log_info("game closed kind={} winner={}", static_cast<int>(result->kind), result->winner);
    }
    return result;
}

std::optional<GameTicked> game_tick(Storage &storage, std::int64_t now) {
    const std::optional<GameTicked> result =
        storage.transaction([now](StorageSession &session) -> std::optional<GameTicked> {
            ConquisterState &state = session.state();
            const std::int64_t closes = counter(state.challenge, "closes");
            if (closes == 0 || closes <= now) {
                return std::nullopt;
            }
            GameTicked ticked;
            ticked.kind = static_cast<Game>(counter(state.challenge, "kind"));
            if (ticked.kind == Game::potato) {
                const std::int64_t fuse = counter(state.challenge, "fuse");
                if (fuse == 0 || fuse > now) {
                    return std::nullopt;
                }
                const auto holder = state.challenge_who.find("leader");
                ticked.decided = true;
                if (holder != state.challenge_who.end()) {
                    ticked.player = holder->second;
                    ticked.palle = counter(state.scores, ticked.player) / 10;
                    state.scores[ticked.player] = counter(state.scores, ticked.player) - ticked.palle;
                }
                state.challenge.clear();
                state.challenge_who.clear();
                return ticked;
            }
            if (ticked.kind == Game::forged) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                const auto which = state.challenge_who.find("forged");
                if (deadline == 0 || deadline > now || which == state.challenge_who.end()) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 25;
                const std::string keyword = which->second;
                const ForgedOutcome beat = run_forged(session, state, keyword, "tick", {now});
                if (!beat.ok) {
                    state.challenge.clear();
                    state.challenge_who.clear();
                    return std::nullopt;
                }
                if (beat.text.empty() && !beat.decided) {
                    return std::nullopt;
                }
                ticked.detail = beat.text;
                ticked.palle = beat.palle;
                ticked.decided = beat.decided;
                if (beat.decided) {
                    record_forged(state, keyword, true, now);
                    state.challenge.clear();
                    state.challenge_who.clear();
                }
                return ticked;
            }
            if (ticked.kind == Game::strike) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 20;
                state.challenge["count"] = counter(state.challenge, "count") + 500;
                ticked.number = counter(state.challenge, "count");
                return ticked;
            }
            if (ticked.kind == Game::contest) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                const std::int64_t round = counter(state.challenge, "round") + 1;
                if (round > 3) {
                    return std::nullopt;
                }
                state.challenge["round"] = round;
                state.challenge["deadline"] = now + 45;
                state.challenge["secret"] =
                    static_cast<std::int64_t>(session.random_index(questions.size()));
                ticked.number = round;
                ticked.detail = questions.at(static_cast<std::size_t>(counter(state.challenge, "secret"))).asked;
                return ticked;
            }
            if (ticked.kind == Game::pilgrimage) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 20;
                const std::int64_t walked = std::max(std::int64_t{0}, counter(state.challenge, "count") - 10);
                state.challenge["count"] = walked;
                ticked.number = walked;
                return ticked;
            }
            if (ticked.kind == Game::apocalypse) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 25;
                const std::vector<std::pair<std::string, std::int64_t>> waiting =
                    players_with(state.challenge, "in");
                if (waiting.empty()) {
                    return std::nullopt;
                }
                const std::pair<std::string, std::int64_t> &lucky =
                    waiting.at(session.random_index(waiting.size()));
                state.challenge.erase(player_key("in", lucky.first));
                state.challenge[player_key("safe", lucky.first)] = 1;
                ticked.player = lucky.first;
                ticked.number = static_cast<std::int64_t>(waiting.size()) - 1;
                return ticked;
            }
            if (ticked.kind == Game::dutch) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                const std::int64_t price =
                    std::max(std::int64_t{0}, counter(state.challenge, "price") - 1000);
                state.challenge["price"] = price;
                state.challenge["deadline"] = now + 20;
                ticked.number = price;
                return ticked;
            }
            if (ticked.kind == Game::riddle) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                const std::int64_t clues = counter(state.challenge, "clues") + 1;
                if (clues > 3) {
                    return std::nullopt;
                }
                state.challenge["clues"] = clues;
                state.challenge["deadline"] = now + 40;
                const std::string_view answer =
                    mirrors.at(static_cast<std::size_t>(counter(state.challenge, "secret")));
                ticked.number = clues;
                ticked.palle = counter(state.challenge, "pot") / clues;
                /* Il secondo indizio è la prima lettera, il terzo anche l'ultima. */
                ticked.detail = clues == 2
                    ? std::format("comincia per {}", answer.front())
                    : std::format("comincia per {} e finisce per {}", answer.front(), answer.back());
                return ticked;
            }
            if (ticked.kind == Game::bingo) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                const std::int64_t number = 1 + static_cast<std::int64_t>(session.random_index(30));
                state.challenge["deadline"] = now + 20;
                state.challenge[std::format("out{}", number)] = 1;
                ticked.number = number;
                for (const std::pair<std::string, std::int64_t> &card : players_with(state.challenge, "c")) {
                    std::int64_t left = card.second;
                    bool full = true;
                    for (int at = 0; at < 5; ++at) {
                        if (counter(state.challenge, std::format("out{}", left % 31)) == 0) {
                            full = false;
                        }
                        left /= 31;
                    }
                    if (full) {
                        ticked.decided = true;
                        ticked.player = card.first;
                        ticked.palle = counter(state.challenge, "pot");
                        state.scores[card.first] = counter(state.scores, card.first) + ticked.palle;
                        state.challenge.clear();
                        state.challenge_who.clear();
                        return ticked;
                    }
                }
                return ticked;
            }
            if (ticked.kind == Game::horses) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 15;
                const std::int64_t running = 1 + static_cast<std::int64_t>(session.random_index(4));
                const std::string lane = std::format("h{}", running);
                const std::int64_t far = counter(state.challenge, lane) + 1;
                state.challenge[lane] = far;
                ticked.number = running;
                ticked.palle = far;
                return ticked;
            }
            if (ticked.kind == Game::quake) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                state.challenge["deadline"] = now + 30;
                /* Trema: chi non si è fatto sentire negli ultimi trenta secondi perde un ventesimo. */
                std::vector<std::string> shaken;
                for (const std::pair<std::string, std::int64_t> &seen : players_with(state.challenge, "seen")) {
                    if (now - seen.second > 30) {
                        const std::int64_t lost = counter(state.scores, seen.first) / 20;
                        state.scores[seen.first] = counter(state.scores, seen.first) - lost;
                        shaken.push_back(seen.first);
                        ticked.palle += lost;
                    }
                }
                if (shaken.empty()) {
                    return std::nullopt;
                }
                ticked.number = static_cast<std::int64_t>(shaken.size());
                ticked.player = shaken.front();
                return ticked;
            }
            if (ticked.kind == Game::chairs) {
                const std::int64_t deadline = counter(state.challenge, "deadline");
                if (deadline == 0 || deadline > now) {
                    return std::nullopt;
                }
                std::vector<std::pair<std::string, std::int64_t>> seated =
                    players_with(state.challenge, "in");
                if (seated.size() < 2) {
                    /* Da soli non si gioca: la musica si ferma senza eliminare nessuno. */
                    state.challenge["deadline"] = now + 25;
                    return std::nullopt;
                }
                /* Esce chi ha scritto di meno, a parità l'ultimo in ordine di nome. */
                std::string slowest = seated.front().first;
                std::int64_t fewest = counter(state.challenge, player_key("n", slowest));
                for (const std::pair<std::string, std::int64_t> &player : seated) {
                    const std::int64_t howmany = counter(state.challenge, player_key("n", player.first));
                    if (howmany < fewest || (howmany == fewest && player.first > slowest)) {
                        fewest = howmany;
                        slowest = player.first;
                    }
                }
                state.challenge.erase(player_key("in", slowest));
                state.challenge.erase(player_key("n", slowest));
                ticked.player = slowest;
                ticked.number = counter(state.challenge, "round");
                state.challenge["round"] = ticked.number + 1;
                state.challenge["deadline"] = now + 25;
                seated = players_with(state.challenge, "in");
                if (seated.size() == 1) {
                    ticked.decided = true;
                    ticked.detail = seated.front().first;
                    ticked.palle = counter(state.challenge, "pot");
                    state.scores[ticked.detail] = counter(state.scores, ticked.detail) + ticked.palle;
                    state.challenge.clear();
                    state.challenge_who.clear();
                }
                return ticked;
            }
            return std::nullopt;
        });

    if (result) {
        log_info("game ticked kind={} player={}", static_cast<int>(result->kind), result->player);
    }
    return result;
}

std::vector<std::pair<std::string, std::int64_t>> lexicon_heard(Storage &storage, std::size_t most) {
    return storage.transaction([most](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> heard{state.lexicon.begin(), state.lexicon.end()};
        std::ranges::sort(heard, [](const auto &first, const auto &second) {
            return first.second > second.second;
        });
        if (heard.size() > most) {
            heard.resize(most);
        }
        return heard;
    });
}

void lexicon_hear(Storage &storage, std::string_view message) {
    const std::vector<std::string> words = words_in(message);
    if (words.empty()) {
        return;
    }
    storage.transaction([&words](StorageSession &session) {
        ConquisterState &state = session.state();
        for (const std::string &word : words) {
            if (word.size() < 4 || word.size() > 14) {
                continue;
            }
            const bool letters_only = std::ranges::all_of(word, [](const char letter) {
                return letter >= 'a' && letter <= 'z';
            });
            if (!letters_only) {
                continue;
            }
            state.lexicon[word] = counter(state.lexicon, word) + 1;
        }
        /* Il vocabolario non cresce all'infinito: quando è pieno, tutti i conti si dimezzano e
           chi resta a zero esce. */
        if (state.lexicon.size() > 2000) {
            Counters kept;
            for (const Counters::value_type &entry : state.lexicon) {
                if (entry.second / 2 > 0) {
                    kept.emplace(entry.first, entry.second / 2);
                }
            }
            state.lexicon = std::move(kept);
        }
        return 0;
    });
}

std::vector<std::string> player_names(Storage &storage) {
    return storage.transaction([](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::string> names;
        names.reserve(state.scores.size());
        std::ranges::transform(state.scores, std::back_inserter(names), [](const Counters::value_type &entry) {
            return text::to_lower_copy(entry.first);
        });
        return names;
    });
}

std::vector<std::string> hand_written_words() {
    std::vector<std::string> taken;
    taken.reserve(game_names.size() + forbidden_words.size());
    std::ranges::transform(game_names, std::back_inserter(taken), [](const auto &named) {
        return std::string{named.first};
    });
    std::ranges::transform(forbidden_words, std::back_inserter(taken), [](const std::string_view word) {
        return std::string{word};
    });
    return taken;
}

std::optional<GameOpened> game_open_forged(
    Storage &storage,
    std::int64_t now,
    std::int64_t open_for,
    std::int64_t pot,
    const std::string &keyword
) {
    if (!forge_source(keyword)) {
        return std::nullopt;
    }
    return game_open(storage, now, open_for, pot, Game::forged, keyword);
}

std::optional<std::string> forged_named(std::string_view message) {
    if (!forge_ready()) {
        return std::nullopt;
    }
    const std::vector<std::string> words = words_in(message);
    const auto mine = std::ranges::find_if(words, [](const std::string &word) {
        return forge_knows(word) && forge_source(word).has_value();
    });
    return mine == words.end() ? std::nullopt : std::optional<std::string>{*mine};
}

HandResult play_hand(Storage &storage, const std::string &username, Hand hand) {
    const HandResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        HandResult played;
        played.played = true;
        played.mine = hand;
        played.theirs = static_cast<Hand>(session.random_index(3));
        const int mine = static_cast<int>(hand);
        const int theirs = static_cast<int>(played.theirs);
        played.outcome = mine == theirs ? 0 : ((mine + 1) % 3 == theirs ? -1 : 1);
        played.palle = counter(state.scores, username) / 20;
        if (played.palle < 0) {
            played.palle = 0;
        }
        state.scores[username] = counter(state.scores, username) + (played.outcome * played.palle);
        return played;
    });

    log_info("hand user={} outcome={} palle={}", username, result.outcome, result.palle);
    return result;
}

HandResult play_parity(Storage &storage, const std::string &username, bool even, std::int64_t said) {
    const HandResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        HandResult played;
        played.played = true;
        const auto mine = static_cast<std::int64_t>(session.random_index(6)) + 1;
        played.theirs = static_cast<Hand>(mine % 3);
        const bool total_even = (said + mine) % 2 == 0;
        played.outcome = total_even == even ? 1 : -1;
        played.palle = counter(state.scores, username) / 20;
        if (played.palle < 0) {
            played.palle = 0;
        }
        played.mine = static_cast<Hand>(mine % 3);
        state.scores[username] = counter(state.scores, username) + (played.outcome * played.palle);
        return played;
    });

    log_info("parity user={} outcome={} palle={}", username, result.outcome, result.palle);
    return result;
}

DuelResult duel(Storage &storage, const std::string &username, std::int64_t now) {
    const DuelResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        DuelResult fight;
        std::vector<std::string> others;
        for (const Counters::value_type &entry : state.scores) {
            if (entry.first != username) {
                others.push_back(entry.first);
            }
        }
        if (others.empty()) {
            return fight;
        }
        fight.fought = true;
        fight.other = others[session.random_index(others.size())];
        fight.won = session.random_index(2) == 0;
        const std::string &loser = fight.won ? fight.other : username;
        const std::string &winner = fight.won ? username : fight.other;
        fight.palle = counter(state.scores, loser) / 10;
        if (fight.palle < 0) {
            fight.palle = 0;
        }
        state.scores[loser] = counter(state.scores, loser) - fight.palle;
        state.scores[winner] = counter(state.scores, winner) + fight.palle;
        static_cast<void>(simpatia_change(state, loser, -1, now));
        return fight;
    });

    if (result.fought) {
        log_info("duel user={} other={} won={} palle={}", username, result.other, result.won ? 1 : 0, result.palle);
    }
    return result;
}

bool lottery_open(Storage &storage, std::int64_t now, std::int64_t open_for) {
    const bool opened = storage.transaction([now, open_for](StorageSession &session) {
        ConquisterState &state = session.state();
        if (counter(state.lottery_clock, "closes") > now) {
            return false;
        }
        state.lottery.clear();
        state.lottery_clock["closes"] = now + open_for;
        return true;
    });

    if (opened) {
        log_info("lottery opened for={}", open_for);
    }
    return opened;
}

std::optional<std::int64_t> lottery_buy(
    Storage &storage,
    const std::string &username,
    std::int64_t cost,
    std::int64_t now
) {
    return storage.transaction([&](StorageSession &session) -> std::optional<std::int64_t> {
        ConquisterState &state = session.state();
        if (counter(state.lottery_clock, "closes") <= now) {
            return std::nullopt;
        }
        const std::int64_t tickets = counter(state.lottery, username) + 1;
        state.lottery[username] = tickets;
        state.scores[username] = counter(state.scores, username) - cost;
        state.lottery_clock["pot"] = counter(state.lottery_clock, "pot") + cost;
        return tickets;
    });
}

std::optional<LotteryDraw> lottery_draw(Storage &storage, std::int64_t now) {
    const std::optional<LotteryDraw> result =
        storage.transaction([now](StorageSession &session) -> std::optional<LotteryDraw> {
            ConquisterState &state = session.state();
            const std::int64_t closes = counter(state.lottery_clock, "closes");
            if (closes == 0 || closes > now) {
                return std::nullopt;
            }
            LotteryDraw drawn;
            drawn.pot = counter(state.lottery_clock, "pot");
            state.lottery_clock.erase("closes");
            state.lottery_clock.erase("pot");
            if (state.lottery.empty()) {
                state.lottery.clear();
                return drawn;
            }
            /* One ticket, one chance. */
            std::vector<std::string> bowl;
            for (const Counters::value_type &entry : state.lottery) {
                for (std::int64_t ticket = 0; ticket < entry.second; ++ticket) {
                    bowl.push_back(entry.first);
                }
            }
            drawn.players = state.lottery.size();
            drawn.tickets = bowl.size();
            drawn.winner = bowl[session.random_index(bowl.size())];
            state.scores[drawn.winner] = counter(state.scores, drawn.winner) + drawn.pot;
            state.lottery.clear();
            return drawn;
        });

    if (result && !result->winner.empty()) {
        log_info("lottery won by={} pot={} tickets={}", result->winner, result->pot, result->tickets);
    }
    return result;
}

std::string domino(Storage &storage, const std::string &username, std::int64_t palle) {
    return storage.transaction([&username, palle](StorageSession &session) {
        ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> ranked(state.scores.begin(), state.scores.end());
        std::ranges::sort(ranked, ranks_before);
        const auto mine = std::ranges::find_if(ranked, [&username](const auto &entry) {
            return entry.first == username;
        });
        if (mine == ranked.end() || mine == ranked.begin()) {
            return std::string{};
        }
        const std::string above = std::prev(mine)->first;
        state.scores[above] = counter(state.scores, above) + palle;
        return above;
    });
}

bool is_last(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        const ConquisterState &state = session.state();
        if (state.scores.size() < 2) {
            return false;
        }
        const auto lowest = std::ranges::min_element(state.scores, [](const auto &a, const auto &b) {
            return a.second < b.second;
        });
        return lowest != state.scores.end() && lowest->first == username;
    });
}

void mark_target(Storage &storage, const std::string &username, std::int64_t now) {
    storage.transaction([&username, now](StorageSession &session) {
        session.state().marked[username] = now;
        return 0;
    });
    log_info("marked user={}", username);
}

namespace {

/* Whoever asked for it, if anybody did, and otherwise one of them at random. */
std::string someone(StorageSession &session, ConquisterState &state) {
    if (!state.marked.empty()) {
        const std::string chosen = state.marked.begin()->first;
        state.marked.erase(chosen);
        if (find_entry(state.scores, chosen) != state.scores.end()) {
            return chosen;
        }
    }
    if (state.scores.empty()) {
        return {};
    }
    const std::size_t which = session.random_index(state.scores.size());
    return std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(which))->first;
}

}

std::optional<HappeningResult> happening_strike(Storage &storage, std::int64_t now) {
    const std::optional<HappeningResult> result =
        storage.transaction([now](StorageSession &session) -> std::optional<HappeningResult> {
            ConquisterState &state = session.state();
            if (state.scores.empty()) {
                return std::nullopt;
            }
            HappeningResult happened;
            happened.what = static_cast<Happening>(session.random_index(14));
            switch (happened.what) {
            case Happening::earthquake:
                state.ids.clear();
                happened.players = state.scores.size();
                break;
            case Happening::amnesty:
                happened.players = state.cooldowns.size();
                state.cooldowns.clear();
                break;
            case Happening::rain:
                happened.palle = 100 + static_cast<std::int64_t>(session.random_index(900));
                for (Counters::value_type &entry : state.scores) {
                    entry.second += happened.palle;
                }
                happened.players = state.scores.size();
                break;
            case Happening::inflation:
                for (Counters::value_type &entry : state.scores) {
                    if (entry.second > 0) {
                        entry.second -= entry.second / 10;
                    }
                }
                happened.players = state.scores.size();
                break;
            case Happening::black_market:
                happened.player = someone(session, state);
                if (happened.player.empty()) {
                    return std::nullopt;
                }
                if (find_entry(state.balloons, happened.player) == state.balloons.end()) {
                    state.balloons[happened.player] = 0;
                }
                break;
            case Happening::pedlar: {
                /* Sold something he never asked for, at a price he never agreed to. */
                happened.player = someone(session, state);
                if (happened.player.empty()) {
                    return std::nullopt;
                }
                happened.palle = 1000 + static_cast<std::int64_t>(session.random_index(9000));
                state.scores[happened.player] = counter(state.scores, happened.player) - happened.palle;
                break;
            }
            case Happening::ministry: {
                /* Either the palle are certified as the real thing, or they are seized as fakes. */
                happened.player = someone(session, state);
                if (happened.player.empty()) {
                    return std::nullopt;
                }
                const std::int64_t had = counter(state.scores, happened.player);
                const bool genuine = session.random_index(2) == 0;
                happened.palle = had / 5;
                state.scores[happened.player] = genuine ? had + happened.palle : had - happened.palle;
                happened.players = genuine ? 1 : 0;
                break;
            }
            case Happening::twinning: {
                if (state.scores.size() < 2) {
                    return std::nullopt;
                }
                const std::size_t first = session.random_index(state.scores.size());
                std::size_t second = session.random_index(state.scores.size());
                if (second == first) {
                    second = (first + 1) % state.scores.size();
                }
                const auto one = std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(first));
                const auto other = std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(second));
                std::swap(one->second, other->second);
                happened.player = one->first;
                happened.palle = other->second;
                happened.players = 2;
                break;
            }
            case Happening::mirror: {
                std::vector<std::int64_t> values;
                std::ranges::transform(state.scores, std::back_inserter(values), [](const auto &entry) {
                    return entry.second;
                });
                std::ranges::sort(values);
                std::vector<std::pair<std::string, std::int64_t>> ranked(state.scores.begin(), state.scores.end());
                std::ranges::sort(ranked, ranks_before);
                for (std::size_t which = 0; which < ranked.size(); ++which) {
                    state.scores[ranked[which].first] = values.at(which);
                }
                happened.players = ranked.size();
                break;
            }
            case Happening::luxury_tax: {
                const std::int64_t total = std::accumulate(
                    state.scores.begin(),
                    state.scores.end(),
                    std::int64_t{0},
                    [](std::int64_t so_far, const Counters::value_type &entry) {
                        return so_far + entry.second;
                    }
                );
                const std::int64_t average = total / static_cast<std::int64_t>(state.scores.size());
                for (Counters::value_type &entry : state.scores) {
                    if (entry.second > average) {
                        const std::int64_t due = (entry.second - average) / 20;
                        entry.second -= due;
                        happened.palle += due;
                        happened.players += 1;
                    }
                }
                break;
            }
            case Happening::daylight_saving:
                happened.palle = 3600;
                for (Counters::value_type &entry : state.scores) {
                    entry.second += happened.palle;
                }
                happened.players = state.scores.size();
                break;
            case Happening::strike:
                for (Raid &raid : state.raids) {
                    raid.arrive = std::min(raid.arrive, now);
                    raid.back = std::min(raid.back, now);
                }
                happened.players = state.raids.size();
                break;
            case Happening::rounding:
                for (Counters::value_type &entry : state.scores) {
                    const std::int64_t rest = ((entry.second % 1000) + 1000) % 1000;
                    entry.second += rest >= 500 ? 1000 - rest : -rest;
                }
                happened.players = state.scores.size();
                break;
            case Happening::famine:
                happened.player = someone(session, state);
                if (happened.player.empty()) {
                    return std::nullopt;
                }
                state.boosts.erase(happened.player);
                state.balloons.erase(happened.player);
                state.shields.erase(happened.player);
                break;
            }
            return happened;
        });

    if (result) {
        log_info("happening what={} player={}", static_cast<int>(result->what), result->player);
    }
    return result;
}

std::optional<SpellResult> spell_cast(
    Storage &storage,
    const std::string &username,
    Spell spell,
    std::int64_t now
) {
    const std::optional<SpellResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<SpellResult> {
            ConquisterState &state = session.state();
            SpellResult cast;
            cast.spell = spell;
            const std::int64_t had = counter(state.scores, username);
            switch (spell) {
            case Spell::multiply:
                if (had == 0) {
                    return std::nullopt;
                }
                cast.multiplier = 2 + static_cast<std::int64_t>(session.random_index(4));
                if (std::abs(had) > std::numeric_limits<std::int64_t>::max() / cast.multiplier) {
                    return std::nullopt;
                }
                cast.score = had * cast.multiplier;
                cast.palle = cast.score - had;
                state.scores[username] = cast.score;
                break;
            case Spell::bet:
                if (had == 0) {
                    return std::nullopt;
                }
                cast.score = session.random_index(2) == 0 ? had * 2 : had / 2;
                cast.palle = cast.score - had;
                state.scores[username] = cast.score;
                break;
            case Spell::alms: {
                if (had < 100) {
                    return std::nullopt;
                }
                const auto poorest = std::ranges::min_element(state.scores, [](const auto &a, const auto &b) {
                    return a.second < b.second;
                });
                if (poorest == state.scores.end() || poorest->first == username) {
                    return std::nullopt;
                }
                cast.other = poorest->first;
                cast.palle = 100;
                poorest->second += cast.palle;
                cast.score = had - cast.palle;
                state.scores[username] = cast.score;
                break;
            }
            case Spell::charisma:
                cast.palle = simpatia_change(state, username, 1, now);
                break;
            case Spell::taunt:
                cast.palle = simpatia_change(state, username, -1, now);
                state.marked[username] = now;
                break;
            case Spell::sixseven:
                /* Whatever he had, it now ends in sixty-seven. */
                cast.score = had - (((had % 100) + 100) % 100) + 67;
                cast.palle = cast.score - had;
                state.scores[username] = cast.score;
                break;
            case Spell::blessing:
                /* An indulgence: the debt is wiped and he is well liked again. */
                cast.score = std::max<std::int64_t>(had, 0);
                cast.palle = cast.score - had;
                state.scores[username] = cast.score;
                state.cooldowns.erase(username);
                cast.multiplier = simpatia_change(state, username, simpatia_full, now);
                break;
            }
            return cast;
        });

    if (result) {
        log_info("spell user={} which={} palle={}", username, static_cast<int>(spell), result->palle);
    }
    return result;
}

std::optional<TaxResult> flegyas_strike(Storage &storage, int share) {
    const std::optional<TaxResult> result =
        storage.transaction([share](StorageSession &session) -> std::optional<TaxResult> {
            ConquisterState &state = session.state();
            if (share <= 0 || state.scores.empty()) {
                return std::nullopt;
            }
            const std::string name = someone(session, state);
            const auto player = find_entry(state.scores, name);
            if (player == state.scores.end() || player->second <= 0) {
                return std::nullopt;
            }
            TaxResult taken;
            taken.player = player->first;
            taken.palle = player->second / share;
            player->second -= taken.palle;
            taken.left = player->second;
            return taken;
        });

    if (result) {
        log_info("flegyas user={} palle={} left={}", result->player, result->palle, result->left);
    }
    return result;
}

std::optional<TaxResult> tax_the_leader(Storage &storage, int percent, std::int64_t now) {
    const std::optional<TaxResult> result =
        storage.transaction([percent, now](StorageSession &session) -> std::optional<TaxResult> {
            ConquisterState &state = session.state();
            if (percent <= 0 || state.scores.empty()) {
                return std::nullopt;
            }
            const auto leader = std::ranges::max_element(state.scores, [](const auto &first, const auto &second) {
                return ranks_before(second, first);
            });
            if (leader == state.scores.end() || leader->second <= 0) {
                return std::nullopt;
            }
            const std::int64_t last = counter(state.taxed, leader->first);
            if (last != 0 && now - last < 86400) {
                return std::nullopt;
            }
            TaxResult taken;
            taken.player = leader->first;
            taken.palle = leader->second * percent / 100;
            leader->second -= taken.palle;
            taken.left = leader->second;
            state.taxed[leader->first] = now;
            return taken;
        });

    if (result) {
        log_info("taxed user={} palle={} left={}", result->player, result->palle, result->left);
    }
    return result;
}

namespace {

constexpr std::array rule_names{
    "quote_cost",
    "balloon_cost",
    "boost_cost",
    "boost_multiplier",
    "raid_share",
    "travel_divisor",
    "attack_cost",
    "cooldown_seconds",
};

std::array<std::int64_t *, rule_names.size()> rule_fields(Rules &rules) {
    return {
        &rules.quote_cost,
        &rules.balloon_cost,
        &rules.boost_cost,
        &rules.boost_multiplier,
        &rules.raid_share,
        &rules.travel_divisor,
        &rules.attack_cost,
        &rules.cooldown_seconds,
    };
}

}

Rules rules_now(Storage &storage, const Rules &fallback) {
    return storage.transaction([&fallback](StorageSession &session) {
        const ConquisterState &state = session.state();
        Rules rules = fallback;
        const auto fields = rule_fields(rules);
        for (std::size_t which = 0; which < rule_names.size(); ++which) {
            const auto found = std::ranges::find_if(state.rules, [which](const Counters::value_type &entry) {
                return entry.first == rule_names.at(which);
            });
            if (found != state.rules.end()) {
                *fields.at(which) = found->second;
            }
        }
        return rules;
    });
}

Rules scramble_rules(Storage &storage, const Rules &least, const Rules &most) {
    const Rules drawn = storage.transaction([&least, &most](StorageSession &session) {
        ConquisterState &state = session.state();
        Rules rules;
        Rules floors = least;
        Rules ceilings = most;
        const auto fields = rule_fields(rules);
        const auto lowest = rule_fields(floors);
        const auto highest = rule_fields(ceilings);
        for (std::size_t which = 0; which < rule_names.size(); ++which) {
            const std::int64_t low = *lowest.at(which);
            const std::int64_t high = std::max(*highest.at(which), low);
            const auto span = static_cast<std::size_t>(high - low + 1);
            *fields.at(which) = low + static_cast<std::int64_t>(session.random_index(span));
            state.rules[rule_names.at(which)] = *fields.at(which);
        }
        return rules;
    });

    log_info(
        "rules shuffled quote={} balloon={} boost={}x{} share={} travel={} attack={} cooldown={}",
        drawn.quote_cost,
        drawn.balloon_cost,
        drawn.boost_cost,
        drawn.boost_multiplier,
        drawn.raid_share,
        drawn.travel_divisor,
        drawn.attack_cost,
        drawn.cooldown_seconds
    );
    return drawn;
}

namespace {

/* What one of the new boons does to a player and, when it takes two, to somebody else. */
void grant_boon(StorageSession &session, ConquisterState &state, const std::string &name, Boon boon,
                std::int64_t now, std::int64_t boost) {
    const auto other_than = [&session, &state, &name]() -> std::string {
        std::vector<std::string> others;
        for (const Counters::value_type &entry : state.scores) {
            if (entry.first != name) {
                others.push_back(entry.first);
            }
        }
        if (others.empty()) {
            return {};
        }
        return others[session.random_index(others.size())];
    };

    switch (boon) {
    case Boon::balloon:
        if (find_entry(state.balloons, name) == state.balloons.end()) {
            state.balloons[name] = 0;
        }
        break;
    case Boon::boost:
        state.boosts[name] = boost;
        break;
    case Boon::teleport:
        state.ids.erase(name);
        static_cast<void>(player_id(session, state, name));
        break;
    case Boon::liked:
        static_cast<void>(simpatia_change(state, name, 1, now));
        break;
    case Boon::disliked:
        static_cast<void>(simpatia_change(state, name, -1, now));
        break;
    case Boon::forgiven:
        state.cooldowns.erase(name);
        break;
    case Boon::steal:
        if (const std::string other = other_than(); !other.empty()) {
            state.scores[other] = counter(state.scores, other) - 50;
            state.scores[name] = counter(state.scores, name) + 50;
        }
        break;
    case Boon::donate:
        if (const std::string other = other_than(); !other.empty()) {
            state.scores[other] = counter(state.scores, other) + 50;
            state.scores[name] = counter(state.scores, name) - 50;
        }
        break;
    case Boon::swap:
        if (const std::string other = other_than(); !other.empty()) {
            const std::int64_t mine = counter(state.scores, name);
            state.scores[name] = counter(state.scores, other);
            state.scores[other] = mine;
        }
        break;
    case Boon::pop:
        state.balloons.erase(name);
        state.shields.erase(name);
        break;
    case Boon::flat:
        state.boosts.erase(name);
        break;
    case Boon::marked:
        state.marked[name] = now;
        break;
    case Boon::freed:
        state.reprogrammed.erase(name);
        state.cooldowns.erase(name);
        break;
    case Boon::restored:
        static_cast<void>(simpatia_change(state, name, simpatia_full, now));
        break;
    case Boon::windfall:
        state.scores[name] = counter(state.scores, name) + (counter(state.scores, name) / 10);
        break;
    case Boon::tithe:
        state.scores[name] = counter(state.scores, name) - (counter(state.scores, name) / 10);
        break;
    case Boon::ticket:
        if (counter(state.lottery_clock, "closes") > now) {
            state.lottery[name] = counter(state.lottery, name) + 1;
        }
        break;
    case Boon::grandfather:
    case Boon::halved:
    case Boon::none:
        break;
    }
}

}

std::optional<FlipperResult> flipper_hit(
    Storage &storage,
    const std::string &username,
    int odds,
    std::int64_t now,
    std::int64_t boost
) {
    const std::optional<FlipperResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<FlipperResult> {
            if (odds <= 0 || session.random_index(static_cast<std::size_t>(odds)) != 0) {
                return std::nullopt;
            }
            ConquisterState &state = session.state();
            FlipperResult hit;
            hit.which = session.random_index(flippers.size());
            const Mishap &what = flippers.at(hit.which);
            const std::int64_t before = counter(state.scores, username);
            hit.score = flipper_score_after(before, what);
            hit.palle = hit.score - before;
            state.scores[username] = hit.score;
            grant_boon(session, state, username, what.boon, now, boost);
            hit.score = counter(state.scores, username);
            return hit;
        });

    if (result) {
        log_info("flipper user={} which={} palle={}", username, result->which, result->palle);
    }
    return result;
}

std::vector<FlipperResult> cascade(
    Storage &storage,
    const std::string &username,
    int how_many,
    std::int64_t now,
    std::int64_t boost
) {
    std::vector<FlipperResult> chain;
    for (int strike = 0; strike < how_many; ++strike) {
        if (const std::optional<FlipperResult> hit = flipper_hit(storage, username, 1, now, boost)) {
            chain.push_back(*hit);
        }
    }
    log_info("cascade user={} events={}", username, chain.size());
    return chain;
}

LuckyResult lucky_word_said(Storage &storage, const std::string &username, int swing) {
    const LuckyResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const auto range = static_cast<std::size_t>(std::max(swing, 1) * 2) + 1;
        const std::int64_t change =
            static_cast<std::int64_t>(session.random_index(range)) - std::max(swing, 1);
        const std::int64_t score = counter(state.scores, username) + change;
        state.scores[username] = score;
        return LuckyResult{change, score};
    });

    log_info("lucky word user={} palle={} left={}", username, result.palle, result.score);
    return result;
}

std::optional<MagicResult> magic_word_said(Storage &storage, const std::string &username, int most) {
    const std::optional<MagicResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<MagicResult> {
            ConquisterState &state = session.state();
            const std::int64_t score = counter(state.scores, username);
            if (score == 0) {
                return std::nullopt;
            }
            MagicResult magic;
            magic.multiplier = 2 + static_cast<std::int64_t>(session.random_index(
                static_cast<std::size_t>(std::max(most, 2) - 1)
            ));
            if (std::abs(score) > std::numeric_limits<std::int64_t>::max() / magic.multiplier) {
                return std::nullopt;
            }
            magic.score = score * magic.multiplier;
            state.scores[username] = magic.score;
            return magic;
        });

    if (result) {
        log_info("magic word user={} multiplier={} left={}", username, result->multiplier, result->score);
    }
    return result;
}

std::int64_t telegram_id_of(Storage &storage, const std::string &username) {
    return storage.transaction([&username](StorageSession &session) {
        const Counters::value_type *found = find_ignore_case(session.state().telegram_ids, username);
        return found != nullptr ? found->second : 0;
    });
}

Leaderboard conquister_leaderboard(Storage &storage, std::size_t limit) {
    return storage.transaction([limit](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> ranked(state.scores.begin(), state.scores.end());
        std::ranges::sort(ranked, ranks_before);
        if (limit != 0 && ranked.size() > limit) {
            ranked.resize(limit);
        }
        Leaderboard leaderboard;
        std::ranges::transform(ranked, std::back_inserter(leaderboard.entries), [&state](const auto &entry) {
            return LeaderboardEntry{entry.first, entry.second, counter(state.quotes_added, entry.first)};
        });
        if (state.current && !state.current->username.empty()) {
            leaderboard.current = state.current;
        }
        return leaderboard;
    });
}

std::vector<LeaderboardEntry> conquister_negatives(Storage &storage, std::size_t limit) {
    return storage.transaction([limit](StorageSession &session) {
        const ConquisterState &state = session.state();
        std::vector<std::pair<std::string, std::int64_t>> ranked;
        for (const Counters::value_type &entry : state.scores) {
            if (entry.second < 0) {
                ranked.emplace_back(entry.first, entry.second);
            }
        }
        /* Deepest in the red first, then username in byte order. */
        std::ranges::sort(ranked, [](const auto &first, const auto &second) {
            return first.second != second.second ? first.second < second.second : first.first < second.first;
        });
        if (limit != 0 && ranked.size() > limit) {
            ranked.resize(limit);
        }
        std::vector<LeaderboardEntry> entries;
        std::ranges::transform(ranked, std::back_inserter(entries), [&state](const auto &entry) {
            return LeaderboardEntry{entry.first, entry.second, counter(state.quotes_added, entry.first)};
        });
        return entries;
    });
}

std::optional<ConquisterUser> conquister_user(Storage &storage, std::string_view username) {
    return storage.transaction([username](StorageSession &session) -> std::optional<ConquisterUser> {
        const ConquisterState &state = session.state();
        ConquisterUser user;
        if (state.current && !state.current->username.empty() &&
            text::equals_ignore_case(state.current->username, username)) {
            user.username = state.current->username;
            user.in_conquister = true;
            user.since = state.current->since;
        }
        const Counters::value_type *score = find_ignore_case(state.scores, username);
        const Counters::value_type *added = find_ignore_case(state.quotes_added, username);
        if (!user.in_conquister) {
            if (score == nullptr && added == nullptr) {
                return std::nullopt;
            }
            user.username = score != nullptr ? score->first : added->first;
        }
        if (score != nullptr) {
            const auto ahead = std::ranges::count_if(state.scores, [score](const Counters::value_type &other) {
                return ranks_before(other, *score);
            });
            user.score = score->second;
            user.rank = static_cast<std::size_t>(ahead) + 1;
        }
        if (added != nullptr) {
            user.quotes_added = added->second;
        }
        return user;
    });
}

std::optional<PlayerCard> player_card(
    Storage &storage,
    const std::string &viewer,
    std::string_view username,
    std::int64_t now,
    int travel_divisor
) {
    return storage.transaction([&](StorageSession &session) -> std::optional<PlayerCard> {
        ConquisterState &state = session.state();
        const std::optional<std::string> known = known_player(state, username);
        if (!known) {
            return std::nullopt;
        }
        PlayerCard card;
        card.username = *known;
        if (const Counters::value_type *score = find_ignore_case(state.scores, *known); score != nullptr) {
            card.score = score->second;
            const auto ahead = std::ranges::count_if(state.scores, [score](const Counters::value_type &other) {
                return ranks_before(other, *score);
            });
            card.rank = static_cast<std::size_t>(ahead) + 1;
        }
        card.quotes_added = counter(state.quotes_added, *known);
        card.simpatia = simpatia_now(state, *known, now);
        if (state.current && state.current->username == *known) {
            card.in_conquister = true;
            card.held_seconds = now > state.current->since ? now - state.current->since : 0;
        }
        if (const auto balloon = find_entry(state.balloons, *known); balloon != state.balloons.end()) {
            card.balloon_attempts = static_cast<int>(balloon->second);
        }
        if (const auto shield = find_entry(state.shields, *known);
            shield != state.shields.end() && shield->second > now) {
            card.shield_seconds = shield->second - now;
        }
        card.boost_multiplier = counter(state.boosts, *known);
        if (const auto penalty = find_entry(state.cooldowns, *known);
            penalty != state.cooldowns.end() && penalty->second > now) {
            card.cooldown_seconds = penalty->second - now;
        }
        if (const Raid *raid = raid_of(state, *known); raid != nullptr) {
            card.travelling = true;
            card.carrying = raid->arrived;
            card.travel_target = raid->target;
            card.travel_seconds =
                std::max<std::int64_t>((raid->arrived ? raid->back : raid->arrive) - now, 0);
        }
        if (!text::equals_ignore_case(viewer, *known)) {
            const position::Point mine = position::coordinates_of(player_id(session, state, viewer));
            const position::Point theirs = position::coordinates_of(player_id(session, state, *known));
            card.distance_seconds =
                position::travel_seconds(position::distance(mine, theirs), travel_divisor);
        }
        return card;
    });
}

BalloonResult balloon_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t now,
    std::int64_t shield_seconds
) {
    const BalloonResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (const auto shield = find_entry(state.shields, username); shield != state.shields.end()) {
            if (shield->second > now) {
                return BalloonResult{BalloonStatus::already_owned, score, shield->second - now};
            }
            state.shields.erase(username);
        }
        if (find_entry(state.balloons, username) != state.balloons.end()) {
            return BalloonResult{BalloonStatus::already_owned, score};
        }
        if (find_entry(state.boosts, username) != state.boosts.end()) {
            return BalloonResult{BalloonStatus::has_boost, score};
        }
        if (score < cost) {
            return BalloonResult{BalloonStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        if (shield_seconds > 0) {
            state.shields[username] = now + shield_seconds;
        } else {
            state.balloons[username] = 0;
        }
        return BalloonResult{BalloonStatus::bought, score - cost, shield_seconds};
    });

    if (result.status == BalloonStatus::bought) {
        log_info(
            "balloon bought user={} cost={} left={} shield={}",
            username,
            cost,
            result.available_score,
            result.shield_seconds
        );
    }
    return result;
}


std::optional<MishapResult> mishap_strike(Storage &storage, std::int64_t now, std::int64_t boost) {
    const std::optional<MishapResult> result =
        storage.transaction([&](StorageSession &session) -> std::optional<MishapResult> {
            ConquisterState &state = session.state();
            if (state.scores.empty()) {
                return std::nullopt;
            }
            const std::size_t who = session.random_index(state.scores.size());
            const auto player = std::next(state.scores.begin(), static_cast<std::ptrdiff_t>(who));
            const std::string name = player->first;
            MishapResult mishap;
            mishap.player = name;
            mishap.which = session.random_index(mishaps.size());
            const Mishap &what = mishaps.at(mishap.which);
            mishap.palle = what.palle;
            player->second += what.palle;
            grant_boon(session, state, name, what.boon, now, boost);
            return mishap;
        });

    if (result) {
        log_info("mishap user={} which={} palle={}", result->player, result->which, result->palle);
    }
    return result;
}

RaidResult raid_start(
    Storage &storage,
    std::int64_t user_id,
    const std::string &username,
    std::string_view target,
    std::int64_t now,
    const RaidRules &rules
) {
    const RaidResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        remember_telegram(state, username, user_id);
        RaidResult outcome;
        const bool homewards = text::equals_ignore_case(username, target);
        const bool holds_place = state.current && text::equals_ignore_case(state.current->username, username);
        Raid *travelling = raid_of(state, username);
        /* Naming yourself is the way home. */
        if (homewards) {
            if (travelling != nullptr) {
                /* He turns his back on the raid and rides the rest of the way home. */
                travelling->arrived = true;
                outcome.status = RaidStatus::coming_home;
                outcome.seconds = std::max<std::int64_t>(travelling->back - now, 0);
                return outcome;
            }
            if (holds_place) {
                const std::string holder = state.current->username;
                const Settlement settled =
                    settle_hold(state, holder, state.current->since, now, rules.signs);
                state.current.reset();
                outcome.status = RaidStatus::left_place;
                outcome.earned = settled.earned;
                outcome.boost_multiplier = settled.boost_multiplier;
                outcome.zodiac_percent = settled.zodiac_percent;
                return outcome;
            }
            outcome.status = RaidStatus::home_already;
            return outcome;
        }
        /* Whoever is already on the road only gets told so. */
        if (travelling != nullptr) {
            outcome.status = RaidStatus::already_travelling;
            outcome.seconds = std::max<std::int64_t>(travelling->back - now, 0);
            return outcome;
        }
        /* Whoever holds the place stays in it: leaving would be leaving it behind. */
        if (holds_place) {
            outcome.status = RaidStatus::holding_place;
            return outcome;
        }
        const std::optional<std::string> known = known_player(state, target);
        if (!known) {
            outcome.status = RaidStatus::unknown_target;
            return outcome;
        }
        outcome.target = *known;
        const position::Point home = position::coordinates_of(player_id(session, state, username));
        const position::Point theirs = position::coordinates_of(player_id(session, state, *known));
        outcome.seconds = position::travel_seconds(position::distance(home, theirs), rules.travel_divisor);
        state.raids.push_back(Raid{
            .raider = username,
            .target = *known,
            .arrive = now + outcome.seconds,
            .back = now + (2 * outcome.seconds),
            .arrived = false,
            .loot = 0,
        });
        return outcome;
    });

    if (result.status == RaidStatus::started) {
        log_info("raid started user={} target={} travel={}", username, result.target, result.seconds);
    }
    if (result.status == RaidStatus::left_place) {
        log_info("left the place user={} earned={}", username, result.earned);
    }
    if (result.status == RaidStatus::coming_home) {
        log_info("raid called off user={} home_in={}", username, result.seconds);
    }
    return result;
}

std::vector<RaidEvent> raid_due(Storage &storage, std::int64_t now, const RaidRules &rules) {
    const std::vector<RaidEvent> events = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        std::vector<RaidEvent> settled;
        for (Raid &raid : state.raids) {
            if (!raid.arrived && now >= raid.arrive) {
                raid.arrived = true;
                RaidEvent event{.kind = RaidEvent::Kind::stolen, .raider = raid.raider, .target = raid.target};
                event.seconds = std::max<std::int64_t>(raid.back - now, 0);
                event.target_on_telegram = counter(state.telegram_ids, raid.target) != 0;
                event.raider_on_telegram = counter(state.telegram_ids, raid.raider) != 0;
                /* He is answered like everyone else and gets nowhere, as the owner asked. */
                const bool shadowed = std::ranges::any_of(rules.shadowed, [&raid](const std::string &name) {
                    return text::equals_ignore_case(name, raid.raider);
                });
                const bool guarded = shadowed || !is_away(state, raid.target);
                event.undefended = !guarded;
                const auto shield = find_entry(state.shields, raid.target);
                const auto balloon = find_entry(state.balloons, raid.target);
                if (shadowed) {
                    event.kind = RaidEvent::Kind::defended;
                    event.undefended = false;
                    event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                } else if (guarded && shield != state.shields.end() && shield->second > now) {
                    event.kind = RaidEvent::Kind::defended;
                    event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                } else if (guarded && balloon != state.balloons.end()) {
                    const std::int64_t attempt = balloon->second + 1;
                    if (static_cast<std::int64_t>(session.random_index(balloon_attempts)) >= attempt) {
                        balloon->second = attempt;
                        event.kind = RaidEvent::Kind::defended;
                        event.cost = charge_attacker(state, raid.raider, rules.attack_cost);
                    } else {
                        state.balloons.erase(raid.target);
                        event.balloon_popped = true;
                    }
                }
                if (event.kind == RaidEvent::Kind::stolen) {
                    const std::int64_t theirs = counter(state.scores, raid.target);
                    event.raider_percent = zodiac::percent_for(raid.raider, now, rules.signs);
                    event.target_percent = zodiac::percent_for(raid.target, now, rules.signs);
                    /* Nothing to carry away from somebody who owns less than nothing. */
                    if (theirs > 0 && rules.loot_share > 0) {
                        const std::int64_t share = theirs / rules.loot_share;
                        event.loot = std::min(theirs, share * event.raider_percent / event.target_percent);
                    }
                    if (event.loot > 0) {
                        state.scores[raid.target] = theirs - event.loot;
                        /* Robbing people is not the way to be liked; being robbed earns some. */
                        event.simpatia = simpatia_change(state, raid.raider, -1, now);
                        event.denounced = event.simpatia < simpatia_threshold &&
                                          event.simpatia + 1 >= simpatia_threshold;
                        static_cast<void>(simpatia_change(state, raid.target, 1, now));
                    }
                    raid.loot = event.loot;
                }
                settled.push_back(std::move(event));
            }
            if (raid.arrived && now >= raid.back) {
                const std::int64_t carried = counter(state.scores, raid.raider);
                if (raid.loot > 0) {
                    state.scores[raid.raider] = carried + raid.loot;
                }
                if (raid.loot > 0) {
                    state.loot_from[raid.raider] = raid.target;
                    state.loot_amount[raid.raider] = raid.loot;
                    state.loot_when[raid.raider] = now;
                }
                settled.push_back(RaidEvent{
                    .kind = RaidEvent::Kind::returned,
                    .raider = raid.raider,
                    .target = raid.target,
                    .loot = raid.loot,
                    .raider_on_telegram = counter(state.telegram_ids, raid.raider) != 0,
                });
            }
        }
        std::erase_if(state.raids, [now](const Raid &raid) { return raid.arrived && now >= raid.back; });
        return settled;
    });

    for (const RaidEvent &event : events) {
        switch (event.kind) {
        case RaidEvent::Kind::stolen:
            log_info(
                "raid stolen user={} target={} loot={} undefended={} balloon_popped={} percent={}/{}",
                event.raider,
                event.target,
                event.loot,
                event.undefended ? 1 : 0,
                event.balloon_popped ? 1 : 0,
                event.raider_percent,
                event.target_percent
            );
            break;
        case RaidEvent::Kind::defended:
            log_info("raid defended user={} target={} cost={}", event.raider, event.target, event.cost);
            break;
        case RaidEvent::Kind::returned:
            log_info("raid returned user={} target={} loot={}", event.raider, event.target, event.loot);
            break;
        }
    }
    return events;
}

BoostResult boost_buy(
    Storage &storage,
    const std::string &username,
    int cost,
    std::int64_t multiplier,
    std::int64_t now
) {
    const BoostResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (const auto boost = find_entry(state.boosts, username); boost != state.boosts.end()) {
            return BoostResult{BoostStatus::already_owned, score, boost->second};
        }
        if (find_entry(state.balloons, username) != state.balloons.end()) {
            return BoostResult{BoostStatus::has_balloon, score};
        }
        if (const auto shield = find_entry(state.shields, username); shield != state.shields.end()) {
            if (shield->second > now) {
                return BoostResult{BoostStatus::has_balloon, score};
            }
            state.shields.erase(username);
        }
        if (score < cost) {
            return BoostResult{BoostStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        state.boosts[username] = multiplier;
        return BoostResult{BoostStatus::bought, score - cost, multiplier};
    });

    if (result.status == BoostStatus::bought) {
        log_info(
            "boost bought user={} cost={} left={} multiplier={}",
            username,
            cost,
            result.available_score,
            result.multiplier
        );
    }
    return result;
}

QuoteAddResult quote_pretend(Storage &storage, const std::string &username, int cost) {
    const QuoteAddResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        const std::int64_t score = counter(state.scores, username);
        if (score < cost) {
            return QuoteAddResult{QuoteAddStatus::insufficient_score, score};
        }
        state.scores[username] = score - cost;
        return QuoteAddResult{QuoteAddStatus::added, score - cost};
    });

    if (result.status == QuoteAddStatus::added) {
        log_info("quote dropped user={} cost={} left={}", username, cost, result.available_score);
    }
    return result;
}

QuoteAddResult quote_add(
    Storage &storage,
    const std::string &username,
    const std::string &quote,
    int cost,
    std::int64_t now
) {
    const QuoteAddResult result = storage.transaction([&](StorageSession &session) {
        ConquisterState &state = session.state();
        Quotes &quotes = session.quotes();
        const std::int64_t score = counter(state.scores, username);
        if (score < cost) {
            return QuoteAddResult{QuoteAddStatus::insufficient_score, score};
        }
        if (std::ranges::find(quotes, quote) != quotes.end()) {
            return QuoteAddResult{QuoteAddStatus::duplicate, score};
        }
        const std::int64_t added = counter(state.quotes_added, username);
        if (added == std::numeric_limits<std::int64_t>::max()) {
            throw StorageError("Quote counter overflow");
        }
        quotes.push_back(quote);
        state.quote_authors[quote] = username;
        static_cast<void>(simpatia_change(state, username, 1, now));
        state.scores[username] = score - cost;
        state.quotes_added[username] = added + 1;
        return QuoteAddResult{QuoteAddStatus::added, score - cost};
    });

    if (result.status == QuoteAddStatus::added) {
        log_info("quote added user={} cost={} left={}", username, cost, result.available_score);
    }
    return result;
}

QuotePage quote_page_load(Storage &storage, int requested_page) {
    return storage.transaction([requested_page](StorageSession &session) {
        const Quotes &quotes = session.quotes();
        QuotePage page;
        page.total = quotes.size();
        if (page.total == 0) {
            return page;
        }
        page.pages = ((page.total - 1) / quotes_page_size) + 1;
        page.page = std::min(requested_page > 0 ? static_cast<std::size_t>(requested_page) : 1U, page.pages);
        const std::size_t offset = (page.page - 1) * quotes_page_size;
        page.first_number = offset + 1;
        page.items = quotes | std::views::drop(offset) | std::views::take(quotes_page_size) |
                     std::ranges::to<std::vector<std::string>>();
        const Authors &authors = session.state().quote_authors;
        std::ranges::transform(page.items, std::back_inserter(page.authors), [&authors](const std::string &quote) {
            const auto found = authors.find(quote);
            return found != authors.end() ? found->second : std::string{};
        });
        return page;
    });
}

std::optional<std::string> quote_random(Storage &storage) {
    return storage.transaction([](StorageSession &session) -> std::optional<std::string> {
        const Quotes &quotes = session.quotes();
        if (quotes.empty()) {
            return std::nullopt;
        }
        return quotes[session.random_index(quotes.size())];
    });
}

std::optional<std::string> quote_delete(Storage &storage, std::string_view selector) {
    return storage.transaction([selector](StorageSession &session) -> std::optional<std::string> {
        Quotes &quotes = session.quotes();
        const std::optional<std::int64_t> position = text::parse_int64(selector);
        const auto selected = position && *position > 0 && static_cast<std::uint64_t>(*position) <= quotes.size()
            ? quotes.begin() + static_cast<std::ptrdiff_t>(*position - 1)
            : std::ranges::find(quotes, std::string{selector});
        if (selected == quotes.end()) {
            return std::nullopt;
        }
        std::string removed = std::move(*selected);
        quotes.erase(selected);
        ConquisterState &state = session.state();
        /* A quote that is deleted no longer counts for whoever added it. */
        if (const auto author = state.quote_authors.find(removed); author != state.quote_authors.end()) {
            if (const std::int64_t added = counter(state.quotes_added, author->second); added > 0) {
                state.quotes_added[author->second] = added - 1;
            }
            state.quote_authors.erase(removed);
        }
        return removed;
    });
}

}
