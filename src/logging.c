#include "logging.h"

#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static void log_line(const char *level, const char *format, va_list arguments) {
    time_t now = time(nullptr);
    char timestamp[32] = "unknown-time";
    struct tm broken_down;
    if (platform_local_time(now, &broken_down)) {
        (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &broken_down);
    }
    (void)fprintf(stderr, "%s %-7s ", timestamp, level);
    (void)vfprintf(stderr, format, arguments);
    (void)fputc('\n', stderr);
}

void log_info(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("INFO", format, arguments);
    va_end(arguments);
}

void log_warning(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("WARNING", format, arguments);
    va_end(arguments);
}

void log_error(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    log_line("ERROR", format, arguments);
    va_end(arguments);
}
