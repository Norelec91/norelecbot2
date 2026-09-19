#include "test_paths.hpp"

#include "commands.hpp"
#include "game.hpp"
#include "storage.hpp"
#include "mishaps.hpp"
#include "position.hpp"
#include "zodiac.hpp"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <set>

using namespace norelecbot;

namespace {

/* Seconds held, times the boost, times what the house of the day was worth to the holder. */
std::int64_t earnings(std::string_view holder, std::int64_t seconds, std::int64_t now, std::int64_t boost = 1) {
    return seconds * boost * zodiac::percent_for(holder, now) / 100;
}

Json read_json(const std::string &path) {
    std::ifstream file{path, std::ios::binary};
    return Json::parse(file);
}

}

TEST_CASE("claims, leaderboard, users and quotes") {
    const TestPaths paths{"telegram-service-test"};
    {
        Storage storage{paths.conquister, paths.quotes};
        CHECK(quote_page_load(storage, 1).total == 0);
        CHECK_FALSE(quote_random(storage));

        ClaimResult claim = conquister_claim(storage, 1, "alice", 100);
        CHECK(claim.status == ClaimStatus::taken);
        CHECK(claim.previous_username.empty());
        claim = conquister_claim(storage, 2, "bob", 1100);
        CHECK(claim.previous_username == "alice");
        CHECK(claim.earned == earnings("alice", 1000, 1100));

        const Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        REQUIRE(leaderboard.entries.size() == 1);
        CHECK(leaderboard.entries[0].username == "alice");
        CHECK(leaderboard.entries[0].score == earnings("alice", 1000, 1100));
        REQUIRE(leaderboard.current);
        CHECK(leaderboard.current->username == "bob");
        CHECK(leaderboard.current->since == 1100);

        std::optional<ConquisterUser> user = conquister_user(storage, "ALICE");
        REQUIRE(user);
        CHECK(user->username == "alice");
        CHECK(user->score == earnings("alice", 1000, 1100));
        CHECK(user->rank == 1);
        CHECK(user->quotes_added == 0);
        CHECK_FALSE(user->in_conquister);
        user = conquister_user(storage, "bob");
        REQUIRE(user);
        CHECK(user->in_conquister);
        CHECK(user->since == 1100);
        CHECK(user->score == 0);
        CHECK(user->rank == 0);
        CHECK_FALSE(conquister_user(storage, "carol"));

        const std::int64_t alice_score = earnings("alice", 1000, 1100);
        QuoteAddResult addition = quote_add(storage, "alice", "quote di prova", 1000, 0);
        CHECK(addition.status == QuoteAddStatus::added);
        CHECK(addition.available_score == alice_score - 1000);
        CHECK(quote_random(storage) == "quote di prova");

        static_cast<void>(conquister_claim(storage, 1, "alice", 1101));
        static_cast<void>(conquister_claim(storage, 2, "bob", 2101));
        addition = quote_add(storage, "alice", "quote di prova", 1000, 0);
        CHECK(addition.status == QuoteAddStatus::duplicate);
        const QuotePage page = quote_page_load(storage, 1);
        REQUIRE(page.items.size() == 1);
        CHECK(page.items[0] == "quote di prova");

        CHECK(quote_delete(storage, "1") == "quote di prova");
        CHECK_FALSE(quote_delete(storage, "1"));
        CHECK(quote_page_load(storage, 1).total == 0);
    }

    SUBCASE("the saved files keep their shape and reload") {
        const Json state = read_json(paths.conquister);
        CHECK(state.is_object());
        CHECK(state.at("current").is_object());
        CHECK(state.at("scores").is_object());
        CHECK(state.at("quotes_added").is_object());
        CHECK(read_json(paths.quotes).is_array());

        Storage storage{paths.conquister, paths.quotes};
        Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        REQUIRE_FALSE(leaderboard.entries.empty());
        CHECK(leaderboard.entries[0].username == "alice");
        CHECK(conquister_leaderboard(storage, 1).entries.size() == 1);
        leaderboard = conquister_leaderboard(storage, 0);
        REQUIRE(leaderboard.entries.size() == 2);
        CHECK(leaderboard.entries[1].username == "bob");
    }
}

