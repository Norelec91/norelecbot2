#ifndef NORELECBOT_MISHAPS_HPP
#define NORELECBOT_MISHAPS_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace norelecbot {

/* Small things that happen to a player for no reason: a palla or two, most often nothing at all. */
struct Mishap {
    std::string_view text;
    std::int64_t palle;
};

inline constexpr std::array mishaps{
    Mishap{"🕳️ {} è inciampato in una buca e ha perso 1 palla.", -1},
    Mishap{"🤧 {} ha starnutito e una palla è rotolata via.", -1},
    Mishap{"🛋️ {} ha trovato 2 palle sotto il divano.", 2},
    Mishap{"🤝 {} ha prestato una palla a un amico. Non la rivedrà.", -1},
    Mishap{"☀️ {} ha lasciato le palle al sole: si sono un po' sgonfiate.", 0},
    Mishap{"🥤 {} ha litigato con un distributore automatico. Ha vinto il distributore.", 0},
    Mishap{"🃏 {} ha perso 3 palle a carte.", -3},
    Mishap{"🕊️ Un gabbiano ha portato via una palla a {}.", -1},
    Mishap{"🧥 {} ha trovato 1 palla nel cappotto dell'inverno scorso.", 1},
    Mishap{"🌑 {} ha calpestato una palla al buio.", 0},
    Mishap{"🅿️ {} ha pagato 2 palle di parcheggio.", -2},
    Mishap{"🪙 {} ha vinto 1 palla a testa o croce.", 1},
    Mishap{"🚆 {} ha dimenticato le palle sul treno. Gliele hanno restituite tutte.", 0},
    Mishap{"☕ {} ha pagato 1 palla per un caffè che non ha nemmeno bevuto.", -1},
    Mishap{"👮 {} è stato fermato per un controllo. Tutto in regola.", 0},
    Mishap{"🔋 A {} si è scaricato il telefono sul più bello.", 0},
    Mishap{"📬 {} ha ricevuto 1 palla di rimborso per un reclamo del 2019.", 1},
    Mishap{"💩 {} ha camminato dove non doveva. Dicono che porti fortuna.", 0},
    Mishap{"🧺 {} ha perso 2 palle in lavatrice.", -2},
    Mishap{"🚗 {} ha trovato parcheggio al primo colpo.", 0},
    Mishap{"🔨 Il vicino di {} ha trapanato il muro per due ore.", 0},
    Mishap{"🍕 {} ha ordinato una pizza e gliene hanno portata un'altra.", 0},
    Mishap{"💸 {} ha ricevuto 1 palla di resto sbagliato e ha fatto finta di niente.", 1},
    Mishap{"🕳️ A {} è caduta una palla in un tombino.", -1},
    Mishap{"📜 {} ha letto le condizioni d'uso fino in fondo. Non è successo niente.", 0},
};

}

#endif
