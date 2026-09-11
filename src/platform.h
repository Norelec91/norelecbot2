#ifndef NORELECBOT_PLATFORM_H
#define NORELECBOT_PLATFORM_H

#include <stdbool.h>
#include <time.h>

void platform_sleep_milliseconds(unsigned long milliseconds);
[[nodiscard]] bool platform_local_time(time_t timestamp, struct tm *result);
[[nodiscard]] bool platform_replace_file(const char *source, const char *destination);

#endif
