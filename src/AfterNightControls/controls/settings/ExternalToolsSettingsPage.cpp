// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExternalToolsSettingsPage.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "PathPickerWidget.h"
#include "DeepSNRCliTools.h"
#include "StarNetCliTools.h"
#include "../GlobalSettingsPersistence.h"
#include "theme/AnDesignTokens.h"

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QThreadPool>
#include <QMetaObject>
#include <QPointer>

W_OBJECT_IMPL(ExternalToolsSettingsPage)

namespace
{
enum class ExternalTool
{
    StarNet2,
    DeepSNR
};

/// Check if a path points to a valid executable
bool isValidExecutable(const QString& path, ExternalTool tool)
{
    if (path.isEmpty())
    {
        return false;
    }
    QFileInfo info(path);
    if (!info.exists() || !info.isExecutable())
    {
        return false;
    }

    switch (tool)
    {
    case ExternalTool::StarNet2:
        return an::ui::starnet::checkExecutable(path, 3000).supported;
    case ExternalTool::DeepSNR:
        return an::ui::deepsnr::checkExecutable(path, 3000).supported;
    }

    return false;
}

enum class ExecutableValidationState
{
    Valid,
    Missing,
    NotExecutable
};

ExecutableValidationState validateExecutablePath(const QString& path)
{
    if (path.isEmpty())
    {
        return ExecutableValidationState::Missing;
    }

    QFileInfo info(path);
    if (!info.exists())
    {
        return ExecutableValidationState::Missing;
    }

    if (!info.isExecutable())
    {
        return ExecutableValidationState::NotExecutable;
    }

    return ExecutableValidationState::Valid;
}

} // namespace

ExternalToolsSettingsPage::ExternalToolsSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    setupUi();
    connectSignals();
}

QString ExternalToolsSettingsPage::pageTitle() const
{
    return tr("External Tools");
}

QIcon ExternalToolsSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("computer"));
}

QStringList ExternalToolsSettingsPage::searchKeywords() const
{
    return QStringList{
        tr("External"), tr("Tools"), tr("StarNet"), tr("Star Removal"), tr("DeepSNR"), tr("Noise Reduction"),
    };
}

void ExternalToolsSettingsPage::setupUi()
{
    const auto& t = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t.spacing.lg);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // === AI CLI Tools Section ===
    auto* aiToolsGroup = new QGroupBox(tr("AI Star Removal and Noise Reduction"), this);
    auto* aiToolsLayout = new QFormLayout(aiToolsGroup);
    aiToolsLayout->setSpacing(t.spacing.md);

    // StarNet2 path picker
    m_StarnetPathPicker = new PathPickerWidget(PathPickerWidget::Mode::Executable, aiToolsGroup);
    m_StarnetPathPicker->setPlaceholderText(tr("Path to StarNet2 CLI executable..."));
    m_StarnetPathPicker->setDialogTitle(tr("Select StarNet2 Executable"));
#ifdef _WIN32
    m_StarnetPathPicker->setFileFilter(tr("Executable files (*.exe);;All Files (*)"));
#else
    m_StarnetPathPicker->setFileFilter(tr("All Files (*)"));
#endif
    m_StarnetPathPicker->setDetectFunction([this]() { return detectStarnet(); });
    aiToolsLayout->addRow(tr("StarNet2:"), m_StarnetPathPicker);

    auto* starnetInfo = createInfoLabel(
        tr("StarNet2 CLI %1 or newer is required. Current installers provide starnet2, ONNX Runtime/CoreML models, "
           "and native mask/unscreen outputs.")
            .arg(an::ui::starnet::minimumSupportedVersionText()));
    aiToolsLayout->addRow("", starnetInfo);

    // DeepSNR path picker
    m_DeepSNRPathPicker = new PathPickerWidget(PathPickerWidget::Mode::Executable, aiToolsGroup);
    m_DeepSNRPathPicker->setPlaceholderText(tr("Path to DeepSNR CLI executable..."));
    m_DeepSNRPathPicker->setDialogTitle(tr("Select DeepSNR Executable"));
#ifdef _WIN32
    m_DeepSNRPathPicker->setFileFilter(tr("Executable files (*.exe);;All Files (*)"));
