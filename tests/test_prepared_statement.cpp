#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/prepared_statement.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace testhelpers;

namespace {
struct events_schema {
    sqlite::connection con;
    events_schema() : con(":memory:") {
        sqlite::execute(con, "CREATE TABLE events(id INTEGER PRIMARY KEY, message TEXT);", true);
    }
};

struct unique_schema {
    sqlite::connection con;
    unique_schema() : con(":memory:") {
        sqlite::execute(con, "CREATE TABLE items(id INTEGER PRIMARY KEY, tag TEXT UNIQUE);", true);
    }
};
} // namespace

TEST(PreparedStatementTest, IssueExampleCallPattern) {
    events_schema db;
    auto insert  = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    auto outcome = insert.execute("started");
    EXPECT_EQ(outcome.affected_rows, 1);
    EXPECT_GT(outcome.last_insert_rowid, 0);

    auto select            = db.con.prepare("SELECT id, message FROM events WHERE id > ?");
    std::int64_t last_seen = 0;
    int rows               = 0;
    for (auto row : select.rows(last_seen)) {
        auto id      = row.get<std::int64_t>(0);
        auto message = row.get<std::string>(1);
        EXPECT_GT(id, last_seen);
        EXPECT_EQ(message, "started");
        ++rows;
    }
    EXPECT_EQ(rows, 1);
    EXPECT_EQ(select.state(), sqlite::execution_state::complete);
}

TEST(PreparedStatementTest, RepeatedExecutionWithDifferentArgs) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    for (const char *message : {"one", "two", "three"}) {
        auto outcome = insert.execute(message);
        EXPECT_EQ(outcome.affected_rows, 1);
        EXPECT_EQ(db.con.get_last_insert_rowid(), outcome.last_insert_rowid);
    }

    auto select = db.con.prepare("SELECT message FROM events ORDER BY id");
    std::vector<std::string> seen;
    for (auto row : select.rows()) {
        // Only the cursor may step while it is active.
        EXPECT_EQ(select.state(), sqlite::execution_state::executing);
        seen.push_back(std::string(row.get<std::string_view>(0)));
    }
    ASSERT_EQ(seen.size(), 3U);
    EXPECT_EQ(seen[0], "one");
    EXPECT_EQ(seen[1], "two");
    EXPECT_EQ(seen[2], "three");
}

TEST(PreparedStatementTest, EarlyRangeExitAllowsReuse) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    insert.execute("a");
    insert.execute("b");
    insert.execute("c");

    auto select = db.con.prepare("SELECT message FROM events ORDER BY id");
    {
        auto cursor = select.rows();
        int visited = 0;
        for (auto row : cursor) {
            EXPECT_EQ(row.get<std::string>(0), "a");
            ++visited;
            break; // early exit leaves the cursor unconsumed
        }
        EXPECT_EQ(visited, 1);
        EXPECT_EQ(select.state(), sqlite::execution_state::executing);
    }
    // The cursor is gone, so the execution finished and the statement is reusable.
    EXPECT_EQ(select.state(), sqlite::execution_state::complete);
    auto outcome = insert.execute("d");
    EXPECT_EQ(outcome.affected_rows, 1);
    EXPECT_EQ(count_rows(db.con, "events"), 4);
}

TEST(PreparedStatementTest, ReturningDmlGoesThroughRows) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?) RETURNING id, message");
    std::int64_t returned_id = 0;
    std::string returned_message;
    for (auto row : insert.rows("via returning")) {
        returned_id      = row.get<std::int64_t>(0);
        returned_message = row.get<std::string>(1);
    }
    EXPECT_GT(returned_id, 0);
    EXPECT_EQ(returned_message, "via returning");
    EXPECT_EQ(count_rows(db.con, "events"), 1);
    EXPECT_EQ(insert.state(), sqlite::execution_state::complete);
}

TEST(PreparedStatementTest, ExecuteRejectsRowReturningStatements) {
    events_schema db;
    db.con.prepare("INSERT INTO events(message) VALUES (?)").execute("seed");

    auto select = db.con.prepare("SELECT id FROM events");
    EXPECT_THROW(select.execute(), sqlite::database_exception);

    auto returning = db.con.prepare("DELETE FROM events RETURNING id");
    EXPECT_THROW(returning.execute(), sqlite::database_exception);

    // The rejection happens before the statement runs, so nothing was deleted and the
    // statements stay reusable through rows().
    EXPECT_EQ(count_rows(db.con, "events"), 1);
    EXPECT_EQ(returning.state(), sqlite::execution_state::prepared);
    int deleted = 0;
    for (auto row : returning.rows()) {
        (void)row;
        ++deleted;
    }
    EXPECT_EQ(deleted, 1);
}

TEST(PreparedStatementTest, ExecuteRejectsSelectWithHelpfulMessage) {
    events_schema db;
    auto select = db.con.prepare("SELECT id, message FROM events");
    try {
        select.execute();
        FAIL() << "execute() must reject statements that return rows";
    } catch (sqlite::database_exception const &ex) {
        EXPECT_NE(std::string(ex.what()).find("rows()"), std::string::npos);
    }
}

