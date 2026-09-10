#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/serialization.hpp>

using namespace testhelpers;

TEST(SerializationTest, RoundTripsInMemoryDatabase) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);
    sqlite::query q(dest, "SELECT COUNT(*) FROM data;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
}

TEST(SerializationTest, FailingDeserializeThrowsCatchablyAndRetainsNoMemory) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    // A nonexistent schema, and the "temp" schema, are rejected by sqlite3_deserialize. SQLite
    // takes ownership of the image buffer through SQLITE_DESERIALIZE_FREEONCLOSE and frees it
    // on failure as well, so the wrapper must neither free it again nor retain it.
    EXPECT_THROW(sqlite::deserialize(dest, image, "missing"), sqlite::database_exception);

    // Warm up lazily allocated SQLite infrastructure so the accounting below only reflects the
    // failed deserialization itself.
    auto const baseline = sqlite3_memory_used();
    EXPECT_THROW(sqlite::deserialize(dest, image, "missing"), sqlite::database_exception);
    EXPECT_THROW(sqlite::deserialize(dest, image, "temp"), sqlite::database_exception);
    EXPECT_EQ(sqlite3_memory_used(), baseline)
        << "Failed deserialization leaked or retained the image buffer.";

    // The target connection stays usable and the success path is unaffected.
    sqlite::deserialize(dest, image);
    EXPECT_EQ(testhelpers::count_rows(dest, "data"), 2);
}
