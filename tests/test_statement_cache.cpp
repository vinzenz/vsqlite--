#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/private/statement_cache_seam.hpp>
#include <sqlite/query.hpp>
#include <sqlite/transaction.hpp>

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

// Required cache invariant: every statement the connection still owns reports
// inactive through sqlite3_stmt_busy() once it has been returned. A statement
// parked at SQLITE_ROW keeps its implicit transaction open, so a busy stored
// statement would still hold read locks.
bool all_statements_idle(sqlite::connection &con) {
    sqlite3 *handle = sqlite::private_accessor::get_handle(con);
    for (sqlite3_stmt *stmt = sqlite3_next_stmt(handle, nullptr); stmt != nullptr;
         stmt               = sqlite3_next_stmt(handle, stmt)) {
        if (sqlite3_stmt_busy(stmt)) {
            return false;
        }
    }
    return true;
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
    // Normal return after a completed step: the stored statement is not busy.
    EXPECT_FALSE(sqlite3_stmt_busy(cached));
    EXPECT_TRUE(all_statements_idle(conn));

    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    sqlite3_stmt *cached_again = sqlite3_next_stmt(handle, nullptr);
    EXPECT_EQ(cached_again, cached);
    EXPECT_FALSE(sqlite3_stmt_busy(cached_again));
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
    EXPECT_FALSE(sqlite3_stmt_busy(cached));
    EXPECT_NO_THROW(sqlite::execute(writer, "INSERT INTO t VALUES (3)", true));
    // The statement stays cached after being reset, it is not thrown away.
    EXPECT_EQ(sqlite3_next_stmt(sqlite::private_accessor::get_handle(reader), nullptr), cached);
    EXPECT_TRUE(all_statements_idle(reader));
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
    EXPECT_TRUE(all_statements_idle(reader));
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
    EXPECT_TRUE(all_statements_idle(reader));
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
    EXPECT_TRUE(all_statements_idle(conn));
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
    // Return after a failed step: nothing the connection still owns is busy.
    EXPECT_TRUE(all_statements_idle(conn));
    // The same SQL can still be prepared and executed afterwards.
    sqlite::command cmd(conn, "INSERT INTO t VALUES (?)");
    cmd % 2;
    EXPECT_NO_THROW(cmd.step_once());
    EXPECT_EQ(count_rows(conn, "t"), 2);
    EXPECT_TRUE(all_statements_idle(conn));
}

// Returning a statement to a full cache evicts the least recently used entry and keeps
// the surviving bookkeeping consistent.
TEST(StatementCacheTest, EvictionOnOverflowReleasesOldestStatement) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 2, .enabled = true});
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    {
        sqlite::command cmd(conn, "SELECT 2;");
        cmd.step_once();
    }
    {
        sqlite::command cmd(conn, "SELECT 3;"); // Overflow: SELECT 1 must be evicted.
        cmd.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 2);
    EXPECT_TRUE(all_statements_idle(conn));
    { // The evicted SQL is prepared fresh again and the cache stays within capacity.
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 2);
    EXPECT_TRUE(all_statements_idle(conn));
    { // The surviving cached statements are still reusable.
        sqlite::command cmd(conn, "SELECT 2;");
        cmd.step_once();
        sqlite::command cmd3(conn, "SELECT 3;");
        cmd3.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 2);
    EXPECT_TRUE(all_statements_idle(conn));
}

