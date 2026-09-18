#ifndef NORELECBOT_MISHAPS_HPP
#define NORELECBOT_MISHAPS_HPP

#include <array>
#include <cstdint>
#include <string_view>

namespace norelecbot {

/* What a mishap leaves behind besides the palle. */
enum class Boon {
    none,
    balloon,
    boost,
    teleport,
    liked,
    disliked,
    forgiven,
    doubled,
    halved,
};

/* Things that happen to a player for no reason at all. */
struct Mishap {
    std::string_view text;
    std::int64_t palle;
    Boon boon;
};

inline constexpr std::array mishaps{
    Mishap{"🕳️ {} è inciampato in una buca che ieri non c'era e ha perso 1 palla.", -1, Boon::none},
    Mishap{"🎈 A {} è arrivato un palloncino per posta. Il mittente è illeggibile.", 0, Boon::balloon},
    Mishap{"🛸 {} è stato prelevato da una luce nel cielo e riconsegnato altrove.", 0, Boon::teleport},
    Mishap{"⚡ {} ha bevuto una bibita energetica scaduta nel 2014: il prossimo possesso vale il triplo.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha guardato {} con evidente disprezzo.", 0, Boon::disliked},
    Mishap{"👵 {} ha aiutato una signora ad attraversare e tutti l'hanno visto.", 0, Boon::liked},
    Mishap{"🚔 La penalità di {} è stata annullata per vizio di forma.", 0, Boon::forgiven},
    Mishap{"🧦 A {} è sparito un calzino con dentro 2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una sedia svedese e gli è avanzata una vite.", 0, Boon::none},
    Mishap{"🧲 {} ha trovato 1 palla attaccata alla calamita del frigo.", 1, Boon::none},
    Mishap{"🎺 Il vicino di {} ha iniziato a studiare tromba.", 0, Boon::disliked},
    Mishap{"🗺️ {} ha seguito un navigatore aggiornato al 1987 ed è finito da un'altra parte.", 0, Boon::teleport},
    Mishap{"🍝 {} ha spezzato gli spaghetti a metà. Il gruppo ha preso nota.", 0, Boon::disliked},
    Mishap{"🎰 {} ha vinto 1 palla a una slot machine di un autogrill.", 1, Boon::none},
    Mishap{"🐜 Una formica ha portato via 1 palla di {}. Piano piano.", -1, Boon::none},
    Mishap{"🎁 {} ha aperto un uovo di Pasqua di marzo e dentro c'era un palloncino.", 0, Boon::balloon},
    Mishap{"🦶 {} ha preso lo spigolo col mignolo: 3 palle di dolore.", -3, Boon::none},
    Mishap{"☕ {} ha offerto il caffè a tutti quanti. Gli è costato 2 palle e ne è valsa la pena.", -2, Boon::liked},
    Mishap{"🚪 {} ha spinto per dieci secondi una porta con scritto TIRARE.", 0, Boon::none},
    Mishap{"🌀 {} ha girato in tondo in una rotonda per venti minuti e ne è uscito da un'altra uscita.", 0, Boon::teleport},
    Mishap{"🧮 {} ha ricontato le palle tre volte e gliene sono tornate 2 in più.", 2, Boon::none},
    Mishap{"🥖 {} ha lasciato il pane dal fornaio, con 1 palla nel sacchetto.", -1, Boon::none},
    Mishap{"🗿 {} ha fissato un muro per otto minuti. Ha vinto il muro.", 0, Boon::none},
    Mishap{"⚡ {} ha toccato una presa con le mani bagnate e adesso brilla: il prossimo possesso vale il triplo.", 0, Boon::boost},
    Mishap{"🧊 {} ha messo l'acqua in freezer per raffreddarla e se n'è dimenticato.", 0, Boon::none},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🎯 {} ha centrato un bumper: +100 palle.", 100, Boon::none},
    Mishap{"💥 {} ha fatto JACKPOT: le palle raddoppiano.", 0, Boon::doubled},
    Mishap{"🚨 {} ha scosso troppo il tavolo. TILT: metà delle palle se ne vanno.", 0, Boon::halved},
    Mishap{"🔵 EXTRA BALL per {}: un palloncino esce dalla buca.", 0, Boon::balloon},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo.", 0, Boon::boost},
    Mishap{"🕳️ La palla di {} è finita in buca centrale: -500 palle.", -500, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità è cancellata.", 0, Boon::forgiven},
    Mishap{"⚡ {} ha preso la rampa a tutta velocità ed è finito da un'altra parte della mappa.", 0, Boon::teleport},
    Mishap{"🎪 {} ha colpito il kickback: +250 palle e un applauso.", 250, Boon::liked},
    Mishap{"🏁 {} ha abbattuto tutti i target: +1000 palle.", 1000, Boon::none},
};

}

#endif
