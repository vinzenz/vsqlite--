#include "test_common.hpp"

#include <stdexcept>

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/savepoint.hpp>
#include <sqlite/transaction.hpp>

using namespace testhelpers;

namespace {
void insert_value(sqlite::connection &conn, std::string const &value) {
    sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
    insert % value;
    insert.step_once();
}
} // namespace

TEST(TransactionTest, TransactionAndSavepoint) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::transaction txn(conn, sqlite::transaction_type::exclusive);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("temporary");
        insert.step_once();
        sqlite::savepoint sp(conn, "sp1");
        sqlite::command insert2(conn, "INSERT INTO items(value) VALUES (?);");
        insert2 % std::string("rollback");
        insert2.step_once();
        sp.rollback();
        sp.release();
        txn.rollback();
    }
    EXPECT_EQ(count_rows(conn, "items"), 0);

    {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("keep");
        insert.step_once();
        txn.commit();
    }
    EXPECT_EQ(count_rows(conn, "items"), 1);
}

TEST(TransactionTest, SavepointDestructionAfterOuterRollback) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::savepoint sp(conn, "sp_rollback");
        insert_value(conn, "discarded");
        txn.rollback();
        // The rollback above already invalidated the SQL savepoint; destroying
        // the guard now must not throw (and thus not terminate).
    }
    EXPECT_EQ(count_rows(conn, "items"), 0);
}

TEST(TransactionTest, SavepointDestructionAfterOuterCommit) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::savepoint sp(conn, "sp_commit");
        insert_value(conn, "kept");
        txn.commit();
        // The commit above already invalidated the SQL savepoint; destroying
        // the guard now must not throw (and thus not terminate).
    }
    EXPECT_EQ(count_rows(conn, "items"), 1);
}

TEST(TransactionTest, NestedSavepointUnwindingViaException) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    try {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::savepoint outer(conn, "outer_sp");
        insert_value(conn, "outer");
        try {
            sqlite::savepoint inner(conn, "inner_sp");
            insert_value(conn, "inner");
            inner.rollback();
            throw std::runtime_error("discard inner work");
        } catch (std::runtime_error const &) {
            // inner guard is destroyed during unwinding while the transaction
            // is still active; releasing it must not terminate.
        }
        EXPECT_EQ(count_rows(conn, "items"), 1);
        txn.commit();
    } catch (std::exception const &ex) {
        FAIL() << "unexpected exception: " << ex.what();
    }
    EXPECT_EQ(count_rows(conn, "items"), 1);
}

TEST(TransactionTest, ExplicitReleaseStillReportsFailure) {
    sqlite::connection conn(":memory:");
    sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
    sqlite::savepoint sp(conn, "sp_explicit");
    txn.rollback();
    // Unlike the destructor, an explicit release() must surface the failure.
    EXPECT_THROW(sp.release(), std::exception);
}
