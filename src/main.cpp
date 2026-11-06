#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QMessageBox>

// Define the ExitCallback function used to confirm an Exit request.
void confirm_exit(QWidget* parent) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Do you want to exit AI Workspace?", 
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    
    if (answer == QMessageBox::Yes) {
        QApplication::quit();
    }
}

int main(int argc, char* argv[]) {
    // Create the QApplication, construct/configure the MainWindow,
    // register the ExitCallback, show the window, and run the event loop.
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    window.set_exit_callback(&confirm_exit);
    window.show();

    // run the event loop
    return app.exec();
}
