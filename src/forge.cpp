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
    std::string family;
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

std::filesystem::path verdicts_path(const Catalogue &shelf) {
    return std::filesystem::path{shelf.directory} / "verdicts.jsonl";
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
        R"({{"k":"{}","f":"{}","a":"{}","t":{}}})",
        game.keyword,
        game.family,
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

/* Le regole che la forgia sa scrivere: ognuna esce come sorgente Lua vero. */
struct Recipe {
    std::string_view family;
    std::int64_t low = 0;
    std::int64_t high = 0;
    std::string_view announce;
    std::string_view body;
};

constexpr auto recipes = std::to_array<Recipe>({
    Recipe{"guess", 20, 200,
           "@EMOJI@ @TITLE@: penso un numero fra 1 e @A@. Chi lo indovina si prende il piatto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("secret", bot.random(@A@) + 1) end
function G.message(bot, who, text)
    if bot.number(text) ~= bot.get("secret") then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " ha detto " .. bot.get("secret") .. " e si prende " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ Era " .. bot.get("secret") .. ". Nessuno.") end
)LUA"},
    Recipe{"closest", 100, 1000,
           "@EMOJI@ @TITLE@: un numero fra 1 e @A@, e alla chiusura vince chi ci è andato più vicino. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("secret", bot.random(@A@) + 1) bot.set("best", @A@ * 10) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    local away = said - bot.get("secret")
    if away < 0 then away = -away end
    if away >= bot.get("best") then return end
    bot.set("best", away) bot.setname("leader", who)
    bot.say("@EMOJI@ " .. who .. " dice " .. said .. " ed è il più vicino per ora.")
end
function G.close(bot)
    local leader = bot.name("leader")
    if leader == "" then bot.say("@EMOJI@ Era " .. bot.get("secret") .. " e non ci ha provato nessuno.") return end
    bot.pay(leader, bot.pot())
    bot.say("@EMOJI@ Era " .. bot.get("secret") .. ": vince " .. leader .. " con " .. bot.pot() .. " palle.")
end
)LUA"},
    Recipe{"hidden", 0, 0,
           "@EMOJI@ @TITLE@: ho in mente una parola che qui si dice spesso. Chi la scrive incassa. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.setname("word", "@W1@") bot.set("tries", 0) end
function G.message(bot, who, text)
    bot.set("tries", bot.get("tries") + 1)
    if string.find(bot.lower(text), bot.name("word"), 1, true) == nil then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " l'ha detta al tentativo " .. bot.get("tries") .. ": " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ La parola era " .. bot.name("word") .. ".") end
)LUA"},
    Recipe{"total", 30, 150,
           "@EMOJI@ @TITLE@: si sommano numeri fino ad arrivare esatti a @A@. Chi sfora paga. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("total", 0) bot.set("target", @A@) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    local total = bot.get("total") + said
    if total > bot.get("target") then
        bot.take(who, 200)
        bot.say("@EMOJI@ " .. who .. " sfora a " .. total .. " e lascia 200 palle.")
        bot.close() return
    end
    bot.set("total", total)
    if total == bot.get("target") then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " chiude a " .. total .. " e si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    bot.say("@EMOJI@ Siamo a " .. total .. " su " .. bot.get("target") .. ".")
end
function G.close(bot) bot.say("@EMOJI@ Fermi a " .. bot.get("total") .. " su " .. bot.get("target") .. ".") end
)LUA"},
    Recipe{"sides", 0, 0,
           "@EMOJI@ @TITLE@: da che parte state, @W1@ o @W2@? Alla chiusura ne tiro a sorte una. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.setname("uno", "@W1@") bot.setname("due", "@W2@") end
function G.message(bot, who, text)
    local said = bot.lower(text)
    if bot.get("v:" .. who) ~= 0 then return end
    if string.find(said, bot.name("uno"), 1, true) ~= nil then
        bot.set("v:" .. who, 1) bot.say("@EMOJI@ " .. who .. " sta con " .. bot.name("uno") .. ".") return
    end
    if string.find(said, bot.name("due"), 1, true) ~= nil then
        bot.set("v:" .. who, 2) bot.say("@EMOJI@ " .. who .. " sta con " .. bot.name("due") .. ".")
    end
end
function G.close(bot)
    local winning = bot.random(2) + 1
    local side = bot.name("uno")
    if winning == 2 then side = bot.name("due") end
    local names = bot.players()
    local howmany = 0
    for i = 1, #names do if bot.get("v:" .. names[i]) == winning then howmany = howmany + 1 end end
    if howmany < 1 then
        for i = 1, #names do
            if bot.get("v:" .. names[i]) ~= 0 then bot.pay(names[i], bot.pot() // 4) end
        end
        bot.say("@EMOJI@ Esce " .. side .. " e non ci stava nessuno: un quarto agli altri.") return
    end
    for i = 1, #names do
        if bot.get("v:" .. names[i]) == winning then bot.pay(names[i], bot.pot() // howmany) end
    end
    bot.say("@EMOJI@ Esce " .. side .. ": " .. (bot.pot() // howmany) .. " palle a testa.")
end
)LUA"},
    Recipe{"wheel", 3, 10,
           "@EMOJI@ @TITLE@: puntate un numero da 1 a @A@, la ruota gira subito. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("faces", @A@) bot.set("giri", 0) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > bot.get("faces") then return end
    if bot.get("g:" .. who) ~= 0 then return end
    bot.set("g:" .. who, 1) bot.set("giri", bot.get("giri") + 1)
    local drawn = bot.random(bot.get("faces")) + 1
    if drawn == said then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ Esce " .. drawn .. "! " .. who .. " si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    bot.take(who, 100)
    bot.say("@EMOJI@ Esce " .. drawn .. ", " .. who .. " aveva " .. said .. ": 100 palle in meno.")
end
function G.close(bot) bot.say("@EMOJI@ La ruota si ferma dopo " .. bot.get("giri") .. " giri.") end
)LUA"},
    Recipe{"melt", 200, 1000,
           "@EMOJI@ @TITLE@: il premio cala di @A@ palle a ogni giro. Il primo che scrive prende quello che resta. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("prize", bot.pot()) end
function G.message(bot, who, text)
    local prize = bot.get("prize")
    if prize < 1 then bot.say("@EMOJI@ Non è rimasto niente per " .. who .. ".") bot.close() return end
    bot.pay(who, prize)
    bot.say("@EMOJI@ " .. who .. " arriva a quota " .. prize .. " e se le prende.")
    bot.close()
end
function G.tick(bot, now)
    local prize = bot.get("prize") - @A@
    if prize < 0 then prize = 0 end
    bot.set("prize", prize)
    bot.say("@EMOJI@ Restano " .. prize .. " palle.")
end
function G.close(bot) bot.say("@EMOJI@ Scaduto con " .. bot.get("prize") .. " palle sul tavolo.") end
)LUA"},
    Recipe{"initial", 0, 0,
           "@EMOJI@ @TITLE@: una parola di almeno cinque lettere che cominci per @L@. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.setname("letter", "@L@") end
function G.message(bot, who, text)
    local said = bot.lower(text)
    local found = string.match(said, "%f[%a]" .. bot.name("letter") .. "%a%a%a%a+")
    if found == nil then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " tira fuori " .. found .. " e si prende " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ Nessuna parola che cominci per " .. bot.name("letter") .. ".") end
)LUA"},
    Recipe{"taboo", 0, 0,
           "@EMOJI@ @TITLE@: c'è una parola proibita e non ve la dico. Chi la scrive paga, gli altri si dividono il piatto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.setname("word", "@W1@") end
function G.message(bot, who, text)
    if string.find(bot.lower(text), bot.name("word"), 1, true) == nil then
        bot.set("in:" .. who, 1) return
    end
    bot.take(who, 300)
    bot.say("@EMOJI@ " .. who .. " ha detto la parola proibita e paga 300 palle.")
    bot.close()
end
function G.close(bot)
    local names = bot.players()
    local howmany = 0
    for i = 1, #names do if bot.get("in:" .. names[i]) ~= 0 then howmany = howmany + 1 end end
    if howmany < 1 then bot.say("@EMOJI@ Nessuno ha parlato. La parola era " .. bot.name("word") .. ".") return end
    for i = 1, #names do
        if bot.get("in:" .. names[i]) ~= 0 then bot.pay(names[i], bot.pot() // howmany) end
    end
    bot.say("@EMOJI@ Nessuno ha detto " .. bot.name("word") .. ": " .. (bot.pot() // howmany) .. " palle a testa.")
end
)LUA"},
    Recipe{"ladder", 10, 25,
           "@EMOJI@ @TITLE@: si conta da 1 a @A@, uno alla volta. Chi sbaglia numero paga. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("count", 0) bot.set("top", @A@) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 0 then return end
    local wanted = bot.get("count") + 1
    if said ~= wanted then
        bot.take(who, 150)
        bot.say("@EMOJI@ " .. who .. " sbaglia numero e paga 150 palle.")
        bot.close() return
    end
    bot.set("count", wanted)
    if wanted >= bot.get("top") then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " arriva a " .. wanted .. " e si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    bot.say("@EMOJI@ " .. wanted .. ". Avanti.")
end
function G.close(bot) bot.say("@EMOJI@ Arrivati a " .. bot.get("count") .. " su " .. bot.get("top") .. ".") end
)LUA"},
    Recipe{"alphabet", 0, 0,
           "@EMOJI@ @TITLE@: dalla A alla J, ogni messaggio comincia con la lettera dopo. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("step", 0) end
function G.message(bot, who, text)
    local letters = "abcdefghij"
    local wanted = string.sub(letters, bot.get("step") + 1, bot.get("step") + 1)
    local first = string.sub(bot.lower(text), 1, 1)
    if first ~= wanted then
        bot.take(who, 150)
        bot.say("@EMOJI@ " .. who .. " doveva cominciare per " .. wanted .. " e paga 150 palle.")
        bot.close() return
    end
    bot.set("step", bot.get("step") + 1)
    if bot.get("step") >= 10 then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " chiude l'alfabeto e si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    bot.say("@EMOJI@ Ora tocca alla " .. string.sub(letters, bot.get("step") + 1, bot.get("step") + 1) .. ".")
end
function G.close(bot) bot.say("@EMOJI@ L'alfabeto si ferma alla lettera numero " .. bot.get("step") .. ".") end
)LUA"},
    Recipe{"longest", 0, 0,
           "@EMOJI@ @TITLE@: vince la parola più lunga scritta entro la chiusura. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("best", 0) end
function G.message(bot, who, text)
    local best = 0
    for word in string.gmatch(bot.lower(text), "%a+") do
        if #word > best then best = #word end
    end
    if best <= bot.get("best") then return end
    bot.set("best", best) bot.setname("leader", who)
    bot.say("@EMOJI@ " .. who .. " passa in testa con " .. best .. " lettere.")
end
function G.close(bot)
    local leader = bot.name("leader")
    if leader == "" then bot.say("@EMOJI@ Nessuna parola degna di nota.") return end
    bot.pay(leader, bot.pot())
    bot.say("@EMOJI@ Vince " .. leader .. " con " .. bot.get("best") .. " lettere: " .. bot.pot() .. " palle.")
end
)LUA"},
    Recipe{"echo", 0, 0,
           "@EMOJI@ @TITLE@: fra un attimo dico un codice di tre numeri, e vince chi lo ripete per primo. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.set("uno", bot.random(9) + 1) bot.set("due", bot.random(9) + 1)
    bot.set("tre", bot.random(9) + 1)
    bot.setname("code", bot.get("uno") .. " " .. bot.get("due") .. " " .. bot.get("tre"))
    bot.say("@EMOJI@ Il codice è " .. bot.name("code") .. ". Chi lo ripete per primo incassa.")
end
function G.message(bot, who, text)
    if string.find(text, bot.name("code"), 1, true) == nil then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " ha ripetuto " .. bot.name("code") .. ": " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ Era " .. bot.name("code") .. ", e nessuno l'ha ripetuto.") end
)LUA"},
    Recipe{"lies", 0, 0,
           "@EMOJI@ @TITLE@: tre voci, 1) @W1@ 2) @W2@ 3) @W3@. Una sola è falsa: scrivete il numero. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.set("false", bot.random(3) + 1)
    bot.setname("uno", "@W1@") bot.setname("due", "@W2@") bot.setname("tre", "@W3@")
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > 3 then return end
    if bot.get("v:" .. who) ~= 0 then return end
    bot.set("v:" .. who, said)
    if said == bot.get("false") then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " ha stanato la bugia numero " .. said .. ": " .. bot.pot() .. " palle.")
        bot.close() return
    end
    bot.take(who, 100)
    bot.say("@EMOJI@ La " .. said .. " era vera: " .. who .. " lascia 100 palle.")
end
function G.close(bot) bot.say("@EMOJI@ La bugia era la numero " .. bot.get("false") .. ".") end
)LUA"},
    Recipe{"suspects", 0, 0,
           "@EMOJI@ @TITLE@: i sospettati sono @W1@, @W2@ e @W3@. Uno è colpevole, ogni accusa a vuoto costa. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.setname("uno", "@W1@") bot.setname("due", "@W2@") bot.setname("tre", "@W3@")
    bot.setname("nome", "@ODD@")
end
function G.message(bot, who, text)
    local said = bot.lower(text)
    if string.find(said, bot.name("nome"), 1, true) ~= nil then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ Colpevole: " .. bot.name("nome") .. ". " .. who .. " si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    local wrong = false
    if string.find(said, bot.name("uno"), 1, true) ~= nil then wrong = true end
    if string.find(said, bot.name("due"), 1, true) ~= nil then wrong = true end
    if string.find(said, bot.name("tre"), 1, true) ~= nil then wrong = true end
    if not wrong then return end
    bot.take(who, 80)
    bot.say("@EMOJI@ " .. who .. " accusa a vuoto e lascia 80 palle.")
end
function G.close(bot) bot.say("@EMOJI@ Era stato " .. bot.name("nome") .. ", e l'ha fatta franca.") end
)LUA"},
    Recipe{"press", 3, 6,
           "@EMOJI@ @TITLE@: si entra con cento palle e ogni messaggio le moltiplica, ma una volta su @A@ crolla tutto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("giro", 0) end
function G.message(bot, who, text)
    if bot.get("out:" .. who) ~= 0 then return end
    local stake = bot.get("s:" .. who)
    if stake == 0 then
        bot.set("s:" .. who, 100) bot.take(who, 100)
        bot.say("@EMOJI@ " .. who .. " entra con cento palle.") return
    end
    if bot.random(@A@) == 0 then
        bot.set("out:" .. who, 1) bot.set("s:" .. who, 0)
        bot.say("@EMOJI@ " .. who .. " perde tutto quello che aveva accumulato.") return
    end
    bot.set("s:" .. who, stake * 3 // 2)
    bot.say("@EMOJI@ " .. who .. " sale a " .. bot.get("s:" .. who) .. ".")
end
function G.close(bot)
    local names = bot.players()
    local paid = 0
    for i = 1, #names do
        local stake = bot.get("s:" .. names[i])
        if stake > 0 then bot.pay(names[i], stake) paid = paid + 1 end
    end
    bot.say("@EMOJI@ Scendono in " .. paid .. " con le palle in tasca.")
end
)LUA"},
    Recipe{"warmer", 40, 200,
           "@EMOJI@ @TITLE@: un numero fra 1 e @A@, vi dico solo se scotta. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("secret", bot.random(@A@) + 1) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > @A@ then return end
    if said == bot.get("secret") then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " trova il " .. said .. " e si prende " .. bot.pot() .. " palle.")
        bot.close() return
    end
    local away = said - bot.get("secret")
    if away < 0 then away = -away end
    local word = "gelo"
    if away <= @A@ // 4 then word = "tiepido" end
    if away <= @A@ // 10 then word = "caldo" end
    if away <= 3 then word = "fuochissimo" end
    bot.say("@EMOJI@ " .. said .. ": " .. word .. ".")
end
function G.close(bot) bot.say("@EMOJI@ Era " .. bot.get("secret") .. ", e si è raffreddato.") end
)LUA"},
    Recipe{"draw", 10, 30,
           "@EMOJI@ @TITLE@: prendetevi un numero fino a @A@ scrivendolo, poi comincio a estrarre. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("extracted", 0) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > @A@ then return end
    if bot.get("c:" .. who) ~= 0 then return end
    bot.set("c:" .. who, said)
    bot.say("@EMOJI@ " .. who .. " prende il numero " .. said .. ".")
end
function G.tick(bot, now)
    local drawn = bot.random(@A@) + 1
    bot.set("extracted", drawn)
    local names = bot.players()
    for i = 1, #names do
        if bot.get("c:" .. names[i]) == drawn then
            bot.pay(names[i], bot.pot())
            bot.say("@EMOJI@ Esce il " .. drawn .. ": tombola di " .. names[i] .. ", " .. bot.pot() .. " palle.")
            return
        end
    end
    bot.say("@EMOJI@ Esce il " .. drawn .. ". Niente.")
end
function G.close(bot) bot.say("@EMOJI@ Ultimo numero estratto: " .. bot.get("extracted") .. ".") end
)LUA"},
    Recipe{"grow", 4, 8,
           "@EMOJI@ @TITLE@: ogni messaggio deve contenere una parola più lunga della precedente. @A@ passi e il piatto si divide. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("best", 0) bot.set("passi", 0) end
function G.message(bot, who, text)
    local best = 0
    for word in string.gmatch(bot.lower(text), "%a+") do
        if #word > best then best = #word end
    end
    if best <= bot.get("best") then return end
    bot.set("best", best)
    bot.set("passi", bot.get("passi") + 1)
    bot.set("in:" .. who, 1)
    if bot.get("passi") >= @A@ then
        local names = bot.players()
        local howmany = 0
        for i = 1, #names do if bot.get("in:" .. names[i]) ~= 0 then howmany = howmany + 1 end end
        for i = 1, #names do
            if bot.get("in:" .. names[i]) ~= 0 then bot.pay(names[i], bot.pot() // howmany) end
        end
        bot.say("@EMOJI@ Scala completata: " .. (bot.pot() // howmany) .. " palle a testa.")
        bot.close() return
    end
    bot.say("@EMOJI@ " .. who .. " sale a " .. best .. " lettere: passo " .. bot.get("passi") .. " su @A@.")
end
function G.close(bot) bot.say("@EMOJI@ Scala ferma al passo " .. bot.get("passi") .. ".") end
)LUA"},
    Recipe{"parity", 0, 0,
           "@EMOJI@ @TITLE@: puntate un numero a testa: alla chiusura conta se il totale è pari o dispari. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("total", 0) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    if bot.get("b:" .. who) ~= 0 then return end
    bot.set("b:" .. who, said)
    bot.set("total", bot.get("total") + said)
    bot.say("@EMOJI@ " .. who .. " mette " .. said .. ": totale " .. bot.get("total") .. ".")
end
function G.close(bot)
    local total = bot.get("total")
    local names = bot.players()
    local howmany = 0
    for i = 1, #names do
        local bet = bot.get("b:" .. names[i])
        if bet ~= 0 and (bet % 2) == (total % 2) then howmany = howmany + 1 end
    end
    if howmany < 1 then bot.say("@EMOJI@ Totale " .. total .. ": nessuno aveva la parità giusta.") return end
    for i = 1, #names do
        local bet = bot.get("b:" .. names[i])
        if bet ~= 0 and (bet % 2) == (total % 2) then bot.pay(names[i], bot.pot() // howmany) end
    end
    bot.say("@EMOJI@ Totale " .. total .. ": " .. (bot.pot() // howmany) .. " palle a chi era in parità.")
end
)LUA"},
    Recipe{"plots", 6, 20,
           "@EMOJI@ @TITLE@: prendete un lotto da 1 a @A@. Quelli chiesti da una persona sola rendono, gli altri no. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("lotti", @A@) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 or said > bot.get("lotti") then return end
    if bot.get("b:" .. who) ~= 0 then return end
    bot.set("b:" .. who, said)
    bot.set("t:" .. said, bot.get("t:" .. said) + 1)
    bot.say("@EMOJI@ " .. who .. " mette gli occhi sul " .. said .. ".")
end
function G.close(bot)
    local names = bot.players()
    local howmany = 0
    for i = 1, #names do
        local mine = bot.get("b:" .. names[i])
        if mine ~= 0 and bot.get("t:" .. mine) == 1 then howmany = howmany + 1 end
    end
    if howmany < 1 then bot.say("@EMOJI@ Tutti i lotti contesi: non rende niente a nessuno.") return end
    for i = 1, #names do
        local mine = bot.get("b:" .. names[i])
        if mine ~= 0 and bot.get("t:" .. mine) == 1 then bot.pay(names[i], bot.pot() // howmany) end
    end
    bot.say("@EMOJI@ " .. howmany .. " lotti assegnati: " .. (bot.pot() // howmany) .. " palle di rendita.")
end
)LUA"},
    Recipe{"maths", 0, 0,
           "@EMOJI@ @TITLE@: un conto a mente, ve lo dico adesso. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.set("uno", bot.random(80) + 11) bot.set("due", bot.random(80) + 11)
    bot.set("secret", bot.get("uno") + bot.get("due"))
    bot.say("@EMOJI@ Quanto fa " .. bot.get("uno") .. " più " .. bot.get("due") .. "?")
end
function G.message(bot, who, text)
    if bot.number(text) ~= bot.get("secret") then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " ha fatto il conto: " .. bot.get("secret") .. ". " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot)
    bot.say("@EMOJI@ Faceva " .. bot.get("uno") .. " più " .. bot.get("due") .. " uguale " .. bot.get("secret") .. ".")
end
)LUA"},
    Recipe{"intruder", 0, 0,
           "@EMOJI@ @TITLE@: @W1@, @W2@, @W3@. Una di queste tre non c'entra niente: scrivete quale. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.setname("odd", "@ODD@") end
function G.message(bot, who, text)
    if string.find(bot.lower(text), bot.name("odd"), 1, true) == nil then return end
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " ha stanato l'intruso: " .. bot.name("odd") .. ". " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ L'intruso era " .. bot.name("odd") .. ".") end
)LUA"},
    Recipe{"bell", 0, 0,
           "@EMOJI@ @TITLE@: prima o poi suona la campana. Chi scrive dopo paga, chi si è mosso prima si divide il piatto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("rung", 0) end
function G.message(bot, who, text)
    if bot.get("rung") == 0 then
        bot.set("in:" .. who, 1)
        return
    end
    bot.take(who, 250)
    bot.say("@EMOJI@ La campana era già suonata: " .. who .. " paga 250 palle.")
    bot.close()
end
function G.tick(bot, now)
    if bot.get("rung") ~= 0 then return end
    if bot.random(3) ~= 0 then bot.say("@EMOJI@ Ancora niente.") return end
    bot.set("rung", 1)
    bot.say("@EMOJI@ DLIN! Da adesso chi scrive paga.")
end
function G.close(bot)
    local names = bot.players()
    local howmany = 0
    for i = 1, #names do if bot.get("in:" .. names[i]) ~= 0 then howmany = howmany + 1 end end
    if howmany < 1 then bot.say("@EMOJI@ Nessuno si è mosso prima della campana.") return end
    for i = 1, #names do
        if bot.get("in:" .. names[i]) ~= 0 then bot.pay(names[i], bot.pot() // howmany) end
    end
    bot.say("@EMOJI@ Chi si è mosso prima della campana porta a casa " .. (bot.pot() // howmany) .. " palle.")
end
)LUA"},
    Recipe{"dice", 0, 0,
           "@EMOJI@ @TITLE@: una tirata a testa, due dadi. Il punteggio più alto alla chiusura prende tutto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("best", 0) end
function G.message(bot, who, text)
    if bot.get("d:" .. who) ~= 0 then return end
    local roll = bot.random(6) + bot.random(6) + 2
    bot.set("d:" .. who, roll)
    if roll > bot.get("best") then bot.set("best", roll) bot.setname("leader", who) end
    bot.say("@EMOJI@ " .. who .. " tira " .. roll .. ".")
end
function G.close(bot)
    local leader = bot.name("leader")
    if leader == "" then bot.say("@EMOJI@ Dadi fermi sul tavolo.") return end
    bot.pay(leader, bot.pot())
    bot.say("@EMOJI@ Vince " .. leader .. " con " .. bot.get("best") .. ": " .. bot.pot() .. " palle.")
end
)LUA"},
    Recipe{"sealed", 0, 0,
           "@EMOJI@ @TITLE@: un'offerta segreta a testa. La più alta vince il piatto e paga quanto ha offerto. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("aperta", 1) end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 1 then return end
    if bot.get("b:" .. who) ~= 0 then return end
    bot.set("b:" .. who, said)
    bot.say("@EMOJI@ Offerta di " .. who .. " registrata. Nessuno la vede.")
end
function G.close(bot)
    local names = bot.players()
    local best = 0
    local winner = ""
    for i = 1, #names do
        local bid = bot.get("b:" .. names[i])
        if bid > best then best = bid winner = names[i] end
    end
    if winner == "" then bot.say("@EMOJI@ Asta deserta.") return end
    bot.pay(winner, bot.pot())
    bot.take(winner, best)
    bot.say("@EMOJI@ Aggiudicato a " .. winner .. " per " .. best .. ": incassa " .. bot.pot() .. " palle.")
end
)LUA"},
    Recipe{"pairs", 5, 20,
           "@EMOJI@ @TITLE@: c'è una coppia nascosta fra 1 e @A@. Chi la scopre incassa. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.set("uno", bot.random(@A@) + 1)
    bot.set("due", bot.get("uno"))
    bot.set("trovate", 0)
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said ~= bot.get("uno") then return end
    bot.set("trovate", bot.get("trovate") + 1)
    bot.pay(who, bot.pot())
    bot.say("@EMOJI@ " .. who .. " ha scoperto la coppia del " .. said .. ": " .. bot.pot() .. " palle.")
    bot.close()
end
function G.close(bot) bot.say("@EMOJI@ La coppia era il " .. bot.get("uno") .. ".") end
)LUA"},
    Recipe{"relay", 4, 8,
           "@EMOJI@ @TITLE@: @A@ passaggi, mai due di fila dalla stessa persona, e il piatto si divide. @FLAVOUR@",
           R"LUA(
function G.open(bot) bot.set("passi", 0) end
function G.message(bot, who, text)
    if bot.name("last") == who then return end
    bot.setname("last", who)
    bot.set("in:" .. who, 1)
    bot.set("passi", bot.get("passi") + 1)
    if bot.get("passi") >= @A@ then
        local names = bot.players()
        local howmany = 0
        for i = 1, #names do if bot.get("in:" .. names[i]) ~= 0 then howmany = howmany + 1 end end
        for i = 1, #names do
            if bot.get("in:" .. names[i]) ~= 0 then bot.pay(names[i], bot.pot() // howmany) end
        end
        bot.say("@EMOJI@ Traguardo! " .. (bot.pot() // howmany) .. " palle a chi ha corso.")
        bot.close() return
    end
    bot.say("@EMOJI@ Passaggio " .. bot.get("passi") .. " su @A@, tocca a un altro.")
end
function G.close(bot) bot.say("@EMOJI@ Testimone caduto al passaggio " .. bot.get("passi") .. ".") end
)LUA"},
    Recipe{"mastermind", 0, 0,
           "@EMOJI@ @TITLE@: un codice di due cifre. A ogni tentativo vi dico quante sono al posto giusto. @FLAVOUR@",
           R"LUA(
function G.open(bot)
    bot.set("uno", bot.random(9) + 1) bot.set("due", bot.random(9) + 1)
    bot.set("secret", bot.get("uno") * 10 + bot.get("due"))
end
function G.message(bot, who, text)
    local said = bot.number(text)
    if said < 10 or said > 99 then return end
    if said == bot.get("secret") then
        bot.pay(who, bot.pot())
        bot.say("@EMOJI@ " .. who .. " rompe il codice " .. said .. ": " .. bot.pot() .. " palle.")
        bot.close() return
    end
    local giuste = 0
    if (said // 10) == bot.get("uno") then giuste = giuste + 1 end
    if (said % 10) == bot.get("due") then giuste = giuste + 1 end
    bot.say("@EMOJI@ " .. said .. ": " .. giuste .. " cifre al posto giusto.")
end
function G.close(bot) bot.say("@EMOJI@ Il codice era " .. bot.get("secret") .. ".") end
)LUA"},
});

/* I giochi di società a cui la forgia si ispira: il vestito, e quali regole ci stanno sotto. */
struct Skin {
    std::string_view emoji;
    std::string_view title;
    std::string_view family;
};

constexpr auto skins = std::to_array<Skin>({
    Skin{"🎲", "TOMBOLONE", "guess"},
    Skin{"🔢", "INDOVINA IL NUMERO", "guess"},
    Skin{"🎱", "OTTO VOLANTE", "guess"},
    Skin{"🧿", "NUMERO SEGRETO", "guess"},
    Skin{"💰", "IL PREZZO GIUSTO", "closest"},
    Skin{"📏", "A OCCHIO E CROCE", "closest"},
    Skin{"⚖️", "LA STIMA", "closest"},
    Skin{"🎯", "VICINO VICINO", "closest"},
    Skin{"🔎", "PAROLA NASCOSTA", "hidden"},
    Skin{"🕵️", "LA TALPA", "hidden"},
    Skin{"🧩", "INDIZIO", "hidden"},
    Skin{"🔤", "CACCIA ALLA PAROLA", "hidden"},
    Skin{"🧮", "SETTE E MEZZO", "total"},
    Skin{"🪙", "VENTUNO", "total"},
    Skin{"📊", "LA COLLETTA", "total"},
    Skin{"➕", "SOMMA ESATTA", "total"},
    Skin{"⚔️", "GUELFI E GHIBELLINI", "sides"},
    Skin{"🗳️", "REFERENDUM", "sides"},
    Skin{"🤼", "DERBY", "sides"},
    Skin{"🚩", "DUE BANDIERE", "sides"},
    Skin{"🎡", "ROULETTE", "wheel"},
    Skin{"🎰", "SLOT", "wheel"},
    Skin{"🃏", "CARTA COPERTA", "wheel"},
    Skin{"🎪", "GIRO DELLA FORTUNA", "wheel"},
    Skin{"⏳", "ASTA AL RIBASSO", "melt"},
    Skin{"🧊", "PREMIO CHE SI SCIOGLIE", "melt"},
    Skin{"📉", "SVENDITA", "melt"},
    Skin{"🕯️", "CANDELA", "melt"},
    Skin{"🏙️", "NOMI COSE CITTÀ", "initial"},
    Skin{"🅰️", "PAROLA CON LA LETTERA", "initial"},
    Skin{"📚", "VOCABOLARIO", "initial"},
    Skin{"🗺️", "GEOGRAFIA", "initial"},
    Skin{"🤐", "TABÙ", "taboo"},
    Skin{"🙊", "PAROLA PROIBITA", "taboo"},
    Skin{"🚫", "NON DIRLO", "taboo"},
    Skin{"🔇", "OMERTÀ", "taboo"},
    Skin{"🪜", "SCALA", "ladder"},
    Skin{"🔟", "UNO DUE TRE", "ladder"},
    Skin{"🧗", "CONTO ALLA ROVESCIA", "ladder"},
    Skin{"📶", "GRADINI", "ladder"},
    Skin{"🔠", "ALFABETO", "alphabet"},
    Skin{"📝", "DALLA A ALLA J", "alphabet"},
    Skin{"🗂️", "ORDINE ALFABETICO", "alphabet"},
    Skin{"✏️", "ABBECEDARIO", "alphabet"},
    Skin{"📏", "SCARABEO", "longest"},
    Skin{"🧾", "PAROLONE", "longest"},
    Skin{"🏆", "LA PIÙ LUNGA", "longest"},
    Skin{"📖", "DIZIONARIO", "longest"},
    Skin{"🔁", "SIMON", "echo"},
    Skin{"🧠", "RIPETI DOPO DI ME", "echo"},
    Skin{"📻", "CODICE MORSE", "echo"},
    Skin{"🎼", "LA SEQUENZA", "echo"},
    Skin{"🎭", "DUE VERITÀ E UNA BUGIA", "lies"},
    Skin{"🤥", "IL BUGIARDO", "lies"},
    Skin{"🕯️", "LUPUS IN FABULA", "lies"},
    Skin{"🎬", "FINZIONE", "lies"},
    Skin{"🔍", "CLUEDO", "suspects"},
    Skin{"🚔", "CHI È STATO", "suspects"},
    Skin{"🗡️", "IL COLPEVOLE", "suspects"},
    Skin{"🏚️", "DELITTO IN SALOTTO", "suspects"},
    Skin{"🎢", "RISCHIATUTTO", "press"},
    Skin{"🧨", "PRESSA LA FORTUNA", "press"},
    Skin{"🎈", "GONFIA E SCOPPIA", "press"},
    Skin{"🪂", "SALTO NEL VUOTO", "press"},
    Skin{"🌡️", "ACQUA FUOCO", "warmer"},
    Skin{"🔥", "FUOCHINO", "warmer"},
    Skin{"❄️", "FREDDO FREDDO", "warmer"},
    Skin{"🧭", "BUSSOLA", "warmer"},
    Skin{"🎫", "TOMBOLA", "draw"},
    Skin{"🔔", "CARTELLA", "draw"},
    Skin{"🎟️", "BINGO", "draw"},
    Skin{"📮", "ESTRAZIONE", "draw"},
    Skin{"🚇", "TUNNEL", "grow"},
    Skin{"📈", "SCALATA", "grow"},
    Skin{"🧱", "MURO", "grow"},
    Skin{"🌱", "CRESCITA", "grow"},
    Skin{"🎲", "PARI O DISPARI", "parity"},
    Skin{"🪙", "TESTA O CROCE", "parity"},
    Skin{"♟️", "BIANCO O NERO", "parity"},
    Skin{"🔀", "PARITÀ", "parity"},
    Skin{"🏘️", "MONOPOLI", "plots"},
    Skin{"🗺️", "RISIKO", "plots"},
    Skin{"🏚️", "CATASTO", "plots"},
    Skin{"🚜", "TERRENI", "plots"},
    Skin{"🧮", "TRIVIAL", "maths"},
    Skin{"➗", "CALCOLO A MENTE", "maths"},
    Skin{"🔢", "ARITMETICA", "maths"},
    Skin{"📐", "QUIZ DI MATEMATICA", "maths"},
    Skin{"🕵️", "L'INTRUSO", "intruder"},
    Skin{"🧐", "CHI NON C'ENTRA", "intruder"},
    Skin{"🚮", "FUORI POSTO", "intruder"},
    Skin{"🎩", "L'ESTRANEO", "intruder"},
    Skin{"🔔", "UN DUE TRE STELLA", "bell"},
    Skin{"💣", "PATATA BOLLENTE", "bell"},
    Skin{"⏰", "LA CAMPANA", "bell"},
    Skin{"🚨", "ALLARME", "bell"},
    Skin{"🎲", "NON T'ARRABBIARE", "dice"},
    Skin{"🐍", "SCALE E SERPENTI", "dice"},
    Skin{"🦆", "GIOCO DELL'OCA", "dice"},
    Skin{"🎯", "DUE DADI", "dice"},
    Skin{"🔨", "MERCANTE IN FIERA", "sealed"},
    Skin{"🕶️", "ASTA CIECA", "sealed"},
    Skin{"💼", "OFFERTA SEGRETA", "sealed"},
    Skin{"🏦", "INCANTO", "sealed"},
    Skin{"🧠", "MEMORY", "pairs"},
    Skin{"🃏", "COPPIA NASCOSTA", "pairs"},
    Skin{"👯", "LE GEMELLE", "pairs"},
    Skin{"🔗", "TROVA IL PARI", "pairs"},
    Skin{"🏃", "STAFFETTA", "relay"},
    Skin{"📞", "TELEFONO SENZA FILI", "relay"},
    Skin{"🤝", "PASSAMANO", "relay"},
    Skin{"🎽", "TESTIMONE", "relay"},
    Skin{"🔐", "MASTERMIND", "mastermind"},
    Skin{"🧩", "FORZA IL CODICE", "mastermind"},
    Skin{"🗝️", "CASSAFORTE", "mastermind"},
    Skin{"🤖", "CIFRARIO", "mastermind"},
});

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

/* Il vestito, le regole e i numeri di un gioco nuovo, messi insieme in sorgente Lua. */
std::string write_one(const ForgeRequest &request, const std::string &keyword, const RandomSource &random) {
    const Skin &skin = skins.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(skins.size()))));
    const auto recipe = std::ranges::find(recipes, skin.family, &Recipe::family);
    if (recipe == recipes.end()) {
        return {};
    }

    /* Tre parole del gruppo, più una che con loro non c'entra niente. */
    const auto group_word = [&request, &random](std::string_view fallback) {
        for (int look = 0; look < 12; ++look) {
            if (request.lexicon.empty()) {
                break;
            }
            const std::string &word =
                request.lexicon.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(request.lexicon.size())))).first;
            if (only_letters(word) && word.size() >= 4) {
                return lua_safe(word);
            }
        }
        return std::string{fallback};
    };
    constexpr auto strangers = std::to_array<std::string_view>({
        "ornitorinco", "tapioca", "monsone", "fotosintesi", "clavicembalo",
        "paguro", "stalattite", "girasole", "bitume", "arcolaio",
    });
    std::string first = group_word("palloncino");
    std::string second = group_word("citazione");
    std::string third = group_word("classifica");
    std::string odd = third;
    if (skin.family == "intruder") {
        odd = std::string{strangers.at(static_cast<std::size_t>(random(static_cast<std::int64_t>(strangers.size()))))};
        /* Lo straniero prende il posto di una delle tre, sorteggiato. */
        const std::int64_t slot = random(3);
        if (slot == 0) {
            first = odd;
        } else if (slot == 1) {
            second = odd;
        } else {
            third = odd;
        }
    }
    if (skin.family == "suspects") {
        const std::int64_t slot = random(3);
        odd = slot == 0 ? first : (slot == 1 ? second : third);
    }

    const std::int64_t span = recipe->high > recipe->low ? recipe->high - recipe->low : 0;
    const std::int64_t value = recipe->low + (span > 0 ? random(span) : 0);
    const std::string letter{std::string_view{"bcfgmprstv"}.substr(static_cast<std::size_t>(random(10)), 1)};

    std::string source = std::format(
        "local G = {{}}\nG.keyword = \"{}\"\nG.family = \"{}\"\nG.announce = \"{}\"\n{}\nreturn G\n",
        keyword,
        skin.family,
        recipe->announce,
        recipe->body
    );
    source = replace_all(std::move(source), "@KEY@", keyword);
    source = replace_all(std::move(source), "@EMOJI@", skin.emoji);
    source = replace_all(std::move(source), "@TITLE@", skin.title);
    source = replace_all(std::move(source), "@FLAVOUR@", flavour_from(request, random));
    source = replace_all(std::move(source), "@A@", std::format("{}", value));
    source = replace_all(std::move(source), "@L@", letter);
    source = replace_all(std::move(source), "@W1@", first);
    source = replace_all(std::move(source), "@W2@", second);
    source = replace_all(std::move(source), "@W3@", third);
    return replace_all(std::move(source), "@ODD@", odd);
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
            .family = json_string_field(line, "f"),
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

std::string forge_family_of(const std::string &keyword) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    const auto found = shelf.games.find(keyword);
    return found == shelf.games.end() ? std::string{} : found->second.family;
}

/* Il registro degli esiti: una riga per partita, in coda, da cui un giorno si impara. */
void forge_record(const ForgeVerdict &verdict) {
    Catalogue &shelf = catalogue();
    const std::lock_guard held{shelf.lock};
    if (!shelf.ready || verdict.keyword.empty()) {
        return;
    }
    std::ofstream book{verdicts_path(shelf), std::ios::binary | std::ios::app};
    if (!book) {
        return;
    }
    book << std::format(
        R"({{"k":"{}","f":"{}","p":{},"m":{},"w":{},"d":{},"b":{},"t":{}}})",
        verdict.keyword,
        verdict.family,
        verdict.players,
        verdict.messages,
        verdict.first_move,
        verdict.decided ? 1 : 0,
        verdict.palle,
        verdict.at
    ) << '\n';
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
        game.family = lua_field(game.source, "family", forge_limits()).value_or(std::string{});

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
        shelf.games[game.keyword] =
            Forged{.announce = game.announce, .family = game.family, .born = born, .broken = false};
        shelf.order.push_back(game.keyword);
        log_info("Forged a new game: {}", game.keyword);
        return game;
    }
    return std::nullopt;
}

}
