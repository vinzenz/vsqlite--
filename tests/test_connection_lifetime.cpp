#include "test_common.hpp"

#include <sqlite/connection_pool.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/prepared_statement.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/private/statement_cache_seam.hpp>
#include <sqlite/query.hpp>

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Tests for the shared connection state (GH issue #69): results and cursors
// retain the state of the connection they were created from, so they can
// outlive both the query object and the connection facade while explicit
// close() rejects busy connections and pooled connections return only after
// every dependent operation finished.

namespace {

// The filesystem adapter is owned by the connection state, so counting its
// destructions observes when the deferred native cleanup actually ran.
struct counting_adapter final : sqlite::default_filesystem_adapter {
    std::atomic<int> *destroyed;

    explicit counting_adapter(std::atomic<int> *counter) : destroyed(counter) {}

    ~counting_adapter() override {
        destroyed->fetch_add(1, std::memory_order_relaxed);
    }
};

} // namespace

TEST(ConnectionLifetimeTest, ResultOutlivesQueryAndConnectionFacade) {
    std::atomic<int> destroyed(0);
    auto conn = std::make_optional<sqlite::connection>(
        ":memory:", std::make_shared<counting_adapter>(&destroyed));
    sqlite::execute(*conn, "CREATE TABLE lifetime_data(id INTEGER, value TEXT);", true);
    sqlite::execute(*conn, "INSERT INTO lifetime_data VALUES (1,'one'),(2,'two'),(3,'three');",
                    true);

    std::shared_ptr<sqlite::result> res;
    {
        sqlite::query q(*conn, "SELECT id, value FROM lifetime_data ORDER BY id;");
        res = q.get_result();
    }
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    auto const memory_with_facade = sqlite3_memory_used();

    // Destroying the facade defers the native cleanup: the state (and with it
    // the filesystem adapter and the SQLite memory) stays alive.
    conn.reset();
    EXPECT_EQ(destroyed.load(), 0);
    EXPECT_GE(sqlite3_memory_used(), memory_with_facade);

    // The already created cursor keeps reading correct data.
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    EXPECT_EQ(res->get<std::string>(1), "two");
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 3);
    EXPECT_EQ(res->get<std::string>(1), "three");
    EXPECT_FALSE(res->next_row());

    // The last dependent released the state, so the native cleanup runs now.
    res.reset();
    EXPECT_EQ(destroyed.load(), 1);
    EXPECT_LT(sqlite3_memory_used(), memory_with_facade);
}

TEST(ConnectionLifetimeTest, PreparedStatementCursorOutlivesFacade) {
    auto conn = std::make_optional<sqlite::connection>(":memory:");
    sqlite::execute(*conn, "CREATE TABLE ps_cursor_t(id INTEGER);", true);
    sqlite::execute(*conn, "INSERT INTO ps_cursor_t VALUES (1),(2),(3);", true);

    sqlite::cursor cur;
    {
        auto stmt = conn->prepare("SELECT id FROM ps_cursor_t ORDER BY id;");
        cur       = stmt.rows();
    }
    // Destroy the facade while the cursor is live; it keeps working because
    // the cursor retains the connection state through its result.
    conn.reset();

    std::vector<std::int64_t> ids;
    for (auto row : cur) {
        ids.push_back(row.get<std::int64_t>(0));
    }
    EXPECT_EQ(ids, (std::vector<std::int64_t>{1, 2, 3}));
}

TEST(ConnectionLifetimeTest, CloseRejectsBusyConnectionThenSucceeds) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE busy_close_t(id INTEGER);", true);
    sqlite::execute(conn, "INSERT INTO busy_close_t VALUES (1),(2);", true);
    {
        sqlite::query q(conn, "SELECT id FROM busy_close_t;");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());

        try {
            sqlite::private_accessor::close(conn);
            FAIL() << "close() must reject while a statement is in use";
        } catch (sqlite::database_exception const &ex) {
            EXPECT_NE(std::string(ex.what()).find("1 statement"), std::string::npos);
        }

        // The rejected close leaves the connection intact for the live cursor.
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 2);
        EXPECT_FALSE(res->next_row());
    }
    // The cursor and its query are gone; close succeeds and stays idempotent.
    EXPECT_NO_THROW(sqlite::private_accessor::close(conn));
    EXPECT_NO_THROW(sqlite::private_accessor::close(conn));
    EXPECT_THROW(sqlite::execute(conn, "SELECT 1;", true), sqlite::database_exception);
}

TEST(ConnectionLifetimeTest, CloseBusyErrorNamesEveryOutstandingStatement) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE busy_count_t(id INTEGER);", true);
    sqlite::query first(conn, "SELECT id FROM busy_count_t;");
    sqlite::query second(conn, "SELECT COUNT(*) FROM busy_count_t;");
    try {
        sqlite::private_accessor::close(conn);
        FAIL() << "close() must reject while statements are in use";
    } catch (sqlite::database_exception const &ex) {
        EXPECT_NE(std::string(ex.what()).find("2 statement"), std::string::npos);
    }
}

