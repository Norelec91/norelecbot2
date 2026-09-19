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
    Mishap{"🕳️ Una buca ha fatto domanda per diventare voragine e {} ha firmato come testimone.", -1, Boon::none},
    Mishap{"🎈 Un palloncino ha citofonato a {} dicendo di essere un parente lontano.", 0, Boon::balloon},
    Mishap{"🛸 {} è stato caricato su un disco volante con targa di Frosinone.", 0, Boon::teleport},
    Mishap{"🥤 {} ha bevuto un energy drink scaduto nel 2011 e vede il futuro per tre secondi.", 0, Boon::boost},
    Mishap{"🦆 Un'anatra ha aperto un fascicolo su {} e lo aggiorna ogni giovedì.", 0, Boon::disliked},
    Mishap{"👵 {} ha portato la spesa di una signora fino al quinto piano senza ascensore.", 0, Boon::liked},
    Mishap{"🔺 Un triangolo col monocolo ha promosso {} a revisore del lato segreto.", 0, Boon::liked},
    Mishap{"🎯 {} ha tentato il 360 noscope e ha centrato il citofono dei vicini.", -3, Boon::none},
    Mishap{"🌮 Due palle sono rotolate nella busta della spesa di {} e si sono nascoste sotto il pane.", 2, Boon::none},
    Mishap{"📢 Un airhorn è partito dentro l'armadio di {} alle 04:07 senza spiegazioni.", 0, Boon::disliked},
    Mishap{"🚔 Il verbale di {} è stato annullato perché redatto tutto in corsivo.", 0, Boon::forgiven},
    Mishap{"🧦 La lavatrice di {} ha trattenuto un calzino come garanzia e due palle come pegno.", -2, Boon::none},
    Mishap{"🪑 {} ha montato una libreria svedese e gli sono avanzate tre viti e un rimorso.", 0, Boon::none},
    Mishap{"🍀 {} ha trovato un quadrifoglio nel parcheggio di un centro commerciale.", 1, Boon::none},
    Mishap{"🐦 Un piccione ha fatto rapporto su {} al piccione capo.", 0, Boon::disliked},
    Mishap{"🌀 {} è entrato in una porta girevole ed è uscito in un'altra regione.", 0, Boon::teleport},
    Mishap{"📻 La radio di {} trasmette solo jingle di supermercati chiusi nel 2009.", 0, Boon::disliked},
    Mishap{"🧾 {} ha trovato uno scontrino con sopra una palla mai riscossa.", 1, Boon::none},
    Mishap{"🔌 {} ha staccato la ciabatta sbagliata e il condominio ha perso una palla.", -1, Boon::none},
    Mishap{"🎈 Dal tombino è salito un palloncino che chiedeva espressamente di {}.", 0, Boon::balloon},
    Mishap{"🥶 Il congelatore di {} si è scongelato nell'unica settimana senza controlli.", -3, Boon::none},
    Mishap{"🧯 {} ha spento un principio d'incendio con la giacca buona.", -2, Boon::liked},
    Mishap{"🪞 Lo specchio di {} ha smesso di rispondere alle domande dirette.", 0, Boon::none},
    Mishap{"🚪 {} ha sbagliato ascensore ed è arrivato a un piano che non risulta in planimetria.", 0, Boon::teleport},
    Mishap{"🍕 Il fornaio ha dato a {} due palle di resto perché era finita la moneta.", 2, Boon::none},
    Mishap{"🪰 Una mosca ha seguito {} per undici minuti prendendo appunti.", -1, Boon::none},
    Mishap{"📠 A casa di {} è arrivato un fax da un ufficio chiuso nel 1998.", 0, Boon::none},
    Mishap{"🧊 {} ha messo il ghiaccio nel caffè e il barista ha chiamato qualcuno.", 0, Boon::none},
    Mishap{"🪤 {} è inciampato in una trappola per talpe montata al contrario.", -2, Boon::none},
    Mishap{"🐕 Un cane randagio ha eletto {} referente ufficiale del quartiere.", 0, Boon::liked},
    Mishap{"🧻 La carta igienica è finita esattamente quando serviva a {}.", -1, Boon::none},
    Mishap{"😾 Il gatto del vicino ha smesso di salutare {} senza dare spiegazioni.", 0, Boon::disliked},
    Mishap{"🚒 {} ha fatto scendere un gatto dall'albero da solo e i pompieri hanno applaudito.", 0, Boon::liked},
    Mishap{"🛒 Il carrello di {} ha scelto la sua direzione preferita per due corsie.", -2, Boon::none},
    Mishap{"🥴 {} ha detto prego al posto di grazie e se n'è accorto tutto il bar.", 0, Boon::disliked},
    Mishap{"👃 {} ha starnutito dentro il microfono di una riunione importante.", 0, Boon::disliked},
    Mishap{"🪙 {} ha trovato una moneta nella tasca di un cappotto dell'anno scorso.", 1, Boon::none},
    Mishap{"🧱 {} ha contato i mattoni del muro di casa e il numero non torna mai.", 0, Boon::none},
    Mishap{"🎺 Una banda di paese è passata sotto la finestra di {} suonando bene.", 0, Boon::liked},
    Mishap{"🌌 Un varco temporale si è aperto nella dispensa di {} ed era pieno di legumi.", 0, Boon::teleport},
    Mishap{"⚡ {} ha toccato la maniglia e ha preso una scossa che gli ha triplicato le idee.", 0, Boon::boost},
    Mishap{"🥒 {} ha mangiato sottaceti a stomaco vuoto davanti a testimoni.", 0, Boon::disliked},
    Mishap{"🍫 Nella macchinetta si sono incastrate due palle e sono cadute a {}.", 2, Boon::none},
    Mishap{"🌻 {} ha innaffiato le piante del pianerottolo senza che nessuno glielo chiedesse.", 0, Boon::liked},
    Mishap{"🌊 Un'onda anomala in vasca ha spedito {} dall'altra parte della mappa.", 0, Boon::teleport},
    Mishap{"🪜 {} è salito su una scala che qualcuno aveva accorciato di un gradino.", -3, Boon::none},
    Mishap{"🎁 Un pacco senza mittente per {} conteneva due palle e una ricevuta bianca.", 2, Boon::none},
    Mishap{"🎈 Il palloncino di {} è stato bucato da un ago della Guardia di Finanza.", 0, Boon::pop},
    Mishap{"💨 Il palloncino di {} si è sgonfiato da solo per protesta sindacale.", 0, Boon::pop},
    Mishap{"🧊 Il moltiplicatore di {} è stato congelato dall'ufficio competente.", 0, Boon::flat},
    Mishap{"🪫 Il bonus di {} si è scaricato prima di essere usato.", 0, Boon::flat},
    Mishap{"🎁 {} ha regalato cinquanta palle a uno sconosciuto per pura distrazione.", 0, Boon::donate},
    Mishap{"🪝 {} ha agganciato cinquanta palle dalla tasca di un passante.", 0, Boon::steal},
    Mishap{"🔄 {} e un altro giocatore hanno scambiato i portafogli per errore all'anagrafe.", 0, Boon::swap},
    Mishap{"♻️ {} è stato riabilitato da una commissione che non sapeva perché.", 0, Boon::restored},
    Mishap{"🔓 {} è stato liberato da un cavillo scritto a matita.", 0, Boon::freed},
    Mishap{"🎯 {} è stato segnato sul taccuino di qualcuno che non dimentica.", 0, Boon::marked},
    Mishap{"🥇 {} ha ceduto il posto in fila e la fila lo ha applaudito.", 0, Boon::liked},
    Mishap{"🧨 {} ha risposto vocale a un messaggio scritto e il gruppo ha perso la pazienza.", 0, Boon::disliked},
    Mishap{"🚿 La doccia di {} è passata da bollente a gelata in mezzo secondo.", -1, Boon::none},
    Mishap{"🤝 {} ha restituito un portafoglio trovato e non ha chiesto nulla in cambio.", -2, Boon::liked},
    Mishap{"🪛 {} ha stretto la vite finché non si è spanata.", 0, Boon::none},
    Mishap{"🕰️ L'orologio di {} va indietro di un minuto al giorno da sempre.", -1, Boon::none},
    Mishap{"🎂 {} ha ricordato un compleanno che tutti avevano dimenticato.", 0, Boon::liked},
    Mishap{"🧴 Il dosatore del sapone ha sparato addosso a {} una dose intera.", -2, Boon::none},
    Mishap{"🙄 {} ha spiegato un meme e il meme è morto sul colpo.", 0, Boon::disliked},
    Mishap{"🥁 {} ha trovato il ritmo giusto battendo sul tavolo con due penne.", 0, Boon::none},
    Mishap{"🫖 Il bollitore di {} fischia anche da spento.", -2, Boon::liked},
    Mishap{"🪴 {} ha salvato una pianta che tutti davano per persa.", 1, Boon::none},
    Mishap{"🧽 {} ha pulito la cucina alle tre di notte per motivi suoi.", -2, Boon::none},
    Mishap{"🚲 {} ha gonfiato la gomma della bici di uno sconosciuto.", 0, Boon::liked},
    Mishap{"🍞 {} ha dimenticato il pane nel forno per undici ore.", 1, Boon::none},
    Mishap{"📦 Il corriere ha consegnato a {} il pacco giusto al primo tentativo.", -3, Boon::none},
    Mishap{"🧲 Tutte le forchette del cassetto si sono attaccate a {}.", 0, Boon::liked},
    Mishap{"🎠 {} è salito su una giostra ferma dal 1994 e si è mossa.", 0, Boon::none},
    Mishap{"🪞 {} ha rotto uno specchio e ha ricevuto la fattura dei sette anni.", 0, Boon::none},
    Mishap{"🎈 Un palloncino si è impigliato nell'antenna di {} e ha deciso di restare.", 0, Boon::balloon},
    Mishap{"📸 {} è uscito bene in una foto scattata di nascosto.", -3, Boon::none},
    Mishap{"🧃 {} ha bucato il succo con la cannuccia dalla parte sbagliata.", 2, Boon::liked},
    Mishap{"🍋 {} ha morso un limone convinto che fosse un mandarino.", -3, Boon::none},
    Mishap{"🌪️ Un vortice di sacchetti ha portato {} in un altro quadrante.", 1, Boon::none},
    Mishap{"🚇 {} ha preso la metro nel verso opposto e non se ne è accorto per otto fermate.", 0, Boon::teleport},
    Mishap{"🎈 Il palloncino di {} è stato requisito dal Ministero del Made in Italy.", 0, Boon::teleport},
    Mishap{"🧑‍🍳 {} ha rifatto la carbonara di nonna e il quartiere ha sentito il profumo.", 0, Boon::flat},
    Mishap{"🐝 Un'ape ha scelto la testa di {} come punto di osservazione.", 0, Boon::liked},
    Mishap{"💶 {} ha pagato il caffè il doppio pur di non fare la fila e se n'è vantato.", -1, Boon::disliked},
    Mishap{"🎲 {} ha tirato un dado ed è rimasto in piedi sullo spigolo per due palle.", 2, Boon::none},
    Mishap{"🎈 Il palloncino di {} è stato bucato dalla penna di un funzionario annoiato.", 2, Boon::none},
    Mishap{"🎈 Il palloncino di {} è stato bucato dalla penna di un funzionario annoiato.", 0, Boon::pop},
    Mishap{"🚀 {} è stato spedito su un razzo di cartone che ha funzionato benissimo.", 0, Boon::teleport},
    Mishap{"💝 {} ha lasciato cinquanta palle come mancia senza dirlo a nessuno.", 0, Boon::donate},
    Mishap{"🥷 {} ha alleggerito qualcuno di cinquanta palle passando dalle scale.", 0, Boon::steal},
    Mishap{"🙃 {} ha messo il pollice in su a una notizia tragica.", 0, Boon::disliked},
    Mishap{"♻️ {} è stato reintegrato dopo un ricorso che non aveva presentato.", 0, Boon::restored},
    Mishap{"🍀 {} ha grattato un gratta e vinci trovato per terra e ha vinto tre palle.", 3, Boon::none},
    Mishap{"🧊 {} ha inciampato in un cubetto di ghiaccio caduto ieri.", -1, Boon::none},
    Mishap{"🫶 {} ha presentato due persone che ora si vogliono bene.", 2, Boon::liked},
    Mishap{"📍 Qualcuno ha messo una puntina sulla mappa esattamente dove sta {}.", 0, Boon::marked},
    Mishap{"🪁 {} ha fatto volare un aquilone senza vento e ha guadagnato una palla.", 1, Boon::none},
    Mishap{"🧓 {} ha ascoltato per intero la storia di un signore alla fermata.", -2, Boon::liked},
    Mishap{"🥱 {} ha sbadigliato durante il discorso di qualcuno che ci teneva.", 0, Boon::disliked},
    Mishap{"😤 {} ha risposto ok punto a un messaggio lungo dieci righe.", 0, Boon::disliked},
    Mishap{"🐌 {} ha calpestato una lumaca e il quartiere lo ha visto.", -1, Boon::disliked},
    Mishap{"🪣 {} ha rovesciato un secchio d'acqua nel corridoio appena lavato.", 0, Boon::none},
    Mishap{"🎁 {} ha dato cinquanta palle a chi diceva di averne bisogno.", 0, Boon::donate},
    Mishap{"🔧 {} ha riparato il rubinetto che perdeva dal 2019.", 0, Boon::none},
    Mishap{"🧁 {} ha portato i dolci in ufficio senza che fosse il suo compleanno.", -2, Boon::liked},
    Mishap{"🚦 {} ha preso tutti i semafori rossi tranne uno.", 0, Boon::disliked},
    Mishap{"🪟 La finestra di {} si è aperta da sola durante il temporale.", 0, Boon::none},
    Mishap{"🧹 {} ha spazzato le scale del condominio per pura educazione.", 1, Boon::none},
    Mishap{"🥅 {} ha parato un rigore in una partita che non stava giocando.", 0, Boon::none},
    Mishap{"📖 {} ha finito un libro cominciato quattro anni fa.", 0, Boon::none},
    Mishap{"🕯️ {} ha acceso una candela e la corrente è tornata subito dopo.", 0, Boon::none},
    Mishap{"🎻 Un violinista ha suonato per {} e non ha voluto niente.", 0, Boon::liked},
    Mishap{"🌉 {} ha attraversato un ponte che non risulta sulle mappe.", 0, Boon::teleport},
    Mishap{"🐿️ Uno scoiattolo ha nascosto qualcosa nella tasca di {} e non dice cosa.", 0, Boon::none},
    Mishap{"🧯 {} ha letto tutte le istruzioni dell'estintore ad alta voce.", -3, Boon::none},
    Mishap{"🚜 Un trattore ha bloccato la strada di {} per venti minuti.", -2, Boon::disliked},
    Mishap{"🍇 {} ha comprato uva senza semi piena di semi.", 0, Boon::none},
    Mishap{"🎃 {} ha trovato una zucca sul pianerottolo a marzo.", 2, Boon::none},
    Mishap{"🛎️ {} ha suonato un campanello e è scappato come nel 2003.", 0, Boon::liked},
    Mishap{"🧀 {} ha grattugiato il parmigiano fino alle dita.", 0, Boon::boost},
    Mishap{"🚨 {} ha fatto scattare l'antitaccheggio uscendo senza aver preso nulla.", -1, Boon::none},
    Mishap{"🎤 {} ha cantato benissimo pensando di essere solo in macchina.", 0, Boon::disliked},
    Mishap{"🪩 Una palla da discoteca si è staccata sopra la testa di {}.", 0, Boon::none},
    Mishap{"🛼 {} ha messo i pattini in casa e la casa ha vinto.", 0, Boon::teleport},
    Mishap{"🐠 Il pesce rosso di {} lo fissa da tre giorni senza battere ciglio.", 2, Boon::none},
    Mishap{"🍺 {} ha aperto una birra con l'accendino al primo colpo.", -3, Boon::none},
    Mishap{"🧗 {} è salito sul tetto per recuperare un pallone ed è sceso in un'altra via.", 0, Boon::teleport},
    Mishap{"🌙 {} ha visto la luna di giorno e l'ha fatto notare a tutti.", 0, Boon::teleport},
    Mishap{"🕸️ {} ha camminato dentro una ragnatela a faccia aperta.", -1, Boon::none},
    Mishap{"🦷 {} ha morso un pezzo di pane e ha sentito un rumore sbagliato.", -2, Boon::disliked},
    Mishap{"💤 {} si è addormentato durante il proprio turno di guardia.", 0, Boon::disliked},
    Mishap{"🎢 {} è salito su una giostra che lo ha depositato in un altro quartiere.", 1, Boon::liked},
    Mishap{"🚗 {} ha trovato parcheggio davanti al portone in pieno centro.", -1, Boon::none},
    Mishap{"🧨 {} ha acceso una miccia bagnata davanti a gente che prende nota.", 0, Boon::marked},
    Mishap{"🗿 Una statua ha seguito {} con lo sguardo e due testimoni lo confermano.", 0, Boon::teleport},
    Mishap{"🌐 {} è stato teletrasportato da una pubblicità a schermo intero.", 3, Boon::none},
    Mishap{"🎖️ {} ha ricevuto una medaglia per meriti mai specificati e ha pagato la targa.", -1, Boon::none},
    Mishap{"🦟 Una zanzara ha scelto {} tra dodici persone nella stessa stanza.", 2, Boon::liked},
    Mishap{"🍯 {} ha aperto il miele nuovo e non si è sporcato le mani.", 0, Boon::disliked},
    Mishap{"😬 {} ha detto anche a te a un buon compleanno.", 1, Boon::liked},
    Mishap{"🌟 {} ha espresso un desiderio e una stella cadente lo ha registrato.", 0, Boon::liked},
    Mishap{"🫴 {} ha offerto il caffè a tutta la fila.", -3, Boon::forgiven},
    Mishap{"⚖️ La penalità di {} è caduta perché il modulo era piegato male.", -1, Boon::none},
    Mishap{"🧤 {} ha perso un guanto e ha trovato l'altro dell'anno scorso.", 0, Boon::none},
    Mishap{"🥯 {} ha trovato una ciambella senza buco e nessuno gli crede.", 0, Boon::disliked},
    Mishap{"💸 Il fisco ha prelevato a {} la decima parte di tutto per la sagra.", 0, Boon::tithe},
    Mishap{"🚩 {} è finito in una lista scritta a penna rossa.", -3, Boon::marked},
    Mishap{"🧎 {} ha chiesto scusa per primo e la lite è finita lì.", 1, Boon::liked},
    Mishap{"🪵 {} ha spaccato la legna per tutto il palazzo.", 0, Boon::none},
    Mishap{"🌋 Un geyser domestico ha spedito {} in orbita bassa.", 0, Boon::teleport},
    Mishap{"🩹 {} si è tagliato con la carta di un contratto mai firmato.", -2, Boon::none},
    Mishap{"💰 A {} è arrivata una rendita improvvisa pari a un decimo di quello che ha.", 0, Boon::windfall},
    Mishap{"🎬 {} è finito sullo sfondo di un servizio del telegiornale.", 0, Boon::none},
    Mishap{"🧴 {} ha usato lo shampoo del coinquilino per tutta la settimana.", -1, Boon::none},
    Mishap{"📌 Qualcuno ha appuntato il nome di {} sulla bacheca sbagliata.", 0, Boon::marked},
    Mishap{"🛏️ {} ha rifatto il letto con le lenzuola di due set diversi.", 0, Boon::none},
    Mishap{"😑 {} ha risposto con un vocale di undici minuti.", 0, Boon::disliked},
    Mishap{"🫂 {} ha abbracciato uno sconosciuto e lo sconosciuto ne aveva bisogno.", 0, Boon::liked},
    Mishap{"🧹 {} è stato ripulito da ogni accusa da una commissione distratta.", 0, Boon::restored},
    Mishap{"💰 A {} è caduta addosso una fortuna pari a un decimo del suo mucchio.", 0, Boon::windfall},
    Mishap{"🥬 {} ha comprato l'insalata già lavata e l'ha lavata lo stesso.", 0, Boon::none},
    Mishap{"🪒 {} si è tagliato radendosi per la fretta.", 0, Boon::none},
    Mishap{"🐜 Una fila di formiche ha attraversato la cucina di {} con una palla in spalla.", 1, Boon::disliked},
    Mishap{"🕵️ {} ha alleggerito un incauto di cinquanta palle durante la fila alle poste.", 0, Boon::steal},
    Mishap{"🍦 {} ha offerto il gelato e ha preso il gusto peggiore per sé.", 0, Boon::liked},
    Mishap{"🪩 {} ha trovato due palle sotto il divano insieme a un telecomando del 2010.", 2, Boon::none},
    Mishap{"🎟️ {} ha trovato in tasca un biglietto della lotteria mai comprato.", 0, Boon::ticket},
    Mishap{"🧊 {} ha rotto un bicchiere e ha incolpato la corrente d'aria.", -2, Boon::none},
    Mishap{"🛠️ {} ha smontato il tostapane per curiosità e ora c'è un pezzo in più.", -3, Boon::none},
    Mishap{"🙊 {} ha raccontato un segreto al gruppo sbagliato.", 0, Boon::disliked},
    Mishap{"💸 Il fisco ha chiesto a {} un decimo del patrimonio per una fontana.", 0, Boon::tithe},
    Mishap{"💰 {} ha trovato sotto il materasso un decimo di quello che aveva già.", 0, Boon::windfall},
    Mishap{"🔁 {} ha scambiato di posto in classifica con uno che dormiva.", 0, Boon::swap},
    Mishap{"💰 Un fondo dimenticato ha fruttato a {} un decimo di tutto.", 0, Boon::windfall},
    Mishap{"🎟️ Un biglietto della lotteria è finito nella giacca di {} da solo.", 0, Boon::ticket},
    Mishap{"🥳 {} ha organizzato una festa e sono venuti tutti.", 0, Boon::liked},
    Mishap{"🎁 {} ha lasciato cinquanta palle sul tavolo con un biglietto gentile.", 0, Boon::donate},
    Mishap{"😐 {} ha corretto la grammatica a chi stava raccontando una disgrazia.", -1, Boon::liked},
    Mishap{"🧭 {} ha camminato un'ora seguendo una bussola rotta.", 0, Boon::none},
    Mishap{"🪆 {} ha aperto una matrioska e dentro c'era un'altra matrioska identica.", 0, Boon::none},
    Mishap{"🎇 {} ha acceso i fuochi d'artificio nel giorno giusto per tre palle.", 3, Boon::none},
    Mishap{"🧦 {} ha indossato due calzini diversi e nessuno ha detto niente.", -1, Boon::none},
    Mishap{"👏 {} ha fatto un applauso e la sala lo ha seguito.", 0, Boon::liked},
    Mishap{"🎧 {} ha prestato le cuffie a chi non aveva musica per una palla.", 1, Boon::liked},
    Mishap{"🫰 {} ha pagato il conto di un tavolo di sconosciuti per due palle.", 2, Boon::liked},
    Mishap{"🪃 Il boomerang di {} è tornato indietro e gli ha bucato il palloncino.", -3, Boon::pop},
    Mishap{"🎁 {} ha regalato cinquanta palle al primo che passava, senza motivo.", 0, Boon::donate},
    Mishap{"🥤 {} ha portato l'acqua a chi correva sotto il sole e ha guadagnato una palla.", 1, Boon::liked},
    Mishap{"🍳 {} ha rotto l'uovo con il guscio dentro la padella.", -1, Boon::none},
    Mishap{"🍀 {} ha trovato due palle nella fodera di una vecchia giacca.", 2, Boon::none},
    Mishap{"🚿 La caldaia di {} si è rotta l'unico giorno freddo dell'anno.", -3, Boon::none},
    Mishap{"🎣 {} ha pescato una palla da un tombino con un magnete.", 1, Boon::none},
    Mishap{"🧮 {} ha rifatto i conti tre volte e tornavano già la prima.", 0, Boon::none},
    Mishap{"🫡 {} ha rispettato un patto che nessuno avrebbe controllato.", 0, Boon::liked},
    Mishap{"🥤 {} ha bevuto un integratore scaduto e per un turno rende il triplo.", 0, Boon::boost},
    Mishap{"🌠 Un lampo ha spostato {} a caso sulla mappa.", 0, Boon::teleport},
    Mishap{"🍅 {} ha coltivato pomodori sul balcone e li ha regalati a tutti.", 0, Boon::liked},
    Mishap{"🐟 {} ha comprato il pesce di lunedì sapendo benissimo che era lunedì.", -1, Boon::disliked},
};