// A mid-cursor return into a full cache must still release the read lock while evicting
// the least recently used entry.
TEST(StatementCacheTest, MidCursorReturnUnderCachePressureReleasesReadLock) {
    TempFile db("statement_cache_pressure");
    sqlite::connection reader(db.string()), writer(db.string());
    reader.configure_statement_cache({.capacity = 1, .enabled = true});
    sqlite::execute(writer, "CREATE TABLE t(x)", true);
    sqlite::execute(writer, "INSERT INTO t VALUES (1)", true);
    { // Park one unrelated statement in the reader's cache.
        sqlite::command cmd(reader, "SELECT 99;");
        cmd.step_once();
    }
    {
        sqlite::query q(reader, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    } // Destroyed mid-cursor: evicts SELECT 99, resets and caches SELECT x FROM t.
    EXPECT_EQ(count_open_statements(reader), 1);
    EXPECT_TRUE(all_statements_idle(reader));
    EXPECT_NO_THROW(sqlite::execute(writer, "INSERT INTO t VALUES (2)", true));
    { // Both the evicted and the reset statements keep working afterwards.
        sqlite::command cmd(reader, "SELECT 99;");
        cmd.step_once();
        sqlite::query q(reader, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    EXPECT_TRUE(all_statements_idle(reader));
}

// Returning a statement is noexcept, so a sqlite3_reset error describing the
// statement's failed evaluation must not escape destruction. It is reported
// through the connection's error hook instead, and the statement is finalized.
TEST(StatementCacheTest, ResetFailureIsReportedThroughErrorHook) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x UNIQUE)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1)", true);
    std::vector<std::string> reports;
    conn.set_statement_cache_error_hook(
        [&reports](std::string message) { reports.push_back(std::move(message)); });
    ASSERT_EQ(count_open_statements(conn), 1);
    {
        sqlite::command cmd(conn, "INSERT INTO t VALUES (?)");
        cmd % 1; // Conflicts with the existing row.
        EXPECT_THROW(cmd.step_once(), sqlite::database_exception);
    } // Destruction returns the statement: reset reports the constraint error.
    ASSERT_EQ(reports.size(), std::size_t{1});
    EXPECT_NE(reports[0].find("sqlite3_reset"), std::string::npos);
    EXPECT_NE(reports[0].find("UNIQUE"), std::string::npos);
    EXPECT_NE(reports[0].find("INSERT INTO t VALUES (?)"), std::string::npos);
    // The statement was finalized instead of retained.
    EXPECT_EQ(count_open_statements(conn), 1);
    EXPECT_TRUE(all_statements_idle(conn));
}

// A throwing hook must not turn the noexcept return path into a termination.
TEST(StatementCacheTest, ThrowingErrorHookDoesNotEscapeStatementDestruction) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x UNIQUE)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1)", true);
    conn.set_statement_cache_error_hook(
        [](std::string const &) { throw std::runtime_error("hook error"); });
    {
        sqlite::command cmd(conn, "INSERT INTO t VALUES (?)");
        cmd % 1;
        EXPECT_THROW(cmd.step_once(), sqlite::database_exception);
    } // Would terminate the process if the hook's exception escaped.
    EXPECT_EQ(count_open_statements(conn), 1);
    EXPECT_TRUE(all_statements_idle(conn));
}

// Failed cache insertion (bookkeeping allocation fails before the map entry is
// created): the statement is finalized exactly once and never stored. The seam
// stands in for the out-of-memory condition.
TEST(StatementCacheTest, FailedBookkeepingInsertionFinalizesStatement) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::statement_cache_seam::fail_next_insertion(
        sqlite::statement_cache_seam::insertion_failure::before_bookkeeping);
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    } // Return throws std::bad_alloc internally; release() must finalize, not store.
    EXPECT_EQ(count_open_statements(conn), 0);
    // The cache stays usable: the next return of the same SQL is stored normally.
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 1);
    EXPECT_TRUE(all_statements_idle(conn));
}

