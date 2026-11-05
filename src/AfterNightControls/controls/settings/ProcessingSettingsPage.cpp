// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProcessingSettingsPage.h"
#include "ProcessingLimits.h"
#include "SettingsKeys.h"
#include "SettingsDefaults.h"
#include "../GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightEngine/Core/EngineLimits.h>

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

#include <algorithm>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#endif

namespace
{

/// The session the page shows the limits of, or null where none is lent.
const an::consumer::EngineSession* pageSession()
{
    auto* context = an::ui::appContext();
    an::ui::IEngineSessionProvider* provider = context ? context->getEngineSessionProvider() : nullptr;
    return provider ? &provider->engineSession() : nullptr;
}

/// The most threads the page offers to choose: every logical core, or 128 where the count is not known.
int offeredThreads(const int logicalCores)
{
    return logicalCores > 0 ? logicalCores : 128;
}

} // namespace

W_OBJECT_IMPL(ProcessingSettingsPage)

ProcessingSettingsPage::ProcessingSettingsPage(GlobalSettingsPersistence* settingsPersistence, QWidget* parent)
    : SettingsPage(parent), m_Settings(settingsPersistence)
{
    // Cache system info at construction time
    m_LogicalCores = getLogicalCoreCount();
    m_TotalMemoryMB = getTotalMemoryMB();

    setupUi();
    connectSignals();
}

QString ProcessingSettingsPage::pageTitle() const
{
    return tr("Processing");
}

QIcon ProcessingSettingsPage::pageIcon() const
{
    return contextIcon(QStringLiteral("stats"));
}

QStringList ProcessingSettingsPage::searchKeywords() const
{
    return QStringList{tr("Processing"), tr("Threads"), tr("CPU"),         tr("Cores"),
                       tr("Parallel"),   tr("Batch"),   tr("Preview"),     tr("Frame rate"),
                       tr("Battery"),    tr("GPU"),     tr("CUDA"),        tr("Acceleration"),
                       tr("Memory"),     tr("RAM"),     tr("Performance"), tr("Restart")};
}

