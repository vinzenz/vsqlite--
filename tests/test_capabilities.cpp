#include "test_common.hpp"

#include <sqlite/capabilities.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/serialization.hpp>
#include <sqlite/session.hpp>
#include <sqlite/snapshot.hpp>

#include <string>
#include <vector>

using namespace testhelpers;

namespace {
bool contains(std::string const &message, std::string const &needle) {
    return message.find(needle) != std::string::npos;
}
} // namespace

// The build mirrors the optional-API detection of the library to the test target, so the
// assertions can rely on the same macros that select direct symbol references there. A
// bundled build (and any build whose configure-time probes succeeded) sees all macros
// defined; without them the capability values must still agree with the runtime
// resolution the wrappers use.
TEST(CapabilitiesTest, MirrorsBuildDetectionAndRuntimeSupport) {
    sqlite::connection con(":memory:");
    auto const caps = con.capabilities();

#if defined(VSQLITE_HAVE_SQLITE3_SESSION)
    EXPECT_TRUE(caps.sessions) << "The build verified the session APIs at link time.";
#else
    EXPECT_EQ(caps.sessions, sqlite::sessions_supported());
#endif
#if defined(VSQLITE_HAVE_SQLITE3_SNAPSHOT)
    EXPECT_TRUE(caps.snapshots) << "The build verified the snapshot APIs at link time.";
#else
    EXPECT_EQ(caps.snapshots, sqlite::snapshots_supported());
#endif
#if defined(VSQLITE_HAVE_SQLITE3_SERIALIZE)
    EXPECT_TRUE(caps.serialization) << "The build verified the serialization APIs at link time.";
#else
    EXPECT_EQ(caps.serialization, sqlite::serialization_supported());
#endif
}

// Distinguishes the two documented error kinds: an absent build capability is reported as
// "not available in this build" with the capability name, while a connection-state problem
// of a present capability surfaces as an operation error carrying the SQLite result code.
// The branch chosen below depends on the runtime capability so the test holds in
// direct-reference and fallback configurations alike.
TEST(CapabilitiesTest, AbsentSnapshotCapabilityDiffersFromConnectionStateError) {
    sqlite::connection con(":memory:");
    auto const caps = con.capabilities();

    try {
        (void)sqlite::snapshot::take(con);
        FAIL() << "snapshot::take must fail outside a WAL read transaction.";
    } catch (sqlite::database_exception_code const &ex) {
        // Capability present: rejecting the operation is a connection-state error with a
        // SQLite result code, not a build-capability report.
        SCOPED_TRACE("connection-state error branch");
        EXPECT_NE(ex.error_code(), 0);
        EXPECT_FALSE(contains(ex.what(), "not available in this build"));
        EXPECT_TRUE(caps.snapshots);
    } catch (sqlite::database_exception const &ex) {
        // Capability absent: the report names the capability and how to enable it, carries
        // no SQLite result code, and is not an operation error.
        SCOPED_TRACE("build capability absence branch");
        EXPECT_FALSE(caps.snapshots);
        EXPECT_TRUE(contains(ex.what(), "not available in this build"));
        EXPECT_TRUE(contains(ex.what(), "snapshots"));
        EXPECT_EQ(dynamic_cast<sqlite::database_exception_code const *>(&ex), nullptr);
    }
}

TEST(CapabilitiesTest, AbsentSerializationCapabilityDiffersFromOperationFailure) {
    sqlite::connection con(":memory:");
    sqlite::execute(con, "CREATE TABLE data(id INTEGER PRIMARY KEY);", true);
    auto const caps = con.capabilities();

    if (!caps.serialization) {
        try {
            (void)sqlite::serialize(con);
            FAIL() << "serialize must report the absent build capability.";
        } catch (sqlite::database_exception const &ex) {
            EXPECT_TRUE(contains(ex.what(), "not available in this build"));
            EXPECT_TRUE(contains(ex.what(), "serialization"));
        }
        return;
    }

    // An unknown schema is an operation failure, never a capability report.
    try {
        (void)sqlite::serialize(con, "missing");
        FAIL() << "serialize must fail for an unknown schema.";
    } catch (sqlite::database_exception const &ex) {
        EXPECT_FALSE(contains(ex.what(), "not available in this build"));
    }

    // A rejected deserialization reports the SQLite result code of the operation.
    std::vector<unsigned char> image = sqlite::serialize(con);
    ASSERT_FALSE(image.empty());
    try {
        sqlite::deserialize(con, image, "missing");
        FAIL() << "deserialize must fail for an unknown schema.";
    } catch (sqlite::database_exception_code const &ex) {
        EXPECT_NE(ex.error_code(), 0);
    }
}

TEST(CapabilitiesTest, AbsentSessionCapabilityDiffersFromOperationError) {
    sqlite::connection con(":memory:");
    auto const caps = con.capabilities();

    if (!caps.sessions) {
        try {
            sqlite::session tracked(con);
            FAIL() << "opening a session must report the absent build capability.";
        } catch (sqlite::database_exception const &ex) {
            EXPECT_TRUE(contains(ex.what(), "not available in this build"));
            EXPECT_TRUE(contains(ex.what(), "sessions"));
        }
        return;
    }

    // A malformed changeset is an operation error carrying a SQLite result code, not a
    // capability report.
    std::vector<unsigned char> const garbage{0xde, 0xad, 0xbe, 0xef};
    try {
        sqlite::apply_changeset(con, garbage);
        FAIL() << "applying a malformed changeset must fail.";
    } catch (sqlite::database_exception const &ex) {
        EXPECT_FALSE(contains(ex.what(), "not available in this build"));
        auto const *coded = dynamic_cast<sqlite::database_exception_code const *>(&ex);
        ASSERT_NE(coded, nullptr);
        EXPECT_NE(coded->error_code(), 0);
    }
}
