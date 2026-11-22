#include "aiws/delayed_chunker.hpp"
#include "aiws/processing_core.hpp"

#include <chrono>
#include <thread>

namespace aiws {

DelayedChunker::DelayedChunker(int delay_ms, std::uint64_t generation, const std::atomic<std::uint64_t>& wanted)
    : chunker_(ChunkingPolicy{ProcessingCore::kMaxChunkTokens, ProcessingCore::kChunkOverlap, ProcessingCore::kParagraphPreferenceWindow}),
      delay_ms_(delay_ms), generation_(generation), wanted_(wanted) {}

// in the worker thread
std::vector<Chunk> DelayedChunker::chunk(const Document& document, std::size_t document_order) const {
#ifdef AIWS_FAULT_NO_CANCEL
    // Fault R3: never checks wanted, so an unwanted build runs every document to the end
    // the next build waits behind it in the mailbox
#else
    // 10 ms slices checking a flag removed since no polling allowed
    if (wanted_.load() != generation_) throw BuildCancelled{};
#endif
    if (delay_ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    }
    return chunker_.chunk(document, document_order);
}

} // namespace aiws