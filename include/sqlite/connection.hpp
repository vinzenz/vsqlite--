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
#ifndef GUARD_SQLITE_CONNECTION_HPP_INCLUDED
#define GUARD_SQLITE_CONNECTION_HPP_INCLUDED
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <sqlite/filesystem_adapter.hpp>
#include <sqlite/statement_cache.hpp>

/**
 * @file sqlite/connection.hpp
 * @brief Owning RAII wrapper for `sqlite3*` handles plus attachment helpers and statement caching.
 *
 * The `sqlite::connection` type encapsulates opening/closing databases, attaches additional files,
 * surfaces `sqlite3_last_insert_rowid`, and exposes the statement cache used by higher-level APIs.
 * Databases are opened through the explicit factories `open_file()`, `open_memory()`, and
 * `open_uri()`, or through the string constructors, which classify their argument and route it to
 * the same internal open paths.
 */
struct sqlite3;

namespace sqlite {
inline namespace v2 {
    enum class open_mode {
        open_readonly,  ///< Opens an existing database for reads only or fails
        open_existing,  ///< Opens an existing database; fails when it is missing
        open_or_create, ///< Opens an existing database or creates it on demand
        always_create   ///< Deletes any existing database file and recreates it
    };

    /// Options for the explicit database factories (open_file, open_memory, open_uri).
    struct open_options {
        open_mode mode = open_mode::open_or_create; ///< How to behave when opening the database
        bool nofollow  = false; ///< Add SQLITE_OPEN_NOFOLLOW so SQLite itself refuses a
                                ///< symlinked database file at open time. The option only
                                ///< strengthens the build-wide VSQLITE_ALLOW_FOLLOW_SYMLINKS
                                ///< setting; it never weakens it. Ignored by open_memory.
    };

    /** \brief connection is used to open, close, attach and detach a database.
     * Further it has to be passed to all classes since it represents the
     * connection to the database and contains the internal needed handle, so
     * you can see a connection object as handle to the database
     *
     * A database name starting with "file:" is interpreted as a SQLite URI
     * (see https://www.sqlite.org/uri.html), e.g.
     * "file:name?mode=memory&cache=shared" opens a shared in-memory database.
     * Any other name is used as a literal filename.
     *
     * Explicit factories make the location category known before validation
     * runs. open_file() accepts an ordinary filename, applies the
     * filesystem-adapter validation described below, and is the only entry
     * point that accepts open_mode::always_create. open_memory() opens an
     * in-memory database and never checks, creates, or deletes anything on
     * disk. open_uri() passes SQLITE_OPEN_URI semantics to SQLite and performs
     * no filesystem checks of its own, so locations handled by custom SQLite
     * VFS implementations keep working there.
     *
     * Every entry point, including the string constructors and attach(),
     * rejects database names containing embedded NUL bytes.
     *
     * Symlink policy scope: the pre-open checks inspect the immediate parent
     * directory and the database file entry itself (lstat semantics through
     * the filesystem adapter) and reject symlinks. Symlinked components
     * deeper inside the parent path are followed by the operating system and
     * are not inspected. SQLITE_OPEN_NOFOLLOW (build-wide through
     * VSQLITE_ALLOW_FOLLOW_SYMLINKS=OFF, per call through
     * open_options::nofollow) makes SQLite's VFS refuse a symlinked database
     * file at open time; it covers only the final path component. A path can
     * change between validation and open (TOCTOU); the wrapper does not close
     * that window.
     *
     * An object of this class is not copyable
     */
    struct connection {
        /** \brief constructor opens the database
         * \param db filename of the database file
         *           if the given file already exists the file will be opened
         *           as database.
         *           If the file does not exist a new database will be created
         */
        connection(std::string const &db);
        connection(std::string const &db, filesystem_adapter_ptr fs);
        connection(connection const &)            = delete;
        connection &operator=(connection const &) = delete;

        /** \brief constructor opens the database
         * \param db filename of the database file
         * \param open_mode How to behave when opening the database
         *
         * \remarks
         * If open_mode::always_create is specified and the file
         * exists but cannot be removed, this will throw a
         * database_system_error exception with the system error code causing
         * the failure.
         *
         * If the database does not exist and open_mode::open_existing was
         * specified, a database_exception will be thrown.
         */
        connection(std::string const &db, sqlite::open_mode open_mode);
        connection(std::string const &db, sqlite::open_mode open_mode, filesystem_adapter_ptr fs);

        /*  Migration table - each legacy constructor maps to one explicit factory:
         *
         *    connection("app.db")                            -> open_file("app.db")
         *    connection(":memory:")                          -> open_memory()
         *    connection("file:name?mode=memory&cache=shared")-> open_memory("name")
         *    connection("file:...")   other "file:" URIs     -> open_uri("file:...")
         *    connection("app.db", open_mode::X)              -> open_file("app.db", {.mode = X})
         *
         *  The string constructors classify their argument and route it to the
         *  same internal open paths, so both spellings behave alike.
         */

        /** \brief opens an ordinary database file; the name is never URI-interpreted
         * \param path filesystem path of the database file
         * \param options open mode and per-call symlink policy
         *
         * File-specific validation runs before opening: the immediate parent
         * directory must be a real directory, and an existing target must be a
         * regular file and no symlink. Names starting with "file:" and the
         * special name ":memory:" are rejected because SQLite would not treat
         * them as ordinary filenames; use open_uri() or open_memory() there.
         *
         * This is the only factory that accepts open_mode::always_create.
         * Recreating removes the main database file only; sidecar files
         * (-journal, -wal, -shm) and databases held open by other connections
         * are left alone (POSIX unlink semantics apply there).
         */
        static connection open_file(std::filesystem::path const &path, open_options options = {});

        /** \brief opens a private anonymous in-memory database
         *
         * The filesystem is never checked, created, or deleted.
         */
        static connection open_memory();

        /** \brief opens a named in-memory database shared between open connections
         * \param name logical name of the shared in-memory database; reserved URI
         *             characters are percent-encoded automatically
         * \param options only open_mode::open_or_create is accepted here
         *
         * The name is a pure key: the filesystem is never checked, created, or
         * deleted, so names that look like paths under missing directories are
         * fine. While at least one connection to the same name stays open, all
         * connections see the same database.
         */
        static connection open_memory(std::string const &name, open_options options = {});

        /** \brief opens a database through a SQLite "file:" URI
         * \param uri the URI, e.g. "file:events?mode=memory&cache=shared"
         * \param options open mode and per-call symlink policy
         *
         * SQLITE_OPEN_URI is passed to SQLite, which owns the URI
         * interpretation (percent decoding, query parameters, vfs= selection).
         * The wrapper only rejects what it can decide correctly: embedded NUL
         * bytes, empty URIs, non-"file:" schemes, open_mode::always_create,
         * and a mode parameter that contradicts \p options (mode=ro pairs
         * with open_mode::open_readonly, mode=rw with open_mode::open_existing,
         * mode=create and mode=memory with open_mode::open_or_create; unknown
         * mode values are left to SQLite). No filesystem checks run here, so
         * locations served by custom VFS implementations are not rejected.
         */
        static connection open_uri(std::string const &uri, open_options options = {});

        /** \brief destructor closes the database automatically
         *
         */
        ~connection();

        /** \brief attaches another database file to the database represented by
         * the object of this class. It is possible to attach up to 10 times
         * the same database file with different aliases
         * \param db database filename of the database should be attached
         *           following the same rules as the connection constructors
         * \param database_alias alias which should be used
         *
         * \remarks VSQLite++ opens every connection with URI support enabled,
         * so SQLite interprets "file:" URIs in ATTACH exactly as it does for
         * the connection constructors. All other names are treated as
         * literal filenames.
         */
        void attach(std::string const &db, std::string const &database_alias);

        /** \brief detaches a database via alias, if the same database was
         * attached with several names they will be still present
         * \param database_alias of the database (must be the same alias which
         * was passed in the attach() call)
         */
        void detach(std::string const &database_alias);

        /** \brief Returns the last inserted rowid in the currently opened
         *  database
         */
        std::int64_t get_last_insert_rowid();

        void configure_statement_cache(statement_cache_config const &cfg);
        statement_cache_config statement_cache_settings() const;
        void clear_statement_cache();

    private:
        friend struct private_accessor;

    private:
        void open(std::string const &db);
        void open(std::string const &db, bool readonly);
        void open(std::string const &db, sqlite::open_mode open_mode);
        explicit connection(std::string const &db, int flags, filesystem_adapter_ptr fs);
        void close();
        void access_check();
        void open_with_flags(std::string const &db, int flags);
        sqlite3_stmt *acquire_cached_statement(std::string const &sql);
        void release_cached_statement(std::string const &sql, sqlite3_stmt *stmt);

    private:
        sqlite3 *handle;
        filesystem_adapter_ptr filesystem;
        statement_cache cache_;
    };
} // namespace v2
} // namespace sqlite
#endif // GUARD_SQLITE_CONNECTION_HPP_INCLUDED
