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

}

#endif
