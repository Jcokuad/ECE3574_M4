#include "aiws/gui/build_mailbox.hpp"

#include <utility>

namespace aiws::gui {


void BuildMailbox::post(BuildJob job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        slot_ = std::move(job); // newest wind
    }
    ready_.notify_one(); // after unlocking, so the worker can take the lock immediately
}

bool BuildMailbox::wait_and_take(BuildJob& out) {
    std::unique_lock<std::mutex> lock(mutex_);  // unique_lock because wait() unlocks and relocks it
    
    while (!stopped_ && !slot_) {
        ready_.wait(lock);  // asleep, not polling
        // while (not if) handles spurious wakeups
    }

    if (stopped_) {
        return false;
    }

    out = std::move(*slot_);
    slot_.reset();
    return true;
}

void BuildMailbox::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
        slot_.reset(); // nontaken request is never built
    } // lock_guard is destoryed here

    ready_.notify_one();
}

} // namespace aiws::gui