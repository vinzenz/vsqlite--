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
#ifndef GUARD_SQLITE_PRIVATE_STATEMENT_CACHE_SEAM_HPP_INCLUDED
#define GUARD_SQLITE_PRIVATE_STATEMENT_CACHE_SEAM_HPP_INCLUDED

#include <sqlite/statement_cache.hpp>

namespace sqlite {
inline namespace v2 {
    /** \brief Test-only seam for statement_cache, not part of the public API.
     *
     * Unit tests use it to reach the cache insertion failure paths of
     * statement_cache::release(), which normal execution cannot trigger without
     * an actual out-of-memory condition. The seam is a single process-wide slot
     * and is not synchronized: it is meant for single-threaded tests only.
     */
    namespace statement_cache_seam {
        /// The insertion step of statement_cache::release() that fails as if its
        /// allocation had failed.
        enum class insertion_failure {
            none,               ///< Do not interfere with the next return.
            before_bookkeeping, ///< Fail before the map entry is created.
            before_retain       ///< Fail after the map entry exists, before the
                                ///< statement is retained in the LRU list.
        };

        /// Requests that the next statement returned to any statement cache fails
        /// at \p point, exactly once.
        void fail_next_insertion(insertion_failure point);

        /// Returns and clears the currently requested failure point.
        insertion_failure take_pending_insertion_failure();
    } // namespace statement_cache_seam
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_PRIVATE_STATEMENT_CACHE_SEAM_HPP_INCLUDED
