#include "forge.hpp"

#include "logging.hpp"
#include "text.hpp"

#include <algorithm>
#include <iterator>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <system_error>

namespace norelecbot {
namespace {

/* Un gioco in catalogo: dove sta il suo codice e come si presenta. */
struct Forged {
    std::string announce;
    std::int64_t born = 0;
    bool broken = false;
};

struct Catalogue {
    std::mutex lock;
    std::string directory;
    LuaLimits limits;
    bool ready = false;
    std::map<std::string, Forged> games;
    /* Le parole in ordine di nascita, la più recente in fondo. */
    std::vector<std::string> order;
};

Catalogue &catalogue() {
    static Catalogue only;
    return only;
}

std::string replace_all(std::string text, std::string_view mark, std::string_view with) {
    std::size_t at = text.find(mark);
    while (at != std::string::npos) {
        text.replace(at, mark.size(), with);
        at = text.find(mark, at + with.size());
    }
    return text;
}

/* Una stringa che si possa mettere fra virgolette dentro il Lua senza farlo esplodere. */
std::string lua_safe(std::string_view text) {
    std::string safe;
    for (const char letter : text) {
        if (letter == '"' || letter == '\\') {
            continue;
        }
        if (letter == '\n' || letter == '\r') {
            safe.push_back(' ');
            continue;
        }
        safe.push_back(letter);
    }
    return safe;
}

std::string upper_ascii(std::string_view text) {
    std::string loud;
    loud.reserve(text.size());
    std::ranges::transform(text, std::back_inserter(loud), [](const char letter) {
        return letter >= 'a' && letter <= 'z' ? static_cast<char>(letter - 'a' + 'A') : letter;
    });
    return loud;
}

bool only_letters(std::string_view word) {
    return !word.empty() && std::ranges::all_of(word, [](const char letter) {
        return letter >= 'a' && letter <= 'z';
    });
}

/* Due parole si pestano i piedi se una contiene l'altra: chi dice carta dice anche cart. */
bool clashes(std::string_view word, std::string_view taken) {
    return word.find(taken) != std::string_view::npos || taken.find(word) != std::string_view::npos;
}

std::filesystem::path script_path(const Catalogue &shelf, const std::string &keyword) {
    return std::filesystem::path{shelf.directory} / (keyword + ".lua");
}

std::filesystem::path index_path(const Catalogue &shelf) {
    return std::filesystem::path{shelf.directory} / "index.jsonl";
}

std::filesystem::path broken_path(const Catalogue &shelf) {
    return std::filesystem::path{shelf.directory} / "broken.txt";
}

/* L'indice è una riga per gioco, aggiunta in coda alla nascita: così non si riscrive mai niente,
   per quanti giochi ci siano. */
void write_index_line(const Catalogue &shelf, const ForgedGame &game, std::int64_t born) {
    std::ofstream index{index_path(shelf), std::ios::binary | std::ios::app};
    if (!index) {
        log_error("Could not append to the forge index");
        return;
    }
    index << std::format(
        R"({{"k":"{}","a":"{}","t":{}}})",
        game.keyword,
        lua_safe(game.announce),
        born
    ) << '\n';
}

std::string json_string_field(std::string_view line, std::string_view field) {
    const std::string mark = std::format(R"("{}":")", field);
    const std::size_t at = line.find(mark);
    if (at == std::string_view::npos) {
        return {};
    }
    const std::size_t from = at + mark.size();
    const std::size_t to = line.find('"', from);
    return to == std::string_view::npos ? std::string{} : std::string{line.substr(from, to - from)};
}

std::int64_t json_number_field(std::string_view line, std::string_view field) {
    const std::string mark = std::format(R"("{}":)", field);
    const std::size_t at = line.find(mark);
    if (at == std::string_view::npos) {
        return 0;
    }
    std::size_t from = at + mark.size();
    std::int64_t found = 0;
    while (from < line.size() && line.at(from) >= '0' && line.at(from) <= '9') {
        found = found * 10 + (line.at(from) - '0');
        ++from;
    }
    return found;
}

/* Lo stato finto su cui si collauda un gioco appena coniato. */
struct Bench {
    std::map<std::string, std::int64_t> numbers;
    std::map<std::string, std::string> names;
    std::uint64_t seed = 12345;
    std::int64_t paid = 0;
};

LuaBotView bench_view(Bench &bench) {
    LuaBotView view;
    view.pot = 5000;
    view.score = [](const std::string &) { return std::int64_t{10000}; };
    view.players = []() { return std::vector<std::string>{"collaudo", "banco"}; };
    view.random = [&bench](std::int64_t count) {
        /* Un generatore piccolo e senza sorprese: serve solo a far girare il collaudo. */
        bench.seed = (bench.seed * 6364136223846793005ULL) + 1442695040888963407ULL;
        const std::uint64_t drawn = bench.seed >> 33U;
        return static_cast<std::int64_t>(drawn % static_cast<std::uint64_t>(std::max<std::int64_t>(count, 1)));
    };
    view.get = [&bench](const std::string &key) {
        const auto found = bench.numbers.find(key);
        return found == bench.numbers.end() ? std::int64_t{0} : found->second;
    };
    view.set = [&bench](const std::string &key, std::int64_t value) { bench.numbers[key] = value; };
    view.name = [&bench](const std::string &key) {
        const auto found = bench.names.find(key);
        return found == bench.names.end() ? std::string{} : found->second;
    };
    view.setname = [&bench](const std::string &key, const std::string &value) { bench.names[key] = value; };
    return view;
}

bool run_bench(
    const std::string &source,
    std::string_view moment,
    const LuaBotView &view,
    const std::vector<LuaArg> &args,
    Bench &bench,
    std::string &why_not
) {
    const LuaRun run = lua_invoke(source, moment, view, args, forge_limits());
    if (!run.ok) {
        why_not = run.error;
        return false;
    }
    for (const LuaEffect &effect : run.effects) {
        if (effect.kind == LuaEffect::Kind::pay || effect.kind == LuaEffect::Kind::take) {
            bench.paid += effect.palle;
        }
    }
    return true;
}

/* Le meccaniche che la forgia sa scrivere. Ognuna esce come sorgente Lua vero. */
std::string mechanic_guess(std::string_view keyword, std::int64_t roof, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "🎲 @LOUD@: penso un numero fra 1 e @ROOF@. Chi lo indovina si prende il piatto. @FLAVOUR@"
function G.open(bot)
    bot.set("secret", bot.random(@ROOF@) + 1)
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said ~= bot.get("secret") then return end
    bot.pay(who, bot.pot())
    bot.say("🎲 " .. who .. " ha detto " .. said .. " e si prende " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot)
    bot.say("🎲 Il numero era " .. bot.get("secret") .. " e non l'ha preso nessuno.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@ROOF@", std::format("{}", roof));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_closest(std::string_view keyword, std::int64_t roof, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "👁️ @LOUD@: numero fra 1 e @ROOF@, vince chi ci va più vicino alla chiusura. @FLAVOUR@"
function G.open(bot)
    bot.set("secret", bot.random(@ROOF@) + 1)
    bot.set("best", @ROOF@ * 10)
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    local away = said - bot.get("secret")
    if away < 0 then away = -away end
    if away >= bot.get("best") then return end
    bot.set("best", away)
    bot.setname("leader", who)
    bot.say("👁️ " .. who .. " dice " .. said .. " ed è il più vicino per ora.")
end
function G.close(bot)
    local leader = bot.name("leader")
    if leader == "" then
        bot.say("👁️ Il numero era " .. bot.get("secret") .. " e non ci ha provato nessuno.")
        return
    end
    bot.pay(leader, bot.pot())
    bot.say("👁️ Il numero era " .. bot.get("secret") .. ": vince " .. leader .. " con " .. bot.pot() .. " palle.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@ROOF@", std::format("{}", roof));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_word(std::string_view keyword, std::string_view hidden, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "🔎 @LOUD@: ho in mente una parola di @LEN@ lettere che questo gruppo dice spesso. Chi la scrive incassa. @FLAVOUR@"
function G.open(bot)
    bot.setname("word", "@HIDDEN@")
    bot.set("tries", 0)
end
function G.message(bot, who, text)
    local said = bot.lower(text)
    bot.set("tries", bot.get("tries") + 1)
    if string.find(said, bot.name("word"), 1, true) == nil then return end
    bot.pay(who, bot.pot())
    bot.say("🔎 " .. who .. " ha detto la parola al tentativo numero " .. bot.get("tries") .. ": " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot)
    bot.say("🔎 La parola era " .. bot.name("word") .. ".")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@HIDDEN@", hidden);
    source = replace_all(std::move(source), "@LEN@", std::format("{}", hidden.size()));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_collect(std::string_view keyword, std::int64_t target, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "🧮 @LOUD@: sommate numeri fino ad arrivare esatti a @TARGET@. Chi sfora paga, chi centra incassa. @FLAVOUR@"
function G.open(bot)
    bot.set("total", 0)
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    local total = bot.get("total") + said
    if total > @TARGET@ then
        bot.take(who, 200)
        bot.say("🧮 " .. who .. " sfora con " .. total .. " e lascia 200 palle.")
        bot.close()
        return
    end
    bot.set("total", total)
    if total == @TARGET@ then
        bot.pay(who, bot.pot())
        bot.say("🧮 " .. who .. " centra @TARGET@ e si prende " .. bot.pot() .. " palle.")
        bot.close()
        return
    end
    bot.say("🧮 Siamo a " .. total .. " su @TARGET@.")
end
function G.close(bot)
    bot.say("🧮 Ci siamo fermati a " .. bot.get("total") .. " su @TARGET@.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@TARGET@", std::format("{}", target));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_vote(
    std::string_view keyword,
    std::string_view first,
    std::string_view second,
    std::string_view flavour
) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "⚖️ @LOUD@: scrivete @FIRST@ oppure @SECOND@. Alla chiusura ne tiro a sorte una: chi stava da quella parte si divide il piatto. @FLAVOUR@"
function G.open(bot)
    bot.set("first", 0)
    bot.set("second", 0)
end
function G.message(bot, who, text)
    local said = bot.lower(text)
    if bot.get("v:" .. who) ~= 0 then return end
    if string.find(said, "@FIRST@", 1, true) ~= nil then
        bot.set("v:" .. who, 1)
        bot.set("first", bot.get("first") + 1)
        bot.say("⚖️ " .. who .. " sta con @FIRST@.")
        return
    end
    if string.find(said, "@SECOND@", 1, true) ~= nil then
        bot.set("v:" .. who, 2)
        bot.set("second", bot.get("second") + 1)
        bot.say("⚖️ " .. who .. " sta con @SECOND@.")
    end
end
function G.close(bot)
    local winning = bot.random(2) + 1
    local howmany = bot.get("first")
    local side = "@FIRST@"
    if winning == 2 then
        howmany = bot.get("second")
        side = "@SECOND@"
    end
    if howmany < 1 then
        local other = 3 - winning
        local names = bot.players()
        local consolation = bot.pot() // 4
        for i = 1, #names do
            if bot.get("v:" .. names[i]) == other then
                bot.pay(names[i], consolation)
            end
        end
        bot.say("⚖️ Esce " .. side .. " e non ci stava nessuno: " .. consolation .. " palle di consolazione agli altri.")
        return
    end
    local share = bot.pot() // howmany
    local names = bot.players()
    for i = 1, #names do
        if bot.get("v:" .. names[i]) == winning then
            bot.pay(names[i], share)
        end
    end
    bot.say("⚖️ Esce " .. side .. ": " .. share .. " palle a testa a chi ci stava.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@FIRST@", first);
    source = replace_all(std::move(source), "@SECOND@", second);
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_gamble(std::string_view keyword, std::int64_t faces, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "🎰 @LOUD@: scrivete un numero da 1 a @FACES@, tiro subito. Chi indovina incassa, chi sbaglia lascia qualcosa. @FLAVOUR@"
function G.open(bot)
    bot.set("giri", 0)
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > @FACES@ then return end
    if bot.get("g:" .. who) ~= 0 then return end
    bot.set("g:" .. who, 1)
    bot.set("giri", bot.get("giri") + 1)
    local drawn = bot.random(@FACES@) + 1
    if drawn == said then
        bot.pay(who, bot.pot())
        bot.say("🎰 Esce " .. drawn .. "! " .. who .. " si prende " .. bot.pot() .. " palle.")
        bot.close()
        return
    end
    bot.take(who, 100)
    bot.say("🎰 Esce " .. drawn .. ": " .. who .. " aveva detto " .. said .. " e lascia 100 palle.")
end
function G.close(bot)
    bot.say("🎰 La ruota si ferma dopo " .. bot.get("giri") .. " giri.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@FACES@", std::format("{}", faces));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

std::string mechanic_countdown(std::string_view keyword, std::int64_t step, std::string_view flavour) {
    std::string source = R"LUA(local G = {}
G.keyword = "@KEY@"
G.announce = "⏳ @LOUD@: il premio parte pieno e cala di @STEP@ palle ogni volta che il tempo gira. Il primo che scrive si prende quello che resta. @FLAVOUR@"
function G.open(bot)
    bot.set("prize", bot.pot())
end
function G.message(bot, who, text)
    local prize = bot.get("prize")
    if prize < 1 then
        bot.say("⏳ Il premio è finito prima che " .. who .. " arrivasse.")
        bot.close()
        return
    end
    bot.pay(who, prize)
    bot.say("⏳ " .. who .. " arriva quando restano " .. prize .. " palle e se le prende.")
    bot.close()
end
function G.tick(bot, now)
    local prize = bot.get("prize") - @STEP@
    if prize < 0 then prize = 0 end
    bot.set("prize", prize)
    bot.say("⏳ Restano " .. prize .. " palle.")
end
function G.close(bot)
    bot.say("⏳ Tempo scaduto con " .. bot.get("prize") .. " palle ancora sul tavolo.")
end
return G
)LUA";
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@LOUD@", upper_ascii(keyword));
    source = replace_all(std::move(source), "@STEP@", std::format("{}", step));
    return replace_all(std::move(source), "@FLAVOUR@", flavour);
}

/* Qualche parola del gruppo da mettere nell'annuncio, giusto per far sentire che è roba loro. */
std::string flavour_from(const ForgeRequest &request, const RandomSource &random) {
    if (request.lexicon.size() < 2) {
        return "Si gioca scrivendo.";
    }
    const auto first = request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size()))));
    const auto second = request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size()))));
    return lua_safe(std::format("In palio l'onore di dire {} prima di {}.", first.first, second.first));
}

