#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/private/private_accessor.hpp>

#include <filesystem>
#include <fstream>

using namespace testhelpers;

TEST(ConnectionTest, OpenModesAndLastInsertId) {
    TempFile file("open_modes");
    {
        sqlite::connection conn(file.string());
        sqlite::execute(conn, "CREATE TABLE sample(id INTEGER PRIMARY KEY, name TEXT);", true);
        sqlite::command insert(conn, "INSERT INTO sample(name) VALUES (?);");
        insert % std::string("alpha");
        insert.step_once();
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
#if defined(_WIN32)
    GTEST_SKIP() << "Symbolic link creation not supported on this platform.";
#endif
    TempFile real("real_db");
    {
        sqlite::connection conn(real.string());
        sqlite::execute(conn, "CREATE TABLE t(id INTEGER);", true);
    }
    auto link = test_root() / "symlinked.db";
    std::error_code ec;
    std::filesystem::remove(link, ec);
    std::filesystem::create_symlink(real.path, link, ec);
    ASSERT_FALSE(ec);
    EXPECT_THROW(sqlite::connection symlink(link.string()), sqlite::database_exception);
    std::filesystem::remove(link, ec);
}

TEST(ConnectionTest, AttachAndDetachQuoteIdentifiers) {
    sqlite::connection main(":memory:");
    TempFile attached("attached_db");
    auto alias = std::string("alias name\";DROP");
    EXPECT_THROW(main.attach(attached.string(), ""), sqlite::database_exception);
    EXPECT_THROW(main.detach(""), sqlite::database_exception);

    ASSERT_NO_THROW(main.attach(attached.string(), alias));
    auto qualified  = quote_identifier(alias) + ".items";
    auto create_sql = "CREATE TABLE " + qualified + "(id INTEGER);";
    auto insert_sql = "INSERT INTO " + qualified + " VALUES (1);";
    auto select_sql = "SELECT COUNT(*) FROM " + qualified + ";";
    EXPECT_NO_THROW(sqlite::execute(main, create_sql, true));
    EXPECT_NO_THROW(sqlite::execute(main, insert_sql, true));
    EXPECT_NO_THROW({
        sqlite::query check(main, select_sql);
        auto res = check.get_result();
        ASSERT_TRUE(res->next_row());
        EXPECT_EQ(res->get<int>(0), 1);
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

TEST(ConnectionTest, MissingParentDirectoryRejected) {
    auto root = test_root();
    auto path = root / "missing_parent" / "db.sqlite";
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);
    EXPECT_THROW(sqlite::connection conn(path.string()), sqlite::database_exception);
}

TEST(ConnectionTest, ParentMustBeDirectory) {
    TempFile file("parent_file");
    auto path = file.path / "child.db";
    EXPECT_THROW(sqlite::connection conn(path.string()), sqlite::database_exception);
}

TEST(ConnectionTest, ParentSymlinkRejected) {
#if defined(_WIN32)
    GTEST_SKIP() << "Symbolic link creation not supported on this platform.";
#endif
    TempFile target("link_target");
    auto link_dir = test_root() / "link_dir";
    std::error_code ec;
    std::filesystem::remove(link_dir, ec);
    std::filesystem::create_directory(target.path);
    std::filesystem::create_directory_symlink(target.path, link_dir, ec);
    ASSERT_FALSE(ec);
    auto path = link_dir / "db.sqlite";
    EXPECT_THROW(sqlite::connection conn(path.string()), sqlite::database_exception);
    std::filesystem::remove(link_dir, ec);
}

TEST(ConnectionTest, PathMustBeRegularFile) {
    auto dir = test_root() / "regular_dir";
    std::filesystem::create_directories(dir);
    EXPECT_THROW(sqlite::connection conn(dir.string()), sqlite::database_exception);
}

TEST(ConnectionTest, SpecialMemoryUri) {
    auto uri = unique_memory_uri();
    sqlite::connection conn(uri);
    sqlite::execute(conn, "CREATE TABLE IF NOT EXISTS memtest(id INTEGER);", true);
    // A named memory URI must never materialize as a file on disk.
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(uri), ec));
}

TEST(ConnectionTest, NamedMemoryUriSharingAcrossConnections) {
    auto uri = unique_memory_uri();
    sqlite::connection first(uri);
    sqlite::execute(first, "CREATE TABLE memtest(id INTEGER);", true);
    sqlite::execute(first, "INSERT INTO memtest VALUES (42);", true);

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(uri), ec));

    {
        sqlite::connection second(uri);
        EXPECT_EQ(count_rows(second, "memtest"), 1);
        sqlite::execute(second, "INSERT INTO memtest VALUES (43);", true);
    }
    // The database must stay alive while at least one connection remains open.
    EXPECT_EQ(count_rows(first, "memtest"), 2);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(uri), ec));
}

