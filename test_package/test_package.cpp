#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/query.hpp>

#include <string>
#include <string_view>

int main() {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE sample(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(conn, "INSERT INTO sample(value) VALUES ('conan');", true);

    sqlite::create_function(conn, "shout", [](std::string_view value) {
        return std::string(value) + "!";
    });

    sqlite::query q(conn, "SELECT COUNT(*), shout('conan') FROM sample;");
    auto res = q.get_result();
    if (!res->next_row()) {
        return 1;
    }
    return (res->get<int>(0) == 1 && res->get<std::string>(1) == "conan!") ? 0 : 2;
}