/* Una parola che nessuno sta già usando: prima si guarda cosa dice il gruppo, poi si conia. */
std::optional<std::string> choose_keyword(const ForgeRequest &request, const RandomSource &random) {
    const auto free_word = [&request](const std::string &word) {
        if (!only_letters(word) || word.size() < 4 || word.size() > 14) {
            return false;
        }
        if (forge_knows(word)) {
            return false;
        }
        return std::ranges::none_of(request.reserved, [&word](const std::string &taken) {
            return !taken.empty() && clashes(word, taken);
        });
    };

    std::vector<std::string> usable;
    for (const std::pair<std::string, std::int64_t> &heard : request.lexicon) {
        if (heard.second >= 3 && free_word(heard.first)) {
            usable.push_back(heard.first);
        }
    }
    if (!usable.empty()) {
        return usable.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(usable.size()))));
    }

    /* Niente di buono nel parlato: si conia unendo due pezzi di quello che si è sentito. */
    std::vector<std::string> pieces;
    for (const std::pair<std::string, std::int64_t> &heard : request.lexicon) {
        if (only_letters(heard.first) && heard.first.size() >= 4) {
            pieces.push_back(heard.first);
        }
    }
    constexpr std::array<std::string_view, 8> spare{
        "parla", "conta", "salta", "tocca", "gira", "pesca", "punta", "rompi"
    };
    for (int attempt = 0; attempt < 60; ++attempt) {
        std::string coined;
        if (pieces.size() >= 2) {
            const std::string &head = pieces.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(pieces.size()))));
            const std::string &tail = pieces.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(pieces.size()))));
            coined = head.substr(0, 3 + static_cast<std::size_t>(random(3))) +
                     tail.substr(tail.size() / 2);
        } else {
            coined = std::string{spare.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(spare.size()))))} +
                     std::string{"aeiou"}.substr(static_cast<std::size_t>(random(5)), 1) +
                     std::string{"lnrstz"}.substr(static_cast<std::size_t>(random(6)), 1) +
                     "o";
        }
        if (coined.size() > 14) {
            coined.resize(14);
        }
        if (free_word(coined)) {
            return coined;
        }
    }
    return std::nullopt;
}

