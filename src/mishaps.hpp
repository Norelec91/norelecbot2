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
    Mishap{"🕳️ La buca di {} ha aperto una partita IVA e fattura il passaggio: -1 palla.", -1, Boon::none},
    Mishap{"🎈 Un palloncino si è presentato a {} con un contratto già firmato da entrambe le parti.", 0, Boon::balloon},
    Mishap{"🛸 Un'astronave ha caricato {} durante un cambio turno e lo ha posato in un'altra regione.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto un tonico del 1966 e per un turno rende il triplo, con la voce di un altro.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha aperto su {} un'istruttoria che durerà più di lei.", 0, Boon::disliked},
    Mishap{"👵 {} ha fatto la fila alle poste al posto di una signora e non ha voluto niente.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha dato a {} la firma sul lato che non esiste.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato l'urna delle offerte: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle si sono messe al riparo dentro il panino di {} e hanno chiesto una branda.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato nella cappa della cucina di {} per tutta la notte di Natale.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è decaduto perché firmato da un vigile che risulta un personaggio di fantasia.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha intestato al calzino un conto corrente: -2 palle di spese.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è uscito un piccolo santuario.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio dentro il manuale dell'aspirapolvere, a pagina ventidue.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha tolto a {} il diritto di guardare in alto.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in un ufficio ed è uscito in un ufficio identico, in un'altra regione.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette gli auguri di un'emittente che ha chiuso nel 2004.", 0, Boon::disliked},
    Mishap{"🧾 {} ha esibito uno scontrino del secolo scorso e la cassa ha pagato una palla senza guardarlo.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha perso tre canali e una certezza.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino con un atto di citazione per {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} ha vuotato il sacco davanti a tutta la famiglia: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e gli hanno chiesto due palle per il ripristino dell'estintore.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto di essere ascoltato prima degli altri testimoni.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso in un piano accatastato come deposito attrezzi.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e un'opinione sulla politica estera.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da sedici minuti e ora ha una scrivania.", -1, Boon::none},
    Mishap{"📠 Il fax del 1998 ha chiesto a {} di confermare che la conferma era confermata.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha nominato una commissione d'inchiesta.", 0, Boon::none},
    Mishap{"🪤 {} è finito in una trappola per talpe di un consorzio in liquidazione dal 1976: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha dato a {} la delega a trattare con gli altri cani.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita, e finita anche la scorta che {} giurava di avere.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha chiesto contro {} un provvedimento d'urgenza.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto dal pino e i pompieri lo hanno messo in organico.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha girato da solo e ha steso la piramide dei pelati: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar ha sospeso il servizio per cinque minuti.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito nel microfono durante la proclamazione dei risultati.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nel taschino di un cappotto ancora sotto inventario giudiziario.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e il muro ha risposto aggiungendone due file.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} e ha ripetuto il pezzo per sicurezza.", 0, Boon::liked},
    Mishap{"🌌 Il varco nella dispensa di {} porta in una dispensa uguale, con i legumi già inventariati.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, con un leggero ronzio.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a stomaco vuoto durante un'audizione parlamentare.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha dato a {} due palle e ha stampato una lettera di scuse.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato tutte le piante del palazzo e ha aggiornato il registro delle innaffiature.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha portato {} dall'altra parte della mappa con tutto il tappetino.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala a cui mancava un gradino per una variante in corso d'opera: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e la sua stessa firma sopra.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} e ne ha misurato il rumore.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato in adesione a uno sciopero generale dei palloncini.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato congelato in attesa di un parere che arriverà nel 2031.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato mentre l'impiegato cercava il timbro giusto.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a una onlus che si occupa di onlus.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un tale che contava i piccioni.", 0, Boon::steal},
    Mishap{"🔄 L'anagrafe ha stabilito che {} e un altro giocatore condividono la stessa identità dal 1979.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e si è scusata a nome di un'altra commissione.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché l'ordine era scritto sul margine di un quotidiano.", 0, Boon::freed},
    Mishap{"🎯 Il nome di {} è stato messo in un registro nero, timbrato, rilegato e fotocopiato.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila ha proposto di intitolargli l'ufficio.", 0, Boon::liked},
    Mishap{"🧨 {} ha mandato tredici vocali per dire che avrebbe chiamato lui.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è diventata gelata nel momento preciso in cui chiudeva gli occhi.", -1, Boon::none},
    Mishap{"🤝 {} ha riportato un portafoglio pieno in un'altra regione pagandosi il biglietto: -2 palle.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre il limite e il limite ha cambiato residenza.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} torna indietro di un minuto al giorno e ha già recuperato un mese.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato a tutti un compleanno che il festeggiato aveva fatto togliere dai registri: -2 palle.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore ha svuotato su {} una tanica di sapone davanti all'ispettore ministeriale.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme con le note, la bibliografia, l'indice e una prefazione.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il locale ha chiesto il permesso per la musica dal vivo.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da spento, da un'altra stanza e adesso anche al telefono: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta che risultava già smaltita come rifiuto verde.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e ha fatto scena muta con l'amministratore: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui, oliato la catena e lasciato il fondello pulito.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno sedici ore e il pane è stato dichiarato di interesse storico.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e ha consegnato al piano interrato: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate si sono attaccate a {} durante il pranzo dell'anniversario.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un giro e ha chiesto scusa.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto di essere messo in regola.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e la fattura dei sette anni è arrivata con l'addebito diretto: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e la sala ha chiesto il bis.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto scattata di nascosto e affissa in cinque bacheche: -3 palle.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito guardando negli occhi tutti.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha portato {} sopra il campanile e lo ha posato tre quartieri più in là.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana al contrario ed è stato invitato a cena dal capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto di depositarla in tribunale.", 0, Boon::liked},
    Mishap{"🐝 Un'ape ha aperto una sede operativa sulla testa di {}: -1 palla di utenze.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per saltare la fila e lo ha raccontato a sette persone.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto sullo spigolo: due palle e un verbale di sopralluogo.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha rispettato il piano di volo e il codice della strada.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha chiesto che non risultasse agli atti.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito di cinquanta palle un tale che osservava i lavori in corso.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto un annuncio di chiusura per lutto.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato grazie a un ricorso firmato da un ufficio soppresso nel 1998.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle e un adesivo.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha segnato sulla mappa dove sta {} e ha plastificato la mappa.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato sei volte la stessa storia alla fermata e ha chiesto il seguito: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto scritto in latino.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è su una lista in portineria, e la portineria ne ha fatto una versione tascabile.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha preso a {} la decima parte di tutto per una rotonda inaugurata cinque volte.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che tiene da parte.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dormiente si è svegliato e ha versato a {} un decimo di tutto, con gli interessi di mora.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria con la data del mese prossimo.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} sparisce nel mobile, che la mette a ruolo e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper rende 250 palle e chiede di risultare fra i soci fondatori.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO versa 3 palle e apre un fondo.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con tanto di dichiarazione di conformità.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e un piano triennale.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile di troppo. TILT: metà delle palle viene messa sotto tutela.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro, e il vetro apre un procedimento disciplinare.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario che nessuno ha autorizzato.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità decade perché il modulo era stato compilato al contrario.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena entra nel verbale.", 0, Boon::boost},
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