#else
    m_DeepSNRPathPicker->setFileFilter(tr("All Files (*)"));
#endif
    m_DeepSNRPathPicker->setDetectFunction([this]() { return detectDeepSNR(); });
    aiToolsLayout->addRow(tr("DeepSNR:"), m_DeepSNRPathPicker);

    auto* deepsnrInfo = createInfoLabel(
        tr("DeepSNR CLI %1 or newer is required. Current installers provide deepsnr and model weights in the DeepSNR "
           "support folder.")
            .arg(an::ui::deepsnr::minimumSupportedVersionText()));
    aiToolsLayout->addRow("", deepsnrInfo);

    mainLayout->addWidget(aiToolsGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);
}

void ExternalToolsSettingsPage::connectSignals()
{
    // Track changes for path pickers
    connect(m_StarnetPathPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateStarnetStatus();
    });
    connect(m_DeepSNRPathPicker, &PathPickerWidget::pathChanged, this, [this]() {
        markAsModified();
        updateDeepSNRStatus();
    });
}

void ExternalToolsSettingsPage::disconnectSignals()
{
    m_StarnetPathPicker->disconnect(this);
    m_DeepSNRPathPicker->disconnect(this);
}

void ExternalToolsSettingsPage::loadSettings()
{
    // Disconnect signals to prevent expensive version checks during load
    disconnectSignals();

    if (!m_Settings)
    {
        resetToDefaultsInternal();
        connectSignals(); // Reconnect after reset
        return;
    }

    // StarNet2
    QString starnetPath = m_Settings->select(SettingsKeys::TOOLS_STARNET_PATH, QString());
    m_StarnetPathPicker->setPath(starnetPath);

    // DeepSNR
    QString deepSNRPath = m_Settings->select(SettingsKeys::TOOLS_DEEPSNR_PATH, QString());
    m_DeepSNRPathPicker->setPath(deepSNRPath);
    // Note: updateStarnetStatus() not called during load to avoid blocking (executes tool)

    // Reconnect signals after loading
    connectSignals();

    clearModified();

    // Trigger async validation in background to avoid blocking
    triggerAsyncValidation();
}

void ExternalToolsSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    // StarNet2
    m_Settings->upsert(SettingsKeys::TOOLS_STARNET_PATH, m_StarnetPathPicker->path());

    // DeepSNR
    m_Settings->upsert(SettingsKeys::TOOLS_DEEPSNR_PATH, m_DeepSNRPathPicker->path());

    clearModified();
}

void ExternalToolsSettingsPage::resetToDefaults()
{
    disconnectSignals();
    resetToDefaultsInternal();
    connectSignals();
}

void ExternalToolsSettingsPage::resetToDefaultsInternal()
{
    m_StarnetPathPicker->setPath(QString(SettingsDefaults::TOOLS_STARNET_PATH));
    m_StarnetPathPicker->clearStatus();
    m_DeepSNRPathPicker->setPath(QString(SettingsDefaults::TOOLS_DEEPSNR_PATH));
    m_DeepSNRPathPicker->clearStatus();

    markAsModified();
}

ValidationResult ExternalToolsSettingsPage::validateSettings() const
{
    // External tool paths are optional - we don't block saving even if paths are invalid.
    // The PathPickerWidget status labels already show users which tools are misconfigured.
    // Only block saving if there's a critical configuration error (none currently).

    return ValidationResult::valid();
}

// === Detection Functions ===

QString ExternalToolsSettingsPage::detectStarnet() const
{
    // First try to find in PATH
    for (const QString& executableName : an::ui::starnet::executableNames())
    {
        const QString pathResult = QStandardPaths::findExecutable(executableName);
        if (!pathResult.isEmpty() && isValidExecutable(pathResult, ExternalTool::StarNet2))
        {
            return pathResult;
        }
    }

    // Check common installation directories
    for (const QString& searchDir : an::ui::starnet::commonInstallDirectories())
    {
        for (const QString& executableName : an::ui::starnet::executableNames())
        {
            const QString candidate = QDir(searchDir).filePath(executableName);
            if (isValidExecutable(candidate, ExternalTool::StarNet2))
            {
                return candidate;
            }
        }
    }

    return {};
}

