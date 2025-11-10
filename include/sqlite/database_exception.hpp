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
#ifndef GUARD_SQLITE_DATABASE_EXCEPTION_HPP_INCLUDED
#define GUARD_SQLITE_DATABASE_EXCEPTION_HPP_INCLUDED

#include <stdexcept>
#include <string>
#include <utility>

/**
 * @file sqlite/database_exception.hpp
 * @brief Exception hierarchy mirroring common SQLite failure categories.
 *
 * The types in this header surface `sqlite3_errcode` values, SQL snippets, and system
 * error codes so callers can build rich diagnostics without sprinkling raw strings.
 */
namespace sqlite {
inline namespace v2 {
    /// Generic runtime failure raised for most SQLite errors.
    struct database_exception : public std::runtime_error {
        database_exception(std::string const & msg)
        : std::runtime_error(msg.c_str())
        {}
    };

    /// Helper that appends ` [SQL: ...]` context to an existing message.
    inline std::string append_sql_context(std::string message, std::string const & sql){
        if(sql.empty()){
            return message;
        }
        message.append(" [SQL: ");
        message.append(sql);
        message.push_back(']');
        return message;
    }

    /// Exception that carries the original SQLite error code and optional SQL snippet.
    struct database_exception_code : database_exception {
        database_exception_code(std::string const & error_message,
                                int sqlite_error_code,
                                std::string sql_context = std::string())
        : database_exception(append_sql_context(error_message, sql_context))
        , sqlite_error_code_(sqlite_error_code)
        , sql_(std::move(sql_context))
        {}

        int error_code() const {
            return sqlite_error_code_;
        }

        std::string const & sql() const {
            return sql_;
        }
    protected:
        int const sqlite_error_code_;
        std::string sql_;
    };

    /// Raised when a caller-provided buffer is too small to hold a blob/text payload.
    struct buffer_too_small_exception : public std::runtime_error{
        buffer_too_small_exception(std::string const & msg)
            : std::runtime_error(msg.c_str()){}
    };

    /// Used for programming errors such as double-closing or using invalidated resources.
    struct database_misuse_exception : public std::logic_error{
        database_misuse_exception(std::string const & msg)
        : std::logic_error(msg)
        {}
    };

    /// Logic-error flavour that also exposes the SQLite status code and SQL string.
    struct database_misuse_exception_code : database_misuse_exception {
        database_misuse_exception_code(std::string const & msg,
                                       int sqlite_error_code,
                                       std::string sql_context = std::string())
        : database_misuse_exception(append_sql_context(msg, sql_context))
        , sqlite_error_code_(sqlite_error_code)
        , sql_(std::move(sql_context))
        {}

        int error_code() const {
            return sqlite_error_code_;
        }

        std::string const & sql() const {
            return sql_;
        }
    protected:
        int const sqlite_error_code_;
        std::string sql_;
    };

    /// Wraps system-level failures (e.g., file I/O) that bubble up from SQLite APIs.
    struct database_system_error : database_exception {
        database_system_error(std::string const & msg,
                              int error_code)
        : database_exception(msg)
        , error_code_(error_code)
        {}

        int error_code() const {
            return error_code_;
        }
    protected:
        int error_code_;
    };
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_DATABASE_EXCEPTION_HPP_INCLUDED
