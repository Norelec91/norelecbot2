#include "test_paths.hpp"

#include "conquister_service.hpp"
#include "json.hpp"
#include "quote_service.hpp"
#include "storage.hpp"

#include <fstream>
#include <print>

namespace {

norelecbot::Json read_json(const std::string &path) {
    std::ifstream file{path, std::ios::binary};
    return norelecbot::Json::parse(file);
}

void assert_json_formats(const TestPaths &paths) {
    const norelecbot::Json state = read_json(paths.conquister);
    assert(state.is_object());
    assert(state.at("current").is_object());
    assert(state.at("scores").is_object());
    assert(state.at("quotes_added").is_object());

    const norelecbot::Json quotes = read_json(paths.quotes);
    assert(quotes.is_array());
}

}

int main() {
    using namespace norelecbot;

    {
        const TestPaths paths{"telegram-service-test"};
        {
            Storage storage{paths.conquister, paths.quotes};
            assert(quote_page_load(storage, 1).total == 0);
            assert(!quote_random(storage));

            ClaimResult claim = conquister_claim(storage, 1, "alice", 100);
            assert(claim.status == ClaimStatus::taken && claim.previous_username.empty());
            claim = conquister_claim(storage, 2, "bob", 1100);
            assert(claim.previous_username == "alice" && claim.earned == 1000);

            const Leaderboard leaderboard = conquister_leaderboard(storage, 10);
            assert(leaderboard.entries.size() == 1);
            assert(leaderboard.entries[0].username == "alice" && leaderboard.entries[0].score == 1000);
            assert(leaderboard.current && leaderboard.current->username == "bob");
            assert(leaderboard.current->since == 1100);

            std::optional<ConquisterUser> user = conquister_user(storage, "ALICE");
            assert(user && user->username == "alice");
            assert(user->score == 1000 && user->rank == 1 && user->quotes_added == 0);
            assert(!user->in_conquister);
            user = conquister_user(storage, "bob");
            assert(user && user->in_conquister && user->since == 1100);
            assert(user->score == 0 && user->rank == 0);
            assert(!conquister_user(storage, "carol"));

            QuoteAddResult addition = quote_add(storage, "alice", "quote di prova", 1000);
            assert(addition.status == QuoteAddStatus::added && addition.available_score == 0);
            assert(quote_random(storage) == "quote di prova");

            static_cast<void>(conquister_claim(storage, 1, "alice", 1101));
            static_cast<void>(conquister_claim(storage, 2, "bob", 2101));
            addition = quote_add(storage, "alice", "quote di prova", 1000);
            assert(addition.status == QuoteAddStatus::duplicate);
            const QuotePage page = quote_page_load(storage, 1);
            assert(page.items.size() == 1 && page.items[0] == "quote di prova");

            assert(quote_delete(storage, "1") == "quote di prova");
            assert(!quote_delete(storage, "1"));
            assert(quote_page_load(storage, 1).total == 0);
        }

        assert_json_formats(paths);
        Storage storage{paths.conquister, paths.quotes};
        Leaderboard leaderboard = conquister_leaderboard(storage, 10);
        assert(!leaderboard.entries.empty() && leaderboard.entries[0].username == "alice");
        assert(conquister_leaderboard(storage, 1).entries.size() == 1);
        leaderboard = conquister_leaderboard(storage, 0);
        assert(leaderboard.entries.size() == 2 && leaderboard.entries[1].username == "bob");
    }

    {
        // A quote saved before a failed state save must be rolled back.
        const TestPaths paths{"rollback-test"};
        {
            std::ofstream file{paths.quotes, std::ios::binary};
            file << "[\"originale\"]";
        }
        Storage storage{"rollback-missing-directory/conquister.json", paths.quotes};
        bool failed = false;
        try {
            static_cast<void>(quote_add(storage, "alice", "nuova", 0));
        } catch (const StorageError &) {
            failed = true;
        }
        assert(failed);
        const QuotePage page = quote_page_load(storage, 1);
        assert(page.total == 1 && page.items[0] == "originale");
    }

    std::println("service tests: ok");
    return 0;
}
