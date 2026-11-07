# M4 Decision Record

## Section (1) Threads and ownership, including the statement for constraint 5

## Section (2) How a finished build is judged valid
MainWindow has enum CorpusState corpus_state_ that has NotBuilt, Building, Built, and Stale instead of M3's bool corpus_current_. The previous bool could not say if the build is running or if a running build is out of date. The variable quint64 build_generation_ is a GUI thread only counter.

## Section (3) Changes since the checkpoint
bool corpus_current_ was replaced with enum corpus_state.

## Section (4) CR-1 impact, meaning which functions changed and why the rest did not 

## Section (5) Where each fault switch is and which test catches it

## Section (6) AI-use disclosure 
