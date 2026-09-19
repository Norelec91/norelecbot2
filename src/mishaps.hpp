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
    Mishap{"🕳️ La buca di {} ha assunto un custode e gli paga lo stipendio con i pedaggi: -1 palla.", -1, Boon::none},
    Mishap{"🎈 Un palloncino ha chiesto a {} di comparire come testimone nella causa contro un altro palloncino.", 0, Boon::balloon},
    Mishap{"🛸 Un'astronave ha caricato {} per un tirocinio non retribuito e lo ha lasciato in un'altra regione.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto uno sciroppo scaduto quando lui non era ancora nato: per un turno rende il triplo.", 0, Boon::boost},
    Mishap{"🦆 La commissione anatre ha aperto su {} un fascicolo che si autoalimenta.", 0, Boon::disliked},
    Mishap{"👵 {} ha insegnato a una signora a usare il bancomat e le ha lasciato il foglietto con i passaggi.", 0, Boon::liked},
    Mishap{"🔺 Il triangolo col monocolo ha nominato {} garante del lato che nessuno ha mai misurato.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato la bacheca degli avvisi: -3 palle.", -3, Boon::none},
    Mishap{"🌮 Due palle si sono barricate dentro la piadina di {} e chiedono un negoziato.", 2, Boon::none},
    Mishap{"📢 Un airhorn ha suonato dentro il contatore del gas di {} per l'intera vigilia.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è decaduto perché il modulo era stato annullato da un altro modulo.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha messo il calzino in cassa integrazione: -2 palle di contributi.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e ne è venuta fuori una sala d'attesa.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio piegato dentro un modulo di reclamo mai spedito.", 1, Boon::none},
    Mishap{"🐦 Il consiglio dei piccioni ha sospeso a {} il diritto di passaggio sulla piazza.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in un ufficio ed è uscito nell'ufficio che controlla quell'ufficio, in un'altra regione.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} manda la replica di un programma che non è mai stato registrato.", 0, Boon::disliked},
    Mishap{"🧾 {} ha portato uno scontrino di trent'anni fa e la cassa gli ha dato una palla e un attestato.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha perso l'archivio delle assemblee.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino con un mandato di comparizione per {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} ha confessato tutto a un altro elettrodomestico: -3 palle.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un incendio e la pratica di rimborso gli è costata due palle di marca da bollo.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha chiesto di essere rappresentato da un altro specchio.", 0, Boon::none},
    Mishap{"🚪 {} ha preso l'ascensore ed è sceso in un piano che esiste solo nelle planimetrie di riserva.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto e un parere sull'ordinamento giudiziario.", 2, Boon::none},
    Mishap{"🪰 L'ispettorato mosche ha assegnato a {} una mosca a tempo pieno.", -1, Boon::none},
    Mishap{"📠 Il fax del 1998 ha chiesto a {} di confermare per iscritto di aver ricevuto il fax.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il bar ha aperto un procedimento contro sé stesso.", 0, Boon::none},
    Mishap{"🪤 {} è finito in una trappola per talpe collaudata da un ente che collauda trappole: -2 palle.", -2, Boon::none},
    Mishap{"🐕 Il sindacato dei cani randagi ha eletto {} delegato di cortile.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita, e il modulo per segnalarlo era nell'ultimo rotolo di {}.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino, per interposta persona, ha diffidato {} dal passare sul pianerottolo.", 0, Boon::disliked},
    Mishap{"🚒 {} ha tirato giù un gatto dal pino e i pompieri lo hanno inserito nel piano ferie.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha investito l'espositore dei moduli: -2 palle di ricostruzione.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego invece di grazie e il bar ha chiesto una relazione scritta.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito nel microfono durante l'approvazione del bilancio.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una palla dentro un fascicolo archiviato per decorrenza dei termini.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni e il muro ha depositato un controconteggio.", 0, Boon::none},
    Mishap{"🎺 La banda del paese ha suonato sotto casa di {} e ha chiesto il patrocinio gratuito.", 0, Boon::liked},
    Mishap{"🌌 Il varco nella dispensa di {} porta a una dispensa che controlla le altre dispense.", 0, Boon::teleport},
    Mishap{"⚡ {} ha preso la scossa dalla maniglia e per un turno rende il triplo, con il ronzio dell'ufficio.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a digiuno davanti alla commissione che vigila sulle commissioni.", 0, Boon::disliked},
    Mishap{"🍫 La macchinetta ha dato a {} due palle e un modulo di soddisfazione da compilare.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato le piante del palazzo e ha protocollato ogni innaffiatura.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala nella vasca ha portato {} dall'altra parte della mappa con tutto il registro.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala a cui mancava un gradino per una perizia mai consegnata: -3 palle.", -3, Boon::none},
    Mishap{"🎁 A {} è arrivato un pacco senza mittente con due palle e il modulo per restituirle.", 2, Boon::none},
    Mishap{"🎈 La Guardia di Finanza ha bucato il palloncino di {} e ha allegato il palloncino al verbale.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato per adesione a una vertenza di categoria.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato congelato dall'ufficio che congela i moltiplicatori.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato mentre l'ufficio cercava il modulo per ricaricarlo.", 0, Boon::flat},
    Mishap{"🎁 {} ha versato cinquanta palle a un ente che vigila sugli enti che vigilano.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un tale che leggeva un avviso pubblico.", 0, Boon::steal},
    Mishap{"🔄 L'anagrafe ha deciso che {} e un altro giocatore sono la stessa pratica in due copie.", 0, Boon::swap},
    Mishap{"♻️ Una commissione ha riabilitato {} e si è sciolta subito dopo, per prudenza.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato perché l'ordine di custodia era stato smarrito dall'ufficio che lo custodiva.", 0, Boon::freed},
    Mishap{"🎯 Il nome di {} è finito in un registro nero che ha un proprio registro nero.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila ha istituito un premio annuale.", 0, Boon::liked},
    Mishap{"🧨 {} ha mandato quindici vocali per dire che preferisce scrivere.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è diventata gelata nel momento previsto dal manuale di manutenzione.", -1, Boon::none},
    Mishap{"🤝 {} ha riportato un portafoglio pieno e ha compilato lui il modulo di riconsegna: -2 palle di bollo.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite oltre il limite e il limite ha fatto ricorso.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} torna indietro di un minuto al giorno e l'ufficio orari lo ha certificato.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato un compleanno che l'interessato aveva fatto cancellare per decreto: -2 palle.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore ha svuotato su {} una tanica di sapone durante la visita ispettiva annuale.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme con le note, la bibliografia e un parere di conformità.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo con due penne e il locale ha chiesto la licenza per il ritmo.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia da spento e adesso ha anche un numero di protocollo: -2 palle.", -2, Boon::liked},
    Mishap{"🪴 {} ha riportato in vita una pianta che risultava già radiata dall'elenco delle piante.", 1, Boon::none},
    Mishap{"🧽 {} ha lavato le scale alle tre di notte e ha depositato il verbale in portineria: -2 palle.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma di una bici altrui e ha lasciato il certificato di gonfiaggio.", 0, Boon::liked},
    Mishap{"🍞 {} ha lasciato il pane nel forno diciassette ore e il pane ha ottenuto un vincolo paesaggistico.", 1, Boon::none},
    Mishap{"📦 Il corriere ha trovato casa di {} al primo colpo e ha consegnato all'ufficio reclami: -3 palle.", -3, Boon::none},
    Mishap{"🧲 Tutte le posate si sono attaccate a {} durante il pranzo dell'ente.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e la giostra ha fatto un giro e ha chiesto il collaudo.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha chiesto l'allaccio alla rete.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e i sette anni sono stati rateizzati in ventotto trimestri: -3 palle.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo dalla parte sbagliata e la platea ha chiesto di metterlo a verbale.", 2, Boon::liked},
    Mishap{"📸 {} è venuto benissimo in una foto affissa nell'albo pretorio: -3 palle di pubblicazione.", -3, Boon::none},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino e lo ha finito per non aprire un contenzioso.", 1, Boon::none},
    Mishap{"🌪️ Un vortice di moduli ha sollevato {} sopra il campanile e lo ha posato in un altro quartiere.", 0, Boon::teleport},
    Mishap{"🚇 {} ha preso la metropolitana al contrario ed è stato assunto dal capolinea.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la ricetta della nonna e la nonna ha chiesto la denominazione di origine.", 0, Boon::liked},
    Mishap{"🐝 Il consorzio delle api ha aperto uno sportello sulla testa di {}: -1 palla di canone.", -1, Boon::disliked},
    Mishap{"💶 {} ha pagato il caffè il doppio per saltare la fila e ha chiesto la ricevuta.", 2, Boon::none},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto sullo spigolo: due palle e una perizia giurata.", 2, Boon::none},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha rispettato il piano di volo e il regolamento interno.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle di mancia e ha chiesto che non finissero in nessun registro.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito di cinquanta palle un tale che leggeva l'albo pretorio.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su sotto un avviso di chiusura per lutto cittadino.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato da un ufficio che è stato istituito apposta e poi chiuso.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto due palle e un modulo.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha segnato sulla mappa dove sta {}, e la mappa è stata protocollata.", 0, Boon::marked},
    Mishap{"🧓 {} ha ascoltato sette volte la stessa storia alla fermata e ha chiesto la versione ufficiale: -2 palle.", -2, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto in carta intestata.", 0, Boon::donate},
    Mishap{"🚩 Il nome di {} è su una lista in portineria, che ora ha un ufficio tutto suo.", 0, Boon::marked},
    Mishap{"💸 Il fisco ha preso a {} la decima parte di tutto per la rotonda che studia le rotonde.", 0, Boon::tithe},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che ha dichiarato.", 0, Boon::windfall},
    Mishap{"💰 Un fondo dormiente si è svegliato e ha dato a {} un decimo di tutto, più le spese di risveglio.", 0, Boon::windfall},
    Mishap{"🎟️ Nella giacca di {} è comparso un biglietto della lotteria con la data di un'estrazione già fatta.", 0, Boon::ticket},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} finisce nel mobile, che la iscrive al proprio registro e trattiene 500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper rende 250 palle e chiede di essere ascoltato dalla commissione.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO deposita 3 palle in un fondo vincolato.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino con il collaudo già scaduto.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio: dal controsoffitto cadono 1000 palle e l'organigramma aggiornato.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile di troppo. TILT: metà delle palle passa all'ufficio che gestisce i TILT.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} oltre il vetro, e il vetro chiede di essere sentito in commissione.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: 100 palle di straordinario e un richiamo scritto.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità decade perché il modulo annullava sé stesso al punto quattro.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e la sirena ha un proprio protocollo.", 0, Boon::boost},
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