TEST(ConnectionTest, MemoryUriRequiresExactModeParameter) {
    TempFile file("uri_not_memory");
    // "?xmode=memory" is not a mode parameter: the URI names a real file.
    {
        sqlite::connection conn("file:" + file.path.string() + "?xmode=memory");
        sqlite::execute(conn, "CREATE TABLE diskcheck(id INTEGER);", true);
    }
    EXPECT_TRUE(std::filesystem::exists(file.path));

    // Unknown mode values are rejected by SQLite when opening.
    EXPECT_THROW(sqlite::connection bad("file:" + file.path.string() + "?mode=bogus"),
                 sqlite::database_exception);
}

TEST(ConnectionTest, PercentEncodedMemoryParameter) {
    static std::atomic<uint64_t> counter{0};
    auto name = (test_root() / ("encoded_mem_" + std::to_string(counter++))).string();
    // "%6d" decodes to 'm', so the effective mode parameter is "memory".
    std::string uri = "file:" + name + "?mode=%6demory&cache=shared";
    sqlite::connection conn(uri);
    sqlite::execute(conn, "CREATE TABLE memtest(id INTEGER);", true);

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(name, ec));
}

TEST(ConnectionTest, MemoryUriNeedsNoExistingDirectory) {
    auto missing = test_root() / "no_such_parent_dir" / "mem.db";
    std::error_code ec;
    std::filesystem::remove_all(missing.parent_path(), ec);
    // With mode=memory the path is a pure database name: no directory checks.
    sqlite::connection conn("file:" + missing.string() + "?mode=memory");
    sqlite::execute(conn, "CREATE TABLE memtest(id INTEGER);", true);
    EXPECT_FALSE(std::filesystem::exists(missing, ec));
}

TEST(ConnectionTest, FileUriPathIsValidated) {
    auto missing = test_root() / "no_such_parent_dir" / "db.sqlite";
    std::error_code ec;
    std::filesystem::remove_all(missing.parent_path(), ec);
    // Without mode=memory the URI path is validated like any other path.
    EXPECT_THROW(sqlite::connection conn("file:" + missing.string()), sqlite::database_exception);

    TempFile file("uri_existing");
    {
        sqlite::connection conn("file:" + file.path.string());
        sqlite::execute(conn, "CREATE TABLE t(id INTEGER);", true);
    }
    sqlite::connection readonly("file:" + file.path.string(), sqlite::open_mode::open_readonly);
    EXPECT_NO_THROW(sqlite::execute(readonly, "SELECT COUNT(*) FROM t;", true));

    TempFile absent("uri_missing");
    std::filesystem::remove(absent.path, ec);
    EXPECT_THROW(
        sqlite::connection fail("file:" + absent.path.string(), sqlite::open_mode::open_existing),
        sqlite::database_exception);
}

TEST(ConnectionTest, AttachMemoryUri) {
    sqlite::connection main(unique_memory_uri());
    sqlite::execute(main, "CREATE TABLE base(id INTEGER);", true);

    auto uri = unique_memory_uri();
    ASSERT_NO_THROW(main.attach(uri, "mem"));
    EXPECT_NO_THROW(sqlite::execute(main, "CREATE TABLE mem.attached(id INTEGER);", true));
    EXPECT_NO_THROW(sqlite::execute(main, "INSERT INTO mem.attached VALUES (1);", true));

    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(uri), ec));
    EXPECT_NO_THROW(main.detach("mem"));
}

