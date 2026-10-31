#include "jobs.h"

namespace wb {

std::string JobManager::submit(JobFunction fn) {
    auto job = std::make_shared<Job>();
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mu_);
        id = "job" + std::to_string(next_id_++);
        jobs_[id] = job;
    }
    Job* raw = job.get();  // jobs_ keeps the Job alive until the manager joins the thread
    job->worker = std::thread([raw, fn = std::move(fn)] {
        JobContext ctx(raw->cancel, raw->progress);
        JobState end = JobState::Done;
        std::string err;
        nlohmann::json result;
        try {
            result = fn(ctx);
        } catch (const JobCancelled&) {
            end = JobState::Cancelled;
        } catch (const std::exception& e) {
            end = JobState::Failed;
            err = e.what();
        }
        std::lock_guard<std::mutex> lock(raw->mu);  // single atomic commit
        if (end == JobState::Done) {
            raw->result = std::move(result);
            raw->progress.store(1.0);
        }
        raw->error = err;
        raw->state = end;
    });
    return id;
}

bool JobManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = jobs_.find(id);
    if (it == jobs_.end()) return false;
    it->second->cancel.store(true);
    return true;
}

bool JobManager::snapshot(const std::string& id, JobSnapshot& out) const {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = jobs_.find(id);
        if (it == jobs_.end()) return false;
        job = it->second;
    }
    std::lock_guard<std::mutex> lock(job->mu);
    out.state = job->state;
    out.progress = job->progress.load();
    out.error = job->error;
    if (job->state == JobState::Done) out.result = job->result;
    return true;
}

const char* JobManager::stateName(JobState s) {
    switch (s) {
        case JobState::Running: return "running";
        case JobState::Done: return "done";
        case JobState::Cancelled: return "cancelled";
        case JobState::Failed: return "failed";
    }
    return "unknown";
}

JobManager::~JobManager() {
    for (auto& kv : jobs_) kv.second->cancel.store(true);
    for (auto& kv : jobs_)
        if (kv.second->worker.joinable()) kv.second->worker.join();
}

}  // namespace wb
