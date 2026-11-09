#include "aiws/gui/main_window.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QObject>
#include <QString>
#include <QTimer>

#include <csignal>
#include <iostream>

namespace {

// Define the ExitCallback function used to confirm an Exit request.
void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Do you want to exit AI Workspace?", 
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

struct Options {
    int build_delay_ms{0}; 
    QString ipc_socket;
};

// Returns the build delay in ms from the command line arguments, or -1 if it is a negative num or not provided
Options parse_options(const QCoreApplication& app) {
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption delay_option("build_delay_ms", "Extra build time per document in milliseconds.", "ms", "0");
    const QCommandLineOption socket_option("ipc-socket", "Unix domain socket that aiws_send connects to.", "path", QString::fromStdString(aiws::ipc::default_socket_path()));
    parser.addOption(delay_option);
    parser.addOption(socket_option);
    parser.process(app);

    Options options;
    bool ok = false;
    const int delay = parser.value(delay_option).toInt(&ok);
    options.build_delay_ms = (ok && delay >= 0) ? delay : -1;
    options.ipc_socket = parser.value(socket_option);
    return options;
}

// SIGINT/SIGTERM only set this flag. Signal Watcher checks it on the GUI thread and quits normally
// so ~MainWindow joins both threads and removes the socket file E4
volatile std::sig_atomic_t g_stop_requested = 0;

void request_stop(int) { g_stop_requested = 1; }

class SignalWatcher : public QObject {
    Q_OBJECT
public slots:
    void check() {
        if (g_stop_requested != 0) QApplication::quit();
    }
};

} //namespace

int main(int argc, char* argv[]) {
    // Create the QApplication, construct/configure the MainWindow,
    // register the ExitCallback, show the window, and run the event loop.
    QApplication app(argc, argv);

    const Options options = parse_options(app);
    if (options.build_delay_ms < 0) {
        std::cerr << "--build-delay-ms must be a non-negative integer\n";
        return 2;
    }

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.set_build_delay_ms(options.build_delay_ms);

    QString ipc_error;
    if (!window.start_ipc_server(options.ipc_socket, &ipc_error)) {
        std::cerr << "warning: remote add disabled: " << ipc_error.toStdString() << '\n';
    }

    window.show();

    std::signal(SIGINT, &request_stop);
    std::signal(SIGTERM, &request_stop);
    SignalWatcher watcher;
    QTimer stop_timer;
    QObject::connect(&stop_timer, &QTimer::timeout, &watcher, &SignalWatcher::check);
    stop_timer.start(100);

    // run the event loop
    return app.exec();
}

#include "main.moc"