TEST_CASE("a balloon defends the holder until it pops") {
    const TestPaths paths{"balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    CHECK(balloon_buy(storage, "alice", 0, 0, 0).status == BalloonStatus::already_owned);
    CHECK(balloon_buy(storage, "carol", 1000, 0, 0).status == BalloonStatus::insufficient_score);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000));

    int attempts = 0;
    ClaimResult attack;
    do {
        attack = conquister_claim(storage, 2, "bob", 3000 + attempts);
        ++attempts;
        if (attack.status == ClaimStatus::defended) {
            CHECK(attack.previous_username == "alice");
            CHECK(attack.next_chance == 25 * (attempts + 1));
            const auto holder = conquister_user(storage, "alice");
            REQUIRE(holder);
            CHECK(holder->in_conquister);
        }
    } while (attack.status == ClaimStatus::defended && attempts < 8);

    CHECK(attempts <= 4);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
    CHECK(attack.previous_username == "alice");

    const ClaimResult without = conquister_claim(storage, 1, "alice", 9000);
    CHECK(without.status == ClaimStatus::taken);
    CHECK_FALSE(without.balloon_popped);
}

TEST_CASE("a penalty blocks the next attempts") {
    const TestPaths paths{"cooldown-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},)"
             << R"("quotes_added":{},"balloons":{},"cooldowns":{"bob":1000}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult blocked = conquister_claim(storage, 2, "bob", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(blocked.status == ClaimStatus::cooldown);
    CHECK(blocked.penalty_seconds == 300);

    const ClaimResult others = conquister_claim(storage, 3, "carol", 700, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(others.status == ClaimStatus::taken);

    const ClaimResult expired = conquister_claim(storage, 2, "bob", 1000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(expired.status == ClaimStatus::taken);
}

TEST_CASE("a failed balloon attempt hands out the penalty") {
    const TestPaths paths{"cooldown-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    CHECK(balloon_buy(storage, "alice", 1000, 0, 0).status == BalloonStatus::bought);
    static_cast<void>(conquister_claim(storage, 1, "alice", 2000));

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 3000, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.penalty_seconds == 300);
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
        CHECK(again.status == ClaimStatus::cooldown);
        CHECK(again.penalty_seconds == 200);
    } else {
        CHECK(attack.status == ClaimStatus::taken);
        CHECK(attack.balloon_popped);
    }
}

TEST_CASE("the fourth attempt pops the balloon for certain") {
    const TestPaths paths{"balloon-certain-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{},)"
             << R"("quotes_added":{},"balloons":{"alice":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult attack = conquister_claim(storage, 2, "bob", 10);
    CHECK(attack.status == ClaimStatus::taken);
    CHECK(attack.balloon_popped);
    const auto winner = conquister_user(storage, "bob");
    REQUIRE(winner);
    CHECK(winner->in_conquister);
}

TEST_CASE("a quote saved before a failed state save is rolled back") {
    const TestPaths paths{"rollback-test"};
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << "[\"originale\"]";
    }
    Storage storage{"rollback-missing-directory/conquister.json", paths.quotes};
    CHECK_THROWS_AS(static_cast<void>(quote_add(storage, "alice", "nuova", 0, 0)), const StorageError &);

    const QuotePage page = quote_page_load(storage, 1);
    CHECK(page.total == 1);
    REQUIRE(page.items.size() == 1);
    CHECK(page.items[0] == "originale");
}

TEST_CASE("one player's balloon cannot be popped until it deflates") {
    const TestPaths paths{"shield-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 5000));
    const BalloonResult bought = balloon_buy(storage, "alice", 1000, 5000, 3600);
    CHECK(bought.status == BalloonStatus::bought);
    CHECK(bought.shield_seconds == 3600);
    const BalloonResult again = balloon_buy(storage, "alice", 0, 5100, 3600);
    CHECK(again.status == BalloonStatus::already_owned);
    CHECK(again.shield_seconds == 3500);
    static_cast<void>(conquister_claim(storage, 1, "alice", 5200));

    const ClaimResult first = conquister_claim(storage, 2, "bob", 5300, ClaimRules{.cooldown_seconds = 300, .attack_cost = 0, .ignores_shield = false, .signs = {}});
    CHECK(first.status == ClaimStatus::defended);
    CHECK(first.previous_username == "alice");
    CHECK(first.shield_seconds == 3300);
    CHECK(first.penalty_seconds == 300);
    CHECK(first.next_chance == 0);

    /* Far more attempts than the four an ordinary balloon survives. */
    for (int attempt = 0; attempt < 20; ++attempt) {
        const ClaimResult attack = conquister_claim(storage, 2, "bob", 6000 + attempt);
        CHECK(attack.status == ClaimStatus::defended);
        CHECK(attack.shield_seconds > 0);
    }

    const ClaimResult deflated = conquister_claim(storage, 3, "carol", 8700);
    CHECK(deflated.status == ClaimStatus::taken);
    CHECK(deflated.previous_username == "alice");
    CHECK_FALSE(deflated.balloon_popped);

    /* Deflated, so the next one can be bought. */
    CHECK(balloon_buy(storage, "alice", 0, 8700, 3600).status == BalloonStatus::bought);
}

TEST_CASE("two shielded players can pop each other's balloon") {
    const TestPaths paths{"shield-duel-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0, ClaimRules{.cooldown_seconds = 0, .attack_cost = 0, .ignores_shield = true, .signs = {}}));
    CHECK(balloon_buy(storage, "alice", 0, 0, 3600).status == BalloonStatus::bought);

    /* carol is an ordinary player: she cannot. */
    const ClaimResult refused = conquister_claim(storage, 3, "carol", 100);
    CHECK(refused.status == ClaimStatus::defended);
    CHECK(refused.shield_seconds == 3500);

    const ClaimResult popped = conquister_claim(storage, 2, "bob", 200, ClaimRules{.cooldown_seconds = 0, .attack_cost = 0, .ignores_shield = true, .signs = {}});
    CHECK(popped.status == ClaimStatus::taken);
    CHECK(popped.balloon_popped);
    CHECK(popped.previous_username == "alice");

    /* Popped for good: alice has to buy another one. */
    CHECK(balloon_buy(storage, "alice", 0, 300, 3600).status == BalloonStatus::bought);
}

TEST_CASE("a boost multiplies what the hold earns, once") {
    const TestPaths paths{"boost-test"};
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(conquister_claim(storage, 1, "alice", 0));
    static_cast<void>(conquister_claim(storage, 2, "bob", 1000));
    const std::int64_t first_hold = earnings("alice", 1000, 1000);
    CHECK(conquister_user(storage, "alice")->score == first_hold);

    const BoostResult bought = boost_buy(storage, "alice", 1000, 3, 1000);
    CHECK(bought.status == BoostStatus::bought);
    CHECK(bought.available_score == first_hold - 1000);
    CHECK(boost_buy(storage, "alice", 0, 3, 1000).status == BoostStatus::already_owned);

    SUBCASE("it is cashed in when the place is taken away") {
        static_cast<void>(conquister_claim(storage, 1, "alice", 2000));
        const ClaimResult kicked = conquister_claim(storage, 2, "bob", 2100);
        CHECK(kicked.previous_username == "alice");
        CHECK(kicked.boost_multiplier == 3);
        CHECK(kicked.earned == earnings("alice", 100, 2100, 3));
        CHECK(conquister_user(storage, "alice")->score == first_hold - 1000 + kicked.earned);

        /* Spent: the next hold earns the usual. */
        static_cast<void>(conquister_claim(storage, 1, "alice", 3000));
        const ClaimResult again = conquister_claim(storage, 2, "bob", 3100);
        CHECK(again.boost_multiplier == 0);
        CHECK(again.earned == earnings("alice", 100, 3100));
    }

    SUBCASE("it rules out a balloon while it waits") {
        CHECK(balloon_buy(storage, "alice", 0, 2000, 0).status == BalloonStatus::has_boost);
    }
}

TEST_CASE("a balloon rules out a boost") {
    const TestPaths paths{"boost-balloon-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(balloon_buy(storage, "alice", 0, 0, 0).status == BalloonStatus::bought);
    CHECK(boost_buy(storage, "alice", 0, 3, 0).status == BoostStatus::has_balloon);

    CHECK(balloon_buy(storage, "bob", 0, 0, 3600).status == BalloonStatus::bought);
    CHECK(boost_buy(storage, "bob", 0, 3, 100).status == BoostStatus::has_balloon);
    /* Once the hour is over the shield is gone and the boost can be bought. */
    CHECK(boost_buy(storage, "bob", 0, 3, 3700).status == BoostStatus::bought);
}

TEST_CASE("an attempt that a balloon survives costs palle") {
    const TestPaths paths{"attack-cost-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"bob":250},)"
             << R"("quotes_added":{},"balloons":{"alice":0},"cooldowns":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult attack =
        conquister_claim(storage, 2, "bob", 10, ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}});
    if (attack.status == ClaimStatus::defended) {
        CHECK(attack.attack_cost == 100);
        CHECK(conquister_user(storage, "bob")->score == 150);
    } else {
        /* It popped straight away, and a pop costs nothing. */
        CHECK(attack.attack_cost == 0);
        CHECK(conquister_user(storage, "bob")->score == 250);
    }
}

TEST_CASE("a failed attempt is paid even in the red") {
    const TestPaths paths{"attack-cost-empty-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"bob":30},)"
             << R"("quotes_added":{},"balloons":{},"cooldowns":{},"shields":{"alice":9999}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const ClaimResult first = conquister_claim(
        storage,
        2,
        "bob",
        10,
        ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}}
    );
    CHECK(first.status == ClaimStatus::defended);
    CHECK(first.attack_cost == 100);
    CHECK(conquister_user(storage, "bob")->score == -70);

    const ClaimResult second = conquister_claim(
        storage,
        2,
        "bob",
        20,
        ClaimRules{.cooldown_seconds = 0, .attack_cost = 100, .ignores_shield = false, .signs = {}}
    );
    CHECK(second.attack_cost == 100);
    CHECK(conquister_user(storage, "bob")->score == -170);
}

namespace {

/* Far enough apart that every ride is the shortest one, so the tests do not depend on where ids land. */
RaidRules quick_rides() {
    return RaidRules{.travel_divisor = 1000000, .loot_share = 4, .attack_cost = 100, .signs = {}, .shadowed = {}};
}

}

TEST_CASE("a raid takes a quarter of what the target has, and carries it home") {
    const TestPaths paths{"raid-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":40},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const RaidResult start = raid_start(storage, 7, "bob", "ALICE", 0, quick_rides());
    CHECK(start.status == RaidStatus::started);
    CHECK(start.target == "alice");
    CHECK(start.seconds == position::shortest_travel);

    CHECK(raid_start(storage, 7, "bob", "alice", 1, quick_rides()).status == RaidStatus::already_travelling);
    CHECK(raid_start(storage, 0, "carol", "carol", 1, quick_rides()).status == RaidStatus::home_already);
    CHECK(raid_start(storage, 0, "carol", "nessuno", 1, quick_rides()).status == RaidStatus::unknown_target);
    CHECK(raid_due(storage, 4, quick_rides()).empty());

    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::stolen);
    CHECK(arrival[0].raider == "bob");
    CHECK(arrival[0].target == "alice");
    const std::int64_t loot =
        250 * zodiac::percent_for("bob", 5) / zodiac::percent_for("alice", 5);
    CHECK(arrival[0].loot == loot);
    /* alice has never written to the bot from Telegram, so her name carries no mention. */
    CHECK_FALSE(arrival[0].target_on_telegram);
    /* Taken from the target at once, handed over only at the end of the ride. */
    CHECK(conquister_user(storage, "alice")->score == 1000 - loot);
    CHECK(conquister_user(storage, "bob")->score == 40);

    CHECK(raid_due(storage, 9, quick_rides()).empty());
    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    CHECK(home[0].kind == RaidEvent::Kind::returned);
    CHECK(home[0].loot == loot);
    CHECK(conquister_user(storage, "bob")->score == 40 + loot);
    /* Home again, so he can leave again. */
    CHECK(raid_start(storage, 7, "bob", "alice", 11, quick_rides()).status == RaidStatus::started);
}

TEST_CASE("a balloon turns a raid back") {
    const TestPaths paths{"raid-balloon-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":500},"quotes_added":{},)"
             << R"("balloons":{"alice":3}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* The fourth attempt pops it for certain, so this raid gets through. */
    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::stolen);
    CHECK(arrival[0].balloon_popped);
    CHECK(arrival[0].loot > 0);

    SUBCASE("a fresh balloon can send him home empty handed") {
        static_cast<void>(raid_due(storage, 10, quick_rides()));
        CHECK(balloon_buy(storage, "alice", 0, 10, 0).status == BalloonStatus::bought);
        static_cast<void>(raid_start(storage, 0, "bob", "alice", 11, quick_rides()));
        const std::vector<RaidEvent> second = raid_due(storage, 16, quick_rides());
        REQUIRE(second.size() == 1);
        if (second[0].kind == RaidEvent::Kind::defended) {
            CHECK(second[0].cost == 100);
            CHECK(second[0].loot == 0);
            const std::vector<RaidEvent> back = raid_due(storage, 21, quick_rides());
            REQUIRE(back.size() == 1);
            CHECK(back[0].kind == RaidEvent::Kind::returned);
            CHECK(back[0].loot == 0);
        } else {
            CHECK(second[0].balloon_popped);
        }
    }
}

