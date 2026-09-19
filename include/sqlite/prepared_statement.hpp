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
#ifndef GUARD_SQLITE_PREPARED_STATEMENT_HPP_INCLUDED
#define GUARD_SQLITE_PREPARED_STATEMENT_HPP_INCLUDED

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sqlite/command.hpp>
#include <sqlite/detail/type_helpers.hpp>
#include <sqlite/query.hpp>

struct sqlite3_stmt;

/**
 * @file sqlite/prepared_statement.hpp
 * @brief One prepared-statement type with an explicit execution state.
 *
 * `sqlite::prepared_statement` is created by `connection::prepare()`. `execute()` binds a
 * complete argument set and runs the statement to completion, while `rows()` binds arguments
 * and returns a move-only `sqlite::cursor` over the result rows. The statement tracks whether
 * it is prepared, executing, complete, or failed, so competing ways to start an execution are
 * rejected instead of silently resetting one another. See docs/prepared-statement.md for the
 * full behavior specification.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    /** \brief Execution state of a \ref prepared_statement object.
     *
     * The state is observed per statement object:
     * - `prepared`: the statement was created or reset and can start an execution.
     * - `executing`: a \ref cursor returned by rows() is live and owns the stepping.
     * - `complete`: the last execution finished; the object is immediately reusable.
     * - `failed`: the last execution raised a database error; reset() is required first.
     */
    enum class execution_state {
        prepared,  ///< Created or reset; no execution started.
        executing, ///< A cursor is live and only it may step.
        complete,  ///< Last execution finished normally; reusable.
        failed     ///< Last execution raised an error; call reset().
    };

    /** \brief Result of a \ref prepared_statement::execute call.
     *
     * The affected-row count is captured while the statement's own completion is still the
     * connection's most recent one, so statements executed afterwards cannot overwrite it.
     */
    struct execution_outcome {
        int affected_rows = 0; ///< Rows changed by INSERT, UPDATE, or DELETE (sqlite3_changes).
        std::int64_t last_insert_rowid =
            0; ///< Rowid of the last INSERT (sqlite3_last_insert_rowid).
    };

    namespace detail {
        template <typename T> struct is_named_parameter : std::false_type {};

        template <typename T> struct is_named_parameter<named_parameter<T>> : std::true_type {};

        template <typename T>
        inline constexpr bool is_named_parameter_v = is_named_parameter<T>::value;

        /** \brief Shared execution state of one prepared_statement and its cursors.
         *
         * The block lets a cursor mark its execution complete or failed when the
         * prepared_statement object itself is not in scope, e.g. while a range-based for
         * loop is still consuming the cursor.
         */
        struct execution_state_private {
            execution_state state = execution_state::prepared;
            sqlite3_stmt *stmt    = nullptr;

            /// Rewinds the statement (errors deliberately ignored) and completes the execution.
            void finish();
            /// Clears the failed evaluation and records the failure.
            void fail();
        };
    } // namespace detail

    /** \brief Move-only cursor over the rows of one \ref prepared_statement::rows call.
     *
     * The cursor owns the active execution for its whole lifetime: until it is destroyed,
     * the statement stays in the \ref execution_state::executing state and every other
     * execution attempt on the same statement is rejected. Reaching the end of the range
     * does not end that ownership; consume cursors in their own scope so the statement
     * becomes reusable when the scope ends. A stepping error marks the execution
     * \ref execution_state::failed immediately. Iteration yields
     * \ref query::result_range::row_view objects that borrow from the cursor and expire on
     * advancement, reset, or destruction; use \ref query::result_range::row (or the postfix
     * increment proxy) for an owning snapshot.
     */
    class cursor {
    public:
        using row_view      = query::result_range::row_view;
        using row           = query::result_range::row;
        using postfix_proxy = query::result_range::iterator::postfix_proxy;

        cursor() = default;
        cursor(cursor &&other) noexcept;
        cursor &operator=(cursor &&other) noexcept;
        cursor(cursor const &)            = delete;
        cursor &operator=(cursor const &) = delete;
        ~cursor();

        /** \brief Forward-only input iterator over the rows of this cursor.
         *
         * Dereferencing yields a \ref row_view that references the current row; as with
         * every input iterator the referenced row is only valid until the iterator is
         * incremented. The postfix increment operator returns an owning \ref postfix_proxy
         * of the row that preceded the increment, so `*it++` observes the old row.
         */
        class iterator {
        public:
            using iterator_category = std::input_iterator_tag;
            using value_type        = row_view;
            using difference_type   = std::ptrdiff_t;
            using pointer           = row_view *;
            using reference         = row_view &;

            iterator() = default;

            reference operator*() const;
            pointer operator->() const;
            iterator &operator++();
            postfix_proxy operator++(int);
            bool operator==(iterator const &other) const;
            bool operator!=(iterator const &other) const;

        private:
            friend class cursor;
            iterator(query::result_range::iterator inner,
                     std::shared_ptr<detail::execution_state_private> execution);

            void step();

            query::result_range::iterator inner_;
            std::shared_ptr<detail::execution_state_private> execution_;
        };

        /// Returns an iterator to the first row; stepping errors mark the execution failed.
        iterator begin();

        /// Returns the end sentinel.
        iterator end() const;

    private:
        friend struct prepared_statement;

        cursor(query::result_range range,
               std::shared_ptr<detail::execution_state_private> execution);

        query::result_range range_;
        std::shared_ptr<detail::execution_state_private> execution_;
        bool begun_ = false;
    };

    /** \brief A prepared statement with an explicit execution state.
     *
     * Create instances through \ref connection::prepare. The class is move-only; the moved-from
     * object must not be used. The statement object itself borrows the \ref connection, like
     * \ref command and \ref query. A \ref cursor returned by rows() additionally retains the
     * connection's shared state, so it may outlive both this statement and the connection
     * facade: destroying the facade defers the native cleanup until the last cursor is
     * destroyed. Rows and row views still borrow from their cursor.
     */
    struct prepared_statement {
        /** \brief Runs the statement with the manually bound values (advanced mode).
         *
         * Every parameter of the statement must have been bound through the \ref bind
         * overloads since the last reset with cleared bindings, otherwise a
         * \ref database_exception is thrown and nothing is executed. This overload never
         * reuses values that an earlier execute(args...) call bound.
         */
        execution_outcome execute();

        /** \brief Binds the complete argument set and runs the statement to completion.
         *
         * Arguments use the same conversion layer as \ref command::operator%, including
         * \ref named parameters. Bindings from earlier invocations are cleared first, so a
         * call never silently reuses earlier values; if the argument set does not cover
         * every parameter, a \ref database_exception is thrown before anything is executed
         * and the statement stays reusable.
         *
         * Statements that return rows (SELECT, or DML with RETURNING) are rejected with a
         * \ref database_exception that points at \ref rows; execute() is for statements
         * without a result set.
         *
         * \throws database_exception when a cursor of this statement is still live, when the
         * statement previously failed and was not reset, when the argument set is
         * incomplete, or when the statement returns rows.
         */
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        execution_outcome execute(Args &&...args) {
            begin_call();
            bind_argument_set(std::forward<Args>(args)...);
            return run_to_completion();
        }

        /** \brief Returns a cursor over the rows produced with the manually bound values.
         *
         * Same completeness rule as the zero-argument \ref execute: every parameter must
         * have been bound manually beforehand.
         */
        cursor rows();

        /** \brief Binds the complete argument set and returns a move-only cursor over the rows.
         *
         * Binding follows the rules of execute(args...); a rejected call throws before the
         * statement runs. While the returned cursor lives, the statement is in the
         * \ref execution_state::executing state and every other execution attempt on it is
         * rejected; the cursor ends that ownership only by being destroyed, so consume it
         * in its own scope.
         */
        template <typename... Args>
            requires(sizeof...(Args) > 0)
        cursor rows(Args &&...args) {
            begin_call();
            bind_argument_set(std::forward<Args>(args)...);
            return open_cursor();
        }

        /** \brief Rewinds the statement so a new execution can start.
         *
         * reset() is permitted in every state except while a cursor is live; it never
         * rewinds a live cursor behind the caller's back. Bindings are preserved by
         * default; pass @p clear_bindings to drop them together with the manual-binding
         * bookkeeping. The SQLite error code of a failed evaluation is deliberately not
         * surfaced here: clearing that failure is the purpose of the call. A failed
         * statement becomes reusable only through reset().
         */
        void reset(bool clear_bindings = false);

        /// Returns the current execution state; throws on a moved-from statement.
        execution_state state() const;

        /// Returns the SQL text this statement was prepared with.
        std::string const &sql() const noexcept;

        /// Returns the number of parameters the statement declares.
        int parameter_count() const;

        /// Returns the 1 based index of the named parameter; 0 semantics as \ref command.
        int parameter_index(std::string_view name) const;

        /** \brief Binds NULL to the given 1 based parameter index (advanced mode).
         *
         * Manual binds are only permitted while no execution is running and after a
         * failure only following reset(). They are recorded so the zero-argument
         * execute()/rows() overloads can verify that the argument set is complete.
         */
        void bind(int idx);

        /// Binds @p value to the given 1 based parameter index (advanced mode).
        template <typename Value> void bind(int idx, Value &&value) {
            bind_access_check();
            query_->bind(idx, std::forward<Value>(value));
            mark_manually_bound(idx);
        }

        /// Binds @p value to the named parameter (advanced mode).
        template <typename Value> void bind(std::string_view name, Value &&value) {
            bind_access_check();
            auto idx = query_->parameter_index(name);
            query_->bind(idx, std::forward<Value>(value));
            mark_manually_bound(idx);
        }

        /// Returns the outcome of the last completed execute() call.
        execution_outcome last_outcome() const noexcept;

        prepared_statement(prepared_statement const &)            = delete;
        prepared_statement &operator=(prepared_statement const &) = delete;
        prepared_statement(prepared_statement &&) noexcept;
        /// Not noexcept: assignment across different connections throws.
        prepared_statement &operator=(prepared_statement &&);
        ~prepared_statement();

    private:
        friend struct connection;

        prepared_statement(connection &con, std::string sql);

        void begin_call();
        void bind_access_check();
        template <typename... Args> void bind_argument_set(Args &&...args) {
            // Start from an empty binding set: an invocation must not reuse values that an
            // earlier one bound (the zero-argument overloads cover that case explicitly).
            query_->clear();
            int positional = 0;
            std::vector<int> named_indexes;
            (bind_one(std::forward<Args>(args), positional, named_indexes), ...);
            verify_argument_set(positional, named_indexes);
        }
        template <typename Arg> void bind_one(Arg &&arg, int &positional, std::vector<int> &named) {
            if constexpr (detail::is_named_parameter_v<detail::decay_t<Arg>>) {
                named.push_back(query_->parameter_index(arg.name));
                (*query_) % std::forward<Arg>(arg);
            } else {
                (*query_) % std::forward<Arg>(arg);
                ++positional;
            }
        }
        void verify_argument_set(int positional, std::vector<int> const &named);
        void verify_manual_bindings();
        void mark_manually_bound(int idx);
        execution_outcome run_to_completion();
        cursor open_cursor();

        connection &con_;
        std::string sql_;
        std::unique_ptr<query> query_;
        std::shared_ptr<detail::execution_state_private> execution_;
        std::vector<char> manually_bound_;
        execution_outcome last_outcome_;
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_PREPARED_STATEMENT_HPP_INCLUDED
