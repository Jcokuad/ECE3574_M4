#pragma once

#include "aiws/chunker.hpp"
#include "aiws/chunking_strategy.hpp"

#include <atomic>
#include <memory>
#include <stdexcept>

namespace aiws {
// Thrown on the worker thread when a build is cancelled partway through a document
class BuildCancelled : public std::runtime_error {
public:
    BuildCancelled() : std::runtime_error("Build cancelled") {}
};

// Chunks after waiting the delay_ms for each document in the workspace
// runs on the worker thread and checks here for the cancel flag
class DelayedChunker final : public ChunkingStrategy {
public:
    DelayedChunker(int delay_ms, std::shared_ptr<const std::atomic<bool>> cancel_flag);
    std::vector<Chunk> chunk(const Document& document, std::size_t document_order) const override;

private:
    Chunker chunker_;
    int delay_ms_;
    std::shared_ptr<const std::atomic<bool>> cancel_flag_;
};

} // namespace aiws