TEST(ConnectionTest, AlwaysCreateRejectsSymlinkTargets) {
#if defined(_WIN32)
    GTEST_SKIP() << "Symbolic link creation not supported on this platform.";
#endif
    TempFile real("always_real");
    TempFile link_target("link_target");
    std::error_code ec;
    std::filesystem::remove(real.path);
    std::filesystem::create_symlink(link_target.path, real.path, ec);
    ASSERT_FALSE(ec);
    EXPECT_THROW(sqlite::connection conn(real.string(), sqlite::open_mode::always_create),
                 sqlite::database_exception);
    std::filesystem::remove(real.path, ec);
}

TEST(ConnectionTest, AlwaysCreateRejectsDirectories) {
    auto dir = test_root() / "always_dir";
    std::filesystem::create_directories(dir);
    EXPECT_THROW(sqlite::connection conn(dir.string(), sqlite::open_mode::always_create),
                 sqlite::database_exception);
}

TEST(ConnectionTest, RepeatedCloseIsHarmless) {
    sqlite::connection conn(":memory:");
    sqlite::execute(conn, "CREATE TABLE close_check(id INTEGER);", true);
    EXPECT_NO_THROW(sqlite::private_accessor::close(conn));
    // The native handle was consumed by the first close call, so closing
    // again is a no-op instead of an error.
    EXPECT_NO_THROW(sqlite::private_accessor::close(conn));
    // Genuinely invalid use of the closed connection still throws.
    EXPECT_THROW(sqlite::execute(conn, "SELECT 1;", true), sqlite::database_exception);
    EXPECT_THROW(conn.get_last_insert_rowid(), sqlite::database_exception);
    // Destruction after the explicit close calls must not throw either.
}

TEST(ConnectionTest, OpenFileFactoryCreatesAndReopens) {
    TempFile file("factory_db");
    {
        auto conn = sqlite::connection::open_file(file.path);
        sqlite::execute(conn, "CREATE TABLE factory_t(id INTEGER);", true);
    }
    EXPECT_TRUE(std::filesystem::exists(file.path));

    {
        auto conn =
            sqlite::connection::open_file(file.path, {.mode = sqlite::open_mode::open_existing});
        EXPECT_NO_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM factory_t;", true));
    }

    {
        auto conn =
            sqlite::connection::open_file(file.path, {.mode = sqlite::open_mode::open_readonly});
        EXPECT_NO_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM factory_t;", true));
        EXPECT_THROW(sqlite::execute(conn, "INSERT INTO factory_t VALUES (1);", true),
                     sqlite::database_exception);
    }

    TempFile missing("factory_missing");
    EXPECT_THROW(auto conn = sqlite::connection::open_file(
                     missing.path, {.mode = sqlite::open_mode::open_existing}),
                 sqlite::database_exception);
}

TEST(ConnectionTest, OpenFileFactoryAlwaysCreateRecreates) {
    TempFile file("factory_recreate");
    {
        std::ofstream sentinel(file.path);
        sentinel << "SENTINEL";
    }
    {
        auto conn =
            sqlite::connection::open_file(file.path, {.mode = sqlite::open_mode::always_create});
        sqlite::execute(conn, "CREATE TABLE reset_check(id INTEGER);", true);
    }
    std::error_code ec;
    EXPECT_GT(std::filesystem::file_size(file.path, ec), 8u);
}

