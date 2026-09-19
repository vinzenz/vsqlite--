#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>

#include <cstddef>
#include <span>

using namespace testhelpers;

TEST(FunctionTest, RegistersScalarFunction) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "double_int", [](int value) { return value * 2; },
                            {.deterministic = true});

    sqlite::query q(conn, "SELECT double_int(21);");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 42);
}

TEST(FunctionTest, OptionalAndBlobArguments) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "blob_size",
                            [](std::optional<std::vector<unsigned char>> blob) -> int {
                                return blob ? static_cast<int>(blob->size()) : -1;
                            });
    sqlite::command insert(conn, "SELECT blob_size(?);");
    std::vector<unsigned char> data{1, 2, 3};
    insert % data;
    EXPECT_TRUE(insert.step_once());
}

TEST(FunctionTest, EmptyTextResultsAreZeroLengthText) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "empty_string_view", [] { return std::string_view{}; });
    sqlite::create_function(conn, "empty_string", [] { return std::string{}; });

    sqlite::query q(conn,
                    "SELECT empty_string_view() IS NULL, typeof(empty_string_view()),"
                    " length(empty_string_view()),"
                    " empty_string() IS NULL, typeof(empty_string()), length(empty_string());");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 0);
    EXPECT_EQ(res->get<std::string>(1), "text");
    EXPECT_EQ(res->get<int>(2), 0);
    EXPECT_EQ(res->get<int>(3), 0);
    EXPECT_EQ(res->get<std::string>(4), "text");
    EXPECT_EQ(res->get<int>(5), 0);
}

TEST(FunctionTest, EmptyBlobResultsAreZeroLengthBlobs) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "empty_byte_vector", [] { return std::vector<unsigned char>{}; });
    sqlite::create_function(conn, "empty_byte_span",
                            [] { return std::span<const unsigned char>{}; });
    sqlite::create_function(conn, "empty_std_byte_vector", [] { return std::vector<std::byte>{}; });
    sqlite::create_function(conn, "empty_std_byte_span",
                            [] { return std::span<const std::byte>{}; });

    sqlite::query q(conn, "SELECT empty_byte_vector() IS NULL, typeof(empty_byte_vector()),"
                          " length(empty_byte_vector()),"
                          " empty_byte_span() IS NULL, typeof(empty_byte_span()),"
                          " length(empty_byte_span()),"
                          " empty_std_byte_vector() IS NULL, typeof(empty_std_byte_vector()),"
                          " length(empty_std_byte_vector()),"
                          " empty_std_byte_span() IS NULL, typeof(empty_std_byte_span()),"
                          " length(empty_std_byte_span());");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 0);
    EXPECT_EQ(res->get<std::string>(1), "blob");
    EXPECT_EQ(res->get<int>(2), 0);
    EXPECT_EQ(res->get<int>(3), 0);
    EXPECT_EQ(res->get<std::string>(4), "blob");
    EXPECT_EQ(res->get<int>(5), 0);
    EXPECT_EQ(res->get<int>(6), 0);
    EXPECT_EQ(res->get<std::string>(7), "blob");
    EXPECT_EQ(res->get<int>(8), 0);
    EXPECT_EQ(res->get<int>(9), 0);
    EXPECT_EQ(res->get<std::string>(10), "blob");
    EXPECT_EQ(res->get<int>(11), 0);
}

TEST(FunctionTest, EngagedEmptyOptionalIsNotNullAndDisengagedIsNull) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "engaged_empty_text",
                            []() -> std::optional<std::string_view> { return std::string_view{}; });
    sqlite::create_function(
        conn, "engaged_empty_blob",
        []() -> std::optional<std::vector<unsigned char>> { return std::vector<unsigned char>{}; });
    sqlite::create_function(conn, "disengaged_text",
                            []() -> std::optional<std::string_view> { return std::nullopt; });

    sqlite::query q(conn, "SELECT engaged_empty_text() IS NULL, typeof(engaged_empty_text()),"
                          " engaged_empty_blob() IS NULL, typeof(engaged_empty_blob()),"
                          " disengaged_text() IS NULL, typeof(disengaged_text());");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 0);
    EXPECT_EQ(res->get<std::string>(1), "text");
    EXPECT_EQ(res->get<int>(2), 0);
    EXPECT_EQ(res->get<std::string>(3), "blob");
    EXPECT_EQ(res->get<int>(4), 1);
    EXPECT_EQ(res->get<std::string>(5), "null");
}