TEST_CASE("an empty house has no defences") {
    const TestPaths paths{"raid-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":0,)"
             << R"("carol":800},"quotes_added":{},"balloons":{"alice":0}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    /* alice leaves to rob carol, so her own balloon guards nothing. */
    static_cast<void>(raid_start(storage, 0, "alice", "carol", 0, quick_rides()));
    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));

    const std::vector<RaidEvent> arrivals = raid_due(storage, 5, quick_rides());
    REQUIRE(arrivals.size() == 2);
    for (const RaidEvent &event : arrivals) {
        CHECK(event.kind == RaidEvent::Kind::stolen);
        if (event.raider == "bob") {
            CHECK(event.undefended);
            CHECK_FALSE(event.balloon_popped);
        }
    }
    /* The balloon is still hers, it simply was not at home either. */
    CHECK(balloon_buy(storage, "alice", 0, 5, 0).status == BalloonStatus::already_owned);
}

TEST_CASE("nobody takes the place from the road") {
    const TestPaths paths{"raid-claim-away-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":100,"carol":10},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(raid_start(storage, 0, "alice", "carol", 0, quick_rides()));
    const ClaimResult refused = conquister_claim(storage, 1, "alice", 1);
    CHECK(refused.status == ClaimStatus::travelling);
    CHECK(refused.travel_seconds == 9);
    CHECK_FALSE(conquister_user(storage, "alice")->in_conquister);

    /* Home again, and the place is his to take. */
    static_cast<void>(raid_due(storage, 10, quick_rides()));
    CHECK(conquister_claim(storage, 1, "alice", 11).status == ClaimStatus::taken);
}

