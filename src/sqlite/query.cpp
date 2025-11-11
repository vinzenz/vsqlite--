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
#include <memory>
#include <utility>
#include <stdexcept>
#include <sqlite/private/result_construct_params_private.hpp>
#include <sqlite/query.hpp>
#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {
    query::query(connection &con, std::string const &sql) : command(con, sql) {}

    query::~query() {}

    std::shared_ptr<result> query::emit_result() {
        bool ended           = !step();
        auto params          = std::make_shared<result_construct_params_private>();
        params->access_check = [this]() { access_check(); };
        params->step         = [this]() -> bool { return step(); };
        params->db           = sqlite3_db_handle(stmt);
        params->changes      = sqlite3_changes(params->db);
        params->statement    = stmt;
        params->ended        = ended;
        return std::shared_ptr<result>(new result(params));
    }

    std::shared_ptr<result> query::get_result() {
        access_check();
        auto params          = std::make_shared<result_construct_params_private>();
        params->access_check = [this]() { access_check(); };
        params->step         = [this]() -> bool { return step(); };
        params->db           = sqlite3_db_handle(stmt);
        params->changes      = sqlite3_changes(params->db);
        params->statement    = stmt;
        params->ended        = false;
        return std::shared_ptr<result>(new result(params));
    }

    query::result_range query::each() {
        return result_range(get_result());
    }

    query::result_range::result_range() = default;

    query::result_range::result_range(result_type res) :
        result_(std::move(res)), cache_(result_ ? std::make_shared<column_cache>() : nullptr) {}

    query::result_range::iterator::iterator() = default;

    query::result_range::iterator::iterator(result_type res, std::shared_ptr<column_cache> cache,
                                            bool end) :
        result_(std::move(res)), end_(end), cache_(std::move(cache)) {
        if (result_ && !end_) {
            prime_cache();
            advance();
        }
    }

    void query::result_range::iterator::prime_cache() {
        if (!result_ || !cache_) {
            return;
        }
        if (!cache_->lookup.empty()) {
            return;
        }
        int columns = result_->get_column_count();
        cache_->names.reserve(columns);
        for (int i = 0; i < columns; ++i) {
            auto name = result_->get_column_name(i);
            cache_->lookup.emplace(name, i);
            cache_->names.push_back(name);
        }
    }

    void query::result_range::iterator::advance() {
        if (!result_) {
            end_     = true;
            current_ = row_view();
            return;
        }
        if (!result_->next_row()) {
            result_.reset();
            end_     = true;
            current_ = row_view();
        } else {
            end_     = false;
            current_ = row_view(result_.get(), cache_);
        }
    }

    query::result_range::iterator::reference query::result_range::iterator::operator*() const {
        return const_cast<row_view &>(current_);
    }

    query::result_range::iterator::pointer query::result_range::iterator::operator->() const {
        return const_cast<row_view *>(&current_);
    }

    query::result_range::iterator &query::result_range::iterator::operator++() {
        advance();
        return *this;
    }

    query::result_range::iterator query::result_range::iterator::operator++(int) {
        auto tmp = *this;
        advance();
        return tmp;
    }

    bool query::result_range::iterator::operator==(iterator const &other) const {
        if (end_ && other.end_) {
            return true;
        }
        return result_ == other.result_ && end_ == other.end_;
    }

    bool query::result_range::iterator::operator!=(iterator const &other) const {
        return !(*this == other);
    }

    query::result_range::iterator query::result_range::begin() {
        if (begin_called_ || !result_) {
            return iterator();
        }
        begin_called_ = true;
        if (!cache_) {
            cache_ = std::make_shared<column_cache>();
        }
        return iterator(result_, cache_, false);
    }

    query::result_range::iterator query::result_range::end() const {
        return iterator();
    }

    int query::result_range::column_cache::index_of(std::string_view name) const {
        auto it = lookup.find(std::string(name));
        if (it == lookup.end()) {
            throw std::out_of_range("no such column name");
        }
        return it->second;
    }

    int query::result_range::row_view::column(std::string_view name) const {
        if (!cache_) {
            throw std::runtime_error("column cache is not initialized");
        }
        return cache_->index_of(name);
    }

    void query::access_check() {
        command::access_check();
    }
    bool query::step() {
        return command::step();
    }
} // namespace v2
} // namespace sqlite
