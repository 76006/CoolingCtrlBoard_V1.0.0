#include "mainwindow.h"
#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("CoolingCtrlBoard");
    app.setApplicationName("CoolingRelief");
    app.setApplicationDisplayName(QStringLiteral("冷媒罐循环泄压试验"));
    MainWindow window;
    window.show();
    return app.exec();
}
