#include "thememanager.h"

#include <QApplication>
#include <QPalette>

namespace dedupvault::ui {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
}

void ThemeManager::apply(core::AppSettings::Theme theme) {
    m_theme = theme;
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;

    app->setStyleSheet(theme == core::AppSettings::Theme::Dark
                           ? darkStylesheet()
                           : lightStylesheet());

    QPalette pal;
    if (theme == core::AppSettings::Theme::Dark) {
        pal.setColor(QPalette::Window, QColor(0x1e, 0x1f, 0x24));
        pal.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xea));
        pal.setColor(QPalette::Base, QColor(0x26, 0x27, 0x2d));
        pal.setColor(QPalette::AlternateBase, QColor(0x2c, 0x2d, 0x34));
        pal.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xea));
        pal.setColor(QPalette::Button, QColor(0x33, 0x34, 0x3c));
        pal.setColor(QPalette::ButtonText, QColor(0xe6, 0xe6, 0xea));
        pal.setColor(QPalette::Highlight, QColor(0x4a, 0x9e, 0xff));
        pal.setColor(QPalette::HighlightedText, QColor(0x0d, 0x0d, 0x10));
        pal.setColor(QPalette::ToolTipBase, QColor(0x33, 0x34, 0x3c));
        pal.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe6, 0xea));
        pal.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8b, 0x93));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x77, 0x78, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x77, 0x78, 0x80));
    } else {
        pal.setColor(QPalette::Window, QColor(0xf5, 0xf6, 0xf8));
        pal.setColor(QPalette::WindowText, QColor(0x1c, 0x1d, 0x21));
        pal.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf1, 0xf4));
        pal.setColor(QPalette::Text, QColor(0x1c, 0x1d, 0x21));
        pal.setColor(QPalette::Button, QColor(0xe8, 0xe9, 0xed));
        pal.setColor(QPalette::ButtonText, QColor(0x1c, 0x1d, 0x21));
        pal.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xeb));
        pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::ToolTipText, QColor(0x1c, 0x1d, 0x21));
        pal.setColor(QPalette::PlaceholderText, QColor(0x86, 0x88, 0x90));
    }
    app->setPalette(pal);
}