TEST_CASE("an id is drawn once and stays") {
    const TestPaths paths{"raid-id-test"};
    std::int64_t drawn = 0;
    {
        Storage storage{paths.conquister, paths.quotes};
        static_cast<void>(conquister_claim(storage, 1, "alice", 0));
        static_cast<void>(conquister_claim(storage, 2, "bob", 10));
        const RaidResult first = raid_start(storage, 0, "alice", "bob", 20, quick_rides());
        CHECK(first.status == RaidStatus::started);
        drawn = first.seconds;
    }

    const Json state = read_json(paths.conquister);
    CHECK(state.at("telegram_ids").at("alice").get<std::int64_t>() == 1);
    CHECK(state.at("telegram_ids").at("bob").get<std::int64_t>() == 2);
    REQUIRE(state.at("ids").is_object());
    CHECK(state.at("ids").size() == 2);
    const std::int64_t alice = state.at("ids").at("alice").get<std::int64_t>();
    CHECK(alice >= 0);
    CHECK(alice < position::ids);
    CHECK(state.at("ids").at("bob").get<std::int64_t>() != alice);

    Storage reopened{paths.conquister, paths.quotes};
    static_cast<void>(raid_due(reopened, 1000, quick_rides()));
    const RaidResult again = raid_start(reopened, 0, "alice", "bob", 1000, quick_rides());
    CHECK(again.seconds == drawn);
    CHECK(read_json(paths.conquister).at("ids").at("alice").get<std::int64_t>() == alice);
}

