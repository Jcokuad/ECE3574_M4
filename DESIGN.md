# M4 Decision Record

## Section (1) Threads and ownership, including the statement for constraint 5
GUI thread has MainWindow, all widgets, SearchResultModel, and every public function of BuildRunner, which includes start, busy, cancel, take_result, shutdown, on_worker_done. Worker thread has BuildRunner::run() and the DelayedChunker it calls.

BuildRunner owns only one thread (worker_) at a time, and start() refuses to allow another one to begin while worker_ is joinable. The run() function is a static function that receives a Workspace snapshot, the promise, its own chunking strategy and the generation. The completed ProcessingCore or the exception goes through std::promise<ProcessingCore> to std::future<ProcessingCore> result_.run() then posts it on on_worker_done() so the GUI only calls get() once the value is ready, which prevents blocking.

DelayedChunker wraps the M1 Chunker and is installed through M2's three-argument ProcessingCore constructor, so the real build path is slowed. 

The shared mutable data part of constraint 5:
- The promise and future shared state
- The cancel flag
Everything else crosses threads by value, such as the Workspace snapshot, the delay, and the generation.

## Section (2) How a finished build is judged valid
MainWindow has enum CorpusState corpus_state_ that has NotBuilt, Building, Built, and Stale instead of M3's bool corpus_current_. The previous bool could not say if the build is running or if a running build is out of date. The variable quint64 build_generation_ is a GUI thread only counter.

MainWindow::on_build_finished(generation) is the only place a corpus can become current. This is done by first computing valid by comparing generation ot build_generation, then if it is valid and take_result() returns, processing_core_ is move assigned, the state becomes current, and the chunk count is shown. If it is not valid, the result or exception is discarded and nothing changes. Build Corpus is disabled while BuildRunner::busy() so only one worker_ exists at a time.

## Section (3) Changes since the checkpoint
- bool corpus_current_ was replaced with enum corpus_state.
- add_document_file() returns false and makes no changes if a file is unreadable and/or already in the workspace. The absolute path used is the doc ID, so a second one would cause rebuild() to throw on a duplicate ID. add_document is now only the dialog and cancelling it doesn't do anything.
- set_build_delay_ms() forces negative values to 0. Running builds keep the same delay it started with. main.cpp rejects non-numeric or negative delay inputs.

## Section (4) CR-1 impact, meaning which functions changed and why the rest did not 

## Section (5) Where each fault switch is and which test catches it

## Section (6) AI-use disclosure 
