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
#include <sqlite/backup.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/query.hpp>
#include <iostream>

int main() {
    try {
        sqlite::connection con("test.db");

        sqlite::connection con_memory(":memory:");

        sqlite::execute(con, "Create Table IF NOT EXISTS test(id INTEGER PRIMARY KEY, name TEXT);",
                        true);

        sqlite::execute ins(con, "INSERT INTO TEST VALUES(?,?);");

        ins % sqlite::nil % "Hallo";

        ins();

        ins.clear();

        ins % sqlite::nil % "Test";

        ins.emit();

        sqlite::query q(con, "SELECT * from test;");

        sqlite::result_type res = q.get_result();
        while (res->next_row()) {
            std::cout << res->get<int>(0) << "|" << res->get<std::string>(1) << std::endl;
        }

        res->reset();
        while (res->next_row()) {
            std::cout << res->get<int>(0) << "|" << res->get<std::string>(1) << std::endl;
        }

        sqlite::backup backup_op(con_memory, con);
        backup_op.step();
        backup_op.finish();

        sqlite::query q_memory(con_memory, "SELECT * FROM test;");

        sqlite::result_type res2 = q_memory.get_result();
        while (res2->next_row()) {
            std::cout << res2->get<int>(0) << "|" << res2->get<std::string>(1) << std::endl;
        }

        sqlite::execute(con, "DROP TABLE test;", true);

        sqlite::execute(con, "VACUUM;", true);
    } catch (std::exception const &e) {
        std::cout << "EXCEPTION: " << e.what() << std::endl;
    }
    return 0;
}