/* The pinball table underneath the chat: now and then a message hits something. */
inline constexpr std::array flippers{
    Mishap{"🕳️ La palla di {} sparisce nel mobile e il mobile emette una ricevuta: -500 palle.", -500, Boon::none},
    Mishap{"🎪 KICKBACK su {}: il flipper restituisce +250 palle e pretende che gli si dia del lei.", 250, Boon::liked},
    Mishap{"💥 JACKPOT di {}: il punteggio raddoppia e IL FAMOSO NONNO aggiunge 3 palle di tasca sua.", 3, Boon::grandfather},
    Mishap{"🔵 EXTRA BALL per {}: dalla gettoniera esce un palloncino già gonfiato e leggermente offeso.", 0, Boon::balloon},
    Mishap{"🏁 {} abbatte l'ultimo bersaglio e dal controsoffitto piovono 1000 palle e un calendario del 2013.", 1000, Boon::none},
    Mishap{"🚨 {} scuote il mobile una volta di troppo. TILT: metà delle palle se ne va senza salutare.", 0, Boon::halved},
    Mishap{"⚡ La rampa spara {} fuori dal tavolo e il vetro si richiude come se niente fosse.", 0, Boon::teleport},
    Mishap{"🎯 Il bumper timbra il cartellino di {}: +100 palle di straordinario non richiesto.", 100, Boon::none},
    Mishap{"🔁 REPLAY per {}: la penalità torna indietro perché il modulo era in comic sans.", 0, Boon::forgiven},
    Mishap{"🎰 MULTIBALL per {}: il prossimo possesso vale il triplo e parte una sirena da nave.", 0, Boon::boost},
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