TEST(FunctionTest, NonEmptyTextResultsKeepValue) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "text_view_result", [] { return std::string_view("view"); });
    sqlite::create_function(conn, "string_result", [] { return std::string("owned"); });
    sqlite::create_function(conn, "optional_text_result", []() -> std::optional<std::string_view> {
        return std::string_view("maybe");
    });

    sqlite::query q(conn, "SELECT typeof(text_view_result()), text_view_result(),"
                          " typeof(string_result()), string_result(),"
                          " typeof(optional_text_result()), optional_text_result();");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "text");
    EXPECT_EQ(res->get<std::string>(1), "view");
    EXPECT_EQ(res->get<std::string>(2), "text");
    EXPECT_EQ(res->get<std::string>(3), "owned");
    EXPECT_EQ(res->get<std::string>(4), "text");
    EXPECT_EQ(res->get<std::string>(5), "maybe");
}

TEST(FunctionTest, NonEmptyBlobResultsKeepValue) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "byte_vector_result",
                            [] { return std::vector<unsigned char>{1, 2, 3}; });
    sqlite::create_function(conn, "byte_span_result", [] {
        static constexpr unsigned char data[] = {4, 5, 6};
        return std::span<const unsigned char>(data);
    });
    sqlite::create_function(conn, "std_byte_vector_result",
                            [] { return std::vector<std::byte>{std::byte{7}, std::byte{8}}; });
    sqlite::create_function(conn, "std_byte_span_result", [] {
        static constexpr std::byte data[] = {std::byte{9}};
        return std::span<const std::byte>(data);
    });

    sqlite::query q(conn,
                    "SELECT typeof(byte_vector_result()), length(byte_vector_result()),"
                    " byte_vector_result(),"
                    " typeof(byte_span_result()), length(byte_span_result()), byte_span_result(),"
                    " typeof(std_byte_vector_result()), length(std_byte_vector_result()),"
                    " std_byte_vector_result(),"
                    " typeof(std_byte_span_result()), length(std_byte_span_result()),"
                    " std_byte_span_result();");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "blob");
    EXPECT_EQ(res->get<int>(1), 3);
    EXPECT_EQ(res->get<std::vector<unsigned char>>(2), (std::vector<unsigned char>{1, 2, 3}));
    EXPECT_EQ(res->get<std::string>(3), "blob");
    EXPECT_EQ(res->get<int>(4), 3);
    EXPECT_EQ(res->get<std::vector<unsigned char>>(5), (std::vector<unsigned char>{4, 5, 6}));
    EXPECT_EQ(res->get<std::string>(6), "blob");
    EXPECT_EQ(res->get<int>(7), 2);
    EXPECT_EQ(res->get<std::vector<unsigned char>>(8), (std::vector<unsigned char>{7, 8}));
    EXPECT_EQ(res->get<std::string>(9), "blob");
    EXPECT_EQ(res->get<int>(10), 1);
    EXPECT_EQ(res->get<std::vector<unsigned char>>(11), (std::vector<unsigned char>{9}));
}

namespace {

/// Move-only callable whose destructor increments `destroyed` exactly once.
/// Moved-from instances hand the counter over and stay silent.
struct tracked_callable {
    explicit tracked_callable(int *destroyed_count) : destroyed(destroyed_count) {}

    tracked_callable(tracked_callable &&other) noexcept : destroyed(other.destroyed) {
        other.destroyed = nullptr;
    }
    tracked_callable(tracked_callable const &)            = delete;
    tracked_callable &operator=(tracked_callable const &) = delete;
    tracked_callable &operator=(tracked_callable &&)      = delete;

    ~tracked_callable() {
        if (destroyed) {
            ++*destroyed;
        }
    }

    int operator()() const {
        return 1;
    }

    int *destroyed;
};

} // namespace

TEST(FunctionTest, OverlongFunctionNameIsCatchable) {
    sqlite::connection conn(":memory:");
    int destroyed = 0;
    try {
        sqlite::create_function(conn, std::string(256, 'x'), tracked_callable{&destroyed});
        FAIL() << "Expected registration of an overlong function name to fail.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_EQ(ex.error_code(), SQLITE_MISUSE);
        EXPECT_EQ(destroyed, 1);
    }
}

TEST(FunctionTest, ReplacementDestroysPreviousCallbackOnce) {
    int first_destroyed  = 0;
    int second_destroyed = 0;
    {
        sqlite::connection conn(":memory:");
        sqlite::create_function(conn, "tracker", tracked_callable{&first_destroyed});
        sqlite::create_function(conn, "tracker", tracked_callable{&second_destroyed});
        EXPECT_EQ(first_destroyed, 1);
        EXPECT_EQ(second_destroyed, 0);

        sqlite::query q(conn, "SELECT tracker();");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    EXPECT_EQ(second_destroyed, 1);
}

TEST(FunctionTest, ConnectionCloseDestroysCallbackOnce) {
    int destroyed = 0;
    {
        sqlite::connection conn(":memory:");
        sqlite::create_function(conn, "tracker", tracked_callable{&destroyed});
        EXPECT_EQ(destroyed, 0);
    }
    EXPECT_EQ(destroyed, 1);
}
