#ifndef NORELECBOT_IRC_HPP
#define NORELECBOT_IRC_HPP

#include "config.hpp"
#include "storage.hpp"

#include <atomic>
#include <string>

namespace norelecbot {

/* Stays connected to the IRC server, reconnecting when needed, until stop is set. */
void irc_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop);

/* Repeats in the IRC channel something the bot said elsewhere; does nothing if IRC is not connected. */
void irc_say(std::string text);
/* Says something to one nick alone, in query. */
void irc_whisper(std::string nick, std::string text);

}

#endif
