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
    Mishap{"🕳️ La buca di {} ha ottenuto un codice fiscale e ha già presentato la prima dichiarazione: -1 palla.", -1, Boon::none},
    Mishap{"🎈 Un palloncino ha chiesto a {} di fargli da garante per un mutuo.", 0, Boon::balloon},
    Mishap{"🛸 Un'astronave in ritardo ha caricato {} per sbaglio e lo ha scaricato in un'altra regione.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto uno sciroppo per la tosse del 1969 e per un turno rende il triplo, in bianco e nero.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha nominato {} nel proprio testamento e poi ha cambiato idea.", 0, Boon::disliked},
    Mishap{"👵 {} ha accompagnato una signora a fare gli esami e ha aspettato fuori tre ore.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha affidato a {} la custodia dell'angolo retto.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato la lapide dei caduti: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle si sono nascoste nel kebab di {} e hanno chiesto asilo politico.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato dentro il forno di {} durante la cottura del pane.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è decaduto perché il vigile era in realtà un cartonato.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha iscritto il calzino alla camera di commercio: -2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è uscito un confessionale.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio dentro la bolletta del gas, alla voce oneri di sistema.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha revocato a {} l'accesso alla piazza principale.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in un ufficio e ne è uscito nello stesso ufficio di un'altra provincia.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette la pubblicità di un negozio chiuso da vent'anni.", 0, Boon::disliked},
    Mishap{"🧾 {} ha esibito uno scontrino di trent'anni fa e la cassa ha pagato una palla senza fiatare.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio è tornato ufficialmente agli anni Novanta.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino con una diffida intestata a {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} ha consegnato alle autorità tutto quello che custodiva: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e gli hanno addebitato due palle di consumo estintore.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto di essere sentito in separata sede.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso a un piano registrato come vano scala.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e un consiglio sul matrimonio.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da quindici minuti e ora dispone di un badge.", -1, Boon::none},
    Mishap{"📠 Il fax del 1998 ha convocato {} per una riunione già avvenuta.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha aperto un procedimento interno.", 0, Boon::none},
    Mishap{"🪤 {} è finito in una trappola per talpe di un ente mai costituito: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha conferito a {} una delega piena sul cortile.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita nel preciso momento in cui serviva a {}, e la scorta pure.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha presentato contro {} un ricorso al condominio.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto dal pino e i pompieri gli hanno offerto il caffè in caserma.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha preso una corsia in contromano e ha travolto le offerte: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar ha convocato un minuto di raccoglimento.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito nel microfono mentre leggevano il verbale dell'assemblea.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nel taschino di un cappotto ancora in giacenza.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e il muro ne ha aggiunti altri per sicurezza.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} e si è concessa il bis da sola.", 0, Boon::liked},
    Mishap{"🌌 Il varco nella dispensa di {} porta a una dispensa identica, dove i legumi sono già in scadenza.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, con qualche scintilla.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a stomaco vuoto davanti a una delegazione in visita.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha dato a {} due palle e si è scusata sul display, in due lingue.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato tutte le piante del palazzo e ha lasciato un promemoria a ciascuna.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha portato {} dall'altra parte della mappa, con la cuffia in testa.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala a cui mancava un gradino per motivi di appalto: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e le chiavi di casa sua.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} e ha sigillato i resti.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato per protesta contro il regolamento condominiale.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato congelato in attesa di parere legale.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato mentre lo sportello chiudeva per pausa.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a un ente morale di dubbia moralità.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un signore che ammirava una vetrina.", 0, Boon::steal},
    Mishap{"🔄 L'anagrafe ha deciso che {} e un altro giocatore sono la stessa persona, con due domicili.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e ha dato fuoco al fascicolo in cortile.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché l'ordine era scritto sul retro di una ricevuta.", 0, Boon::freed},
    Mishap{"🎯 Il nome di {} è stato messo in un registro nero, timbrato, rilegato e plastificato.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila gli ha dedicato una targa.", 0, Boon::liked},
    Mishap{"🧨 {} ha mandato undici vocali per annunciare che sarebbe stato breve.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è diventata gelata nel momento esatto del balsamo.", -1, Boon::none},
    Mishap{"🤝 {} ha riportato un portafoglio pieno in un'altra provincia, pagandosi il viaggio: -2 palle.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre ogni limite e il limite si è trasferito all'estero.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} torna indietro di un minuto al giorno e ha già recuperato due settimane.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato un compleanno che il festeggiato aveva fatto radiare dagli atti: -2 palle.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore ha rovesciato su {} una tanica di sapone durante l'ispezione ministeriale.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme con le note, la bibliografia e un indice analitico.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il locale ha prorogato l'orario.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da spento, da un'altra stanza e ora anche in sogno: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta già iscritta nel bilancio consuntivo come perdita.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e si è avvalso della facoltà di non rispondere: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui, oliato la catena e regolato i freni.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno quindici ore e il pane è stato messo sotto vincolo.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e ha lasciato il pacco al bar: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate si sono attaccate a {} durante il pranzo di comunione.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un giro e si è ricoricata.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto di essere iscritto all'anagrafe.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e la fattura dei sette anni è arrivata con la mora: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e la sala si è alzata in piedi.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto di nascosto, ora esposta in quattro bacheche: -3 palle.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito per principio.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha sollevato {} sopra il campanile e lo ha posato a due quartieri di distanza.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana al contrario ed è stato adottato dal capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto il marchio registrato.", 0, Boon::liked},
    Mishap{"🐝 Un'ape ha aperto un ufficio di rappresentanza sulla testa di {}: -1 palla di spese.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per saltare la fila e lo ha raccontato a sei persone.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto sullo spigolo: due palle e un sopralluogo tecnico.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha rispettato il piano di volo e la raccolta differenziata.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia chiedendo che non venisse messo a verbale.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito di cinquanta palle un tale che studiava la mappa della metro.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto un avviso di chiusura definitiva.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato per un ricorso firmato da un ufficio che non esiste.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle e un portachiavi.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha segnato sulla mappa il punto dove sta {} e ha aggiunto due frecce.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato cinque volte la stessa storia alla fermata e ha chiesto una copia: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto scritto a penna stilografica.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è finito su una lista in portineria, che l'ha già distribuita a tutti i piani.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha preso a {} la decima parte di tutto per una rotonda inaugurata quattro volte.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che ha in tasca.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dormiente si è svegliato e ha dato a {} un decimo di tutto, arretrati compresi.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria con la data della settimana prossima.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} finisce nel mobile, che la protocolla, la archivia e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper rende 250 palle e chiede di essere inserito nell'organigramma.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO mette 3 palle e una polizza vita.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con revisione già effettuata.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e un piano di evacuazione.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile di troppo. TILT: metà delle palle finisce in liquidazione coatta.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro, e il vetro chiede il risarcimento del disturbo.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario respinte dal controllo di gestione.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità decade perché il modulo era in tre copie e nessuna leggibile.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena viene allegata agli atti.", 0, Boon::boost},
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
