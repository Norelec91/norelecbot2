#ifndef NORELECBOT_LUA_VM_HPP
#define NORELECBOT_LUA_VM_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace norelecbot {

/* Quello che uno script ha chiesto di fare. Lo script non tocca niente da solo: chiede, e il C++
   decide se e quanto concedere. */
struct LuaEffect {
    enum class Kind { pay, take, say, close };

    Kind kind = Kind::say;
    std::string who;
    std::string text;
    std::int64_t palle = 0;
};

/* Quello che lo script può guardare mentre gira. */
struct LuaBotView {
    std::int64_t pot = 0;
    std::function<std::int64_t(const std::string &)> score;
    std::function<std::vector<std::string>()> players;
    /* Un numero fra 0 e count - 1, dal generatore della sessione di storage. */
    std::function<std::int64_t(std::int64_t)> random;
    std::function<std::int64_t(const std::string &)> get;
    std::function<void(const std::string &, std::int64_t)> set;
    std::function<std::string(const std::string &)> name;
    std::function<void(const std::string &, const std::string &)> setname;
};

/* Quanto può costare una chiamata prima di essere interrotta. */
struct LuaLimits {
    std::int64_t steps = 200000;
    std::size_t memory_bytes = 4U * 1024U * 1024U;
};

using LuaArg = std::variant<std::int64_t, std::string>;

struct LuaRun {
    bool ok = false;
    std::string error;
    std::vector<LuaEffect> effects;
};

/* Carica lo script in una macchina nuova e chiama quella funzione della tabella che restituisce.
   Una funzione che manca non è un errore: lo script semplicemente non ha niente da dire. */
[[nodiscard]] LuaRun lua_invoke(
    std::string_view source,
    std::string_view function,
    const LuaBotView &view,
    const std::vector<LuaArg> &args,
    const LuaLimits &limits
);

/* Un campo di testo della tabella dello script, per esempio la parola chiave o l'annuncio. */
[[nodiscard]] std::optional<std::string> lua_field(
    std::string_view source,
    std::string_view field,
    const LuaLimits &limits
);

}

#endif
