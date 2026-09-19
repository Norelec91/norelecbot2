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
    Mishap{"🪿 Un'oca col tesserino da controllore ha multato {} di 1 palla per piumaggio irregolare.", -1, Boon::none},
    Mishap{"🎈 Il citofono di {} ha partorito un palloncino. Il condominio nega tutto.", 0, Boon::balloon},
    Mishap{"🛸 Un disco volante ha scambiato {} per il telecomando e l'ha posato altrove.", 0, Boon::teleport},
    Mishap{"🔋 {} ha leccato una pila trovata in sagrestia: il prossimo possesso vale il triplo.", 0, Boon::boost},
    Mishap{"🦚 Un pavone ha aperto una recensione a una stella contro {}.", 0, Boon::disliked},
    Mishap{"🪜 {} ha tenuto ferma una scala a un fantasma: il quartiere approva.", 0, Boon::liked},
    Mishap{"🧃 {} ha bevuto succo al gusto proroga: il prossimo possesso vale il triplo.", 0, Boon::boost},
    Mishap{"🔺 Un triangolo col monocolo ha nominato {} custode del lato segreto.", 0, Boon::liked},
    Mishap{"🎯 {} ha mancato un bersaglio disegnato sul proprio gomito: -3 palle di dignità.", -3, Boon::none},
    Mishap{"🌮 Il taco di {} ha emesso lo scontrino: dentro c'erano 2 palle di resto.", 2, Boon::none},
    Mishap{"📯 Un airhorn senziente si è trasferito nel comodino di {}.", 0, Boon::disliked},
    Mishap{"⚖️ La penalità di {} è stata annullata perché il giudice era tre procioni in toga.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice ha restituito a {} un calzino solo, trattenendo 2 palle di cauzione.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una sedia al contrario e ora il soffitto può sedersi.", 0, Boon::none},
    Mishap{"🧲 Una calamita sindacalista ha recuperato 1 palla arretrata per {}.", 1, Boon::none},
    Mishap{"🎺 Il frigorifero di {} ha preso lezioni di tromba. È già solista.", 0, Boon::disliked},
    Mishap{"🗺️ Il navigatore ha dichiarato {} territorio d'oltremare e l'ha spostato sulla mappa.", 0, Boon::teleport},
    Mishap{"🍝 {} ha messo il ketchup sugli spaghetti e il senato ha aperto un fascicolo.", 0, Boon::disliked},
    Mishap{"🎰 Una slot per tostapane ha pagato a {} un dividendo di 1 palla.", 1, Boon::none},
    Mishap{"🐜 Una formica col muletto ha requisito 1 palla a {} per lavori in corso.", -1, Boon::none},
    Mishap{"🥚 {} ha aperto un uovo sodo: dentro c'era un palloncino già maggiorenne.", 0, Boon::balloon},
    Mishap{"🦶 Il mignolo di {} ha sfidato lo spigolo: lo spigolo incassa 3 palle.", -3, Boon::none},
    Mishap{"☕ {} ha offerto il caffè a due statue: -2 palle, ma le statue mettono like.", -2, Boon::liked},
    Mishap{"🚪 {} ha bussato a una porta aperta. La porta ha risposto «avanti».", 0, Boon::none},
    Mishap{"🌀 Una rotonda tascabile ha fatto uscire {} dal comune sbagliato.", 0, Boon::teleport},
    Mishap{"🧮 L'abaco di {} ha starnutito e sono comparse 2 palle non contabilizzate.", 2, Boon::none},
    Mishap{"🥖 Una baguette ha chiesto a {} 1 palla per non raccontare cosa ha visto.", -1, Boon::none},
    Mishap{"🗿 {} ha discusso con un busto di marmo. Il busto vuole la rivincita.", 0, Boon::none},
    Mishap{"🧊 {} ha brevettato il ghiaccio tiepido e non sa più dove metterlo.", 0, Boon::none},
    Mishap{"🐂 Un consulente taurino ha fatturato a {} 2 palle per una call senza audio.", -2, Boon::none},
    Mishap{"📜 {} è stato nominato feudatario del parcheggio multipiano.", 0, Boon::liked},
    Mishap{"🧘 Un biscotto della fortuna ha convinto {} a liberarsi di 1 palla materiale.", -1, Boon::none},
    Mishap{"📰 Un giornale di domani ha smentito {} ieri: tutti indignati in anticipo.", 0, Boon::disliked},
    Mishap{"💬 Una chat di tostapane ha eletto {} commento memorabile della settimana.", 0, Boon::liked},
    Mishap{"🏫 La gita scolastica di {} era nel corridoio: caparra non rimborsabile di 2 palle.", -2, Boon::none},
    Mishap{"🃏 L'enciclopedia ha aggiunto una nota su {}: «fonte: me l'ha detto un piccione».", 0, Boon::disliked},
    Mishap{"🧔 Un barbiere metafisico ha dichiarato {} ufficialmente friendzonato dal destino.", 0, Boon::disliked},
    Mishap{"🍕 Una pizza perfettamente quadrata ha premiato {} con 1 palla rotonda.", 1, Boon::none},
    Mishap{"🎭 {} ha raccontato una battuta a un citofono. Il citofono chiede spiegazioni.", 0, Boon::none},
    Mishap{"🍺 Il sindacato dei bicchieri vuoti ha offerto una birra immaginaria a {}.", 0, Boon::liked},
    Mishap{"📸 Una fototessera del 2016 ha risucchiato {} e l'ha ristampato altrove.", 0, Boon::teleport},
    Mishap{"🕶️ Due occhiali da sole sono esplosi alle spalle di {}: prossimo possesso triplo.", 0, Boon::boost},
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