// Failed cache insertion (list insertion fails after the map entry exists): the
// half-created bookkeeping is rolled back and the statement is finalized exactly
// once, leaving no cache entry that points at a finalized statement.
TEST(StatementCacheTest, FailedRetainInsertionRollsBackAndFinalizesStatement) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    auto handle = sqlite::private_accessor::get_handle(conn);
    sqlite::statement_cache_seam::fail_next_insertion(
        sqlite::statement_cache_seam::insertion_failure::before_retain);
    {
        sqlite::command cmd(conn, "SELECT 42;");
        cmd.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 0);
    // The rolled-back entry must not hand out a dangling pointer: the same SQL
    // is prepared fresh and cached again, then reused on the next use.
    {
        sqlite::command cmd(conn, "SELECT 42;");
        EXPECT_TRUE(cmd.step_once());
    }
    EXPECT_EQ(count_open_statements(conn), 1);
    sqlite3_stmt *stored = sqlite3_next_stmt(handle, nullptr);
    ASSERT_NE(stored, nullptr);
    {
        sqlite::command cmd(conn, "SELECT 42;");
        EXPECT_TRUE(cmd.step_once());
    }
    EXPECT_EQ(sqlite3_next_stmt(handle, nullptr), stored);
    EXPECT_FALSE(sqlite3_stmt_busy(stored));
}

// Reconfiguration contract (see README.md): reconfiguring the cache finalizes
// idle entries immediately.
TEST(StatementCacheTest, ReconfigureDisposesIdleEntries) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    ASSERT_EQ(count_open_statements(conn), 1);
    conn.configure_statement_cache({.capacity = 8, .enabled = true});
    EXPECT_EQ(count_open_statements(conn), 0);
}

// Reconfiguration contract (see README.md): a statement that is checked out
// while the cache is reconfigured may re-enter the new configuration when it is
// returned. The cache keys entries by SQL text only, so no generation tracking
// separates statements prepared under an older configuration.
TEST(StatementCacheTest, ReconfigureWhileCheckedOutAcceptsReturnedStatement) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1),(2)", true);
    {
        sqlite::query q(conn, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
        // Live cursor: idle entries are disposed, the checked-out statement is not.
        conn.configure_statement_cache({.capacity = 8, .enabled = true});
        EXPECT_EQ(conn.statement_cache_settings().capacity, std::size_t{8});
        EXPECT_EQ(count_open_statements(conn), 1);
    } // The checked-out statement returns into the new configuration.
    sqlite3_stmt *stored = sqlite3_next_stmt(sqlite::private_accessor::get_handle(conn), nullptr);
    ASSERT_NE(stored, nullptr);
    EXPECT_FALSE(sqlite3_stmt_busy(stored));
    { // It re-entered the new configuration: the same SQL reuses it.
        sqlite::query q2(conn, "SELECT x FROM t");
        auto res = q2.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    EXPECT_EQ(sqlite3_next_stmt(sqlite::private_accessor::get_handle(conn), nullptr), stored);
    EXPECT_FALSE(sqlite3_stmt_busy(stored));
    EXPECT_TRUE(all_statements_idle(conn));
}

// Reconfiguration to a disabled cache while a statement is checked out: the
// return path finalizes the statement instead of storing it.
TEST(StatementCacheTest, ReconfigureToDisabledWhileCheckedOutFinalizesReturn) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    sqlite::execute(conn, "CREATE TABLE t(x)", true);
    sqlite::execute(conn, "INSERT INTO t VALUES (1)", true);
    {
        sqlite::query q(conn, "SELECT x FROM t");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        conn.configure_statement_cache({.capacity = 4, .enabled = false});
        EXPECT_FALSE(conn.statement_cache_settings().enabled);
    } // Returned with caching disabled: finalized, and nothing stays busy.
    EXPECT_EQ(count_open_statements(conn), 0);
    EXPECT_TRUE(all_statements_idle(conn));
    { // Re-enabling starts from an empty, working cache.
        conn.configure_statement_cache({.capacity = 4, .enabled = true});
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.step_once();
    }
    EXPECT_EQ(count_open_statements(conn), 1);
    EXPECT_TRUE(all_statements_idle(conn));
}

