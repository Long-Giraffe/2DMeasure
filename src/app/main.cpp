#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("2D Measure"));
    QApplication::setOrganizationName(QStringLiteral("2D Measure"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = QApplication::font();
    font.setPointSize(10);
    QApplication::setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#centralWidget {
            color: #263442;
            background-color: #eef1f4;
        }
        QGroupBox {
            background-color: #ffffff;
            border: 1px solid #d6dde5;
            border-radius: 5px;
            margin-top: 14px;
            padding-top: 8px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 4px;
            color: #33475b;
            background-color: transparent;
        }
        QGroupBox::indicator {
            width: 14px;
            height: 14px;
        }
        QPushButton {
            min-height: 28px;
            padding: 2px 12px;
            border: 1px solid #b9c5d1;
            border-radius: 4px;
            background-color: #ffffff;
            color: #263442;
        }
        QPushButton:hover {
            border-color: #6c9ecb;
            background-color: #f4f9fd;
        }
        QPushButton:pressed, QPushButton:checked {
            border-color: #2878b5;
            background-color: #dceefa;
            color: #155b8f;
        }
        QPushButton:disabled {
            border-color: #d7dde3;
            background-color: #f3f5f7;
            color: #9aa6b2;
        }
        QPushButton[role="primary"] {
            border-color: #2478b5;
            background-color: #2478b5;
            color: #ffffff;
            font-weight: 600;
        }
        QPushButton[role="primary"]:hover {
            border-color: #1c659a;
            background-color: #1c6fa8;
        }
        QPushButton[role="danger"] {
            border-color: #d6a8a8;
            color: #a33b3b;
            background-color: #fffafa;
        }
        QPushButton[role="danger"]:hover {
            border-color: #c96a6a;
            background-color: #fff0f0;
        }
        QPushButton[role="tool"]:checked {
            border-color: #2478b5;
            background-color: #dceefa;
            color: #155b8f;
            font-weight: 600;
        }
        QPushButton#compactButton {
            min-height: 24px;
            padding: 1px 10px;
        }
        QWidget#imageToolbar {
            background-color: #ffffff;
            border: 1px solid #d6dde5;
            border-radius: 4px;
        }
        QLabel#imageToolbarTitle {
            color: #33475b;
            background-color: transparent;
            font-weight: 600;
        }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
            min-height: 26px;
            padding: 1px 6px;
            border: 1px solid #bdc8d3;
            border-radius: 3px;
            background-color: #ffffff;
            selection-background-color: #2478b5;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
            border-color: #2478b5;
        }
        QComboBox::drop-down {
            border: none;
            width: 22px;
        }
        QListWidget, QTableWidget {
            background-color: #ffffff;
            alternate-background-color: #f6f8fa;
            border: 1px solid #d6dde5;
            border-radius: 3px;
            gridline-color: #e3e8ed;
            selection-background-color: #dceefa;
            selection-color: #174f78;
        }
        QListWidget::item {
            min-height: 28px;
            padding: 2px 6px;
        }
        QListWidget::item:selected {
            border-left: 3px solid #2478b5;
        }
        QHeaderView::section {
            padding: 5px 7px;
            border: none;
            border-right: 1px solid #d6dde5;
            border-bottom: 1px solid #cbd5df;
            background-color: #e8edf2;
            color: #33475b;
            font-weight: 600;
        }
        QTabWidget::pane {
            border: 1px solid #d6dde5;
            background-color: #ffffff;
        }
        QTabBar::tab {
            min-width: 90px;
            padding: 7px 14px;
            border: 1px solid #d6dde5;
            border-bottom: none;
            background-color: #e8edf2;
            color: #536474;
        }
        QTabBar::tab:selected {
            background-color: #ffffff;
            color: #1d679e;
            font-weight: 600;
        }
        QProgressBar {
            min-height: 20px;
            border: 1px solid #c4cfd9;
            border-radius: 3px;
            background-color: #ffffff;
            text-align: center;
        }
        QProgressBar::chunk {
            background-color: #3a91cf;
        }
        QScrollBar:vertical {
            width: 12px;
            background: #eef1f4;
        }
        QScrollBar:horizontal {
            height: 12px;
            background: #eef1f4;
        }
        QScrollBar::handle {
            min-height: 28px;
            min-width: 28px;
            border-radius: 5px;
            background: #b7c2cd;
        }
        QScrollBar::handle:hover {
            background: #8fa2b4;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0px;
            height: 0px;
        }
        QSplitter::handle {
            background-color: #d6dde5;
        }
        QSplitter::handle:hover {
            background-color: #9cb6cc;
        }
        QStatusBar {
            border-top: 1px solid #d6dde5;
            background-color: #ffffff;
            color: #536474;
        }
    )"));

    MainWindow window;
    window.show();
    return app.exec();
}
