#ifndef NORELECBOT_TELEGRAM_HPP
#define NORELECBOT_TELEGRAM_HPP

#include "config.hpp"
#include "storage.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace norelecbot {

/* Skips the updates received while the bot was offline, then long-polls until stop is set. */
void telegram_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop);

void telegram_say(const AppConfig &config, std::int64_t chat_id, const std::string &text);

}

#endif
