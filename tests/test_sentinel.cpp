#include "test_paths.hpp"

#include "sentinel.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace norelecbot;

namespace {

std::string same(const std::string &key) {
    return key;
}

/* Somebody who plays in sittings of an hour, waits as a person does and talks now and then. */
void play_like_a_person(Sentinel &watching, const std::string &name, std::int64_t from,
                        std::string host = {}) {
    if (host.empty()) {
        host = name + ".example";
    }
    const std::int64_t waits[] = {14, 95, 40, 600, 22, 180, 9, 1300, 61, 33, 410, 75};
    std::int64_t now = from;
    for (int sitting = 0; sitting < 6; ++sitting) {
        for (int ride = 0; ride < 8; ++ride) {
            watching.ride(name, "target", false, now);
            watching.heard("irc:" + name, host, true, now);
            now += 100;
            watching.back(name, now);
            now += waits[(sitting * 8 + ride) % 12];
            if (ride % 3 == 0) {
                watching.heard("irc:" + name, host, false, now);
            }
        }
        now += 3 * 60 * 60;
    }
    watching.link("irc:" + name, name);
}

}

TEST_CASE("an account that sets off the instant it is home, day and night, is told as a script") {
    Sentinel watching;
    std::int64_t now = 1'000'000;
    for (int ride = 0; ride < 300; ++ride) {
        watching.ride("bot", "victim", false, now);
        watching.heard("irc:bot", "cloud.example", true, now);
        now += 299;
        watching.back("bot", now);
        now += 1;
    }
    watching.link("irc:bot", "bot");
    play_like_a_person(watching, "anna", 1'000'000);
    const std::string report = watching.report(now, same);
    CHECK(report.contains("🤖 Script quasi certi\n• bot"));
    CHECK(report.contains("riparte entro 3 secondi dal rientro nel 100%"));
    CHECK(report.contains("senza una pausa di 15 minuti"));
    CHECK(report.contains("nessun messaggio in chat"));
    CHECK(report.contains("✅ Nella norma: anna"));
}

TEST_CASE("waiting always the same few minutes gives a script away too") {
    Sentinel watching;
    std::int64_t now = 1'000'000;
    for (int ride = 0; ride < 100; ++ride) {
        watching.ride("clock", "victim", false, now);
        now += 382;
        watching.back("clock", now);
        now += 338 + (ride % 3) * 4;
    }
    const std::string report = watching.report(now, same);
    CHECK(report.contains("aspetta sempre lo stesso tempo prima di ripartire"));
    CHECK(report.contains("• clock"));
}

TEST_CASE("one feeding another from the same machine is told as the same person") {
    Sentinel watching;
    play_like_a_person(watching, "kio", 1'000'000, "home.example");
    play_like_a_person(watching, "kioris", 1'000'000 + 50 * 60 * 60, "home.example");
    for (int gift = 0; gift < 3; ++gift) {
        watching.ride("kioris", "kio", true, 1'500'000 + gift * 1000);
    }
    play_like_a_person(watching, "lucy", 1'000'000 + 24 * 60 * 60);
    const std::string report = watching.report(1'600'000, same);
    CHECK(report.contains("👥 Probabilmente la stessa persona"));
    CHECK(report.contains("regali solo da kioris a kio (3)"));
    CHECK(report.contains("stesso host IRC"));
    CHECK(report.contains("nomi simili"));
    CHECK_FALSE(report.contains("lucy e"));
}

TEST_CASE("what the sentinel saw survives a restart, and a week later it is forgotten") {
    const TestPaths paths{"sentinel-test"};
    Sentinel watching;
    std::int64_t now = 1'000'000;
    for (int ride = 0; ride < 300; ++ride) {
        watching.ride("bot", "victim", false, now);
        now += 299;
        watching.back("bot", now);
        now += 1;
    }
    watching.set_reported_day(42);
    watching.save(paths.conquister);
    Sentinel again;
    again.load(paths.conquister);
    CHECK(again.reported_day() == 42);
    CHECK(again.report(now, same) == watching.report(now, same));
    CHECK(again.report(now + 8 * 24 * 60 * 60, same).contains("Ancora pochi dati"));
}

TEST_CASE("only after a whole day of watching are the sure scripts named for blocking") {
    Sentinel watching;
    std::int64_t now = 1'000'000;
    for (int ride = 0; ride < 300; ++ride) {
        watching.ride("bot", "victim", false, now);
        now += 299;
        watching.back("bot", now);
        now += 1;
        if (now - 1'000'000 < 24 * 60 * 60) {
            CHECK(watching.scripts(now).empty());
        }
    }
    play_like_a_person(watching, "anna", 1'000'000);
    CHECK(watching.scripts(now) == std::vector<std::string>{"bot"});
}

TEST_CASE("a blocked account stays out until its time or an owner lets it back, and is told once an hour") {
    const TestPaths paths{"sentinel-block-test"};
    Sentinel watching;
    watching.block("bot", 5000);
    CHECK(watching.blocked_until("bot", 1000) == 5000);
    CHECK(watching.blocked_until("bot", 5000) == 0);
    CHECK(watching.blocked_until("anna", 1000) == 0);
    CHECK(watching.tell_blocked("bot", 1000));
    CHECK_FALSE(watching.tell_blocked("bot", 1000 + 3599));
    CHECK(watching.tell_blocked("bot", 1000 + 3600));
    CHECK(watching.report(1000, same).contains("🚫 Bloccati"));
    watching.save(paths.conquister);
    Sentinel again;
    again.load(paths.conquister);
    CHECK(again.blocked_until("bot", 1000) == 5000);
    CHECK(again.unblock("bot"));
    CHECK_FALSE(again.unblock("bot"));
    CHECK(again.blocked_until("bot", 1000) == 0);
}
