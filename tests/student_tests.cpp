// Your M4 tests (specification, Section 6).

#include "aiws/gui/main_window.hpp"
#include "aiws/gui/build_worker.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

namespace ipc = aiws::ipc;
using aiws::gui::BuildWorker;
using aiws::gui::BuiltCorpus;

QString write_file(const QTemporaryDir& dir, const QString& name, const QByteArray& content) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content);
    }
    return path;
}

QPushButton* button(QWidget& window, const QString& text) {
    const auto buttons = window.findChildren<QPushButton*>();
    for (QPushButton* b : buttons) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

QLabel* corpus_status(QWidget& w) { return w.findChild<QLabel*>("corpusStatusLabel"); }

QLineEdit* query_edit(QWidget& w) {
    for (QLineEdit* i : w.findChildren<QLineEdit*>()) {
        if (i->placeholderText() == "Enter a query") return i;
    }
    return nullptr;
}

aiws::Workspace docs(int n) {
    aiws::Workspace ws;
    for (int i = 0; i < n; i++) {
        const std::string id = "d" + std::to_string(i);
        ws.add_document(aiws::Document{id, id, "hello world number " + id});
    }
    return ws;
}

class FailingChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document&, std::size_t) const override {
        throw std::runtime_error("injected chunking failure");
    }
};

std::unique_ptr<aiws::ChunkingStrategy> make_failing_chunker(int, std::uint64_t, const std::atomic<std::uint64_t>&) {
    return std::make_unique<FailingChunker>();
}

// records which generations the worker actually started building
// factory runs on the worker thread

std::mutex started_mutex;
std::vector<std::uint64_t> started;
std::unique_ptr<aiws::ChunkingStrategy> make_recording_chunker(int delay_ms, std::uint64_t generation,
                                                               const std::atomic<std::uint64_t>& wanted) {
    {
        std::lock_guard<std::mutex> lock(started_mutex);
        started.push_back(generation);
    }
    return aiws::gui::make_delayed_chunker(delay_ms, generation, wanted);
}

bool has_started(std::uint64_t generation) {
    std::lock_guard<std::mutex> lock(started_mutex);
    return std::find(started.begin(), started.end(), generation) != started.end();
}

int exit_requests = 0;
void record_exit(QWidget*) {
    exit_requests++;
}

namespace ipc = aiws::ipc;

struct Reply { 
    int status{-1}; 
    std::string reason; 
};

Reply send_frame(const QString& socket, const std::string& frame, std::size_t piece = 4096, int pause_ms = 0) {
    Reply reply;
    sockaddr_un address {};
    if (!ipc::make_address(socket.toStdString(), address)) return reply;

    ipc::UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return reply;
    }

    const auto deadline = ipc::Clock::now() + std::chrono::seconds(5);
    for (std::size_t off = 0; off < frame.size(); off += piece) {
        const std::size_t n = std::min(piece, frame.size() - off);
        if (ipc::write_all(fd.get(), frame.data() + off, n, deadline) != ipc::IoResult::Ok) return reply;
        if (pause_ms > 0) QThread::msleep(pause_ms);
    }

    char header[5];
    if (ipc::read_exact(fd.get(), header, 5, deadline) != ipc::IoResult::Ok) return reply;
    
    reply.reason.resize(ipc::get_u32_be(header + 1));
    if (!reply.reason.empty()) ipc::read_exact(fd.get(), &reply.reason[0], reply.reason.size(), deadline);
    
    reply.status = static_cast<unsigned char>(header[0]);
    return reply;
}
}  // namespace

class StudentTests : public QObject {
    Q_OBJECT
private slots:
    // R4 first: under a shutdown bug every later window would hang when destroyed
    void idle_shutdown_is_prompt();
    void shutdown_mid_build_is_prompt();      // R4
    void one_worker_runs_every_build();       // R1
    void outdated_request_is_never_built();   // R2
    void worker_reports_failure();
    void click_hands_off_and_becomes_current(); // R2
    void reset_mid_build_does_not_delay_next_build(); // R3
    void add_mid_build_never_becomes_current(); // R3, decision 1
    void add_document_file_valid();
    void add_document_file_invalid();
    void exit_and_repeated_builds();
    void ipc_add_odd_path_in_pieces();        // E1, E2, E3
    void ipc_add_during_build_stale_corpus(); // E1 with decision 2
    void ipc_reject_bad_requests();
    void aiws_send_exit_codes();              // E1
    void ipc_stops_and_removes_socket();      // E4
};

// stop() an idle worker on a helper thread and give it 2 s. If it hangs, the
// helper can never be joined, so report and end the process here.
void StudentTests::idle_shutdown_is_prompt() {
    auto worker = std::make_unique<BuildWorker>();
    std::promise<void> done;
    std::future<void> stopped = done.get_future();
    std::thread helper([&worker, &done] {
        worker->stop();
        done.set_value();
    });

    if (stopped.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        std::fprintf(stderr, "FAIL: idle_shutdown_is_prompt: stop() with no build running hung (R4)\n");
        std::_Exit(1);
    }
    helper.join();
}

