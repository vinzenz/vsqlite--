#include "test_common.hpp"

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
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

TEST(SerializationTest, NoCopyKeepsDeserializedDatabaseUsable) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    // Reading the connection image (SQLITE_SERIALIZE_NOCOPY) returns the connection's
    // own storage; the wrapper must copy the bytes without freeing the buffer it does
    // not own.
    auto nocopy =
        sqlite::serialize(dest, "main", sqlite::serialize_options{.use_connection_image = true});
    ASSERT_FALSE(nocopy.empty());

    // The connection stays usable afterwards and closes cleanly on destruction.
    sqlite::query q(dest, "SELECT COUNT(*) FROM data;");
    auto res = q.get_result();
    ASSERT_TRUE(res->next_row());
    EXPECT_EQ(res->get<int>(0), 2);
}

TEST(SerializationTest, NoCopyMatchesOwningCopyImage) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    auto copied = sqlite::serialize(dest);
    auto nocopy =
        sqlite::serialize(dest, "main", sqlite::serialize_options{.use_connection_image = true});
    ASSERT_FALSE(copied.empty());
    EXPECT_EQ(nocopy, copied);
}

TEST(SerializationTest, NoCopyWithoutContiguousImageThrows) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    TempFile file("serialization_nocopy_file");
    sqlite::connection con(file.string());
    sqlite::execute(con, "CREATE TABLE data(id INTEGER PRIMARY KEY);", true);

    // A file-backed database has no contiguous in-memory image, so reading the
    // connection image yields NULL from sqlite3_serialize.
    EXPECT_THROW(
        sqlite::serialize(con, "main", sqlite::serialize_options{.use_connection_image = true}),
        sqlite::database_exception);

    // The connection remains usable, and the owning-copy path still works.
    EXPECT_EQ(count_rows(con, "data"), 0);
    auto image = sqlite::serialize(con);
    EXPECT_FALSE(image.empty());
}

TEST(SerializationTest, DeprecatedFlagOverloadMapsToTypedOptions) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    // Reading the connection image only works for a deserialized (memdb) database.
    sqlite::connection dest(":memory:");
    sqlite::deserialize(dest, image);

    // The deprecated raw-flag overload is an adapter: SQLITE_SERIALIZE_NOCOPY maps to
    // serialize_options::use_connection_image, everything else to a plain owning copy.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    auto via_nocopy_flag = sqlite::serialize(dest, "main", SQLITE_SERIALIZE_NOCOPY);
    auto via_plain_flags = sqlite::serialize(src, "main", 0u);
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

    EXPECT_EQ(
        via_nocopy_flag,
        sqlite::serialize(dest, "main", sqlite::serialize_options{.use_connection_image = true}));
    EXPECT_EQ(via_plain_flags, sqlite::serialize(src, "main", sqlite::serialize_options{}));
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

TEST(SerializationTest, WritableDeserializedDatabaseGrowsBeyondImage) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
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
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
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
