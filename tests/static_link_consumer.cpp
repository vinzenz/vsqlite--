/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

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
// Verifies that the optional SQLite APIs stay usable when VSQLite++ and SQLite are
// linked statically into an executable that does not export its own symbols.
// See https://github.com/vinzenz/vsqlite--/issues/59.

#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/query.hpp>
#include <sqlite/serialization.hpp>
#include <sqlite/session.hpp>
#include <sqlite/snapshot.hpp>

#include <iostream>

namespace {
int count_rows(sqlite::connection &con, char const *sql) {
    sqlite::query q(con, sql);
    auto result = q.get_result();
    if (!result->next_row()) {
        std::cerr << "Query returned no rows: " << sql << '\n';
        return -1;
    }
    return result->get<int>(0);
}

// Returns 0 when a call whose API is reported unavailable fails with the
// documented exception, and 1 when it silently succeeds.
template <typename Fn> int expect_unavailable(char const *api, Fn &&call) {
    try {
        call();
    } catch (sqlite::database_exception const &) {
        return 0;
    }
    std::cerr << "API reported unavailable but usage did not throw: " << api << '\n';
    return 1;
}
} // namespace

int main() {
    sqlite::connection db(":memory:");
    sqlite::connection_capabilities const caps = db.capabilities();
    bool const sessions                        = caps.sessions;
    bool const snapshots                       = caps.snapshots;
    bool const serialization                   = caps.serialization;
    std::cout << "sessions=" << sessions << " snapshots=" << snapshots
              << " serialization=" << serialization << '\n';

#if defined(VSQLITE_EXPECT_ALL_OPTIONAL_APIS)
    if (!sessions || !snapshots || !serialization) {
        std::cerr << "Optional SQLite APIs are missing in a plain static link "
                     "(no -rdynamic or equivalent was used).\n";
        return 1;
    }
#else
    // Builds against external SQLite implementations may legitimately lack the
    // optional APIs. Reported availability only has to match actual behavior.
    if (!serialization && expect_unavailable("serialization", [&] { sqlite::serialize(db); })) {
        return 1;
    }
    if (!sessions && expect_unavailable("sessions", [&] { sqlite::session s(db); })) {
        return 1;
    }
#endif

    // Exercise the session round trip when the API is available.
    if (sessions) {
        sqlite::execute(db, "CREATE TABLE inventory(id INTEGER PRIMARY KEY, qty INTEGER);", true);
        {
            sqlite::session session(db);
            session.attach("inventory");
            sqlite::execute(db, "INSERT INTO inventory(qty) VALUES (5), (7);", true);
            auto changeset = session.changeset();

            sqlite::connection consumer(":memory:");
            sqlite::execute(consumer,
                            "CREATE TABLE inventory(id INTEGER PRIMARY KEY, qty INTEGER);", true);
            sqlite::apply_changeset(consumer, changeset);
            if (count_rows(consumer, "SELECT COUNT(*) FROM inventory;") != 2) {
                std::cerr << "Applied changeset did not reproduce the captured rows.\n";
                return 1;
            }
        }
    }

    // Exercise a serialization round trip when the API is available.
    if (serialization) {
        sqlite::execute(db, "CREATE TABLE payload(id INTEGER PRIMARY KEY, v TEXT);", true);
        sqlite::execute(db, "INSERT INTO payload(v) VALUES ('a'), ('b');", true);
        auto image = sqlite::serialize(db);
        sqlite::connection restored(":memory:");
        sqlite::deserialize(restored, image);
        if (count_rows(restored, "SELECT COUNT(*) FROM payload;") != 2) {
            std::cerr << "Deserialized database did not contain the expected rows.\n";
            return 1;
        }
    }

    std::cout << "ok\n";
    return 0;
}
