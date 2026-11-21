#pragma once 

#include "aiws/gui/build_job.hpp"

#include <condition_variable>
#include <optional>
#include <mutex>

namespace aiws::gui {

// Lecture 22 TreadSafeQueue using one slot
class BuildMailbox {
public:

    void post(BuildJob job); // puts a job in the slot and wakes the worker
    // sleeps on the condition variable until a job is posted or stop() is called
    bool wait_and_take(BuildJob& out); // worker thread
    // makes every wait_and_take() return false and wakes the worker if it is asleep
    void stop();

private:
    std::mutex mutex_;
    std::condition_variable ready_;  // "the slot has a job, or we are stopping"
    std::optional<BuildJob> slot_;   // guarded by mutex_
    bool stopped_{false};            // guarded by mutex_
};
} // namespace aiws::gui