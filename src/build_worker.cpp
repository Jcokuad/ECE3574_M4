#include "aiws/gui/build_worker.hpp"

#include "aiws/context_builder.hpp"
#include "aiws/delayed_chunker.hpp"
#include "aiws/retrieval_engine.hpp"

#include <exception>
#include <utility>

namespace aiws::gui {

std::unique_ptr<ChunkingStrategy> make_delayed_chunker(
    int delay_ms, std::uint64_t generation, const std::atomic<std::uint64_t>& wanted) {
        return std::make_unique<DelayedChunker>(delay_ms, generation, wanted);
}

BuildWorker::BuildWorker(QObject* parent) : QObject(parent) {
    qRegisterMetaType<BuiltCorpus>();  // so a queued signal can copy it into the event queue
    thread_ = std::thread(&BuildWorker::run, this); // only thread created by this class
}

BuildWorker::~BuildWorker() { stop(); }

std::uint64_t BuildWorker::request(Workspace snapshot, int delay_ms) {
    const std::uint64_t generation = ++last_generation_; // sets first ticket as 1, so 0 is saved for none wanted
    wanted_.store(generation);  // first, so a running older build sees it is unwanted
    mailbox_.post(BuildJob{generation, std::move(snapshot), delay_ms, chunker_factory_});
    return generation;
}

void BuildWorker::cancel() { wanted_.store(0); }

void BuildWorker::stop() {
    wanted_.store(0);  // a running build stops at its next document
    mailbox_.stop();   // a sleeping worker wakes and its loop ends
    if (thread_.joinable()) thread_.join();
}

void BuildWorker::set_chunker_factory(ChunkerFactory factory) noexcept {
    chunker_factory_ = factory;
}

std::thread::id BuildWorker::thread_id() const noexcept { 
    return thread_.get_id(); 
}

// Worker thread.
void BuildWorker::run() {
    BuildJob job;
    while (mailbox_.wait_and_take(job)) {
        if (job.generation != wanted_.load()) continue;  // never built
        build(job);
    }
}

// Worker thread. Touches only job, its own copy, and wanted_, which is atomic
void BuildWorker::build(BuildJob& job) {
    try {
        ProcessingCore core(job.make_chunker(job.delay_ms, job.generation, wanted_), std::make_unique<RetrievalEngine>(), 
                                std::make_unique<ContextBuilder>());
        core.rebuild(job.snapshot);
        emit build_succeeded(BuiltCorpus{job.generation, std::make_shared<ProcessingCore>(std::move(core)), std::this_thread::get_id()});
    } catch (const BuildCancelled&) {
        // useless build with nothing to report, and it never becomes current
    } catch (const std::exception& error) {
        emit build_failed(job.generation, QString::fromUtf8(error.what()));
    }
}

}  // namespace aiws::gui