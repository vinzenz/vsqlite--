# Reflect-CPP ORM Addon

- **Status:** draft
- **Owner:** (unassigned)
- **Last updated:** 2024-05-14
- **Related issues/PRs:** _n/a_

## Problem

vsqlite++ exposes low-level, type-safe bindings for the SQLite C API, but writing CRUD code still requires crafting SQL statements, binding each parameter manually, and mapping rows back into structs. Projects that already use reflection libraries (e.g., reflect-cpp) could benefit from an optional ORM layer that automates these repetitive steps without bloating the core library.

## Proposal

Build a header-only “addons” module (e.g., `include/sqlite/addons/orm.hpp`) that leverages reflect-cpp to generate SQL and binding glue for plain structs. Key ideas:

- **Schema mapping:** Use reflect-cpp to enumerate struct members and synthesize CREATE TABLE statements (`orm::schema<T>()`), allowing opt-in migrations or validation.
- **Repositories:** Provide a templated `repository<T>` that exposes `insert`, `update`, `erase`, and `find` helpers. These functions bind fields via vsqlite++’s `command` API and read rows via `result::get<T>()`/`get_tuple`.
- **Query builder:** Offer convenience functions for simple filters (e.g., `repo.find_where("flag = ?", value)`), while still allowing callers to drop down to raw SQL when needed.
- **Transaction integration:** Support unit-of-work patterns by accepting an optional `sqlite::transaction` or connection reference.
- **Opt-in dependency:** The addon should live outside the core target so downstream users only include it when they already depend on reflect-cpp.

## Alternatives considered

- **Core integration:** Embedding reflection/ORM features into the main library would force every user to pull in reflect-cpp, growing compile times and violating the minimal-dependency philosophy.
- **Code generation:** Instead of runtime templates, we could generate SQL/binding code via scripts. This adds tooling complexity and reduces flexibility compared to reflect-cpp’s compile-time introspection.

## Open questions

- How to represent primary keys and constraints? (Attributes? Traits specializations?)
- Should the ORM manage migrations or simply emit schema suggestions?
- How to expose query composition (fluent API vs. raw SQL strings)?
- What is the minimum reflect-cpp version we support?

## Next steps

1. Sketch a prototype repository class (`repository<T>`) that supports insert/select/delete via reflect-cpp.
2. Evaluate attribute metadata for primary keys, nullable fields, and custom column names.
3. Decide on the directory/namespace layout (`sqlite::addons::orm`?).
4. Document the addon in `docs/ideas` and reference it from the README once prototyping begins.
