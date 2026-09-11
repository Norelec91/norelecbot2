#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
typedef CRITICAL_SECTION NativeMutex;
typedef CONDITION_VARIABLE NativeCondition;
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t NativeMutex;
typedef pthread_cond_t NativeCondition;
#endif

typedef struct {
    PlatformThreadFunction function;
    void *context;
} ThreadStart;

bool platform_mutex_init(PlatformMutex *mutex) {
    if (mutex == NULL) {
        return false;
    }
    mutex->native = NULL;
    NativeMutex *native = malloc(sizeof(*native));
    if (native == NULL) {
        return false;
    }
#ifdef _WIN32
    InitializeCriticalSection(native);
#else
    if (pthread_mutex_init(native, NULL) != 0) {
        free(native);
        return false;
    }
#endif
    mutex->native = native;
    return true;
}

void platform_mutex_destroy(PlatformMutex *mutex) {
    if (mutex == NULL || mutex->native == NULL) {
        return;
    }
    NativeMutex *native = mutex->native;
#ifdef _WIN32
    DeleteCriticalSection(native);
#else
    (void)pthread_mutex_destroy(native);
#endif
    free(native);
    mutex->native = NULL;
}

bool platform_mutex_lock(PlatformMutex *mutex) {
    if (mutex == NULL || mutex->native == NULL) {
        return false;
    }
    NativeMutex *native = mutex->native;
#ifdef _WIN32
    EnterCriticalSection(native);
    return true;
#else
    return pthread_mutex_lock(native) == 0;
#endif
}

bool platform_mutex_unlock(PlatformMutex *mutex) {
    if (mutex == NULL || mutex->native == NULL) {
        return false;
    }
    NativeMutex *native = mutex->native;
#ifdef _WIN32
    LeaveCriticalSection(native);
    return true;
#else
    return pthread_mutex_unlock(native) == 0;
#endif
}

bool platform_condition_init(PlatformCondition *condition) {
    if (condition == NULL) {
        return false;
    }
    condition->native = NULL;
    NativeCondition *native = malloc(sizeof(*native));
    if (native == NULL) {
        return false;
    }
#ifdef _WIN32
    InitializeConditionVariable(native);
#else
    if (pthread_cond_init(native, NULL) != 0) {
        free(native);
        return false;
    }
#endif
    condition->native = native;
    return true;
}

void platform_condition_destroy(PlatformCondition *condition) {
    if (condition == NULL || condition->native == NULL) {
        return;
    }
    NativeCondition *native = condition->native;
#ifndef _WIN32
    (void)pthread_cond_destroy(native);
#endif
    free(native);
    condition->native = NULL;
}

bool platform_condition_wait(PlatformCondition *condition, PlatformMutex *mutex) {
    if (condition == NULL || condition->native == NULL || mutex == NULL ||
        mutex->native == NULL) {
        return false;
    }
    NativeCondition *native_condition = condition->native;
    NativeMutex *native_mutex = mutex->native;
#ifdef _WIN32
    return SleepConditionVariableCS(native_condition, native_mutex, INFINITE) != 0;
#else
    return pthread_cond_wait(native_condition, native_mutex) == 0;
#endif
}

bool platform_condition_broadcast(PlatformCondition *condition) {
    if (condition == NULL || condition->native == NULL) {
        return false;
    }
    NativeCondition *native = condition->native;
#ifdef _WIN32
    WakeAllConditionVariable(native);
    return true;
#else
    return pthread_cond_broadcast(native) == 0;
#endif
}

#ifdef _WIN32
static unsigned __stdcall run_thread(void *argument) {
    ThreadStart *start = argument;
    PlatformThreadFunction function = start->function;
    void *context = start->context;
    free(start);
    return (unsigned)function(context);
}
#else
static void *run_thread(void *argument) {
    ThreadStart *start = argument;
    PlatformThreadFunction function = start->function;
    void *context = start->context;
    free(start);
    (void)function(context);
    return NULL;
}
#endif

bool platform_thread_start_detached(PlatformThreadFunction function, void *context) {
    if (function == NULL) {
        return false;
    }
    ThreadStart *start = malloc(sizeof(*start));
    if (start == NULL) {
        return false;
    }
    *start = (ThreadStart){.function = function, .context = context};
#ifdef _WIN32
    uintptr_t thread = _beginthreadex(NULL, 0U, run_thread, start, 0U, NULL);
    if (thread == 0U) {
        free(start);
        return false;
    }
    (void)CloseHandle((HANDLE)thread);
#else
    pthread_t thread;
    if (pthread_create(&thread, NULL, run_thread, start) != 0) {
        free(start);
        return false;
    }
    (void)pthread_detach(thread);
#endif
    return true;
}

void platform_sleep_milliseconds(unsigned long milliseconds) {
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec remaining = {
        .tv_sec = (time_t)(milliseconds / 1000UL),
        .tv_nsec = (long)(milliseconds % 1000UL) * 1000000L,
    };
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
#endif
}

bool platform_local_time(time_t timestamp, struct tm *result) {
    if (result == NULL) {
        return false;
    }
#ifdef _WIN32
    return localtime_s(result, &timestamp) == 0;
#else
    return localtime_r(&timestamp, result) != NULL;
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
