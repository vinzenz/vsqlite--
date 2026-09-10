#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/session.hpp>

#include <stdexcept>
#include <string>

using namespace testhelpers;

namespace {

/// Builds a source connection with the given schema and tracked writes, returning the
/// recorded changeset.
template <typename Setup>
std::vector<unsigned char> tracked_changeset(std::string const &ddl, Setup setup,
                                             bool patchset = false) {
    sqlite::connection source(":memory:");
    sqlite::execute(source, ddl, true);
    sqlite::session session(source);
    session.attach_all();
    setup(source);
    return patchset ? session.patchset() : session.changeset();
}

void open_consumer(sqlite::connection &consumer, std::string const &ddl,
                   std::string const &seed = {}) {
    sqlite::execute(consumer, ddl, true);
    if (!seed.empty()) {
        sqlite::execute(consumer, seed, true);
    }
}

} // namespace

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

TEST(SessionTest, ChangesetConflictAbortsByDefault) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset  = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    try {
        sqlite::apply_changeset(consumer, changeset);
        FAIL() << "Expected conflicting changeset application to throw.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_EQ(ex.error_code(), SQLITE_ABORT);
    }
    EXPECT_EQ(count_rows(consumer, "t"), 1);
    sqlite::query q(consumer, "SELECT v FROM t WHERE id = 1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "original");
}

TEST(SessionTest, ChangesetConflictOmitPolicySkipsConflictingRows) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a'), (2, 'b');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    sqlite::apply_changeset(consumer, changeset, sqlite::conflict_policy::omit);

    EXPECT_EQ(count_rows(consumer, "t"), 2);
    sqlite::query q(consumer, "SELECT v FROM t WHERE id = 1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "original");
}

TEST(SessionTest, ChangesetConflictReplacePolicyOverwritesRow) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a'), (2, 'b');", true);
        sqlite::execute(con, "UPDATE t SET v = 'a2' WHERE id = 1;", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'stale'), (2, 'b');");

    sqlite::apply_changeset(consumer, changeset, sqlite::conflict_policy::replace);

    EXPECT_EQ(count_rows(consumer, "t"), 2);
    sqlite::query q(consumer, "SELECT v FROM t WHERE id = 1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "a2");
}

TEST(SessionTest, ConflictHandlerReceivesConflictDetails) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset  = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    sqlite::changeset_conflict seen{};
    auto handler = [&seen](sqlite::changeset_conflict const &conflict) {
        seen    = conflict;
        return sqlite::conflict_policy::omit;
    };
    sqlite::apply_changeset(consumer, changeset, handler);

    EXPECT_EQ(seen.type, sqlite::changeset_conflict_type::conflict);
    EXPECT_EQ(seen.operation, sqlite::changeset_operation::insert);
    EXPECT_EQ(seen.table, "t");
    EXPECT_TRUE(seen.replace_supported());
    EXPECT_EQ(count_rows(consumer, "t"), 1);
}

TEST(SessionTest, ConflictHandlerExceptionPropagatesAndRollsBack) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset  = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a'), (2, 'b');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    EXPECT_THROW(sqlite::apply_changeset(consumer, changeset,
                                         [](sqlite::changeset_conflict const &)
                                             -> sqlite::conflict_policy {
                                             throw std::runtime_error("boom");
                                         }),
                 std::runtime_error);
    EXPECT_EQ(count_rows(consumer, "t"), 1);
}

TEST(SessionTest, EmptyConflictHandlerAbortsLikeDefault) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset  = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    sqlite::conflict_handler no_handler;
    EXPECT_THROW(sqlite::apply_changeset(consumer, changeset, no_handler),
                 sqlite::database_exception);
    EXPECT_EQ(count_rows(consumer, "t"), 1);
}

TEST(SessionTest, ChangesetConflictReplacePolicyOverwritesDuplicateKeyRow) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto changeset  = tracked_changeset(ddl, [](sqlite::connection &con) {
        sqlite::execute(con, "INSERT INTO t VALUES (1, 'a');", true);
    });
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    sqlite::apply_changeset(consumer, changeset, sqlite::conflict_policy::replace);

    EXPECT_EQ(count_rows(consumer, "t"), 1);
    sqlite::query q(consumer, "SELECT v FROM t WHERE id = 1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "a");
}