TEST(ConnectionTest, OpenFileFactoryAlwaysCreateRejectsSymlinks) {
#if defined(_WIN32)
    GTEST_SKIP() << "Symbolic link creation not supported on this platform.";
#endif
    TempFile real("factory_always_real");
    TempFile link_target("factory_link_target");
    std::error_code ec;
    std::filesystem::remove(real.path);
    std::filesystem::create_symlink(link_target.path, real.path, ec);
    ASSERT_FALSE(ec);
    EXPECT_THROW(auto conn = sqlite::connection::open_file(
                     real.path, {.mode = sqlite::open_mode::always_create}),
                 sqlite::database_exception);
    std::filesystem::remove(real.path, ec);
}

TEST(ConnectionTest, OpenFileFactoryRejectsSpecialNames) {
    EXPECT_THROW(auto conn = sqlite::connection::open_file(":memory:"), sqlite::database_exception);
    TempFile file("factory_uri_name");
    EXPECT_THROW(auto conn = sqlite::connection::open_file("file:" + file.string()),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_file(""), sqlite::database_exception);
}

TEST(ConnectionTest, OpenMemoryFactoryCreatesNoFile) {
    {
        auto conn = sqlite::connection::open_memory();
        sqlite::execute(conn, "CREATE TABLE mem(id INTEGER);", true);
    }
}

TEST(ConnectionTest, NamedMemoryFactorySharesWithoutFile) {
    static std::atomic<uint64_t> counter{0};
    auto name = "factory_mem_" + std::to_string(counter++);
    {
        auto first = sqlite::connection::open_memory(name);
        sqlite::execute(first, "CREATE TABLE mem(id INTEGER);", true);
        sqlite::execute(first, "INSERT INTO mem VALUES (7);", true);
        {
            auto second = sqlite::connection::open_memory(name);
            EXPECT_EQ(count_rows(second, "mem"), 1);
            sqlite::execute(second, "INSERT INTO mem VALUES (8);", true);
        }
        EXPECT_EQ(count_rows(first, "mem"), 2);
    }
    // Neither the bare name nor anything derived from the URI may materialize.
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(name, ec));
    EXPECT_FALSE(std::filesystem::exists(test_root() / name, ec));
    EXPECT_FALSE(
        std::filesystem::exists("file:" + std::string(name) + "?mode=memory&cache=shared", ec));
    // The name is a pure key: no directory is ever inspected or created.
    EXPECT_NO_THROW({
        auto conn = sqlite::connection::open_memory("no_such_parent_dir/factory_mem");
        sqlite::execute(conn, "CREATE TABLE mem(id INTEGER);", true);
    });
}

TEST(ConnectionTest, OpenMemoryFactoryRejectsUnsupportedModes) {
    EXPECT_THROW(auto conn = sqlite::connection::open_memory(""), sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_memory(
                     "x", {.mode = sqlite::open_mode::always_create}),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_memory(
                     "x", {.mode = sqlite::open_mode::open_readonly}),
                 sqlite::database_exception);
}

TEST(ConnectionTest, OpenUriFactoryOpensPercentEncodedPaths) {
    auto dir = test_root() / "uri_factory_dir";
    std::filesystem::create_directories(dir);
    // Every character is a legal file name character on Linux, macOS, and Windows ('?'
    // is not on Windows), while each needs percent-encoding inside a "file:" URI.
    auto odd = dir / "enc #%.db";
    std::error_code ec;
    std::filesystem::remove(odd, ec);
    {
        auto conn = sqlite::connection::open_file(odd);
        sqlite::execute(conn, "CREATE TABLE uri_t(id INTEGER);", true);
    }
    // space -> %20, '#' -> %23, '%' -> %25
    std::string uri = "file:" + dir.string() + "/enc%20%23%25.db";
    {
        auto conn = sqlite::connection::open_uri(uri);
        EXPECT_NO_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM uri_t;", true));
    }
    {
        auto conn = sqlite::connection::open_uri(uri, {.mode = sqlite::open_mode::open_readonly});
        EXPECT_NO_THROW(sqlite::execute(conn, "SELECT COUNT(*) FROM uri_t;", true));
        EXPECT_THROW(sqlite::execute(conn, "INSERT INTO uri_t VALUES (1);", true),
                     sqlite::database_exception);
    }
    std::filesystem::remove(odd, ec);
}

