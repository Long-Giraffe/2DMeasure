#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("2D Measure"));
    QApplication::setOrganizationName(QStringLiteral("2D Measure"));

    QFont font = QApplication::font();
    font.setPointSize(10);
    QApplication::setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
