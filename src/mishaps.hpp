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
    grandfather,
    halved,
};

/* Things that happen to a player for no reason at all. */
struct Mishap {
    std::string_view text;
    std::int64_t palle;
    Boon boon;
};

inline constexpr std::array mishaps{
    Mishap{"🕳️ {} è inciampato in una buca che ieri non c'era: -1 palla.", -1, Boon::none},
    Mishap{"🎈 A {} è arrivato un palloncino per posta, mittente illeggibile.", 0, Boon::balloon},
    Mishap{"🛸 {} è stato prelevato da una luce nel cielo e riconsegnato dall'altra parte della mappa.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto una bibita scaduta nel 2014 e vede i colori: prossimo possesso triplo.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha guardato {} con evidente disprezzo.", 0, Boon::disliked},
    Mishap{"👵 {} ha aiutato una signora ad attraversare e l'hanno visto tutti.", 0, Boon::liked},
    Mishap{"🔺 {} ha girato l'angolo e c'era un triangolo che lo fissava. ILLUMINATI.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato un 360 noscope contro un muro: -3 palle e un mignolo a pezzi.", -3, Boon::none},
    Mishap{"🌮 A {} sono rotolate 2 palle dentro un sacchetto di patatine, ritrovate.", 2, Boon::none},
    Mishap{"📢 Un airhorn è partito alle quattro del mattino in casa di {}.", 0, Boon::disliked},
    Mishap{"🚔 La penalità di {} è stata annullata per vizio di forma.", 0, Boon::forgiven},
    Mishap{"🧦 A {} è sparito un calzino con dentro 2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una sedia svedese e gli è avanzata una vite.", 0, Boon::none},
    Mishap{"🧲 {} ha trovato 1 palla attaccata alla calamita del frigo.", 1, Boon::none},
    Mishap{"🎺 Il vicino di {} studia tromba. Di notte. Da tre settimane.", 0, Boon::disliked},
    Mishap{"🗺️ Il navigatore di {} è fermo al 1987 e l'ha portato da un'altra parte.", 0, Boon::teleport},
    Mishap{"🍝 {} ha spezzato gli spaghetti a metà. Il gruppo ha preso nota.", 0, Boon::disliked},
    Mishap{"🎰 {} ha vinto 1 palla a una slot di un autogrill.", 1, Boon::none},
    Mishap{"🐜 Una formica ha portato via 1 palla di {}. Piano piano.", -1, Boon::none},
    Mishap{"🎁 {} ha aperto un uovo di Pasqua a settembre: dentro c'era un palloncino.", 0, Boon::balloon},
    Mishap{"🦶 {} ha preso lo spigolo col mignolo: 3 palle di dolore.", -3, Boon::none},
    Mishap{"☕ {} ha offerto il caffè a tutti: 2 palle spese bene.", -2, Boon::liked},
    Mishap{"🚪 {} ha spinto per dieci secondi una porta con scritto TIRARE.", 0, Boon::none},
    Mishap{"🌀 {} è entrato in una rotonda e ne è uscito da tutt'altra parte.", 0, Boon::teleport},
    Mishap{"🧮 {} ha ricontato le palle tre volte e gliene sono tornate 2 in più.", 2, Boon::none},
    Mishap{"🥖 {} ha lasciato il pane dal fornaio, con 1 palla nel sacchetto.", -1, Boon::none},
    Mishap{"🗿 {} ha fissato un muro per otto minuti. Ha vinto il muro.", 0, Boon::none},
    Mishap{"🧊 {} ha messo l'acqua in freezer per raffreddarla e se n'è dimenticato.", 0, Boon::none},
    Mishap{"🐂 Il Milanese Imbruttito ha detto a {} di fatturare: 2 palle di consulenza.", -2, Boon::none},
    Mishap{"📜 Feudalesimo e Libertà ha nominato {} vassallo del contado.", 0, Boon::liked},
    Mishap{"🧘 Una frase di Osho ha convinto {} a lasciar andare 1 palla.", -1, Boon::none},
    Mishap{"📰 Lercio ha pubblicato una notizia falsa su {} e ci hanno creduto tutti.", 0, Boon::disliked},
    Mishap{"💬 Commenti Memorabili ha screenshottato {}: famoso suo malgrado.", 0, Boon::liked},
    Mishap{"🏫 ScuolaZoo ha organizzato la gita e {} ha pagato la caparra: -2 palle.", -2, Boon::none},
    Mishap{"🃏 Nonciclopedia ha scritto la voce su {}. Era tutto vero.", 0, Boon::disliked},
    Mishap{"🧔 Il Signor Distruggere ha condiviso un post di {}: BOOM FRIENDZONED!", 0, Boon::disliked},
    Mishap{"🍕 Very Normal People ha invitato {} a una cena normalissima: +1 palla.", 1, Boon::none},
    Mishap{"🎭 Spinoza ha fatto una battuta su {} e non l'ha capita nessuno.", 0, Boon::none},
    Mishap{"🍺 I Socialisti Gaudenti hanno offerto una birra a {}.", 0, Boon::liked},
    Mishap{"📸 {} è finito su una pagina di meme del 2016 e non se n'è più liberato.", 0, Boon::teleport},
    Mishap{"🕶️ {} ha ricevuto gli occhiali da sole in faccia al rallentatore. DEAL WITH IT.", 0, Boon::boost},
    Mishap{"🛗 {} è rimasto in ascensore con uno che spiegava le criptovalute.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato 2 palle nella tasca di un cappotto dell'altro inverno.", 2, Boon::none},
    Mishap{"🚦 {} ha preso otto semafori rossi di fila e ha imparato la pazienza.", 0, Boon::liked},
    Mishap{"📻 La radio di {} si è sintonizzata da sola su una stazione che non esiste.", 0, Boon::teleport},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} ha preso l'uscita di servizio: -500 palle inghiottite dal mobile.", -500, Boon::none},
    Mishap{"🎪 KICKBACK di {}: il flipper restituisce +250 palle e pretende un inchino.", 250, Boon::liked},
    Mishap{"💥 JACKPOT per {}: tutto raddoppia e compare IL FAMOSO NONNO con 3 palle.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera nasce un palloncino.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: +1000 palle cadono dal controsoffitto.", 1000, Boon::none},
    Mishap{"🚨 {} dà una gomitata al destino. TILT: metà delle palle prende il bus.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il bordo della mappa e richiude il passaggio.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: +100 palle di straordinario.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità viene respinta per calligrafia sospetta.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e suona una sirena.", 0, Boon::boost},
};

/* Numeric part of a target hit. The jackpot doubles first, then the grandfather adds his gift. */
[[nodiscard]] constexpr std::int64_t flipper_score_after(std::int64_t before, const Mishap &what) {
    if (what.boon == Boon::grandfather) {
        return before * 2 + what.palle;
    }
    if (what.boon == Boon::halved) {
        return before / 2;
    }
    return before + what.palle;
}

}

#endif