void ProcessingSettingsPage::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    mainLayout->addWidget(createInfoLabel(tr("The thread and memory limits are fixed while AfterNight runs: a change "
                                             "is stored and takes effect the next time it starts. Open images and "
                                             "their history are not affected.")));

    // === Parallelism Section ===
    auto* parallelismGroup = new QGroupBox(tr("Parallelism"), this);
    auto* parallelismLayout = new QVBoxLayout(parallelismGroup);
    parallelismLayout->setSpacing(12);

    auto* batchLayout = new QHBoxLayout();
    m_AutoThreadsBatchCheck = new QCheckBox(tr("Auto"), parallelismGroup);
    m_AutoThreadsBatchCheck->setToolTip(tr("Use every logical core of this computer"));
    batchLayout->addWidget(m_AutoThreadsBatchCheck);

    m_MaxThreadsBatchSpin = new QSpinBox(parallelismGroup);
    m_MaxThreadsBatchSpin->setObjectName(QStringLiteral("processingMaxThreadsSpin"));
    m_MaxThreadsBatchSpin->setRange(1, offeredThreads(m_LogicalCores));
    m_MaxThreadsBatchSpin->setToolTip(tr("The most threads AfterNight's processing uses at once"));
    batchLayout->addWidget(m_MaxThreadsBatchSpin);
    batchLayout->addStretch();

    auto* batchFormLayout = new QFormLayout();
    batchFormLayout->addRow(tr("Max Threads:"), batchLayout);
    parallelismLayout->addLayout(batchFormLayout);

    m_BatchThreadsInfoLabel = new QLabel(parallelismGroup);
    m_BatchThreadsInfoLabel->setObjectName(QStringLiteral("processingThreadsInUseLabel"));
    applySecondaryTextStyle(m_BatchThreadsInfoLabel);
    parallelismLayout->addWidget(m_BatchThreadsInfoLabel);

    m_ThreadsPendingLabel = createWarningLabel(tr("The thread limit takes effect the next time AfterNight starts."));
    m_ThreadsPendingLabel->setObjectName(QStringLiteral("processingThreadsPendingLabel"));
    parallelismLayout->addWidget(m_ThreadsPendingLabel);

    m_PreviewThreadsInfoLabel = new QLabel(parallelismGroup);
    m_PreviewThreadsInfoLabel->setObjectName(QStringLiteral("processingPreviewThreadsLabel"));
    m_PreviewThreadsInfoLabel->setWordWrap(true);
    applySecondaryTextStyle(m_PreviewThreadsInfoLabel);
    parallelismLayout->addWidget(m_PreviewThreadsInfoLabel);

    auto* coresInfo = createInfoLabel(tr("Detected logical CPU cores: %1").arg(m_LogicalCores));
    parallelismLayout->addWidget(coresInfo);

    mainLayout->addWidget(parallelismGroup);

    // === Real-time Preview Section ===
    auto* previewGroup = new QGroupBox(tr("Real-time Preview"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->setSpacing(12);

    m_PreviewFrameRateSpin = new QSpinBox(previewGroup);
    m_PreviewFrameRateSpin->setObjectName(QStringLiteral("processingPreviewFrameRateSpin"));
    m_PreviewFrameRateSpin->setRange(static_cast<int>(an::ui::kMinPreviewFrameRate),
                                     static_cast<int>(an::ui::kMaxPreviewFrameRate));
    m_PreviewFrameRateSpin->setToolTip(
        tr("The most previews a process window shows in a second while you drag one of its sliders"));
    auto* previewFormLayout = new QFormLayout();
    previewFormLayout->addRow(tr("Updates per second while dragging:"), m_PreviewFrameRateSpin);
    previewLayout->addLayout(previewFormLayout);

    previewLayout->addWidget(createInfoLabel(
        tr("Lower values use less CPU and battery while you drag; higher ones follow the slider more closely. Letting "
           "go runs one more preview with every thread a preview may use. A change applies to the next preview, with "
           "no restart.")));

    mainLayout->addWidget(previewGroup);

    // === GPU Acceleration Section ===
    auto* gpuGroup = new QGroupBox(tr("GPU Acceleration"), this);
    auto* gpuLayout = new QVBoxLayout(gpuGroup);
    gpuLayout->setSpacing(12);

    m_GpuEnabledCheck = new QCheckBox(tr("Enable GPU Acceleration"), gpuGroup);
    m_GpuEnabledCheck->setToolTip(tr("Use GPU for compatible operations (FFT, convolutions, AI tools)"));
    m_GpuEnabledCheck->setEnabled(false); // Not yet implemented
    gpuLayout->addWidget(m_GpuEnabledCheck);

    auto* gpuComboLayout = new QFormLayout();
    m_PreferredGpuCombo = new QComboBox(gpuGroup);
    m_PreferredGpuCombo->addItem(tr("Auto (Use Best Available)"), "auto");
    m_PreferredGpuCombo->setEnabled(false); // Not yet implemented
    gpuComboLayout->addRow(tr("Preferred GPU:"), m_PreferredGpuCombo);
    gpuLayout->addLayout(gpuComboLayout);

    auto* gpuInfo = createInfoLabel(tr("GPU acceleration is planned for a future release."));
    gpuLayout->addWidget(gpuInfo);

    auto* gpuNote = createWarningLabel(
        tr("Currently, GPU is only used by extension-provided AI tools, which have their own GPU settings."));
    gpuLayout->addWidget(gpuNote);

    mainLayout->addWidget(gpuGroup);

    // === Memory Section ===
    auto* memoryGroup = new QGroupBox(tr("Memory"), this);
    auto* memoryLayout = new QVBoxLayout(memoryGroup);
    memoryLayout->setSpacing(12);

    auto* memoryInputLayout = new QHBoxLayout();
    m_AutoMemoryCheck = new QCheckBox(tr("Auto"), memoryGroup);
    m_AutoMemoryCheck->setToolTip(tr("Set no memory budget of AfterNight's own"));
    memoryInputLayout->addWidget(m_AutoMemoryCheck);

    m_MemoryLimitSpin = new QSpinBox(memoryGroup);
    m_MemoryLimitSpin->setObjectName(QStringLiteral("processingMemoryLimitSpin"));
    m_MemoryLimitSpin->setRange(512, static_cast<int>(m_TotalMemoryMB > 0 ? m_TotalMemoryMB : 65536));
    m_MemoryLimitSpin->setSuffix(tr(" MB"));
    m_MemoryLimitSpin->setSingleStep(1024);
    m_MemoryLimitSpin->setToolTip(tr("The most memory the engine holds for images at once"));
    memoryInputLayout->addWidget(m_MemoryLimitSpin);
    memoryInputLayout->addStretch();

    auto* memoryFormLayout = new QFormLayout();
    memoryFormLayout->addRow(tr("Memory Limit:"), memoryInputLayout);
    memoryLayout->addLayout(memoryFormLayout);

    m_MemoryInfoLabel = new QLabel(memoryGroup);
    m_MemoryInfoLabel->setObjectName(QStringLiteral("processingMemoryInUseLabel"));
    applySecondaryTextStyle(m_MemoryInfoLabel);
    memoryLayout->addWidget(m_MemoryInfoLabel);

    m_MemoryPendingLabel = createWarningLabel(tr("The memory limit takes effect the next time AfterNight starts."));
    m_MemoryPendingLabel->setObjectName(QStringLiteral("processingMemoryPendingLabel"));
    memoryLayout->addWidget(m_MemoryPendingLabel);

    auto* memoryInfo = createInfoLabel(
        tr("Total system RAM: %1 MB (%2 GB)").arg(m_TotalMemoryMB).arg(m_TotalMemoryMB / 1024.0, 0, 'f', 1));
    memoryLayout->addWidget(memoryInfo);

    auto* memoryWarning = createWarningLabel(
        tr("A limit too low refuses the stacking or processing of large images when they would not fit; the refusal "
           "names the memory asked for and the limit."));
    memoryLayout->addWidget(memoryWarning);

    mainLayout->addWidget(memoryGroup);

    // Add stretch to push content to top
    mainLayout->addStretch(1);

    updateLimitLabels();
}

void ProcessingSettingsPage::connectSignals()
{
    connect(m_AutoThreadsBatchCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_MaxThreadsBatchSpin->setEnabled(!checked);
        updateLimitLabels();
        markAsModified();
    });
    connect(m_MaxThreadsBatchSpin, &QSpinBox::valueChanged, this, [this]() { updateLimitLabels(); });
    trackChanges(m_MaxThreadsBatchSpin);

    // GPU controls (currently disabled, but wire up for future)
    trackChanges(m_GpuEnabledCheck);
    trackChanges(m_PreferredGpuCombo);

    connect(m_AutoMemoryCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_MemoryLimitSpin->setEnabled(!checked);
        updateLimitLabels();
        markAsModified();
    });
    connect(m_MemoryLimitSpin, &QSpinBox::valueChanged, this, [this]() { updateLimitLabels(); });
    trackChanges(m_MemoryLimitSpin);

    trackChanges(m_PreviewFrameRateSpin);
}

