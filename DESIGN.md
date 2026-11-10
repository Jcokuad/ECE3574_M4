# M4 Decision Record

## Section (1) Threads and ownership, including the statement for constraint 5
GUI thread has MainWindow, all widgets, SearchResultModel, and every public function of BuildRunner, which includes start, busy, cancel, take_result, shutdown, on_worker_done. Worker thread has BuildRunner::run() and the DelayedChunker it calls.

BuildRunner owns only one thread (worker_) at a time, and start() refuses to allow another one to begin while worker_ is joinable. The run() function is a static function that receives a Workspace snapshot, the promise, its own chunking strategy and the generation. The completed ProcessingCore or the exception goes through std::promise<ProcessingCore> to std::future<ProcessingCore> result_.run() then posts it on on_worker_done() so the GUI only calls get() once the value is ready, which prevents blocking.

DelayedChunker wraps the M1 Chunker and is installed through M2's three-argument ProcessingCore constructor, so the real build path is slowed. 

The shared mutable data part of constraint 5:
- The promise and future shared state
- The cancel flag
Everything else crosses threads by value, such as the Workspace snapshot, the delay, and the generation.

Lifecycle:
- MainWindow::~MainWindow() calls build_runner_->shutdown() first. It sets the cancel flag first then joins. 
- Reset_application() replaces processing_core_ with ProcessingCore{} instead of M3's rebuild on the BUI thread
- Repeated builds change with at most one thread existsing and none accumulating. BuildRunner::start() refuses while busy.

Ownership Path of a Corpus:
1) run(), where worker constructs a local ProcessingCore
2) rebuild(snapshot), where promise.set_value(std::move(core)) moves it to the promise/future shared state
3) on_build_finished(), where the GUI calls take_result() to moves it out with future.get()
4) Valid check
5) Accepted corpus lives until the next accepted build

IPC thread (CR-1): serve() and handle_client() run on IpcServer's own
std::thread. MainWindow owns the IpcServer with a unique_ptr, and start()
and stop() run on the GUI thread. ~MainWindow shuts down the build thread
first, then stops the server, so both threads are joined before anything
gets destroyed and nothing can be posted to a destroyed MainWindow. Sockets
are held by UniqueFd, which closes them automatically.

Shared data for CR-1:
- stop_ is a std::atomic<bool>. The GUI thread sets it in stop() and the
  IPC thread checks it at least every 100 ms.
- receiver_, socket_path_, and listen_fd_ are only set before the thread
  starts and only cleaned up after it is joined, so the two threads never
  touch them at the same time.
- The path sent to the GUI is a QString copied into Qt's event queue, which
  is thread safe.

## Section (2) How a finished build is judged valid
MainWindow has enum CorpusState corpus_state_ that has NotBuilt, Building, Built, and Stale instead of M3's bool corpus_current_. The previous bool could not say if the build is running or if a running build is out of date. The variable quint64 build_generation_ is a GUI thread only counter.

MainWindow::on_build_finished(generation) is the only place a corpus can become current. This is done by first computing valid by comparing generation ot build_generation, then if it is valid and take_result() returns, processing_core_ is move assigned, the state becomes current, and the chunk count is shown. If it is not valid, the result or exception is discarded and nothing changes. Build Corpus is disabled while BuildRunner::busy() so only one worker_ exists at a time.

## Section (3) Changes since the checkpoint
- bool corpus_current_ was replaced with enum corpus_state.
- add_document_file() returns false and makes no changes if a file is unreadable and/or already in the workspace. The absolute path used is the doc ID, so a second one would cause rebuild() to throw on a duplicate ID. add_document is now only the dialog and cancelling it doesn't do anything.
- set_build_delay_ms() forces negative values to 0. Running builds keep the same delay it started with. main.cpp rejects non-numeric or negative delay inputs.

## Section (4) CR-1 impact, meaning which functions changed and why the rest did not 
Request: a 4 byte big endian length N, then N bytes: 1 opcode byte (1 means
add) followed by the UTF-8 path. Length prefixing makes spaces, newlines,
and non ASCII bytes safe (E2) and lets the server wait for exactly N bytes
(E3). N has to be between 1 and 64 KiB, so a length of 0, a garbage length,
an unknown opcode, or a relative path gets rejected.

Reply: 1 status byte (0 ok, 1 rejected), a 4 byte big endian length M, then
M bytes of UTF-8 reason text.

