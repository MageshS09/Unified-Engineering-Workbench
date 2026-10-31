// Async job runner: each job runs on its own thread; the result is committed atomically only
// when the job completes, so cancelled or failed jobs never expose partial results.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "json.hpp"

namespace wb {

struct JobCancelled {};

class JobContext {
public:
    JobContext(const std::atomic<bool>& cancel, std::atomic<double>& progress) : cancel_(cancel), progress_(progress) {}
    bool cancelled() const { return cancel_.load(); }
    void throwIfCancelled() const {
        if (cancelled()) throw JobCancelled{};
    }
    void setProgress(double fraction) { progress_.store(fraction); }

private:
    const std::atomic<bool>& cancel_;
    std::atomic<double>& progress_;
};

enum class JobState { Running, Done, Cancelled, Failed };

struct JobSnapshot {
    JobState state = JobState::Running;
    double progress = 0.0;
    std::string error;      // set when Failed
    nlohmann::json result;  // set when Done
};

using JobFunction = std::function<nlohmann::json(JobContext&)>;

class JobManager {
public:
    ~JobManager();
    std::string submit(JobFunction fn);
    bool cancel(const std::string& id);
    bool snapshot(const std::string& id, JobSnapshot& out) const;  // false if unknown id
    static const char* stateName(JobState s);

private:
    struct Job {
        std::atomic<bool> cancel{false};
        std::atomic<double> progress{0.0};
        mutable std::mutex mu;
        JobState state = JobState::Running;
        std::string error;
        nlohmann::json result;
        std::thread worker;
    };
    mutable std::mutex mu_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    int next_id_ = 1;
};

}  // namespace wb
