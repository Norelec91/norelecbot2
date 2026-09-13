#ifndef NORELECBOT_IRC_HPP
#define NORELECBOT_IRC_HPP

#include "config.hpp"
#include "storage.hpp"

#include <atomic>

namespace norelecbot {

/* Stays connected to the IRC server, reconnecting when needed, until stop is set. */
void irc_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop);

}

#endif
