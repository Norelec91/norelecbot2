#ifndef NORELECBOT_MISHAPS_HPP
#define NORELECBOT_MISHAPS_HPP

#include <array>
#include <cstdint>
#include <limits>
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
    /* A tenth of what he has, in or out, and a free ticket when the draw is open. */
    windfall,
    tithe,
    ticket,
};

/* Things that happen to a player for no reason at all. */
struct Mishap {
    std::string_view text;
    std::int64_t palle;
    Boon boon;
};

inline constexpr std::array mishaps{
    Mishap{"🕳️ La buca davanti a casa di {} ha ottenuto una targa commemorativa e un piccolo contributo: -1 palla.", -1, Boon::none},
    Mishap{"🎈 Un palloncino ha chiesto udienza a {} e si è presentato con due testimoni.", 0, Boon::balloon},
    Mishap{"🛸 {} è stato prelevato da un'astronave che cercava un bagno e lo ha lasciato in un'altra regione.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto un amaro digestivo del 1998 e per un turno rende il triplo, parlando in latino.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha citato {} davanti al giudice di pace per uso improprio dello stagno.", 0, Boon::disliked},
    Mishap{"👵 {} ha spiegato lo smartphone a una signora per un'ora intera senza mai alzare la voce.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha affidato a {} le chiavi dell'ipotenusa.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha abbattuto l'albero di Natale del comune: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle sono rotolate dentro la piadina di {} e si rifiutano di uscire.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato dentro il comodino di {} per tutta la notte di Capodanno.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è stato annullato perché il vigile risultava in pensione dal 1994.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha aperto una posizione contributiva a nome del calzino: -2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è uscita una cappella votiva.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio dentro il libretto delle istruzioni del forno.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha degradato {} a semplice passante.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in una rotonda e ne è uscito in una provincia che non confina con la sua.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette il notiziario di un paese che risulta disabitato.", 0, Boon::disliked},
    Mishap{"🧾 {} ha trovato uno scontrino di trent'anni fa e la cassa gli ha dato una palla senza discutere.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e l'intero condominio è tornato all'analogico.", -1, Boon::none},
    Mishap{"🎈 Un palloncino è emerso dal tombino con una raccomandata indirizzata a {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} si è aperto durante il pranzo di Natale e ha parlato: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e il comune gli ha fatturato due palle di anidride carbonica.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto di essere trasferito in un'altra stanza per incompatibilità.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è arrivato in un piano dove parlano un dialetto diverso.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e gli ha affidato un segreto di famiglia.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da tredici minuti e adesso ha anche un tesserino.", -1, Boon::none},
    Mishap{"📠 Il fax del 1998 è tornato a casa di {} per confermare la conferma della convocazione.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha dichiarato lo stato di agitazione.", 0, Boon::none},
    Mishap{"🪤 {} è inciampato in una trappola per talpe di un ente sciolto nel 1976: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha conferito a {} la cittadinanza onoraria del marciapiede.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita e con lei la scorta, la riserva e la speranza di {}.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha impugnato davanti al TAR la presenza di {} sul pianerottolo.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto dal pino e i pompieri gli hanno offerto un posto in squadra.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha svoltato da solo e ha abbattuto la piramide delle offerte: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar ha chiuso cinque minuti per riflettere.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito nel microfono durante la lettura del testamento.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nella tasca interna di un cappotto sotto sequestro.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e il muro ha aggiunto una fila per dispetto.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} e ha chiesto il bis a sé stessa.", 0, Boon::liked},
    Mishap{"🌌 Il varco nella dispensa di {} porta in una dispensa uguale, con i legumi già scaduti.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, illuminando la stanza.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a stomaco vuoto durante una seduta di consiglio comunale.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha dato a {} due palle e ha chiesto scusa attraverso il display.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato tutte le piante del palazzo e ha lasciato un biglietto a ciascuna.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha portato {} dall'altra parte della mappa, con l'accappatoio.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala priva di un gradino per ragioni di bilancio regionale: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e una fotografia di casa sua.", 2, Boon::none},
    Mishap{"🎈 Il palloncino di {} è stato bucato dalla Guardia di Finanza e messo agli atti.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato in segno di lutto per un altro palloncino.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato sequestrato in via cautelare dal nucleo speciale.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato in fila allo sportello, al numero prima del suo.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a una fondazione che si occupa di fondazioni.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un signore che leggeva l'orario dei treni.", 0, Boon::steal},
    Mishap{"🔄 L'anagrafe ha stabilito che {} e un altro giocatore sono la stessa persona dal 1981.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e ha bruciato il fascicolo in cortile.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché l'ordine di custodia era scritto in stampatello dubbio.", 0, Boon::freed},
    Mishap{"🎯 Il nome di {} è stato iscritto in un registro nero, timbrato e plastificato.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila ha chiesto di adottarlo.", 0, Boon::liked},
    Mishap{"🧨 {} ha mandato sette vocali consecutivi per dire che non poteva parlare.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è passata a gelata nel preciso istante del risciacquo.", -1, Boon::none},
    Mishap{"🤝 {} ha riportato un portafoglio pieno al proprietario, in taxi, di notte: -2 palle.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre il limite e il limite si è spostato.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} torna indietro di un minuto al giorno e nessuno riesce a fermarlo.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato a tutti un compleanno che il festeggiato aveva fatto sparire: -2 palle.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore ha sparato su {} mezza tanica di sapone davanti al consiglio di amministrazione.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme con le note a piè di pagina e la bibliografia.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il tavolo lo ha seguito fino alla chiusura del locale.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da scollegato, da un'altra stanza e ora anche da casa dei vicini: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta già iscritta a bilancio come perdita.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e ha rifiutato ogni spiegazione: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui, oliato la catena e regolato il sellino.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno per tredici ore e il pane è entrato in un museo.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e ha consegnato al piano sbagliato: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate si sono attaccate a {} durante una cena con il vescovo.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un solo giro, in suo onore.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto la residenza.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e ha ricevuto la fattura dei sette anni con le spese di incasso: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e la sala si è complimentata lo stesso.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto di nascosto, ora affissa in portineria: -3 palle.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito per non ammetterlo.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha sollevato {} sopra il campanile e lo ha posato in un altro quartiere.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana al contrario e ha fatto amicizia con il capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto di poterla copiare.", 0, Boon::liked},
    Mishap{"🐝 Un'ape ha aperto una filiale sulla testa di {}: -1 palla di affitto.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per non fare la fila e lo ha raccontato a quattro persone.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto sullo spigolo per due palle e un lungo silenzio.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha rispettato il piano di volo e l'orario.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha pregato di non farne parola con nessuno.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito di cinquanta palle un tale che stava leggendo gli orari dei treni.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto l'annuncio di una chiusura.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato per un ricorso presentato da una persona che non lo conosce.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle nette e un caffè.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha piantato due puntine sulla mappa dove si trova {}, e poi ha cerchiato il punto.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato tre volte la stessa storia alla fermata e ha chiesto di risentirla: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto scritto in corsivo inglese.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è finito su una lista appesa in portineria, e la portineria fa nomi.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha preso a {} la decima parte di tutto per una rotonda già inaugurata due volte.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che ha in mano.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dormiente si è svegliato e ha dato a {} un decimo di tutto, con gli arretrati.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria con la data di domani.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} entra nel mobile, che la iscrive a bilancio e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper restituisce 250 palle e chiede di essere citato nei ringraziamenti finali.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO aggiunge 3 palle e un consiglio non richiesto.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con garanzia di due anni.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e il registro delle presenze.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile di troppo. TILT: metà delle palle viene affidata a un curatore.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro, e il vetro nega di averlo mai lasciato passare.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario che l'ufficio contesterà.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità decade perché il modulo era compilato con inchiostro simpatico.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena sveglia mezzo quartiere.", 0, Boon::boost},
};

/* Numeric part of a target hit. The jackpot doubles first, then the grandfather adds his gift. */
/* Nothing here may run off the end of the number: a jackpot on a huge pile would wrap it negative. */
[[nodiscard]] constexpr std::int64_t flipper_score_after(std::int64_t before, const Mishap &what) {
    constexpr std::int64_t ceiling = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t floor = std::numeric_limits<std::int64_t>::min();
    if (what.boon == Boon::halved) {
        return before / 2;
    }
    std::int64_t after = before;
    if (what.boon == Boon::grandfather) {
        after = before > ceiling / 2 ? ceiling : (before < floor / 2 ? floor : before * 2);
    }
    if (what.palle > 0 && after > ceiling - what.palle) {
        return ceiling;
    }
    if (what.palle < 0 && after < floor - what.palle) {
        return floor;
    }
    return after + what.palle;
}

}

#endif
