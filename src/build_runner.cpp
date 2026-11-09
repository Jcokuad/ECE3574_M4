#include "aiws/gui/build_runner.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/delayed_chunker.hpp"
#include "aiws/retrieval_engine.hpp"

#include <QMetaObject>
#include <stdexcept>
#include <exception>
#include <utility>

namespace aiws::gui {

namespace {
std::unique_ptr<ChunkingStrategy> make_delayed_chunker(int delay_ms, std::shared_ptr<const std::atomic<bool>> cancel_flag) {
    return std::make_unique<DelayedChunker>(delay_ms, std::move(cancel_flag));
}
} // namespace

BuildRunner::BuildRunner(QObject* parent)
    : QObject(parent), chunker_factory_(&make_delayed_chunker) {}

BuildRunner::~BuildRunner() {
    shutdown();
}

bool BuildRunner::start(quint64 generation, const Workspace& snapshot, int build_delay_ms) {
    if (busy()) {
        return false; // already running
    }
    cancel_flag_ = std::make_shared<std::atomic<bool>>(false);
    std::promise<ProcessingCore> promise;
    result_ = promise.get_future();
    std::unique_ptr<ChunkingStrategy> chunker = chunker_factory_(build_delay_ms, cancel_flag_);
    
    // everything used by the worker is moved or copied in
    worker_ = std::thread(&BuildRunner::run, this, std::move(promise), std::move(snapshot),
                            std::move(chunker), generation);
    return true;
}

bool BuildRunner::busy() const noexcept { return worker_.joinable(); }

void BuildRunner::cancel() noexcept {
    if (cancel_flag_) cancel_flag_->store(true);
}

ProcessingCore BuildRunner::take_result() {
    if (!result_.valid()) throw std::logic_error("no build result available");
    return result_.get(); // already ready
}

void BuildRunner::shutdown() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void BuildRunner::set_chunker_factory(ChunkerFactory factory) noexcept {
    if (factory != nullptr) chunker_factory_ = factory;
}

void BuildRunner::on_worker_done(quint64 generation) {
    // GUI thread
    if (worker_.joinable()) worker_.join();
    emit build_finished(generation);
}

// worker thread
void BuildRunner::run(BuildRunner* notify, std::promise<ProcessingCore> promise, 
                        Workspace snapshot, std::unique_ptr<ChunkingStrategy> chunker, quint64 generation) {
    try {
        ProcessingCore core(std::move(chunker), std::make_unique<RetrievalEngine>(), std::make_unique<ContextBuilder>());
        core.rebuild(snapshot);
        promise.set_value(std::move(core));
    } catch (...) {
        promise.set_exception(std::current_exception());
#ifdef AIWS_FAULT_LOSE_FAILURE
        return; // FAULT R3, the failure never reaches the GUI, which stays in Building
#endif
    } 
    QMetaObject::invokeMethod(notify, "on_worker_done", Qt::QueuedConnection, Q_ARG(quint64, generation));
}

} // namespace aiws::gui