QString ThemeManager::darkStylesheet() {
    return QStringLiteral(R"QSS(
    QWidget { background-color: #1e1f24; color: #e6e6ea; font-size: 13px; }
    QMainWindow::separator { background: #1e1f24; }
    QToolTip { background-color: #33343c; color: #e6e6ea; border: 1px solid #4a9eff; }

    QLabel { background: transparent; }
    QLabel[role="title"] { font-size: 20px; font-weight: 600; }
    QLabel[role="subtitle"] { color: #9a9ba4; font-size: 13px; }
    QLabel[role="stat-value"] { font-size: 22px; font-weight: 700; color: #4a9eff; }
    QLabel[role="warn"] { color: #f5b942; }
    QLabel[role="error"] { color: #ff6b6b; }
    QLabel[role="ok"] { color: #4ade80; }

    QPushButton {
        background-color: #33343c; color: #e6e6ea;
        border: 1px solid #44454f; border-radius: 6px;
        padding: 7px 14px; min-height: 20px;
    }
    QPushButton:hover { background-color: #3c3d47; border-color: #4a9eff; }
    QPushButton:pressed { background-color: #2a2b32; }
    QPushButton:disabled { color: #77787f; background-color: #26272d; }
    QPushButton[role="primary"] { background-color: #2563eb; border-color: #3b82f6; font-weight: 600; }
    QPushButton[role="primary"]:hover { background-color: #3b82f6; }
    QPushButton[role="primary"]:disabled { background-color: #2b3550; border-color: #33415e; color: #7c8bb0; }
    QPushButton[role="danger"] { background-color: #7f1d1d; border-color: #b91c1c; }
    QPushButton[role="danger"]:hover { background-color: #991b1b; }

    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
        background-color: #26272d; border: 1px solid #44454f;
        border-radius: 5px; padding: 5px 8px; selection-background-color: #4a9eff;
    }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #4a9eff; }
    QLineEdit:disabled, QComboBox:disabled { color: #77787f; }
    QComboBox::drop-down { border: none; width: 22px; }
    QComboBox QAbstractItemView {
        background-color: #26272d; border: 1px solid #44454f;
        selection-background-color: #4a9eff; selection-color: #0d0d10;
    }

    QTreeWidget, QTableView, QTextEdit, QPlainTextEdit {
        background-color: #26272d; alternate-background-color: #2c2d34;
        border: 1px solid #3a3b43; border-radius: 6px;
        gridline-color: #3a3b43;
    }
    QTreeWidget::item { padding: 4px 2px; }
    QTreeWidget::item:selected { background-color: #2c4a77; color: #e6e6ea; }
    QHeaderView::section {
        background-color: #2c2d34; color: #b9bac2;
        border: none; border-right: 1px solid #3a3b43; border-bottom: 1px solid #3a3b43;
        padding: 5px 8px; font-weight: 600;
    }

    QCheckBox { spacing: 8px; }
    QCheckBox::indicator { width: 16px; height: 16px; }
    QProgressBar {
        background-color: #26272d; border: 1px solid #44454f;
        border-radius: 6px; text-align: center; min-height: 18px;
    }
    QProgressBar::chunk { background-color: #4a9eff; border-radius: 5px; }

    QListWidget, QListView {
        background-color: #23242a; border: 1px solid #3a3b43; border-radius: 8px;
    }
    QListWidget::item { padding: 9px 12px; border-radius: 6px; margin: 2px 4px; }
    QListWidget::item:selected { background-color: #2c4a77; color: #e6e6ea; }
    QListWidget::item:hover { background-color: #2c2d34; }
    QListWidget#sidebar { border: none; background-color: #191a1f; }
    QListWidget::item:disabled { color: #6a6b73; }

    QGroupBox {
        border: 1px solid #3a3b43; border-radius: 8px;
        margin-top: 14px; padding-top: 8px; font-weight: 600;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
    QTabWidget::pane { border: 1px solid #3a3b43; border-radius: 6px; }
    QTabBar::tab {
        background-color: #26272d; padding: 7px 16px;
        border-top-left-radius: 6px; border-top-right-radius: 6px;
        margin-right: 2px;
    }
    QTabBar::tab:selected { background-color: #33343c; color: #4a9eff; }
    QSplitter::handle { background-color: #1e1f24; }
    QScrollBar:vertical { background: #1e1f24; width: 11px; border-radius: 5px; }
    QScrollBar::handle:vertical {
        background: #44454f; border-radius: 5px; min-height: 24px;
    }
    QScrollBar::handle:vertical:hover { background: #55565f; }
    QScrollBar:horizontal { background: #1e1f24; height: 11px; border-radius: 5px; }
    QScrollBar::handle:horizontal {
        background: #44454f; border-radius: 5px; min-width: 24px;
    }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
    QStatusBar { background: #191a1f; color: #9a9ba4; }
    QToolBar { background: transparent; border: none; spacing: 6px; }
    QMenuBar { background: #191a1f; }
    QMenuBar::item:selected { background: #33343c; }
    QMenu { background: #26272d; border: 1px solid #44454f; }
    QMenu::item:selected { background: #2c4a77; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QSpinBox::up-button, QSpinBox::down-button,
    QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
        background: #33343c; border: none; width: 16px;
    }
    QMessageBox { background-color: #26272d; }
    QDialog { background-color: #1e1f24; }
  )QSS");
}

QString ThemeManager::lightStylesheet() {
    return QStringLiteral(R"QSS(
    QWidget { background-color: #f5f6f8; color: #1c1d21; font-size: 13px; }
    QToolTip { background-color: #ffffff; color: #1c1d21; border: 1px solid #2563eb; }

    QLabel { background: transparent; }
    QLabel[role="title"] { font-size: 20px; font-weight: 600; }
    QLabel[role="subtitle"] { color: #5b5d66; font-size: 13px; }
    QLabel[role="stat-value"] { font-size: 22px; font-weight: 700; color: #2563eb; }
    QLabel[role="warn"] { color: #a16207; }
    QLabel[role="error"] { color: #b91c1c; }
    QLabel[role="ok"] { color: #15803d; }

    QPushButton {
        background-color: #e8e9ed; color: #1c1d21;
        border: 1px solid #c9cbd2; border-radius: 6px;
        padding: 7px 14px; min-height: 20px;
    }
    QPushButton:hover { background-color: #dcdfe5; border-color: #2563eb; }
    QPushButton:disabled { color: #9497a1; background-color: #eeeff2; }
    QPushButton[role="primary"] { background-color: #2563eb; border-color: #2563eb; color: white; font-weight: 600; }
    QPushButton[role="primary"]:hover { background-color: #1d4ed8; }
    QPushButton[role="primary"]:disabled { background-color: #a9c0f5; border-color: #a9c0f5; }
    QPushButton[role="danger"] { background-color: #fee2e2; border-color: #ef4444; color: #991b1b; }

    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
        background-color: #ffffff; border: 1px solid #c9cbd2;
        border-radius: 5px; padding: 5px 8px;
    }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #2563eb; }
    QComboBox::drop-down { border: none; width: 22px; }
    QComboBox QAbstractItemView {
        background-color: #ffffff; border: 1px solid #c9cbd2;
        selection-background-color: #2563eb; selection-color: #ffffff;
    }

    QTreeWidget, QTableView, QTextEdit, QPlainTextEdit {
        background-color: #ffffff; alternate-background-color: #f5f6f8;
        border: 1px solid #d7d9df; border-radius: 6px; gridline-color: #e4e5ea;
    }
    QTreeWidget::item { padding: 4px 2px; }
    QTreeWidget::item:selected { background-color: #dbe7ff; color: #1c1d21; }
    QHeaderView::section {
        background-color: #eef0f4; color: #45474f;
        border: none; border-right: 1px solid #d7d9df; border-bottom: 1px solid #d7d9df;
        padding: 5px 8px; font-weight: 600;
    }

    QCheckBox { spacing: 8px; }
    QCheckBox::indicator { width: 16px; height: 16px; }
    QProgressBar {
        background-color: #e8e9ed; border: 1px solid #c9cbd2;
        border-radius: 6px; text-align: center; min-height: 18px;
    }
    QProgressBar::chunk { background-color: #2563eb; border-radius: 5px; }

    QListWidget, QListView {
        background-color: #ffffff; border: 1px solid #d7d9df; border-radius: 8px;
    }
    QListWidget::item { padding: 9px 12px; border-radius: 6px; margin: 2px 4px; }
    QListWidget::item:selected { background-color: #dbe7ff; color: #1c1d21; }
    QListWidget::item:hover { background-color: #f0f1f4; }
    QListWidget#sidebar { border: none; background-color: #ecedf1; }
    QListWidget::item:disabled { color: #a0a2ab; }

    QGroupBox {
        border: 1px solid #d7d9df; border-radius: 8px;
        margin-top: 14px; padding-top: 8px; font-weight: 600;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }
    QTabWidget::pane { border: 1px solid #d7d9df; border-radius: 6px; }
    QTabBar::tab {
        background-color: #e8e9ed; padding: 7px 16px;
        border-top-left-radius: 6px; border-top-right-radius: 6px; margin-right: 2px;
    }
    QTabBar::tab:selected { background-color: #ffffff; color: #2563eb; }
    QSplitter::handle { background-color: #f5f6f8; }
    QScrollBar:vertical { background: #f5f6f8; width: 11px; border-radius: 5px; }
    QScrollBar::handle:vertical {
        background: #c9cbd2; border-radius: 5px; min-height: 24px;
    }
    QScrollBar:horizontal { background: #f5f6f8; height: 11px; border-radius: 5px; }
    QScrollBar::handle:horizontal {
        background: #c9cbd2; border-radius: 5px; min-width: 24px;
    }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
    QStatusBar { background: #ecedf1; color: #5b5d66; }
    QMenuBar { background: #ecedf1; }
    QMenuBar::item:selected { background: #dcdfe5; }
    QMenu { background: #ffffff; border: 1px solid #c9cbd2; }
    QMenu::item:selected { background: #dbe7ff; }
    QMessageBox { background-color: #ffffff; }
    QDialog { background-color: #f5f6f8; }
  )QSS");
}

} // namespace dedupvault::ui