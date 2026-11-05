// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PathPickerWidget.h"
#include "app/FileDialogUtils.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include <verdigris/wobjectimpl.h>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QStyle>
#include <QEvent>
#include <QSizePolicy>

W_OBJECT_IMPL(PathPickerWidget)

namespace
{

void applyStatusTone(QLabel* label, const QString& tone)
{
    if (!label)
    {
        return;
    }

    label->setProperty("anStatusTone", tone);
    if (auto* style = label->style())
    {
        style->unpolish(label);
        style->polish(label);
    }
    label->update();
}

} // anonymous namespace

PathPickerWidget::PathPickerWidget(Mode mode, QWidget* parent) : QWidget(parent), m_Mode(mode)
{
    setupUi();
}

void PathPickerWidget::setupUi()
{
    const auto& tokenSet = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(tokenSet.spacing.xs);

    // Path input row
    auto* inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(tokenSet.spacing.xs);

    m_PathEdit = new QLineEdit(this);
    m_PathEdit->setPlaceholderText(tr("(not configured)"));
    inputLayout->addWidget(m_PathEdit, 1);

    m_BrowseButton = new QPushButton(this);
    refreshBrowseIcon();
    m_BrowseButton->setToolTip(tr("Browse..."));
    m_BrowseButton->setFixedWidth(32);
    m_BrowseButton->setAutoDefault(false);
    m_BrowseButton->setDefault(false);
    m_BrowseButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    inputLayout->addWidget(m_BrowseButton);

    m_DetectButton = new QPushButton(tr("Detect"), this);
    m_DetectButton->setToolTip(tr("Auto-detect path"));
    m_DetectButton->setAutoDefault(false);
    m_DetectButton->setDefault(false);
    m_DetectButton->setProperty("anProcessButtonRole", QStringLiteral("secondary"));
    m_DetectButton->setVisible(false); // Hidden by default
    inputLayout->addWidget(m_DetectButton);

    mainLayout->addLayout(inputLayout);

    // Status label (hidden by default)
    m_StatusLabel = new QLabel(this);
    m_StatusLabel->setObjectName(QStringLiteral("pathPickerStatusLabel"));
    m_StatusLabel->setVisible(false);
    m_StatusLabel->setWordWrap(true);
    m_StatusLabel->setMinimumWidth(0);
    m_StatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_StatusLabel->setFont(tokenSet.typography.caption);
    mainLayout->addWidget(m_StatusLabel);

    // Connections
    connect(m_PathEdit, &QLineEdit::textChanged, this, &PathPickerWidget::pathChanged);
    connect(m_BrowseButton, &QPushButton::clicked, this, &PathPickerWidget::onBrowseClicked);
    connect(m_DetectButton, &QPushButton::clicked, this, &PathPickerWidget::onDetectClicked);
}

void PathPickerWidget::refreshBrowseIcon()
{
    if (!m_BrowseButton)
    {
        return;
    }

    QIcon browseIcon;
    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            browseIcon = iconProvider->getIcon("folder_open");
        }
    }

    if (browseIcon.isNull() && style())
    {
        browseIcon = style()->standardIcon(QStyle::SP_DirOpenIcon);
    }

    m_BrowseButton->setIcon(browseIcon);
}

QString PathPickerWidget::path() const
{
    return m_PathEdit->text();
}

void PathPickerWidget::setPath(const QString& path)
{
    m_PathEdit->setText(path);
}

void PathPickerWidget::setPlaceholderText(const QString& text)
{
    m_PathEdit->setPlaceholderText(text);
}

void PathPickerWidget::setFileFilter(const QString& filter)
{
    m_FileFilter = filter;
}

void PathPickerWidget::setDialogTitle(const QString& title)
{
    m_DialogTitle = title;
}

void PathPickerWidget::setDetectEnabled(bool enabled)
{
    m_DetectButton->setVisible(enabled);
}

void PathPickerWidget::setDetectFunction(std::function<QString()> detectFunc)
{
    m_DetectFunction = std::move(detectFunc);
    setDetectEnabled(static_cast<bool>(m_DetectFunction));
}

void PathPickerWidget::setStatus(const QString& text, bool isError)
{
    m_StatusLabel->setText(text);
    m_StatusLabel->setVisible(!text.isEmpty());

    if (isError)
    {
        applyStatusTone(m_StatusLabel, QStringLiteral("error"));
    }
    else
    {
        applyStatusTone(m_StatusLabel, QStringLiteral("success"));
    }
}

