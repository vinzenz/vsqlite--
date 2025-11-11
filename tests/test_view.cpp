#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/view.hpp>

using namespace testhelpers;

TEST(ViewTest, CreateAndDropView) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE src(id INTEGER);", true);
    sqlite::view view(conn);
    view.create(false, "v_src", "SELECT * FROM src;");
    view.drop("v_src");
}

TEST(ViewTest, CreateAndDropWithDatabaseName) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE src(id INTEGER);", true);
    sqlite::view view(conn);
    view.create(true, "temp", "v_src", "SELECT * FROM src;");
    view.drop("temp", "v_src");
}

