#ifdef NDEBUG
#undef NDEBUG
#endif

#include "config.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>

namespace {

void write_file(const std::string &path, std::string_view content) {
    std::ofstream file{path, std::ios::binary};
    file << content;
    assert(file.good());
}

}

int main() {
    using norelecbot::load_config;

    assert(!load_config("missing-required-config-test.env"));

    const std::string path = "config-quote-cost-test.env";
    write_file(path, "NORELECBOT_QUOTE_COST=250\n");
    auto config = load_config(path);
    assert(config && config->quote_cost == 250);

    write_file(path, "NORELECBOT_QUOTE_COST=0\n");
    config = load_config(path);
    assert(config && config->quote_cost == 0);

    write_file(path, "NORELECBOT_OWNER_ID=\n");
    config = load_config(path);
    assert(config && config->quote_cost == 1000);
    assert(config->conquister_chat_id == 0);

    write_file(path, "NORELECBOT_CONQUISTER_CHAT_ID=-1001234567890\n");
    config = load_config(path);
    assert(config && config->conquister_chat_id == -1001234567890);
    write_file(path, "NORELECBOT_CONQUISTER_CHAT_ID=gruppo\n");
    assert(!load_config(path));

    write_file(path, "NORELECBOT_QUOTE_COST=-1\n");
    assert(!load_config(path));
    write_file(path, "NORELECBOT_QUOTE_COST=tante\n");
    assert(!load_config(path));
    write_file(path, "NORELECBOT_QUOTE_COST=2147483648\n");
    assert(!load_config(path));

    write_file(path, "# commento\r\n\r\nNORELECBOT_API_PORT = \"9000\"\r\nNORELECBOT_QUOTE_COST=5");
    config = load_config(path);
    assert(config && config->api_port == 9000 && config->quote_cost == 5);

    assert(std::filesystem::remove(path));
    std::println("config tests: ok");
    return 0;
}