TEST(PreparedStatementTest, SecondExecutionWhileCursorLiveThrows) {
    events_schema db;
    db.con.prepare("INSERT INTO events(message) VALUES (?)").execute("a");
    db.con.prepare("INSERT INTO events(message) VALUES (?)").execute("b");

    auto select = db.con.prepare("SELECT message FROM events ORDER BY id");
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");

    auto cursor = select.rows();
    auto it     = cursor.begin();
    ASSERT_NE(it, cursor.end());

    // A second rows() on the same statement while the first cursor is live.
    EXPECT_THROW(select.rows(), sqlite::database_exception);
    // Rewinding the live cursor behind the caller's back is refused as well.
    EXPECT_THROW(select.reset(), sqlite::database_exception);
    // The rejected calls did not touch the cursor's data.
    EXPECT_EQ(it->get<std::string>(0), "a");
    EXPECT_EQ(select.state(), sqlite::execution_state::executing);

    // execute() is rejected too when the cursor's own statement is live, in either
    // order: a cursor on an INSERT yields no rows but still owns the execution.
    {
        auto live = insert.rows("seed");
        EXPECT_EQ(insert.state(), sqlite::execution_state::executing);
        EXPECT_THROW(insert.execute("c"), sqlite::database_exception);
        EXPECT_THROW(insert.rows("d"), sqlite::database_exception);
        EXPECT_THROW(insert.reset(), sqlite::database_exception);
        // Execution state is tracked per statement object: a different statement of the
        // same connection keeps working while a cursor is live elsewhere.
        db.con.prepare("INSERT INTO events(message) VALUES (?)").execute("other");
    }
    EXPECT_EQ(insert.state(), sqlite::execution_state::complete);
    EXPECT_EQ(insert.execute("after-scope").affected_rows, 1);
    // "seed" was never stepped: the cursor binds its arguments but starts before the first
    // row, and the rejected calls never let it step.
    EXPECT_EQ(count_rows(db.con, "events"), 4);
}

TEST(PreparedStatementTest, StateTransitionsThroughExecuteRowsAndFailure) {
    unique_schema db;
    auto insert = db.con.prepare("INSERT INTO items(tag) VALUES (?)");
    EXPECT_EQ(insert.state(), sqlite::execution_state::prepared);
    insert.execute("first");
    EXPECT_EQ(insert.state(), sqlite::execution_state::complete);

    EXPECT_THROW(insert.execute("first"), sqlite::database_exception); // UNIQUE violation
    EXPECT_EQ(insert.state(), sqlite::execution_state::failed);

    // A failed statement refuses further work until reset() is called.
    EXPECT_THROW(insert.execute("second"), sqlite::database_exception);
    EXPECT_THROW(insert.bind(1, std::string("x")), sqlite::database_exception);

    insert.reset();
    EXPECT_EQ(insert.state(), sqlite::execution_state::prepared);
    insert.execute("second");
    EXPECT_EQ(count_rows(db.con, "items"), 2);
}

TEST(PreparedStatementTest, FailureThenResetThenReuse) {
    unique_schema db;
    auto insert = db.con.prepare("INSERT INTO items(id, tag) VALUES (?, ?)");
    insert.execute(1, "one");
    EXPECT_THROW(insert.execute(1, "one-again"), sqlite::database_exception);
    EXPECT_EQ(insert.state(), sqlite::execution_state::failed);
    insert.reset();
    EXPECT_EQ(insert.state(), sqlite::execution_state::prepared);
    auto outcome = insert.execute(2, "two");
    EXPECT_EQ(outcome.affected_rows, 1);
    EXPECT_EQ(count_rows(db.con, "items"), 2);
}

TEST(PreparedStatementTest, AffectedRowsPreservedAfterLaterStatements) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    insert.execute("a");
    insert.execute("b");
    insert.execute("c");

    auto update  = db.con.prepare("UPDATE events SET message = 'x' WHERE id > 0");
    auto outcome = update.execute();
    EXPECT_EQ(outcome.affected_rows, 3);

    // Later statements on the same connection must not change the captured outcome.
    insert.execute("d");
    sqlite::execute(db.con, "UPDATE events SET message = 'y' WHERE id > 0;", true);
    EXPECT_EQ(update.last_outcome().affected_rows, 3);
    EXPECT_EQ(update.execute().affected_rows, 4);

    auto remove = db.con.prepare("DELETE FROM events WHERE id <= ?");
    EXPECT_EQ(remove.execute(2).affected_rows, 2);
    EXPECT_EQ(count_rows(db.con, "events"), 2);
}

