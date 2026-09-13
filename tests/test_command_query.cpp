#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/json_fts.hpp>
#include <sqlite/query.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

using namespace testhelpers;

TEST(CommandQueryTest, BindsAndRetrievesData) {
    sqlite::connection conn(":memory:");
    dump_sqlite_diagnostics(conn, "CommandQueryTest.BindsAndRetrievesData");
    sqlite::execute(conn,
                    "CREATE TABLE sample(id INTEGER PRIMARY KEY, ival INTEGER, lval INTEGER, note "
                    "TEXT, amount REAL, data BLOB, nullable TEXT);",
                    true);

    sqlite::command insert(conn, "INSERT INTO sample(ival, lval, note, amount, data, nullable) "
                                 "VALUES (?, ?, ?, ?, ?, ?);");
    std::vector<unsigned char> blob{0, 1, 2, 3, 4};
    std::array<unsigned char, 3> blob_view_data{9, 8, 7};
    insert.bind(1, 42);
    insert.bind(2, static_cast<std::int64_t>(1) << 40);
    insert.bind(3, std::string_view("hello"));
    insert.bind(4, 3.14);
    insert.bind(5, std::span<const unsigned char>(blob));
    insert.bind(6);
    insert.step_once();
    insert.clear();

    std::string_view world_view("world_view");
    insert % 7 % static_cast<std::int64_t>(9007199254740993LL) % world_view % 2.71 %
        std::span<const unsigned char>(blob_view_data) % std::string("value");
    insert();

    dump_table_info(conn, "sample");

    sqlite::query q(conn,
                    "SELECT id, ival, lval, note, amount, data, nullable FROM sample ORDER BY id;");
    auto result = q.get_result();
    ASSERT_TRUE(result->next_row());
    EXPECT_EQ(result->get_column_count(), 7);
    EXPECT_EQ(result->get_column_name(0), "id");
    auto declared_type = result->get_column_decltype(2);
    EXPECT_TRUE(declared_type == "BIGINT" || declared_type == "INTEGER");
    EXPECT_EQ(result->get<int>(1), 42);
    EXPECT_EQ(result->get<std::int64_t>(2), static_cast<std::int64_t>(1) << 40);
    EXPECT_EQ(result->get<std::string>(3), "hello");
    EXPECT_DOUBLE_EQ(result->get<double>(4), 3.14);
    auto note_view = result->get<std::string_view>(3);
    EXPECT_EQ(note_view, "hello");
    auto stored_blob = load_blob(*result, 5);
    EXPECT_EQ(stored_blob, blob);
    auto blob_span = result->get_binary_span(5);
    EXPECT_EQ(blob_span.size(), blob.size());
    EXPECT_TRUE(std::equal(blob_span.begin(), blob_span.end(), blob.begin()));
    EXPECT_EQ(result->get_variant(5).index(), 6);
    EXPECT_EQ(result->get_binary_size(5), blob.size());
    EXPECT_EQ(result->get_variant(6).index(), 5);

    EXPECT_TRUE(result->next_row());
    auto v = result->get_variant(2);
    EXPECT_TRUE(std::holds_alternative<std::int64_t>(v));
    EXPECT_EQ(std::get<std::int64_t>(v), 9007199254740993LL);
    auto text_variant = result->get_variant(3);
    EXPECT_TRUE(std::holds_alternative<std::string>(text_variant));
    auto string_view_second = result->get<std::string_view>(3);
    EXPECT_EQ(string_view_second, world_view);
    auto blob_span_second = result->get_binary_span(5);
    EXPECT_EQ(blob_span_second.size(), blob_view_data.size());
    EXPECT_TRUE(
        std::equal(blob_span_second.begin(), blob_span_second.end(), blob_view_data.begin()));
    EXPECT_EQ(result->get_variant(4).index(), 3);
    auto blob_variant = result->get_variant(5);
    EXPECT_TRUE(std::holds_alternative<sqlite::blob_ref_t>(blob_variant));
    EXPECT_FALSE(result->next_row());
    EXPECT_TRUE(result->end());
    result->reset();
    EXPECT_TRUE(result->next_row());

    sqlite::query count_query(conn, "SELECT COUNT(*) FROM sample;");
    int total = -1;
    for (auto &row : count_query.each()) {
        total = row.get<int>(0);
    }
    EXPECT_EQ(total, 2);
}

