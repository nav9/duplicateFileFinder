#include "core/databasemanager.h"
#include "core/logger.h"
#include "core/settings.h"
#include "ui/mainwindow.h"
#include "ui/setupdialog.h"
#include "ui/thememanager.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DedupVault"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("DedupVault"));
    QApplication::setOrganizationDomain(QStringLiteral("dedupvault.local"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/gallery/app_icon.svg")));

    dedupvault::core::AppSettings settings;

    // --- Working folder: verify write privileges, offer browse/create ------
    // A first run (or a folder that became read-only / was deleted) must be
    // resolved before anything else: the catalog and logs live there.
    if (!settings.isWorkingFolderValid()) {
        dedupvault::ui::SetupDialog dialog(settings.workingFolder());
        if (!dialog.run()) {
            // User chose to quit without a writable working folder.
            return 0;
        }
    }

    const QString workingFolder = settings.workingFolder();
    const QString logsDir = QDir(workingFolder).filePath(QStringLiteral("logs"));
    const QString tmpDir = QDir(workingFolder).filePath(QStringLiteral("tmp"));
    QDir().mkpath(logsDir);
    QDir().mkpath(tmpDir);

    // --- Logging (rotating, configurable, can be switched off) -------------
    auto& logger = dedupvault::core::Logger::instance();
    logger.initialize(
        logsDir,
        settings.loggingEnabled(),
        static_cast<dedupvault::core::Logger::Level>(settings.logLevel()),
        settings.maxLogFileSize(),
        settings.maxLogFiles());
    logger.installMessageHandler();
    logger.info(QStringLiteral("DedupVault %1 starting (Qt %2, working folder: %3)")
                    .arg(QApplication::applicationVersion(),
                         QString::fromLatin1(qVersion()),
                         workingFolder));
    logger.info(QStringLiteral("Temporary working memory directory: %1")
                    .arg(tmpDir));

    // --- Theme (dark by default; light via Settings) -----------------------
    dedupvault::ui::ThemeManager theme;
    theme.apply(settings.theme());

    // --- Persistent catalog ------------------------------------------------
    dedupvault::core::DatabaseManager db;
    const QString dbPath = QDir(workingFolder).filePath(QStringLiteral("catalog.db"));
    if (!db.open(dbPath)) {
        const QString message =
            QObject::tr("DedupVault could not open its catalog database:\n%1\n\n%2")
                .arg(dbPath, db.lastError());
        logger.error(message);
        QMessageBox::critical(nullptr, QObject::tr("Database error"), message);
        return 1;
    }

    dedupvault::ui::MainWindow window(db, settings, theme);
    window.show();

    const int exitCode = app.exec();
    logger.info(QStringLiteral("DedupVault exiting with code %1").arg(exitCode));
    logger.flush();
    return exitCode;
}