TEST(PreparedStatementTest, ZeroArgumentCallsRequireManualBindings) {
    unique_schema db;
    auto insert = db.con.prepare("INSERT INTO items(id, tag) VALUES (?, ?)");

    // A zero-argument call must not silently reuse values from an earlier invocation.
    EXPECT_THROW(insert.execute(), sqlite::database_exception);
    EXPECT_THROW(insert.rows(), sqlite::database_exception);
    EXPECT_EQ(insert.state(), sqlite::execution_state::prepared);
    EXPECT_EQ(count_rows(db.con, "items"), 0);

    // Advanced mode: bind manually, then run the zero-argument overload.
    insert.bind(1, 10);
    EXPECT_THROW(insert.execute(), sqlite::database_exception); // still incomplete
    insert.bind(2, std::string("ten"));
    EXPECT_EQ(insert.execute().affected_rows, 1);
    insert.bind(1, 11); // manual bindings persist across executions
    insert.bind(2, std::string("eleven"));
    EXPECT_EQ(insert.execute().affected_rows, 1);
    EXPECT_EQ(count_rows(db.con, "items"), 2);

    insert.reset(true);
    EXPECT_THROW(insert.execute(), sqlite::database_exception); // bindings were cleared
    insert.execute(3, "three");
    EXPECT_EQ(count_rows(db.con, "items"), 3);
}

TEST(PreparedStatementTest, ArgumentSetIsCompleteAndNotSticky) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    insert.execute("first");

    // Too few arguments throw instead of reusing the earlier bound value.
    EXPECT_THROW(insert.execute(), sqlite::database_exception);
    EXPECT_EQ(count_rows(db.con, "events"), 1);

    // Extra arguments are rejected by SQLite's parameter range check.
    auto select = db.con.prepare("SELECT message FROM events");
    EXPECT_THROW((select.rows(1, std::string("extra"))), sqlite::database_exception);

    insert.execute("second");
    EXPECT_EQ(count_rows(db.con, "events"), 2);
}

TEST(PreparedStatementTest, NamedParametersBindByName) {
    unique_schema db;
    auto insert = db.con.prepare("INSERT INTO items(id, tag) VALUES (:id, :tag)");
    insert.execute(sqlite::named(":id", 1), sqlite::named(":tag", std::string("one")));
    // A named parameter set with a gap is rejected.
    EXPECT_THROW(insert.execute(sqlite::named(":id", 2)), sqlite::database_exception);
    EXPECT_EQ(count_rows(db.con, "items"), 1);

    insert.execute(sqlite::named(":tag", "two"), sqlite::named(":id", 2));
    auto select = db.con.prepare("SELECT tag FROM items WHERE id = :id");
    for (auto row : select.rows(sqlite::named(":id", 2))) {
        EXPECT_EQ(row.get<std::string>(0), "two");
    }
}

TEST(PreparedStatementTest, OwningRowSurvivesAdvancement) {
    events_schema db;
    auto insert = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    insert.execute("a");
    insert.execute("b");

    auto select = db.con.prepare("SELECT id, message FROM events ORDER BY id");
    std::vector<sqlite::cursor::row> owned;
    {
        auto cursor = select.rows();
        for (auto it = cursor.begin(); it != cursor.end();) {
            auto snap = it++; // postfix proxy owns the row that preceded the increment
            owned.push_back(*snap);
        }
    }
    ASSERT_EQ(owned.size(), 2U);
    EXPECT_EQ(owned[0].get<std::string>(1), "a");
    EXPECT_EQ(owned[1].get<std::string>(1), "b");
    EXPECT_EQ(owned[1].get<std::int64_t>(0), 2);
}

TEST(PreparedStatementTest, MoveSemantics) {
    events_schema db;
    auto statement                   = db.con.prepare("INSERT INTO events(message) VALUES (?)");
    sqlite::prepared_statement moved = std::move(statement);
    EXPECT_EQ(moved.execute("moved").affected_rows, 1);
    EXPECT_EQ(count_rows(db.con, "events"), 1);

    // The moved-from object rejects use.
    EXPECT_THROW(statement.execute("again"), sqlite::database_exception);
    EXPECT_THROW(statement.state(), std::runtime_error);
    EXPECT_THROW(statement.rows(), sqlite::database_exception);

    sqlite::prepared_statement target(db.con.prepare("INSERT INTO events(message) VALUES (?)"));
    target = std::move(moved);
    EXPECT_EQ(target.execute("assigned").affected_rows, 1);
    EXPECT_EQ(count_rows(db.con, "events"), 2);
}

TEST(PreparedStatementTest, ParameterlessStatementRunsThroughZeroArgumentExecute) {
    events_schema db;
    auto insert_literal = db.con.prepare("INSERT INTO events(message) VALUES ('literal')");
    EXPECT_EQ(insert_literal.execute().affected_rows, 1);
    EXPECT_EQ(insert_literal.execute().affected_rows, 1);
    EXPECT_EQ(insert_literal.parameter_count(), 0);
    EXPECT_EQ(count_rows(db.con, "events"), 2);
}

TEST(PreparedStatementTest, CursorIsMoveOnly) {
    static_assert(!std::is_copy_constructible<sqlite::cursor>::value, "cursor must be move-only");
    static_assert(!std::is_copy_assignable<sqlite::cursor>::value, "cursor must be move-only");
    static_assert(std::is_move_constructible<sqlite::cursor>::value, "cursor must be movable");
    static_assert(!std::is_copy_constructible<sqlite::prepared_statement>::value,
                  "prepared_statement must be move-only");
    static_assert(std::is_move_constructible<sqlite::prepared_statement>::value,
                  "prepared_statement must be movable");
    SUCCEED();
}
