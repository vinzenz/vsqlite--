#include <sqlite/connection_pool.hpp>

#include <sqlite/database_exception.hpp>

namespace sqlite {
inline namespace v2 {

    struct connection_pool::pool_state {
        connection_factory factory;
        std::size_t capacity = 0;
        std::size_t created = 0;
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::shared_ptr<connection>> idle;

        void release(std::shared_ptr<connection> conn) {
            std::lock_guard<std::mutex> lock(mutex);
            idle.push_back(std::move(conn));
            cv.notify_one();
        }
    };

    struct connection_pool::lease::shared_state {
        std::weak_ptr<pool_state> pool;
        std::shared_ptr<connection> resource;

        ~shared_state() {
            if (resource) {
                if (auto state = pool.lock()) {
                    state->release(std::move(resource));
                }
            }
        }
    };

    connection_pool::lease::lease(connection_pool *pool, std::shared_ptr<connection> conn) {
        if (pool && conn) {
            state_             = std::make_shared<shared_state>();
            state_->pool       = pool->state_;
            state_->resource   = std::move(conn);
            connection_        = std::shared_ptr<connection>(state_, state_->resource.get());
        }
    }

    connection_pool::lease::lease(lease &&other) noexcept = default;

    connection_pool::lease &connection_pool::lease::operator=(lease &&other) noexcept {
        if (this != &other) {
            release();
            state_      = std::move(other.state_);
            connection_ = std::move(other.connection_);
        }
        return *this;
    }

    connection_pool::lease::~lease() {
        release();
    }

    void connection_pool::lease::release() {
        connection_.reset();
        state_.reset();
    }

    connection &connection_pool::lease::operator*() const {
        return *connection_;
    }

    connection *connection_pool::lease::operator->() const {
        return connection_.get();
    }

    std::shared_ptr<connection> connection_pool::lease::shared() const {
        return connection_;
    }

    connection_pool::connection_pool(std::size_t capacity, connection_factory factory) :
        state_(std::make_shared<pool_state>()) {
        if (capacity == 0) {
            throw database_exception("connection_pool capacity must be greater than zero");
        }
        if (!factory) {
            throw database_exception("connection_pool requires a valid factory");
        }
        state_->factory  = std::move(factory);
        state_->capacity = capacity;
    }

    connection_pool::connection_factory
    connection_pool::make_factory(std::string db, open_mode mode, filesystem_adapter_ptr fs) {
        return [db = std::move(db), mode, fs = std::move(fs)]() mutable {
            if (fs) {
                return std::make_shared<connection>(db, mode, fs);
            }
            return std::make_shared<connection>(db, mode);
        };
    }

    connection_pool::lease connection_pool::acquire() {
        std::shared_ptr<connection> conn;
        bool needs_creation = false;
        auto state = state_;

        {
            std::unique_lock<std::mutex> lock(state->mutex);
            while (true) {
                if (!state->idle.empty()) {
                    conn = std::move(state->idle.back());
                    state->idle.pop_back();
                    break;
                }
                if (state->created < state->capacity) {
                    ++state->created;
                    needs_creation = true;
                    break;
                }
                state->cv.wait(lock);
            }
        }

        if (needs_creation) {
            try {
                conn = state->factory();
            } catch (...) {
                std::lock_guard<std::mutex> guard(state->mutex);
                --state->created;
                state->cv.notify_one();
                throw;
            }
        }

        return lease(this, std::move(conn));
    }

    void connection_pool::release(std::shared_ptr<connection> conn) {
        state_->release(std::move(conn));
    }

    std::size_t connection_pool::capacity() const {
        return state_->capacity;
    }

    std::size_t connection_pool::idle_count() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->idle.size();
    }

    std::size_t connection_pool::created_count() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->created;
    }

} // namespace v2
} // namespace sqlite