namespace {

// One multi-statement workload, run against a fresh connection: writes with
// bound parameters, committed and rolled back transactions, an UPDATE with a
// change count, a destroyed mid-cursor read, full range consumption, and a
// repeated query. All observable outcomes are recorded so that a cache-enabled
// and a cache-disabled run can be compared.
struct workload_outcomes {
    std::vector<int> repeated_counts;
    std::vector<int> range_rows;
    int committed_rows       = 0;
    int rolled_back_rows     = 0;
    int update_changes       = 0;
    int mid_cursor_first_row = 0;
    std::int64_t last_rowid  = 0;
};

workload_outcomes run_workload(bool cache_enabled) {
    workload_outcomes out;
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 8, .enabled = cache_enabled});
    sqlite::execute(conn, "CREATE TABLE t(id INTEGER PRIMARY KEY, x)", true);
    {
        sqlite::transaction tx(conn);
        for (int i = 1; i <= 5; ++i) {
            sqlite::command cmd(conn, "INSERT INTO t(x) VALUES (?)");
            cmd % (i * 10);
            EXPECT_FALSE(cmd.step_once());
        }
        out.last_rowid = conn.get_last_insert_rowid();
        tx.commit();
    }
    out.committed_rows = count_rows(conn, "t");
    {
        sqlite::transaction tx(conn);
        sqlite::command cmd(conn, "INSERT INTO t(x) VALUES (?)");
        cmd % 999;
        EXPECT_FALSE(cmd.step_once());
        tx.rollback();
    }
    out.rolled_back_rows = count_rows(conn, "t");
    {
        sqlite::query q(conn, "UPDATE t SET x = x + 1 WHERE x > 20");
        auto res = q.get_result();
        EXPECT_FALSE(res->next_row());
        out.update_changes = res->get_changes();
    }
    {
        sqlite::query q(conn, "SELECT x FROM t ORDER BY x");
        auto res = q.get_result();
        if (res->next_row()) {
            out.mid_cursor_first_row = res->get<int>(0);
        }
    } // Destroyed mid-cursor; the rest of the workload must be unaffected.
    {
        sqlite::query q(conn, "SELECT x FROM t ORDER BY x");
        for (auto const &row : q.each()) {
            out.range_rows.push_back(row.get<int>(0));
        }
    }
    for (int i = 0; i < 3; ++i) {
        sqlite::query q(conn, "SELECT COUNT(*) FROM t");
        auto res = q.get_result();
        EXPECT_TRUE(res->next_row());
        out.repeated_counts.push_back(res->get<int>(0));
    }
    EXPECT_TRUE(all_statements_idle(conn));
    return out;
}

} // namespace

// The cache must change preparation cost only, never observable behavior: the
// same workload produces the same rows, change counts, rowids, and transaction
// outcomes with caching enabled and disabled.
TEST(StatementCacheTest, CacheDisabledRunMatchesCacheEnabledRun) {
    workload_outcomes const enabled  = run_workload(true);
    workload_outcomes const disabled = run_workload(false);

    // Sanity: the workload actually exercised data and transactions.
    ASSERT_EQ(enabled.committed_rows, 5);
    ASSERT_EQ(enabled.rolled_back_rows, 5);
    ASSERT_EQ(enabled.update_changes, 3); // x = 30, 40, 50 match "x > 20"
    ASSERT_EQ(enabled.last_rowid, 5);
    ASSERT_EQ(enabled.mid_cursor_first_row, 10);
    ASSERT_EQ(enabled.range_rows, (std::vector<int>{10, 20, 31, 41, 51}));
    ASSERT_EQ(enabled.repeated_counts, (std::vector<int>{5, 5, 5}));

    EXPECT_EQ(disabled.committed_rows, enabled.committed_rows);
    EXPECT_EQ(disabled.rolled_back_rows, enabled.rolled_back_rows);
    EXPECT_EQ(disabled.update_changes, enabled.update_changes);
    EXPECT_EQ(disabled.last_rowid, enabled.last_rowid);
    EXPECT_EQ(disabled.mid_cursor_first_row, enabled.mid_cursor_first_row);
    EXPECT_EQ(disabled.range_rows, enabled.range_rows);
    EXPECT_EQ(disabled.repeated_counts, enabled.repeated_counts);
}
