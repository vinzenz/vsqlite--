#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/savepoint.hpp>
#include <sqlite/snapshot.hpp>
#include <sqlite/transaction.hpp>

using namespace testhelpers;

namespace {
void prime_snapshot_connection(sqlite::connection &conn) {
    sqlite::query q(conn, "PRAGMA application_id;");
    auto res = q.get_result();
    res->next_row();
}
} // namespace

TEST(SnapshotTest, TransactionSnapshotProvidesHistoricalReads) {
    if (!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    TempFile db("snapshot_txn");
    sqlite::connection writer(db.string());
    sqlite::enable_wal(writer);
    dump_sqlite_diagnostics(writer, "SnapshotTest.TransactionSnapshotProvidesHistoricalReads");
    sqlite::execute(writer, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);

    sqlite::command insert(writer, "INSERT INTO docs(body) VALUES (?);");
    {
        sqlite::transaction txn(writer, sqlite::transaction_type::immediate);
        insert % std::string("a");
        insert.step_once();
        insert.clear();
        txn.commit();
    }

    sqlite::connection reader_snapshot(db.string());
    sqlite::connection reader_open(db.string());
    prime_snapshot_connection(reader_snapshot);
    prime_snapshot_connection(reader_open);

    sqlite::snapshot snap;
    {
        sqlite::transaction read(reader_snapshot, sqlite::transaction_type::deferred);
        sqlite::query q(reader_snapshot, "SELECT COUNT(*) FROM docs;");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
        snap = read.take_snapshot();
        read.commit();
    }

    {
        sqlite::transaction txn(writer, sqlite::transaction_type::immediate);
        insert % std::string("b");
        insert.step_once();
        insert.clear();
        txn.commit();
    }

    dump_table_info(writer, "docs");

    sqlite::transaction read(reader_open, sqlite::transaction_type::deferred);
    snap.open(reader_open);
    sqlite::query q(reader_open, "SELECT COUNT(*) FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
}

TEST(SnapshotTest, SavepointSnapshotControlsScope) {
    if (!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    TempFile db("snapshot_savepoint");
    sqlite::connection writer(db.string());
    sqlite::enable_wal(writer);
    dump_sqlite_diagnostics(writer, "SnapshotTest.SavepointSnapshotControlsScope");
    sqlite::execute(writer, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);

    sqlite::command insert(writer, "INSERT INTO docs(body) VALUES (?);");
    {
        sqlite::transaction txn(writer, sqlite::transaction_type::immediate);
        insert % std::string("alpha");
        insert.step_once();
        insert.clear();
        txn.commit();
    }

    sqlite::connection reader_snapshot(db.string());
    sqlite::connection reader_open(db.string());
    prime_snapshot_connection(reader_snapshot);
    prime_snapshot_connection(reader_open);

    sqlite::snapshot snap;
    {
        sqlite::savepoint sp(reader_snapshot, "sp");
        sqlite::query prime(reader_snapshot, "SELECT COUNT(*) FROM docs;");
        auto prime_res = prime.get_result();
        ASSERT_TRUE(prime_res->next_row());
        snap = sp.take_snapshot();
        sp.release();
    }

    {
        sqlite::transaction txn(writer, sqlite::transaction_type::immediate);
        insert % std::string("beta");
        insert.step_once();
        insert.clear();
        txn.commit();
    }

    dump_table_info(writer, "docs");

    sqlite::savepoint sp(reader_open, "sp_read");
    sp.open_snapshot(snap);
    sqlite::query check(reader_open, "SELECT COUNT(*) FROM docs;");
    auto snap_res = check.get_result();
    ASSERT_TRUE(snap_res->next_row());
    EXPECT_EQ(snap_res->get<int>(0), 1);
}
