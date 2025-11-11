#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/savepoint.hpp>
#include <sqlite/snapshot.hpp>
#include <sqlite/transaction.hpp>

using namespace testhelpers;

TEST(SnapshotTest, TransactionSnapshotProvidesHistoricalReads) {
    if (!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);

    sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
    sqlite::command insert(conn, "INSERT INTO docs(body) VALUES (?);");
    insert % std::string("a");
    insert.step_once();
    auto snap = txn.take_snapshot();
    insert % std::string("b");
    insert.step_once();
    txn.commit();

    sqlite::transaction read(conn, sqlite::transaction_type::deferred);
    snap.open(conn);
    sqlite::query q(conn, "SELECT COUNT(*) FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
}

TEST(SnapshotTest, SavepointSnapshotControlsScope) {
    if (!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);

    sqlite::savepoint sp(conn, "sp");
    sqlite::command insert(conn, "INSERT INTO docs(body) VALUES (?);");
    insert % std::string("alpha");
    insert.step_once();
    auto snap = sp.take_snapshot();
    insert % std::string("beta");
    insert.step_once();
    sp.rollback();

    sqlite::query q(conn, "SELECT COUNT(*) FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 0);

    snap.open(conn);
    sqlite::query check(conn, "SELECT COUNT(*) FROM docs;");
    auto snap_res = check.get_result();
    ASSERT_TRUE(snap_res->next_row());
    EXPECT_EQ(snap_res->get<int>(0), 1);
}
