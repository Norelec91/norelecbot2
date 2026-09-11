#include "platform.h"
#include "test_paths.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    int written = fputs(text, file);
    int closed = fclose(file);
    assert(written != EOF && closed == 0);
}

int main(void) {
    char source[1024];
    char destination[1024];
    test_paths("platform-test", source, destination);
    write_text(source, "new");
    write_text(destination, "old");
    assert(platform_replace_file(source, destination));

    char contents[4] = {};
    FILE *file = fopen(destination, "rb");
    assert(file != nullptr);
    size_t read = fread(contents, 1U, 3U, file);
    int closed = fclose(file);
    assert(read == 3U && closed == 0);
    assert(strcmp(contents, "new") == 0);
    test_paths_remove(source, destination);

    struct tm local;
    bool converted = platform_local_time(time(nullptr), &local);
    assert(converted);
    platform_sleep_milliseconds(1UL);
    puts("platform tests: ok");
    return 0;
}
