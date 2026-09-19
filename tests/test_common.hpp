#pragma once

#include <gtest/gtest.h>

#include <sqlite/connection.hpp>
#include <sqlite/query.hpp>
#include <sqlite/result.hpp>
#include <sqlite/serialization.hpp>
#include <sqlite/session.hpp>
#include <sqlite/snapshot.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

// Optional-API guards that cross-check the configure-time detection in
// CMakeLists.txt against the runtime capability probes.
//
// When the build verified that the selected SQLite provides an API group, the
// test executable is compiled with VSQLITE_EXPECT_SQLITE3_<API> and a runtime
// "unavailable" verdict must fail the run: silently skipping the test would
// hide a broken capability probe (GH issue #75). Builds that could not verify
// the API keep skipping, so distributions without it stay supported.
#if defined(VSQLITE_EXPECT_SQLITE3_SESSION)
#define VSQLITE_REQUIRE_SESSIONS_SUPPORTED()                                                       \
    do {                                                                                           \
        if (!sqlite::sessions_supported()) {                                                       \
            GTEST_FAIL() << "Configure-time detection verified the SQLite session API, "           \
                            "but the runtime probe reports it unavailable.";                       \
        }                                                                                          \
    } while (false)
#else
#define VSQLITE_REQUIRE_SESSIONS_SUPPORTED()                                                       \
    do {                                                                                           \
        if (!sqlite::sessions_supported()) {                                                       \
            GTEST_SKIP() << "SQLite session API not available in this build.";                     \
        }                                                                                          \
    } while (false)
#endif

#if defined(VSQLITE_EXPECT_SQLITE3_SNAPSHOT)
#define VSQLITE_REQUIRE_SNAPSHOTS_SUPPORTED()                                                      \
    do {                                                                                           \
        if (!sqlite::snapshots_supported()) {                                                      \
            GTEST_FAIL() << "Configure-time detection verified the SQLite snapshot "               \
                            "APIs, but the runtime probe reports them unavailable.";               \
        }                                                                                          \
    } while (false)
#else
#define VSQLITE_REQUIRE_SNAPSHOTS_SUPPORTED()                                                      \
    do {                                                                                           \
        if (!sqlite::snapshots_supported()) {                                                      \
            GTEST_SKIP() << "SQLite snapshot APIs not available in this build.";                   \
        }                                                                                          \
    } while (false)
#endif

#if defined(VSQLITE_EXPECT_SQLITE3_SERIALIZE)
#define VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED()                                                  \
    do {                                                                                           \
        if (!sqlite::serialization_supported()) {                                                  \
            GTEST_FAIL() << "Configure-time detection verified the SQLite "                        \
                            "serialization APIs, but the runtime probe reports them "              \
                            "unavailable.";                                                        \
        }                                                                                          \
    } while (false)
#else
#define VSQLITE_REQUIRE_SERIALIZATION_SUPPORTED()                                                  \
    do {                                                                                           \
        if (!sqlite::serialization_supported()) {                                                  \
            GTEST_SKIP() << "SQLite serialization APIs not available in this build.";              \
        }                                                                                          \
    } while (false)
#endif