TEST(CommandQueryTest, TypeSafeBindingAndTupleGet) {
    sqlite::connection conn(":memory:");
    sqlite::execute(
        conn,
        "CREATE TABLE events(id INTEGER PRIMARY KEY, happened INTEGER, note TEXT, flag INTEGER);",
        true);

    sqlite::command insert(conn,
                           "INSERT INTO events(id, happened, note, flag) VALUES (?, ?, ?, ?);");
    auto now =
        std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    insert % 1;
    insert % now;
    insert % std::optional<std::string>("typed");
    insert % std::optional<int>();
    insert.step_once();

    sqlite::query q(conn, "SELECT id, happened, note, flag FROM events;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    auto row = res->get_tuple<std::int64_t, std::chrono::system_clock::time_point,
                              std::optional<std::string>, std::optional<int>>();

    EXPECT_EQ(std::get<0>(row), 1);
    EXPECT_TRUE(std::get<2>(row).has_value());
    EXPECT_EQ(*std::get<2>(row), "typed");
    EXPECT_FALSE(std::get<3>(row).has_value());
    EXPECT_EQ(std::get<1>(row).time_since_epoch(), now.time_since_epoch());

    sqlite::query range_query(conn, "SELECT id, happened, note, flag FROM events;");
    int row_count = 0;
    for (auto &row_view : range_query.each()) {
        EXPECT_EQ(row_view.get<std::int64_t>("id"), 1);
        auto note_opt = row_view.get<std::optional<std::string>>("note");
        EXPECT_TRUE(note_opt.has_value());
        ++row_count;
    }
    EXPECT_EQ(row_count, 1);
}

TEST(CommandQueryTest, VariadicOperatorBindsParameters) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE variadic(k INTEGER, v TEXT, flag INTEGER);", true);

    sqlite::command insert(conn, "INSERT INTO variadic(k, v, flag) VALUES(?, ?, ?);");
    insert(1, std::string("one"), sqlite::nil);
    insert(2, std::string_view("two"), 1);

    sqlite::query q(conn, "SELECT k, v, flag FROM variadic ORDER BY k;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    EXPECT_EQ(res->get<std::string>(1), "one");
    EXPECT_TRUE(res->is_null(2));
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    EXPECT_EQ(res->get<std::string_view>(1), "two");
    EXPECT_EQ(res->get<int>(2), 1);
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, NamedPlaceholdersWorkWithPercentAndCallOperator) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE named(id INTEGER PRIMARY KEY, tag TEXT, body TEXT);", true);

    sqlite::command insert(conn, "INSERT INTO named(id, tag, body) VALUES(:id, @tag, $body);");
    insert % sqlite::named(":id", 1) % sqlite::named("@tag", "docs") %
        sqlite::named("$body", std::string("alpha"));
    insert();

    sqlite::command insert_all(conn, "INSERT INTO named(id, tag, body) VALUES(:id, @tag, $body);");
    insert_all(sqlite::named(":id", 2), sqlite::named("@tag", std::string_view("code")),
               sqlite::named("$body", "bravo"));

    sqlite::query q(conn, "SELECT tag, body FROM named ORDER BY id;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "docs");
    EXPECT_EQ(res->get<std::string>(1), "alpha");
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "code");
    EXPECT_EQ(res->get<std::string_view>(1), "bravo");
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, VariadicOperatorRespectsExistingBindings) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE variadic_mix(k INTEGER, v TEXT, flag INTEGER);", true);

    sqlite::command insert(conn, "INSERT INTO variadic_mix(k, v, flag) VALUES(?, ?, ?);");
    insert % 10;
    insert(std::string("prefix-bound"), 0);

    sqlite::command insert_all(conn, "INSERT INTO variadic_mix(k, v, flag) VALUES(?, ?, ?);");
    insert_all(11, "all-variadic", 1);

    sqlite::query q(conn, "SELECT k, v, flag FROM variadic_mix ORDER BY k;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 10);
    EXPECT_EQ(res->get<std::string>(1), "prefix-bound");
    EXPECT_EQ(res->get<int>(2), 0);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 11);
    EXPECT_EQ(res->get<std::string_view>(1), "all-variadic");
    EXPECT_EQ(res->get<int>(2), 1);
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, QueryEachBindsVariadicArguments) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE notes(id INTEGER PRIMARY KEY, tag TEXT, body TEXT);", true);
    sqlite::execute insert(conn, "INSERT INTO notes(tag, body) VALUES(?, ?);");
    insert % "docs" % "first";
    insert.step_once();
    insert.clear();
    insert % "code" % "second";
    insert.step_once();
    insert.clear();
    insert % "docs" % "third";
    insert.step_once();

    sqlite::query q(conn, "SELECT body FROM notes WHERE tag = ? ORDER BY id;");
    std::vector<std::string> bodies;
    for (auto &row : q.each("docs")) {
        bodies.push_back(row.get<std::string>(0));
    }
    ASSERT_EQ(bodies.size(), 2u);
    EXPECT_EQ(bodies[0], "first");
    EXPECT_EQ(bodies[1], "third");
}

