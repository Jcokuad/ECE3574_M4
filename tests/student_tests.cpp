// Your M4 tests (specification, Section 6).

#include "aiws/gui/main_window.hpp"
#include "aiws/gui/build_runner.hpp"

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
        if (i->placeholderText() == "Search the current corpus") {
            return i;
        }
    }
    return nullptr;
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
    QVERIFY(finished.wait(5000));
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
    QVERIFY(finished.wait(5000)); // lost failure does not come and fails after 5s
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
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 5000);
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: 2 chunks"));
    query_edit(window)->setText("alpha");
    QVERIFY(button(window, "Search")->isEnabled());
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
    QTRY_VERIFY_WITH_TIMEOUT(build->isEnabled(), 5000);
    // old build finished and joined
    QCOMPARE(corpus_status(window)->text(), QString("Corpus: Old Corpus (rebuild required)"));
    query_edit(window)->setText("alpha");
    QVERIFY(!button(window, "Search")->isEnabled());
}

// creates a QApplication since the target links Qt Widgets and the test uses Qt Widgets
QTEST_MAIN(StudentTests)
#include "student_tests.moc"