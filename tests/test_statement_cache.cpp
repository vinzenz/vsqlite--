#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/private/private_accessor.hpp>

#include <sqlite3.h>

using namespace testhelpers;

TEST(StatementCacheTest, RetainsStatementsBetweenUses) {
    sqlite::connection conn(":memory:");
    conn.configure_statement_cache({.capacity = 4, .enabled = true});
    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.emit();
    }
    auto handle = sqlite::private_accessor::get_handle(conn);
    sqlite3_stmt * cached = sqlite3_next_stmt(handle, nullptr);
    ASSERT_NE(cached, nullptr);

    {
        sqlite::command cmd(conn, "SELECT 1;");
        cmd.emit();
    }
    sqlite3_stmt * cached_again = sqlite3_next_stmt(handle, nullptr);
    EXPECT_EQ(cached_again, cached);
}

