#ifdef NDEBUG
#undef NDEBUG
#endif

#include "config.h"

#include <assert.h>
#include <stdio.h>

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    int written = fputs(content, file);
    int closed = fclose(file);
    assert(written >= 0 && closed == 0);
}

int main(void) {
    AppConfig config = {};
    assert(!config_load(&config, "missing-required-config-test.env"));

    const char *path = "config-quote-cost-test.env";
    write_file(path, "NORELECBOT_QUOTE_COST=250\n");
    assert(config_load(&config, path));
    assert(config.quote_cost == 250);

    write_file(path, "NORELECBOT_QUOTE_COST=0\n");
    assert(config_load(&config, path));
    assert(config.quote_cost == 0);

    write_file(path, "NORELECBOT_OWNER_ID=\n");
    assert(config_load(&config, path));
    assert(config.quote_cost == 1000);
    assert(config.conquister_chat_id == 0);

    write_file(path, "NORELECBOT_CONQUISTER_CHAT_ID=-1001234567890\n");
    assert(config_load(&config, path));
    assert(config.conquister_chat_id == INT64_C(-1001234567890));
    write_file(path, "NORELECBOT_CONQUISTER_CHAT_ID=gruppo\n");
    assert(!config_load(&config, path));

    write_file(path, "NORELECBOT_QUOTE_COST=-1\n");
    assert(!config_load(&config, path));
    write_file(path, "NORELECBOT_QUOTE_COST=tante\n");
    assert(!config_load(&config, path));
    write_file(path, "NORELECBOT_QUOTE_COST=2147483648\n");
    assert(!config_load(&config, path));

    int removed = remove(path);
    assert(removed == 0);
    puts("config tests: ok");
    return 0;
}
