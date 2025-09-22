#include <QApplication>
#include "MZipGui.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MZipGui gui;
    gui.show();

    // Open archive from command line if provided
    QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        gui.loadArchive(std::filesystem::path(args[1].toStdString()));
    }

    return app.exec();
}