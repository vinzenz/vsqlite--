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
    auto nocopy = sqlite::serialize(dest, "main", SQLITE_SERIALIZE_NOCOPY);
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

    // A file-backed database has no contiguous in-memory image, so
    // SQLITE_SERIALIZE_NOCOPY yields NULL from sqlite3_serialize.
    EXPECT_THROW(sqlite::serialize(con, "main", SQLITE_SERIALIZE_NOCOPY),
                 sqlite::database_exception);

    // The connection remains usable, and the owning-copy path still works.
    EXPECT_EQ(count_rows(con, "data"), 0);
    auto image = sqlite::serialize(con);
    EXPECT_FALSE(image.empty());
}

TEST(SerializationTest, DeserializeIntoUnknownSchemaThrows) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    // SQLite owns the wrapper's image buffer once SQLITE_DESERIALIZE_FREEONCLOSE is
    // handed over; the failure below frees it inside sqlite3_deserialize, so the
    // wrapper must not release it a second time.
    EXPECT_THROW(sqlite::deserialize(dest, image, "missing"), sqlite::database_exception);

    // The connection remains usable after the rejected deserialization.
    sqlite::execute(dest, "CREATE TABLE other(id INTEGER);", true);
    EXPECT_EQ(count_rows(dest, "other"), 0);
}

TEST(SerializationTest, DeserializeIntoTempSchemaThrows) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    // sqlite3_deserialize refuses the temp schema outright (it only reopens
    // non-temp schemas as an in-memory database).
    EXPECT_THROW(sqlite::deserialize(dest, image, "temp"), sqlite::database_exception);

    // The connection remains usable after the rejected deserialization.
    sqlite::execute(dest, "CREATE TABLE other(id INTEGER);", true);
    EXPECT_EQ(count_rows(dest, "other"), 0);
}

TEST(SerializationTest, DeserializeWhileTargetInUseIsSafe) {
    if (!sqlite::serialization_supported()) {
        GTEST_SKIP() << "SQLite serialization APIs not available in this build.";
    }
    sqlite::connection src(":memory:");
    sqlite::execute(src, "CREATE TABLE data(id INTEGER PRIMARY KEY, value TEXT);", true);
    sqlite::execute(src, "INSERT INTO data(value) VALUES ('one'), ('two');", true);
    auto image = sqlite::serialize(src);

    sqlite::connection dest(":memory:");
    sqlite::execute(dest, "CREATE TABLE existing(id INTEGER);", true);
    bool replaced = false;
    {
        // Keep a read transaction open on the target schema while deserializing.
        // Builds that reject this report a database error instead; either way the
        // image buffer must be released exactly once (no leak, no double free).
        sqlite::query q(dest, "SELECT COUNT(*) FROM existing;");
        auto res = q.get_result();
        ASSERT_TRUE(res->next_row());
        try {
            sqlite::deserialize(dest, image);
            replaced = true;
        } catch (sqlite::database_exception const &) {
            // Rejected by this SQLite build while the schema was in use.
        }
    }
    // The target stays usable whichever way the deserialization went.
    if (replaced) {
        EXPECT_EQ(count_rows(dest, "data"), 2);
    } else {
        EXPECT_EQ(count_rows(dest, "existing"), 0);
    }
}
