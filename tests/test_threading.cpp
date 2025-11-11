#include "test_common.hpp"

#include <sqlite/threading.hpp>

TEST(ThreadingTest, ConfigureSerializedMode) {
    EXPECT_TRUE(sqlite::configure_threading(sqlite::threading_mode::serialized));
    EXPECT_EQ(sqlite::current_threading_mode(), sqlite::threading_mode::serialized);
}

