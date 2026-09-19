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
    Mishap{"🕳️ Una voragine ha chiesto la residenza in casa di {} e l'ufficio anagrafe sta valutando.", -1, Boon::none},
    Mishap{"🎈 Un palloncino si è presentato da {} con un contratto di comodato d'uso gratuito.", 0, Boon::balloon},
    Mishap{"🛸 Tre luci in fila hanno prelevato {} per una verifica e lo hanno restituito in un'altra provincia.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto uno sciroppo scaduto nel 2004 e per un turno rende il triplo, con qualche visione.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha aperto contro {} un procedimento amministrativo che andrà avanti per anni.", 0, Boon::disliked},
    Mishap{"👵 {} ha montato il condizionatore a una signora del quarto piano senza chiedere niente.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha nominato {} tesoriere della faccia nascosta.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato la teca delle reliquie: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle sono cadute nel tramezzino di {} e hanno chiesto di restarci.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato dentro il frigorifero di {} per tutta la notte di ferragosto.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è stato annullato perché redatto su carta del pane.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha restituito il calzino ma ha trattenuto due palle di penale.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è uscito un altare.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio dentro un pacchetto di insalata e lo ha piantato.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha revocato a {} il permesso di attraversare la piazza.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in un ufficio postale ed è uscito da un ufficio postale di un'altra regione.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette una sigla che nessuna emittente ammette di avere mai mandato.", 0, Boon::disliked},
    Mishap{"🧾 {} ha trovato uno scontrino con una palla non riscossa e la cassa ha pagato senza discutere.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha commissariato la scala B.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino intestato a {} con tanto di visura.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} ha restituito tutto quello che nascondeva davanti agli ospiti: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e il verbale gli ha addebitato due palle di schiuma.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto di essere sentito come persona informata sui fatti.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso in un piano che il catasto dichiara demolito nel 1971.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e una teoria sul futuro del paese.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da dodici minuti e ogni tanto si ferma a prendere appunti.", -1, Boon::none},
    Mishap{"📠 A casa di {} è arrivato un fax dal 1998 che chiede conferma di una convocazione.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha convocato un consiglio straordinario.", 0, Boon::none},
    Mishap{"🪤 {} è inciampato in una trappola per talpe installata da un ente che non esiste più: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha eletto {} suo rappresentante legale.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita e la scorta era finita dal mese scorso, cosa che {} scopre adesso.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha aperto una pratica di sfratto contro {}.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto da un pino e i pompieri gli hanno stretto la mano dal furgone.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha imboccato una corsia vietata e ha sfondato una piramide di pelati: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar ha smesso di servire per un minuto.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito dentro il microfono di un funerale trasmesso in diretta.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nel taschino di una giacca requisita.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e ogni volta il muro aggiunge una fila.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} il pezzo che gli piace di più.", 0, Boon::liked},
    Mishap{"🌌 Un varco nella dispensa di {} porta in una dispensa identica, con gli stessi legumi.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, con i capelli dritti.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a stomaco vuoto davanti a una delegazione straniera.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha restituito a {} due palle e si è scusata per iscritto.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato le piante di tutto il palazzo, comprese quelle di plastica, per non offenderle.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha spedito {} dall'altra parte della mappa.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala a cui mancava un gradino per motivi di bilancio comunale: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e un foglio scritto al contrario.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} e ne ha verbalizzato il rumore.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato durante l'assemblea, che si è interrotta.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato posto sotto sequestro cautelativo.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato mentre aspettava il proprio numero allo sportello.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a un comitato di cui non ha capito lo scopo.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un signore che guardava in alto.", 0, Boon::steal},
    Mishap{"🔄 Per un disguido dell'anagrafe {} e un altro giocatore risultano la stessa persona.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e gli ha chiesto scusa in latino.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché il modulo di detenzione era scritto con la penna cancellabile.", 0, Boon::freed},
    Mishap{"🎯 Qualcuno ha scritto il nome di {} su un registro nero e lo ha timbrato due volte.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila ha proposto di intitolargli una via.", 0, Boon::liked},
    Mishap{"🧨 {} ha risposto con tre vocali di fila a un messaggio di due parole.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è diventata gelata proprio mentre aveva il sapone negli occhi.", -1, Boon::none},
    Mishap{"🤝 {} ha restituito un portafoglio pieno e ha rifiutato la ricompensa, pagandosi il taxi: -2 palle.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre il punto di non ritorno e poi ha stretto ancora.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} va indietro di un minuto al giorno e nessun orologiaio vuole toccarlo.", -1, Boon::none},
    Mishap{"🎂 {} si è ricordato di un compleanno che il festeggiato aveva rimosso: -2 palle di torta.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore del sapone ha sparato su {} una dose industriale davanti al direttore.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme in nota a piè di pagina.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il tavolo intero lo ha seguito fino alla fine.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da scollegato e adesso anche da un'altra stanza: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta che l'amministratore aveva già dichiarato perduta.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e non intende spiegare perché: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui e le ha anche oliato la catena.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno dodici ore e il pane è diventato un reperto.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo, ma ha suonato dal vicino: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate del cassetto si sono attaccate a {} durante una cena di lavoro.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un giro solo, per lui.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto di essere lasciato lì.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e gli è arrivata la fattura dei sette anni in tre rate: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e il tavolo ha applaudito lo stesso.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto scattata di nascosto e ora la foto è in comune: -3 palle.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito per orgoglio.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha sollevato {} sopra il campanile e lo ha posato altrove.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana nel verso sbagliato e ha fatto amicizia con il capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto la ricetta a lui.", 0, Boon::liked},
    Mishap{"🐝 Un'ape ha stabilito la propria sede legale sulla testa di {}: -1 palla.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per non fare la fila e lo ha raccontato a tre persone.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto in piedi sullo spigolo: due palle di premio tecnico.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che, incredibilmente, ha rispettato il piano di volo.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha chiesto di non farne parola.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito un passante di cinquanta palle mentre quello leggeva l'orario dei bus.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto il necrologio di un vicino.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato in seguito a un ricorso presentato da uno sconosciuto.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle nette.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha piantato una puntina sulla mappa esattamente dove si trova {}, e poi due.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato due volte la stessa storia alla fermata e ha chiesto il finale: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto scritto in bella grafia.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è finito su una lista plastificata e appesa in portineria.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha prelevato a {} la decima parte di tutto per una rotonda già inaugurata.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che possiede.", 0, Boon::windfall},
    Mishap{"💰 Una polizza dimenticata ha versato a {} un decimo di tutto, con gli arretrati.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria che non ricorda di aver comprato.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} sparisce nel mobile, che apre un fascicolo e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper restituisce 250 palle e chiede di essere nominato nel testamento.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO aggiunge 3 palle e una raccomandazione.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con tanto di certificato di collaudo.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e una delibera del 1987.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile una volta di troppo. TILT: metà delle palle viene messa all'asta giudiziaria.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro e il vetro dichiara di non aver visto niente.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario mai autorizzato dall'ufficio.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità torna indietro perché il modulo era firmato da un omonimo.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena sveglia tre condomini.", 0, Boon::boost},
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
