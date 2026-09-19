#include "lua_vm.hpp"

#include "logging.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <memory>
#include <new>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace norelecbot {
namespace {

constexpr std::size_t hook_every = 1000;

/* Quanta memoria lo script ha già preso, e quanta gliene resta. */
struct Budget {
    std::size_t used = 0;
    std::size_t cap = 0;
};

void *allocate(void *opaque, void *block, std::size_t was, std::size_t wanted) {
    auto *budget = static_cast<Budget *>(opaque);
    if (wanted == 0) {
        budget->used -= block != nullptr ? was : 0;
        std::free(block);
        return nullptr;
    }
    const std::size_t after = budget->used - (block != nullptr ? was : 0) + wanted;
    if (after > budget->cap) {
        return nullptr;
    }
    void *fresh = std::realloc(block, wanted);
    if (fresh != nullptr) {
        budget->used = after;
    }
    return fresh;
}

/* Tutto quello che le funzioni di bot hanno bisogno di vedere, passato come upvalue. */
struct Context {
    const LuaBotView *view = nullptr;
    std::vector<LuaEffect> *effects = nullptr;
    std::int64_t steps_left = 0;
    std::int64_t moved = 0;
    std::int64_t may_move = 0;
};

Context &context_of(lua_State *machine) {
    void *stored = lua_touserdata(machine, lua_upvalueindex(1));
    return *static_cast<Context *>(stored);
}

/* Il gancio non ha upvalue, quindi il contesto passa dal registro. */
void hook(lua_State *machine, lua_Debug *activity) {
    lua_getfield(machine, LUA_REGISTRYINDEX, "norelecbot_context");
    auto *context = static_cast<Context *>(lua_touserdata(machine, -1));
    lua_pop(machine, 1);
    if (context == nullptr) {
        return;
    }
    context->steps_left -= static_cast<std::int64_t>(hook_every);
    if (context->steps_left <= 0) {
        luaL_error(machine, "lo script ci sta mettendo troppo");
    }
    static_cast<void>(activity);
}

std::string text_argument(lua_State *machine, int at) {
    std::size_t length = 0;
    const char *text = lua_tolstring(machine, at, &length);
    return text == nullptr ? std::string{} : std::string{text, length};
}

int bot_random(lua_State *machine) {
    const Context &context = context_of(machine);
    const auto count = static_cast<std::int64_t>(luaL_checkinteger(machine, 1));
    if (count < 1 || !context.view->random) {
        lua_pushinteger(machine, 0);
        return 1;
    }
    lua_pushinteger(machine, context.view->random(count));
    return 1;
}

int bot_pot(lua_State *machine) {
    lua_pushinteger(machine, context_of(machine).view->pot);
    return 1;
}

int bot_get(lua_State *machine) {
    const Context &context = context_of(machine);
    lua_pushinteger(machine, context.view->get(text_argument(machine, 1)));
    return 1;
}

int bot_set(lua_State *machine) {
    const Context &context = context_of(machine);
    context.view->set(text_argument(machine, 1), static_cast<std::int64_t>(luaL_checkinteger(machine, 2)));
    return 0;
}

int bot_name(lua_State *machine) {
    const Context &context = context_of(machine);
    const std::string found = context.view->name(text_argument(machine, 1));
    lua_pushlstring(machine, found.data(), found.size());
    return 1;
}

int bot_setname(lua_State *machine) {
    const Context &context = context_of(machine);
    context.view->setname(text_argument(machine, 1), text_argument(machine, 2));
    return 0;
}

int bot_score(lua_State *machine) {
    const Context &context = context_of(machine);
    lua_pushinteger(machine, context.view->score(text_argument(machine, 1)));
    return 1;
}

int bot_players(lua_State *machine) {
    const Context &context = context_of(machine);
    const std::vector<std::string> names = context.view->players();
    lua_createtable(machine, static_cast<int>(names.size()), 0);
    int at = 1;
    for (const std::string &name : names) {
        lua_pushlstring(machine, name.data(), name.size());
        lua_rawseti(machine, -2, at);
        ++at;
    }
    return 1;
}

/* Paga e togli hanno lo stesso tetto: una mossa sola non muove più del piatto, e un messaggio
   intero non muove più del doppio. */
int move_palle(lua_State *machine, LuaEffect::Kind kind) {
    Context &context = context_of(machine);
    const std::string who = text_argument(machine, 1);
    auto howmany = static_cast<std::int64_t>(luaL_checkinteger(machine, 2));
    if (who.empty() || howmany <= 0) {
        return 0;
    }
    howmany = std::min(howmany, context.view->pot);
    howmany = std::min(howmany, context.may_move - context.moved);
    if (howmany <= 0) {
        return 0;
    }
    context.moved += howmany;
    context.effects->push_back(LuaEffect{.kind = kind, .who = who, .text = {}, .palle = howmany});
    return 0;
}

int bot_pay(lua_State *machine) { return move_palle(machine, LuaEffect::Kind::pay); }

int bot_take(lua_State *machine) { return move_palle(machine, LuaEffect::Kind::take); }

int bot_say(lua_State *machine) {
    Context &context = context_of(machine);
    std::string said = text_argument(machine, 1);
    if (said.empty() || context.effects->size() > 20) {
        return 0;
    }
    if (said.size() > 400) {
        said.resize(400);
    }
    context.effects->push_back(
        LuaEffect{.kind = LuaEffect::Kind::say, .who = {}, .text = std::move(said), .palle = 0}
    );
    return 0;
}

int bot_close(lua_State *machine) {
    Context &context = context_of(machine);
    context.effects->push_back(LuaEffect{.kind = LuaEffect::Kind::close, .who = {}, .text = {}, .palle = 0});
    return 0;
}

/* Il primo numero scritto in un messaggio, o -1 se non ce ne sono. */
int bot_number(lua_State *machine) {
    const std::string said = text_argument(machine, 1);
    std::int64_t found = -1;
    std::int64_t current = 0;
    bool reading = false;
    for (const char letter : said) {
        if (letter >= '0' && letter <= '9') {
            current = current * 10 + (letter - '0');
            current = std::min(current, std::int64_t{1000000000});
            reading = true;
            continue;
        }
        if (reading) {
            found = found < 0 ? current : found;
            reading = false;
            current = 0;
        }
    }
    if (reading && found < 0) {
        found = current;
    }
    lua_pushinteger(machine, found);
    return 1;
}

int bot_lower(lua_State *machine) {
    std::string said = text_argument(machine, 1);
    std::ranges::transform(said, said.begin(), [](const char letter) {
        return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
    });
    lua_pushlstring(machine, said.data(), said.size());
    return 1;
}

struct MachineCloser {
    void operator()(lua_State *machine) const noexcept {
        if (machine != nullptr) {
            lua_close(machine);
        }
    }
};

using Machine = std::unique_ptr<lua_State, MachineCloser>;

void put_function(lua_State *machine, const char *name, lua_CFunction work, void *context) {
    lua_pushlightuserdata(machine, context);
    lua_pushcclosure(machine, work, 1);
    lua_setfield(machine, -2, name);
}

/* Una macchina con dentro solo quello che serve a un gioco: niente file, niente processi, niente
   modo di caricare altro codice. */
Machine fresh_machine(Budget &budget, Context &context) {
    Machine machine{lua_newstate(allocate, &budget)};
    if (!machine) {
        return machine;
    }
    lua_State *raw = machine.get();
    luaL_requiref(raw, LUA_GNAME, luaopen_base, 1);
    lua_pop(raw, 1);
    luaL_requiref(raw, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(raw, 1);
    luaL_requiref(raw, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(raw, 1);
    luaL_requiref(raw, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(raw, 1);
    for (const char *forbidden : {"dofile", "loadfile", "load", "loadstring", "require", "print",
                                  "collectgarbage", "rawset", "rawget", "rawequal", "rawlen",
                                  "setmetatable", "getmetatable", "io", "os", "package", "debug",
                                  "coroutine", "arg"}) {
        lua_pushnil(raw);
        lua_setglobal(raw, forbidden);
    }

    lua_createtable(raw, 0, 14);
    put_function(raw, "random", bot_random, &context);
    put_function(raw, "pot", bot_pot, &context);
    put_function(raw, "get", bot_get, &context);
    put_function(raw, "set", bot_set, &context);
    put_function(raw, "name", bot_name, &context);
    put_function(raw, "setname", bot_setname, &context);
    put_function(raw, "score", bot_score, &context);
    put_function(raw, "players", bot_players, &context);
    put_function(raw, "pay", bot_pay, &context);
    put_function(raw, "take", bot_take, &context);
    put_function(raw, "say", bot_say, &context);
    put_function(raw, "close", bot_close, &context);
    put_function(raw, "number", bot_number, &context);
    put_function(raw, "lower", bot_lower, &context);
    lua_setglobal(raw, "bot");

    lua_pushlightuserdata(raw, &context);
    lua_setfield(raw, LUA_REGISTRYINDEX, "norelecbot_context");
    lua_sethook(raw, hook, LUA_MASKCOUNT, static_cast<int>(hook_every));
    return machine;
}

/* Carica il sorgente e lascia sullo stack la tabella che restituisce. */
bool load_table(lua_State *machine, std::string_view source, std::string &error) {
    if (luaL_loadbuffer(machine, source.data(), source.size(), "gioco") != LUA_OK) {
        error = text_argument(machine, -1);
        lua_pop(machine, 1);
        return false;
    }
    if (lua_pcall(machine, 0, 1, 0) != LUA_OK) {
        error = text_argument(machine, -1);
        lua_pop(machine, 1);
        return false;
    }
    if (lua_type(machine, -1) != LUA_TTABLE) {
        error = "lo script non restituisce una tabella";
        lua_pop(machine, 1);
        return false;
    }
    return true;
}

}

LuaRun lua_invoke(
    std::string_view source,
    std::string_view function,
    const LuaBotView &view,
    const std::vector<LuaArg> &args,
    const LuaLimits &limits
) {
    LuaRun run;
    Budget budget{.used = 0, .cap = limits.memory_bytes};
    Context context{
        .view = &view,
        .effects = &run.effects,
        .steps_left = limits.steps,
        .moved = 0,
        .may_move = view.pot * 2,
    };
    const Machine machine = fresh_machine(budget, context);
    if (!machine) {
        run.error = "memoria finita prima di cominciare";
        return run;
    }
    lua_State *raw = machine.get();
    if (!load_table(raw, source, run.error)) {
        return run;
    }
    lua_getfield(raw, -1, std::string{function}.c_str());
    if (lua_type(raw, -1) != LUA_TFUNCTION) {
        /* Un gioco che non risponde a questo momento del gioco non è un gioco rotto. */
        lua_pop(raw, 2);
        run.ok = true;
        return run;
    }
    /* Il primo argomento è sempre la tabella bot: gli script la ricevono, non la cercano fuori. */
    lua_getglobal(raw, "bot");
    for (const LuaArg &argument : args) {
        if (std::holds_alternative<std::int64_t>(argument)) {
            lua_pushinteger(raw, std::get<std::int64_t>(argument));
            continue;
        }
        const auto &text = std::get<std::string>(argument);
        lua_pushlstring(raw, text.data(), text.size());
    }
    if (lua_pcall(raw, static_cast<int>(args.size()) + 1, 0, 0) != LUA_OK) {
        run.error = text_argument(raw, -1);
        run.effects.clear();
        lua_pop(raw, 2);
        return run;
    }
    lua_pop(raw, 1);
    run.ok = true;
    return run;
}

std::optional<std::string> lua_field(
    std::string_view source,
    std::string_view field,
    const LuaLimits &limits
) {
    Budget budget{.used = 0, .cap = limits.memory_bytes};
    const LuaBotView empty;
    std::vector<LuaEffect> nowhere;
    Context context{
        .view = &empty,
        .effects = &nowhere,
        .steps_left = limits.steps,
        .moved = 0,
        .may_move = 0,
    };
    const Machine machine = fresh_machine(budget, context);
    if (!machine) {
        return std::nullopt;
    }
    lua_State *raw = machine.get();
    std::string error;
    if (!load_table(raw, source, error)) {
        log_error("Forged game will not load: {}", error);
        return std::nullopt;
    }
    lua_getfield(raw, -1, std::string{field}.c_str());
    if (lua_type(raw, -1) != LUA_TSTRING) {
        lua_pop(raw, 2);
        return std::nullopt;
    }
    auto found = text_argument(raw, -1);
    lua_pop(raw, 2);
    return found;
}

}
