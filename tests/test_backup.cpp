#include "test_common.hpp"

#include <sqlite/backup.hpp>
#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>

using namespace testhelpers;

TEST(BackupTest, CopiesBetweenDatabases) {
    TempFile src_file("backup_src");
    TempFile dst_file("backup_dst");
    sqlite::connection src(src_file.string());
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);

    sqlite::connection dst(dst_file.string());
    sqlite::backup job(dst, src);
    while (job.step()) {
    }
    job.finish();

    sqlite::query q(dst, "SELECT COUNT(*) FROM data;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
}

TEST(BackupTest, StepAfterFinishThrows) {
    TempFile src_file("backup_src2");
    TempFile dst_file("backup_dst2");
    sqlite::connection src(src_file.string());
    sqlite::connection dst(dst_file.string());
    sqlite::backup job(dst, src);
    job.finish();
    EXPECT_THROW(job.step(), sqlite::database_exception);
    job.finish();
}

namespace {
// Creates a destination file first so that it can be re-opened read-only.
void init_readonly_destination(TempFile const &dst_file) {
    sqlite::connection init(dst_file.string());
    sqlite::execute(init, "CREATE TABLE placeholder(id INTEGER PRIMARY KEY);", true);
}
} // namespace

TEST(BackupTest, FailedBackupFinishThenDestructionDoesNotCrash) {
    TempFile src_file("backup_fail_src");
    TempFile dst_file("backup_fail_dst");
    init_readonly_destination(dst_file);

    sqlite::connection src(src_file.string());
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);

    sqlite::connection dst(dst_file.string(), sqlite::open_mode::open_readonly);
    sqlite::backup job(dst, src);
    EXPECT_THROW(job.step(), sqlite::database_exception);

    try {
        job.finish();
        ADD_FAILURE() << "finish() should have reported the backup failure";
    } catch (sqlite::database_exception_code const &e) {
        EXPECT_EQ(e.error_code(), SQLITE_READONLY);
    }

    // The backup handle is released by the first finish() call even though it
    // reported an error: repeated calls and the upcoming destruction must not
    // operate on the released handle anymore.
    job.finish();
}

TEST(BackupTest, DestructorFinishesFailedBackupWithoutExplicitFinish) {
    TempFile src_file("backup_dtor_src");
    TempFile dst_file("backup_dtor_dst");
    init_readonly_destination(dst_file);

    sqlite::connection src(src_file.string());
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);

    sqlite::connection dst(dst_file.string(), sqlite::open_mode::open_readonly);
    sqlite::backup job(dst, src);
    EXPECT_THROW(job.step(), sqlite::database_exception);
    // No explicit finish() here: the destructor releases the failed backup and
    // must survive the error it reports.
}
