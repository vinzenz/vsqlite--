#include <gtest/gtest.h>

#include <sqlite/backup.hpp>
#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/connection_pool.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>
#include <sqlite/serialization.hpp>
#include <sqlite/session.hpp>
#include <sqlite/snapshot.hpp>
#include <sqlite/query.hpp>
#include <sqlite/savepoint.hpp>
#include <sqlite/threading.hpp>
#include <sqlite/transaction.hpp>
#include <sqlite/view.hpp>

#include <algorithm>
#include <cstddef>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <variant>

namespace {

std::filesystem::path test_root() {
    static const std::filesystem::path root = [] {
        auto base = std::filesystem::temp_directory_path() / "vsqlitepp_tests";
        std::filesystem::create_directories(base);
        return base;
    }();
    return root;
}

std::filesystem::path unique_db_path(std::string_view hint) {
    static std::atomic<uint64_t> counter{0};
    auto root = test_root();
    auto now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << hint << "_" << now << "_" << counter++ << ".db";
    auto path = root / oss.str();
    std::filesystem::create_directories(path.parent_path());
    return path;
}

struct TempFile {
    explicit TempFile(std::string_view hint)
        : path(unique_db_path(hint)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::filesystem::path path;
    std::string string() const { return path.string(); }
};

int count_rows(sqlite::connection & con, std::string const & table_expression) {
    sqlite::query q(con, "SELECT COUNT(*) FROM " + table_expression + ";");
    auto res = q.get_result();
    EXPECT_TRUE(res->next_row());
    return res->get_int(0);
}

std::vector<unsigned char> load_blob(sqlite::result & res, int idx) {
    std::vector<unsigned char> data;
    res.get_binary(idx, data);
    return data;
}

std::string quote_identifier(std::string_view identifier) {
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('"');
    for(char c : identifier) {
        if(c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::string unique_memory_uri() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;
    oss << "file:memdb_" << counter++ << "?mode=memory&cache=shared";
    return oss.str();
}

} // namespace

TEST(ConnectionTest, OpenModesAndLastInsertId) {
    TempFile file("open_modes");
    {
        sqlite::connection conn(file.string());
        sqlite::execute(conn, "CREATE TABLE sample(id INTEGER PRIMARY KEY, name TEXT);", true);
        sqlite::command insert(conn, "INSERT INTO sample(name) VALUES (?);");
        insert % std::string("alpha");
        insert.emit();
        EXPECT_GT(conn.get_last_insert_rowid(), 0);
    }

    {
        sqlite::connection existing(file.string(), sqlite::open_mode::open_existing);
        EXPECT_NO_THROW(sqlite::execute(existing, "SELECT 1;", true));
    }

    {
        sqlite::connection readonly(file.string(), sqlite::open_mode::open_readonly);
        EXPECT_THROW(sqlite::execute(readonly, "INSERT INTO sample VALUES (1, 'x');", true),
                     sqlite::database_exception);
    }

    TempFile missing("missing_db");
    EXPECT_THROW(sqlite::connection fail(missing.string(), sqlite::open_mode::open_existing),
                 sqlite::database_exception);
    EXPECT_THROW(sqlite::connection fail2(missing.string(), sqlite::open_mode::open_readonly),
                 sqlite::database_exception);

    // Ensure always_create removes existing file contents.
    {
        std::ofstream sentinel(file.path);
        sentinel << "SENTINEL";
    }
    {
        sqlite::connection recreated(file.string(), sqlite::open_mode::always_create);
        sqlite::execute(recreated, "CREATE TABLE reset_check(id INTEGER);", true);
    }
    std::error_code ec;
    EXPECT_GT(std::filesystem::file_size(file.path, ec), 8u);

    EXPECT_THROW(sqlite::connection empty("", sqlite::open_mode::open_or_create),
                 sqlite::database_exception);
}

TEST(ConnectionTest, RejectsSymlinks) {
    TempFile real("real_db");
    {
        sqlite::connection conn(real.string());
        sqlite::execute(conn, "CREATE TABLE t(id INTEGER);", true);
    }
    auto link = test_root() / "symlinked.db";
    auto link_str = link.string();
    std::error_code ec;
    std::filesystem::remove(link, ec);
#ifdef _WIN32
    // symbolic links on Windows require elevated privileges; skip
    GTEST_SKIP() << "Symbolic link creation not supported on this platform.";
#else
    std::filesystem::create_symlink(real.path, link);
    EXPECT_THROW(sqlite::connection bad(link_str), sqlite::database_exception);
#endif
    std::filesystem::remove(link, ec);
}

TEST(ConnectionTest, AttachAndDetachQuoteIdentifiers) {
    sqlite::connection main(":memory:");
    TempFile attached("attached_db");
    auto alias = std::string("alias name\";DROP");
    EXPECT_THROW(main.attach(attached.string(), ""), sqlite::database_exception);
    EXPECT_THROW(main.detach(""), sqlite::database_exception);

    ASSERT_NO_THROW(main.attach(attached.string(), alias));
    auto qualified = quote_identifier(alias) + ".items";
    auto create_sql = "CREATE TABLE " + qualified + "(id INTEGER);";
    auto insert_sql = "INSERT INTO " + qualified + " VALUES (1);";
    auto select_sql = "SELECT COUNT(*) FROM " + qualified + ";";
    EXPECT_NO_THROW(sqlite::execute(main, create_sql, true));
    EXPECT_NO_THROW(sqlite::execute(main, insert_sql, true));
    EXPECT_NO_THROW({
        sqlite::query check(main, select_sql);
        auto res = check.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get_int(0), 1);
    });
    EXPECT_NO_THROW(main.detach(alias));
    EXPECT_THROW(sqlite::execute(main, "SELECT COUNT(*) FROM " + qualified + ";", true),
                 sqlite::database_exception);
}

TEST(ConnectionTest, RelativePathSupported) {
    std::filesystem::path relative = "vsqlitepp_relative.db";
    std::error_code ec;
    std::filesystem::remove(relative, ec);
    {
        sqlite::connection conn(relative.string());
        sqlite::execute(conn, "CREATE TABLE rel(id INTEGER);", true);
    }
    std::filesystem::remove(relative, ec);
    EXPECT_FALSE(std::filesystem::exists(relative));
}

TEST(SnapshotTest, TransactionSnapshotProvidesHistoricalReads) {
    if(!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    TempFile db("snapshot_read");
    sqlite::connection writer(db.string());
    sqlite::enable_wal(writer);
    sqlite::execute(writer, "CREATE TABLE docs(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::command insert(writer, "INSERT INTO docs(value) VALUES (?);");
        insert % std::string("before");
        insert.emit();
    }

    sqlite::connection reader(db.string());
    sqlite::enable_wal(reader);

    sqlite::transaction capture(reader, sqlite::transaction_type::deferred);
    auto snap = capture.take_snapshot();
    capture.commit();

    {
        sqlite::command update(writer, "UPDATE docs SET value=? WHERE id=1;");
        update % std::string("after");
        update.emit();
    }

    sqlite::transaction replay(reader, sqlite::transaction_type::deferred);
    replay.open_snapshot(snap);
    sqlite::query past(reader, "SELECT value FROM docs WHERE id=1;");
    auto past_res = past.get_result();
    ASSERT_TRUE(past_res->next_row());
    EXPECT_EQ(past_res->get_string(0), "before");
    replay.commit();

    sqlite::transaction latest(reader, sqlite::transaction_type::deferred);
    sqlite::query now(reader, "SELECT value FROM docs WHERE id=1;");
    auto now_res = now.get_result();
    ASSERT_TRUE(now_res->next_row());
    EXPECT_EQ(now_res->get_string(0), "after");
    latest.commit();
}

TEST(SnapshotTest, SavepointSnapshotControlsScope) {
    if(!sqlite::snapshots_supported()) {
        GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";
    }
    TempFile db("snapshot_savepoint");
    sqlite::connection writer(db.string());
    sqlite::enable_wal(writer, true);
    sqlite::execute(writer, "CREATE TABLE numbers(id INTEGER PRIMARY KEY, value INTEGER);", true);
    sqlite::execute(writer, "INSERT INTO numbers(value) VALUES (42);", true);

    sqlite::connection reader(db.string());
    sqlite::enable_wal(reader);

    sqlite::transaction read_tx(reader, sqlite::transaction_type::deferred);
    sqlite::savepoint guard(reader, "snap");
    auto snap = guard.take_snapshot();
    guard.release();
    read_tx.commit();

    sqlite::execute(writer, "UPDATE numbers SET value = 7;", true);

    sqlite::transaction scoped(reader, sqlite::transaction_type::deferred);
    sqlite::savepoint branch(reader, "snap_branch");
    branch.open_snapshot(snap);
    sqlite::query q(reader, "SELECT value FROM numbers;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 42);
    branch.release();
    scoped.commit();
}

TEST(SessionTest, CapturesAndAppliesChangeset) {
    if(!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    TempFile db("session_changeset");
    sqlite::connection writer(db.string());
    sqlite::connection replica(db.string());
    sqlite::execute(writer, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(replica, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);

    sqlite::session tracker(writer);
    tracker.attach_all();
    sqlite::execute(writer, "INSERT INTO items(value) VALUES ('alpha');", true);
    sqlite::execute(writer, "UPDATE items SET value='beta' WHERE id=1;", true);
    auto changes = tracker.changeset();
    ASSERT_FALSE(changes.empty());

    EXPECT_NO_THROW(sqlite::apply_changeset(replica, changes));
    sqlite::query q(replica, "SELECT value FROM items WHERE id=1;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_string(0), "beta");
}

TEST(SessionTest, PatchsetTracksDeletes) {
    if(!sqlite::sessions_supported()) {
        GTEST_SKIP() << "SQLite session API not available in this build.";
    }
    TempFile db("session_patchset");
    sqlite::connection writer(db.string());
    sqlite::connection replica(db.string());
    sqlite::execute(writer, "CREATE TABLE logs(id INTEGER PRIMARY KEY, msg TEXT);", true);
    sqlite::execute(replica, "CREATE TABLE logs(id INTEGER PRIMARY KEY, msg TEXT);", true);
    sqlite::execute(writer, "INSERT INTO logs VALUES (1, 'keep');", true);
    sqlite::execute(writer, "INSERT INTO logs VALUES (2, 'drop');", true);
    sqlite::execute(replica, "INSERT INTO logs VALUES (1, 'keep');", true);
    sqlite::execute(replica, "INSERT INTO logs VALUES (2, 'drop');", true);

    sqlite::session tracker(writer);
    tracker.attach_all();
    sqlite::execute(writer, "DELETE FROM logs WHERE id=2;", true);
    auto patch = tracker.patchset();
    ASSERT_FALSE(patch.empty());

    EXPECT_NO_THROW(sqlite::apply_patchset(replica, patch));
    sqlite::query q(replica, "SELECT COUNT(*) FROM logs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 1);
}

TEST(SerializationTest, RoundTripsInMemoryDatabase) {
    if(!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection source(":memory:");
    sqlite::execute(source, "CREATE TABLE docs(id INTEGER PRIMARY KEY, body TEXT);", true);
    sqlite::execute(source, "INSERT INTO docs(body) VALUES ('alpha'), ('beta');", true);

    auto image = sqlite::serialize(source);
    ASSERT_FALSE(image.empty());

    sqlite::connection restored(":memory:");
    EXPECT_NO_THROW(sqlite::deserialize(restored, image));
    sqlite::query q(restored, "SELECT COUNT(*) FROM docs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 2);
}

TEST(FunctionTest, RegistersScalarFunction) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "repeat_text",
        [](std::string_view text, int times) -> std::string {
            auto copies = times < 0 ? 0 : times;
            std::string out;
            out.reserve(text.size() * static_cast<std::size_t>(copies));
            for(int i = 0; i < copies; ++i) {
                out.append(text);
            }
            return out;
        },
        {.deterministic = true}
    );

    sqlite::query q(conn, "SELECT repeat_text('ab', 3);");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_string(0), "ababab");
}

TEST(FunctionTest, OptionalAndBlobArguments) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "maybe_concat",
        [](std::optional<std::string_view> prefix, std::string_view value) -> std::optional<std::string> {
            if(!prefix) {
                return std::nullopt;
            }
            std::string out(prefix->data(), prefix->size());
            out.append(value);
            return out;
        }
    );

    sqlite::query null_check(conn, "SELECT maybe_concat(NULL, 'beta') IS NULL;");
    auto null_res = null_check.get_result();
    ASSERT_TRUE(null_res->next_row());
    EXPECT_EQ(null_res->get_int(0), 1);

    sqlite::query value_check(conn, "SELECT maybe_concat('pre', 'beta');");
    auto value_res = value_check.get_result();
    ASSERT_TRUE(value_res->next_row());
    EXPECT_EQ(value_res->get_string(0), "prebeta");

    sqlite::create_function(conn, "blob_sum",
        [](std::span<const std::byte> blob) -> std::int64_t {
            std::int64_t total = 0;
            for(std::byte b : blob) {
                total += std::to_integer<unsigned>(b);
            }
            return total;
        }
    );

    sqlite::query blob(conn, "SELECT blob_sum(x'00010203FF');");
    auto blob_res = blob.get_result();
    ASSERT_TRUE(blob_res->next_row());
    EXPECT_EQ(blob_res->get_int64(0), 0 + 1 + 2 + 3 + 255);

    sqlite::create_function(conn, "explode",
        []() -> int {
            throw sqlite::database_exception("boom");
        }
    );
    EXPECT_THROW(sqlite::execute(conn, "SELECT explode();", true), sqlite::database_exception);
}

TEST(ConnectionTest, MissingParentDirectoryRejected) {
    auto db_path = test_root() / "missing_parent" / "db.sqlite";
    std::filesystem::remove_all(db_path.parent_path());
    EXPECT_THROW(sqlite::connection fail(db_path.string()), sqlite::database_exception);
}

TEST(ConnectionTest, ParentMustBeDirectory) {
    auto parent = test_root() / "not_dir_parent";
    {
        std::ofstream f(parent);
        f << "x";
    }
    auto db_path = parent / "db.sqlite";
    EXPECT_THROW(sqlite::connection fail(db_path.string()), sqlite::database_exception);
    std::error_code ec;
    std::filesystem::remove(parent, ec);
}

#ifndef _WIN32
TEST(ConnectionTest, ParentSymlinkRejected) {
    auto real_dir = test_root() / "real_parent";
    std::filesystem::create_directories(real_dir);
    auto link_dir = test_root() / "parent_link";
    std::error_code ec;
    std::filesystem::remove(link_dir, ec);
    std::filesystem::create_directory_symlink(real_dir, link_dir);
    auto db_path = link_dir / "db.sqlite";
    EXPECT_THROW(sqlite::connection fail(db_path.string()), sqlite::database_exception);
    std::filesystem::remove(link_dir, ec);
    std::filesystem::remove_all(real_dir, ec);
}
#endif

TEST(ConnectionTest, PathMustBeRegularFile) {
    auto dir = test_root() / "dir_as_db";
    std::filesystem::create_directories(dir);
    EXPECT_THROW(sqlite::connection fail(dir.string(), sqlite::open_mode::open_existing),
                 sqlite::database_exception);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(ConnectionTest, SpecialMemoryUri) {
    sqlite::connection conn(unique_memory_uri());
    sqlite::execute(conn, "CREATE TABLE IF NOT EXISTS data(id INTEGER);", true);
    sqlite::execute(conn, "DELETE FROM data;", true);
    sqlite::execute(conn, "INSERT INTO data VALUES (1);", true);
    sqlite::query q(conn, "SELECT COUNT(*) FROM data;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 1);
}

#ifndef _WIN32
TEST(ConnectionTest, AlwaysCreateRejectsSymlinkTargets) {
    TempFile real("real_db_for_symlink");
    {
        sqlite::connection conn(real.string());
    }
    auto link_path = test_root() / "db_link.sqlite";
    std::error_code ec;
    std::filesystem::remove(link_path, ec);
    std::filesystem::create_symlink(real.path, link_path);
    EXPECT_THROW(sqlite::connection fail(link_path.string(), sqlite::open_mode::always_create),
                 sqlite::database_exception);
    std::filesystem::remove(link_path, ec);
}
#endif

TEST(ConnectionTest, AlwaysCreateRejectsDirectories) {
    auto dir = test_root() / "dir_target_for_always_create";
    std::filesystem::create_directories(dir);
    EXPECT_THROW(sqlite::connection fail(dir.string(), sqlite::open_mode::always_create),
                 sqlite::database_exception);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST(ThreadingTest, ConfigureSerializedMode) {
    ASSERT_TRUE(sqlite::configure_threading(sqlite::threading_mode::serialized));
    EXPECT_EQ(sqlite::current_threading_mode(), sqlite::threading_mode::serialized);
}

TEST(ConnectionPoolTest, BlocksUntilConnectionReturns) {
    TempFile file("pool_db");
    {
        sqlite::connection setup(file.string());
        sqlite::execute(setup, "CREATE TABLE logs(id INTEGER PRIMARY KEY AUTOINCREMENT, tag TEXT);", true);
    }

    sqlite::connection_pool pool(2, sqlite::connection_pool::make_factory(file.string()));
    auto lease1 = pool.acquire();
    auto lease2 = pool.acquire();

    std::atomic<bool> worker_acquired{false};
    std::thread worker([&]() {
        auto lease3 = pool.acquire();
        sqlite::command cmd(*lease3, "INSERT INTO logs(tag) VALUES (?);");
        cmd % std::string("worker");
        cmd.emit();
        worker_acquired = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker_acquired.load());
    lease1 = {};
    worker.join();
    EXPECT_TRUE(worker_acquired.load());
    EXPECT_LE(pool.created_count(), pool.capacity());

    sqlite::connection reader(file.string());
    sqlite::query q(reader, "SELECT COUNT(*) FROM logs;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 1);
}

TEST(CommandQueryTest, BindsAndRetrievesData) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn,
                    "CREATE TABLE sample(id INTEGER PRIMARY KEY, "
                    "ival INT, lval BIGINT, note TEXT, amount REAL, data BLOB, nullable TEXT);",
                    true);

    sqlite::command insert(conn, "INSERT INTO sample(ival, lval, note, amount, data, nullable) "
                                 "VALUES (?, ?, ?, ?, ?, ?);");
    std::vector<unsigned char> blob{0, 1, 2, 3, 4};
    std::array<unsigned char, 3> blob_view_data{9, 8, 7};
    insert.bind(1, 42);
    insert.bind(2, static_cast<std::int64_t>(1) << 40);
    insert.bind(3, std::string_view("hello"));
    insert.bind(4, 3.14);
    insert.bind(5, std::span<const unsigned char>(blob));
    insert.bind(6);
    insert.emit();
    insert.clear();
    std::string_view world_view("world_view");
    insert % 7 % static_cast<std::int64_t>(9007199254740993LL) % world_view % 2.71 % std::span<const unsigned char>(blob_view_data) % std::string("value");
    insert();

    sqlite::query q(conn, "SELECT id, ival, lval, note, amount, data, nullable FROM sample ORDER BY id;");
    auto result = q.get_result();
    ASSERT_TRUE(result->next_row());
    EXPECT_EQ(result->get_column_count(), 7);
    EXPECT_EQ(result->get_column_name(0), "id");
    EXPECT_EQ(result->get_column_decltype(2), "BIGINT");
    EXPECT_EQ(result->get_int(1), 42);
    EXPECT_EQ(result->get_int64(2), static_cast<std::int64_t>(1) << 40);
    EXPECT_EQ(result->get_string(3), "hello");
    EXPECT_DOUBLE_EQ(result->get_double(4), 3.14);
    auto note_view = result->get_string_view(3);
    EXPECT_EQ(note_view, "hello");
    auto stored_blob = load_blob(*result, 5);
    EXPECT_EQ(stored_blob, blob);
    auto blob_span = result->get_binary_span(5);
    EXPECT_EQ(blob_span.size(), blob.size());
    EXPECT_TRUE(std::equal(blob_span.begin(), blob_span.end(), blob.begin()));
    EXPECT_EQ(result->get_variant(5).index(), 6); // blob variant
    EXPECT_EQ(result->get_binary_size(5), blob.size());
    EXPECT_EQ(result->get_variant(6).index(), 5); // null

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    auto row_count = result->get_row_count();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    EXPECT_GE(row_count, 0);
    std::array<unsigned char, 2> tiny{};
    EXPECT_THROW(result->get_binary(5, tiny.data(), tiny.size()), sqlite::buffer_too_small_exception);

    EXPECT_TRUE(result->next_row());
    auto v = result->get_variant(2);
    EXPECT_TRUE(std::holds_alternative<std::int64_t>(v));
    EXPECT_EQ(std::get<std::int64_t>(v), 9007199254740993LL);
    auto text_variant = result->get_variant(3);
    EXPECT_TRUE(std::holds_alternative<std::string>(text_variant));
    auto string_view_second = result->get_string_view(3);
    EXPECT_EQ(string_view_second, world_view);
    auto blob_span_second = result->get_binary_span(5);
    EXPECT_EQ(blob_span_second.size(), blob_view_data.size());
    EXPECT_TRUE(std::equal(blob_span_second.begin(), blob_span_second.end(), blob_view_data.begin()));
    EXPECT_EQ(result->get_variant(4).index(), 3); // long double
    auto blob_variant = result->get_variant(5);
    EXPECT_TRUE(std::holds_alternative<sqlite::blob_ref_t>(blob_variant));
    EXPECT_FALSE(result->next_row());
    EXPECT_TRUE(result->end());
    result->reset();
    EXPECT_TRUE(result->next_row());

    sqlite::query count_query(conn, "SELECT COUNT(*) FROM sample;");
    auto emitted = count_query.emit_result();
    EXPECT_FALSE(emitted->next_row());
}

TEST(TransactionTest, TransactionAndSavepoint) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT);", true);
    {
        sqlite::transaction txn(conn, sqlite::transaction_type::exclusive);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("temporary");
        insert.emit();
        sqlite::savepoint sp(conn, "sp1");
        sqlite::command insert2(conn, "INSERT INTO items(value) VALUES (?);");
        insert2 % std::string("rollback");
        insert2.emit();
        sp.rollback();
        sp.release();
        txn.rollback();
    }
    EXPECT_EQ(count_rows(conn, "items"), 0);

    {
        sqlite::transaction txn(conn, sqlite::transaction_type::immediate);
        sqlite::command insert(conn, "INSERT INTO items(value) VALUES (?);");
        insert % std::string("keep");
        insert.emit();
        txn.commit();
    }
    EXPECT_EQ(count_rows(conn, "items"), 1);
}

TEST(BackupTest, CopiesBetweenDatabases) {
    TempFile source("backup_source");
    TempFile destination("backup_destination");
    {
        sqlite::connection conn(source.string());
        sqlite::execute(conn, "CREATE TABLE records(id INTEGER PRIMARY KEY, note TEXT);", true);
        sqlite::command insert(conn, "INSERT INTO records(note) VALUES (?);");
        insert % std::string("data from source");
        insert.emit();
    }

    {
        sqlite::connection src(source.string(), sqlite::open_mode::open_existing);
        sqlite::connection dst(destination.string(), sqlite::open_mode::open_or_create);
        sqlite::backup backup(dst, src);
        while (backup.step(1)) {
        }
        backup.finish();
    }

    {
        sqlite::connection dst(destination.string(), sqlite::open_mode::open_readonly);
        sqlite::query q(dst, "SELECT note FROM records;");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get_string(0), "data from source");
    }
}

TEST(BackupTest, StepAfterFinishThrows) {
    TempFile source("backup_source_again");
    TempFile destination("backup_destination_again");
    sqlite::connection src(source.string());
    sqlite::connection dst(destination.string());
    sqlite::backup backup(dst, src);
    backup.finish();
    EXPECT_THROW(backup.step(1), sqlite::database_exception);
}

TEST(ViewTest, CreateAndDropView) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE base(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(conn, "INSERT INTO base(value) VALUES ('A'), ('B');", true);
    sqlite::view vw(conn);
    vw.create(false, "main", "view name with spaces", "SELECT * FROM base");
    sqlite::query q(conn, "SELECT COUNT(*) FROM \"view name with spaces\";");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 2);
    vw.drop("view name with spaces");
    EXPECT_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM \"view name with spaces\";", true),
                 sqlite::database_exception);
}

TEST(ViewTest, CreateAndDropWithDatabaseName) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE foo(id INTEGER PRIMARY KEY);", true);
    sqlite::view vw(conn);
    vw.create(false, "main", "foo_view_plain", "SELECT * FROM foo");
    sqlite::query q(conn, "SELECT COUNT(*) FROM foo_view_plain;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get_int(0), 0);
    vw.drop("main", "foo_view_plain");
    EXPECT_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM foo_view_plain;", true),
                 sqlite::database_exception);
}
