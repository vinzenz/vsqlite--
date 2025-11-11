#include "test_common.hpp"

#include <sqlite/connection_pool.hpp>
#include <sqlite/execute.hpp>

#include <future>
#include <thread>

using namespace testhelpers;

TEST(ConnectionPoolTest, BlocksUntilConnectionReturns) {
    auto factory = sqlite::connection_pool::make_factory(":memory:");
    sqlite::connection_pool pool(1, factory);

    auto first = pool.acquire();
    auto fut   = std::async(std::launch::async, [&]() {
        auto second = pool.acquire();
        sqlite::execute(*second, "SELECT 1;", true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    first = {};
    fut.wait();
}

TEST(ConnectionPoolTest, ReturnsConnectionAfterSharedAlias) {
    auto factory = sqlite::connection_pool::make_factory(":memory:");
    sqlite::connection_pool pool(1, factory);

    auto lease  = pool.acquire();
    auto shared = lease.shared();
    EXPECT_EQ(pool.idle_count(), 0u);

    lease = {};
    EXPECT_EQ(pool.idle_count(), 0u);

    sqlite::execute(*shared, "SELECT 1;", true);

    shared.reset();
    EXPECT_EQ(pool.idle_count(), 1u);
}
