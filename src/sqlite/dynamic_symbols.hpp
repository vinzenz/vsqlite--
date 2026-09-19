#pragma once

#include <sqlite3.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sqlite {
inline namespace v2 {
    namespace detail {

        // Returns the module handle of the SQLite implementation this library actually
        // uses, derived from the code that satisfied the link-time reference to
        // sqlite3_libversion. Optional APIs resolved from this handle therefore belong to
        // the same implementation as the connection handles, instead of whichever module
        // a process-global symbol search would happen to find first.
        //
        // Remaining limits of this fallback: when SQLite is statically linked into this
        // library (or its consumer), the handle describes that image and the optional
        // symbols are only found when that image exports them. A module path that no
        // longer resolves for the loader (relative path with a changed working directory,
        // replaced or deleted file) falls back to the main program handle, whose search
        // can again reach a foreign SQLite copy loaded by another module. Builds that
        // verified an API group never use this path; they reference the symbols directly.
        inline void *sqlite_module_handle() {
#if defined(_WIN32)
            static HMODULE handle = []() -> HMODULE {
                // The address of an imported function denotes an import thunk inside this
                // image, so derive the module from data the SQLite build itself produced:
                // the version string lives in the image that implements the API.
                char const *version = sqlite3_libversion();
                if (version == nullptr) {
                    return nullptr;
                }
                HMODULE module = nullptr;
                // FROM_ADDRESS maps the pointer to its module; UNCHANGED_REFCOUNT keeps
                // the reference count as is, because the module stays loaded for as long
                // as this library links against it.
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        reinterpret_cast<LPCWSTR>(version), &module)) {
                    return nullptr;
                }
                return module;
            }();
            return handle;
#else
            static void *handle = []() -> void * {
                Dl_info info{};
                if (dladdr(reinterpret_cast<void const *>(&sqlite3_libversion), &info) == 0 ||
                    info.dli_fname == nullptr) {
                    return nullptr;
                }
                // RTLD_NOLOAD returns the handle of the already loaded module without
                // loading a second copy of it.
                if (void *loaded = dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD)) {
                    return loaded;
                }
                // Last resort for a module path the loader no longer resolves (see the
                // limits above).
                return dlopen(nullptr, RTLD_LAZY);
            }();
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
        // Otherwise the symbols are looked up in the module that provides the SQLite
        // implementation this library links against, which keeps supporting SQLite
        // implementations that only expose the functions dynamically (shared libraries,
        // runtime loaded modules).

#if defined(VSQLITE_HAVE_SQLITE3_SESSION)
#define VSQLITE_SESSION_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SESSION_SYMBOL(fn_t, fn) (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

#if defined(VSQLITE_HAVE_SQLITE3_SNAPSHOT)
#define VSQLITE_SNAPSHOT_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SNAPSHOT_SYMBOL(fn_t, fn) (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

#if defined(VSQLITE_HAVE_SQLITE3_SERIALIZE)
#define VSQLITE_SERIALIZE_SYMBOL(fn_t, fn) (&::fn)
#else
#define VSQLITE_SERIALIZE_SYMBOL(fn_t, fn) (::sqlite::detail::load_sqlite_symbol<fn_t>(#fn))
#endif

    } // namespace detail
} // namespace v2
} // namespace sqlite
