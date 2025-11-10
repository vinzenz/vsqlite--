/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
 Copyright (c) 2014 mickey mickey.mouse-1985@libero.it
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
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/transaction.hpp>
#include <sqlite/snapshot.hpp>
#include <string>

namespace sqlite {
inline namespace v2 {
    transaction::transaction(connection &con, transaction_type type) :
        m_con(con), m_isActive(false) {
        begin(type);
    }

    transaction::~transaction() {
        if (m_isActive) {
            rollback();
        }
    }

    void transaction::begin(transaction_type type) {
        std::string sql("BEGIN ");
        switch (type) {
        case transaction_type::deferred:
            sql += "DEFERRED ";
            break;
        case transaction_type::immediate:
            sql += "IMMEDIATE ";
            break;
        case transaction_type::exclusive:
            sql += "EXCLUSIVE ";
            break;
        case transaction_type::undefined:; /* noop */
        }
        sql += "TRANSACTION";
        exec(sql);
        m_isActive = true;
    }

    void transaction::end() {
        exec("END TRANSACTION");
        m_isActive = false;
    }

    void transaction::commit() {
        exec("COMMIT TRANSACTION");
        m_isActive = false;
    }

    void transaction::rollback() {
        exec("ROLLBACK TRANSACTION");
        m_isActive = false;
    }

    snapshot transaction::take_snapshot(std::string_view schema) {
        if (!m_isActive) {
            throw database_exception("Cannot capture a snapshot on an inactive transaction.");
        }
        return snapshot::take(m_con, schema);
    }

    void transaction::open_snapshot(snapshot const &snap, std::string_view schema) {
        if (!m_isActive) {
            throw database_exception("Cannot open a snapshot without an active transaction.");
        }
        snap.open(m_con, schema);
    }

    void transaction::exec(std::string const &cmd) {
        execute(m_con, cmd, true);
    }
} // namespace v2
} // namespace sqlite