TEST(ConnectionLifetimeTest, PoolKeepsConnectionUntilDependentCursorEnds) {
    sqlite::connection_pool pool(2, sqlite::connection_pool::make_factory(":memory:"));
    auto lease = pool.acquire();
    sqlite::execute(*lease, "CREATE TABLE pool_retention_t(id INTEGER);", true);
    sqlite::execute(*lease, "INSERT INTO pool_retention_t VALUES (1),(2);", true);

    std::shared_ptr<sqlite::result> res;
    {
        sqlite::query q(*lease, "SELECT id FROM pool_retention_t ORDER BY id;");
        res = q.get_result();
    }
    ASSERT_TRUE(res->next_row());
    auto const *borrowed = lease.shared().get();

    // Destroy the public lease object: the live cursor still retains the
    // connection, so it must not return to the pool.
    lease = {};
    EXPECT_EQ(pool.idle_count(), 0u);

    // A second acquire must not receive the same connection; the pool creates
    // a new one per its capacity strategy.
    auto other = pool.acquire();
    EXPECT_NE(other.shared().get(), borrowed);
    EXPECT_EQ(pool.created_count(), 2u);
    other = {};
    EXPECT_EQ(pool.idle_count(), 1u);

    // The first borrower's cursor keeps reading its own connection's data.
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    EXPECT_FALSE(res->next_row());

    // Ending the cursor returns the retained connection to the pool.
    res.reset();
    EXPECT_EQ(pool.idle_count(), 2u);
}

TEST(ConnectionLifetimeTest, PoolOfSizeOneBlocksUntilDependentCursorEnds) {
    sqlite::connection_pool pool(1, sqlite::connection_pool::make_factory(":memory:"));
    std::shared_ptr<sqlite::result> res;
    {
        auto lease = pool.acquire();
        sqlite::execute(*lease, "CREATE TABLE pool_block_t(id INTEGER);", true);
        sqlite::execute(*lease, "INSERT INTO pool_block_t VALUES (42);", true);
        sqlite::query q(*lease, "SELECT id FROM pool_block_t;");
        res = q.get_result();
        ASSERT_TRUE(res->next_row());
    }
    EXPECT_EQ(pool.idle_count(), 0u);

    auto acquired = std::async(std::launch::async, [&pool] {
        auto lease = pool.acquire();
        sqlite::execute(*lease, "SELECT 1;", true);
    });
    // The pool is exhausted and the only connection is retained by the
    // cursor, so the second acquire blocks instead of handing it out.
    EXPECT_EQ(acquired.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);

    res.reset();
    acquired.wait();
    EXPECT_EQ(pool.idle_count(), 1u);
}

TEST(ConnectionLifetimeTest, DeferredCleanupFinalizesEveryCachedStatementOnce) {
    auto const baseline = sqlite3_memory_used();
    std::atomic<int> destroyed(0);
    std::shared_ptr<sqlite::result> res;
    {
        auto conn = std::make_optional<sqlite::connection>(
            ":memory:", std::make_shared<counting_adapter>(&destroyed));
        sqlite::execute(*conn, "CREATE TABLE finalize_t(id INTEGER);", true);
        // Distinct SQL texts fill the cache with prepared statements that only
        // the deferred cleanup may finalize.
        for (int i = 0; i < 20; ++i) {
            sqlite::execute(*conn, "INSERT INTO finalize_t VALUES (" + std::to_string(i) + ");",
                            true);
        }
        sqlite::query q(*conn, "SELECT id FROM finalize_t ORDER BY id;");
        res = q.get_result();
        ASSERT_TRUE(res->next_row());
    }
    // The facade is gone; the checked-out statement and the cached ones are
    // finalized exactly once when the last dependent releases the state.
    res.reset();
    EXPECT_EQ(destroyed.load(), 1);
    EXPECT_EQ(sqlite3_memory_used(), baseline);
}

TEST(ConnectionLifetimeTest, DeferredReleaseFinalizesWhenCacheInsertionFails) {
    auto const baseline = sqlite3_memory_used();
    std::shared_ptr<sqlite::result> res;
    {
        auto conn = std::make_optional<sqlite::connection>(":memory:");
        sqlite::execute(*conn, "CREATE TABLE seam_t(id INTEGER);", true);
        sqlite::execute(*conn, "INSERT INTO seam_t VALUES (7);", true);
        sqlite::query q(*conn, "SELECT id FROM seam_t;");
        res = q.get_result();
        ASSERT_TRUE(res->next_row());
    }
    // Force the cache bookkeeping of the deferred statement return to fail:
    // the release path must finalize the statement instead of storing it, and
    // the state destruction must still finalize the remaining cache exactly
    // once.
    sqlite::statement_cache_seam::fail_next_insertion(
        sqlite::statement_cache_seam::insertion_failure::before_bookkeeping);
    EXPECT_EQ(res->get<int>(0), 7);
    res.reset();
    EXPECT_EQ(sqlite3_memory_used(), baseline);
}
