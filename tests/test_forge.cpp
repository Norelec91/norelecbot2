#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "forge.hpp"
#include "game.hpp"
#include "lua_vm.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <set>
#include <string>
#include <vector>

using namespace norelecbot;

namespace {

constexpr LuaLimits small{.steps = 50000, .memory_bytes = std::size_t{1024} * 1024};

LuaBotView nothing_view(std::int64_t pot = 1000) {
    LuaBotView view;
    view.pot = pot;
    view.score = [](const std::string &) { return std::int64_t{5000}; };
    view.players = []() { return std::vector<std::string>{"alice", "bob"}; };
    view.random = [](std::int64_t count) { return count > 1 ? std::int64_t{1} : std::int64_t{0}; };
    view.get = [](const std::string &) { return std::int64_t{0}; };
    view.set = [](const std::string &, std::int64_t) {};
    view.name = [](const std::string &) { return std::string{}; };
    view.setname = [](const std::string &, const std::string &) {};
    return view;
}

/* Una cartella tutta sua per la forgia, buttata via alla fine. */
struct ForgeDirectory {
    std::filesystem::path where;

    ForgeDirectory() {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        where = std::filesystem::temp_directory_path() / std::format("norelecbot-forge-{}", stamp);
        std::filesystem::create_directories(where);
    }

    ~ForgeDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(where, ignored);
    }

    ForgeDirectory(const ForgeDirectory &) = delete;
    ForgeDirectory &operator=(const ForgeDirectory &) = delete;
    ForgeDirectory(ForgeDirectory &&) = delete;
    ForgeDirectory &operator=(ForgeDirectory &&) = delete;
};

ForgeRequest group_talk() {
    ForgeRequest request;
    request.lexicon = {
        {"denuncia", 40}, {"palloncino", 31}, {"zimbello", 22}, {"doronzo", 19},
        {"flegias", 17},  {"ministero", 14},  {"sperma", 13},   {"ricorso", 11},
        {"quote", 9},     {"segnalazione", 8}, {"tribunale", 7}, {"cartella", 6},
        {"la", 900},      {"carta", 120},     {"asta", 44},     {"norelec", 60},
    };
    request.reserved = {"la", "carta", "asta", "norelec", "corsa", "occhio", "somma"};
    return request;
}

std::int64_t rolling(std::int64_t count) {
    static std::uint64_t seed = 7;
    seed = (seed * 6364136223846793005ULL) + 1442695040888963407ULL;
    const std::uint64_t drawn = seed >> 33U;
    return static_cast<std::int64_t>(drawn % static_cast<std::uint64_t>(std::max<std::int64_t>(count, 1)));
}

}

TEST_CASE("a forged game cannot reach outside its own table") {
    const std::string nosy = R"LUA(local G = {}
G.keyword = "spione"
G.announce = "prova"
function G.open(bot)
    bot.say("io=" .. tostring(io) .. " os=" .. tostring(os) .. " require=" .. tostring(require) ..
            " load=" .. tostring(load) .. " debug=" .. tostring(debug) .. " dofile=" .. tostring(dofile))
end
return G
)LUA";
    const LuaBotView view = nothing_view();
    const LuaRun run = lua_invoke(nosy, "open", view, {}, small);
    REQUIRE(run.ok);
    REQUIRE(run.effects.size() == 1);
    CHECK(run.effects.at(0).text == "io=nil os=nil require=nil load=nil debug=nil dofile=nil");
}

TEST_CASE("a game that never stops is stopped") {
    const std::string forever = R"LUA(local G = {}
G.keyword = "eterno"
G.announce = "prova"
function G.open(bot) while true do end end
return G
)LUA";
    const LuaBotView view = nothing_view();
    const LuaRun run = lua_invoke(forever, "open", view, {}, small);
    CHECK_FALSE(run.ok);
    CHECK(run.error.contains("troppo"));
}

TEST_CASE("a game that eats memory is cut off") {
    const std::string hungry = R"LUA(local G = {}
G.keyword = "vorace"
G.announce = "prova"
function G.open(bot)
    local heap = {}
    for i = 1, 100000 do heap[i] = string.rep("x", 1000) end
end
return G
)LUA";
    const LuaBotView view = nothing_view();
    const LuaRun run = lua_invoke(hungry, "open", view, {}, small);
    CHECK_FALSE(run.ok);
    CHECK_FALSE(run.error.empty());
}