std::string write_one(const ForgeRequest &request, const std::string &keyword, const RandomSource &random) {
    const std::string flavour = flavour_from(request, random);
    std::string hidden = "palle";
    if (!request.lexicon.empty()) {
        const auto &picked =
            request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size()))));
        if (only_letters(picked.first) && picked.first.size() >= 4) {
            hidden = picked.first;
        }
    }
    std::string first = "sì";
    std::string second = "no";
    if (request.lexicon.size() >= 4) {
        first = lua_safe(request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size())))).first);
        second = lua_safe(request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size())))).first);
        if (first == second) {
            second = "no";
        }
    }

    switch (random(6)) {
    case 0:
        return mechanic_guess(keyword, 20 + random(180), flavour);
    case 1:
        return mechanic_closest(keyword, 100 + random(900), flavour);
    case 2:
        return mechanic_word(keyword, hidden, flavour);
    case 3:
        return mechanic_collect(keyword, 30 + random(120), flavour);
    case 4:
        return mechanic_vote(keyword, first, second, flavour);
    default:
        return random(2) == 0 ? mechanic_gamble(keyword, 3 + random(8), flavour)
                              : mechanic_countdown(keyword, 200 + random(800), flavour);
    }
}

}

void forge_open_catalogue(std::string directory, LuaLimits limits) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    shelf.directory = std::move(directory);
    shelf.limits = limits;
    shelf.games.clear();
    shelf.order.clear();

    std::error_code trouble;
    std::filesystem::create_directories(shelf.directory, trouble);
    if (trouble) {
        log_error("Could not open the forge at {}: {}", shelf.directory, trouble.message());
        shelf.ready = false;
        return;
    }

    std::ifstream index{index_path(shelf), std::ios::binary};
    std::string line;
    while (std::getline(index, line)) {
        const std::string keyword = json_string_field(line, "k");
        if (keyword.empty()) {
            continue;
        }
        shelf.games[keyword] = Forged{
            .announce = json_string_field(line, "a"),
            .born = json_number_field(line, "t"),
            .broken = false,
        };
        shelf.order.push_back(keyword);
    }
    std::ifstream broken{broken_path(shelf), std::ios::binary};
    while (std::getline(broken, line)) {
        const auto found = shelf.games.find(line);
        if (found != shelf.games.end()) {
            found->second.broken = true;
        }
    }
    shelf.ready = true;
    log_info("Forge ready at {} with {} games", shelf.directory, shelf.games.size());
}