TEST_CASE("whoever holds the place does not leave it") {
    const TestPaths paths{"raid-holder-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":{"user_id":1,"username":"alice","since":0},"scores":{"alice":900,"bob":10},)"
             << R"("quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const RaidResult refused = raid_start(storage, 1, "ALICE", "bob", 1, quick_rides());
    CHECK(refused.status == RaidStatus::holding_place);
    /* Naming herself is how she comes back down. */
    CHECK(raid_start(storage, 1, "alice", "alice", 1, quick_rides()).status == RaidStatus::left_place);
    CHECK(conquister_user(storage, "alice")->score >= 900);

    /* Out of the place, free to go. */
    static_cast<void>(conquister_claim(storage, 2, "bob", 2));
    CHECK(raid_start(storage, 1, "alice", "bob", 3, quick_rides()).status == RaidStatus::started);
}

TEST_CASE("naming yourself is the way home") {
    const TestPaths paths{"raid-home-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":700},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    SUBCASE("at home it changes nothing") {
        const RaidResult already = raid_start(storage, 0, "alice", "ALICE", 0, quick_rides());
        CHECK(already.status == RaidStatus::home_already);
        CHECK(conquister_user(storage, "alice")->score == 1000);
    }

    SUBCASE("from the place it is instant, and the hold is cashed in") {
        static_cast<void>(conquister_claim(storage, 1, "alice", 100));
        const RaidResult left = raid_start(storage, 1, "alice", "alice", 400, quick_rides());
        CHECK(left.status == RaidStatus::left_place);
        CHECK(left.earned == 300 * zodiac::percent_for("alice", 400) / 100);
        CHECK(conquister_user(storage, "alice")->score == 1000 + left.earned);
        /* The place is empty now, and she can leave on a raid. */
        CHECK_FALSE(conquister_leaderboard(storage, 10).current);
        CHECK(raid_start(storage, 1, "alice", "bob", 401, quick_rides()).status == RaidStatus::started);
    }

    SUBCASE("on the road it calls the raid off and takes the rest of the ride") {
        static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
        const RaidResult back = raid_start(storage, 0, "bob", "bob", 2, quick_rides());
        CHECK(back.status == RaidStatus::coming_home);
        CHECK(back.seconds == 8);

        /* Nothing is stolen at the hour he would have arrived. */
        CHECK(raid_due(storage, 5, quick_rides()).empty());
        CHECK(conquister_user(storage, "alice")->score == 1000);

        const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
        REQUIRE(home.size() == 1);
        CHECK(home[0].kind == RaidEvent::Kind::returned);
        CHECK(home[0].loot == 0);
        CHECK(conquister_user(storage, "bob")->score == 700);
    }
}

