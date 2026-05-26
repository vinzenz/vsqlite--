#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/query.hpp>

#include <string>

int main()
{
  sqlite::connection conn(":memory:");
  sqlite::execute(conn, "CREATE TABLE smoke_test(value TEXT);", true);
  sqlite::execute(conn, "INSERT INTO smoke_test(value) VALUES ('vsqlitepp');", true);

  sqlite::query query(conn, "SELECT value FROM smoke_test;");
  auto result = query.get_result();
  if(!result || !result->next_row()) {
    return 1;
  }

  return result->get<std::string>(0) == "vsqlitepp" ? 0 : 1;
}
