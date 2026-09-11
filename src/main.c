#include "config.h"
#include "http_server.h"
#include "logging.h"
#include "platform.h"
#include "storage.h"
#include "telegram.h"

#include <curl/curl.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

int main(void) {
    AppConfig config = {0};
    if (!config_load(&config, ".env")) {
        return EXIT_FAILURE;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        log_error("Could not initialize libcurl");
        return EXIT_FAILURE;
    }
    Storage storage = {0};
    if (!storage_open(&storage, config.conquister_path, config.quotes_path)) {
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    (void)signal(SIGINT, request_stop);
    (void)signal(SIGTERM, request_stop);

    HttpServer http = {0};
    if (!http_server_start(
            &http,
            config.api_host,
            config.api_port,
            &storage
        )) {
        storage_close(&storage);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    if (config.conquister_enabled) {
        if (config.bot_token[0] == '\0') {
            log_error(
                "NORELECBOT_CONQUISTER_ENABLED is set but "
                "NORELECBOT_TELEGRAM_TOKEN is missing"
            );
            result = EXIT_FAILURE;
        } else if (telegram_run(&storage, &config, &stop_requested) != 0) {
            result = EXIT_FAILURE;
        }
    } else {
        log_info("Conquister disabled");
        while (stop_requested == 0) {
            platform_sleep_milliseconds(1000UL);
        }
    }

    stop_requested = 1;
    http_server_stop(&http);
    telegram_wait_for_sends();
    storage_close(&storage);
    curl_global_cleanup();
    log_info("NorelecBot stopped");
    return result;
}
