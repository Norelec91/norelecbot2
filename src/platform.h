#ifndef NORELECBOT_PLATFORM_H
#define NORELECBOT_PLATFORM_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    void *native;
} PlatformMutex;

typedef struct {
    void *native;
} PlatformCondition;

typedef int (*PlatformThreadFunction)(void *context);

bool platform_mutex_init(PlatformMutex *mutex);
void platform_mutex_destroy(PlatformMutex *mutex);
bool platform_mutex_lock(PlatformMutex *mutex);
bool platform_mutex_unlock(PlatformMutex *mutex);

bool platform_condition_init(PlatformCondition *condition);
void platform_condition_destroy(PlatformCondition *condition);
bool platform_condition_wait(PlatformCondition *condition, PlatformMutex *mutex);
bool platform_condition_broadcast(PlatformCondition *condition);

bool platform_thread_start_detached(PlatformThreadFunction function, void *context);
void platform_sleep_milliseconds(unsigned long milliseconds);
bool platform_local_time(time_t timestamp, struct tm *result);
bool platform_replace_file(const char *source, const char *destination);

#endif
