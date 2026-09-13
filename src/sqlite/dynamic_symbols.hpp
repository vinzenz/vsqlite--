#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sqlite {
inline namespace v2 {
    namespace detail {

        inline void *sqlite_module_handle() {
#if defined(_WIN32)
            static HMODULE handle = GetModuleHandleA(nullptr);
            return handle;
#else
            static void *handle = dlopen(nullptr, RTLD_LAZY);
            return handle;
#endif
        }

        template <typename Fn> Fn load_sqlite_symbol(char const *name) {
            auto handle = sqlite_module_handle();
            if (!handle) {
                return nullptr;
            }
#if defined(_WIN32)
            return reinterpret_cast<Fn>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
            return reinterpret_cast<Fn>(dlsym(handle, name));
#endif
        }

// The build system defines VSQLITE_HAVE_SQLITE3_* when it verified that the SQLite
// implementation linked into this library provides the corresponding optional API
// group. In that case the wrappers use direct references, which the linker resolves
// for static and shared builds alike — without exporting the executable's symbols.
// Otherwise the symbols are looked up in the address space of the running process,
// which keeps supporting SQLite implementations that only expose the functions
// dynamically (shared libraries, runtime loaded modules).

#if defined(VSQLITE_HAVE_SQLITE3_SESSION)
#define VSQLITE_SESSION_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SESSION_SYMBOL(fn_t, fn)                                                       \
    (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

#if defined(VSQLITE_HAVE_SQLITE3_SNAPSHOT)
#define VSQLITE_SNAPSHOT_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SNAPSHOT_SYMBOL(fn_t, fn)                                                      \
    (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

#if defined(VSQLITE_HAVE_SQLITE3_SERIALIZE)
#define VSQLITE_SERIALIZE_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SERIALIZE_SYMBOL(fn_t, fn)                                                     \
    (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

    } // namespace detail
} // namespace v2
} // namespace sqlite
