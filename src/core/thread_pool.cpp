#include "thread_pool.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace wb {

bool parallelFor(std::size_t count, int workers, const std::function<void(std::size_t)>& fn,
                 const std::function<bool()>& stop) {
    if (workers < 1) workers = 1;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> aborted{false};
    std::exception_ptr first_error;
    std::mutex error_mu;

    auto worker = [&] {
        for (;;) {
            if (aborted.load(std::memory_order_relaxed)) return;
            if (stop && stop()) {
                aborted.store(true);
                return;
            }
            const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= count) return;
            try {
                fn(i);
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_mu);
                if (!first_error) first_error = std::current_exception();
                aborted.store(true);
                return;
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers));
    for (int w = 0; w < workers; ++w) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    if (first_error) std::rethrow_exception(first_error);
    return !aborted.load();
}

}  // namespace wb
