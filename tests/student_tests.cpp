// Your M4 tests (specification, Section 6).

#include "aiws/gui/main_window.hpp"

#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QPushButton>

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
}  // namespace

class StudentTests : public QObject {
    Q_OBJECT
private slots:
    void add_document_file_valid(); // adds a valid readable document file
    void add_document_file_invalid(); // rejects missing, unreadable, or duplicate document files
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

// creates a QApplication since the target links Qt Widgets and the test uses Qt Widgets
QTEST_MAIN(StudentTests)
#include "student_tests.moc"