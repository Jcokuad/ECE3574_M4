#pragma once

#include "aiws/chunking_strategy.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <QObject>
#include <QtGlobal>

#include <atomic>
#include <memory>
#include <future>
#include <thread>

namespace aiws::gui {

// runs one build in a separate thread and hands the finished ProcessingCore back to the GUI thread when done
class BuildRunner final : public QObject {
    Q_OBJECT

public:
    // creates teh chunking strategy with the delay and the cancel flag for one build
    using ChunkerFactory = std::unique_ptr<ChunkingStrategy> (*)(
        int delay_ms, std::shared_ptr<const std::atomic<bool>> cancel_flag);
    explicit BuildRunner(QObject* parent = nullptr);
    ~BuildRunner() override; // shuts down the worker thread and waits for it to finish

    bool start(quint64 generation, const Workspace& snapshot, int build_delay_ms); // return false if a build is already running
    bool busy() const noexcept; // true if a build is running
    void cancel() noexcept;
    ProcessingCore take_result(); // the built core is returned or rethrows the worker's exception
    void shutdown();
    void set_chunker_factory(ChunkerFactory factory) noexcept;

signals:
    void build_finished(quint64 generation);

private slots:
    void on_worker_done(quint64 generation);

private:
    static void run (BuildRunner* notify, std::promise<ProcessingCore> promise, Workspace snapshot,
                     std::unique_ptr<ChunkingStrategy> chunker, quint64 generation);
    std::thread worker_;
    std::future<ProcessingCore> result_;
    std::shared_ptr<std::atomic<bool>> cancel_flag_;
    ChunkerFactory chunker_factory_;
};

} // namespace aiws::gui
