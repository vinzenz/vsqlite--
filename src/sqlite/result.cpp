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
#include <sqlite/private/result_construct_params_private.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/detail/conversion.hpp>
#include <sqlite/result.hpp>
#include <sqlite/query.hpp>
#include <sqlite3.h>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <limits>

namespace sqlite {
inline namespace v2 {
    namespace detail {
        bool end(result_construct_params_private const &params) {
            return params.ended;
        }

        void reset(result_construct_params_private &params) {
            // Rewind the prepared statement itself; clearing the end flag alone would
            // leave a partially consumed cursor advancing from the next row. Bindings
            // are preserved by sqlite3_reset. The statement is reset even when the
            // call reports the error of the last evaluation, so the cursor state is
            // updated before that error is surfaced.
            int err        = sqlite3_reset(params.statement);
            params.ended   = false;
            params.changes = 0;
            // Results sharing the statement keep their own ended flag; rewind theirs
            // too so exhausted siblings revive instead of staying unusable. The
            // statement will run again, so their captured affected-row counts also
            // go back to 0 until the new run completes.
            if (params.siblings) {
                for (auto const &weak : params.siblings->live) {
                    if (auto sibling = weak.lock()) {
                        sibling->ended   = false;
                        sibling->changes = 0;
                    }
                }
            }
            if (err != SQLITE_OK)
                throw database_exception_code(sqlite3_errmsg(params.db), err, params.sql);
        }
    } // namespace detail

    result::result(construct_params p) : m_params(p) {
        m_params->access_check();
        m_columns = sqlite3_column_count(m_params->statement);
    }

    result::~result() {}

    bool result::next_row() {
        if (!m_params->ended) {
            m_params->ended = !m_params->step();
            if (m_params->ended) {
                // The statement just finished, so sqlite3_changes() now reports the
                // rows affected by it. Snapshot the count so statements executed
                // afterwards on the same connection cannot overwrite it.
                m_params->changes = sqlite3_changes(m_params->db);
            }
            return !end();
        }
        return false;
    }

    std::string result::get_column_decltype(int idx) {
        access_check(idx);
        auto *decltype_name = sqlite3_column_decltype(m_params->statement, idx);
        return decltype_name ? decltype_name : "";
    }

    type result::get_column_type(int idx) {
        access_check(idx);
        switch (sqlite3_column_type(m_params->statement, idx)) {
        case SQLITE_BLOB:
            return sqlite::blob;
        case SQLITE_NULL:
            return sqlite::null;
        case SQLITE_FLOAT:
            return sqlite::real;
        case SQLITE_INTEGER:
            return sqlite::integer;
        case SQLITE_TEXT:
            return sqlite::text;
        default:
            break;
        }
        return sqlite::unknown;
    }

    variant_t result::get_variant(int idx) {
        variant_t v;
        switch (get_column_type(idx)) {
        case sqlite::integer: {
            std::int64_t i = get_int64(idx);
            if (i > std::numeric_limits<int>::max() || i < std::numeric_limits<int>::min()) {
                v = i;
            } else {
                v = int(i);
            }
        } break;
        case sqlite::blob:
            v = std::make_shared<blob_t>();
            get_binary(idx, *std::get<blob_ref_t>(v));
            break;
        case sqlite::real: {
            long double x = get_double(idx);
            v             = x;
        } break;
        case sqlite::null:
            v = null_t();
            break;
        default:
        case sqlite::text:
            v = get_string(idx);
            break;
        }
        return v;
    }

    int result::get_int(int idx) {
        access_check(idx);
        return detail::conversion::legacy_int(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    std::int64_t result::get_int64(int idx) {
        access_check(idx);
        return detail::conversion::legacy_int64(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    void result::require_not_null(int idx) {
        access_check(idx);
        if (detail::conversion::column_adapter{m_params->statement, idx}.is_null()) {
            throw database_exception(
                "NULL in column read through sqlite::result::get_checked but the requested type "
                "is not nullable.");
        }
    }

    std::int64_t result::checked_int64(int idx) {
        require_not_null(idx);
        return get_int64(idx);
    }

    double result::checked_double(int idx) {
        require_not_null(idx);
        return get_double(idx);
    }

    std::string result::get_string(int idx) {
        auto view = get_string_view(idx);
        return std::string(view.begin(), view.end());
    }

    std::string_view result::get_string_view(int idx) {
        access_check(idx);
        return detail::conversion::legacy_text(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    double result::get_double(int idx) {
        access_check(idx);
        return detail::conversion::legacy_double(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    size_t result::get_binary_size(int idx) {
        access_check(idx);
        return detail::conversion::legacy_blob_size(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    void result::get_binary(int idx, void *buf, size_t buf_size) {
        access_check(idx);
        detail::conversion::column_adapter adapted{m_params->statement, idx};
        if (adapted.is_null())
            return;
        size_t size = static_cast<size_t>(adapted.byte_count());
        if (size > buf_size)
            throw buffer_too_small_exception("buffer too small");
        memcpy(buf, adapted.blob_value(), size);
    }

    void result::get_binary(int idx, std::vector<unsigned char> &v) {
        auto span = get_binary_span(idx);
        v.assign(span.begin(), span.end());
    }

    std::span<const unsigned char> result::get_binary_span(int idx) {
        access_check(idx);
        return detail::conversion::legacy_blob(
            detail::conversion::column_adapter{m_params->statement, idx});
    }

    std::string result::get_column_name(int idx) {
        access_check(idx);
        return sqlite3_column_name(m_params->statement, idx);
    }

    bool result::is_null(int idx) {
        access_check(idx);
        return sqlite3_column_type(m_params->statement, idx) == SQLITE_NULL;
    }

    void result::access_check(int idx) {
        m_params->access_check();
        if (idx < 0 || idx >= m_columns)
            throw std::out_of_range("no such column index");
    }

    int result::get_changes() {
        return m_params->changes;
    }

    int result::get_column_count() {
        return m_columns;
    }
} // namespace v2
} // namespace sqlite
