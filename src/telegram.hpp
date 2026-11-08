#ifndef NORELECBOT_TELEGRAM_HPP
#define NORELECBOT_TELEGRAM_HPP

#include "config.hpp"
#include "pictures.hpp"
#include "storage.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace norelecbot {

/* Skips the updates received while the bot was offline, then long-polls until stop is set. */
void telegram_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop);

void telegram_say(const AppConfig &config, std::int64_t chat_id, const std::string &text);

/* A message sent so that it can be changed afterwards: its id, or nothing when it could not be sent. */
[[nodiscard]] std::optional<std::int64_t> telegram_send(const AppConfig &config, std::int64_t chat_id,
                                                        const std::string &text);
void telegram_edit(const AppConfig &config, std::int64_t chat_id, std::int64_t message_id, const std::string &text);

/* A picture built into the bot, with the text under it: uploaded the first time, then sent again by the id
   Telegram gave it. Too long for a caption, the text follows as a message of its own. */
void telegram_photo(const AppConfig &config, std::int64_t chat_id, const Picture &picture, const std::string &text);

}

#endif