bool forge_ready() {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    return shelf.ready;
}

std::size_t forge_count() {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    return shelf.games.size();
}

std::vector<std::string> forge_recent(std::size_t most) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    std::vector<std::string> newest;
    for (const std::string &keyword : shelf.order | std::views::reverse) {
        if (newest.size() >= most) {
            break;
        }
        newest.push_back(keyword);
    }
    return newest;
}

bool forge_knows(const std::string &keyword) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    return shelf.games.find(keyword) != shelf.games.end();
}

std::optional<std::string> forge_source(const std::string &keyword) {
    Catalogue &shelf = catalogue();
    std::filesystem::path where;
    {
        const std::lock_guard held{shelf.lock};
        const auto found = shelf.games.find(keyword);
        if (!shelf.ready || found == shelf.games.end() || found->second.broken) {
            return std::nullopt;
        }
        where = script_path(shelf, keyword);
    }
    std::ifstream file{where, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

std::optional<std::string> forge_announce_of(const std::string &keyword) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    const auto found = shelf.games.find(keyword);
    if (found == shelf.games.end() || found->second.announce.empty()) {
        return std::nullopt;
    }
    return found->second.announce;
}

std::optional<std::string> forge_any(const RandomSource &random) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    if (!shelf.ready || shelf.order.empty() || !random) {
        return std::nullopt;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::string &keyword =
            shelf.order.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(shelf.order.size()))));
        const auto found = shelf.games.find(keyword);
        if (found != shelf.games.end() && !found->second.broken) {
            return keyword;
        }
    }
    return std::nullopt;
}