TEST(ConnectionTest, OpenUriFactoryValidatesSchemeAndMode) {
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(""), sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri("http://localhost/db"),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri("plain.db"), sqlite::database_exception);

    TempFile file("uri_conflict");
    {
        auto conn = sqlite::connection::open_file(file.path);
        sqlite::execute(conn, "CREATE TABLE uri_t(id INTEGER);", true);
    }
    auto base = "file:" + file.string();

    // Contradictions are rejected before anything is opened:
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(
                     base + "?mode=rw", {.mode = sqlite::open_mode::open_readonly}),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(
                     base + "?mode=ro", {.mode = sqlite::open_mode::open_or_create}),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(
                     base + "?mode=create", {.mode = sqlite::open_mode::open_existing}),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(
                     base + "?mode=memory", {.mode = sqlite::open_mode::open_readonly}),
                 sqlite::database_exception);
    // "%72o" percent-decodes to "ro", so it still contradicts open_or_create:
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(
                     base + "?mode=%72o", {.mode = sqlite::open_mode::open_or_create}),
                 sqlite::database_exception);

    // Matching combinations open the database:
    EXPECT_NO_THROW({
        auto conn = sqlite::connection::open_uri(base + "?mode=rw",
                                                 {.mode = sqlite::open_mode::open_existing});
        sqlite::execute(conn, "SELECT COUNT(*) FROM uri_t;", true);
    });
    EXPECT_NO_THROW({
        auto conn = sqlite::connection::open_uri(base + "?mode=ro",
                                                 {.mode = sqlite::open_mode::open_readonly});
        sqlite::execute(conn, "SELECT COUNT(*) FROM uri_t;", true);
    });
    // Without a mode parameter the wrapper options govern:
    EXPECT_NO_THROW({
        auto conn = sqlite::connection::open_uri(base, {.mode = sqlite::open_mode::open_readonly});
        sqlite::execute(conn, "SELECT COUNT(*) FROM uri_t;", true);
    });
    // Unknown mode values are left to SQLite, which rejects them itself:
    EXPECT_THROW(auto conn = sqlite::connection::open_uri(base + "?mode=bogus"),
                 sqlite::database_exception);
    // Destructive recreation is refused outright:
    EXPECT_THROW(auto conn =
                     sqlite::connection::open_uri(base, {.mode = sqlite::open_mode::always_create}),
                 sqlite::database_exception);
}

TEST(ConnectionTest, NulBytesRejectedEverywhere) {
    std::string nul_name = std::string("bad\0name", 8);
    TempFile file("nul_guard");

    EXPECT_THROW(sqlite::connection conn(nul_name), sqlite::database_exception);
    EXPECT_THROW(sqlite::connection conn(nul_name, sqlite::open_mode::open_existing),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_file(std::filesystem::path(nul_name)),
                 sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_memory(nul_name), sqlite::database_exception);
    EXPECT_THROW(auto conn = sqlite::connection::open_uri("file:" + nul_name),
                 sqlite::database_exception);

    sqlite::connection conn(file.string());
    EXPECT_THROW(conn.attach(nul_name, "alias"), sqlite::database_exception);
}

TEST(ConnectionTest, MigrationDefaultConstructorMatchesOpenFile) {
    TempFile legacy("migrate_default_legacy");
    TempFile modern("migrate_default_modern");
    {
        sqlite::connection conn(legacy.string());
        sqlite::execute(conn, "CREATE TABLE t(id INTEGER);", true);
    }
    {
        auto conn = sqlite::connection::open_file(modern.path);
        sqlite::execute(conn, "CREATE TABLE t(id INTEGER);", true);
    }
    EXPECT_TRUE(std::filesystem::exists(legacy.path));
    EXPECT_TRUE(std::filesystem::exists(modern.path));
}

