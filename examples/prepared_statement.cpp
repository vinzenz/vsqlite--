/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/prepared_statement.hpp>
#include <iostream>

int main() {
    try {
        sqlite::connection con(":memory:");
        sqlite::execute(con, "CREATE TABLE events(id INTEGER PRIMARY KEY, message TEXT);", true);

        // prepare() returns a move-only statement; execute() binds the complete argument
        // set and runs it to completion.
        auto insert = con.prepare("INSERT INTO events(message) VALUES (?);");
        for (char const *message : {"started", "stopped", "restarted"}) {
            auto outcome = insert.execute(message);
            std::cout << "inserted rowid " << outcome.last_insert_rowid << " ("
                      << outcome.affected_rows << " row changed)\n";
        }

        // rows() binds arguments and returns a cursor; row access uses the same
        // row_view/row types as sqlite::query.
        auto select            = con.prepare("SELECT id, message FROM events WHERE id > ?;");
        std::int64_t last_seen = 0;
        for (auto row : select.rows(last_seen)) {
            std::cout << "event " << row.get<std::int64_t>(0) << ": " << row.get<std::string>(1)
                      << '\n';
        }

        // DML with RETURNING produces rows, so it goes through rows() as well.
        auto remove = con.prepare("DELETE FROM events WHERE id <= ? RETURNING id;");
        for (auto row : remove.rows(2)) {
            std::cout << "deleted event " << row.get<std::int64_t>(0) << '\n';
        }

        // Statement text without parameters runs through the zero-argument execute().
        auto count = con.prepare("SELECT COUNT(*) FROM events;");
        for (auto row : count.rows()) {
            std::cout << row.get<int>(0) << " event(s) remain\n";
        }
    } catch (std::exception const &e) {
        std::cout << "EXCEPTION: " << e.what() << std::endl;
    }
    return 0;
}
