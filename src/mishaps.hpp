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
    Mishap{"🕳️ Il Catasto ha registrato la buca di {} come vano accessorio e gli ha chiesto una palla di imposta.", -1, Boon::none},
    Mishap{"🎈 Un palloncino si è presentato a {} con delega firmata e documento scaduto.", 0, Boon::balloon},
    Mishap{"🛸 Un disco volante ha prelevato {} per un sondaggio e lo ha rimesso giù nel posto sbagliato.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto una bibita ritirata dal mercato nel 2007: per un turno rende il triplo.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha depositato contro {} un esposto in triplice copia.", 0, Boon::disliked},
    Mishap{"👵 {} ha riportato a casa una signora che aveva perso l'autobus e lei lo racconta a tutti.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha conferito a {} la delega al lato invisibile.", 0, Boon::liked},
    Mishap{"🎯 {} ha fatto il 360 noscope e ha centrato la vetrina della farmacia: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle si sono nascoste nel doppio fondo del panino di {}.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato l'inno nazionale sotto le finestre di {} alle 4:07.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è stato annullato perché il vigile aveva sbagliato secolo.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha trattenuto due palle come cauzione e non rilascia ricevuta.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è uscito un mobile che nessuno riconosce.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio in mezzo ai sanpietrini e lo ha dato a un bambino.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha convocato {} per un chiarimento sul mangime.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in una porta girevole di un ufficio comunale ed è uscito in provincia di Enna.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette solo una sigla di un programma mai andato in onda.", 0, Boon::disliked},
    Mishap{"🧾 {} ha trovato uno scontrino con sopra una palla mai riscossa dal 1994.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha votato una mozione di sfiducia.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è uscito un palloncino che dichiara di essere intestato a {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} si è aperto da solo e ha restituito tutto quello che nascondeva: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e la commissione lo ha ringraziato per iscritto, con due palle di multa.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} risponde solo alle domande poste da terzi.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso in un piano che il Comune dichiara inesistente.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e un consiglio non richiesto.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da undici minuti e compila un registro.", -1, Boon::none},
    Mishap{"📠 Un fax da un ufficio chiuso nel 1998 ha convocato {} per domani.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il barista ha sospeso il servizio.", 0, Boon::none},
    Mishap{"🪤 {} è inciampato in una trappola per talpe installata dal Ministero per errore.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha nominato {} portavoce del quartiere senza consultarlo.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita nel momento esatto in cui serviva a {}.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha ritirato la fiducia a {} con un comunicato.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto da un pino e i pompieri hanno applaudito dal furgone.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha imboccato una corsia che non era in programma: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar intero ha fatto silenzio.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito dentro il microfono durante il minuto di raccoglimento.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nella fodera di un cappotto sequestrato.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e il numero cambia a ogni conteggio.", 0, Boon::none},
    Mishap{"🎺 La banda del paese è passata sotto casa di {} e ha dedicato a lui il pezzo finale.", 0, Boon::liked},
    Mishap{"🌌 Un varco si è aperto nella dispensa di {} e dall'altra parte c'erano solo legumi.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno pensa il triplo più in fretta.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a digiuno davanti a una commissione sanitaria.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha restituito a {} due palle e un sacchetto vuoto.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato le piante del pianerottolo di tutto il palazzo, comprese le finte.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala in vasca ha spedito {} dall'altra parte della mappa.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala a cui mancava un gradino per motivi di bilancio: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con dentro due palle e un foglio bianco.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} durante un controllo a campione.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato per solidarietà con un altro palloncino.", 0, Boon::pop},
    Mishap{"🧊 L'ufficio competente ha congelato il moltiplicatore di {} in attesa di chiarimenti.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato mentre era in fila per essere usato.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a un ente di cui non ricorda il nome.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un passante distratto.", 0, Boon::steal},
    Mishap{"🔄 Per un errore dell'anagrafe {} e un altro giocatore hanno scambiato le identità.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} senza sapere di cosa fosse accusato.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché il modulo di detenzione era scritto a matita.", 0, Boon::freed},
    Mishap{"🎯 Qualcuno ha scritto il nome di {} su un taccuino nero e lo ha sottolineato due volte.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila lo ha portato in trionfo.", 0, Boon::liked},
    Mishap{"🧨 {} ha risposto con un vocale a un messaggio scritto e il gruppo ha perso ogni stima.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è passata da bollente a gelata durante l'ammollo dello shampoo.", -1, Boon::none},
    Mishap{"🤝 {} ha restituito un portafoglio trovato e ha rifiutato la ricompensa, con due palle di spesa.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite finché non si è spanata e poi ha stretto ancora.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} va indietro di un minuto al giorno dal giorno che è stato comprato.", -1, Boon::none},
    Mishap{"🎂 {} si è ricordato di un compleanno che aveva dimenticato anche il festeggiato.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore del sapone ha sparato addosso a {} una dose da cantiere: -2 palle.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme riga per riga e il meme non si è più ripreso.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo giusto con due penne e il tavolo lo ha seguito.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia anche da spento, e ora anche da scollegato: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta che il condominio aveva già pianto.", 1, Boon::none},
    Mishap{"🧽 {} ha pulito la cucina alle tre di notte e non vuole parlarne: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici non sua e l'ha lasciata meglio di come l'ha trovata.", 0, Boon::liked},
    Mishap{"🍞 {} ha dimenticato il pane nel forno per undici ore e il pane ha resistito.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e non ci crede nessuno: -3 palle di spedizione.", -3, Boon::none},
    Mishap{"🧲 Tutte le forchette del cassetto si sono attaccate a {} durante una cena formale.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra è ripartita.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e ha ricevuto la fattura dei sette anni in formato elettronico.", 0, Boon::none},
    Mishap{"📸 {} è venuto benissimo in una foto che qualcuno ha scattato di nascosto: -3 palle di diritti.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e due palle sono finite sul tavolo.", 2, Boon::liked},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e ha continuato per orgoglio: -3 palle.", -3, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha portato {} sopra il campanile e lo ha posato altrove.", 1, Boon::none},
    Mishap{"🚇 {} ha preso la metropolitana nel verso sbagliato e se ne è accorto in un'altra città.", 0, Boon::teleport},
    Mishap{"🎈 Il Ministero del Made in Italy ha requisito il palloncino di {} per motivi di rappresentanza.", 0, Boon::teleport},
    Mishap{"🐝 Un'ape ha eletto la testa di {} come punto di osservazione strategico: -1 palla.", 0, Boon::liked},
    Mishap{"💶 {} ha pagato il caffè il doppio pur di non fare la fila e se n'è vantato in pubblico.", -1, Boon::disliked},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto in piedi sullo spigolo: due palle di premio tecnico.", 2, Boon::none},
    Mishap{"🧺 {} ha trovato due palle nel cesto della biancheria di casa sua e non sa spiegarselo.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che, contro ogni previsione, ha funzionato.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha chiesto di non dirlo a nessuno.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito un passante di cinquanta palle passando dalle scale antincendio.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto la notizia di un lutto.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato a seguito di un ricorso che non aveva mai presentato.", 0, Boon::restored},
    Mishap{"🫶 {} ha presentato due persone che ora si vogliono bene e lo ricordano a ogni occasione.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha piantato una puntina sulla mappa esattamente dove si trova {}.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato fino in fondo la storia di un signore alla fermata, due volte: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha dato cinquanta palle a chi diceva di averne bisogno, senza fare domande.", 0, Boon::donate},
    Mishap{"🧨 {} ha acceso una miccia bagnata davanti a gente che prende nota.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha prelevato a {} la decima parte di tutto per finanziare una sagra.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di tutto quello che possiede.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dimenticato ha fruttato a {} un decimo di quello che aveva già.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria che non ha mai comprato.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} finisce nel mobile, che emette una ricevuta fiscale e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper restituisce 250 palle e pretende che venga citato nei ringraziamenti.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO firma un assegno da 3 palle.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con regolare bolla di accompagnamento.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e il verbale dell'assemblea.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile una volta di troppo. TILT: metà delle palle passa in amministrazione controllata.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro e il vetro si richiude fingendo di non averlo mai conosciuto.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario non autorizzato.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità torna indietro perché il modulo era compilato in stampatello minuscolo.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e parte una sirena da nave da crociera.", 0, Boon::boost},
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
