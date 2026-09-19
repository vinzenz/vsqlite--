# Prepared statements with explicit execution state

`sqlite::prepared_statement` (from `#include <sqlite/prepared_statement.hpp>`) is one
prepared-statement type with separate operations for command execution and row iteration.
It is created by `connection::prepare(sql)` and tracks its own execution state, so the
competing ways of starting, stepping, and resetting an execution that `command`, `query`,
`result`, and `execute` spread across several objects collapse into one place.

```cpp
sqlite::connection db(":memory:");

auto insert = db.prepare("INSERT INTO events(message) VALUES (?)");
auto outcome = insert.execute("started");

auto select = db.prepare("SELECT id, message FROM events WHERE id > ?");
for (auto row : select.rows(last_seen)) {
    auto id = row.get<std::int64_t>(0);
    auto message = row.get<std::string>(1);
}
```

This document is the behavior specification of the facade. The legacy classes keep their
public signatures; see [Migration from command, query, and execute](#migration-from-command-query-and-execute)
for how their concepts map onto this model.

## States

Every `prepared_statement` object is in exactly one state, exposed by `state()` as
`sqlite::execution_state`:

| State       | Meaning                                                                    |
| ----------- | -------------------------------------------------------------------------- |
| `prepared`  | Created or reset; an execution can start.                                  |
| `executing` | A cursor returned by `rows()` is live and only it may step.                |
| `complete`  | The last execution finished normally; the object is immediately reusable.  |
| `failed`    | The last execution raised a database error; `reset()` is required first.   |

State is tracked per statement object. Two different `prepared_statement` objects of the
same connection do not block each other, exactly as two `sqlite::command` objects never
did. Because statements integrate with the connection's statement cache, one underlying
`sqlite3_stmt` is never shared by two live objects.

## Binding

There are two ways to supply parameter values, both using the same conversion layer as the
legacy `%` streaming syntax (`std::optional`, `std::chrono` types, enums, strings, blobs,
and `sqlite::named(...)` parameters are all accepted).

1. **Argument sets** (the default): `execute(args...)` and `rows(args...)` bind the complete
   argument set of that call. Positional arguments bind to the indexes 1..N in call order;
   `sqlite::named` arguments bind by name and do not consume a positional index. Bindings
   left over from earlier invocations are cleared first, so a call never silently reuses
   earlier values. If the argument set does not cover every declared parameter, a
   `sqlite::database_exception` is thrown **before** the statement runs and the object stays
   in its previous, usable state. Extra positional arguments are rejected by SQLite's
   parameter range check.

2. **Manual binding** (advanced mode): the `bind(idx, value)`, `bind(name, value)` and
   `bind(idx)` (NULL) overloads record what was bound. The zero-argument overloads
   `execute()` and `rows()` run with those manually bound values, but only after every
   parameter of the statement was bound manually since the last `reset(true)`; otherwise
   they throw the same incomplete-argument-set error. Manual binds persist across
   zero-argument executions. They are refused while a cursor is live and after a failure
   until `reset()` was called.

A zero-argument call on a parameterized statement therefore never falls back to values an
earlier `execute(args...)` call bound: either every parameter was bound manually, or the
call is rejected.

## Stepping and completion

- `execute(args...)` runs the statement to completion (`SQLITE_DONE`) and returns an
  `sqlite::execution_outcome` with `affected_rows` (`sqlite3_changes`) and
  `last_insert_rowid` (`sqlite3_last_insert_rowid`). Both counters are captured while the
  statement's own completion is still the connection's most recent one, so statements that
  run afterwards on the same connection cannot alter the snapshot; the outcome is also
  kept in `last_outcome()`. After a successful `execute` the object is `complete` and
  immediately reusable.
- `rows(args...)` binds the arguments and returns a move-only `sqlite::cursor`. The cursor
  owns the active execution for its whole lifetime: until it is destroyed, the statement is
  `executing` and every other `execute`/`rows`/`reset`/`bind` on the same object is
  rejected with a clear error. Reaching the end of the range does not end that ownership;
  consume cursors in their own scope so the statement becomes reusable when the scope ends.
  A range-based for loop does this automatically, because its cursor temporary lives only
  for the loop. A stepping error marks the statement `failed` immediately; the cursor is
  finished and only `reset()` revives the statement.
- A cursor starts before the first row (the first step happens in `begin()`) and reaches
  the end once. Early exit (`break`) simply leaves the cursor unconsumed until it is
  destroyed, which rewinds the underlying statement at that point.

## execute() versus rows()

`execute()` rejects statements that return rows — `SELECT`, or DML with a `RETURNING`
clause — with a `sqlite::database_exception` whose message points at `rows()`. The check
uses the statement's column count and happens before anything is executed, so a rejected
call leaves the database untouched and the statement usable. This is a deliberate
decision: `execute()` means "run to completion, report the outcome", and an unobserved
result set would contradict that. Use `rows()` for everything that produces rows, including
`INSERT ... RETURNING` and `DELETE ... RETURNING`; the affected-row count of such a
statement is observed through the rows it yields, not through an outcome.

Statements without parameters may use the zero-argument `execute()` and `rows()` freely.

## Reset

`reset(bool clear_bindings = false)` rewinds the statement so a new execution can start.

- Permitted in every state except while a cursor is live; it never rewinds a live cursor
  behind the caller's back.
- Bindings are preserved by default (the next argument-set call still clears them itself);
  pass `clear_bindings = true` to drop the values together with the manual-binding
  bookkeeping, after which zero-argument calls are rejected again until everything is
  re-bound.
- The SQLite error code of a previously failed evaluation is deliberately not surfaced:
  clearing that failure is the purpose of the call. A `failed` statement becomes reusable
  only through `reset()`.

## Error matrix

| Situation                                                            | Result                                                                   |
| -------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `execute`/`rows` while a cursor of this statement is live            | `database_exception` ("another execution is still running...")            |
| `execute`/`rows`/`bind` after a failure without `reset()`            | `database_exception` ("the previous execution failed; call reset()...")   |
| `reset()` while a cursor is live                                     | `database_exception` ("cannot reset while a cursor is live...")           |
| Argument set does not cover every parameter (incl. zero-argument     | `database_exception` ("incomplete argument set..."), thrown before the    |
| calls without complete manual bindings)                              | statement runs                                                            |
| `execute()` on a statement that returns rows (SELECT, RETURNING)     | `database_exception` ("...use rows() to consume the result set"), thrown  |
|                                                                      | before the statement runs                                                 |
| Stepping or binding raises a SQLite error                            | that error (`database_exception`/`database_misuse_exception`); the        |
|                                                                      | statement becomes `failed` until `reset()`                                |
| Preparing invalid SQL                                                | `database_exception` from preparation, as with `sqlite::command`          |
| Use of a moved-from statement                                        | `database_exception` ("prepared_statement was moved from") or             |
|                                                                      | `std::runtime_error` from `state()`                                       |

## Borrowed versus owning row access

Row access uses the same types as `sqlite::query` (`sqlite::query::result_range`):

- `row.get<T>(index_or_name)` on the `row_view` yielded by `*it` **borrows** from the
  cursor: the view (and any `std::string_view`/blob span read from it) expires when the
  iterator is incremented, the cursor is destroyed, or the statement is reset.
- `sqlite::cursor::row` is an **owning** snapshot. Take one while the row is current when
  the values must outlive the iteration, e.g. by copying from the postfix increment
  proxy (`*it++`) or by storing rows in a container. The snapshot keeps the original
  SQLite storage class per column; reading with a type from a different storage class
  throws instead of coercing (see the `row` class comment in `sqlite/query.hpp`).

## Statement cache and lifetimes

`connection::prepare()` participates in the connection's LRU statement cache exactly like
`sqlite::command`: a cached `sqlite3_stmt` for identical SQL text is reused when one is
available, schema-changing statements bypass and clear the cache, and the statement is
handed back when the `prepared_statement` object is destroyed. The owning
`sqlite::connection` must outlive the statement and every cursor created from it, the
same rule the legacy classes follow. `prepared_statement` and `cursor` are move-only;
`prepare()` returns the statement by value.

## Migration from command, query, and execute

| Legacy concept                                    | Prepared-statement equivalent                                     |
| ------------------------------------------------- | ----------------------------------------------------------------- |
| `sqlite::command cmd(con, sql)`                   | `auto cmd = con.prepare(sql)`                                     |
| `cmd % a % b; cmd.step_once();`                   | `cmd.execute(a, b)`                                               |
| `cmd(a, b)` (variadic `operator()`)               | `cmd.execute(a, b)` (complete set; partial pre-binds no longer mix) |
| `sqlite::execute exec(con, sql, true)`            | `con.prepare(sql).execute()`                                      |
| `exec.clear(); exec % a; exec.step_once();`       | `exec.execute(a)` (clearing is part of the call)                  |
| `exec.reset_statement()` before re-running        | not needed; every argument-set call resets and clears first       |
| `sqlite::query q(con, sql); q % x; q.get_result()`| `q.rows(x)`                                                       |
| `for (auto row : q.each(a, b))`                   | `for (auto row : q.rows(a, b))` (same row_view/row types)         |
| `res->next_row()` loop, `res->reset()`            | range-based for over the cursor; cursors are single-pass           |
| `res->get_changes()` after exhaustion             | `outcome.affected_rows` / `last_outcome()` from `execute()`        |
| `con.get_last_insert_rowid()`                     | `outcome.last_insert_rowid`                                       |
| named parameters via `% sqlite::named(...)`       | `execute(sqlite::named(...), ...)` or `bind(name, value)`         |

The legacy classes are unchanged and remain supported; `deprecated` helpers such as
`emit_result()` are unaffected. Adapters that want a single internal execution object can
delegate to this facade later without changing their public signatures.
