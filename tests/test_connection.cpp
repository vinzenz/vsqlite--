#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>

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