TEST_CASE("a quote remembers who added it") {
    const TestPaths paths{"quote-author-test"};
    Storage storage{paths.conquister, paths.quotes};

    CHECK(quote_add(storage, "alice", "una citazione", 0, 0).status == QuoteAddStatus::added);
    CHECK(quote_add(storage, "bob", "un'altra", 0, 0).status == QuoteAddStatus::added);

    QuotePage page = quote_page_load(storage, 1);
    REQUIRE(page.authors.size() == 2);
    CHECK(page.authors[0] == "alice");
    CHECK(page.authors[1] == "bob");
    CHECK(read_json(paths.conquister).at("quote_authors").at("una citazione") == "alice");

    /* Deleting a quote forgets its author and takes it off his count. */
    CHECK(conquister_user(storage, "alice")->quotes_added == 1);
    CHECK(quote_delete(storage, "1") == "una citazione");
    CHECK(conquister_user(storage, "alice")->quotes_added == 0);
    page = quote_page_load(storage, 1);
    REQUIRE(page.authors.size() == 1);
    CHECK(page.authors[0] == "bob");
    CHECK(read_json(paths.conquister).at("quote_authors").size() == 1);

    /* A quote nobody is known to have added takes nothing off anybody. */
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << R"(["senza autore","un'altra"])";
    }
    {
        Storage orphan{paths.conquister, paths.quotes};
        CHECK(quote_delete(orphan, "1") == "senza autore");
        CHECK(conquister_user(orphan, "bob")->quotes_added == 1);
    }

    /* A quote from before the bot wrote it down has no author, and that is not a hole. */
    {
        std::ofstream file{paths.quotes, std::ios::binary};
        file << R"(["vecchia","un'altra"])";
    }
    Storage reopened{paths.conquister, paths.quotes};
    page = quote_page_load(reopened, 1);
    REQUIRE(page.authors.size() == 2);
    CHECK(page.authors[0].empty());
    CHECK(page.authors[1] == "bob");
}

TEST_CASE("a shadowed raider always comes home empty handed") {
    const TestPaths paths{"shadow-raid-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"giangiui":500},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const std::vector<std::string> shadowed{"Giangiui"};
    RaidRules rules = quick_rides();
    rules.shadowed = shadowed;

    static_cast<void>(raid_start(storage, 0, "giangiui", "alice", 0, rules));
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, rules);
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].kind == RaidEvent::Kind::defended);
    CHECK(arrival[0].loot == 0);
    CHECK(arrival[0].cost == 100);
    /* The target keeps everything, and no balloon of his was spent. */
    CHECK(conquister_user(storage, "alice")->score == 1000);

    const std::vector<RaidEvent> home = raid_due(storage, 10, rules);
    REQUIRE(home.size() == 1);
    CHECK(home[0].loot == 0);
    CHECK(conquister_user(storage, "giangiui")->score == 400);

    /* Everyone else robs him as usual. */
    static_cast<void>(raid_start(storage, 0, "alice", "giangiui", 11, rules));
    const std::vector<RaidEvent> theirs = raid_due(storage, 16, rules);
    REQUIRE(theirs.size() == 1);
    CHECK(theirs[0].kind == RaidEvent::Kind::stolen);
    CHECK(theirs[0].loot > 0);
}

