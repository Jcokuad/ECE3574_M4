#pragma once

#include "aiws/gui/build_job.hpp"
#include "aiws/gui/build_mailbox.hpp"

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <cstdint>

namespace aiws::gui {

// default delayedchunker that sleeps delay_ms per doc and stops when wanted isn't equal to generation anymore
std::unique_ptr<ChunkingStrategy> make_delayed_chunker(int delay_ms, std::uint64_t generation, 
                                                            const std::atomic<std::uint64_t>& wanted);
 
// owns the one build thread from R1
class BuildWorker final : public QObject {
    Q_OBJECT

public:
    explicit BuildWorker(QObject* parent = nullptr);
    ~BuildWorker() override;  // stop()

    BuildWorker(const BuildWorker&) = delete;
    BuildWorker& operator=(const BuildWorker&) = delete;

    // hands a build of snapshot to the worker and returns immediately from R2
    std::uint64_t request(Workspace snapshot, int delay_ms);

    // no build is wanted any more and a running build stops at its next doc from R3
    void cancel();

    // cancels, wakes the worker, and joins it from R4 . Safe to call twice.
    void stop();

    // used for builds requested after this call
    void set_chunker_factory(ChunkerFactory factory) noexcept;

    // the build thread's id, the same from construction to stop() from R1
    std::thread::id thread_id() const noexcept;

signals:
    // emitted on the worker thread
    void build_succeeded(const aiws::gui::BuiltCorpus& built);
    void build_failed(quint64 generation, const QString& message);

private:
    void run(); // worker thread loop
    void build(BuildJob& job); // worker thread one build

    // GUI thread only
    std::uint64_t last_generation_{0};
    ChunkerFactory chunker_factory_{&make_delayed_chunker};

    // shared by both threads
    BuildMailbox mailbox_;
    std::atomic<std::uint64_t> wanted_{0};

    std::thread thread_; // starts after everything run() uses exists
};


} // namespace aiws::gui