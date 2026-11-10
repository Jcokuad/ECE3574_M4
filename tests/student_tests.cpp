// Your M4 tests (specification, Section 6).

#include "aiws/gui/main_window.hpp"
#include "aiws/gui/build_runner.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QPushButton>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <stdexcept>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QThread>
#include <memory>

namespace {
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

class FailingChunker final : public aiws::ChunkingStrategy {
public:
    std::vector<aiws::Chunk> chunk(const aiws::Document&, std::size_t) const override {
        throw std::runtime_error("injected chunking failure");
    }
};

std::unique_ptr<aiws::ChunkingStrategy> make_failing_chunker(int, std::shared_ptr<const std::atomic<bool>>) {
    return std::make_unique<FailingChunker>();
}

aiws::Workspace two_docs() {
    aiws::Workspace ws;
    ws.add_document(aiws::Document{"h", "h", "hello world"});
    ws.add_document(aiws::Document{"j", "j", "jorts shirt"});
    return ws;
}

QLabel* corpus_status(QWidget& w) {
    return w.findChild<QLabel*>("corpusStatusLabel");
}

QLineEdit* query_edit(QWidget& w) {
    for (QLineEdit* i : w.findChildren<QLineEdit*>()) {
        if (i->placeholderText() == "Enter a query") {
            return i;
        }
    }
    return nullptr;
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
    void add_document_file_valid(); // adds a valid readable document file
    void add_document_file_invalid(); // rejects missing, unreadable, or duplicate document files
    void runner_delivers_built_corpus(); 
    void runner_throws_failure();
    void runner_shutdown_does_not_wait();
    void build_runs_off_gui_thread(); // build runs off the gui thread and becomes current
    void discards_results_add_during_build(); 
    void exit_and_rejoin(); // exited and repeated builds keep working
    void ipc_add_odd_path_in_pieces(); // E1, E2, E3
    void ipc_add_during_build_stale_corpus(); // E1 with R2
    void ipc_reject_bad_requests(); 
    void aiws_send_exit_codes(); // E1
    void ipc_stops_and_removes_socket(); // E4
};

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
    // missing file
    QVERIFY(!window.add_document_file(dir.filePath("missing.txt")));
    QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
    QVERIFY(!button(window, "Build Corpus")->isEnabled());
    // duplicate file
    const QString valid = write_file(dir, "a.txt", "Hello World");
    QVERIFY(window.add_document_file(valid));
    QVERIFY(!window.add_document_file(valid));
    QCOMPARE(window.findChild<QListWidget*>()->count(), 1);
}

// starts a build and checks that a second start is not allowed
// waits for the finished signal then checks the result has 2 chunks
void StudentTests::runner_delivers_built_corpus() {
    aiws::gui::BuildRunner runner;
    QSignalSpy finished(&runner, &aiws::gui::BuildRunner::build_finished);
    QVERIFY(runner.start(7, two_docs(), 0));
    QVERIFY(!runner.start(8, two_docs(), 0)); // only one worker allowed at a time
    QVERIFY(finished.wait(2000));
    QCOMPARE(finished.first().first().value<quint64>(), quint64{7});
    QVERIFY(!runner.busy()); // joined before signal
    QCOMPARE(runner.take_result().chunk_count(), std::size_t{2});
}

// uses a chunker that always throws and ensures that gui get notified and take_result() rethrows
void StudentTests::runner_throws_failure() {
    aiws::gui::BuildRunner runner;
    runner.set_chunker_factory(&make_failing_chunker);
    QSignalSpy finished(&runner, &aiws::gui::BuildRunner::build_finished);
    QVERIFY(runner.start(1, two_docs(), 0));
    QVERIFY(finished.wait(2000)); // lost failure does not come and fails after 5s
    bool threw = false;
    try {
        runner.take_result();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    QVERIFY(threw);
}

// starts a 10s per document build, calls shutdown() and checks it returns in under 1 sec so Exit does not hang
void StudentTests::runner_shutdown_does_not_wait() {
    aiws::gui::BuildRunner runner;
    QVERIFY(runner.start(1, two_docs(), 10000)); // 10s per doc
    QElapsedTimer timer;
    timer.start();
    runner.shutdown();
    QVERIFY(timer.elapsed() < 1000);
    QVERIFY(!runner.busy());
}

void StudentTests::build_runs_off_gui_thread() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(300);
    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QVERIFY(window.add_document_file(write_file(dir, "b.txt", "jorts shirt")));
    QPushButton* build = button(window, "Build Corpus");
    QElapsedTimer timer;
    timer.start();
    build->click();
    QVERIFY(timer.elapsed() < 200);
    QVERIFY(!build->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 2000);
    QLabel* status = corpus_status(window);
    QVERIFY2(status != nullptr, "no qlabel with object name corpusStatusLabel");
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: 2 chunks"));

    QLineEdit* query = query_edit(window);
    QVERIFY2(query != nullptr, "no qlinedit placeholder");
    query->setText("hello");

    QPushButton* search = button(window, "Search");
    QVERIFY2(search != nullptr, "no search button");
    QVERIFY(search->isEnabled());
}