TEST_CASE("there is nothing to steal from somebody in the red") {
    const TestPaths paths{"raid-red-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":-500,"bob":100},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
    const std::vector<RaidEvent> arrival = raid_due(storage, 5, quick_rides());
    REQUIRE(arrival.size() == 1);
    CHECK(arrival[0].loot == 0);
    CHECK(conquister_user(storage, "alice")->score == -500);
    CHECK(conquister_user(storage, "bob")->score == 100);
}

TEST_CASE("robbing people costs the good name it takes to keep") {
    const TestPaths paths{"simpatia-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":100000,"bob":0},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    const auto rob = [&](std::int64_t at) {
        static_cast<void>(raid_start(storage, 0, "bob", "alice", at, quick_rides()));
        const std::vector<RaidEvent> arrival = raid_due(storage, at + 5, quick_rides());
        static_cast<void>(raid_due(storage, at + 10, quick_rides()));
        REQUIRE(arrival.size() == 1);
        return arrival[0];
    };

    /* Everybody starts liked, and nobody is told anything until it slips under eighteen. */
    RaidEvent first = rob(0);
    CHECK(first.simpatia == 19);
    CHECK_FALSE(first.denounced);
    CHECK(rob(20).simpatia == 18);
    const RaidEvent third = rob(40);
    CHECK(third.simpatia == 17);
    CHECK(third.denounced);
    /* Said once, not at every raid from then on. */
    CHECK_FALSE(rob(60).denounced);

    /* The one being robbed gains what the robber loses. */
    const auto victim = player_card(storage, "bob", "alice", 80, 1000000);
    REQUIRE(victim);
    CHECK(victim->simpatia == 20);

    const auto robber = player_card(storage, "alice", "bob", 80, 1000000);
    REQUIRE(robber);
    CHECK(robber->simpatia == 16);

    SUBCASE("a quote gives a point back, and so does a day of quiet") {
        CHECK(quote_add(storage, "bob", "una citazione", 0, 80).status == QuoteAddStatus::added);
        CHECK(player_card(storage, "alice", "bob", 80, 1000000)->simpatia == 17);

        const std::int64_t tomorrow = 80 + 86400;
        CHECK(player_card(storage, "alice", "bob", tomorrow, 1000000)->simpatia == 18);
        const std::int64_t next_week = 80 + (7 * 86400);
        CHECK(player_card(storage, "alice", "bob", next_week, 1000000)->simpatia == 20);
    }
}

TEST_CASE("something small happens to somebody, and it is always small") {
    const TestPaths paths{"mishap-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":1000},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    std::set<std::string> unlucky;
    std::set<std::size_t> seen;
    bool saw_balloon = false;
    bool saw_boost = false;
    for (int strike = 0; strike < 2000; ++strike) {
        const std::optional<MishapResult> mishap = mishap_strike(storage, 1000, 3);
        REQUIRE(mishap);
        unlucky.insert(mishap->player);
        seen.insert(mishap->which);
        CHECK(mishap->which < mishaps.size());
        /* A nuisance, never a blow. */
        CHECK(mishap->palle >= -3);
        CHECK(mishap->palle <= 3);
        CHECK_FALSE(mishap_reply(*mishap).empty());
        CHECK(mishap_reply(*mishap).contains(mishap->player));
        const auto card = player_card(storage, "alice", mishap->player, 1000, 1000000);
        REQUIRE(card);
        saw_balloon = saw_balloon || card->balloon_attempts >= 0;
        saw_boost = saw_boost || card->boost_multiplier > 0;
    }
    CHECK(unlucky.size() == 2);
    CHECK(seen.size() == mishaps.size());

    /* The gifts are handed out for real, even if a later strike takes them away again. */
    CHECK(saw_balloon);
    CHECK(saw_boost);

    /* Two thousand of them and nobody has been ruined: they are nuisances, not blows. */
    CHECK(conquister_user(storage, "alice")->score > -2000);
    CHECK(conquister_user(storage, "bob")->score > -2000);
}

TEST_CASE("the jackpot brings the famous grandfather and his three palle") {
    const Mishap *jackpot = nullptr;
    std::size_t grandfathers = 0;
    for (const Mishap &target : flippers) {
        if (target.boon == Boon::grandfather) {
            jackpot = &target;
            ++grandfathers;
        }
    }

    REQUIRE(grandfathers == 1);
    REQUIRE(jackpot != nullptr);
    CHECK(jackpot->palle == 3);
    CHECK(jackpot->text.contains("JACKPOT"));
    CHECK(jackpot->text.contains("NONNO"));
    CHECK(flipper_score_after(10, *jackpot) == 23);
}

