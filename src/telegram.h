#ifndef NORELECBOT_TELEGRAM_H
#define NORELECBOT_TELEGRAM_H

#include "config.h"
#include "storage.h"

#include <signal.h>

int telegram_run(Storage *storage, const AppConfig *config, volatile sig_atomic_t *stop);

#endif
