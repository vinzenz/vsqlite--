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
#include <sqlite/prepared_statement.hpp>

#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace sqlite {
inline namespace v2 {
    namespace {
        database_exception rows_rejected(std::string const &sql) {
            return database_exception(
                "prepared_statement::execute: '" + sql +
                "' returns rows (SELECT or a statement with RETURNING); use rows() to consume "
                "the result set");
        }

        database_exception cursor_live(char const *operation) {
            return database_exception(std::string("prepared_statement::") + operation +
                                      ": another execution is still running; a cursor returned "
                                      "by rows() owns the statement until it is destroyed or "
                                      "reaches its end");
        }

        database_exception failed_not_reset(char const *operation) {
            return database_exception(std::string("prepared_statement::") + operation +
                                      ": the previous execution failed; call reset() before "
                                      "starting another execution");
        }
    } // namespace

    void detail::execution_state_private::finish() {
        if (state != execution_state::executing) {
            return;
        }
        // Rewind so the statement releases its locks even though the cursor object is only
        // now going away. sqlite3_reset() reports the error of a failed evaluation here;
        // that error was already surfaced (or nothing ran at all), so it is deliberately
        // not thrown again.
        if (stmt) {
            (void)sqlite3_reset(stmt);
        }
        state = execution_state::complete;
    }

    void detail::execution_state_private::fail() {
        // Clear the failed evaluation so the underlying statement can run again once the
        // caller went through prepared_statement::reset(); the failure itself is recorded
        // in the state, not in SQLite's per-statement error slot.
        if (stmt) {
            (void)sqlite3_reset(stmt);
        }
        state = execution_state::failed;
    }

    cursor::cursor(query::result_range range,
                   std::shared_ptr<detail::execution_state_private> execution) :
        range_(std::move(range)), execution_(std::move(execution)) {}

    cursor::cursor(cursor &&other) noexcept :
        range_(other.range_), execution_(std::move(other.execution_)), begun_(other.begun_) {
        other.range_ = query::result_range();
        other.begun_ = false;
    }

    cursor &cursor::operator=(cursor &&other) noexcept {
        if (this != &other) {
            if (execution_ && execution_->state == execution_state::executing) {
                execution_->finish();
            }
            range_       = other.range_;
            execution_   = std::move(other.execution_);
            begun_       = other.begun_;
            other.range_ = query::result_range();
            other.begun_ = false;
        }
        return *this;
    }

    cursor::~cursor() {
        if (execution_ && execution_->state == execution_state::executing) {
            execution_->finish();
        }
    }

    cursor::iterator cursor::begin() {
        if (!execution_ || begun_) {
            return iterator();
        }
        begun_ = true;
        query::result_range::iterator inner;
        try {
            inner = range_.begin();
        } catch (...) {
            execution_->fail();
            throw;
        }
        if (inner == range_.end()) {
            // The range is exhausted before it yielded a row. The cursor still owns the
            // execution for the rest of its lifetime; only its destruction (or a stepping
            // error) ends that ownership, so nothing resets the statement behind the
            // caller's back.
            return iterator();
        }
        return iterator(std::move(inner), execution_);
    }

    cursor::iterator cursor::end() const {
        return iterator();
    }

    cursor::iterator::iterator(query::result_range::iterator inner,
                               std::shared_ptr<detail::execution_state_private> execution) :
        inner_(std::move(inner)), execution_(std::move(execution)) {}

    cursor::iterator::reference cursor::iterator::operator*() const {
        return *inner_;
    }

    cursor::iterator::pointer cursor::iterator::operator->() const {
        return &*inner_;
    }

    cursor::iterator &cursor::iterator::operator++() {
        step();
        return *this;
    }

    cursor::postfix_proxy cursor::iterator::operator++(int) {
        try {
            return inner_++;
        } catch (...) {
            execution_->fail();
            inner_ = query::result_range::iterator();
            throw;
        }
    }

    void cursor::iterator::step() {
        if (!execution_) {
            inner_ = query::result_range::iterator();
            return;
        }
        try {
            ++inner_;
        } catch (...) {
            execution_->fail();
            inner_ = query::result_range::iterator();
            throw;
        }
        // Reaching the end does not finish the execution: the cursor object keeps owning
        // the statement until it is destroyed, so a later execution cannot reset the
        // cursor's statement behind the caller's back.
    }

    bool cursor::iterator::operator==(iterator const &other) const {
        return inner_ == other.inner_;
    }

    bool cursor::iterator::operator!=(iterator const &other) const {
        return inner_ != other.inner_;
    }

    prepared_statement::prepared_statement(connection &con, std::string sql) :
        con_(con), sql_(std::move(sql)), query_(std::make_unique<query>(con, sql_)),
        execution_(std::make_shared<detail::execution_state_private>()), manually_bound_(),
        last_outcome_() {
        execution_->stmt = private_accessor::statement(*query_);
    }

    prepared_statement::prepared_statement(prepared_statement &&other) noexcept :
        con_(other.con_), sql_(std::move(other.sql_)), query_(std::move(other.query_)),
        execution_(std::move(other.execution_)), manually_bound_(std::move(other.manually_bound_)),
        last_outcome_(other.last_outcome_) {}

    prepared_statement &prepared_statement::operator=(prepared_statement &&other) {
        if (this != &other) {
            if (&con_ != &other.con_) {
                throw std::runtime_error(
                    "prepared_statement move-assignment requires both statements to use the "
                    "same connection");
            }
            query_          = std::move(other.query_);
            execution_      = std::move(other.execution_);
            manually_bound_ = std::move(other.manually_bound_);
            last_outcome_   = other.last_outcome_;
            sql_            = std::move(other.sql_);
        }
        return *this;
    }

    prepared_statement::~prepared_statement() {}

    void prepared_statement::begin_call() {
        private_accessor::acccess_check(con_);
        if (!query_ || !execution_) {
            throw database_exception("prepared_statement was moved from");
        }
        if (execution_->state == execution_state::executing) {
            throw cursor_live("execute/rows");
        }
        if (execution_->state == execution_state::failed) {
            throw failed_not_reset("execute/rows");
        }
    }

    void prepared_statement::bind_access_check() {
        private_accessor::acccess_check(con_);
        if (!query_ || !execution_) {
            throw database_exception("prepared_statement was moved from");
        }
        if (execution_->state == execution_state::executing) {
            throw cursor_live("bind");
        }
        if (execution_->state == execution_state::failed) {
            throw failed_not_reset("bind");
        }
    }

    void prepared_statement::verify_argument_set(int positional, std::vector<int> const &named) {
        int count = sqlite3_bind_parameter_count(execution_->stmt);
        // Allocation-free completeness check: positional arguments cover indexes 1..N in
        // their call order, named arguments carry their own indexes.
        for (int i = 1; i <= count; ++i) {
            bool is_bound = i <= positional;
            for (int idx : named) {
                if (idx == i) {
                    is_bound = true;
                    break;
                }
            }
            if (!is_bound) {
                throw database_exception(
                    "prepared_statement: incomplete argument set for '" + sql_ + "': parameter " +
                    std::to_string(i) +
                    " is not bound; supply every parameter in the execute()/rows() call, or "
                    "bind manually and use the zero-argument overload");
            }
        }
    }

    void prepared_statement::verify_manual_bindings() {
        int count = sqlite3_bind_parameter_count(execution_->stmt);
        for (int i = 1; i <= count; ++i) {
            auto idx = static_cast<std::size_t>(i);
            if (idx >= manually_bound_.size() || !manually_bound_[idx]) {
                throw database_exception(
                    "prepared_statement: incomplete argument set for '" + sql_ + "': parameter " +
                    std::to_string(i) +
                    " is not bound; pass the arguments to execute()/rows(), or bind every "
                    "parameter manually before calling the zero-argument overload");
            }
        }
    }

    void prepared_statement::mark_manually_bound(int idx) {
        if (idx < 1) {
            return;
        }
        auto position = static_cast<std::size_t>(idx);
        if (manually_bound_.size() <= position) {
            manually_bound_.resize(position + 1, 0);
        }
        manually_bound_[position] = 1;
    }

    execution_outcome prepared_statement::run_to_completion() {
        sqlite3_stmt *stmt = execution_->stmt;
        sqlite3 *db        = sqlite3_db_handle(stmt);
        // Statements that return rows are the domain of rows(); reject them before the
        // statement runs so the caller's data is not half-written when the error appears.
        if (sqlite3_column_count(stmt) != 0) {
            throw rows_rejected(sql_);
        }
        execution_->state = execution_state::executing;
        int err           = sqlite3_step(stmt);
        if (err != SQLITE_DONE) {
            execution_->fail();
            if (err == SQLITE_MISUSE) {
                throw database_misuse_exception_code(sqlite3_errmsg(db), err, sql_);
            }
            throw database_exception_code(sqlite3_errmsg(db), err, sql_);
        }
        execution_outcome outcome;
        // Capture both counters while this statement's completion is still the connection's
        // most recent one; statements executed afterwards cannot alter the snapshot.
        outcome.affected_rows     = sqlite3_changes(db);
        outcome.last_insert_rowid = static_cast<std::int64_t>(sqlite3_last_insert_rowid(db));
        (void)sqlite3_reset(stmt); // SQLITE_OK after SQLITE_DONE; releases locks for reuse
        execution_->state = execution_state::complete;
        last_outcome_     = outcome;
        return outcome;
    }

    cursor prepared_statement::open_cursor() {
        // get_result() prepares the result without stepping; a throw leaves the previous
        // state untouched, so the statement stays reusable.
        auto res          = query_->get_result();
        execution_->state = execution_state::executing;
        return cursor(query::result_range(std::move(res)), execution_);
    }

    execution_outcome prepared_statement::execute() {
        begin_call();
        verify_manual_bindings();
        return run_to_completion();
    }

    cursor prepared_statement::rows() {
        begin_call();
        verify_manual_bindings();
        return open_cursor();
    }

    void prepared_statement::reset(bool clear_bindings) {
        private_accessor::acccess_check(con_);
        if (!execution_) {
            throw database_exception("prepared_statement was moved from");
        }
        if (execution_->state == execution_state::executing) {
            throw database_exception(
                "prepared_statement::reset: cannot reset while a cursor is live; a live cursor "
                "must not be rewound behind the caller's back");
        }
        if (execution_->stmt) {
            // The returned error code belongs to the already surfaced failure; clearing it
            // is the purpose of this call, so it is not thrown here.
            (void)sqlite3_reset(execution_->stmt);
            if (clear_bindings) {
                sqlite3_clear_bindings(execution_->stmt);
                manually_bound_.clear();
            }
        }
        execution_->state = execution_state::prepared;
    }

    execution_state prepared_statement::state() const {
        if (!execution_) {
            throw std::runtime_error("prepared_statement was moved from");
        }
        return execution_->state;
    }

    std::string const &prepared_statement::sql() const noexcept {
        return sql_;
    }

    int prepared_statement::parameter_count() const {
        if (!execution_) {
            throw std::runtime_error("prepared_statement was moved from");
        }
        return sqlite3_bind_parameter_count(execution_->stmt);
    }

    int prepared_statement::parameter_index(std::string_view name) const {
        if (!query_) {
            throw std::runtime_error("prepared_statement was moved from");
        }
        return query_->parameter_index(name);
    }

    void prepared_statement::bind(int idx) {
        bind_access_check();
        query_->bind(idx);
        mark_manually_bound(idx);
    }

    execution_outcome prepared_statement::last_outcome() const noexcept {
        return last_outcome_;
    }
} // namespace v2
} // namespace sqlite
