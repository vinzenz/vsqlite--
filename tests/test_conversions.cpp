#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/query.hpp>
#include <sqlite/result.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace testhelpers;

// The tests in this file pin the conversion contract documented in
// docs/conversions.md: the same value must survive command binding, result
// extraction, and SQL function callbacks with identical meaning, NULL and
// empty values must stay distinct, and the legacy coercions must remain
// observable so that any future change is visible.

namespace {

// Reads the single column produced by statements like "SELECT ?;".
template <typename T, typename... Args>
T scalar(sqlite::connection &con, std::string const &sql, Args &&...args) {
    sqlite::query q(con, sql);
    ((q % std::forward<Args>(args)), ...);
    auto res = q.get_result();
    if (!res->next_row()) {
        throw std::runtime_error("statement returned no row: " + sql);
    }
    return res->get<T>(0);
}

} // namespace

TEST(ConversionCrossPathTest, ValuesAgreeAcrossBindingExtractionAndCallbacks) {
    sqlite::connection conn(":memory:");
    sqlite::execute(
        conn, "CREATE TABLE items(i32, i64, dbl, txt, ucblob, byteblob, opt_txt, opt_blob);", true);

    // One value set bound through command binding.
    std::string const text_owned("hello");
    std::vector<unsigned char> const blob_uchar{1, 2, 3};
    std::vector<std::byte> const blob_byte{std::byte{9}, std::byte{8}, std::byte{7}};
    std::vector<unsigned char> const blob_uchar_from_byte{9, 8, 7};
    std::optional<std::string> const opt_text("optional");
    std::optional<std::vector<unsigned char>> const opt_blob(blob_uchar);

    sqlite::command insert(
        conn, "INSERT INTO items(i32, i64, dbl, txt, ucblob, byteblob, opt_txt, opt_blob) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    insert % 42 % (static_cast<std::int64_t>(1) << 40) % 2.5 % text_owned %
        std::span<const unsigned char>(blob_uchar) % std::span<const std::byte>(blob_byte) %
        opt_text % opt_blob;
    insert();

    // The same value set returned through SQL function callbacks.
    sqlite::create_function(conn, "ident_int", [](int v) { return v; });
    sqlite::create_function(conn, "ident_int64", [](std::int64_t v) { return v; });
    sqlite::create_function(conn, "ident_double", [](double v) { return v; });
    sqlite::create_function(conn, "ident_string", [](std::string v) { return v; });
    sqlite::create_function(conn, "ident_string_view", [](std::string_view v) { return v; });
    sqlite::create_function(
        conn, "ident_opt_text",
        [](std::optional<std::string_view> v) -> std::optional<std::string_view> { return v; });
    sqlite::create_function(conn, "ident_uchar_span",
                            [](std::span<const unsigned char> v) { return v; });
    sqlite::create_function(conn, "ident_byte_span",
                            [](std::span<const std::byte> v) { return v; });
    sqlite::create_function(conn, "ident_uchar_vector",
                            [](std::vector<unsigned char> v) { return v; });
    sqlite::create_function(conn, "ident_byte_vector", [](std::vector<std::byte> v) { return v; });
    sqlite::create_function(conn, "ident_opt_blob",
                            [](std::optional<std::vector<unsigned char>> v)
                                -> std::optional<std::vector<unsigned char>> { return v; });

    sqlite::query q(conn,
                    "SELECT i32, i64, dbl, txt, ucblob, byteblob, opt_txt, opt_blob FROM items;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());

    // Extraction path, owning and borrowed access alike.
    EXPECT_EQ(res->get<int>(0), 42);
    EXPECT_EQ(res->get<std::int64_t>(1), static_cast<std::int64_t>(1) << 40);
    EXPECT_DOUBLE_EQ(res->get<double>(2), 2.5);
    EXPECT_EQ(res->get<std::string>(3), text_owned);
    EXPECT_EQ(res->get<std::string_view>(3), std::string_view(text_owned));
    EXPECT_EQ(res->get<std::vector<unsigned char>>(4), blob_uchar);
    EXPECT_EQ(res->get<std::span<const unsigned char>>(4).size(), blob_uchar.size());
    EXPECT_EQ(res->get<std::span<const unsigned char>>(4)[0], blob_uchar[0]);
    auto byte_blob = res->get<std::span<const std::byte>>(5);
    ASSERT_EQ(byte_blob.size(), blob_byte.size());
    EXPECT_EQ(byte_blob[0], blob_byte[0]);
    EXPECT_EQ(res->get<std::optional<std::string>>(6), opt_text);
    EXPECT_EQ(res->get<std::optional<std::vector<unsigned char>>>(7), opt_blob);

    // Callback path: same values in, same values out.
    EXPECT_EQ(scalar<int>(conn, "SELECT ident_int(?);", 42), 42);
    EXPECT_EQ(
        scalar<std::int64_t>(conn, "SELECT ident_int64(?);", static_cast<std::int64_t>(1) << 40),
        static_cast<std::int64_t>(1) << 40);
    EXPECT_DOUBLE_EQ(scalar<double>(conn, "SELECT ident_double(?);", 2.5), 2.5);
    EXPECT_EQ(scalar<std::string>(conn, "SELECT ident_string(?);", text_owned), text_owned);
    EXPECT_EQ(
        scalar<std::string>(conn, "SELECT ident_string_view(?);", std::string_view(text_owned)),
        text_owned);
    EXPECT_EQ(scalar<std::optional<std::string>>(conn, "SELECT ident_opt_text(?);",
                                                 std::optional<std::string_view>(*opt_text)),
              opt_text);
    EXPECT_EQ(scalar<std::vector<unsigned char>>(conn, "SELECT ident_uchar_vector(?);",
                                                 std::span<const unsigned char>(blob_uchar)),
              blob_uchar);
    EXPECT_EQ(scalar<std::vector<unsigned char>>(conn, "SELECT ident_uchar_span(?);",
                                                 std::span<const unsigned char>(blob_uchar)),
              blob_uchar);
    EXPECT_EQ(scalar<std::vector<unsigned char>>(conn, "SELECT ident_byte_span(?);",
                                                 std::span<const std::byte>(blob_byte)),
              blob_uchar_from_byte);
    EXPECT_EQ(scalar<std::vector<unsigned char>>(conn, "SELECT ident_byte_vector(?);",
                                                 std::span<const std::byte>(blob_byte)),
              blob_uchar_from_byte);
    EXPECT_EQ(scalar<std::optional<std::vector<unsigned char>>>(
                  conn, "SELECT ident_opt_blob(?);",
                  std::optional<std::vector<unsigned char>>(blob_uchar)),
              opt_blob);
}

TEST(ConversionCrossPathTest, NullStaysDistinctFromEmptyTextOnAllPaths) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE texts(v);", true);

    sqlite::command insert(conn, "INSERT INTO texts(v) VALUES (?);");
    insert % sqlite::nil; // SQL NULL
    insert();
    insert.clear();
    // A default-constructed string_view has a null data pointer; the shared
    // empty-value rule must still bind it as present, empty text.
    insert % std::string_view{};
    insert();
    insert.clear();
    insert % std::optional<std::string_view>(); // disengaged optional is NULL
    insert();
    insert.clear();
    insert % std::optional<std::string_view>(std::string_view{}); // engaged empty stays empty
    insert();

    // Callback path with a nullable identity function.
    sqlite::create_function(
        conn, "ident_nullable_text",
        [](std::optional<std::string_view> v) -> std::optional<std::string_view> { return v; });

    sqlite::query q(conn, "SELECT v IS NULL, typeof(v), length(v), v FROM texts;");
    auto res = q.get_result();

    ASSERT_TRUE(res->next_row()); // the NULL row
    EXPECT_TRUE(res->get<bool>(0));
    EXPECT_EQ(res->get<std::string>(1), "null");
    EXPECT_FALSE(res->get<std::optional<std::string_view>>(3).has_value());

    ASSERT_TRUE(res->next_row()); // the empty text row
    EXPECT_FALSE(res->get<bool>(0));
    EXPECT_EQ(res->get<std::string>(1), "text");
    EXPECT_EQ(res->get<int>(2), 0);
    auto empty_text = res->get<std::optional<std::string_view>>(3);
    ASSERT_TRUE(empty_text.has_value());
    EXPECT_TRUE(empty_text->empty());

    ASSERT_TRUE(res->next_row()); // the disengaged optional row
    EXPECT_TRUE(res->get<bool>(0));

    ASSERT_TRUE(res->next_row()); // the engaged empty optional row
    EXPECT_FALSE(res->get<bool>(0));
    EXPECT_EQ(res->get<std::string>(1), "text");
    EXPECT_EQ(res->get<int>(2), 0);

    // NULL in, NULL out; empty text in, empty text out through the callback.
    EXPECT_EQ(scalar<int>(conn, "SELECT ident_nullable_text(?) IS NULL;", sqlite::nil), 1);
    EXPECT_EQ(scalar<int>(conn, "SELECT ident_nullable_text(?) IS NULL;", std::string_view{}), 0);
    EXPECT_EQ(
        scalar<std::string>(conn, "SELECT typeof(ident_nullable_text(?));", std::string_view{}),
        "text");
    EXPECT_EQ(scalar<int>(conn, "SELECT length(ident_nullable_text(?));", std::string_view{}), 0);
    auto returned = scalar<std::optional<std::string_view>>(conn, "SELECT ident_nullable_text(?);",
                                                            std::string_view{});
    ASSERT_TRUE(returned.has_value());
    EXPECT_TRUE(returned->empty());
}

TEST(ConversionCrossPathTest, NullStaysDistinctFromEmptyBlobOnAllPaths) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE blobs(v);", true);

    std::vector<unsigned char> const empty_uchar;
    std::vector<std::byte> const empty_byte;
    std::span<const unsigned char> const empty_uchar_span{};

    sqlite::command insert(conn, "INSERT INTO blobs(v) VALUES (?);");
    insert % sqlite::nil;
    insert();
    insert.clear();
    insert % empty_uchar; // empty vector blob stays present
    insert();
    insert.clear();
    insert % empty_byte; // empty std::byte vector blob stays present
    insert();
    insert.clear();
    insert % empty_uchar_span; // empty span blob stays present
    insert();
    insert.clear();
    insert % std::optional<std::vector<unsigned char>>(empty_uchar); // engaged empty optional
    insert();

    sqlite::create_function(conn, "ident_nullable_blob",
                            [](std::optional<std::span<const unsigned char>> v)
                                -> std::optional<std::span<const unsigned char>> { return v; });

    sqlite::query q(conn, "SELECT v IS NULL, typeof(v), length(v) FROM blobs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row()); // the NULL row
    EXPECT_TRUE(res->get<bool>(0));
    EXPECT_EQ(res->get<std::string>(1), "null");
    for (int row = 1; row <= 4; ++row) { // the four empty-blob rows
        ASSERT_TRUE(res->next_row());
        EXPECT_FALSE(res->get<bool>(0)) << "row " << row;
        EXPECT_EQ(res->get<std::string>(1), "blob") << "row " << row;
        EXPECT_EQ(res->get<int>(2), 0) << "row " << row;
    }

    EXPECT_EQ(scalar<int>(conn, "SELECT ident_nullable_blob(?) IS NULL;", sqlite::nil), 1);
    EXPECT_EQ(scalar<int>(conn, "SELECT ident_nullable_blob(?) IS NULL;", empty_uchar), 0);
    EXPECT_EQ(scalar<std::string>(conn, "SELECT typeof(ident_nullable_blob(?));", empty_uchar),
              "blob");
    EXPECT_EQ(scalar<int>(conn, "SELECT length(ident_nullable_blob(?));", empty_uchar), 0);
}

TEST(ConversionLegacyTest, NullCoercionsArePinned) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE pinned(v);", true);
    sqlite::command insert(conn, "INSERT INTO pinned(v) VALUES (?);");
    insert % sqlite::nil;
    insert();

    sqlite::query q(conn, "SELECT v FROM pinned;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());

    // Legacy coercions of SQL NULL on the extraction path. These are
    // compatibility behavior, not desirable semantics; get_checked<T> rejects
    // NULL for non-optional types instead.
    EXPECT_EQ(res->get<int>(0), 0);
    EXPECT_EQ(res->get<std::int64_t>(0), 0);
    EXPECT_DOUBLE_EQ(res->get<double>(0), 0.0);
    EXPECT_FALSE(res->get<bool>(0));
    EXPECT_EQ(res->get<std::string>(0), "NULL");
    EXPECT_EQ(res->get<std::string_view>(0), std::string_view("NULL"));
    EXPECT_TRUE(res->get<std::vector<unsigned char>>(0).empty());
    EXPECT_EQ(res->get_binary_size(0), 0U);
}

TEST(ConversionLegacyTest, Int64NarrowingWrapsOnExtractionAndCallbackArguments) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE narrow(v);", true);
    // 0x80000000 does not fit int; the legacy static_cast wraps to INT32_MIN.
    sqlite::command insert(conn, "INSERT INTO narrow(v) VALUES (?);");
    insert % static_cast<std::int64_t>(2147483648LL);
    insert();

    sqlite::query q(conn, "SELECT v FROM narrow;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::int64_t>(0), 2147483648LL);
    EXPECT_EQ(res->get<int>(0), -2147483648); // pinned wrap
    EXPECT_EQ(res->get<std::int16_t>(0), 0);  // pinned wrap of the low 16 bits

    // Same legacy narrowing on SQL function arguments.
    sqlite::create_function(conn, "arg_as_int64",
                            [](int v) { return static_cast<std::int64_t>(v); });
    EXPECT_EQ(scalar<std::int64_t>(conn, "SELECT arg_as_int64(?);",
                                   static_cast<std::int64_t>(2147483648LL)),
              -2147483648LL);
}

TEST(ConversionCheckedTest, RejectsNarrowingAndSignednessLoss) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE checked(iv, dv);", true);
    sqlite::command insert(conn, "INSERT INTO checked(iv, dv) VALUES (?, ?);");
    insert % static_cast<std::int64_t>(2147483648LL) % 1e300;
    insert();
    insert.clear();
    insert % static_cast<std::int64_t>(-1) % 1.5;
    insert();

    sqlite::query q(conn, "SELECT iv, dv FROM checked ORDER BY iv DESC;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row()); // 2147483648, 1e300
    EXPECT_THROW(res->get_checked<int>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::int32_t>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::int16_t>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<float>(1), sqlite::database_exception);
    EXPECT_EQ(res->get_checked<std::int64_t>(0), 2147483648LL);
    EXPECT_DOUBLE_EQ(res->get_checked<double>(1), 1e300);

    ASSERT_TRUE(res->next_row()); // -1, 1.5
    EXPECT_THROW(res->get_checked<unsigned int>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<unsigned char>(0), sqlite::database_exception);
    EXPECT_EQ(res->get_checked<int>(0), -1);
    EXPECT_FLOAT_EQ(res->get_checked<float>(1), 1.5f);
    EXPECT_TRUE(res->get_checked<bool>(1));
}

TEST(ConversionCheckedTest, RejectsNullForNonNullableButNotForOptional) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE nulls(v);", true);
    sqlite::command insert(conn, "INSERT INTO nulls(v) VALUES (?);");
    insert % sqlite::nil;
    insert();

    sqlite::query q(conn, "SELECT v FROM nulls;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_THROW(res->get_checked<int>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<double>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<bool>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::string>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::vector<unsigned char>>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::chrono::nanoseconds>(0), sqlite::database_exception);
    EXPECT_THROW(res->get_checked<std::chrono::system_clock::time_point>(0),
                 sqlite::database_exception);
    EXPECT_EQ(res->get_checked<std::optional<int>>(0), std::nullopt);
    EXPECT_EQ(res->get_checked<std::optional<std::string>>(0), std::nullopt);
}

TEST(ConversionCheckedTest, AcceptsInRangeNarrowing) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE inrange(v);", true);
    sqlite::command insert(conn, "INSERT INTO inrange(v) VALUES (?);");
    insert % static_cast<std::int64_t>(42);
    insert();

    sqlite::query q(conn, "SELECT v FROM inrange;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_checked<int>(0), 42);
    EXPECT_EQ(res->get_checked<unsigned>(0), 42U);
    EXPECT_EQ(res->get_checked<std::int16_t>(0), 42);
    EXPECT_EQ(res->get_checked<std::uint8_t>(0), 42U);
    EXPECT_TRUE(res->get_checked<bool>(0));
}

TEST(ConversionChronoTest, NanosecondsTruncateTowardsZeroOnBindAndRead) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE times(v);", true);

    sqlite::command insert(conn, "INSERT INTO times(v) VALUES (?);");
    insert % std::chrono::nanoseconds(1234); // 1.234 us -> stored as 1 us
    insert();
    insert.clear();
    insert % std::chrono::nanoseconds(-1234); // -1.234 us -> stored as -1 us
    insert();

    sqlite::query q(conn, "SELECT v FROM times;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::int64_t>(0), 1); // bind truncation pinned
    EXPECT_EQ(res->get<std::chrono::nanoseconds>(0), std::chrono::nanoseconds(1000));
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::int64_t>(0), -1);
    EXPECT_EQ(res->get<std::chrono::nanoseconds>(0), std::chrono::nanoseconds(-1000));
}

