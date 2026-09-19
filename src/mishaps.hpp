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
    /* Takes fifty palle off somebody else, or hands them over. */
    steal,
    donate,
    /* Trades places in the ledger with whoever it lands on. */
    swap,
    /* Loses what he was keeping for the next fight. */
    pop,
    flat,
    /* Asks for trouble, or gets out of it. */
    marked,
    freed,
    restored,
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
    Mishap{"🛸 {} è stato prelevato da una luce nel cielo e riconsegnato altrove.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto una bibita scaduta nel 2014: prossimo possesso triplo.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha guardato {} con evidente disprezzo.", 0, Boon::disliked},
    Mishap{"👵 {} ha aiutato una signora ad attraversare e l'hanno visto tutti.", 0, Boon::liked},
    Mishap{"🔺 Un triangolo col monocolo ha nominato {} custode del lato segreto.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato un 360 noscope contro un muro: -3 palle.", -3, Boon::none},
    Mishap{"🌮 A {} sono rotolate 2 palle dentro un sacchetto di patatine.", 2, Boon::none},
    Mishap{"📢 Un airhorn è partito alle quattro del mattino in casa di {}.", 0, Boon::disliked},
    Mishap{"🚔 La penalità di {} è stata annullata per vizio di forma.", 0, Boon::forgiven},
    Mishap{"🧦 A {} è sparito un calzino con dentro 2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una sedia svedese e gli è avanzata una vite.", 0, Boon::none},
    Mishap{"🧲 {} ha trovato 1 palla attaccata alla calamita del frigo.", 1, Boon::none},
    Mishap{"🎺 Il vicino di {} studia tromba. Di notte. Da tre settimane.", 0, Boon::disliked},
    Mishap{"🗺️ Il navigatore di {} è fermo al 1987 e l'ha portato altrove.", 0, Boon::teleport},
    Mishap{"🍝 {} ha spezzato gli spaghetti a metà. Il gruppo ha preso nota.", 0, Boon::disliked},
    Mishap{"🎰 {} ha vinto 1 palla a una slot di un autogrill.", 1, Boon::none},
    Mishap{"🐜 Una formica ha portato via 1 palla di {}. Piano piano.", -1, Boon::none},
    Mishap{"🎁 {} ha aperto un uovo di Pasqua a settembre: dentro c'era un palloncino.", 0, Boon::balloon},
    Mishap{"🦶 {} ha preso lo spigolo col mignolo: 3 palle di dolore.", -3, Boon::none},
    Mishap{"☕ {} ha offerto il caffè a tutti: 2 palle spese bene.", -2, Boon::liked},
    Mishap{"🚪 {} ha spinto per dieci secondi una porta con scritto TIRARE.", 0, Boon::none},
    Mishap{"🌀 {} è entrato in una rotonda e ne è uscito da tutt'altra parte.", 0, Boon::teleport},
    Mishap{"🧮 {} ha ricontato le palle tre volte e gliene tornano 2 in più.", 2, Boon::none},
    Mishap{"🥖 {} ha lasciato il pane dal fornaio, con 1 palla nel sacchetto.", -1, Boon::none},
    Mishap{"🗿 {} ha fissato un muro per otto minuti. Ha vinto il muro.", 0, Boon::none},
    Mishap{"🧊 {} ha messo l'acqua in freezer per raffreddarla e se n'è scordato.", 0, Boon::none},
    Mishap{"🐂 Il Milanese Imbruttito ha detto a {} di fatturare: -2 palle.", -2, Boon::none},
    Mishap{"📜 Feudalesimo e Libertà ha nominato {} vassallo del contado.", 0, Boon::liked},
    Mishap{"🧘 Una frase di Osho ha convinto {} a lasciar andare 1 palla.", -1, Boon::none},
    Mishap{"📰 Lercio ha pubblicato una notizia falsa su {} e ci han creduto tutti.", 0, Boon::disliked},
    Mishap{"💬 Commenti Memorabili ha screenshottato {}: famoso suo malgrado.", 0, Boon::liked},
    Mishap{"🏫 ScuolaZoo ha organizzato la gita e {} ha pagato la caparra.", -2, Boon::none},
    Mishap{"🃏 Nonciclopedia ha scritto la voce su {}. Era tutto vero.", 0, Boon::disliked},
    Mishap{"🧔 Il Signor Distruggere ha condiviso un post di {}: BOOM FRIENDZONED!", 0, Boon::disliked},
    Mishap{"🍕 Very Normal People ha invitato {} a una cena normalissima.", 1, Boon::none},
    Mishap{"🎭 Spinoza ha fatto una battuta su {} e non l'ha capita nessuno.", 0, Boon::none},
    Mishap{"🍺 I Socialisti Gaudenti hanno offerto una birra a {}.", 0, Boon::liked},
    Mishap{"📸 {} è finito su una pagina di meme del 2016 e non se n'è più liberato.", 0, Boon::teleport},
    Mishap{"🕶️ {} ha ricevuto gli occhiali da sole al rallentatore. DEAL WITH IT.", 0, Boon::boost},
    Mishap{"🛗 {} è rimasto in ascensore con uno che spiegava le criptovalute.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato 2 palle nel cappotto dell'altro inverno.", 2, Boon::none},
    Mishap{"🚦 {} ha preso otto semafori rossi di fila e ha imparato la pazienza.", 0, Boon::liked},
    Mishap{"📻 La radio di {} si è sintonizzata su una stazione che non esiste.", 0, Boon::teleport},
    Mishap{"🦷 {} ha morso un torrone e ha lasciato 3 palle dal dentista.", -3, Boon::none},
    Mishap{"🧾 {} ha trovato uno scontrino del 2011 e valeva ancora 2 palle.", 2, Boon::none},
    Mishap{"🪤 {} ha messo una trappola e ci è finito dentro lui.", 0, Boon::pop},
    Mishap{"🐍 Un serpente si è mangiato il palloncino di {}.", 0, Boon::pop},
    Mishap{"🧨 Il boost di {} è esploso prima del tempo.", 0, Boon::flat},
    Mishap{"🫧 {} ha starnutito e il suo boost è svanito.", 0, Boon::flat},
    Mishap{"🤝 {} ha stretto la mano a uno sconosciuto e gli ha dato 50 palle.", 0, Boon::donate},
    Mishap{"🫳 {} ha trovato il portafoglio di qualcun altro e non l'ha restituito.", 0, Boon::steal},
    Mishap{"🔁 {} e un altro si sono scambiati la vita per sbaglio.", 0, Boon::swap},
    Mishap{"📿 {} ha detto tre avemarie e la simpatia è tornata piena.", 0, Boon::restored},
    Mishap{"🔓 Kio ha mollato la presa su {} senza dare spiegazioni.", 0, Boon::freed},
    Mishap{"🎯 {} ha alzato troppo la voce: adesso è il bersaglio designato.", 0, Boon::marked},
    Mishap{"🚿 {} ha cantato sotto la doccia e i vicini hanno applaudito.", 0, Boon::liked},
    Mishap{"🧻 {} ha finito la carta igienica nel momento peggiore.", 0, Boon::disliked},
    Mishap{"🦟 Una zanzara ha tenuto sveglio {} fino alle cinque.", -1, Boon::none},
    Mishap{"🛞 {} ha bucato una gomma e l'ha cambiata da solo. Applausi.", -2, Boon::liked},
    Mishap{"🍋 {} ha morso un limone convinto fosse un'arancia.", 0, Boon::none},
    Mishap{"📦 Un pacco per {} è stato consegnato a un vicino che non esiste.", -1, Boon::none},
    Mishap{"🎪 {} è stato scelto dal mago per salire sul palco.", 0, Boon::liked},
    Mishap{"🧯 {} ha scaricato un estintore per sbaglio in salotto.", -2, Boon::none},
    Mishap{"🪟 {} ha lavato i vetri e ha piovuto dieci minuti dopo.", 0, Boon::disliked},
    Mishap{"🛏️ {} ha dormito male e ha sognato la classifica.", 0, Boon::none},
    Mishap{"🚲 A {} hanno rubato la bici e gli hanno lasciato un biglietto di scuse.", -2, Boon::liked},
    Mishap{"🧃 {} ha bucato il succo col cannuccino al primo colpo.", 1, Boon::none},
    Mishap{"🐕 Il cane di {} ha sotterrato 2 palle in giardino.", -2, Boon::none},
    Mishap{"🐈 Un gatto ha deciso di vivere da {}. Non era invitato.", 0, Boon::liked},
    Mishap{"🍀 {} ha trovato un quadrifoglio e se l'è dimenticato in tasca.", 1, Boon::none},
    Mishap{"🧱 {} ha preso il muro in retromarcia. Il muro sta bene.", -3, Boon::none},
    Mishap{"🎬 {} è comparso sullo sfondo di un servizio del TG.", 0, Boon::liked},
    Mishap{"🕰️ L'orologio di {} è indietro di venti minuti da un anno.", 0, Boon::none},
    Mishap{"🧤 {} ha perso un guanto. Solo uno. Sempre lo stesso.", 0, Boon::none},
    Mishap{"🪆 {} ha aperto una matrioska e dentro c'era un palloncino.", 0, Boon::balloon},
    Mishap{"⛽ {} ha fatto il pieno il giorno prima del ribasso.", -3, Boon::none},
    Mishap{"🧑‍⚖️ Un giudice di pace ha dato ragione a {} su una cosa del 2014.", 2, Boon::liked},
    Mishap{"📉 {} ha investito in una piramide di cugini.", -3, Boon::none},
    Mishap{"🎣 {} ha pescato una palla dal fiume.", 1, Boon::none},
    Mishap{"🏔️ {} si è perso in montagna ed è sceso da un altro versante.", 0, Boon::teleport},
    Mishap{"🚁 Un elicottero ha depositato {} da un'altra parte per errore.", 0, Boon::teleport},
    Mishap{"🥶 {} ha lasciato la finestra aperta e ora il boost è congelato.", 0, Boon::flat},
    Mishap{"🫀 {} ha fatto un gesto gentile e nessuno lo ha visto. Quasi nessuno.", 0, Boon::liked},
    Mishap{"🤡 {} è caduto dalle scale mobili in senso contrario.", -1, Boon::disliked},
    Mishap{"🧠 {} ha ricordato una password di dodici anni fa.", 2, Boon::none},
    Mishap{"📮 Una lettera del 1998 è arrivata a {} con 2 palle dentro.", 2, Boon::none},
    Mishap{"🪃 Il boomerang di {} è tornato indietro e ha bucato il palloncino.", 0, Boon::pop},
    Mishap{"🧳 La valigia di {} è partita per un'altra città.", 0, Boon::teleport},
    Mishap{"🎻 {} ha ascoltato un violinista in metro e ha lasciato 50 palle.", 0, Boon::donate},
    Mishap{"🫰 {} ha schioccato le dita e 50 palle sono passate di tasca.", 0, Boon::steal},
    Mishap{"🛎️ {} ha suonato un campanello e è scappato. Come nel 1999.", 0, Boon::disliked},
    Mishap{"🪩 {} è andato a ballare e ha conosciuto tutti.", 0, Boon::restored},
    Mishap{"🦴 {} ha trovato un osso di dinosauro in cortile.", 3, Boon::none},
    Mishap{"🧂 {} ha rovesciato il sale e ha deciso di non crederci.", -1, Boon::none},
    Mishap{"🎤 {} ha vinto il karaoke con una canzone di Ranieri.", 2, Boon::liked},
    Mishap{"🚀 {} ha guardato il cielo e ha visto passare Kio.", 0, Boon::marked},
    Mishap{"🧊 {} ha vinto una gara di resistenza al freddo contro nessuno.", 1, Boon::none},
    Mishap{"🍇 {} ha calpestato l'uva e ha fatto il vino. Due palle di fatica.", -2, Boon::liked},
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
