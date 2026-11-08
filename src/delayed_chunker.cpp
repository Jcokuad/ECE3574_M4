#include "aiws/delayed_chunker.hpp"
#include "aiws/processing_core.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace aiws {

namespace {
constexpr std::chrono::milliseconds kSlice{10}; // slice the delay into 10ms chunks to check for cancellation
}

DelayedChunker::DelayedChunker(int delay_ms, std::shared_ptr<const std::atomic<bool>> cancel_flag)
    : chunker_(ChunkingPolicy{ProcessingCore::kMaxChunkTokens, ProcessingCore::kChunkOverlap, ProcessingCore::kParagraphPreferenceWindow}),
      delay_ms_(delay_ms),
      cancel_flag_(std::move(cancel_flag)) {}

std::vector<Chunk> DelayedChunker::chunk(const Document& document, std::size_t document_order) const {
    // sleep in slices of 10ms to check for cancellation
    std::chrono::milliseconds remaining{delay_ms_};
    while (true) {
        if (cancel_flag_ && cancel_flag_->load()) {
            throw BuildCancelled{};
        }
        if (remaining <= std::chrono::milliseconds::zero()) {
            break;
        }
        const std::chrono::milliseconds sleep_duration = std::min(remaining, kSlice);
        std::this_thread::sleep_for(sleep_duration);
        remaining -= sleep_duration;
    }
    return chunker_.chunk(document, document_order);
}

} // namespace aiws