#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QCommandLineOption>

// Define the ExitCallback function used to confirm an Exit request.
void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Do you want to exit AI Workspace?", 
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

// Returns the build delay in ms from the command line arguments, or -1 if it is a negative num or not provided
int build_delay_from_args(const QCoreApplication& app) {
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption delay_option("build_delay_ms", "Extra build time per document in milliseconds.", "ms", "0");
    parser.addOption(delay_option);
    parser.process(app);
    bool ok = false;
    const int delay = parser.value(delay_option).toInt(&ok);
    return (ok && delay >= 0) ? delay : -1;
}

int main(int argc, char* argv[]) {
    // Create the QApplication, construct/configure the MainWindow,
    // register the ExitCallback, show the window, and run the event loop.
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.show();

    const int build_delay = build_delay_from_args(app);
    if (build_delay < 0) {
        QMessageBox::warning(&window, "Build Delay", "Invalid build delay provided. Using default of 0 ms.");
    } else {
        window.set_build_delay_ms(build_delay);
    }

    // run the event loop
    return app.exec();
}
