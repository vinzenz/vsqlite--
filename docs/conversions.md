# Value conversions

VSQLite++ converts C++ values to SQL values and back in three places:

- **Binding** — `sqlite::command::bind`, `operator%`, and `bind_value` write bound
  parameters through `sqlite3_bind_*`.
- **Extraction** — `sqlite::result::get<T>` (legacy) and `sqlite::result::get_checked<T>`
  (strict) read result columns through `sqlite3_column_*`.
- **Callbacks** — `sqlite::create_function` callables receive arguments through
  `sqlite3_value_*` and return results through `sqlite3_result_*`.

All three use the one conversion policy implemented in
`include/sqlite/detail/conversion.hpp`. This document is the mapping contract; the
implementation must not drift from it. Nothing here changes legacy behavior — where a
rule exists only for compatibility it is marked **legacy** and pinned by regression
tests, so a future change becomes visible.

## Mapping table

| C++ value | Binding (`command`) | Extraction (`result::get`) | Extraction (`get_checked`) | Callback argument | Callback result |
|---|---|---|---|---|---|
| `sqlite::nil`, `null_type` | SQL NULL | — | — | — | SQL NULL |
| `std::nullptr_t`, raw `bind(idx, nullptr, 0)` | SQL NULL | — | — | — | SQL NULL |
| disengaged `std::optional<T>` | SQL NULL | — | — | SQL NULL (via optional argument) | SQL NULL |
| SQL NULL, read as `std::optional<T>` | — | `std::nullopt` | `std::nullopt` | `std::nullopt` (optional only) | — |
| SQL NULL, read as other type | — | **legacy coercion**: `0` / `0.0` / `false` / text `"NULL"` / empty binary | `database_exception` | `database_exception` (unless the parameter is `std::optional` or raw `sqlite3_value*`) | — |
| empty `std::string`, `std::string_view` | empty text (zero length, **not** NULL) | empty text | empty text | empty text | empty text |
| empty `std::vector<unsigned char>`, `std::vector<std::byte>`, `std::span<const unsigned char>`, `std::span<const std::byte>` | empty blob (zero length, **not** NULL) | empty blob | empty blob | empty blob | empty blob |
| engaged `std::optional<T>` | converts `T` | converts `T` | converts `T` | converts `T` | converts `T` |
| integral / enum | stored as 64-bit integer (`int` bind keeps 32-bit overload; enums cast to `std::int64_t`) | `std::int64_t` column value, `static_cast` to `T` — **legacy**: narrowing wraps, sign loss allowed | rejects narrowing and sign loss that do not fit `T` (`std::in_range`) | same legacy cast as extraction; NULL rejected | written as 64-bit integer |
| `bool` | — | `sqlite3_column_int(...) != 0`; NULL coerces to `false` (**legacy**) | `int64 != 0`; NULL rejected | `sqlite3_value_int(...) != 0`; NULL rejected | `0` or `1` |
| floating point | stored as `double` | `double` column value, `static_cast` to `T` — **legacy**: out-of-range `double` to `float` becomes infinity | rejects `double` values outside the representable range of a narrower floating point type | same legacy cast; NULL rejected | written as `double` |
| text (`std::string`, `std::string_view`, string-likes) | bound as UTF-8 text with `SQLITE_TRANSIENT` | `std::string` copies; `std::string_view` borrows (see below); NULL coerces to `"NULL"` (**legacy**) | same as `get`, but NULL rejected | `std::string_view` borrows the argument (see below) | copied through `sqlite3_result_text` |
| binary (byte vectors and spans) | bound as blob with `SQLITE_TRANSIENT` | `std::vector<unsigned char>` copies; spans borrow (see below); NULL coerces to empty (**legacy**) | same as `get`, but NULL rejected | spans borrow the argument (see below) | copied through `sqlite3_result_blob` |
| `std::chrono::duration<Rep, Period>` | microseconds since epoch as `std::int64_t` (see below) | rebuilt from the stored microseconds (see below) | same conversion as `get`, but NULL rejected | not supported (compile-time error) | not supported (compile-time error) |
| `std::chrono::time_point<Clock, Duration>` | microseconds since epoch as `std::int64_t` | rebuilt from the stored microseconds (see below) | same conversion as `get`, but NULL rejected | not supported (compile-time error) | not supported (compile-time error) |
| raw `sqlite3_value *` / `sqlite3_value const *` | — | — | — | passed through unchanged; explicit advanced path | — |

## NULL and empty values

NULL is a distinct SQL value. The explicit NULL representations are `sqlite::nil`
(binding and callback results), `std::nullptr_t` (callback results), a disengaged
`std::optional` (all surfaces), and the raw pointer overload
`command::bind(int, void const *, size_t)` with a null pointer.

SQLite treats a null data pointer as SQL NULL regardless of the byte count. Empty text
and empty blobs therefore pass a non-null dummy pointer with length zero
(`detail::conversion::data_or_empty`), so an engaged empty string or blob stays present
on every surface. NULL and empty never convert into each other; only the **legacy**
extraction coercions read NULL as `0`, `0.0`, `false`, `"NULL"`, or empty binary data,
and only the legacy `get<T>` path applies them.

## Numeric conversions

SQLite applies its own affinity coercion first (`sqlite3_column_int64`,
`sqlite3_column_double`, `sqlite3_value_int64`, `sqlite3_value_double`). The wrapper
then casts to the requested C++ type:

- **legacy** (`result::get<T>`, callback arguments): a C `static_cast`. An `int64_t`
  that does not fit wraps (for example, binding `2147483648` and reading it as `int`
  yields `-2147483648`), and negative values convert to unsigned types unchanged.
  Pinned by `tests/test_conversions.cpp` so a future change is visible.
- **checked** (`result::get_checked<T>`): rejects values outside the destination range,
  including signedness loss, with `database_exception` instead of wrapping. For floating
  point narrowing, values that would become infinity are rejected. NULL is rejected for
  non-optional types.

## Chrono values

`std::chrono::duration` and `std::chrono::time_point` are stored as microseconds since
the epoch in a 64-bit integer column. This stored unit must not change: existing
databases contain microsecond counts.

- Precision loss: values with finer than microsecond resolution are truncated towards
  zero on bind; reading into a coarser duration or time point truncates towards zero as
  well (`std::chrono::duration_cast` semantics). Reading into a finer resolution scales
  the count back up, so nanosecond round-trips lose the sub-microsecond remainder.
- Range: a value whose microsecond count does not fit `std::int64_t` is not
  representable. The count conversion truncates like a C cast (wraps; the truncating
  cast is implementation-defined for source reps wider than 64 bits). Boundary behavior
  is pinned by `tests/test_conversions.cpp`.

## Borrowed versus owning access

- Borrowing (`std::string_view`, `std::span<const unsigned char>`,
  `std::span<const std::byte>`): the view references SQLite-owned storage. A view read
  from a `result` row expires when the cursor advances (`next_row`), resets (`reset`),
  or dies together with its statement. A view handed to a SQL function callback is valid
  for that invocation only. Returning a view that references a destroyed temporary is
  invalid.
- Owning (`std::string`, `std::vector<unsigned char>`, `std::vector<std::byte>`): the
  data is copied. Bound text and blobs are always copied into SQLite
  (`SQLITE_TRANSIENT`), as are callback results, so callers may pass and return
  temporaries.

## Error handling at the callback boundary

Exceptions never cross the C boundary. `function_entry` catches every exception leaving
the callable, translates `database_exception` and `std::exception` messages into
`sqlite3_result_error`, and substitutes a generic error for unknown types.
