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
#ifndef GUARD_SQLITE_TRANSACTION_HPP_INCLUDED
#define GUARD_SQLITE_TRANSACTION_HPP_INCLUDED

#include <string>
#include <string_view>

/**
 * @file sqlite/transaction.hpp
 * @brief RAII helper for BEGIN/COMMIT/ROLLBACK sequences and WAL snapshots.
 *
 * The `sqlite::transaction` type guarantees that transactions are closed on scope exit and
 * provides helpers for selecting the transaction kind or wiring in snapshot semantics.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;
    struct snapshot;

    /** \brief Defines the kind of transaction to begin.
     *
     * \see http://www.sqlite.org/lang_transaction.html for documentation
     * about transaction kind.
     *
     * When no transaction type is defined (\c undefined) the default behaviour
     * is to create a deferred transaction, but the \c undefined constant is
     * because maybe in future the default behaviour may change.
     *
     * Note that for more type safety this needs Strongly Typed enums C++11
     * feature:
     * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2007/n2347.pdf
     */
    enum class transaction_type { undefined, deferred, immediate, exclusive };

    /** \brief transaction is a helper class to start transactions within SQLite
     *
     */
    struct transaction {
    public:
        /** \brief constructor
         * \param con a reference to the connection object where the
         * transaction should be started/ended/committed or rolled back
         * \param type define the transaction type
         */
        transaction(connection &con, transaction_type type = transaction_type::undefined);

        /** \brief destructor
         *
         */
        ~transaction();

        /** \brief Starts a transaction
         * \param type define the transaction type
         */
        void begin(transaction_type type = transaction_type::undefined);

        /** \brief Ends an transaction
         *
         */
        void end();

        /** \brief Commits a transaction
         *
         */
        void commit();

        /** \brief Rolls back a transaction
         *
         */
        void rollback();

        /** \brief Allow to check if transaction handled by this object is
         * currently active
         * \return \c true if transaction is still active, \c false otherwise
         */
        bool isActive() const { return m_isActive; }

        bool isEnding() const { return m_isEnding; }

        /** \brief Capture a consistent snapshot for the current transaction.
         * \param schema Database schema name (defaults to "main").
         */
        snapshot take_snapshot(std::string_view schema = "main");

        /** \brief Re-open this transaction so it reads from the supplied snapshot. */
        void open_snapshot(snapshot const &snap, std::string_view schema = "main");

    private:
        void exec(std::string const &);
        connection &m_con;
        bool m_isActive{false}; ///< if \c true there is a transaction currently opened
        bool m_isEnding{false}; ///< true while COMMIT/ROLLBACK/END is running
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_TRANSACTION_HPP_INCLUDED
