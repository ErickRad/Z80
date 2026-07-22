#include <QApplication>

#include "window.hpp"

int main (int argc, char *argv[])
{
    QApplication app (argc, argv);
    QApplication::setApplicationName ("Z80 Virtual Machine");
    QApplication::setOrganizationName ("UFPel");

    MainWindow window;
    window.show();

    return app.exec();
}