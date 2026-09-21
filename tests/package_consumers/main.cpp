#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/query.hpp>
#include <sqlite/result.hpp>
#include <sqlite/serialization.hpp>
#include <sqlite/transaction.hpp>

#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view what) {
    if (condition) {
        std::cout << "ok: " << what << '\n';
    } else {
        std::cout << "FAILED: " << what << '\n';
        ++g_failures;
    }
}

int count_rows(sqlite::connection &con, std::string_view table) {
    sqlite::query q(con, "SELECT COUNT(*) FROM " + std::string(table) + ";");
    auto row = q.get_result();
    if (!row || !row->next_row()) {
        return -1;
    }
    return row->get<int>(0);
}

// Binds and reads back NULL, empty text, and empty blob values. The three are
// distinct storage states, and the consumer must be able to tell them apart.
void check_null_and_empty_values(sqlite::connection &con) {
    sqlite::execute(con, "CREATE TABLE values_test(id INTEGER PRIMARY KEY, txt TEXT, data BLOB);",
                    true);

    sqlite::command insert(con, "INSERT INTO values_test(id, txt, data) VALUES (?, ?, ?);");
    insert % 1 % sqlite::nil % std::vector<unsigned char>{};
    insert.step_once();
    insert.clear();
    insert % 2 % std::string{} % sqlite::nil;
    insert.step_once();

    {
        sqlite::query q(con, "SELECT txt, data FROM values_test WHERE id = 1;");
        auto row = q.get_result();
        check(row && row->next_row(), "row with NULL text and empty blob produced");
        check(row->is_null(0), "bound NULL text reads back as SQL NULL");
        check(!row->get<std::optional<std::string>>(0).has_value(),
              "bound NULL text reads back as empty optional");
        check(!row->is_null(1), "bound empty blob reads back as non-NULL");
        check(row->get_column_type(1) == sqlite::blob, "bound empty blob keeps the BLOB type");
        check(row->get_binary_size(1) == 0, "bound empty blob reads back with zero length");
    }
    {
        sqlite::query q(con, "SELECT txt, data FROM values_test WHERE id = 2;");
        auto row = q.get_result();
        check(row && row->next_row(), "row with empty text and NULL blob produced");
        check(!row->is_null(0), "bound empty text reads back as non-NULL");
        check(row->get_column_type(0) == sqlite::text, "bound empty text keeps the TEXT type");
        check(row->get<std::string>(0).empty(), "bound empty text reads back as empty string");
        auto const text = row->get<std::optional<std::string>>(0);
        check(text.has_value() && text->empty(), "bound empty text reads back as engaged optional");
        check(row->is_null(1), "bound NULL blob reads back as SQL NULL");
    }
}

// Runs a committed and a rolled back transaction and checks that only the
// committed change survives.
void check_transaction_rollback(sqlite::connection &con) {
    sqlite::execute(con, "CREATE TABLE transaction_test(value INTEGER);", true);
    {
        sqlite::transaction rolled_back(con);
        sqlite::execute(con, "INSERT INTO transaction_test(value) VALUES (1);", true);
        rolled_back.rollback();
    }
    {
        sqlite::transaction committed(con);
        sqlite::execute(con, "INSERT INTO transaction_test(value) VALUES (2);", true);
        committed.commit();
    }
    check(count_rows(con, "transaction_test") == 1, "rolled back insert is not visible");
}

// Exercises one capability-guarded optional API: when the linked SQLite
// provides serialization, an in-memory database must survive a round trip.
// Consumers linked against SQLite builds without the API still pass.
void check_optional_serialization(sqlite::connection &con) {
    if (!sqlite::serialization_supported()) {
        std::cout << "note: serialization API not available in this SQLite build\n";
        return;
    }
    auto image       = sqlite::serialize(con);
    bool round_trips = false;
    {
        sqlite::connection restored(":memory:");
        sqlite::deserialize(restored, image);
        round_trips = count_rows(restored, "smoke_test") == count_rows(con, "smoke_test") &&
                      count_rows(restored, "smoke_test") > 0;
    }
    check(round_trips, "serialization round trip restores the database");
}

} // namespace

int main() {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE smoke_test(value TEXT);", true);
    sqlite::execute(conn, "INSERT INTO smoke_test(value) VALUES ('vsqlitepp');", true);

    sqlite::create_function(conn, "shout",
                            [](std::string_view value) { return std::string(value) + "!"; });

    sqlite::query query(conn, "SELECT shout(value) FROM smoke_test;");
    auto result = query.get_result();
    check(result && result->next_row(), "query produced a row");
    check(result->get<std::string>(0) == "vsqlitepp!", "user-defined function result");

    check_null_and_empty_values(conn);
    check_transaction_rollback(conn);
    check_optional_serialization(conn);

    if (g_failures != 0) {
        std::cout << g_failures << " consumer check(s) failed\n";
        return 1;
    }
    std::cout << "consumer checks passed\n";
    return 0;
}
