#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/execute.hpp>

#include <thread>
#include <vector>

TEST(ConnectionThreadSafetyTest, StatementCacheConcurrentAccess) {
    sqlite::connection con(":memory:");
    sqlite::execute(con, "CREATE TABLE concurrency(value INTEGER);", true);

    constexpr int kThreads    = 4;
    constexpr int kIterations = 64;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t] {
            for (int i = 0; i < kIterations; ++i) {
                sqlite::command insert(con, "INSERT INTO concurrency(value) VALUES (?);");
                insert % (t * kIterations + i);
                insert();

                sqlite::command query(con, "SELECT COUNT(*) FROM concurrency WHERE value = ?;");
                query % (t * kIterations + i);
                query();
            }
        });
    }

    for (auto &worker : workers) {
        worker.join();
    }

    EXPECT_EQ(testhelpers::count_rows(con, "concurrency"), kThreads * kIterations);
}
