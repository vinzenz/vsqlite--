#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>

using namespace testhelpers;

TEST(FunctionTest, RegistersScalarFunction) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "double_int", [](int value) { return value * 2; },
                            {.deterministic = true});

    sqlite::query q(conn, "SELECT double_int(21);");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 42);
}

TEST(FunctionTest, OptionalAndBlobArguments) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "blob_size",
                            [](std::optional<std::vector<unsigned char>> blob) -> int {
                                return blob ? static_cast<int>(blob->size()) : -1;
                            });
    sqlite::command insert(conn, "SELECT blob_size(?);");
    std::vector<unsigned char> data{1, 2, 3};
    insert % data;
    EXPECT_TRUE(insert.step_once());
}

namespace {

/// Move-only callable whose destructor increments `destroyed` exactly once.
/// Moved-from instances hand the counter over and stay silent.
struct tracked_callable {
    explicit tracked_callable(int *destroyed_count) : destroyed(destroyed_count) {}

    tracked_callable(tracked_callable &&other) noexcept : destroyed(other.destroyed) {
        other.destroyed = nullptr;
    }
    tracked_callable(tracked_callable const &)            = delete;
    tracked_callable &operator=(tracked_callable const &) = delete;
    tracked_callable &operator=(tracked_callable &&)      = delete;

    ~tracked_callable() {
        if (destroyed) {
            ++*destroyed;
        }
    }

    int operator()() const {
        return 1;
    }

    int *destroyed;
};

} // namespace

TEST(FunctionTest, OverlongFunctionNameIsCatchable) {
    sqlite::connection conn(":memory:");
    int destroyed = 0;
    try {
        sqlite::create_function(conn, std::string(256, 'x'), tracked_callable{&destroyed});
        FAIL() << "Expected registration of an overlong function name to fail.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_EQ(ex.error_code(), SQLITE_MISUSE);
        EXPECT_EQ(destroyed, 1);
    }
}

TEST(FunctionTest, ReplacementDestroysPreviousCallbackOnce) {
    int first_destroyed  = 0;
    int second_destroyed = 0;
    {
        sqlite::connection conn(":memory:");
        sqlite::create_function(conn, "tracker", tracked_callable{&first_destroyed});
        sqlite::create_function(conn, "tracker", tracked_callable{&second_destroyed});
        EXPECT_EQ(first_destroyed, 1);
        EXPECT_EQ(second_destroyed, 0);

        sqlite::query q(conn, "SELECT tracker();");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    EXPECT_EQ(second_destroyed, 1);
}

TEST(FunctionTest, ConnectionCloseDestroysCallbackOnce) {
    int destroyed = 0;
    {
        sqlite::connection conn(":memory:");
        sqlite::create_function(conn, "tracker", tracked_callable{&destroyed});
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1);
}
