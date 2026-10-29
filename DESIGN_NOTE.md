# M4 Design Note

Due Wed Oct 28, 11:59 pm. See specification Section 7.

1. Your components and the thread each one's function runs on.

For my implementation in M3, the MainWindow::build_corpus() calls rebuild() on the GUI thread. In comparison to M4, it would move that to one worker thread based on my understanding from the specification. This then means that MainWindow would then decide if a completed build becomes the current corpus. 

MainWindow, is on the GUI thread, owns workspace_, processing_core_, corpus_state_, and build_generation_. All the widgets from M3 and SearchResultModel, which are also on the GUI thread, would stay the same. These show the building status and the button states. BuildRunner, which is added in M4 and is on the GUI thread, handles start(), cancel(), take_result(), shutdown(), on_worker_done(), and shutdown(). DelayedChunker, which was also added in M4 and is on the worker thread, wraps chunker and throws if the build is cancelled.  


2. Your corpus states and the events that move between them.

The states of the corpus include NotBuilt, Current, and Stale. The build_generation_ would be incremented by Build, Add Document, and Reset.

Build Corpus would only be enabled when the workspace has something there and no worker is currently acting, so only one thread exists at a time. The events that take place are Build Corpus, then Building Finished, then success, then Current Finished showing chunk and chunk count, then stale if failure, then add document, then reset, then notbuilt or finished. Reset can also happen during any state.


3. The event sequences that threaten R1 to R4 and how your design handles each, with two of them walked through step by step.

The event sequences that threaten R1 to R4 include add document or reset during a build in R2, the build being thrown in R3, an exit happending during a build in R4, and repeated builds happening in R4. Adding a document duing a build would cause the generation check to discrd the build. A build being thrown would cause a throw in the GUI thread, which would cause it to move to the Stale state. An exit happening duing a build would lead to the MainWindow calling a shutdown. Repeated builds are also an issue be cause each thread would be joined in on_worker_done() before the next build can start. 

Walkthrough: Adding a document during a build
1. the build would start at generation 6 for example
2. the user would then add a file, the generation becomes 7, the state would then become stale and the job is cancelled.
3. The worker finished and posts the finish on_worker_done(6)
4. On the GUI thread, the thread is joined and since the generation numbers done match, the result would be dropped and the old chunk count would not be shown.

Walkthrough: Exit during a build
1. The user clicks exit and yes, which causes exec() to return
2. Mainwindow calls shutdown, which sets the cancel flag
3. The Delayed chunker would then throw and nothing would be updated
4. The thread ends and join() returns


4. The ownership path of one built corpus, from creation to destruction.

In the ownership of a built corpus from start to finish, it starts with the worker creating a local ProcessingCore and setting a value. Then on the GUI thread, take_result() would then move it back with future.get(). After this if the generation is valid, an std::move(result) would take the ownership and delete the old corpus. Finally the new corpus would stay until the next accepted build.

5. One alternative you rejected, and why.
One alternative I rejected would be allowing the worker to rebuild the processing core in place. Since the GUI reads it during Search, it could cause a stale build and overwrite the current corpus before checks happen. Building privately is the best opion here since handling the result over by std::move() makes accepting easy and only done in a single GUI decision. 