// 10 docs x 300 ms. Once the build is really running, Exit (the worker's
// destructor, which ~MainWindow also reaches) must not wait out the other 9.
void StudentTests::shutdown_mid_build_is_prompt() {
    {
        std::lock_guard<std::mutex> lock(started_mutex);
        started.clear();
    }

    auto worker = std::make_unique<BuildWorker>();
    worker->set_chunker_factory(&make_recording_chunker);
    const std::uint64_t g = worker->request(docs(10), 300);

    QTRY_VERIFY_WITH_TIMEOUT(has_started(g), 2000);
    QElapsedTimer timer;
    timer.start();
    worker.reset();  // cancel, wake, join
    QVERIFY2(timer.elapsed() < 900, "Exit waited for more than one document");
}

void StudentTests::one_worker_runs_every_build() {
    BuildWorker worker;
    const std::thread::id first = worker.thread_id();

    QVERIFY(first != std::this_thread::get_id());
    QSignalSpy done(&worker, &BuildWorker::build_succeeded);
    for (int i = 1; i <= 3; i++) {
        worker.request(docs(2), 0);
        QVERIFY(done.wait(2000));
        QCOMPARE(done.count(), i);
        const auto result = done.last().first().value<BuiltCorpus>();
        QVERIFY(result.built_on == first);  // the same thread, every time
        QCOMPARE(result.core->chunk_count(), std::size_t{2});
    }

    QVERIFY(worker.thread_id() == first);
}

// g1 is running (asleep on its first document). g2 and g3 are posted back to
// back, so g2 is already outdated when the worker next takes from the mailbox:
// it must never start, and only g3 may become a result.
void StudentTests::outdated_request_is_never_built() {
    {
        std::lock_guard<std::mutex> lock(started_mutex);
        started.clear();
    }

    BuildWorker worker;
    worker.set_chunker_factory(&make_recording_chunker);
    QSignalSpy done(&worker, &BuildWorker::build_succeeded);
    const std::uint64_t g1 = worker.request(docs(5), 200);

    QTRY_VERIFY_WITH_TIMEOUT(has_started(g1), 2000);
    const std::uint64_t g2 = worker.request(docs(5), 200);
    const std::uint64_t g3 = worker.request(docs(1), 0);

    QVERIFY(done.wait(2000));
    QTest::qWait(300);  // anything else would have arrived by now
    QCOMPARE(done.count(), 1);
    QCOMPARE(done.first().first().value<BuiltCorpus>().generation, g3);
    QVERIFY(!has_started(g2));
}

void StudentTests::worker_reports_failure() {
    BuildWorker worker;
    worker.set_chunker_factory(&make_failing_chunker);
    QSignalSpy failed(&worker, &BuildWorker::build_failed);
    const std::uint64_t g = worker.request(docs(2), 0);

    QVERIFY(failed.wait(2000));
    QCOMPARE(failed.first().at(0).value<quint64>(), quint64{g});
    QVERIFY(failed.first().at(1).toString().contains("injected"));
}

void StudentTests::click_hands_off_and_becomes_current() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(300);

    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QVERIFY(window.add_document_file(write_file(dir, "b.txt", "jorts shirt")));
    QPushButton* build = button(window, "Build Corpus");
    QElapsedTimer timer;
    timer.start();
    build->click();

    QVERIFY(timer.elapsed() < 200);  // the build takes 600 ms, the click did not wait for it
    QVERIFY(!build->isEnabled());
    QCOMPARE(corpus_status(window)->text(), QString("Building..."));
    QTRY_COMPARE_WITH_TIMEOUT(corpus_status(window)->text(), QString("Corpus: 2 chunks"), 2000);
    query_edit(window)->setText("hello");
    QVERIFY(button(window, "Search")->isEnabled());
}

// 10 docs x 200 ms. Reset mid-build, add one doc, build again: the new build
// must not wait behind the 2 s useless one.
void StudentTests::reset_mid_build_does_not_delay_next_build() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(200);
    for (int i = 0; i < 10; i++) {
        QVERIFY(window.add_document_file(write_file(dir, QString("%1.txt").arg(i), "hello world")));
    }

    QPushButton* build = button(window, "Build Corpus");
    build->click();
    QTest::qWait(50);
    button(window, "Reset")->click();

    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Not Built"));
    QVERIFY(window.add_document_file(write_file(dir, "new.txt", "jorts shirt")));
    QElapsedTimer timer;
    timer.start();
    build->click();
    // without cancellation this takes about 2 s; with it, about 0.4 s
    QTRY_COMPARE_WITH_TIMEOUT(corpus_status(window)->text(), QString("Corpus: 1 chunks"), 1000);
    QVERIFY(timer.elapsed() < 1000);
}

void StudentTests::add_mid_build_never_becomes_current() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(200);

    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QPushButton* build = button(window, "Build Corpus");
    build->click();

    QVERIFY(window.add_document_file(write_file(dir, "b.txt", "jorts shirt")));
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
    QVERIFY(build->isEnabled());  // decision 1: rebuild allowed right away
    QTest::qWait(500);            // the cancelled build would have finished by now
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
    query_edit(window)->setText("hello");
    QVERIFY(!button(window, "Search")->isEnabled());
}

