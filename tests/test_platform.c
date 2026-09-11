#include "platform.h"
#include "test_paths.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    PlatformMutex mutex;
    PlatformCondition condition;
    bool finished;
    int value;
} ThreadState;

static int worker(void *context) {
    ThreadState *state = context;
    assert(platform_mutex_lock(&state->mutex));
    state->value = 42;
    state->finished = true;
    assert(platform_condition_broadcast(&state->condition));
    assert(platform_mutex_unlock(&state->mutex));
    return 0;
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    int written = fputs(text, file);
    int closed = fclose(file);
    assert(written != EOF && closed == 0);
}

int main(void) {
    ThreadState state = {0};
    assert(platform_mutex_init(&state.mutex));
    assert(platform_condition_init(&state.condition));
    assert(platform_mutex_lock(&state.mutex));
    bool started = platform_thread_start_detached(worker, &state);
    assert(started);
    while (!state.finished) {
        assert(platform_condition_wait(&state.condition, &state.mutex));
    }
    assert(platform_mutex_unlock(&state.mutex));
    assert(state.value == 42);
    platform_condition_destroy(&state.condition);
    platform_mutex_destroy(&state.mutex);

    char source[1024];
    char destination[1024];
    test_paths("platform-test", source, destination);
    write_text(source, "new");
    write_text(destination, "old");
    assert(platform_replace_file(source, destination));

    char contents[4] = {0};
    FILE *file = fopen(destination, "rb");
    assert(file != NULL);
    size_t read = fread(contents, 1U, 3U, file);
    int closed = fclose(file);
    assert(read == 3U && closed == 0);
    assert(strcmp(contents, "new") == 0);
    test_paths_remove(source, destination);

    struct tm local;
    bool converted = platform_local_time(time(NULL), &local);
    assert(converted);
    platform_sleep_milliseconds(1UL);
    puts("platform tests: ok");
    return 0;
}
