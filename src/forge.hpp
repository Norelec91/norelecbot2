#ifndef NORELECBOT_FORGE_HPP
#define NORELECBOT_FORGE_HPP

#include "lua_vm.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace norelecbot {

/* Un gioco nato dalla forgia: la parola che lo chiama, la riga che lo annuncia, il suo codice. */
struct ForgedGame {
    std::string keyword;
    std::string family;
    std::string announce;
    std::string source;
};

/* Quello che serve per coniarne uno: le parole che il gruppo dice e quelle già occupate. */
struct ForgeRequest {
    /* Parola e quante volte è stata sentita, dalla più detta alla meno detta. */
    std::vector<std::pair<std::string, std::int64_t>> lexicon;
    std::vector<std::string> reserved;
};

using RandomSource = std::function<std::int64_t(std::int64_t)>;

/* Apre la cartella dei giochi generati e si ricorda cosa c'è dentro. Da chiamare una volta sola,
   all'avvio: senza, la forgia resta spenta e il bot si comporta come prima. */
void forge_open_catalogue(std::string directory, LuaLimits limits);
[[nodiscard]] bool forge_ready();
[[nodiscard]] std::size_t forge_count();
/* Le parole chiave, dalla più recente alla più vecchia. */
[[nodiscard]] std::vector<std::string> forge_recent(std::size_t most);
[[nodiscard]] bool forge_knows(const std::string &keyword);
[[nodiscard]] std::optional<std::string> forge_source(const std::string &keyword);
[[nodiscard]] std::optional<std::string> forge_announce_of(const std::string &keyword);
[[nodiscard]] std::optional<std::string> forge_any(const RandomSource &random);
[[nodiscard]] LuaLimits forge_limits();
/* Un gioco che si è rotto in partita non si riapre più. */
void forge_condemn(const std::string &keyword, std::string_view why);

/* Com'è andata una partita: serve a sapere quali giochi piacciono davvero. */
struct ForgeVerdict {
    std::string keyword;
    std::string family;
    std::int64_t players = 0;
    std::int64_t messages = 0;
    /* Quanti secondi sono passati fra l'apertura e la prima mossa; zero se non si è mosso nessuno. */
    std::int64_t first_move = 0;
    bool decided = false;
    std::int64_t palle = 0;
    std::int64_t at = 0;
};

/* Scrive una riga in coda al registro degli esiti. */
void forge_record(const ForgeVerdict &verdict);
[[nodiscard]] std::string forge_family_of(const std::string &keyword);

/* Conia un gioco nuovo, lo collauda a vuoto e lo scrive su disco. Restituisce niente se non è
   riuscito a farne uno che stia in piedi. */
[[nodiscard]] std::optional<ForgedGame> forge_mint(const ForgeRequest &request, const RandomSource &random);

/* Il collaudo, esposto perché i test lo possano usare su un sorgente qualsiasi. */
[[nodiscard]] bool forge_try_out(const std::string &source, std::string &why_not);

}

#endif
