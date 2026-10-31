// Bounded worker pool for independent index-based work (Task D).
#pragma once
#include <cstddef>
#include <functional>

namespace wb {

// Runs fn(i) for every i in [0, count) on `workers` threads. Work is handed out through a shared
// atomic counter, so each index runs exactly once. If `stop` is set and returns true, remaining
// indices are skipped and the function returns false. The first exception thrown by fn is rethrown
// after all workers have joined. workers == 1 still uses one separate worker thread.
bool parallelFor(std::size_t count, int workers, const std::function<void(std::size_t)>& fn,
                 const std::function<bool()>& stop = {});

}  // namespace wb