namespace testhelpers {

inline std::filesystem::path test_root() {
    static const std::filesystem::path root = [] {
        auto base = std::filesystem::temp_directory_path() / "vsqlitepp_tests";
        std::filesystem::create_directories(base);
        return base;
    }();
    return root;
}

inline std::filesystem::path unique_db_path(std::string_view hint) {
    static std::atomic<uint64_t> counter{0};
    auto root = test_root();
    auto now  = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << hint << "_" << now << "_" << counter++ << ".db";
    auto path = root / oss.str();
    std::filesystem::create_directories(path.parent_path());
    return path;
}

struct TempFile {
    explicit TempFile(std::string_view hint) : path(unique_db_path(hint)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    std::filesystem::path path;
    std::string string() const {
        return path.string();
    }
};

inline int count_rows(sqlite::connection &con, std::string const &table_expression) {
    sqlite::query q(con, "SELECT COUNT(*) FROM " + table_expression + ";");
    auto res = q.get_result();
    EXPECT_TRUE(res->next_row());
    return res->get<int>(0);
}

inline std::vector<unsigned char> load_blob(sqlite::result &res, int idx) {
    std::vector<unsigned char> data;
    res.get_binary(idx, data);
    return data;
}

inline std::string quote_identifier(std::string_view identifier) {
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('"');
    for (char c : identifier) {
        if (c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

inline bool diagnostics_enabled() {
    if (std::getenv("VSQLITE_TEST_DIAGNOSTICS")) {
        return true;
    }
    if (std::getenv("CI")) {
        return true;
    }
    return false;
}

inline void dump_sqlite_diagnostics(sqlite::connection &con, std::string_view label) {
    if (!diagnostics_enabled()) {
        return;
    }
    std::cerr << "\n[sqlite diagnostics] " << label << '\n';
    std::cerr << "  sqlite3_libversion: " << sqlite3_libversion() << '\n';
    std::cerr << "  sqlite3_sourceid: " << sqlite3_sourceid() << '\n';

    try {
        sqlite::query version_q(con, "SELECT sqlite_version();");
        auto version_res = version_q.get_result();
        if (version_res->next_row()) {
            std::cerr << "  sqlite_version(): " << version_res->get<std::string>(0) << '\n';
        } else {
            std::cerr << "  sqlite_version(): <no rows>\n";
        }
    } catch (std::exception const &ex) {
        std::cerr << "  sqlite_version(): <error> " << ex.what() << '\n';
    }

    try {
        sqlite::query db_list(con, "PRAGMA database_list;");
        auto db_res = db_list.get_result();
        std::cerr << "  database_list:\n";
        while (db_res->next_row()) {
            std::cerr << "    " << db_res->get<int>(0) << " | " << db_res->get<std::string>(1)
                      << " | " << db_res->get<std::string>(2) << '\n';
        }
    } catch (std::exception const &ex) {
        std::cerr << "  database_list: <error> " << ex.what() << '\n';
    }

    try {
        sqlite::query opts(con, "PRAGMA compile_options;");
        auto opt_res = opts.get_result();
        std::cerr << "  compile_options:\n";
        while (opt_res->next_row()) {
            std::cerr << "    " << opt_res->get<std::string>(0) << '\n';
        }
    } catch (std::exception const &ex) {
        std::cerr << "  compile_options: <error> " << ex.what() << '\n';
    }
}

inline void dump_table_info(sqlite::connection &con, std::string_view table) {
    if (!diagnostics_enabled()) {
        return;
    }
    std::cerr << "  table_info(" << table << "):\n";
    try {
        sqlite::query schema_q(
            con, "SELECT name, sql FROM sqlite_master WHERE type='table' AND name=?;");
        schema_q % std::string(table);
        auto schema_res = schema_q.get_result();
        if (schema_res->next_row()) {
            std::cerr << "    schema: " << schema_res->get<std::string>(1) << '\n';
        } else {
            std::cerr << "    schema: <not found>\n";
        }
    } catch (std::exception const &ex) {
        std::cerr << "    schema: <error> " << ex.what() << '\n';
    }

    try {
        sqlite::query count_q(con,
                              "SELECT COUNT(*) FROM " + quote_identifier(std::string(table)) + ";");
        auto count_res = count_q.get_result();
        if (count_res->next_row()) {
            std::cerr << "    row_count: " << count_res->get<int>(0) << '\n';
        } else {
            std::cerr << "    row_count: <no rows>\n";
        }
    } catch (std::exception const &ex) {
        std::cerr << "    row_count: <error> " << ex.what() << '\n';
    }
}

inline std::string unique_memory_uri(bool absolute = false) {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;
    oss << "file:";
    if (absolute) {
        oss << (test_root() / ("memdb_" + std::to_string(counter++))).string();
    } else {
        oss << "memdb_" << counter++;
    }
    oss << "?mode=memory&cache=shared";
    return oss.str();
}

} // namespace testhelpers
