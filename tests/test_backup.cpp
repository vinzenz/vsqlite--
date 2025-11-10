#include "test_common.hpp"

#include <sqlite/backup.hpp>
#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
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
}