TEST(CommandQueryTest, QueryEachSupportsMixedBinding) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE audit(level TEXT, message TEXT, tag TEXT);", true);
    sqlite::execute insert(conn, "INSERT INTO audit(level, message, tag) VALUES(?, ?, ?);");
    insert % "info" % "boot";
    insert % "system";
    insert.step_once();
    insert.clear();
    insert % "warn" % "slow";
    insert % "ui";
    insert.step_once();
    insert.clear();

    sqlite::query q(conn, "SELECT message FROM audit WHERE level = ? AND tag = ?;");
    q % "warn";
    std::vector<std::string> messages;
    for (auto &row : q.each("ui")) {
        messages.push_back(row.get<std::string>(0));
    }
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0], "slow");
}

TEST(CommandQueryTest, ResultCanOutliveQueryObject) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE retained(id INTEGER, note TEXT);", true);
    sqlite::execute(conn, "INSERT INTO retained VALUES(1, 'kept');", true);

    sqlite::result_type res;
    {
        sqlite::query q(conn, "SELECT id, note FROM retained;");
        res = q.get_result();
    }

    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    EXPECT_EQ(res->get<std::string>(1), "kept");
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, ExpressionDecltypeReturnsEmptyString) {
    sqlite::connection conn(":memory:");
    sqlite::query q(conn, "SELECT 1 + 1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_column_decltype(0), "");
}

TEST(CommandQueryTest, ClearRemovesPreviousBindings) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE clear_bindings(id INTEGER, note TEXT NOT NULL);", true);

    sqlite::command insert(conn, "INSERT INTO clear_bindings(id, note) VALUES(?, ?);");
    insert % 1 % "first";
    insert.step_once();
    insert.clear();

    insert % 2;
    EXPECT_THROW(insert.step_once(), sqlite::database_exception);

    sqlite::query q(conn, "SELECT id, note FROM clear_bindings;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    EXPECT_EQ(res->get<std::string>(1), "first");
    EXPECT_FALSE(res->next_row());
}

namespace {
    // Shared fixture data for the postfix increment tests.
    void fill_postfix_rows(sqlite::connection &conn) {
        sqlite::execute(
            conn,
            "CREATE TABLE postfix_data(id INTEGER PRIMARY KEY, note TEXT, ratio REAL, payload BLOB, "
            "nullable TEXT);",
            true);
        sqlite::command insert(
            conn, "INSERT INTO postfix_data(note, ratio, payload, nullable) VALUES(?, ?, ?, ?);");
        std::vector<unsigned char> blob_a{1, 2, 3};
        std::vector<unsigned char> blob_c{9, 8, 7, 6};
        insert % "first" % 0.25 % std::span<const unsigned char>(blob_a) % sqlite::nil;
        insert.step_once();
        insert.clear();
        insert % "second" % 0.5 % sqlite::nil % "set";
        insert.step_once();
        insert.clear();
        insert % "third" % 0.75 % std::span<const unsigned char>(blob_c) % sqlite::nil;
        insert.step_once();
    }
} // namespace

TEST(CommandQueryTest, PostfixIncrementYieldsRowBeforeAdvance) {
    sqlite::connection conn(":memory:");
    fill_postfix_rows(conn);

    sqlite::query q(
        conn, "SELECT id, note, ratio, payload, nullable FROM postfix_data ORDER BY id;");
    auto rows = q.each();
    auto it   = rows.begin();
    auto last = rows.end();

    ASSERT_NE(it, last);
    // Regression for #63: *it++ used to observe the row after the increment.
    EXPECT_EQ((*it++).get<int>(0), 1);
    EXPECT_EQ((*it++).get<std::string>("note"), "second");
    // The live iterator itself keeps observing the row it points at.
    EXPECT_EQ(it->get<int>(0), 3);
    // Postfix increment on the final row still yields the final row's values.
    EXPECT_EQ((*it++).get<double>("ratio"), 0.75);
    EXPECT_EQ(it, last);
}

TEST(CommandQueryTest, PostfixIncrementPreservesTypedRowValues) {
    sqlite::connection conn(":memory:");
    fill_postfix_rows(conn);
    std::vector<unsigned char> blob_a{1, 2, 3};

    sqlite::query q(
        conn, "SELECT id, note, ratio, payload, nullable FROM postfix_data ORDER BY id;");
    auto rows = q.each();
    auto it   = rows.begin();

    // Keep the postfix result as an owning row value and exhaust the iterator afterwards.
    auto first = *it++;
    ASSERT_TRUE(first.valid());
    ++it;
    ++it;
    EXPECT_EQ(first.get<std::int64_t>(0), 1);
    EXPECT_EQ(first.get<std::string_view>("note"), "first");
    EXPECT_DOUBLE_EQ(first.get<double>("ratio"), 0.25);
    EXPECT_EQ(first.get<std::optional<std::string>>("nullable"), std::nullopt);
    EXPECT_EQ(first.get<std::vector<unsigned char>>("payload"), blob_a);
    auto payload_span = first.get<std::span<const unsigned char>>("payload");
    EXPECT_TRUE(std::equal(payload_span.begin(), payload_span.end(), blob_a.begin()));
    // Cross-storage-class coercion is the documented exception: the snapshot keeps the original
    // storage class instead of applying SQLite's implicit text conversion.
    EXPECT_THROW(first.get<std::string>("ratio"), sqlite::database_exception);
    EXPECT_THROW(first.get<int>("note"), sqlite::database_exception);
    EXPECT_THROW(first.get<std::string>(42), std::out_of_range);
}

TEST(CommandQueryTest, PostfixIncrementOnEndIteratorIsHarmless) {
    sqlite::connection conn(":memory:");
    fill_postfix_rows(conn);

    sqlite::query q(
        conn, "SELECT id, note, ratio, payload, nullable FROM postfix_data ORDER BY id;");
    auto rows = q.each();
    auto it   = rows.begin();
    std::advance(it, 3);
    ASSERT_EQ(it, rows.end());

    // Incrementing the end iterator again keeps it at the end and stays dereference-safe.
    it++;
    EXPECT_EQ(it, rows.end());

    auto end_snapshot = rows.end()++;
    EXPECT_FALSE((*end_snapshot).valid());
    EXPECT_THROW((*end_snapshot).get<int>(0), std::runtime_error);
}

TEST(CommandQueryTest, ResultRangeIteratorSatisfiesInputIteratorRequirements) {
    using iterator = sqlite::query::result_range::iterator;
    static_assert(std::input_iterator<iterator>);
    static_assert(std::sentinel_for<iterator, iterator>);
    static_assert(std::ranges::input_range<sqlite::query::result_range>);

    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE nums(v INTEGER);", true);
    sqlite::execute(conn, "INSERT INTO nums VALUES(1),(2),(3),(4),(5);", true);

    sqlite::query q(conn, "SELECT v FROM nums;");
    auto rows  = q.each();
    auto first = rows.begin();
    auto last  = rows.end();
    int sum    = std::accumulate(
        first, last, 0,
        [](int acc, sqlite::query::result_range::row_view const &row) {
            return acc + row.get<int>(0);
        });
    EXPECT_EQ(sum, 15);

    sqlite::query distance_query(conn, "SELECT v FROM nums;");
    auto distance_rows = distance_query.each();
    EXPECT_EQ(std::distance(distance_rows.begin(), distance_rows.end()), 5);

    sqlite::query count_query(conn, "SELECT v FROM nums;");
    auto count_rows = count_query.each();
    EXPECT_EQ(std::count_if(count_rows.begin(),
                            count_rows.end(),
                            [](sqlite::query::result_range::row_view const &row) {
                                return row.get<int>(0) % 2 == 0;
                            }),
              2);
}

TEST(CommandQueryTest, ResetRewindsPartiallyConsumedCursor) {
    sqlite::connection conn(":memory:");
    sqlite::query q(conn, "SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);

    res->reset();

    // The cursor restarts from the first row instead of continuing with row 3.
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 3);
    EXPECT_FALSE(res->next_row());
    EXPECT_TRUE(res->end());
}

TEST(CommandQueryTest, ResetBeforeFirstStepRestartsQuery) {
    sqlite::connection conn(":memory:");
    sqlite::query q(conn, "SELECT 3 UNION ALL SELECT 4");
    auto res = q.get_result();

    res->reset();

    EXPECT_FALSE(res->end());
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 3);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 4);
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, ResetAfterExhaustionRestartsQuery) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE restart(id INTEGER);", true);
    sqlite::execute(conn, "INSERT INTO restart VALUES(10),(20);", true);
    sqlite::query q(conn, "SELECT id FROM restart ORDER BY id;");
    auto res = q.get_result();

    std::vector<int> first_pass;
    while (res->next_row()) {
        first_pass.push_back(res->get<int>(0));
    }
    ASSERT_EQ(first_pass.size(), 2u);
    EXPECT_TRUE(res->end());

    // reset() must revive the cursor after SQLITE_DONE.
    res->reset();
    EXPECT_FALSE(res->end());

    std::vector<int> second_pass;
    while (res->next_row()) {
        second_pass.push_back(res->get<int>(0));
    }
    EXPECT_EQ(second_pass, first_pass);
    EXPECT_TRUE(res->end());
}

