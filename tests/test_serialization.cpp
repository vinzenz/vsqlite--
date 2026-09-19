#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/serialization.hpp>

using namespace testhelpers;

TEST(SerializationTest, RoundTripsInMemoryDatabase) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
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

TEST(SerializationTest, NoCopyKeepsDeserializedDatabaseUsable) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    // SQLITE_SERIALIZE_NOCOPY returns the connection's own storage; the wrapper must
    // copy the bytes without freeing the buffer it does not own.
    auto nocopy = sqlite::serialize(dest, "main", SQLITE_SERIALIZE_NOCOPY);
    ASSERT_FALSE(nocopy.empty());

    // The connection stays usable afterwards and closes cleanly on destruction.
    sqlite::query q(dest, "SELECT COUNT(*) FROM data;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
}

TEST(SerializationTest, NoCopyMatchesOwningCopyImage) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    auto copied = sqlite::serialize(dest);
    auto nocopy = sqlite::serialize(dest, "main", SQLITE_SERIALIZE_NOCOPY);
    ASSERT_FALSE(copied.empty());
    EXPECT_EQ(nocopy, copied);
}

TEST(SerializationTest, NoCopyWithoutContiguousImageThrows) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
    TempFile file("serialization_nocopy_file");
    sqlite::connection con(file.string());
    sqlite::execute(con, "CREATE TABLE data(id INTEGER PRIMARY KEY);", true);

    // A file-backed database has no contiguous in-memory image, so
    // SQLITE_SERIALIZE_NOCOPY yields NULL from sqlite3_serialize.
    EXPECT_THROW(sqlite::serialize(con, "main", SQLITE_SERIALIZE_NOCOPY),
                 sqlite::database_exception);

    // The connection remains usable, and the owning-copy path still works.
    EXPECT_EQ(count_rows(con, "data"), 0);
    auto image = sqlite::serialize(con);
    EXPECT_FALSE(image.empty());
}

TEST(SerializationTest, FailingDeserializeThrowsCatchablyAndRetainsNoMemory) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
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

TEST(SerializationTest, WritableDeserializedDatabaseGrowsBeyondImage) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE t(x);", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    // The zeroblob forces the database past the page count of the source image.
    // Without SQLITE_DESERIALIZE_RESIZEABLE this used to fail with SQLITE_FULL.
    sqlite::execute(dest, "INSERT INTO t VALUES (zeroblob(65536));", true);

    sqlite::query q(dest, "SELECT length(x) FROM t;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 65536);

    auto grown = sqlite::serialize(dest);
    EXPECT_GT(grown.size(), image.size());

    // The grown database still round-trips into a fresh connection.
    sqlite::connection reloaded(":memory:");
    sqlite::deserialize(reloaded, grown);
    EXPECT_EQ(count_rows(reloaded, "t"), 1);
}

TEST(SerializationTest, ReadOnlyDeserializedDatabaseRejectsWrites) {
    VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED();
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image, "main", true);

    EXPECT_THROW(sqlite::execute(dest, "INSERT INTO data(value) VALUES ('three');", true),
                 sqlite::database_exception);
    EXPECT_EQ(count_rows(dest, "data"), 2);
}