an::ui::ProcessingLimits ProcessingSettingsPage::stagedLimits() const
{
    an::ui::ProcessingLimits limits;
    limits.threads =
        m_AutoThreadsBatchCheck->isChecked() ? 0u : static_cast<std::uint32_t>(m_MaxThreadsBatchSpin->value());
    limits.memoryLimitMegabytes =
        m_AutoMemoryCheck->isChecked() ? 0u : static_cast<std::uint64_t>(m_MemoryLimitSpin->value());
    return limits;
}

std::uint32_t ProcessingSettingsPage::stagedPreviewFrameRate() const
{
    return static_cast<std::uint32_t>(m_PreviewFrameRateSpin->value());
}

void ProcessingSettingsPage::loadSettings()
{
    auto blocker = blockSignalsDuringLoad();

    if (!m_Settings)
    {
        resetToDefaults();
        return;
    }

    const an::ui::ProcessingLimits limits = an::ui::readProcessingLimits(
        [this](const QString& key, const QString& fallback) { return m_Settings->select(key, fallback); });

    // A limit stored above this computer's logical cores - settings brought from a larger computer - is the one the
    // session builds with at the next start, so the box admits it rather than show the core count in its place.
    m_MaxThreadsBatchSpin->setMaximum((std::max)(offeredThreads(m_LogicalCores), static_cast<int>(limits.threads)));
    m_AutoThreadsBatchCheck->setChecked(limits.threads == 0u);
    m_MaxThreadsBatchSpin->setValue(limits.threads == 0u ? m_LogicalCores : static_cast<int>(limits.threads));
    m_MaxThreadsBatchSpin->setEnabled(limits.threads != 0u);

    m_PreviewFrameRateSpin->setValue(static_cast<int>(an::ui::readPreviewFrameRate(
        [this](const QString& key, const QString& fallback) { return m_Settings->select(key, fallback); })));

    // GPU enabled (placeholder for future)
    QString gpuEnabledStr =
        m_Settings->select(SettingsKeys::PROCESSING_GPU_ENABLED, SettingsDefaults::PROCESSING_GPU_ENABLED ? "1" : "0");
    m_GpuEnabledCheck->setChecked(gpuEnabledStr == "1");

    // Preferred GPU (placeholder for future)
    QString preferredGpu =
        m_Settings->select(SettingsKeys::PROCESSING_PREFERRED_GPU, SettingsDefaults::PROCESSING_PREFERRED_GPU);
    int gpuIndex = m_PreferredGpuCombo->findData(preferredGpu);
    m_PreferredGpuCombo->setCurrentIndex(gpuIndex >= 0 ? gpuIndex : 0);

    m_AutoMemoryCheck->setChecked(limits.memoryLimitMegabytes == 0u);
    m_MemoryLimitSpin->setValue(
        limits.memoryLimitMegabytes == 0u
            ? static_cast<int>(m_TotalMemoryMB > 0 ? (m_TotalMemoryMB * 3) / 4 : 8192)
            : static_cast<int>(
                  (std::min)(limits.memoryLimitMegabytes, static_cast<std::uint64_t>(m_MemoryLimitSpin->maximum()))));
    m_MemoryLimitSpin->setEnabled(limits.memoryLimitMegabytes != 0u);

    updateLimitLabels();
    clearModified();
}