void PathPickerWidget::setWarningStatus(const QString& text)
{
    m_StatusLabel->setText(text);
    m_StatusLabel->setVisible(!text.isEmpty());
    applyStatusTone(m_StatusLabel, QStringLiteral("warning"));
}

void PathPickerWidget::clearStatus()
{
    m_StatusLabel->clear();
    m_StatusLabel->setVisible(false);
    m_StatusLabel->setProperty("anStatusTone", QString());
}

void PathPickerWidget::setReadOnly(bool readOnly)
{
    m_PathEdit->setReadOnly(readOnly);
    m_BrowseButton->setEnabled(!readOnly);
    m_DetectButton->setEnabled(!readOnly);
}

void PathPickerWidget::onBrowseClicked()
{
    QString selectedPath;
    const QString currentPath = m_PathEdit->text().trimmed();

    // For directory mode, start in the current directory itself (if it exists)
    // For file mode, start in the parent directory of the current file
    QString startDir;
    if (currentPath.isEmpty())
    {
        startDir = QDir::homePath();
    }
    else
    {
        QString expandedPath = currentPath;
        if (expandedPath.startsWith("~/") || expandedPath == "~")
        {
            expandedPath = QDir::homePath() + expandedPath.mid(1);
        }

        if (m_Mode == Mode::Directory)
        {
            startDir = expandedPath;
        }
        else
        {
            startDir = QFileInfo(expandedPath).absolutePath();
        }

        // Navigate up to the closest existing parent directory
        QDir dir(startDir);
        while (!startDir.isEmpty() && !dir.exists())
        {
            QString parentDir = QFileInfo(startDir).absolutePath();
            if (parentDir == startDir)
            {
                break; // Reached root
            }
            startDir = parentDir;
            dir.setPath(startDir);
        }

        // Ensure the start directory actually exists, otherwise fallback to home
        if (startDir.isEmpty() || !dir.exists())
        {
            startDir = QDir::homePath();
        }
    }

    const QString title = m_DialogTitle.isEmpty()
                              ? (m_Mode == Mode::Directory ? tr("Select Directory") : tr("Select File"))
                              : m_DialogTitle;

    // Use an explicit QFileDialog instance with setDirectoryUrl() instead of
    // the static convenience methods.  KDE's portal-based native dialog
    // (xdg-desktop-portal-kde) does not always honour the plain-string
    // directory argument passed to the static helpers, causing the dialog to
    // open at $HOME.  Setting the directory via a QUrl is handled correctly
    // by the portal D-Bus interface.
    QFileDialog dialog(this, title);
    dialog.setDirectoryUrl(QUrl::fromLocalFile(startDir));
    auto* context = an::ui::appContext();
    an::ui::dialogs::applyDialogBackendPreference(dialog, context ? context->getGlobalSettings() : nullptr);

    switch (m_Mode)
    {
    case Mode::Directory:
        dialog.setFileMode(QFileDialog::Directory);
        dialog.setOption(QFileDialog::ShowDirsOnly, true);
        break;

    case Mode::File:
    case Mode::Executable:
        dialog.setFileMode(QFileDialog::ExistingFile);
        if (!m_FileFilter.isEmpty())
        {
            dialog.setNameFilter(m_FileFilter);
        }
        break;
    }

    if (dialog.exec() == QDialog::Accepted)
    {
        const auto files = dialog.selectedFiles();
        if (!files.isEmpty())
        {
            selectedPath = files.first();
        }
    }

    if (!selectedPath.isEmpty())
    {
        m_PathEdit->setText(selectedPath);
    }
}

void PathPickerWidget::onDetectClicked()
{
    emit detectRequested();

    if (m_DetectFunction)
    {
        const QString detected = m_DetectFunction();
        if (!detected.isEmpty())
        {
            m_PathEdit->setText(detected);
            // Status will be updated by the pathChanged signal handler
        }
        else
        {
            // Show warning that detection failed
            setWarningStatus(tr("⚠ Could not auto-detect. Please select manually."));
        }
    }
}

void PathPickerWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);

    if (!event)
    {
        return;
    }

    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::ThemeChange)
    {
        refreshBrowseIcon();
    }
}