TEST_CASE("a game cannot hand out more than the pot allows") {
    const std::string greedy = R"LUA(local G = {}
G.keyword = "ingordo"
G.announce = "prova"
function G.open(bot)
    bot.pay("alice", 999999)
    bot.pay("alice", 999999)
    bot.pay("alice", 999999)
end
return G
)LUA";
    const LuaBotView view = nothing_view(1000);
    const LuaRun run = lua_invoke(greedy, "open", view, {}, small);
    REQUIRE(run.ok);
    std::int64_t handed = 0;
    for (const LuaEffect &effect : run.effects) {
        CHECK(effect.palle <= 1000);
        handed += effect.palle;
    }
    /* Una mossa sola non supera il piatto, e un giro intero non ne supera il doppio. */
    CHECK(handed == 2000);
}

TEST_CASE("a broken game is an error, not a crash") {
    const std::string broken = R"LUA(local G = {}
G.keyword = "rotto"
G.announce = "prova"
function G.open(bot) error("mi sono rotto") end
return G
)LUA";
    const LuaRun run = lua_invoke(broken, "open", nothing_view(), {}, small);
    CHECK_FALSE(run.ok);
    CHECK(run.error.contains("mi sono rotto"));
    CHECK(run.effects.empty());

    const LuaRun nonsense = lua_invoke("questo non è Lua", "open", nothing_view(), {}, small);
    CHECK_FALSE(nonsense.ok);
}

TEST_CASE("the forge mints games that stand up on their own") {
    const ForgeDirectory home;
    forge_open_catalogue(home.where.string(), small);
    REQUIRE(forge_ready());

    const ForgeRequest request = group_talk();
    std::set<std::string> minted;
    for (int made = 0; made < 40; ++made) {
        const std::optional<ForgedGame> game = forge_mint(request, rolling);
        REQUIRE(game);
        /* Ogni parola è nuova, e nessuna pesta una parola già occupata. */
        CHECK(minted.find(game->keyword) == minted.end());
        minted.insert(game->keyword);
        for (const std::string &taken : request.reserved) {
            CHECK(game->keyword.find(taken) == std::string::npos);
            CHECK(taken.find(game->keyword) == std::string::npos);
        }
        CHECK_FALSE(game->announce.empty());
        CHECK(game->source.contains("G.keyword"));
        std::string why_not;
        CHECK(forge_try_out(game->source, why_not));
    }
    CHECK(forge_count() == 40);

    /* Il codice è su disco e si rilegge. */
    const std::string one = *minted.begin();
    const std::optional<std::string> source = forge_source(one);
    REQUIRE(source);
    CHECK(source->contains(one));
    CHECK(forge_announce_of(one).has_value());
    CHECK(forge_any(rolling).has_value());

    /* E si ritrova tutto dopo un riavvio, leggendo la stessa cartella. */
    forge_open_catalogue(home.where.string(), small);
    CHECK(forge_count() == 40);
    CHECK(forge_knows(one));

    /* Un gioco che si rompe in partita non si riapre più. */
    forge_condemn(one, "prova");
    CHECK_FALSE(forge_source(one).has_value());
}

TEST_CASE("a game that does nothing is thrown away") {
    const ForgeDirectory home;
    forge_open_catalogue(home.where.string(), small);
    const std::string lazy = R"LUA(local G = {}
G.keyword = "pigro"
G.announce = "non succede niente"
function G.open(bot) end
return G
)LUA";
    std::string why_not;
    CHECK_FALSE(forge_try_out(lazy, why_not));
    CHECK(why_not == "non muove mai una palla");
}

TEST_CASE("a plain word from the group becomes a game name") {
    const ForgeDirectory home;
    forge_open_catalogue(home.where.string(), small);
    ForgeRequest request;
    request.lexicon = {{"zimbello", 40}, {"denuncia", 22}, {"ricorso", 9}};
    request.reserved = hand_written_words();
    const auto always_first = [](std::int64_t) { return std::int64_t{0}; };
    const std::optional<ForgedGame> born = forge_mint(request, always_first);
    REQUIRE(born);
    CHECK(born->keyword == "zimbello");
}

TEST_CASE("the forge does not keep writing the same game") {
    const ForgeDirectory home;
    forge_open_catalogue(home.where.string(), small);
    const ForgeRequest request = group_talk();
    std::set<std::string> families;
    std::set<std::string> titles;
    for (int made = 0; made < 120; ++made) {
        const std::optional<ForgedGame> game = forge_mint(request, rolling);
        REQUIRE(game);
        const std::optional<std::string> family = lua_field(game->source, "family", small);
        REQUIRE(family);
        families.insert(*family);
        titles.insert(game->announce.substr(0, game->announce.find(':')));
    }
    /* Cento partite non possono essere tutte lo stesso gioco con un nome diverso. */
    CHECK(families.size() >= 20);
    CHECK(titles.size() >= 30);
}
