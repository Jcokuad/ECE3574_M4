#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QSocketNotifier>

#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <iostream>

namespace {

volatile std::sig_atomic_t g_signal_write_fd = -1;  // set once, before the handlers

void request_stop(int) {
    const char byte = 1;
    if (::write(g_signal_write_fd, &byte, 1) < 0) {}
}

class SignalWatcher : public QObject {
    Q_OBJECT
public slots:
    void on_signal() { QApplication::quit(); }
};

// Define the ExitCallback function used to confirm an Exit request.
void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Do you want to exit AI Workspace?", 
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

} //namespace

int main(int argc, char* argv[]) {
    // Create the QApplication, construct/configure the MainWindow,
    // register the ExitCallback, show the window, and run the event loop.
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption delay_option("build-delay-ms", "Extra build time per document, in ms.", "ms", "0");
    const QCommandLineOption socket_option("ipc-socket", "Unix socket for aiws_send.", "path", QString::fromStdString(aiws::ipc::default_socket_path()));
    parser.addOption(delay_option);
    parser.addOption(socket_option);
    parser.process(app);

    bool ok = false;
    const int delay = parser.value(delay_option).toInt(&ok);
    if (!ok || delay < 0) { 
        std::cerr << "--build-delay-ms must be a non-negative integer\n"; return 2; 
    }

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(delay);
    QString ipc_error;
    if (!window.start_ipc_server(parser.value(socket_option), &ipc_error))
        std::cerr << "warning: remote add disabled: " << ipc_error.toStdString() << '\n';
    window.show();

    int signal_pipe[2];
    if (::pipe2(signal_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        std::cerr << "could not create signal pipe\n";
        return 2;
    }

    aiws::ipc::UniqueFd signal_read(signal_pipe[0]);
    aiws::ipc::UniqueFd signal_write(signal_pipe[1]);
    g_signal_write_fd = signal_write.get();

    SignalWatcher watcher;
    QSocketNotifier notifier(signal_read.get(), QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &watcher, &SignalWatcher::on_signal);
    std::signal(SIGINT, &request_stop);
    std::signal(SIGTERM, &request_stop);

    const int code = app.exec();
    std::signal(SIGINT, SIG_DFL);  // the pipe closes below; no handler may write to it after
    std::signal(SIGTERM, SIG_DFL);

    return code;
}

#include "main.moc"
