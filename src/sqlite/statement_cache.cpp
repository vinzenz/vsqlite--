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
#include <sqlite/statement_cache.hpp>

#include <sqlite/private/statement_cache_seam.hpp>
#include <sqlite3.h>

#include <iostream>
#include <new>
#include <string>
#include <utility>

namespace {
// Composes the text handed to the error hook when a returned statement cannot
// be reset cleanly. The error describes the statement's last evaluation, which
// sqlite3_reset reports again while rewinding it.
std::string describe_reset_failure(sqlite3_stmt *stmt, int error_code, std::string_view sql) {
    sqlite3 *db         = sqlite3_db_handle(stmt);
    std::string message = "sqlite3_reset failed with error ";
    message += std::to_string(error_code);
    if (db != nullptr) {
        message += " (";
        message += sqlite3_errmsg(db);
        message += ")";
    }
    message += " while returning: ";
    message.append(sql);
    return message;
}

// The hook is plain diagnostics: it must never turn a cache return into a
// failure, so exceptions escaping it are swallowed.
void report_reset_failure(sqlite::statement_cache_error_hook const &hook,
                          std::string const &message) noexcept {
    if (hook) {
        try {
            hook(message);
        } catch (...) {
        }
        return;
    }
#ifndef NDEBUG
    try {
        std::cerr << "vsqlite++: discarded statement after failed reset: " << message << '\n';
    } catch (...) {
    }
#endif
}
} // namespace

namespace sqlite {
inline namespace v2 {
    namespace statement_cache_seam {
        namespace {
            insertion_failure pending = insertion_failure::none;
        } // namespace

        void fail_next_insertion(insertion_failure point) {
            pending = point;
        }

        insertion_failure take_pending_insertion_failure() {
            insertion_failure point = pending;
            pending                 = insertion_failure::none;
            return point;
        }
    } // namespace statement_cache_seam

    statement_cache::statement_cache(statement_cache_config cfg) : config_(std::move(cfg)) {}

    sqlite3_stmt *statement_cache::acquire(sqlite3 *db, std::string_view sql) {
        if (!config_.enabled || config_.capacity == 0) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(std::string(sql));
        if (it == map_.end()) {
            return nullptr;
        }
        auto node          = *(it->second);
        sqlite3_stmt *stmt = node.stmt;
        lru_.erase(it->second);
        map_.erase(it);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3 *owner = sqlite3_db_handle(stmt);
        if (owner != db) {
            sqlite3_finalize(stmt);
            return nullptr;
        }
        return stmt;
    }

    void statement_cache::release(std::string_view sql, sqlite3_stmt *stmt) noexcept {
        if (stmt == nullptr) {
            return;
        }
        statement_cache_seam::insertion_failure const injected =
            statement_cache_seam::take_pending_insertion_failure();
        if (!config_.enabled || config_.capacity == 0) {
            sqlite3_finalize(stmt);
            return;
        }
        // Statements are returned from destructor paths (the destruction of a
        // statement's final shared owner), so nothing here may escape: every
        // failure path finalizes the statement instead of retaining it, and a
        // reset error is reported through the error hook once the lock is
        // released.
        statement_cache_error_hook hook;
        std::string reset_error;
        bool report_reset_error = false;
        bool settled            = false; // the statement was finalized or stored
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string key(sql);
            if (map_.find(key) != map_.end()) {
                // Another owner already stored this SQL text.
                sqlite3_finalize(stmt);
                settled = true;
            } else {
                // A statement parked at SQLITE_ROW keeps its implicit read transaction
                // open and would block writers on other connections until the next
                // checkout or eviction. Rewind it and drop leftover bindings before
                // retaining it.
                int const reset_result = sqlite3_reset(stmt);
                if (reset_result != SQLITE_OK) {
                    // The statement's last evaluation failed; it must not be retained.
                    reset_error        = describe_reset_failure(stmt, reset_result, sql);
                    hook               = error_hook_; // copied under the lock
                    report_reset_error = true;
                    sqlite3_finalize(stmt);
                    settled = true;
                } else {
                    sqlite3_clear_bindings(stmt);
                    if (lru_.size() >= config_.capacity) {
                        auto &back = lru_.back();
                        sqlite3_finalize(back.stmt);
                        map_.erase(back.sql);
                        lru_.pop_back();
                    }
                    if (injected == statement_cache_seam::insertion_failure::before_bookkeeping) {
                        // Test seam: behaves like a map allocation failure below.
                        throw std::bad_alloc();
                    }
                    // Insert the map node first so a failed list insertion can be
                    // rolled back: on any allocation failure the statement is
                    // finalized here, never cached-but-unreachable or owned twice.
                    auto [it, inserted] = map_.emplace(std::move(key), lru_.end());
                    if (!inserted) {
                        sqlite3_finalize(stmt);
                        settled = true;
                    } else {
                        try {
                            if (injected ==
                                statement_cache_seam::insertion_failure::before_retain) {
                                // Test seam: behaves like a list allocation failure.
                                throw std::bad_alloc();
                            }
                            lru_.push_front(entry{it->first, stmt});
                        } catch (...) {
                            map_.erase(it);
                            throw;
                        }
                        it->second = lru_.begin();
                        settled    = true;
                    }
                }
            }
        } catch (...) {
            // Cache bookkeeping ran out of memory; discard the statement instead
            // of leaking it or propagating the failure out of a destructor.
            report_reset_error = false;
            if (!settled) {
                sqlite3_finalize(stmt);
            }
            return;
        }
        if (report_reset_error) {
            report_reset_failure(hook, reset_error);
        }
    }

    void statement_cache::set_error_hook(statement_cache_error_hook hook) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_hook_ = std::move(hook);
    }

    void statement_cache::clear(sqlite3 *) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &node : lru_) {
            sqlite3_finalize(node.stmt);
        }
        lru_.clear();
        map_.clear();
    }

    void statement_cache::reset(statement_cache_config cfg) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &node : lru_) {
            sqlite3_finalize(node.stmt);
        }
        lru_.clear();
        map_.clear();
        config_ = std::move(cfg);
    }
} // namespace v2
} // namespace sqlite