LuaLimits forge_limits() {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    return shelf.limits;
}

void forge_condemn(const std::string &keyword, std::string_view why) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    const auto found = shelf.games.find(keyword);
    if (found == shelf.games.end() || found->second.broken) {
        return;
    }
    found->second.broken = true;
    std::ofstream broken{broken_path(shelf), std::ios::binary | std::ios::app};
    if (broken) {
        broken << keyword << '\n';
    }
    log_error("Forged game {} is out of service: {}", keyword, why);
}

bool forge_try_out(const std::string &source, std::string &why_not) {
    const std::optional<std::string> keyword = lua_field(source, "keyword", forge_limits());
    const std::optional<std::string> announce = lua_field(source, "announce", forge_limits());
    if (!keyword || keyword->empty() || !announce || announce->empty()) {
        why_not = "manca la parola chiave o l'annuncio";
        return false;
    }

    Bench bench;
    const LuaBotView view = bench_view(bench);
    if (!run_bench(source, "open", view, {}, bench, why_not)) {
        return false;
    }
    /* Le risposte giuste stanno nello stato: si prova con tutte, numeri e parole. */
    std::vector<std::string> probes{"ciao a tutti", "7"};
    std::ranges::transform(bench.numbers, std::back_inserter(probes), [](const auto &kept) {
        return std::format("dico {}", kept.second);
    });
    std::ranges::transform(bench.names, std::back_inserter(probes), [](const auto &kept) {
        return std::format("dico {}", kept.second);
    });
    const bool every_probe_held = std::ranges::all_of(probes, [&](const std::string &probe) {
        return run_bench(source, "message", view, {std::string{"collaudo"}, probe}, bench, why_not);
    });
    if (!every_probe_held) {
        return false;
    }
    if (!run_bench(source, "tick", view, {std::int64_t{1000}}, bench, why_not)) {
        return false;
    }
    if (!run_bench(source, "close", view, {}, bench, why_not)) {
        return false;
    }
    if (bench.paid == 0) {
        why_not = "non muove mai una palla";
        return false;
    }
    return true;
}

