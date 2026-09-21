#include "test_common.hpp"
#include <iostream>
#include <format>
#include <string>
#include <utility>
#include <vector>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/json_fts.hpp>
#include <sqlite3.h>

using namespace testhelpers;

TEST(JsonFtsHelpersTest, JsonContainsHelper) {
    sqlite::connection conn(":memory:");
    if (!sqlite::json::available(conn)) {
        GTEST_SKIP() << "JSON1 extension is not available in this build.";
    }
    try {
        sqlite::json::register_contains_function(conn);
    } catch (sqlite::database_exception const &ex) {
        GTEST_SKIP() << "JSON1 extension not usable: " << ex.what();
    }
    sqlite::execute(conn, "CREATE TABLE docs(payload JSON);", true);
    sqlite::command insert(conn, "INSERT INTO docs(payload) VALUES (?);");
    insert % std::string(R"({"tags":["vsqlite","cpp"]})");
    insert.step_once();

    sqlite::query q(conn, "SELECT json_contains_value(payload, '$.tags[0]', 'vsqlite') FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 1);
}

TEST(JsonFtsHelpersTest, FtsRankFunction) {
    sqlite::connection conn(":memory:");
    if (!sqlite::fts::available(conn)) {
        GTEST_SKIP() << "FTS5 extension is not available in this build.";
    }
    sqlite::execute(conn, "CREATE VIRTUAL TABLE docs USING fts5(body);", true);
    sqlite::json::register_contains_function(conn); // ensure coexistence
    try {
        sqlite::fts::register_rank_function(conn);
    } catch (sqlite::database_exception const &ex) {
        GTEST_SKIP() << "FTS5 extension not usable: " << ex.what();
    }

    sqlite::execute(conn, "INSERT INTO docs(body) VALUES ('hello world'), ('hello sqlite');", true);
    try {
        sqlite::query q(conn, "SELECT fts_rank(docs) FROM docs WHERE docs MATCH 'hello';");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_GE(res->get<double>(0), 0.0);
    } catch (sqlite::database_exception const &ex) {
        GTEST_SKIP() << "FTS5 matchinfo/fts_rank unavailable: " << ex.what();
    }
}

TEST(JsonFtsHelpersTest, JsonPathBuilderEscapesKeysWithJsonRules) {
    // Quoted segments use JSON escaping: backslash and double quote are
    // escaped, control characters become \u00XX, everything else (including
    // multi-byte UTF-8) passes through unchanged.
    EXPECT_EQ(sqlite::json::path().key("plain").str(), "$.plain");
    EXPECT_EQ(sqlite::json::path().key("a\"b").str(), "$.\"a\\\"b\"");
    EXPECT_EQ(sqlite::json::path().key("a\\b").str(), "$.\"a\\\\b\"");
    EXPECT_EQ(sqlite::json::path().key("a\tb").str(), "$.\"a\\u0009b\"");
    EXPECT_EQ(sqlite::json::path().key("").str(), "$.\"\"");
    EXPECT_EQ(sqlite::json::path().key("h\xc3\xa9llo").str(), "$.\"h\xc3\xa9llo\"");
}

TEST(JsonFtsHelpersTest, JsonPathBoundLookupMatchesSpecialKeys) {
    sqlite::connection conn(":memory:");
    if (!sqlite::json::available(conn)) {
        GTEST_SKIP() << "JSON1 extension is not available in this build.";
    }
    std::string const doc =
        "{\"a\\\"b\":2,\"a\\\\b\":3,\"\":\"empty\",\"a.b\":\"dot\",\"a[0]\":\"bracket\","
        "\"a\\tb\":41,\"h\\u00e9llo\":42}";
    std::vector<std::pair<std::string, std::string>> const cases = {
        {"a\"b", "2"},       {"a\\b", "3"},  {"", "empty"},          {"a.b", "dot"},
        {"a[0]", "bracket"}, {"a\tb", "41"}, {"h\xc3\xa9llo", "42"},
    };
    // SQLite's JSON path parser skips backslash escapes inside quoted labels only since
    // 3.47.0; older builds end the label at the escaped quote and the lookup yields NULL.
    bool const escaped_quote_supported = sqlite3_libversion_number() >= 3047000;
    for (auto const &[key, expected] : cases) {
        if (!escaped_quote_supported && key.find('"') != std::string::npos) {
            std::cout << "skipping key containing a double quote: SQLite "
                      << sqlite3_libversion()
                      << " does not resolve escaped quotes in JSON path labels\n";
            continue;
        }
        auto path = sqlite::json::path().key(key);
        sqlite::query q(conn, "SELECT json_extract(?, ?);");
        q % doc % path.str();
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row()) << "key: " << key;
        EXPECT_EQ(res->get<std::string>(0), expected) << "key: " << key;
    }
}

TEST(JsonFtsHelpersTest, ExpressionHelpersEscapeApostrophesInPaths) {
    sqlite::connection conn(":memory:");
    if (!sqlite::json::available(conn)) {
        GTEST_SKIP() << "JSON1 extension is not available in this build.";
    }
    std::string const doc = "{\"O'Reilly\":1,\"O''Reilly\":2}";

    auto single = sqlite::json::path().key("O'Reilly");
    EXPECT_EQ(sqlite::json::extract_expression("?", single), "json_extract(?, '$.\"O''Reilly\"')");

    {
        sqlite::query q(conn, "SELECT " + sqlite::json::extract_expression("?", single) + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    {
        sqlite::query q(conn, "SELECT json_extract(?, ?);");
        q % doc % single.str();
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    {
        sqlite::query q(conn,
                        "SELECT " + sqlite::json::contains_expression("?", single, "1") + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }

    // A key with a repeated apostrophe must address the two-apostrophe key,
    // not silently fall back to the single-apostrophe key.
    auto doubled = sqlite::json::path().key("O''Reilly");
    {
        sqlite::query q(conn, "SELECT " + sqlite::json::extract_expression("?", doubled) + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 2);
    }
    {
        sqlite::query q(conn,
                        "SELECT " + sqlite::json::contains_expression("?", doubled, "2") + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
    {
        sqlite::query q(conn, "SELECT json_extract(?, ?);");
        q % doc % doubled.str();
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 2);
    }
}

TEST(JsonFtsHelpersTest, ExpressionHelpersKeepSqlPunctuationInsideKey) {
    sqlite::connection conn(":memory:");
    if (!sqlite::json::available(conn)) {
        GTEST_SKIP() << "JSON1 extension is not available in this build.";
    }
    // A key built from SQL punctuation must remain data: it can neither break
    // the generated SQL nor redirect the expression at other keys.
    std::string const key = "', x) OR ('1'='1";
    std::string const doc = "{\"x\":999,\"', x) OR ('1'='1\":7}";
    auto path             = sqlite::json::path().key(key);

    EXPECT_EQ(sqlite::json::extract_expression("?", path),
              "json_extract(?, '$.\"'', x) OR (''1''=''1\"')");

    int via_expression = 0;
    {
        sqlite::query q(conn, "SELECT " + sqlite::json::extract_expression("?", path) + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        via_expression = res->get<int>(0);
    }
    int via_parameter = 0;
    {
        sqlite::query q(conn, "SELECT json_extract(?, ?);");
        q % doc % path.str();
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        via_parameter = res->get<int>(0);
    }
    EXPECT_EQ(via_expression, 7);
    EXPECT_EQ(via_expression, via_parameter);

    {
        sqlite::query q(conn, "SELECT " + sqlite::json::contains_expression("?", path, "7") + ";");
        q % doc;
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
    }
}
