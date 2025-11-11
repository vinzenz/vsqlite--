#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/savepoint.hpp>
#include <sqlite/transaction.hpp>

using namespace testhelpers;

TEST(TransactionTest, TransactionAndSavepoint) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::transaction txn(conn, sqlite::transaction_type::exclusive);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("temporary");
        insert.emit();
        sqlite::savepoint sp(conn, "sp1");
        sqlite::command insert2(conn, "INSERT INTO items(value) VALUES (?);");
        insert2 % std::string("rollback");
        insert2.emit();
        sp.rollback();
        sp.release();
        txn.rollback();
    }
    EXPECT_EQ(count_rows(conn, "items"), 0);

    {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("keep");
        insert.emit();
        txn.commit();
    }
    EXPECT_EQ(count_rows(conn, "items"), 1);
}
