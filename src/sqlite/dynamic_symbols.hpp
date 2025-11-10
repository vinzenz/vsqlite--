#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sqlite {
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

} // namespace detail
} // namespace sqlite
