/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of virtuosic bytes nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

##############################################################################*/
// Micro-benchmark: repeated INSERT + SELECT workload comparing the
// prepared_statement facade against the legacy command/query/execute objects.
// Not part of ctest; build the target explicitly and run the binary.

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/prepared_statement.hpp>
#include <sqlite/query.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

using clock_type = std::chrono::steady_clock;

constexpr int kIterations = 5000;

void create_schema(sqlite::connection &con) {
    sqlite::execute(con, "CREATE TABLE bench(id INTEGER PRIMARY KEY, message TEXT, value INTEGER);",
                    true);
    // The lookup stays O(log n) as the table grows, so the measurement shows the
    // wrapper overhead instead of an ever slower table scan.
    sqlite::execute(con, "CREATE INDEX bench_value ON bench(value);", true);
}

// Legacy style: construct the execute/query objects per operation. This is how
// short-lived call sites typically use the wrapper.
bool legacy_objects_per_operation(sqlite::connection &con, int i) {
    sqlite::execute insert(con, "INSERT INTO bench(message, value) VALUES (?, ?);");
    insert % std::string("message") % i;
    insert.step_once();

    sqlite::query select(con, "SELECT value FROM bench WHERE value = ?;");
    select % i;
    auto res = select.get_result();
    return res->next_row() && res->get<int>(0) == i;
}

// Legacy style: keep one execute and one query object alive and reuse them.
struct legacy_reuse_state {
    sqlite::execute insert;
    sqlite::query select;
};

bool legacy_reused_objects(legacy_reuse_state &state, sqlite::connection &con, int i) {
    (void)con;
    state.insert % std::string("message") % i;
    state.insert.step_once();
    state.insert.clear();

    state.select.clear(); // rewinds and restarts the '%' argument index
    state.select % i;
    auto res     = state.select.get_result();
    bool matched = res->next_row() && res->get<int>(0) == i;
    res->reset();
    return matched;
}

// New facade: one prepared_statement per SQL text, execute()/rows() with arguments.
struct facade_state {
    sqlite::prepared_statement insert;
    sqlite::prepared_statement select;
};

bool prepared_statement_facade(facade_state &state, sqlite::connection &con, int i) {
    (void)con;
    state.insert.execute(std::string("message"), i);
    for (auto row : state.select.rows(i)) {
        return row.get<int>(0) == i;
    }
    return false;
}

// Creates the arm's state from @p make_state, warms it up, then times @p step for
// kIterations rounds. The checksum proves all arms ran the same workload.
template <typename State, typename MakeState, typename Step>
void measure(char const *label, MakeState &&make_state, Step &&step) {
    sqlite::connection con(":memory:");
    create_schema(con);
    auto state = make_state(con);
    step(*state, con, 100); // warm up; also lets per-arm state settle
    sqlite::execute(con, "DELETE FROM bench;", true);

    std::uint64_t checksum = 0;
    auto started           = clock_type::now();
    for (int i = 0; i < kIterations; ++i) {
        if (step(*state, con, i)) {
            checksum += static_cast<std::uint64_t>(i);
        }
    }
    auto finished = clock_type::now();

    double milliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    std::cout << label << ": " << milliseconds << " ms ("
              << (milliseconds / static_cast<double>(kIterations)) << " ms/iter"
              << ", checksum " << checksum << ")\n";
}

} // namespace

int main() {
    try {
        std::cout << "prepared statement benchmark (" << kIterations
                  << " INSERT+SELECT rounds, in-memory database, Debug build)\n";
        measure<int>(
            "legacy objects per operation",
            [](sqlite::connection &) { return std::make_unique<int>(0); },
            [](int &, sqlite::connection &con, int i) {
                return legacy_objects_per_operation(con, i);
            });
        measure<legacy_reuse_state>(
            "legacy reused execute/query ",
            [](sqlite::connection &con) {
                // The state members are neither copyable nor movable, so the aggregate is
                // constructed in place.
                return std::unique_ptr<legacy_reuse_state>(new legacy_reuse_state{
                    sqlite::execute(con, "INSERT INTO bench(message, value) VALUES (?, ?);"),
                    sqlite::query(con, "SELECT value FROM bench WHERE value = ?;")});
            },
            legacy_reused_objects);
        measure<facade_state>(
            "prepared_statement facade   ",
            [](sqlite::connection &con) {
                return std::unique_ptr<facade_state>(new facade_state{
                    con.prepare("INSERT INTO bench(message, value) VALUES (?, ?);"),
                    con.prepare("SELECT value FROM bench WHERE value = ?;")});
            },
            prepared_statement_facade);
    } catch (std::exception const &ex) {
        std::cerr << "benchmark failed: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
