#ifndef NORELECBOT_TEST_PATHS_H
#define NORELECBOT_TEST_PATHS_H

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <time.h>

static void test_paths(
    const char *suite,
    char conquister_path[1024],
    char quotes_path[1024]
) {
    struct timespec now = {0};
    assert(timespec_get(&now, TIME_UTC) == TIME_UTC);
    int conquister_length = snprintf(
        conquister_path,
        1024U,
        "%s-%lld-%ld-conquister.json",
        suite,
        (long long)now.tv_sec,
        now.tv_nsec
    );
    int quotes_length = snprintf(
        quotes_path,
        1024U,
        "%s-%lld-%ld-quotes.json",
        suite,
        (long long)now.tv_sec,
        now.tv_nsec
    );
    assert(conquister_length >= 0 && conquister_length < 1024);
    assert(quotes_length >= 0 && quotes_length < 1024);
    (void)remove(conquister_path);
    (void)remove(quotes_path);
}

static void test_paths_remove(const char *conquister_path, const char *quotes_path) {
    char temporary[1032];
    (void)remove(conquister_path);
    (void)remove(quotes_path);
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", conquister_path) > 0) {
        (void)remove(temporary);
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", quotes_path) > 0) {
        (void)remove(temporary);
    }
}

#endif
