#include "aiws/gui/corpus_phase.hpp"

#include <utility>

namespace aiws::gui {

// CorpusPhase default is to ignroe the event and stay in the current state

std::unique_ptr<CorpusPhase> CorpusPhase::build_requested(CorpusContext&) { return nullptr; }
std::unique_ptr<CorpusPhase> CorpusPhase::workspace_changed(CorpusContext&) { return nullptr; }
std::unique_ptr<CorpusPhase> CorpusPhase::reset(CorpusContext&) { return nullptr; }
std::unique_ptr<CorpusPhase> CorpusPhase::build_succeeded(CorpusContext&, const BuiltCorpus&) { return nullptr; }
std::unique_ptr<CorpusPhase> CorpusPhase::build_failed(CorpusContext&, std::uint64_t, const QString&) { return nullptr; }

// NotBuilt
std::unique_ptr<CorpusPhase> NotBuiltPhase::build_requested(CorpusContext& ctx) {
    return std::make_unique<BuildingPhase>(ctx.start_build());
}

// Building, Build is disabled here
std::unique_ptr<CorpusPhase> BuildingPhase::workspace_changed(CorpusContext& ctx) {
    ctx.cancel_build(); // currently running build does not match the workspace anymore
    return std::make_unique<StalePhase>();
}

std::unique_ptr<CorpusPhase> BuildingPhase::reset(CorpusContext& ctx) {
    ctx.cancel_build();
    return std::make_unique<NotBuiltPhase>();
}

std::unique_ptr<CorpusPhase> BuildingPhase::build_succeeded(CorpusContext& ctx, const BuiltCorpus& built) {
    // only this state's own build counts
    if (built.generation != generation_) {
        return nullptr;
    }

    const std::size_t chunks= ctx.accept_corpus(std::move(*built.core));
    return std::make_unique<CurrentPhase>(chunks);
}

std::unique_ptr<CorpusPhase> BuildingPhase::build_failed(CorpusContext& ctx, std::uint64_t generation, const QString& message) {
    if (generation != generation_) {
        return nullptr;
    }

    ctx.show_message(QString("Build has failed: %1").arg(message));
    return std::make_unique<FailedPhase>();
}

// Current
std::unique_ptr<CorpusPhase> CurrentPhase::build_requested(CorpusContext& ctx) {
    return std::make_unique<BuildingPhase>(ctx.start_build());
}

std::unique_ptr<CorpusPhase> CurrentPhase::workspace_changed(CorpusContext&) {
    return std::make_unique<StalePhase>();
}

std::unique_ptr<CorpusPhase> CurrentPhase::reset(CorpusContext&) {
    return std::make_unique<NotBuiltPhase>();
}

// Stale
std::unique_ptr<CorpusPhase> StalePhase::build_requested(CorpusContext& ctx) {
    // build is allowed immediately and the worker takes the request within one docs time
    return std::make_unique<BuildingPhase>(ctx.start_build());
}

std::unique_ptr<CorpusPhase> StalePhase::reset(CorpusContext&) {
    return std::make_unique<NotBuiltPhase>();
}

// Failed
std::unique_ptr<CorpusPhase> FailedPhase::build_requested(CorpusContext& ctx) {
    return std::make_unique<BuildingPhase>(ctx.start_build());
}

std::unique_ptr<CorpusPhase> FailedPhase::workspace_changed(CorpusContext&) {
    return std::make_unique<StalePhase>();
}

std::unique_ptr<CorpusPhase> FailedPhase::reset(CorpusContext&) {
    return std::make_unique<NotBuiltPhase>();
}

} //namespace aiws::gui