TEST(ConnectionTest, MigrationMemoryConstructorMatchesOpenMemory) {
    sqlite::connection legacy(":memory:");
    sqlite::execute(legacy, "CREATE TABLE t(id INTEGER);", true);
    auto modern = sqlite::connection::open_memory();
    sqlite::execute(modern, "CREATE TABLE t(id INTEGER);", true);
}

TEST(ConnectionTest, MigrationNamedMemoryUriMatchesOpenMemoryName) {
    static std::atomic<uint64_t> counter{0};
    auto name = "migrate_mem_" + std::to_string(counter++);
    sqlite::connection legacy("file:" + name + "?mode=memory&cache=shared");
    sqlite::execute(legacy, "CREATE TABLE t(id INTEGER);", true);
    sqlite::execute(legacy, "INSERT INTO t VALUES (5);", true);
    {
        // Same shared in-memory database as the legacy URI constructor.
        auto modern = sqlite::connection::open_memory(name);
        EXPECT_EQ(count_rows(modern, "t"), 1);
    }
    std::error_code ec;
    EXPECT_FALSE(std::filesystem::exists(name, ec));
}

TEST(ConnectionTest, MigrationFileUriConstructorMatchesOpenUri) {
    TempFile file("migrate_uri");
    std::string uri = "file:" + file.string();
    {
        sqlite::connection legacy(uri);
        sqlite::execute(legacy, "CREATE TABLE t(id INTEGER);", true);
    }
    auto modern = sqlite::connection::open_uri(uri);
    EXPECT_NO_THROW(sqlite::execute(modern, "SELECT COUNT(*) FROM t;", true));
}

TEST(ConnectionTest, MigrationOpenExistingConstructorMatchesOpenFile) {
    TempFile legacy("migrate_existing_legacy");
    TempFile modern("migrate_existing_modern");
    {
        sqlite::connection created(legacy.string());
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    {
        auto created = sqlite::connection::open_file(modern.path);
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    EXPECT_NO_THROW({
        sqlite::connection conn(legacy.string(), sqlite::open_mode::open_existing);
        sqlite::execute(conn, "SELECT COUNT(*) FROM t;", true);
    });
    EXPECT_NO_THROW({
        auto conn =
            sqlite::connection::open_file(modern.path, {.mode = sqlite::open_mode::open_existing});
        sqlite::execute(conn, "SELECT COUNT(*) FROM t;", true);
    });
}

TEST(ConnectionTest, MigrationOpenReadonlyConstructorMatchesOpenFile) {
    TempFile legacy("migrate_readonly_legacy");
    TempFile modern("migrate_readonly_modern");
    {
        sqlite::connection created(legacy.string());
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    {
        auto created = sqlite::connection::open_file(modern.path);
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    EXPECT_THROW(
        {
            sqlite::connection conn(legacy.string(), sqlite::open_mode::open_readonly);
            sqlite::execute(conn, "INSERT INTO t VALUES (1);", true);
        },
        sqlite::database_exception);
    EXPECT_THROW(
        {
            auto conn = sqlite::connection::open_file(legacy.path,
                                                      {.mode = sqlite::open_mode::open_readonly});
            sqlite::execute(conn, "INSERT INTO t VALUES (1);", true);
        },
        sqlite::database_exception);
}

TEST(ConnectionTest, MigrationAlwaysCreateConstructorMatchesOpenFile) {
    TempFile legacy("migrate_always_legacy");
    TempFile modern("migrate_always_modern");
    {
        sqlite::connection created(legacy.string());
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    {
        auto created = sqlite::connection::open_file(modern.path);
        sqlite::execute(created, "CREATE TABLE t(id INTEGER);", true);
    }
    {
        sqlite::connection recreated(legacy.string(), sqlite::open_mode::always_create);
        sqlite::execute(recreated, "CREATE TABLE fresh(id INTEGER);", true);
    }
    {
        auto recreated =
            sqlite::connection::open_file(modern.path, {.mode = sqlite::open_mode::always_create});
        sqlite::execute(recreated, "CREATE TABLE fresh(id INTEGER);", true);
    }
}
