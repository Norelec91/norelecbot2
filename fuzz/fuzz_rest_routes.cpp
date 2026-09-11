#include "json.hpp"
#include "rest_routes.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <print>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

namespace {

[[noreturn]] void fail(std::string_view reason, std::string_view method, std::string_view path, std::string_view body) {
    std::print(stderr, "REST invariant violated: {} ({} {}) -> {}\n", reason, method, path, body);
    std::abort();
}

norelecbot::Storage &fuzz_storage() {
    static norelecbot::Storage storage = [] {
        const std::string conquister_path = "fuzz-conquister.json";
        const std::string quotes_path = "fuzz-quotes.json";
        const norelecbot::Json state{
            {"current", {{"user_id", 1}, {"username", "Norelec"}, {"since", 100}}},
            {"scores", {{"Lord_Possum", 7074023}, {"Norelec", 206989}, {"citt\xC3\xA0", 5}}},
            {"quotes_added", {{"Norelec", 1}}},
        };
        const norelecbot::Json quotes = norelecbot::Json::array({"Pillola azzurra", "con \"virgolette\" / e slash"});
        std::ofstream{conquister_path, std::ios::binary} << state.dump();
        std::ofstream{quotes_path, std::ios::binary} << quotes.dump();
        return norelecbot::Storage{conquister_path, quotes_path};
    }();
    return storage;
}

}

/* First byte picks the method, the rest is the URL path. */
int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    static constexpr std::array<std::string_view, 4> methods{"GET", "POST", "HEAD", "PUT"};
    const std::string_view method = methods.at(data[0] % methods.size());
    const std::string bytes(reinterpret_cast<const char *>(data + 1), size - 1);
    // HTTP servers hand over the URL path as a C string, so it ends at the first NUL byte.
    const std::string_view path = std::string_view{bytes}.substr(0, bytes.find('\0'));

    norelecbot::RestResponse response;
    try {
        response = norelecbot::rest_route_dispatch(fuzz_storage(), method, path);
    } catch (const std::exception &error) {
        fail("dispatch failed", method, path, error.what());
    }
    if (response.status_code != 200 && response.status_code != 404) {
        fail("unexpected status", method, path, response.body);
    }
    if (!response.body.ends_with('\n')) {
        fail("body does not end with a newline", method, path, response.body);
    }
    if (!norelecbot::Json::accept(response.body)) {
        fail("body is not valid JSON", method, path, response.body);
    }
    return 0;
}
