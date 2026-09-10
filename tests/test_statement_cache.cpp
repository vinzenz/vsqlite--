#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/query.hpp>

#include <sqlite3.h>

using namespace testhelpers;

namespace {

int count_open_statements(sqlite::connection &con) {
    sqlite3 *handle = sqlite::private_accessor::get_handle(con);
    int count       = 0;
    for (sqlite3_stmt *stmt = sqlite3_next_stmt(handle, nullptr); stmt != nullptr;
         stmt               = sqlite3_next_stmt(handle, stmt)) {
        ++count;
    }
    return count;
}

} // namespace

TEST(StatementCacheTest, RetainsStatementsBetweenUses) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    auto handle          = sqlite::private_accessor::get_handle(conn);
    sqlite3_stmt *cached = sqlite3_next_stmt(handle, nullptr);
    ASSERT_NE(cached, nullptr);

    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    sqlite3_stmt *cached_again = sqlite3_next_stmt(handle, nullptr);
    EXPECT_EQ(cached_again, cached);
}

// Regression test for issue #49: a statement returned to the cache while parked at
// SQLITE_ROW must release its implicit read transaction, otherwise writers on other
// connections stay blocked until the cache is cleared.
TEST(StatementCacheTest, MidCursorReturnReleasesReadLock) {
    TempFile db("statement_cache_read_lock");
    sqlite::connection reader(db.string()), writer(db.string());
    reader.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(writer, "CREATE TABLE t(x)", true);
    sqlite::execute(writer, "INSERT INTO t VALUES (1),(2)", true);
    {
        sqlite::query q(reader, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    } // query and result destroyed with the cursor still at SQLITE_ROW.
    sqlite3_stmt *cached = sqlite3_next_stmt(sqlite::private_accessor::get_handle(reader), nullptr);
    ASSERT_NE(cached, nullptr);
    EXPECT_NO_THROW(sqlite::execute(writer, "INSERT INTO t VALUES (3)", true));
    // The statement stays cached after being reset, it is not thrown away.
    EXPECT_EQ(sqlite3_next_stmt(sqlite::private_accessor::get_handle(reader), nullptr), cached);
}

// Early exit from a range loop leaves the cursor at SQLITE_ROW as well.
TEST(StatementCacheTest, EarlyRangeLoopExitReleasesReadLock) {
    TempFile db("statement_cache_range_exit");
    sqlite::connection reader(db.string()), writer(db.string());
    reader.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(writer, "CREATE TABLE t(x)", true);
    sqlite::execute(writer, "INSERT INTO t VALUES (1),(2)", true);
    {
        sqlite::query q(reader, "SELECT x FROM t");
        for (auto const &row : q.each()) {
            EXPECT_EQ(row.get<int>(0), 1);
            break;
        }
    }
    EXPECT_NO_THROW(sqlite::execute(writer, "INSERT INTO t VALUES (3)", true));
}

// A one-row aggregate whose cursor never advances to SQLITE_DONE.
TEST(StatementCacheTest, UnfinishedAggregateReleasesReadLock) {
    TempFile db("statement_cache_aggregate");
    sqlite::connection reader(db.string()), writer(db.string());
    reader.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(writer, "CREATE TABLE t(x)", true);
    sqlite::execute(writer, "INSERT INTO t VALUES (1),(2)", true);
    {
        sqlite::query q(reader, "SELECT COUNT(*) FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 2);
        // No second next_row(): the cursor never reaches SQLITE_DONE.
    }
    EXPECT_NO_THROW(sqlite::execute(writer, "INSERT INTO t VALUES (3)", true));
}

TEST(StatementCacheTest, ReuseAfterMidCursorReturnRestartsFromFirstRow) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1),(2)", true);
    {
        sqlite::query q(conn, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    sqlite::query q(conn, "SELECT x FROM t");
    auto res = q.get_result();
    EXPECT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    EXPECT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    EXPECT_FALSE(res->next_row());
}

TEST(StatementCacheTest, BindingsDoNotSurviveReturnToCache) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    {
        sqlite::query q(conn, "SELECT 42 AS v WHERE ? != 0;");
        q % 1;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 42);
    }
    sqlite::query q(conn, "SELECT 42 AS v WHERE ? != 0;");
    auto res = q.get_result();
    EXPECT_FALSE(res->next_row()); // The previous binding of 1 must not be reused.
}

TEST(StatementCacheTest, StatementFailingDuringStepIsNotRetained) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x UNIQUE)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1)", true);
    ASSERT_EQ(count_open_statements(conn), 1);
    try {
        sqlite::command cmd(conn, "INSERT INTO t VALUES (?)");
        cmd % 1;
        cmd.step_once();
        FAIL() << "expected the duplicate insert to fail";
    } catch (std::exception const &) {
    }
    // The reset reported the failed evaluation, so the statement was finalized
    // instead of being retained.
    EXPECT_EQ(count_open_statements(conn), 1);
    // The same SQL can still be prepared and executed afterwards.
    sqlite::command cmd(conn, "INSERT INTO t VALUES (?)");
    cmd % 2;
    EXPECT_NO_THROW(cmd.step_once());
    EXPECT_EQ(count_rows(conn, "t"), 2);
}
