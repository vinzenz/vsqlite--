#include <sqlite/connection_pool.hpp>

#include <sqlite/database_exception.hpp>

namespace sqlite {

    connection_pool::lease::lease(connection_pool * pool, std::shared_ptr<connection> conn)
        : pool_(pool)
        , connection_(std::move(conn)) {}

    connection_pool::lease::lease(lease && other) noexcept {
        pool_ = other.pool_;
        connection_ = std::move(other.connection_);
        other.pool_ = nullptr;
    }

    connection_pool::lease & connection_pool::lease::operator=(lease && other) noexcept {
        if(this != &other) {
            release();
            pool_ = other.pool_;
            connection_ = std::move(other.connection_);
            other.pool_ = nullptr;
        }
        return *this;
    }

    connection_pool::lease::~lease() {
        release();
    }

    void connection_pool::lease::release() {
        if(pool_ && connection_) {
            pool_->release(std::move(connection_));
        }
        pool_ = nullptr;
    }

    connection & connection_pool::lease::operator*() const {
        return *connection_;
    }

    connection * connection_pool::lease::operator->() const {
        return connection_.get();
    }

    std::shared_ptr<connection> connection_pool::lease::shared() const {
        return connection_;
    }

    connection_pool::connection_pool(std::size_t capacity, connection_factory factory)
        : factory_(std::move(factory))
        , capacity_(capacity) {
        if(capacity_ == 0) {
            throw database_exception("connection_pool capacity must be greater than zero");
        }
        if(!factory_) {
            throw database_exception("connection_pool requires a valid factory");
        }
    }

    connection_pool::connection_factory connection_pool::make_factory(std::string db,
                                                                      open_mode mode,
                                                                      filesystem_adapter_ptr fs) {
        return [db = std::move(db), mode, fs = std::move(fs)]() mutable {
            if(fs) {
                return std::make_shared<connection>(db, mode, fs);
            }
            return std::make_shared<connection>(db, mode);
        };
    }

    connection_pool::lease connection_pool::acquire() {
        std::shared_ptr<connection> conn;
        bool needs_creation = false;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            while(true) {
                if(!idle_.empty()) {
                    conn = std::move(idle_.back());
                    idle_.pop_back();
                    break;
                }
                if(created_ < capacity_) {
                    ++created_;
                    needs_creation = true;
                    break;
                }
                cv_.wait(lock);
            }
        }

        if(needs_creation) {
            try {
                conn = factory_();
            }
            catch(...) {
                std::lock_guard<std::mutex> guard(mutex_);
                --created_;
                cv_.notify_one();
                throw;
            }
        }

        return lease(this, std::move(conn));
    }

    void connection_pool::release(std::shared_ptr<connection> conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.push_back(std::move(conn));
        cv_.notify_one();
    }

    std::size_t connection_pool::capacity() const {
        return capacity_;
    }

    std::size_t connection_pool::idle_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_.size();
    }

    std::size_t connection_pool::created_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return created_;
    }

}
