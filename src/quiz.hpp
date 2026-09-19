#ifndef NORELECBOT_QUIZ_HPP
#define NORELECBOT_QUIZ_HPP

#include <array>
#include <string_view>

namespace norelecbot {

struct Question {
    std::string_view asked;
    std::string_view answer;
};

/* Questions anybody in the group can answer, or argue about. */
inline constexpr std::array questions{
    Question{"In che anno è finita la prima guerra mondiale?", "1918"},
    Question{"Quanti sono i gironi dell'Inferno di Dante?", "nove"},
    Question{"Come si chiama il fiume che attraversa Roma?", "tevere"},
    Question{"Qual è la capitale dell'Australia?", "canberra"},
    Question{"Quante corde ha un basso elettrico classico?", "quattro"},
    Question{"Chi ha dipinto la Gioconda?", "leonardo"},
    Question{"Quanti minuti dura un tempo di pallacanestro FIBA?", "dieci"},
    Question{"Qual è il metallo liquido a temperatura ambiente?", "mercurio"},
    Question{"In che regione si trova Falconara Marittima?", "marche"},
    Question{"Quante zampe ha un ragno?", "otto"},
    Question{"Qual è il pianeta più vicino al Sole?", "mercurio"},
    Question{"Come si chiama il verso del gabbiano in italiano?", "stridio"},
    Question{"Quanti bit ci sono in un byte?", "otto"},
    Question{"Chi ha scritto Il Nome della Rosa?", "eco"},
    Question{"Qual è il mare a est dell'Italia?", "adriatico"},
    Question{"Quante carte ha un mazzo da briscola?", "quaranta"},
    Question{"In che anno è caduto il muro di Berlino?", "1989"},
    Question{"Come si chiama la moneta del Giappone?", "yen"},
    Question{"Quanti lati ha un esagono?", "sei"},
    Question{"Qual è la montagna più alta d'Italia?", "bianco"},
};

/* Words long enough to be worth scrambling. */
inline constexpr std::array anagrams{
    "palloncino", "conquista", "citazione", "pallista", "terremoto",
    "biglietto", "zodiaco", "penalità", "razzia", "classifica",
};

/* Words with no accents, so reading them backwards stays a word-sized job. */
inline constexpr std::array mirrors{
    "palla", "gioco", "regno", "quota", "denaro",
    "tastiera", "fortuna", "pianeta", "gettone", "cartone",
};

/* A country and its capital, for the game that asks. */
inline constexpr std::array capitals{
    Question{"Giappone", "tokyo"},      Question{"Portogallo", "lisbona"},
    Question{"Norvegia", "oslo"},       Question{"Marocco", "rabat"},
    Question{"Canada", "ottawa"},       Question{"Grecia", "atene"},
    Question{"Croazia", "zagabria"},    Question{"Egitto", "cairo"},
    Question{"Australia", "canberra"},  Question{"Turchia", "ankara"},
};

/* Emoji on one side, the word they draw on the other. */
inline constexpr std::array emojis{
    Question{"🐭🧀", "topo"},           Question{"🌊🏄", "onda"},
    Question{"🔥🚒", "incendio"},       Question{"🌧️☂️", "pioggia"},
    Question{"🎂🕯️", "compleanno"},    Question{"⚽🥅", "gol"},
    Question{"🚂🛤️", "treno"},         Question{"🌕🐺", "luna"},
    Question{"🍝🍅", "pasta"},          Question{"📚🎒", "scuola"},
};

/* Something that happened, and the year it happened in. */
inline constexpr std::array years{
    Question{"lo sbarco sulla Luna", "1969"},
    Question{"la caduta del muro di Berlino", "1989"},
    Question{"il primo iPhone", "2007"},
    Question{"l'Italia campione del mondo in Germania", "2006"},
    Question{"l'affondamento del Titanic", "1912"},
    Question{"l'unità d'Italia", "1861"},
    Question{"la fine della seconda guerra mondiale", "1945"},
    Question{"l'euro nei portafogli", "2002"},
    Question{"il disastro di Chernobyl", "1986"},
    Question{"la scoperta dell'America", "1492"},
};

/* How a proverb starts, and how it has to finish. */
inline constexpr std::array proverbs{
    Question{"Chi dorme", "non piglia pesci"},
    Question{"Tanto va la gatta al lardo", "che ci lascia lo zampino"},
    Question{"Meglio un uovo oggi", "che una gallina domani"},
    Question{"Chi fa da sé", "fa per tre"},
    Question{"A caval donato", "non si guarda in bocca"},
    Question{"L'erba del vicino", "è sempre più verde"},
    Question{"Non tutte le ciambelle", "riescono col buco"},
    Question{"Chi va piano", "va sano e va lontano"},
    Question{"Can che abbaia", "non morde"},
    Question{"Rosso di sera", "bel tempo si spera"},
};

/* An animal, described the long way round. */
inline constexpr std::array animals{
    Question{"ha la proboscide e non dimentica niente", "elefante"},
    Question{"dorme di giorno e porta male se ti attraversa la strada", "gatto"},
    Question{"cambia colore a seconda di dove si appoggia", "camaleonte"},
    Question{"ha otto braccia e tre cuori", "polpo"},
    Question{"porta la casa sulla schiena e non ha fretta", "lumaca"},
    Question{"ripete quello che sente e ha le piume", "pappagallo"},
    Question{"ha il collo lungo e mangia dagli alberi", "giraffa"},
    Question{"si rotola nel fango e fa la ricotta", "maiale"},
    Question{"salta e tiene il figlio nella tasca", "canguro"},
    Question{"fa il miele e lavora troppo", "ape"},
};

/* Colours, one of which the bot is thinking of. */
inline constexpr std::array colours{
    "rosso", "verde", "giallo", "azzurro", "viola",
    "arancione", "marrone", "rosa", "grigio", "nero",
};

/* Three letters a word has to contain, all of them. */
inline constexpr std::array triples{
    "art", "cns", "lmo", "pri", "tsa",
    "gno", "brd", "vel", "chi", "rst",
};

/* Words whose last three letters are easy to match. */
inline constexpr std::array rhymes{
    "pallone", "cuore", "destino", "canzone", "mattina",
    "quaderno", "pensiero", "fortuna", "bicchiere", "montagna",
};

}

#endif
