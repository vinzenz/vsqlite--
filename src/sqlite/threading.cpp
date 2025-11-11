#include <sqlite/threading.hpp>

#include <atomic>
#include <mutex>
#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {

    namespace {
        std::atomic<threading_mode> configured_mode(threading_mode::serialized);
        std::mutex threading_mutex;
        bool threading_locked = false;
    }

    bool configure_threading(threading_mode mode) {
        std::lock_guard<std::mutex> guard(threading_mutex);
        if (threading_locked) {
            return configured_mode.load(std::memory_order_relaxed) == mode;
        }
        int flag = SQLITE_CONFIG_SERIALIZED;
        switch (mode) {
        case threading_mode::single_thread:
            flag = SQLITE_CONFIG_SINGLETHREAD;
            break;
        case threading_mode::multi_thread:
            flag = SQLITE_CONFIG_MULTITHREAD;
            break;
        case threading_mode::serialized:
        default:
            flag = SQLITE_CONFIG_SERIALIZED;
            break;
        }

        sqlite3_shutdown();
        int rc = sqlite3_config(flag);
        if (rc != SQLITE_OK) {
            sqlite3_initialize();
            return false;
        }
        rc = sqlite3_initialize();
        if (rc == SQLITE_OK) {
            configured_mode.store(mode, std::memory_order_relaxed);
            threading_locked = true;
            return true;
        }
        return false;
    }

    threading_mode current_threading_mode() {
        return configured_mode.load(std::memory_order_relaxed);
    }

} // namespace v2
} // namespace sqlite