QString ExternalToolsSettingsPage::detectDeepSNR() const
{
    // First try to find in PATH
    for (const QString& executableName : an::ui::deepsnr::executableNames())
    {
        const QString pathResult = QStandardPaths::findExecutable(executableName);
        if (!pathResult.isEmpty() && isValidExecutable(pathResult, ExternalTool::DeepSNR))
        {
            return pathResult;
        }
    }

    // Check common installation directories
    for (const QString& searchDir : an::ui::deepsnr::commonInstallDirectories())
    {
        for (const QString& executableName : an::ui::deepsnr::executableNames())
        {
            const QString candidate = QDir(searchDir).filePath(executableName);
            if (isValidExecutable(candidate, ExternalTool::DeepSNR))
            {
                return candidate;
            }
        }
    }

    return {};
}

// === Status Update Functions ===

void ExternalToolsSettingsPage::updateStarnetStatus()
{
    QString path = m_StarnetPathPicker->path();
    if (path.isEmpty())
    {
        m_StarnetPathPicker->clearStatus();
        return;
    }

    const ExecutableValidationState validation = validateExecutablePath(path);
    if (validation == ExecutableValidationState::Valid)
    {
        m_StarnetPathPicker->setStatus(tr("Checking StarNet2 version..."), false);

        QPointer<ExternalToolsSettingsPage> self(this);
        QThreadPool::globalInstance()->start([self, path]() {
            if (!self)
            {
                return;
            }

            const auto check = an::ui::starnet::checkExecutable(path);
            if (!self)
            {
                return;
            }

            QMetaObject::invokeMethod(
                self.data(),
                [self, check, path]() {
                    if (!self || self->m_StarnetPathPicker->path() != path)
                    {
                        return;
                    }

                    if (check.supported)
                    {
                        self->m_StarnetPathPicker->setStatus(self->tr("✓ StarNet2 %1 detected").arg(check.versionText),
                                                             false);
                    }
                    else
                    {
                        self->m_StarnetPathPicker->setStatus(check.detail, true);
                    }
                },
                Qt::QueuedConnection);
        });
    }
    else if (validation == ExecutableValidationState::NotExecutable)
    {
        m_StarnetPathPicker->setStatus(tr("✗ Path is not executable"), true);
    }
    else
    {
        m_StarnetPathPicker->setStatus(tr("✗ Executable not found"), true);
    }
}

void ExternalToolsSettingsPage::updateDeepSNRStatus()
{
    QString path = m_DeepSNRPathPicker->path();
    if (path.isEmpty())
    {
        m_DeepSNRPathPicker->clearStatus();
        return;
    }

    const ExecutableValidationState validation = validateExecutablePath(path);
    if (validation == ExecutableValidationState::Valid)
    {
        m_DeepSNRPathPicker->setStatus(tr("Checking DeepSNR version..."), false);

        QPointer<ExternalToolsSettingsPage> self(this);
        QThreadPool::globalInstance()->start([self, path]() {
            if (!self)
            {
                return;
            }

            const auto check = an::ui::deepsnr::checkExecutable(path);
            if (!self)
            {
                return;
            }

            QMetaObject::invokeMethod(
                self.data(),
                [self, check, path]() {
                    if (!self || self->m_DeepSNRPathPicker->path() != path)
                    {
                        return;
                    }

                    if (check.supported)
                    {
                        self->m_DeepSNRPathPicker->setStatus(self->tr("✓ DeepSNR %1 detected").arg(check.versionText),
                                                             false);
                    }
                    else
                    {
                        self->m_DeepSNRPathPicker->setStatus(check.detail, true);
                    }
                },
                Qt::QueuedConnection);
        });
    }
    else if (validation == ExecutableValidationState::NotExecutable)
    {
        m_DeepSNRPathPicker->setStatus(tr("✗ Path is not executable"), true);
    }
    else
    {
        m_DeepSNRPathPicker->setStatus(tr("✗ Executable not found"), true);
    }
}

void ExternalToolsSettingsPage::triggerAsyncValidation()
{
    updateStarnetStatus();
    updateDeepSNRStatus();
}
