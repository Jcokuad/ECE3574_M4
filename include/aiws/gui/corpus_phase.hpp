#pragma once

#include "aiws/gui/build_job.hpp"
#include "aiws/processing_core.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace aiws::gui {

class CorpusContext {
public:
    virtual ~CorpusContext() = default;

    // gives a copy of the workspace to the worker, then returns the build's generation.
    virtual std::uint64_t start_build() = 0;
    // tells the worker the running build is useless
    virtual void cancel_build() = 0;
    // installs a finished corpus; returns its chunk count
    virtual std::size_t accept_corpus(ProcessingCore core) = 0;
    // shows text on the status line
    virtual void show_message(const QString& text) = 0;
};

// state pattern
class CorpusPhase {
public:
    virtual ~CorpusPhase() = default;

    // events
    virtual std::unique_ptr<CorpusPhase> build_requested(CorpusContext& ctx);
    virtual std::unique_ptr<CorpusPhase> workspace_changed(CorpusContext& ctx);
    virtual std::unique_ptr<CorpusPhase> reset(CorpusContext& ctx);
    virtual std::unique_ptr<CorpusPhase> build_succeeded(CorpusContext& ctx, const BuiltCorpus& built);
    virtual std::unique_ptr<CorpusPhase> build_failed(CorpusContext& ctx, std::uint64_t generation, const QString& message);

    // used to enable buttons and show the corpus status
    virtual bool can_build() const = 0;
    virtual bool can_process() const = 0;  // search, inspect, build context
    virtual QString status_text() const = 0;
};

// startup and after a reset
class NotBuiltPhase final : public CorpusPhase {

public:
    std::unique_ptr<CorpusPhase> build_requested(CorpusContext& ctx) override;
    bool can_build() const override { 
        return true; 
    }
    bool can_process() const override { 
        return false; 
    }
    QString status_text() const override { 
        return "Corpus: Not Built"; 
    }
};

// a build of generation_ is running on the worker
class BuildingPhase final : public CorpusPhase {
    public:
    explicit BuildingPhase(std::uint64_t generation) : generation_(generation) {}

    std::unique_ptr<CorpusPhase> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> reset(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> build_succeeded(CorpusContext& ctx, const BuiltCorpus& built) override;
    std::unique_ptr<CorpusPhase> build_failed(CorpusContext& ctx, std::uint64_t generation, const QString& message) override;

    bool can_build() const override { 
        return false; // no build during a build
    }  
    bool can_process() const override { 
        return false; 
    }
    QString status_text() const override { return "Building..."; }

private:
    std::uint64_t generation_;
};

// corpus matches the workspace
class CurrentPhase final : public CorpusPhase {

public:
    explicit CurrentPhase(std::size_t chunks) : chunks_(chunks) {}

    std::unique_ptr<CorpusPhase> build_requested(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> reset(CorpusContext& ctx) override;

    bool can_build() const override { 
        return true; 
    }
    bool can_process() const override { 
        return true; 
    }
    QString status_text() const override { return QString("Corpus: %1 chunks").arg(chunks_); }

private:
    std::size_t chunks_;
};

// workspace changed after or during a build
class StalePhase final : public CorpusPhase {

public:
    std::unique_ptr<CorpusPhase> build_requested(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> reset(CorpusContext& ctx) override;

    bool can_build() const override { 
        return true; 
    }
    bool can_process() const override { 
        return false; 
    }
    QString status_text() const override { return "Corpus: Old Corpus (rebuild required)"; }
};

// the last build threw
class FailedPhase final : public CorpusPhase {

public:
    std::unique_ptr<CorpusPhase> build_requested(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusPhase> reset(CorpusContext& ctx) override;

    bool can_build() const override { 
        return true; 
    }
    bool can_process() const override { 
        return false; 
    }
    QString status_text() const override { return "Build has failed"; }
};

} // namespace aiws::gui