TEST(ConversionChronoTest, MicrosecondCountOverflowWraps) {
#if defined(__SIZEOF_INT128__)
    sqlite::connection conn(":memory:");
    // 2^64 microseconds exceed the int64 storage unit. The count conversion
    // truncates like a C cast, keeping the low 64 bits: the stored value wraps.
    // Pinned here as the documented current behavior on GCC and Clang.
    using wide_micros = std::chrono::duration<__int128, std::micro>;
    wide_micros const beyond{static_cast<__int128>(1) << 64};
    EXPECT_EQ(scalar<std::int64_t>(conn, "SELECT ?;", beyond), 0);
    EXPECT_EQ(scalar<std::int64_t>(
                  conn, "SELECT ?;",
                  wide_micros{(static_cast<__int128>(1) << 64) + static_cast<__int128>(7)}),
              7);
#else
    GTEST_SKIP() << "128-bit integer support required to pin microsecond overflow wrap";
#endif
}

TEST(ConversionChronoTest, TimePointAndDurationRoundTripInMicroseconds) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE roundtrip(v);", true);

    auto const now =
        std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
    std::chrono::milliseconds const duration_value(123456); // stored as 123456000 us
    std::chrono::microseconds const negative_value(-987654);

    sqlite::command insert(conn, "INSERT INTO roundtrip(v) VALUES (?);");
    insert % now;
    insert();
    insert.clear();
    insert % duration_value;
    insert();
    insert.clear();
    insert % negative_value;
    insert();

    sqlite::query q(conn, "SELECT v FROM roundtrip;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::chrono::system_clock::time_point>(0),
              std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
    EXPECT_EQ(res->get<std::chrono::microseconds>(0), now.time_since_epoch());
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::chrono::milliseconds>(0), duration_value);
    EXPECT_EQ(res->get<std::chrono::microseconds>(0), std::chrono::microseconds(123456000));
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::chrono::microseconds>(0), negative_value);
}