std::optional<ForgedGame> forge_mint(const ForgeRequest &request, const RandomSource &random) {
    Catalogue &shelf = catalogue();
    {
        const std::lock_guard held{shelf.lock};
        if (!shelf.ready) {
            return std::nullopt;
        }
    }
    for (int attempt = 0; attempt < 10; ++attempt) {
        const std::optional<std::string> keyword = choose_keyword(request, random);
        if (!keyword) {
            continue;
        }
        ForgedGame game;
        game.keyword = *keyword;
        game.source = write_one(request, game.keyword, random);
        std::string why_not;
        if (!forge_try_out(game.source, why_not)) {
            log_error("Forged game {} failed its trial run: {}", game.keyword, why_not);
            continue;
        }
        const std::optional<std::string> announce = lua_field(game.source, "announce", forge_limits());
        if (!announce) {
            continue;
        }
        game.announce = *announce;

        const std::lock_guard held{shelf.lock};
        std::ofstream file{script_path(shelf, game.keyword), std::ios::binary};
        if (!file) {
            log_error("Could not write the forged game {}", game.keyword);
            return std::nullopt;
        }
        file << game.source;
        file.close();
        const std::int64_t born = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch()
                                  ).count();
        write_index_line(shelf, game, born);
        shelf.games[game.keyword] = Forged{.announce = game.announce, .born = born, .broken = false};
        shelf.order.push_back(game.keyword);
        log_info("Forged a new game: {}", game.keyword);
        return game;
    }
    return std::nullopt;
}

}
