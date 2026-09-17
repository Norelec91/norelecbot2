#ifndef NORELECBOT_RAIDS_HPP
#define NORELECBOT_RAIDS_HPP

#include "config.hpp"
#include "storage.hpp"

#include <atomic>

namespace norelecbot {

/* Settles the raids that arrive or come home while nobody is writing, and tells both sides. */
void raids_run(Storage &storage, const AppConfig &config, const std::atomic<bool> &stop);

}

#endif
