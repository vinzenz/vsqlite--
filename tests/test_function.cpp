#include "test_common.hpp"

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/function.hpp>

using namespace testhelpers;

TEST(FunctionTest, RegistersScalarFunction) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "double_int", [](int value) { return value * 2; },
                            {.deterministic = true});

    sqlite::query q(conn, "SELECT double_int(21);");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 42);
}

TEST(FunctionTest, OptionalAndBlobArguments) {
    sqlite::connection conn(":memory:");
    sqlite::create_function(conn, "blob_size",
                            [](std::optional<std::vector<unsigned char>> blob) -> int {
                                return blob ? static_cast<int>(blob->size()) : -1;
                            });
    sqlite::command insert(conn, "SELECT blob_size(?);");
    std::vector<unsigned char> data{1, 2, 3};
    insert % data;
    EXPECT_TRUE(insert.step_once());
}
