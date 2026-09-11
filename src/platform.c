#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <stdio.h>
#include <threads.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void platform_sleep_milliseconds(unsigned long milliseconds) {
    struct timespec remaining = {
        .tv_sec = (time_t)(milliseconds / 1000UL),
        .tv_nsec = (long)(milliseconds % 1000UL) * 1000000L,
    };
    while (thrd_sleep(&remaining, &remaining) == -1) {
    }
}

bool platform_local_time(time_t timestamp, struct tm *result) {
    if (result == nullptr) {
        return false;
    }
#ifdef _WIN32
    return localtime_s(result, &timestamp) == 0;
#else
    return localtime_r(&timestamp, result) != nullptr;
#endif
}

bool platform_replace_file(const char *source, const char *destination) {
#ifdef _WIN32
    return MoveFileExA(
               source,
               destination,
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
           ) != 0;
#else
    return rename(source, destination) == 0;
#endif
}
