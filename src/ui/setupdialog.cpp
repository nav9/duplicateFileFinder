#include "setupdialog.h"
#include "core/filesystemutils.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace dedupvault::ui {

SetupDialog::SetupDialog(const QString& suggested, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("DedupVault Setup — Working Folder"));
    setModal(true);
    resize(560, 260);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Choose a working folder"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    auto* expl = new QLabel(
        tr("DedupVault stores its file catalog, rotating logs and working "
           "memory in this folder. It needs write access to it. "
           "You can change it later in Settings."), this);
    expl->setProperty("role", "subtitle");
    expl->setWordWrap(true);
    layout->addWidget(expl);

    auto* pathRow = new QHBoxLayout;
    m_pathEdit = new QLineEdit(suggested, this);
    m_pathEdit->setPlaceholderText(tr("Path to the working folder"));
    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(browseBtn);
    layout->addLayout(pathRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* btnRow = new QHBoxLayout;
    m_createButton = new QPushButton(tr("Create && Use This Folder"), this);
    m_createButton->setProperty("role", "primary");
    auto* testBtn = new QPushButton(tr("Test Write Access"), this);
    btnRow->addWidget(testBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(m_createButton);
    layout->addLayout(btnRow);

    auto* buttons = new QDialogButtonBox(this);
    m_okButton = buttons->addButton(tr("Use Folder"), QDialogButtonBox::AcceptRole);
    m_okButton->setProperty("role", "primary");
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this, &SetupDialog::browse);
    connect(testBtn, &QPushButton::clicked, this, &SetupDialog::testCurrent);
    connect(m_createButton, &QPushButton::clicked, this, &SetupDialog::createAndUse);
    connect(m_pathEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        refreshActionState();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshActionState();
    if (!m_chosen.isEmpty()) setStatus(tr("Folder is ready."), "ok");
}

void SetupDialog::setStatus(const QString& text, const char* role) {
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("role", role);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void SetupDialog::refreshActionState() {
    const QString path = m_pathEdit->text().trimmed();
    const bool exists = QFileInfo::exists(path);
    const bool writable = core::AppSettings::isFolderWritable(path);
    const bool parentWritable = !exists &&
        core::FileSystemUtils::isWritable(QFileInfo(path).absolutePath());

    m_okButton->setEnabled(writable);
    m_createButton->setEnabled(!exists && parentWritable);
    m_createButton->setText(exists && !writable
        ? tr("Folder exists but is read-only — browse for another")
        : tr("Create && Use This Folder"));

    if (path.isEmpty()) {
        setStatus(QString(), "subtitle");
    } else if (writable) {
        setStatus(tr("✓ Writable: %1").arg(path), "ok");
    } else if (!exists) {
        setStatus(tr("Folder does not exist yet: %1").arg(path),
                  parentWritable ? "warn" : "error");
    } else {
        setStatus(tr("No write privileges for: %1 — choose a different folder "
                     "or create the folder with your system permissions first.")
                      .arg(path), "error");
    }
}

void SetupDialog::browse() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select or create a working folder"), m_pathEdit->text());
    if (!dir.isEmpty()) m_pathEdit->setText(dir);
    refreshActionState();
}

void SetupDialog::createAndUse() {
    const QString path = m_pathEdit->text().trimmed();
    if (path.isEmpty()) return;
    if (!core::AppSettings::createFolder(path)) {
        QMessageBox::warning(this, tr("Create folder failed"),
                             tr("Could not create:\n%1").arg(path));
        return;
    }
    refreshActionState();
    if (core::AppSettings::isFolderWritable(path)) {
        m_chosen = path;
        core::AppSettings settings;
        settings.setWorkingFolder(path);
        setStatus(tr("✓ Created and writable: %1").arg(path), "ok");
    }
}

void SetupDialog::testCurrent() {
    refreshActionState();
    const QString path = m_pathEdit->text().trimmed();
    if (!core::AppSettings::isFolderWritable(path)) {
        QMessageBox::information(
            this, tr("Write test failed"),
            tr("DedupVault cannot write to:\n%1\n\nBrowse for a writable folder "
               "or create this one.").arg(path));
        return;
    }
    // Prove it with an actual write.
    QDir dir(path);
    const QString probe = dir.filePath(QStringLiteral(".dedupvault_write_probe"));
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Write test failed"),
                             tr("Could not create a test file:\n%1\n%2")
                                 .arg(probe, f.errorString()));
        return;
    }
    f.write("ok");
    f.close();
    f.remove();
    m_chosen = path;
    core::AppSettings settings;
    settings.setWorkingFolder(path);
    setStatus(tr("✓ Write test passed: %1").arg(path), "ok");
    QMessageBox::information(this, tr("Write test passed"),
                             tr("DedupVault can write to:\n%1").arg(path));
}

bool SetupDialog::run() {
    while (true) {
        if (exec() != QDialog::Accepted) {
            const int choice = QMessageBox::question(
                this, tr("Working folder required"),
                tr("DedupVault cannot start without a writable working folder. "
                   "Retry setup?"),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
            if (choice != QMessageBox::Yes) return false;
            continue;
        }
        const QString path = m_pathEdit->text().trimmed();
        if (core::AppSettings::isFolderWritable(path)) {
            m_chosen = path;
            core::AppSettings settings;
            settings.setWorkingFolder(path);
            return true;
        }
        setStatus(tr("Selected folder is not writable — pick another one."), "error");
    }
    return false;
}

} // namespace dedupvault::ui