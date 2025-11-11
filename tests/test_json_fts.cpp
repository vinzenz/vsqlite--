#include "test_common.hpp"
#include <iostream>
#include <format>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/json_fts.hpp>

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
