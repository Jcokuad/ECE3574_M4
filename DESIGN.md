# M5 Decision Record

## Section (1)  M4 fixes:


## Section (2)  Threads and shared data:
Three threads. GUI thread: widgets, MainWindow, all corpus states. Build thread: one std::thread owned by BuildWorker, started in its constructor and joined in stop(), runs every build (R1). IPC thread from CR-1. The GUI and build thread share only two things: BuildMailbox (std::mutex + std::condition_variable) holds at most one BuildJob, newest wins; the GUI posts and the worker sleeps in wait_and_take() until there is a job or stop. The lock is only held to move the job in or out, never during a build, so the GUI never waits (R2). wanted_ (std::atomic<uint64_t>) is the generation still wanted, 0 = none; request() sets it, cancel() and stop() set 0, and DelayedChunker checks it before every document. The job holds a copy of workspace_ made on the GUI thread, and results come back by queued signal, so neither one is shared.

Shared data: the mailbox and wanted_. It cannot be smaller: the worker needs some way to get a request while asleep (mailbox, needs a cv to wait without polling), and a running build needs some way to learn mid-build that it is useless (wanted_), which the mailbox cannot do because the worker is not reading it during a build.

## Section (3) States:
Mainwindow is the context, phase_ holds the current CorpusPhase, and every event calls transition(phase_->event(*this)), which returns the next state or nullptr to stay. NotBuilt, Current, Stale, and Failed all start a build when Build is clicked in the window. Building cancels then goes stale on a change or NotBuilt on reset, goes to Current or Failed on its own result, and ignores results from any other generation. Current and Failed go to stale on a change, while every other state goes to NotBuilt on a reset.

## Section (4)  One event sequence that threatens R1 to R4, traced step by step through your functions on both threads.

add_document_file() calls Building::workspace_changed(), which calls cancel_build() so
wanted_ becomes 0, and the state goes to Stale. On the worker, the current document's sleep
ends, DelayedChunker::chunk() sees wanted_ doesn't match its generation and throws
BuildCancelled, and build() emits nothing. If the last document was already done, the
queued result is ignored by Stale or a newer Building, so it never becomes current.


## Section (5) Three Decisions:
1. Build is off during a build since BuildingPhase::can_build() is false. After a change
cancels a build, Stale allows Build right away and request() sets wanted_ so the old build
stops within one document.

2. remote_add_requested() calls add_document_file(), the same event as a local add, so the
build is cancelled and the corpus shows "Corpus: Old Corpus (rebuild required)" with no
automatic rebuild.

## Section (6) Fault Switches:
AIWS_FAULT_NO_CANCEL skips the wanted_ check in DelayedChunker::chunk(), so the useless
build runs every document and the next one waits behind it, caught by reset_mid_build_does_not_delay_next_build and shutdown_mid_build_is_prompt AIWS_FAULT_SILENT_SHUTDOWN skips notify_one() in BuildMailbox::stop(), so an idle worker
never wakes and Exit hangs on join, caught by idle_shutdown_is_prompt, which runs first and
exits after 2 s.

## Section (7) AI-use disclosure 
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