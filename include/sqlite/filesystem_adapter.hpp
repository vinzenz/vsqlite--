/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2024
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
#ifndef GUARD_SQLITE_FILESYSTEM_ADAPTER_HPP_INCLUDED
#define GUARD_SQLITE_FILESYSTEM_ADAPTER_HPP_INCLUDED

#include <filesystem>
#include <memory>

/**
 * @file sqlite/filesystem_adapter.hpp
 * @brief Pluggable abstraction for the filesystem operations used by `sqlite::connection`.
 *
 * Consumers can override the default adapter to redirect file lookups/removals to virtual file
 * systems or to inject additional validation when opening databases.
 */
namespace sqlite {
inline namespace v2 {

    /// Result of probing a filesystem path that bundles status metadata and the failure code.
    struct filesystem_entry {
        std::filesystem::file_status status;
        std::error_code error;
    };

    /// Interface for querying and mutating filesystem paths before SQLite touches them.
    struct filesystem_adapter {
        virtual ~filesystem_adapter() = default;

        virtual filesystem_entry status(std::filesystem::path const &target) const          = 0;
        virtual bool remove(std::filesystem::path const &target, std::error_code &ec) const = 0;
    };

    /// Default adapter that simply forwards to `std::filesystem`.
    class default_filesystem_adapter : public filesystem_adapter {
    public:
        filesystem_entry status(std::filesystem::path const &target) const override {
            std::error_code ec;
            auto stat = std::filesystem::symlink_status(target, ec);
            return {stat, ec};
        }

        bool remove(std::filesystem::path const &target, std::error_code &ec) const override {
            return std::filesystem::remove(target, ec);
        }
    };

    using filesystem_adapter_ptr = std::shared_ptr<filesystem_adapter>;

} // namespace v2
} // namespace sqlite

#endif
