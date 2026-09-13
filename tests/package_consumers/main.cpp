#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/query.hpp>

#include <string>
#include <string_view>

int main()
{
  sqlite::connection conn(":memory:");
  sqlite::execute(conn, "CREATE TABLE smoke_test(value TEXT);", true);
  sqlite::execute(conn, "INSERT INTO smoke_test(value) VALUES ('vsqlitepp');", true);

  sqlite::create_function(conn, "shout", [](std::string_view value) {
    return std::string(value) + "!";
  });

  sqlite::query query(conn, "SELECT shout(value) FROM smoke_test;");
  auto result = query.get_result();
  if(!result || !result->next_row()) {
    return 1;
  }

  return result->get<std::string>(0) == "vsqlitepp!" ? 0 : 1;
}