void StudentTests::discards_results_add_during_build() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_build_delay_ms(200);
    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QPushButton* build = button(window, "Build Corpus");
    build->click();
    // build could not have delivered since no event loop has run yet
    QVERIFY(window.add_document_file(write_file(dir, "b.txt", "jorts shirt")));
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 2000);
    // old build finished and joined
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
    query_edit(window)->setText("alpha");
    QVERIFY(!button(window, "Search")->isEnabled());
}

void StudentTests::exit_and_rejoin() {
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    window.set_exit_callback(&record_exit);
    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QPushButton* build = button(window, "Build Corpus");
    for (int i = 0; i < 5; i++) {  // each thread is joined before the next starts
        build->click();
        QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 2000);
        QCOMPARE(corpus_status(window)->text(), QString("Corpus: 1 chunks"));
    }
    exit_requests = 0;
    build->click();
    button(window, "Exit")->click(); // exit during a build asks the callback
    QCOMPARE(exit_requests, 1);
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 2000);
}

void StudentTests::ipc_add_odd_path_in_pieces() {// E1, E2, E3
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    const QString socket = dir.filePath("s.sock");
    QVERIFY(window.start_ipc_server(socket));
    const QString path = write_file(dir, QString::fromUtf8("my file é\nline.txt"), "hello world");
    // 3 bytes at a time, 20 ms apart. The test thread sleeps, so the reply
    // arriving also proves it never waited for the GUI thread.
    const Reply reply = send_frame(socket, ipc::encode_request(ipc::kOpAdd, path.toStdString()), 3, 20);
    QCOMPARE(reply.status, 0);
    QListWidget* list = window.findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(), QString::fromUtf8("my file é\nline.txt"));
}

void StudentTests::ipc_add_during_build_stale_corpus() {// E1 with R2
    QTemporaryDir dir;
    aiws::gui::MainWindow window;
    const QString socket = dir.filePath("s.sock");
    QVERIFY(window.start_ipc_server(socket));
    window.set_build_delay_ms(200);
    QVERIFY(window.add_document_file(write_file(dir, "a.txt", "hello world")));
    QPushButton* build = button(window, "Build Corpus");
    QVERIFY(build != nullptr);
    build->click();
    const QString b = write_file(dir, "b.txt", "jorts shirt");
    QCOMPARE(send_frame(socket, ipc::encode_request(ipc::kOpAdd, b.toStdString())).status, 0);
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 5000);
    QLabel* status = corpus_status(window);
    QVERIFY(status != nullptr);
    QCOMPARE(status->text(), QString("Corpus: Old Corpus (rebuild required)"));

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
    QTest::qWait(100);  // let any wrongly posted add arrive
    QCOMPARE(window.findChild<QListWidget*>()->count(), 0);
}

void StudentTests::aiws_send_exit_codes() {// E1
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
    window.reset();  // app gone
    send.start(AIWS_SEND_PATH, {"--socket", socket, "add", "rel.txt"});
    QVERIFY(send.waitForFinished(8000));
    QCOMPARE(send.exitCode(), 2);
}

void StudentTests::ipc_stops_and_removes_socket() {// E4
    QTemporaryDir dir;
    auto window = std::make_unique<aiws::gui::MainWindow>();
    const QString socket = dir.filePath("s.sock");
    QVERIFY(window->start_ipc_server(socket));
    QVERIFY(QFile::exists(socket));

    // A client stalled halfway through a request must not delay the stop.
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

// creates a QApplication since the target links Qt Widgets and the test uses Qt Widgets
QTEST_MAIN(StudentTests)
#include "student_tests.moc"