The byte format lives in ipc_protocol.hpp. It has no Qt in it, so
IpcServer, aiws_send, and the tests all share it. Big endian numbers are
built by shifting bytes, not by memcpy.

aiws_send turns a relative path into an absolute one using its own working
directory (E1). Exit codes: 0 received and readable, 1 rejected (reason
printed to stderr), 2 no application within 2 s, a failure within 5 s, or
bad usage.

Server: a new IpcServer class with its own std::thread. It uses poll() with
100 ms timeouts and checks an atomic stop flag, so it can stop well inside
500 ms. The thread checks that the file is readable with POSIX calls,
replies, and then posts add_document_file to the GUI thread with a queued
invokeMethod (through the remote_add_requested slot). The reply never waits
for the GUI thread (constraint 3).

What changed: IpcServer, start_ipc_server(), remote_add_requested(), the
--ipc-socket option in main.cpp, aiws_send, and ~MainWindow now also stops
the server.
What did not change: build_corpus(), on_build_finished(), BuildRunner, and
the generation check. A remote add goes through add_document_file() on the
GUI thread exactly like Add Document does, so it invalidates the corpus and
bumps build_generation_ the same way. A remote add during a build still
makes that build stale (R2).

Shutdown: ~MainWindow stops and joins the server and then removes the
socket file (E4). A client stuck halfway through a request can't hold it up
because every read checks the stop flag. SIGINT and SIGTERM just set a flag
that main.cpp checks every 100 ms, then quit normally, so ~MainWindow still
runs and the socket file still gets removed.

Limits: one client at a time, so a stalled client can hold the next one for
up to 5 s. start() removes an old socket file before bind(), so a second
copy of the app on the same path takes it over.

## Section (5) Where each fault switch is and which test catches it
AIWS_FAULT_ACCEPT_STALE
It was placed in MainWindow::on_build_finished() in the line that computes valid. The defect makes it so that valid is always true, which would make outdated builds still accepted after Add Document or Reset. It's caught by my test called discards_results_add_during_build(), and the corpus label shows a chunk count or "Build failed" instead of the line "Out of date (rebuild required)". 

AIWS_FAULT_LOSE_FAILURE
It was placed in the catch block of BuildRunner::run(). The defect makes it so that after set_exception(), run() returns without posting on_worker_done(), so the GUI never leaves Building. It was caught by my test called runner_throws_failure().

## Section (6) AI-use disclosure 
AI was used for assiting with planning the CR-1 design, Implementing thorough Test Cases, and gaining a better understanding of how all the pieces fit together. AI was also used to improve the formatting of this Design.md so it is more readable and covers the planning phase that it was used for. 

## Pair Programming log

Partner: Simon Azumah

Session 1: We started on 11/06/26 from 7:00pm to 7:45pm in person, working on Simon's code. We talked about what approach we should take for CR1 and decided who would go first as the developer. We generated ideas using the CR-1 plan, covering the message format, the server designm, and the shutdown design, which covers steps 7a to 7d. 
Session 2: On 11/06/26 from 8:30pm to 9:00pm, we worked on my code and worked out how to adapt the steps we made into the format of my implementation.

Role Swaps: Simon and I took turns talking about our plan and code was not developed in the first session.

My contribution when I was navigator: I reviewed Simon's code and made two main observations:
1. In ~MainWindow, he reset build_job before the ipc_server which did not match the shutdown order in his DESIGN.md. I also noticed that if the server is stopped first, then no new remote add can arrive when a build is being cancelled or destroyed.
2. I also realized a difference between what the client was told and what actually happened. Even though the server replies with 'ok' after the readability check, the add itself doesn't run until later in the GUI thread. If add_document_file ever fails at that point, then that means the client was already told 'ok' and the status bar shows "Remote add failed".  

Simon's contribution when he was navigator: He reviewed my code and ensured I had the three things that CR1 depends on:
1. The Build Button: Simon ensured that my Build Corpus button was disabled when a build job exists, and not only when the state was building
2. The Threads: He checked if anything that runs off the main thread touched a widget directly, and if results returned through a queued call.
3. The Destructor: He checked if my destructor stops first and if a thread could outlive the objects it uses.

What worked best: When we met in person and worked through code togethger, it made development more efficient that if we did it offline. Also explaining our reasoning behind decisions we made also helped to keep each other accountable and helped us to get a better understanding of the project as we worked on it. 