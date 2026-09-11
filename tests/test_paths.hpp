#ifndef NORELECBOT_TEST_PATHS_HPP
#define NORELECBOT_TEST_PATHS_HPP

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

/* Unique JSON file names for one test, removed together with their .tmp files on construction and destruction. */
struct TestPaths {
    std::string conquister;
    std::string quotes;

    explicit TestPaths(std::string_view suite) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        conquister = std::format("{}-{}-conquister.json", suite, stamp);
        quotes = std::format("{}-{}-quotes.json", suite, stamp);
        remove_files();
    }

    ~TestPaths() {
        remove_files();
    }

    TestPaths(const TestPaths &) = delete;
    TestPaths &operator=(const TestPaths &) = delete;
    TestPaths(TestPaths &&) = delete;
    TestPaths &operator=(TestPaths &&) = delete;

    void remove_files() const {
        std::error_code ignored;
        for (const std::string &path : {conquister, quotes, conquister + ".tmp", quotes + ".tmp"}) {
            std::filesystem::remove(path, ignored);
        }
    }
};

#endif
