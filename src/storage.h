#ifndef NORELECBOT_STORAGE_H
#define NORELECBOT_STORAGE_H

#include "platform.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    PlatformMutex mutex;
    char conquister_path[1024];
    char quotes_path[1024];
    uint64_t quote_random_state;
    bool initialized;
} Storage;

bool storage_open(Storage *storage, const char *conquister_path, const char *quotes_path);
void storage_close(Storage *storage);

#endif
