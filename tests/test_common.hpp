#pragma once

#include <gtest/gtest.h>

#include <sqlite/connection.hpp>
#include <sqlite/query.hpp>
#include <sqlite/result.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

inline std::string unique_memory_uri() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;
    oss << "file:memdb_" << counter++ << "?mode=memory&cache=shared";
    return oss.str();
}

} // namespace testhelpers
