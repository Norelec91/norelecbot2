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
    Mishap{"🧑‍🍳 {} ha messo la panna nella carbonara. Il gruppo ha visto.", 0, Boon::disliked},
    Mishap{"🍍 {} ha ordinato la pizza con l'ananas e l'ha difesa in pubblico.", 0, Boon::disliked},
    Mishap{"🥄 {} ha spezzato la pasta nel sugo con il cucchiaio.", -1, Boon::disliked},
    Mishap{"🧄 {} ha bruciato l'aglio e ha aperto tutte le finestre.", 0, Boon::none},
    Mishap{"🍅 {} ha fatto la passata e ne ha regalata mezza al vicino.", 0, Boon::donate},
    Mishap{"🫒 {} ha trovato un'oliva nel divano. Era del 2019.", 0, Boon::none},
    Mishap{"🍷 {} ha aperto una bottiglia buona per sbaglio, di martedì.", -2, Boon::liked},
    Mishap{"🧀 {} ha grattugiato il formaggio sul pesce. Silenzio in sala.", 0, Boon::disliked},
    Mishap{"🥬 {} ha comprato l'insalata in busta e l'ha lavata due volte.", 0, Boon::none},
    Mishap{"🍒 {} ha mangiato una ciliegia e poi tutte le altre.", 1, Boon::none},
    Mishap{"📺 {} ha guardato una televendita fino in fondo e ha quasi comprato.", 0, Boon::none},
    Mishap{"🎞️ {} ha rivisto un film e ha capito il finale solo adesso.", 0, Boon::none},
    Mishap{"📼 {} ha trovato una videocassetta e niente per vederla.", 0, Boon::none},
    Mishap{"🎙️ {} ha partecipato a un podcast di due ore su niente.", 0, Boon::liked},
    Mishap{"📱 Il telefono di {} si è aggiornato e ha spostato tutte le icone.", 0, Boon::teleport},
    Mishap{"🔋 {} ha caricato il telefono al 100% e si è scaricato lo stesso.", 0, Boon::none},
    Mishap{"💻 {} ha chiuso il portatile senza salvare. Di nuovo.", -3, Boon::none},
    Mishap{"🖨️ La stampante di {} chiede il toner a colori per stampare in nero.", -2, Boon::disliked},
    Mishap{"📡 Il wifi di {} funziona solo in corridoio.", 0, Boon::none},
    Mishap{"🛜 {} ha indovinato la password del wifi del vicino al terzo tentativo.", 2, Boon::none},
    Mishap{"🧑‍⚕️ {} ha fatto una visita di controllo ed è tutto a posto.", 0, Boon::liked},
    Mishap{"💊 {} ha preso una vitamina scaduta e si sente benissimo.", 0, Boon::boost},
    Mishap{"🩹 {} si è tagliato con un foglio di carta. Male.", -1, Boon::none},
    Mishap{"🦠 {} ha starnutito in ascensore e ha cambiato piano.", 0, Boon::disliked},
    Mishap{"🫁 {} ha fatto le scale a piedi e si è pentito al terzo piano.", 0, Boon::none},
    Mishap{"🚇 {} ha corso per prendere la metro ed era quella sbagliata.", 0, Boon::teleport},
    Mishap{"🚌 L'autobus di {} è passato in anticipo. Cosa mai vista.", 2, Boon::none},
    Mishap{"🚕 Il tassista di {} ha fatto il giro lungo e lui lo sapeva.", -3, Boon::none},
    Mishap{"✈️ Il volo di {} è stato spostato a un altro aeroporto.", 0, Boon::teleport},
    Mishap{"🛳️ {} ha preso il traghetto sbagliato ed è finito su un'isola.", 0, Boon::teleport},
    Mishap{"⚽ {} ha guardato la partita e ha perso la sua squadra.", -1, Boon::none},
    Mishap{"🎾 {} ha rotto la racchetta contro la rete. Colpa della rete.", -2, Boon::disliked},
    Mishap{"🏊 {} ha nuotato mille metri e ne ha raccontati duemila.", 0, Boon::disliked},
    Mishap{"🚴 {} ha fatto una salita in bici e l'ha messa su Strava.", 1, Boon::liked},
    Mishap{"🏋️ {} ha alzato un bilanciere senza dischi e si è fatto male.", -1, Boon::none},
    Mishap{"🧗 {} è salito su una parete e non sa come scendere.", 0, Boon::marked},
    Mishap{"🎿 {} ha sbagliato pista ed è arrivato in un'altra valle.", 0, Boon::teleport},
    Mishap{"⛳ {} ha fatto buca in uno e non c'era nessuno a vederlo.", 3, Boon::none},
    Mishap{"🥊 {} ha tirato un pugno al sacco e ha vinto il sacco.", -1, Boon::none},
    Mishap{"🏆 {} ha vinto un torneo di briscola al bar.", 2, Boon::liked},
    Mishap{"🧑‍🏫 {} ha spiegato una cosa sbagliata con grande sicurezza.", 0, Boon::disliked},
    Mishap{"📚 {} ha finito un libro cominciato nel 2019.", 1, Boon::liked},
    Mishap{"✏️ {} ha fatto un cruciverba tutto a penna. Coraggioso.", 0, Boon::liked},
    Mishap{"🧾 {} ha pagato una multa del 2021 con la sanzione ridotta.", -3, Boon::forgiven},
    Mishap{"🏛️ {} ha fatto la fila all'anagrafe per un documento già scaduto.", -1, Boon::none},
    Mishap{"📬 {} ha ricevuto una raccomandata e non era niente.", 0, Boon::none},
    Mishap{"🗳️ {} ha votato in assemblea di condominio e ha perso 2 a 8.", 0, Boon::disliked},
    Mishap{"🏠 L'amministratore di condominio ha bussato a {}.", 0, Boon::tithe},
    Mishap{"🔑 {} si è chiuso fuori casa con il forno acceso.", -3, Boon::marked},
    Mishap{"🪜 {} ha cambiato una lampadina senza scala. Ha funzionato.", 1, Boon::liked},
    Mishap{"🌡️ {} ha litigato col termosifone e ha perso.", 0, Boon::none},
    Mishap{"🌬️ Il vento ha portato l'ombrellone di {} alla spiaggia dopo.", 0, Boon::teleport},
    Mishap{"⛈️ Un temporale ha sorpreso {} con i panni stesi.", -2, Boon::none},
    Mishap{"🌈 {} ha visto un arcobaleno doppio e ci ha creduto.", 0, Boon::windfall},
    Mishap{"❄️ È nevicato in casa di {} e in nessun altro posto.", 0, Boon::none},
    Mishap{"🌞 {} ha preso il sole dieci minuti e si è scottato.", -1, Boon::none},
    Mishap{"🌙 {} ha visto la luna piena e ha fatto scelte discutibili.", 0, Boon::marked},
    Mishap{"⭐ {} ha espresso un desiderio su una stella. Era un aereo.", 0, Boon::none},
    Mishap{"🎆 {} ha sparato un botto di capodanno a settembre.", 0, Boon::disliked},
    Mishap{"🕯️ {} ha acceso una candela per il gruppo.", 0, Boon::liked},
    Mishap{"🧿 Qualcuno ha regalato un occhio turco a {}.", 0, Boon::restored},
    Mishap{"🃏 {} ha vinto a scopa con la settebello all'ultima mano.", 0, Boon::windfall},
    Mishap{"🎲 {} ha tirato due dadi e sono usciti entrambi dal tavolo.", 0, Boon::none},
    Mishap{"🀄 {} ha imparato il mahjong in una sera e l'ha già dimenticato.", 0, Boon::none},
    Mishap{"♟️ {} ha dato scacco matto in quattro mosse e se n'è vantato.", 1, Boon::disliked},
    Mishap{"🎳 {} ha fatto strike con la palla dell'altra corsia.", 0, Boon::steal},
    Mishap{"🎮 {} ha finito un gioco al 100% e non ricorda perché.", 0, Boon::liked},
    Mishap{"🕹️ {} ha trovato un cabinato funzionante in un bar.", 2, Boon::none},
    Mishap{"🎫 {} ha trovato un biglietto della lotteria nel cappotto.", 0, Boon::ticket},
    Mishap{"🏷️ {} ha comprato in saldo una cosa che non gli serviva.", -2, Boon::none},
    Mishap{"🛒 {} ha fatto la spesa affamato. Si vede dallo scontrino.", -3, Boon::none},
    Mishap{"💳 La carta di {} è stata rifiutata per un centesimo.", 0, Boon::disliked},
    Mishap{"🏦 La banca di {} ha cambiato le condizioni in otto pagine di mail.", 0, Boon::tithe},
    Mishap{"💰 {} ha trovato un fondo pensione di vent'anni fa.", 0, Boon::windfall},
    Mishap{"📊 Il consulente di {} gli ha detto di diversificare. Ha diversificato.", 0, Boon::swap},
    Mishap{"🧮 Il commercialista di {} ha trovato una detrazione dimenticata.", 0, Boon::windfall},
    Mishap{"🏪 {} ha grattato un biglietto e ne ha vinto un altro.", 0, Boon::ticket},
    Mishap{"🚬 {} ha smesso di fumare per la quarta volta quest'anno.", 0, Boon::liked},
    Mishap{"🍫 {} ha trovato l'ultimo cioccolatino e l'ha lasciato agli altri.", 0, Boon::donate},
    Mishap{"🧁 {} ha portato i dolci in ufficio senza motivo.", -1, Boon::liked},
    Mishap{"🥛 {} ha bevuto il latte guardando la data. Andava bene.", 0, Boon::none},
    Mishap{"🫖 {} ha fatto il tè e se l'è scordato sul fornello.", 0, Boon::none},
    Mishap{"🧽 {} ha pulito a fondo e ha trovato 3 palle sotto il letto.", 3, Boon::none},
    Mishap{"🪣 {} ha rovesciato il secchio appena finito di lavare.", -1, Boon::none},
    Mishap{"🧹 {} ha scopato il pianerottolo anche se non toccava a lui.", 0, Boon::liked},
    Mishap{"🪛 {} ha riparato una cosa con una vite trovata per terra.", 1, Boon::liked},
    Mishap{"🔧 {} ha chiamato l'idraulico e ha risolto da solo mentre aspettava.", 2, Boon::liked},
    Mishap{"🪚 {} ha segato il ramo su cui era seduto. Letteralmente.", -3, Boon::pop},
    Mishap{"🧰 {} ha prestato il trapano e non l'ha più rivisto.", 0, Boon::donate},
    Mishap{"🔨 {} ha piantato un chiodo al primo colpo davanti a testimoni.", 1, Boon::liked},
    Mishap{"🧼 {} ha lavato la macchina e ha piovuto subito dopo.", -1, Boon::none},
    Mishap{"🚗 {} ha trovato parcheggio proprio davanti. Non è normale.", 2, Boon::none},
    Mishap{"🅿️ {} ha preso la multa nell'unico giorno di divieto.", -3, Boon::none},
    Mishap{"🛵 Lo scooter di {} è partito al primo colpo dopo due mesi.", 1, Boon::none},
    Mishap{"🚧 {} ha trovato un cantiere aperto da sei anni ancora aperto.", 0, Boon::none},
    Mishap{"🗼 {} ha fatto il turista nella sua città e gli è piaciuta.", 0, Boon::liked},
    Mishap{"🏖️ {} ha preso un weekend al mare fuori stagione. Ottima idea.", 0, Boon::boost},
    Mishap{"🎪 {} è finito in una sagra di paese e non è più uscito.", 0, Boon::teleport},
    Mishap{"🐔 Una gallina si è affezionata a {} e lo segue ovunque.", 0, Boon::liked},
    Mishap{"🐟 {} ha comprato il pesce di lunedì. Lo sapeva e l'ha fatto lo stesso.", -1, Boon::disliked},
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