TEST(SessionTest, MissingRowConflictAbortsByDefaultAndOmitAppliesRest) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    sqlite::connection source(":memory:");
    sqlite::execute(source, "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);", true);
    // Seed before the session starts tracking so the changeset holds only the delete.
    sqlite::execute(source, "INSERT INTO t VALUES (1, 'a'), (2, 'b');", true);
    sqlite::session session(source);
    session.attach_all();
    sqlite::execute(source, "DELETE FROM t WHERE id = 1;", true);
    auto changeset = session.changeset();

    sqlite::connection consumer(":memory:");
    open_consumer(consumer, "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);",
                  "INSERT INTO t VALUES (2, 'b');");

    try {
        sqlite::apply_changeset(consumer, changeset);
        FAIL() << "Expected missing-row conflict to abort the application.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_EQ(ex.error_code(), SQLITE_ABORT);
    }
    EXPECT_EQ(count_rows(consumer, "t"), 1);

    sqlite::changeset_conflict seen{};
    sqlite::apply_changeset(consumer, changeset, [&seen](sqlite::changeset_conflict const &c) {
        seen = c;
        return sqlite::conflict_policy::omit;
    });
    EXPECT_EQ(seen.type, sqlite::changeset_conflict_type::not_found);
    EXPECT_EQ(seen.operation, sqlite::changeset_operation::remove);
    EXPECT_EQ(seen.table, "t");
    EXPECT_EQ(count_rows(consumer, "t"), 1);
}

TEST(SessionTest, PatchsetConflictAbortsByDefault) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl  = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto patchset   = tracked_changeset(
        ddl,
        [](sqlite::connection &con) {
            sqlite::execute(con, "INSERT INTO t VALUES (1, 'a');", true);
        },
        true);
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    try {
        sqlite::apply_patchset(consumer, patchset);
        FAIL() << "Expected conflicting patchset application to throw.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_EQ(ex.error_code(), SQLITE_ABORT);
    }
    EXPECT_EQ(count_rows(consumer, "t"), 1);
}

TEST(SessionTest, PatchsetConflictOmitPolicyAppliesRest) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const ddl = "CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);";
    auto patchset  = tracked_changeset(
        ddl,
        [](sqlite::connection &con) {
            sqlite::execute(con, "INSERT INTO t VALUES (1, 'a'), (2, 'b');", true);
        },
        true);
    sqlite::connection consumer(":memory:");
    open_consumer(consumer, ddl, "INSERT INTO t VALUES (1, 'original');");

    sqlite::apply_patchset(consumer, patchset, sqlite::conflict_policy::omit);

    EXPECT_EQ(count_rows(consumer, "t"), 2);
    sqlite::query q(consumer, "SELECT v FROM t WHERE id = 2;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<std::string>(0), "b");
}

TEST(SessionTest, ForeignKeyConflictAbortsAndOmits) {
    if (!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    auto const child_ddl =
        "CREATE TABLE child(id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id));";

    sqlite::connection source(":memory:");
    sqlite::execute(source, "CREATE TABLE parent(id INTEGER PRIMARY KEY);", true);
    sqlite::execute(source, child_ddl, true);
    sqlite::session session(source);
    session.attach("child");
    sqlite::execute(source, "INSERT INTO child VALUES (1, 42);", true);
    auto changeset = session.changeset();

    auto open_fk_consumer = [child_ddl](sqlite::connection &con) {
        sqlite::execute(con, "CREATE TABLE parent(id INTEGER PRIMARY KEY);", true);
        sqlite::execute(con, child_ddl, true);
        sqlite::execute(con, "PRAGMA foreign_keys = ON;", true);
    };

    sqlite::changeset_conflict seen{};
    sqlite::connection consumer(":memory:");
    open_fk_consumer(consumer);
    try {
        sqlite::apply_changeset(consumer, changeset, [&seen](sqlite::changeset_conflict const &c) {
            seen = c;
            return sqlite::conflict_policy::abort;
        });
        FAIL() << "Expected foreign key conflict to abort the application.";
    } catch (sqlite::database_exception_code const &ex) {
        // Foreign key violations are deferred until the end of the application; declining
        // them surfaces as SQLITE_CONSTRAINT rather than SQLITE_ABORT.
        EXPECT_EQ(ex.error_code(), SQLITE_CONSTRAINT);
    }
    EXPECT_EQ(seen.type, sqlite::changeset_conflict_type::foreign_key);
    EXPECT_EQ(count_rows(consumer, "child"), 0);

    sqlite::connection tolerant(":memory:");
    open_fk_consumer(tolerant);
    sqlite::apply_changeset(tolerant, changeset, [](sqlite::changeset_conflict const &) {
        return sqlite::conflict_policy::omit;
    });
    // Omitting the foreign key conflict commits the change despite the violation.
    EXPECT_EQ(count_rows(tolerant, "child"), 1);
}
