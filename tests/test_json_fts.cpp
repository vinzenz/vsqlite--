#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/json_fts.hpp>

using namespace testhelpers;

TEST(JsonFtsHelpersTest, JsonContainsHelper) {
#if !defined(SQLITE_ENABLE_JSON1)
    GTEST_SKIP() << "JSON1 extension is not available in this build.";
#endif
    sqlite::connection conn(":memory:");
    sqlite::json::register_contains_function(conn);
    sqlite::execute(conn, "CREATE TABLE docs(payload JSON);", true);
    sqlite::command insert(conn, "INSERT INTO docs(payload) VALUES (?);");
    insert % std::string(R"({"tags":["vsqlite","cpp"]})");
    insert.emit();

    auto where = sqlite::json::contains_expression("payload",
                                                   sqlite::json::path().key("tags"),
                                                   "'vsqlite'");
    sqlite::query q(conn, ("SELECT COUNT(*) FROM docs WHERE " + where + ";").c_str());
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 1);
}

TEST(JsonFtsHelpersTest, FtsRankFunction) {
#if !defined(SQLITE_ENABLE_FTS5)
    GTEST_SKIP() << "FTS5 extension is not available in this build.";
#endif
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE VIRTUAL TABLE docs USING fts5(body);", true);
    sqlite::json::register_contains_function(conn); // ensure coexistence
    sqlite::fts::register_rank_function(conn);

    sqlite::execute(conn, "INSERT INTO docs(body) VALUES ('hello world'), ('hello sqlite');", true);
    sqlite::query q(conn, "SELECT fts_rank(matchinfo(docs)) FROM docs WHERE docs MATCH 'hello';");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_GT(res->get_double(0), 0.0);
}