void StudentTests::add_document_file_valid() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;

    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "Hello World")));
    QCOMPARE(window.findChild<QListWidget*>()->count(), 1);
    QVERIFY(button(window, "Build Corpus")->isEnabled());
}

void StudentTests::add_document_file_invalid() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;

    QVERIFY(!window.add_document_file(dir.filePath("missing.txt")));
    QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
    QVERIFY(!button(window, "Build Corpus")->isEnabled());
}

void StudentTests::exit_and_repeated_builds() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_exit_callback(&record_exit);

    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QPushButton* build = button(window, "Build Corpus");
    for (int i = 0; i < 5; i++) {  // same worker thread every time (R1)
        build->click();
        QTRY_COMPARE_WITH_TIMEOUT(corpus_status(window)->text(), QString("Corpus: 1 chunks"), 2000);
    }

    exit_requests = 0;
    build->click();
    button(window, "Exit")->click();  // exit during a build asks the callback
    QCOMPARE(exit_requests, 1);
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 2000);
}

void StudentTests::ipc_add_odd_path_in_pieces() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    const QString socket = dir.filePath("s.sock");

    QVERIFY(window.start_ipc_server(socket));
    const QString path = write_file(dir, QString::fromUtf8("my file é\nline.txt"), "hello world");
    // 3 bytes at a time, 20 ms apart; the reply arriving while this thread
    // sleeps proves the server never waited for the GUI thread
    const Reply reply = send_frame(socket, ipc::encode_request(ipc::kOpAdd, path.toStdString()), 3, 20);
    QCOMPARE(reply.status, 0);
    QListWidget* list = window.findChild<QListWidget*>();
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QString::fromUtf8("my file é\nline.txt"));
}

void StudentTests::ipc_add_during_build_stale_corpus() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    const QString socket = dir.filePath("s.sock");

    QVERIFY(window.start_ipc_server(socket));
    window.set_build_delay_ms(200);
    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    button(window, "Build Corpus")->click();
    const QString b = write_file(dir, "b.txt", "jorts shirt");
    QCOMPARE(send_frame(socket, ipc::encode_request(ipc::kOpAdd, b.toStdString())).status, 0);
    QTRY_COMPARE(window.findChild<QListWidget*>()->count(), 2);
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
    QTest::qWait(400);  // the cancelled build never becomes current
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
}

void StudentTests::ipc_reject_bad_requests() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    const QString socket = dir.filePath("s.sock");

    QVERIFY(window.start_ipc_server(socket));
    const Reply missing = send_frame(socket, ipc::encode_request(ipc::kOpAdd, dir.filePath("missing.txt").toStdString()));
    QCOMPARE(missing.status, 1);
    QVERIFY(!missing.reason.empty());
    std::string zero_length;
    ipc::put_u32_be(zero_length, 0);
    QCOMPARE(send_frame(socket, zero_length).status, 1);
    QCOMPARE(send_frame(socket, ipc::encode_request(9, "/x")).status, 1);
    QTest::qWait(100);
    QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
}

void StudentTests::aiws_send_exit_codes() {
    QTemporaryDir dir;
    auto window = std::make_unique<aiws::gui::MainWindow>();
    const QString socket = dir.filePath("s.sock");
    QVERIFY(window->start_ipc_server(socket));
    write_file(dir, "rel.txt", "hello world");
    QProcess send;
    send.setWorkingDirectory(dir.path());
    send.start(AIWS_SEND_PATH, {"--socket", socket, "add", "rel.txt"});

    QVERIFY(send.waitForFinished(8000));
    QCOMPARE(send.exitCode(), 0);
    QTRY_COMPARE(window->findChild<QListWidget*>()->count(), 1);
    send.start(AIWS_SEND_PATH, {"--socket", socket, "add", "missing.txt"});
    QVERIFY(send.waitForFinished(8000));
    QCOMPARE(send.exitCode(), 1);
    window.reset();
    send.start(AIWS_SEND_PATH, {"--socket", socket, "add", "rel.txt"});
    QVERIFY(send.waitForFinished(8000));
    QCOMPARE(send.exitCode(), 2);
}

// a client that sent half a header is stuck mid-read; stop must not wait for it
void StudentTests::ipc_stops_and_removes_socket() {
    QTemporaryDir dir;
    auto window = std::make_unique<aiws::gui::MainWindow>();
    const QString socket = dir.filePath("s.sock");

    QVERIFY(window->start_ipc_server(socket));
    QVERIFY(QFile::exists(socket));
    sockaddr_un address{};
    QVERIFY(ipc::make_address(socket.toStdString(), address));
    ipc::UniqueFd stalled(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    QVERIFY(::connect(stalled.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
    QVERIFY(::send(stalled.get(), "\0\0", 2, MSG_NOSIGNAL) == 2);
    QTest::qWait(150);
    QElapsedTimer timer;
    timer.start();
    window.reset();
    QVERIFY(timer.elapsed() < 500);
    QVERIFY(!QFile::exists(socket));
}

QTEST_MAIN(StudentTests)
#include "student_tests.moc"