TEST(ConversionBorrowedTest, OwningAccessCopiesWhileBorrowedAccessIsRowBound) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE borrowed(txt, data);", true);

    sqlite::command insert(conn, "INSERT INTO borrowed(txt, data) VALUES (?, ?);");
    insert % std::string("owned") % std::vector<unsigned char>{4, 5, 6};
    insert();

    sqlite::query q(conn, "SELECT txt, data FROM borrowed;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());

    // Borrowed views are valid for the current row only; compare before the
    // cursor advances.
    auto borrowed_text = res->get<std::string_view>(0);
    auto borrowed_blob = res->get<std::span<const unsigned char>>(1);
    EXPECT_EQ(borrowed_text, std::string_view("owned"));
    EXPECT_EQ(std::vector<unsigned char>(borrowed_blob.begin(), borrowed_blob.end()),
              (std::vector<unsigned char>{4, 5, 6}));

    // Owning conversions copy, so they stay valid after the cursor moved on.
    auto owned_text = res->get<std::string>(0);
    auto owned_blob = res->get<std::vector<unsigned char>>(1);
    EXPECT_FALSE(res->next_row());
    EXPECT_EQ(owned_text, "owned");
    EXPECT_EQ(owned_blob, (std::vector<unsigned char>{4, 5, 6}));
}
