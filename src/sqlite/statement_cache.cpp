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

#include <sqlite/database_exception.hpp>
#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {
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

    void statement_cache::release(std::string_view sql, sqlite3_stmt *stmt) {
        if (!config_.enabled || config_.capacity == 0 || !stmt) {
            sqlite3_finalize(stmt);
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::string(sql);
        if (map_.find(key) != map_.end()) {
            sqlite3_finalize(stmt);
            return;
        }
        if (lru_.size() >= config_.capacity) {
            auto &back = lru_.back();
            sqlite3_finalize(back.stmt);
            map_.erase(back.sql);
            lru_.pop_back();
        }
        lru_.push_front(entry{key, stmt});
        map_[key] = lru_.begin();
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
