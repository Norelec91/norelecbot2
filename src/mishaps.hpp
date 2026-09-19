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
    Mishap{"🕳️ La buca di {} è stata dichiarata bene culturale e adesso lui paga il biglietto: -1 palla.", -1, Boon::none},
    Mishap{"🎈 Un palloncino si è costituito parte civile nel procedimento che riguarda {}.", 0, Boon::balloon},
    Mishap{"🛸 Un'astronave ha caricato {} per un controllo qualità e lo ha riconsegnato in un'altra provincia.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto un tonico del 1971 e per un turno rende il triplo, ricordando cose mai vissute.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha chiesto e ottenuto il domicilio digitale di {}.", 0, Boon::disliked},
    Mishap{"👵 {} ha fatto la spesa per tre signore del piano di sotto e non ha voluto lo scontrino indietro.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha conferito a {} la presidenza del lato che non si vede.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato il gonfalone del comune: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle si sono rifugiate nel panino di {} e chiedono protezione internazionale.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato dentro la lavastoviglie di {} per tutta la vigilia.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è decaduto perché la strada risulta demolita nel 1963.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha aperto una posizione IVA a nome del calzino: -2 palle.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è venuta fuori una cappella con due panche.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio dentro la garanzia della lavatrice, valida fino al 2041.", 1, Boon::none},
    Mishap{"🐦 Il piccione capo ha sospeso a {} il diritto di sosta sotto i portici.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in un ufficio e ne è uscito nello stesso ufficio, ma di un'altra città.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette il meteo di una regione che non risulta più.", 0, Boon::disliked},
    Mishap{"🧾 {} ha esibito uno scontrino del 1991 e la cassa gli ha dato una palla senza fare domande.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha perso la connessione con il resto del secolo.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino con una raccomandata con ricevuta di ritorno per {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} ha restituito tutto quello che nascondeva davanti a due testimoni: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e la commissione gli ha addebitato due palle di sopralluogo.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto il trasferimento per incompatibilità ambientale.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso in un piano che risulta accatastato come sottotetto.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e una previsione sul raccolto.", 2, Boon::none},
    Mishap{"🪰 Una mosca segue {} da quattordici minuti e ora ha anche un cartellino con la foto.", -1, Boon::none},
    Mishap{"📠 Il fax del 1998 chiede a {} conferma della conferma della conferma.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha indetto un'assemblea straordinaria.", 0, Boon::none},
    Mishap{"🪤 {} è finito in una trappola per talpe di un consorzio sciolto nel 1976: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha nominato {} suo procuratore per tutto il quartiere.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita, e con essa la scorta, la riserva e l'ultimo rotolo di {}.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha presentato contro {} un'istanza di allontanamento urgente.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto dal pino e i pompieri lo hanno inserito nel turno di notte.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha svoltato da solo e ha buttato giù la piramide delle offerte: -2 palle.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e nel bar è calato un silenzio istituzionale.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito nel microfono durante la lettura dei numeri della tombola.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla nel taschino di un cappotto ancora sotto inventario.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro e il muro ha risposto con un'altra fila.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} e si è richiamata da sola per il bis.", 0, Boon::liked},
    Mishap{"🌌 Il varco nella dispensa di {} porta in una dispensa uguale, dove i legumi sono già stati contati.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, con un ronzio di fondo.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a digiuno davanti a una commissione di inchiesta.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha restituito a {} due palle e un messaggio di scuse a caratteri scorrevoli.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato tutte le piante del palazzo e ha compilato un registro per ciascuna.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha portato {} dall'altra parte della mappa con tutto l'asciugamano.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala senza un gradino, mancante per motivi di bilancio: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e la piantina di casa sua.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} e ha verbalizzato anche il fischio.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato in solidarietà con un palloncino di un altro condominio.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato messo sotto sequestro probatorio a tempo indeterminato.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato allo sportello, un numero prima del suo.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a un consorzio che si occupa di consorzi.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un tale che controllava l'orologio della stazione.", 0, Boon::steal},
    Mishap{"🔄 L'anagrafe ha stabilito che {} e un altro giocatore coincidono dal giorno della nascita.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e ha chiesto scusa in tre lingue.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché l'ordine era stato scritto su un post-it.", 0, Boon::freed},
    Mishap{"🎯 Il nome di {} è stato trascritto in un registro nero, timbrato e rilegato.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila ha votato per fargli un monumento.", 0, Boon::liked},
    Mishap{"🧨 {} ha mandato nove vocali per dire che stava per chiamare.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è passata a gelata nell'istante preciso in cui si insaponava la testa.", -1, Boon::none},
    Mishap{"🤝 {} ha riportato un portafoglio pieno in un'altra città e ha pagato lui il treno: -2 palle.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre il limite e il limite ha presentato le dimissioni.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} torna indietro di un minuto al giorno e ha già recuperato un'intera settimana.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato a tutti un compleanno che il festeggiato aveva fatto cancellare dai registri: -2 palle.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore ha svuotato su {} una tanica di sapone durante la visita del direttore generale.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme con le note, la bibliografia e un breve apparato critico.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il locale ha rimandato la chiusura.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da spento, da un'altra stanza e adesso anche dal piano di sopra: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta che l'amministratore aveva già messo a bilancio come perdita.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e ha rifiutato ogni forma di spiegazione: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui, oliato la catena e lasciato un biglietto di auguri.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno quattordici ore e il pane è stato catalogato.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e ha consegnato al civico accanto: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate del cassetto si sono attaccate a {} durante il pranzo della cresima.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un giro e si è rimessa a dormire.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto di essere censito.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e ha ricevuto la fattura dei sette anni con interessi di mora: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e la platea ha applaudito comunque.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto scattata di nascosto e affissa in tre bacheche: -3 palle.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito per non dare soddisfazione.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha portato {} sopra il campanile e lo ha lasciato in un altro quartiere.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana al contrario e ha conosciuto tutto il personale del capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto di poterla depositare a suo nome.", 0, Boon::liked},
    Mishap{"🐝 Un'ape ha aperto una sede secondaria sulla testa di {}: -1 palla di canone.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per non fare la fila e lo ha raccontato a cinque persone.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto sullo spigolo: due palle e un verbale di constatazione.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha rispettato il piano di volo e il regolamento.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha chiesto che non risultasse da nessuna parte.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito di cinquanta palle un tale che stava leggendo l'orario dei traghetti.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto il manifesto di un funerale.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato grazie a un ricorso firmato da una persona mai esistita.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle e una matita.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha piantato due puntine sulla mappa dove sta {}, e ci ha girato intorno col pennarello.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato quattro volte la stessa storia alla fermata e ha preso appunti: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto in carta filigranata.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è finito su una lista in portineria, e la portineria l'ha fotocopiata.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha preso a {} la decima parte di tutto per una rotonda inaugurata tre volte.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che tiene in mano.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dormiente si è svegliato e ha versato a {} un decimo di tutto, con gli arretrati.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria con la data di dopodomani.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} sparisce nel mobile, che apre un fascicolo, lo protocolla e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper restituisce 250 palle e chiede di comparire nei titoli di coda.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO firma 3 palle e una fideiussione.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con libretto di circolazione.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e un piano regolatore.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile di troppo. TILT: metà delle palle passa in amministrazione straordinaria.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro, e il vetro presenta un esposto per uso improprio.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario già contestate dall'ufficio.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità decade perché il modulo era compilato da un omonimo del 1954.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena viene messa a verbale.", 0, Boon::boost},
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