void ProcessingSettingsPage::saveSettings()
{
    if (!m_Settings)
    {
        return;
    }

    // Stored for the next start: the session in use keeps the limits it was built with.
    an::ui::writeProcessingLimits(stagedLimits(),
                                  [this](const QString& key, const QString& value) { m_Settings->upsert(key, value); });

    // Read when a process window paces its next frame, so it applies at once.
    an::ui::writePreviewFrameRate(stagedPreviewFrameRate(),
                                  [this](const QString& key, const QString& value) { m_Settings->upsert(key, value); });

    // GPU settings
    m_Settings->upsert(SettingsKeys::PROCESSING_GPU_ENABLED, m_GpuEnabledCheck->isChecked() ? "1" : "0");
    m_Settings->upsert(SettingsKeys::PROCESSING_PREFERRED_GPU, m_PreferredGpuCombo->currentData().toString());

    updateLimitLabels();
    clearModified();
}

void ProcessingSettingsPage::resetToDefaults()
{
    auto blocker = blockSignalsDuringLoad();

    m_AutoThreadsBatchCheck->setChecked(SettingsDefaults::PROCESSING_MAX_THREADS_BATCH == 0);
    m_MaxThreadsBatchSpin->setMaximum(offeredThreads(m_LogicalCores));
    m_MaxThreadsBatchSpin->setValue(m_LogicalCores);
    m_MaxThreadsBatchSpin->setEnabled(SettingsDefaults::PROCESSING_MAX_THREADS_BATCH != 0);

    m_PreviewFrameRateSpin->setValue(SettingsDefaults::PROCESSING_PREVIEW_FRAME_RATE);

    // GPU settings
    m_GpuEnabledCheck->setChecked(SettingsDefaults::PROCESSING_GPU_ENABLED);
    int gpuIndex = m_PreferredGpuCombo->findData(QString(SettingsDefaults::PROCESSING_PREFERRED_GPU));
    m_PreferredGpuCombo->setCurrentIndex(gpuIndex >= 0 ? gpuIndex : 0);

    m_AutoMemoryCheck->setChecked(SettingsDefaults::PROCESSING_MEMORY_LIMIT_MB == 0);
    qint64 autoLimit = (m_TotalMemoryMB * 3) / 4;
    m_MemoryLimitSpin->setValue(static_cast<int>(autoLimit > 0 ? autoLimit : 8192));
    m_MemoryLimitSpin->setEnabled(SettingsDefaults::PROCESSING_MEMORY_LIMIT_MB != 0);

    updateLimitLabels();
    markAsModified();
}

