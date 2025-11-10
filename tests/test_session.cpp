#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/session.hpp>

using namespace testhelpers;

TEST(SessionTest, CapturesAndAppliesChangeset) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE inventory(id INTEGER PRIMARY KEY, qty INTEGER);", true);
    sqlite::session session(conn);
    session.attach("inventory");
    sqlite::execute(conn, "INSERT INTO inventory(qty) VALUES (5), (7);", true);

    auto changeset = session.changeset();

    sqlite::connection consumer(":memory:");
    sqlite::execute(consumer, "CREATE TABLE inventory(id INTEGER PRIMARY KEY, qty INTEGER);", true);
    sqlite::apply_changeset(consumer, changeset);
    sqlite::query q(consumer, "SELECT COUNT(*) FROM inventory;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
}

TEST(SessionTest, PatchsetTracksDeletes) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);
    sqlite::execute(conn, "INSERT INTO docs(body) VALUES ('old');", true);
    sqlite::session session(conn);
    session.attach("docs");

    sqlite::execute(conn, "DELETE FROM docs;", true);
    auto patchset = session.patchset();

    sqlite::connection other(":memory:");
    sqlite::execute(other, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);
    sqlite::execute(other, "INSERT INTO docs(body) VALUES ('old');", true);
    sqlite::apply_patchset(other, patchset);
    sqlite::query q(other, "SELECT COUNT(*) FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 0);
}
