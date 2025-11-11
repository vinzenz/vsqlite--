#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/json_fts.hpp>
#include <sqlite/query.hpp>

#include <array>
#include <optional>
#include <span>
#include <string_view>

using namespace testhelpers;

TEST(CommandQueryTest, BindsAndRetrievesData) {
    sqlite::connection conn(":memory:");
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
    insert.emit();
    insert.clear();

    std::string_view world_view("world_view");
    insert % 7 % static_cast<std::int64_t>(9007199254740993LL) % world_view % 2.71 %
        std::span<const unsigned char>(blob_view_data) % std::string("value");
    insert();

    sqlite::query q(conn, "SELECT id, ival, lval, note, amount, data, nullable FROM sample ORDER BY id;");
    auto result = q.get_result();
    ASSERT_TRUE(result->next_row());
    EXPECT_EQ(result->get_column_count(), 7);
    EXPECT_EQ(result->get_column_name(0), "id");
    auto declared_type = result->get_column_decltype(2);
    EXPECT_TRUE(declared_type == "BIGINT" || declared_type == "INTEGER");
    EXPECT_EQ(result->get_int(1), 42);
    EXPECT_EQ(result->get_int64(2), static_cast<std::int64_t>(1) << 40);
    EXPECT_EQ(result->get_string(3), "hello");
    EXPECT_DOUBLE_EQ(result->get_double(4), 3.14);
    auto note_view = result->get_string_view(3);
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
    auto string_view_second = result->get_string_view(3);
    EXPECT_EQ(string_view_second, world_view);
    auto blob_span_second = result->get_binary_span(5);
    EXPECT_EQ(blob_span_second.size(), blob_view_data.size());
    EXPECT_TRUE(std::equal(blob_span_second.begin(), blob_span_second.end(), blob_view_data.begin()));
    EXPECT_EQ(result->get_variant(4).index(), 3);
    auto blob_variant = result->get_variant(5);
    EXPECT_TRUE(std::holds_alternative<sqlite::blob_ref_t>(blob_variant));
    EXPECT_FALSE(result->next_row());
    EXPECT_TRUE(result->end());
    result->reset();
    EXPECT_TRUE(result->next_row());

    sqlite::query count_query(conn, "SELECT COUNT(*) FROM sample;");
    int total = -1;
    for(auto & row : count_query.each()) {
        total = row.get<int>(0);
    }
    EXPECT_EQ(total, 2);
}

TEST(CommandQueryTest, TypeSafeBindingAndTupleGet) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE events(id INTEGER PRIMARY KEY, happened INTEGER, note TEXT, flag INTEGER);", true);

    sqlite::command insert(conn, "INSERT INTO events(id, happened, note, flag) VALUES (?, ?, ?, ?);");
    auto now = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    insert % 1;
    insert % now;
    insert % std::optional<std::string>("typed");
    insert % std::optional<int>();
    insert.emit();

    sqlite::query q(conn, "SELECT id, happened, note, flag FROM events;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    auto row = res->get_tuple<
        std::int64_t,
        std::chrono::system_clock::time_point,
        std::optional<std::string>,
        std::optional<int>
    >();

    EXPECT_EQ(std::get<0>(row), 1);
    EXPECT_TRUE(std::get<2>(row).has_value());
    EXPECT_EQ(*std::get<2>(row), "typed");
    EXPECT_FALSE(std::get<3>(row).has_value());
    EXPECT_EQ(std::get<1>(row).time_since_epoch(), now.time_since_epoch());

    sqlite::query range_query(conn, "SELECT id, happened, note, flag FROM events;");
    int row_count = 0;
    for(auto & row_view : range_query.each()) {
        EXPECT_EQ(row_view.get<std::int64_t>("id"), 1);
        auto note_opt = row_view.get<std::optional<std::string>>("note");
        EXPECT_TRUE(note_opt.has_value());
        ++row_count;
    }
    EXPECT_EQ(row_count, 1);
}
