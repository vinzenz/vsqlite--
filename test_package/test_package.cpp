#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/query.hpp>

int main() {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE sample(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(conn, "INSERT INTO sample(value) VALUES ('conan');", true);

    sqlite::query q(conn, "SELECT COUNT(*) FROM sample;");
    auto res = q.get_result();
    if (!res->next_row()) {
        return 1;
    }
    return res->get<int>(0) == 1 ? 0 : 2;
}