ValidationResult ProcessingSettingsPage::validateSettings() const
{
    if (!m_AutoMemoryCheck->isChecked())
    {
        int memoryLimit = m_MemoryLimitSpin->value();
        if (memoryLimit < 1024)
        {
            return ValidationResult::invalid(
                tr("Memory limit of %1 MB is too low. Recommended minimum is 1024 MB.").arg(memoryLimit),
                "memoryLimit");
        }
    }

    return ValidationResult::valid();
}

int ProcessingSettingsPage::getLogicalCoreCount() const
{
    unsigned int cores = std::thread::hardware_concurrency();
    return cores > 0 ? static_cast<int>(cores) : 4; // Default to 4 if detection fails
}

qint64 ProcessingSettingsPage::getTotalMemoryMB() const
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo))
    {
        return static_cast<qint64>(memInfo.ullTotalPhys / (1024 * 1024));
    }
    return 8192; // Default to 8 GB if detection fails
#elif defined(Q_OS_LINUX)
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0)
    {
        return static_cast<qint64>((memInfo.totalram * memInfo.mem_unit) / (1024 * 1024));
    }
    return 8192;
#else
    return 8192; // Default for unknown platforms
#endif
}

void ProcessingSettingsPage::updateLimitLabels()
{
    const an::consumer::EngineSession* session = pageSession();
    if (session == nullptr || session->width() == 0u)
    {
        m_BatchThreadsInfoLabel->setText(tr("No engine session is running."));
        m_MemoryInfoLabel->setText(QString());
        m_PreviewThreadsInfoLabel->setText(QString());
        m_ThreadsPendingLabel->setVisible(false);
        m_MemoryPendingLabel->setVisible(false);
        return;
    }

    const an::ui::ProcessingLimitsState state = an::ui::processingLimitsState(*session, stagedLimits());
    m_BatchThreadsInfoLabel->setText(tr("In use now: %1 threads.").arg(state.width));
    m_ThreadsPendingLabel->setVisible(state.threadsPendingRestart);

    if (state.previewThreads == 0u)
    {
        m_PreviewThreadsInfoLabel->setText(
            tr("Previews: with one thread, a preview waits its turn with every other operation."));
    }
    else
    {
        m_PreviewThreadsInfoLabel->setText(tr("Previews: while a preview is open, %1 of the %2 threads are kept for "
                                              "previews, and a preview also uses every thread no other work needs; "
                                              "when none is open, all of them are available to processing.")
                                               .arg(state.previewThreads)
                                               .arg(state.width));
    }

    if (state.memoryBudgetBytes == an::kDefaultMemoryBudgetBytes)
    {
        m_MemoryInfoLabel->setText(tr("In use now: no memory limit."));
    }
    else
    {
        m_MemoryInfoLabel->setText(
            tr("In use now: %1 MB.").arg(static_cast<qulonglong>(state.memoryBudgetBytes / (1024u * 1024u))));
    }
    m_MemoryPendingLabel->setVisible(state.memoryPendingRestart);
}

void ProcessingSettingsPage::refreshStyles()
{
    SettingsPage::refreshStyles();
    applySecondaryTextStyle(m_BatchThreadsInfoLabel);
    applySecondaryTextStyle(m_PreviewThreadsInfoLabel);
    applySecondaryTextStyle(m_MemoryInfoLabel);
}