TEST_CASE("nothing happens while nobody is playing") {
    const TestPaths paths{"mishap-empty-test"};
    Storage storage{paths.conquister, paths.quotes};
    CHECK_FALSE(mishap_strike(storage, 1000, 3));
}

TEST_CASE("a raid can be disputed, returned, or settled by the support") {
    const TestPaths paths{"reso-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":0},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
    static_cast<void>(raid_due(storage, 5, quick_rides()));
    const std::vector<RaidEvent> home = raid_due(storage, 10, quick_rides());
    REQUIRE(home.size() == 1);
    const std::int64_t loot = home[0].loot;
    REQUIRE(loot > 0);

    /* Nobody else has anything to report, and the one robbed has seven days. */
    CHECK(dispute_open(storage, "carol", 20).status == DisputeStatus::nothing_to_report);
    const DisputeResult opened = dispute_open(storage, "alice", 20);
    CHECK(opened.status == DisputeStatus::done);
    CHECK(opened.seller == "bob");
    CHECK(opened.palle == loot);
    CHECK(dispute_open(storage, "alice", 21).status == DisputeStatus::already_open);

    const std::int64_t bob_had = conquister_user(storage, "bob")->score;
    const std::int64_t alice_had = conquister_user(storage, "alice")->score;
    const ReturnResult giving = loot_return(storage, "bob", 30, true);
    REQUIRE(giving.status == ReturnStatus::done);
    CHECK(giving.disputed);
    CHECK(giving.victim == "alice");
    if (giving.overturned) {
        /* The support refunded her out of thin air and left him the palle. */
        CHECK(conquister_user(storage, "bob")->score == bob_had);
        CHECK(conquister_user(storage, "alice")->score == alice_had + loot);
    } else {
        CHECK(giving.postage == loot / return_postage_share);
        CHECK(conquister_user(storage, "bob")->score == bob_had - loot - giving.postage);
        CHECK(conquister_user(storage, "alice")->score == alice_had + loot);
    }

    /* Settled once and for all. */
    CHECK(loot_return(storage, "bob", 40, true).status == ReturnStatus::nothing_to_return);
    CHECK(dispute_open(storage, "alice", 40).status == DisputeStatus::nothing_to_report);
}

TEST_CASE("nothing comes back after fourteen days") {
    const TestPaths paths{"reso-late-test"};
    {
        std::ofstream file{paths.conquister, std::ios::binary};
        file << R"({"current":null,"scores":{"alice":1000,"bob":0},"quotes_added":{}})";
    }
    Storage storage{paths.conquister, paths.quotes};

    static_cast<void>(raid_start(storage, 0, "bob", "alice", 0, quick_rides()));
    static_cast<void>(raid_due(storage, 5, quick_rides()));
    static_cast<void>(raid_due(storage, 10, quick_rides()));

    const std::int64_t late = 10 + return_window_seconds + 1;
    CHECK(dispute_open(storage, "alice", late).status == DisputeStatus::too_late);
    CHECK(loot_return(storage, "bob", late, true).status == ReturnStatus::too_late);
}

TEST_CASE("the rules themselves can be drawn again") {
    const TestPaths paths{"chaos-test"};
    Storage storage{paths.conquister, paths.quotes};

    const Rules configured{1000, 1000, 1500, 3, 4, 350, 100, 300};
    /* Nothing written down, so the owner's numbers stand. */
    const Rules before = rules_now(storage, configured);
    CHECK(before.quote_cost == 1000);
    CHECK(before.raid_share == 4);
    CHECK(before.cooldown_seconds == 300);

    const Rules least{100, 100, 100, 2, 2, 100, 0, 0};
    const Rules most{5000, 5000, 5000, 10, 10, 2000, 1000, 1800};
    const Rules drawn = scramble_rules(storage, least, most);
    CHECK(drawn.quote_cost >= 100);
    CHECK(drawn.quote_cost <= 5000);
    CHECK(drawn.boost_multiplier >= 2);
    CHECK(drawn.boost_multiplier <= 10);
    CHECK(drawn.travel_divisor >= 100);
    CHECK(drawn.travel_divisor <= 2000);

    /* And from now on those are the rules, reread from the file and all. */
    Storage reopened{paths.conquister, paths.quotes};
    const Rules after = rules_now(reopened, configured);
    CHECK(after.quote_cost == drawn.quote_cost);
    CHECK(after.raid_share == drawn.raid_share);
    CHECK(after.attack_cost == drawn.attack_cost);
}
