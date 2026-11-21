#pragma once

#include "aiws/chunking_strategy.hpp"
#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <QMetaType>
#include <cstdint>
#include <memory>
#include <thread>

namespace aiws::gui {

// makes the chunker for one build and is called on the worker thread
using ChunkerFactory = std::unique_ptr<ChunkingStrategy> (*)(
    int delay_ms, std::uint64_t generation, const std::atomic<std::uint64_t>& wanted);

// one build request going from the GUI thread to the worker
// everything is a copy made on the GUI thread, so once it is in the mailbox the GUI doesn't touch it again
struct BuildJob {
    std::uint64_t generation{0}; // this build's ticket
    Workspace snapshot; // copy of workspace_ when Build is clicked
    int delay_ms{0}; // --build-delay-ms when Build is clicked
    ChunkerFactory make_chunker{nullptr}; 
};

// finished build going from the worker thread back to the GUI thread,
// carried by queued signal
// Qt copis the signal arguments into the GUI thread's event queue, and coprus moves by shared_ptr
struct BuiltCorpus {
    std::uint64_t generation{0};
    std::shared_ptr<ProcessingCore> core;
    std::thread::id built_on;  // which thread ran the build (R1 test evidence)
};

} // namespace aiws::gui

Q_DECLARE_METATYPE(aiws::gui::BuiltCorpus)