TEST(CommandQueryTest, ResetPreservesBoundParameters) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE bound(id INTEGER);", true);
    sqlite::execute(conn, "INSERT INTO bound VALUES(1),(2),(3);", true);

    sqlite::query q(conn, "SELECT id FROM bound WHERE id > ? ORDER BY id;");
    q % 1;
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 3);
    EXPECT_FALSE(res->next_row());

    res->reset();

    // Bindings survive the rewind, so the same filtered rows come back.
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 3);
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, ResetAfterSteppingErrorReportsErrorAndKeepsCursorUsable) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(
        conn, "fail_once", [failed = false]() mutable -> std::int64_t {
            if (!failed) {
                failed = true;
                throw sqlite::database_exception("fail_once evaluation failed");
            }
            return 7;
        });

    sqlite::query q(conn, "SELECT fail_once() UNION ALL SELECT 8;");
    auto res = q.get_result();
    EXPECT_THROW(res->next_row(), sqlite::database_exception);

    // reset() reports the pending error of the failed evaluation ...
    EXPECT_THROW(res->reset(), sqlite::database_exception);
    // ... but the statement was still reset, so a second reset succeeds and the
    // cursor can be iterated from the beginning.
    res->reset();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 7);
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 8);
    EXPECT_FALSE(res->next_row());
}

TEST(CommandQueryTest, ResetRewindsResultsSharingTheSameStatement) {
    sqlite::connection conn(":memory:");
    sqlite::query q(conn, "SELECT 1 UNION ALL SELECT 2");
    auto first  = q.get_result();
    auto second = q.get_result();

    // Both results drive the same underlying prepared statement.
    ASSERT_TRUE(first->next_row());
    EXPECT_EQ(first->get<int>(0), 1);
    ASSERT_TRUE(second->next_row());
    EXPECT_EQ(second->get<int>(0), 2);

    // Resetting one result rewinds the shared cursor for the other as well.
    first->reset();
    ASSERT_TRUE(second->next_row());
    EXPECT_EQ(second->get<int>(0), 1);
}
