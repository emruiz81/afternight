// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Stacking window: the shared file list, the groups it makes, the stacking settings, a preview of the
/// selected file, and the run that stacks every group on the thread pool.

#include "stdafx.hpp"
#include "ImageStackingProcess.h"

#include "app/AppContextHelpers.h"
#include "app/DemosaicTextMaps.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TableWidgetUtils.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/WorkflowToolFiles.h"
#include "processes/core/StandaloneStackingSettingsPersistence.h"
#include "processes/core/WorkflowToolCompanions.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Stacking/StackRejectionParameters.h>
#include <AfterNightEngine/Core/ErrorCode.h>

#include <AfterNightPlatform/Logging.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidgetItem>
#include <QThreadPool>

#include <nlohmann/json.hpp>

#include <cmath>
#include <stop_token>
#include <utility>

using an::ui::StandaloneStackingFrame;
using an::ui::StandaloneStackingGroup;
using an::ui::processes::StandaloneStackRun;
using an::ui::processes::WorkflowFile;
using an::ui::processes::WorkflowFileColumn;
using an::ui::processes::WorkflowFileList;

namespace
{

constexpr const char* kFilesContext = "__IMAGE_STACK_ADD_FILES__";

/// The persisted settings' version: 5 lists the frames beside the parameters.
constexpr int kSettingsVersion = 5;

const QString kDefaultTemplate = QStringLiteral("stacked_$filter.fit");

/// The largest threshold the rejection boxes offer: any positive one is a threshold the stack reads.
constexpr double kMaximumRejectionThreshold = 1000.0;

/// The largest number the boxes offer the generalized ESD test, whose two fractions lie strictly below one: the
/// largest the boxes' three decimals spell.
constexpr double kMaximumGesdtValue = 0.999;

/// A table item ordered by a number rather than its text.
class NumericSortItem final : public QTableWidgetItem
{
public:
    NumericSortItem(const QString& text, const double sortValue) : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, sortValue);
        setFlags(flags() & ~Qt::ItemIsEditable);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
    }
};

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// The weighting text the settings record states for the dialog's choice.
[[nodiscard]] QString weightingSettingsText(const QString& text)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("none (equal)") || normalized == QStringLiteral("none"))
    {
        return QStringLiteral("None");
    }
    if (normalized == QStringLiteral("fwhm"))
    {
        return QStringLiteral("Weighted FWHM");
    }
    if (normalized == QStringLiteral("stars"))
    {
        return QStringLiteral("Number of stars");
    }
    return text.trimmed();
}

[[nodiscard]] QString formatTotalExposure(const double seconds)
{
    if (!(seconds > 0.0))
    {
        return QStringLiteral("-");
    }
    const bool whole = std::abs(seconds - std::round(seconds)) < 1e-6;
    return QStringLiteral("%1 s").arg(seconds, 0, 'f', whole ? 0 : 1);
}

void setComboText(QComboBox* combo, const QString& text)
{
    const int index = combo->findText(text);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

/// A file as the grouping reads it: its filter, when it was taken and its exposure.
[[nodiscard]] StandaloneStackingFrame stackingFrameOf(const WorkflowFile& file)
{
    return StandaloneStackingFrame{.entry = file.entry,
                                   .filter = file.figures.filter(),
                                   .capturedAtUtc = file.figures.capturedAtUtc(),
                                   .exposureSeconds = file.figures.exposureSeconds()};
}

} // namespace

struct ImageStackingProcess::Impl
{
    an::ui::processes::ImageStackingProcessUiElements ui;
    ImageViewControl* preview = nullptr;
    std::unique_ptr<WorkflowFileList> files;
    std::vector<StandaloneStackingGroup> groups;
    LocalNormalizationSettings localNormalization = an::ui::standaloneStackingDefaultLocalNormalizationSettings();
    QString lastRejectionMode;
    QStringList lastWritten;

    bool running = false;
    std::uint64_t bannerGeneration = 0u;
    std::uint32_t bannersInFlight = 0u;
    QPointer<ProgressDialogControl> progress;
};

ImageStackingProcess::ImageStackingProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(parent);
    m_impl->ui = an::ui::processes::buildImageStackingProcessUi(this);
    setProcessDescription(QStringLiteral("Stack groups of frames with rejection, weighting and normalization."));
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto& ui = m_impl->ui;
    ui.stackMethodSelector->addItems({QStringLiteral("Average"), QStringLiteral("Sum"), QStringLiteral("Maximum"),
                                      QStringLiteral("Minimum"), QStringLiteral("Median"),
                                      QStringLiteral("Auto-Adaptive Weighted Average")});
    ui.weightingSelector->addItems({QStringLiteral("None (Equal)"), QStringLiteral("FWHM"), QStringLiteral("Noise"),
                                    QStringLiteral("SNR"), QStringLiteral("Stars"), QStringLiteral("Exposure")});
    ui.normalizationMode->addItems({QStringLiteral("None"), QStringLiteral("Additive"),
                                    QStringLiteral("Multiplicative"), QStringLiteral("Additive With Scaling"),
                                    QStringLiteral("Multiplicative With Scaling"), QStringLiteral("Local")});
    ui.scaleEstimatorSelector->addItems({QStringLiteral("IKSS"), QStringLiteral("MAD")});
    ui.cometStackingModeSelector->addItems({QStringLiteral("Stars"), QStringLiteral("Comet")});
    ui.rejectionMethodSelector->addItems({QStringLiteral("None"), QStringLiteral("Sigma Clipping"),
                                          QStringLiteral("MAD Clipping"), QStringLiteral("Median Sigma"),
                                          QStringLiteral("Winsorized Sigma Clipping"), QStringLiteral("Percentile"),
                                          QStringLiteral("GESDT"), QStringLiteral("Linear Fit")});
    ui.cfaPattern->addItems({an::ui::kBayerPatternFromImageText, an::ui::kBayerPatternRggbText,
                             an::ui::kBayerPatternBggrText, an::ui::kBayerPatternGbrgText,
                             an::ui::kBayerPatternGrbgText});
    // The methods the engine carries; a persisted name the table does not hold reads as RCD.
    ui.debayerMethod->addItems({process_utils::DEMOSAIC_BILINEAR, process_utils::DEMOSAIC_VNG4,
                                process_utils::DEMOSAIC_AHD, process_utils::DEMOSAIC_AMAZE,
                                process_utils::DEMOSAIC_LMMSE, process_utils::DEMOSAIC_RCD});

    ui.groupTable->setAlternatingRowColors(true);
    ui.groupTable->setColumnCount(6);
    ui.groupTable->setHeaderLabels({QStringLiteral("Group"), QStringLiteral("Frames"), QStringLiteral("Filter"),
                                    QStringLiteral("Session"), QStringLiteral("Total Exposure"),
                                    QStringLiteral("Output Name")});
    ui.groupTable->setupSelectionColors();

    QObject::connect(ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageStackingProcess::on_dialog_box_button_clicked);
    QObject::connect(ui.addFiles, &QPushButton::released, this, &ImageStackingProcess::on_addFilesButtonPushed);
    QObject::connect(ui.addFolder, &QPushButton::released, this, &ImageStackingProcess::on_addFolderButtonPushed);
    QObject::connect(ui.removeSelected, &QPushButton::released, this,
                     [this]() { removeItems(process_utils::selectedFilesFromTable(m_impl->ui.tableWidget)); });
    QObject::connect(ui.moveUpButton, &QPushButton::released, this, [this]() { m_impl->files->moveSelected(-1); });
    QObject::connect(ui.moveDownButton, &QPushButton::released, this, [this]() { m_impl->files->moveSelected(1); });
    QObject::connect(ui.clearAll, &QPushButton::released, this, [this]() {
        if (!m_impl->running)
        {
            m_impl->files->clear();
        }
    });
    QObject::connect(ui.stackImagesButton, &QPushButton::released, this, &ImageStackingProcess::stack);
    QObject::connect(ui.outputDirectoryButton, &QPushButton::released, this,
                     &ImageStackingProcess::on_outputDirectoryButtonPushed);
    QObject::connect(ui.localNormalizationConfigureButton, &QPushButton::released, this,
                     &ImageStackingProcess::on_localNormalizationConfigureButtonPushed);
    QObject::connect(ui.stackMethodSelector, &QComboBox::currentTextChanged, this, [this]() { on_stackModeChanged(); });
    QObject::connect(ui.rejectionMethodSelector, &QComboBox::currentTextChanged, this,
                     [this]() { on_rejectionModeChanged(); });
    QObject::connect(ui.normalizationMode, &QComboBox::currentTextChanged, this,
                     [this]() { on_normalizationModeChanged(); });
    QObject::connect(ui.weightingSelector, &QComboBox::currentTextChanged, this, [this]() { updateWeightingBanner(); });
    for (QCheckBox* box : {ui.groupByFilter, ui.groupBySession, ui.groupByExposure})
    {
        QObject::connect(box, &QCheckBox::toggled, this, [this]() { rebuildGroupTable(); });
    }
    QObject::connect(ui.exposureTolerancePercentValue, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this]() { rebuildGroupTable(); });
    QObject::connect(ui.outputImageNameEdit, &QLineEdit::textChanged, this, [this]() { rebuildGroupTable(); });
    QObject::connect(ui.outputDirectoryEdit, &QLineEdit::textChanged, this, [this]() { rebuildGroupTable(); });
    QObject::connect(ui.createRejectionMapsCheck, &QCheckBox::toggled, this, [this]() { updateControls(); });
    QObject::connect(ui.mergeRejectionMapsCheck, &QCheckBox::toggled, this,
                     [this]() { updateRejectionMapsPathLabel(); });

    m_impl->preview = process_utils::createStaticPreviewInPlaceholder(ui.imagePreviewWidget, "Image Preview");
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
    }

    an::ui::processes::WorkflowFileListOptions options;
    options.columns = {{QStringLiteral("File"), WorkflowFileColumn::File},
                       {QStringLiteral("Date"), WorkflowFileColumn::Date},
                       {QStringLiteral("Size"), WorkflowFileColumn::Size},
                       {QStringLiteral("Filter"), WorkflowFileColumn::Filter},
                       {QStringLiteral("ISO/Gain"), WorkflowFileColumn::IsoGain},
                       {QStringLiteral("Offset"), WorkflowFileColumn::Offset},
                       {QStringLiteral("Exposure"), WorkflowFileColumn::Exposure},
                       {QStringLiteral("CFA"), WorkflowFileColumn::Cfa},
                       {QStringLiteral("Depth"), WorkflowFileColumn::Depth}};
    options.busy = [this]() { return m_impl->running; };
    options.stop = windowStopToken();
    m_impl->files = std::make_unique<WorkflowFileList>(
        this,
        an::ui::processes::WorkflowFileListWidgets{.table = ui.tableWidget,
                                                   .removeSelected = ui.removeSelected,
                                                   .moveUp = ui.moveUpButton,
                                                   .moveDown = ui.moveDownButton,
                                                   .clearAll = ui.clearAll,
                                                   .currentLabel = ui.currentImageLabel,
                                                   .totalLabel = ui.totalImagesLabel},
        std::move(options));
    m_impl->files->onChanged = [this]() { listChanged(); };
    m_impl->files->onSelected = [this](int row) { rowSelected(row); };
    m_impl->files->onImportFinished = [this](const an::ui::processes::WorkflowImportReport& report) {
        an::ui::processes::sayWorkflowImport(this, report);
    };

    resetSettings();
    initializeTopLevelProcess();
}

ImageStackingProcess::~ImageStackingProcess() = default;

std::string ImageStackingProcess::getProcessNameStatic()
{
    return "Image Stacking";
}

void ImageStackingProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
    if (m_impl->preview != nullptr)
    {
        m_impl->preview->refreshTheme();
    }
    m_impl->ui.tableWidget->setupSelectionColors();
    m_impl->ui.groupTable->setupSelectionColors();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_impl->ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        m_impl->ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
    }
}

void ImageStackingProcess::addItemsNonBlocking(const QStringList& items)
{
    m_impl->files->addFiles(items);
}

void ImageStackingProcess::removeItems(const std::vector<QString>& items)
{
    if (!m_impl->running)
    {
        m_impl->files->remove(items);
    }
}

const std::vector<StandaloneStackingGroup>& ImageStackingProcess::groups() const
{
    return m_impl->groups;
}

const LocalNormalizationSettings& ImageStackingProcess::localNormalizationSettings() const
{
    return m_impl->localNormalization;
}

const WorkflowFileList& ImageStackingProcess::fileList() const
{
    return *m_impl->files;
}

ImageViewControl* ImageStackingProcess::previewView() const
{
    return m_impl->preview;
}

bool ImageStackingProcess::isIdle() const
{
    return m_impl->files->idle() && !m_impl->running && m_impl->bannersInFlight == 0u;
}

const QStringList& ImageStackingProcess::lastWrittenFiles() const
{
    return m_impl->lastWritten;
}

an::ui::processes::ImageStackingProcessUiElements& ImageStackingProcess::ui()
{
    return m_impl->ui;
}

StandaloneStackRun ImageStackingProcess::runSettings() const
{
    const auto& ui = m_impl->ui;
    StackingSettings settings;
    settings.stackMethod = ui.stackMethodSelector->currentText();
    settings.rejectionMethod = ui.rejectionMethodSelector->currentText();
    settings.sigmaLow = ui.sigmaLowValue->value();
    settings.sigmaHigh = ui.sigmaHighValue->value();
    settings.weighting = weightingSettingsText(ui.weightingSelector->currentText());
    settings.cometStackingMode = ui.cometStackingModeSelector->currentText();
    settings.createRejectionMaps = ui.createRejectionMapsCheck->isChecked();
    settings.mergeRejectionMaps = ui.mergeRejectionMapsCheck->isChecked();
    settings.limitMemory = ui.limitMemoryUsageGroupBox->isChecked();
    settings.maxMemoryMb = ui.maxMemoryMBValue->value();
    const QString normalization = ui.normalizationMode->currentText();
    if (normalization == QStringLiteral("None"))
    {
        settings.normalizationMode = NormalizationMode::None;
    }
    else if (normalization == QStringLiteral("Local"))
    {
        settings.normalizationMode = NormalizationMode::Local;
    }
    else
    {
        settings.normalizationMode = NormalizationMode::Global;
        if (normalization == QStringLiteral("Additive"))
        {
            settings.globalNormalizationMethod = GlobalNormalizationMethod::Additive;
        }
        else if (normalization == QStringLiteral("Multiplicative"))
        {
            settings.globalNormalizationMethod = GlobalNormalizationMethod::Multiplicative;
        }
        else if (normalization == QStringLiteral("Multiplicative With Scaling"))
        {
            settings.globalNormalizationMethod = GlobalNormalizationMethod::MultiplicativeScaling;
        }
        else
        {
            settings.globalNormalizationMethod = GlobalNormalizationMethod::AdditiveScaling;
        }
    }
    settings.scaleEstimator = ui.scaleEstimatorSelector->currentText() == QStringLiteral("MAD")
                                  ? ScaleEstimatorMethod::MAD
                                  : ScaleEstimatorMethod::IKSS;
    an::ui::synchronizeStandaloneStackingLocalNormalization(settings, m_impl->localNormalization);

    StandaloneStackRun run;
    run.stacking = an::ui::processes::sanitizeStandaloneStackingSettings(std::move(settings));
    run.output64 = ui.process64BitsImageCheckbox->isChecked();
    run.sumExposureTimes = ui.sumExposureTimesCheckBox->isChecked();
    run.debayer = ui.debayerCFAGroupBox->isChecked();
    run.cfaPattern = an::ui::bayerPatternChoiceFromText(ui.cfaPattern->currentText());
    run.demosaicMethod = process_utils::demosaicChoiceFromLabel(ui.debayerMethod->currentText()).method;
    run.outputDirectory = ui.outputDirectoryEdit->text().trimmed();
    return run;
}

void ImageStackingProcess::stack()
{
    const an::consumer::EngineSession* session = currentSession();
    if (!isIdle() || session == nullptr)
    {
        return;
    }
    rebuildGroupTable();
    const StandaloneStackRun run = runSettings();
    if (run.outputDirectory.isEmpty())
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Missing output directory"),
                                          QStringLiteral("Choose an output directory before stacking."));
        return;
    }
    if (m_impl->groups.empty())
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("No groups to stack"),
                                          QStringLiteral("Add frames before starting the stack."));
        return;
    }
    const std::vector<StandaloneStackingGroup> groups = m_impl->groups;
    const bool openAfter = m_impl->ui.openAfterStackCheck->isChecked();
    // What the last run said is cleared; this run says what it has to once it ends.
    clearProcessMessage();

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Image Stacking"));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->show();
    m_impl->progress = progress;
    m_impl->running = true;
    m_impl->lastWritten.clear();
    updateControls();

    QPointer<ImageStackingProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = windowStopToken();
    const std::stop_token cancel = progress->stopToken();
    QThreadPool::globalInstance()->start([session, run, groups, openAfter, guard, dialog, windowStop, cancel]() {
        std::stop_source stop;
        const std::stop_callback onClose(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback onCancel(cancel, [&stop]() { stop.request_stop(); });
        QElapsedTimer timer;
        timer.start();
        LOG_INFO("Image Stacking: {} group(s) into {}", groups.size(), run.outputDirectory.toStdString());

        QString failure;
        bool stopped = false;
        // What the weights and the engine said of each group's stack, shown once the run has ended; each was logged
        // as it was said.
        QStringList notes;
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            const StandaloneStackingGroup& group = groups[index];
            if (stop.stop_requested())
            {
                stopped = true;
                break;
            }
            postToGui([dialog, group, index, count = groups.size()]() {
                if (dialog)
                {
                    dialog->setText(QStringLiteral("Stacking group '%1' (%2 frames), %3 of %4")
                                        .arg(group.label)
                                        .arg(group.images.size())
                                        .arg(index + 1u)
                                        .arg(count));
                }
            });
            const double share = 100.0 / static_cast<double>(groups.size());
            an::Result<an::ui::processes::StackedGroup> stacked = an::ui::processes::stackStandaloneGroup(
                *session, group, run, stop.get_token(), [dialog, index, share](double fraction) {
                    const int percent = static_cast<int>(share * (static_cast<double>(index) + fraction));
                    postToGui([dialog, percent]() {
                        if (dialog)
                        {
                            dialog->setValue(percent);
                        }
                    });
                });
            if (!stacked.hasValue())
            {
                if (stacked.status().code() == an::ErrorCode::Cancelled)
                {
                    stopped = true;
                }
                else
                {
                    failure = QString::fromStdString(stacked.status().message());
                    LOG_ERROR("Image Stacking: {}", stacked.status().message());
                }
                break;
            }
            if (!stacked->weightingWarning.isEmpty())
            {
                notes << QStringLiteral("Group '%1': %2").arg(group.label, stacked->weightingWarning);
            }
            for (const QString& warning : stacked->warnings)
            {
                notes << QStringLiteral("Group '%1': %2").arg(group.label, warning);
            }
            auto files = std::make_shared<std::vector<an::ui::processes::StackedFile>>(std::move(stacked->files));
            postToGui([guard, files, openAfter]() {
                if (!guard)
                {
                    return;
                }
                an::ui::IViewFactory* factory = process_utils::currentViewFactory();
                ImageViewControl* neighbour = nullptr;
                for (an::ui::processes::StackedFile& file : *files)
                {
                    guard->m_impl->lastWritten.push_back(file.path);
                    if (!openAfter || factory == nullptr)
                    {
                        continue;
                    }
                    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
                        std::move(file.asset),
                        an::ui::DocumentNames{.displayName = QFileInfo(file.path).completeBaseName(),
                                              .sourcePath = file.path});
                    if (!document.hasValue())
                    {
                        continue;
                    }
                    if (ImageViewControl* view = factory->createNewImageViewControlFromImage(
                            nullptr, std::move(*document), nullptr, file.description.toStdString(),
                            ViewControlMode::standard, neighbour, false, false))
                    {
                        view->fitToWindowAsync();
                        neighbour = view;
                    }
                }
            });
        }
        LOG_INFO("Image Stacking: finished in {:.2f} seconds{}", static_cast<double>(timer.elapsed()) / 1000.0,
                 stopped ? " (stopped)" : "");
        postToGui([guard, dialog, failure, notes, stopped]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                return;
            }
            guard->runFinished();
            if (!failure.isEmpty())
            {
                process_utils::notifyErrorMessage(
                    guard, QStringLiteral("Error stacking images"),
                    QStringLiteral("An error occurred while stacking images: %1").arg(failure));
                return;
            }
            if (!stopped && !notes.isEmpty())
            {
                constexpr qsizetype kShownNotes = 8;
                QString message = notes.mid(0, kShownNotes).join(QLatin1Char(' '));
                if (notes.size() > kShownNotes)
                {
                    message += QStringLiteral(" And %1 more - see the log.").arg(notes.size() - kShownNotes);
                }
                guard->showProcessMessage(QStringLiteral("Image Stacking"),
                                          QStringLiteral("Stacking completed with warnings. %1").arg(message),
                                          ProcessMessageTone::Warning);
            }
        });
    });
}

void ImageStackingProcess::runFinished()
{
    m_impl->running = false;
    m_impl->progress.clear();
    updateControls();
}

void ImageStackingProcess::on_addFilesButtonPushed()
{
    addItemsNonBlocking(
        process_utils::selectImageFiles(this, QStringLiteral("Select Files"), QString::fromLatin1(kFilesContext)));
}

void ImageStackingProcess::on_addFolderButtonPushed()
{
    const ImageListProcessHelpers::AddFolderSelection selection =
        ImageListProcessHelpers::showAddFolderOptionsDialog(this, QString::fromLatin1(kFilesContext));
    if (!selection.accepted)
    {
        return;
    }
    m_impl->files->addFolder(
        selection.directory, selection.recursive,
        an::ui::processes::WorkflowFileFilter{.frameType = selection.typeFilter, .filterName = selection.filterName});
}

void ImageStackingProcess::on_outputDirectoryButtonPushed()
{
    QString initial = m_impl->ui.outputDirectoryEdit->text().trimmed();
    if (initial.isEmpty() && !m_impl->files->files().empty())
    {
        initial = QFileInfo(m_impl->files->files().front().path()).absolutePath();
    }
    const QString directory =
        process_utils::selectExistingDirectory(this, QStringLiteral("Select Output Directory"), initial);
    if (!directory.isEmpty())
    {
        m_impl->ui.outputDirectoryEdit->setText(directory);
    }
}

void ImageStackingProcess::on_stackModeChanged()
{
    m_impl->ui.rejectionSection->setEnabled(m_impl->ui.stackMethodSelector->currentText() == QStringLiteral("Average"));
    on_rejectionModeChanged();
}

void ImageStackingProcess::on_rejectionModeChanged()
{
    auto& ui = m_impl->ui;
    const QString text = ui.rejectionMethodSelector->currentText();
    const bool changed = text != m_impl->lastRejectionMode;
    // Each rejection reads its two numbers as its own thresholds, and a change of rejection restores its defaults.
    struct Labels
    {
        const char* rejection;
        const char* low;
        const char* high;
        double lowDefault;
        double highDefault;
    };
    // Percentile reads its numbers as fractions of the median, and starts at the engine's.
    static constexpr Labels kLabels[] = {{"Linear Fit", "Linear Low", "Linear High", 5.0, 4.0},
                                         {"GESDT", "ESD Outliers", "ESD Significance", 0.30, 0.05},
                                         {"Percentile", "Low Fraction of Median", "High Fraction of Median",
                                          an::kDefaultLowMedianClipFraction, an::kDefaultHighMedianClipFraction}};
    const Labels* labels = nullptr;
    for (const Labels& entry : kLabels)
    {
        if (text == QLatin1String(entry.rejection))
        {
            labels = &entry;
        }
    }
    ui.sigmaLowLabel->setText(labels != nullptr ? QString::fromLatin1(labels->low) : QStringLiteral("Sigma Low"));
    ui.sigmaHighLabel->setText(labels != nullptr ? QString::fromLatin1(labels->high) : QStringLiteral("Sigma High"));
    // The generalized ESD test reads both numbers as fractions strictly between zero and one; every other rejection
    // reads any positive threshold.
    const double maximum = text == QStringLiteral("GESDT") ? kMaximumGesdtValue : kMaximumRejectionThreshold;
    ui.sigmaLowValue->setMaximum(maximum);
    ui.sigmaHighValue->setMaximum(maximum);
    if (changed && text != QStringLiteral("None"))
    {
        ui.sigmaLowValue->setValue(labels != nullptr ? labels->lowDefault : 3.0);
        ui.sigmaHighValue->setValue(labels != nullptr ? labels->highDefault : 3.0);
    }
    m_impl->lastRejectionMode = text;
    updateControls();
}

void ImageStackingProcess::on_normalizationModeChanged()
{
    const QString text = m_impl->ui.normalizationMode->currentText();
    const bool local = text == QStringLiteral("Local");
    // Only a global normalization that scales reads the estimator; a local one fits its own surface.
    m_impl->ui.scaleEstimatorSelector->setEnabled(text.contains(QStringLiteral("Scaling"), Qt::CaseInsensitive));
    m_impl->ui.localNormalizationConfigureButton->setVisible(local);
    m_impl->ui.localNormalizationBanner->setVisible(local);
}

void ImageStackingProcess::on_localNormalizationConfigureButtonPushed()
{
    QPointer<ImageStackingProcess> guard(this);
    an::ui::processes::openLocalNormalizationSettings(this, m_impl->localNormalization,
                                                      [guard](const LocalNormalizationSettings& accepted) {
                                                          if (guard)
                                                          {
                                                              guard->m_impl->localNormalization = accepted;
                                                          }
                                                      });
}

void ImageStackingProcess::listChanged()
{
    if (m_impl->files->files().empty() && m_impl->preview != nullptr)
    {
        m_impl->preview->clear();
    }
    rebuildGroupTable();
}

void ImageStackingProcess::rowSelected(const int row)
{
    const WorkflowFile* file = m_impl->files->selectedFile();
    if (row >= 0 && file != nullptr && m_impl->preview != nullptr)
    {
        // The view reads, demosaics and presents the file on its own workers.
        m_impl->preview->loadImageFromFile(file->path().toUtf8().toStdString());
    }
}

void ImageStackingProcess::rebuildGroupTable()
{
    auto& ui = m_impl->ui;
    an::ui::StandaloneStackingGroupingOptions options;
    options.byFilter = ui.groupByFilter->isChecked();
    options.bySession = ui.groupBySession->isChecked();
    options.byExposure = ui.groupByExposure->isChecked();
    options.exposureToleranceFraction = ui.exposureTolerancePercentValue->value() / 100.0;
    std::vector<StandaloneStackingFrame> frames;
    frames.reserve(m_impl->files->files().size());
    for (const WorkflowFile& file : m_impl->files->files())
    {
        frames.push_back(stackingFrameOf(file));
    }
    m_impl->groups = an::ui::buildStandaloneStackingGroups(frames, options, ui.outputImageNameEdit->text());

    const int previousRow = ui.groupTable->currentRow();
    ui.groupTable->setRowCount(0);
    for (int row = 0; row < static_cast<int>(m_impl->groups.size()); ++row)
    {
        const StandaloneStackingGroup& group = m_impl->groups[static_cast<std::size_t>(row)];
        ui.groupTable->insertRow(row);
        auto* label = new QTableWidgetItem(group.label);
        label->setFlags(label->flags() & ~Qt::ItemIsEditable);
        ui.groupTable->setItem(row, 0, label);
        ui.groupTable->setItem(row, 1, new NumericSortItem(QString::number(group.frameCount), group.frameCount));
        ImageListProcessHelpers::setTableItemText(ui.groupTable, row, 2, group.filter);
        ImageListProcessHelpers::setTableItemText(ui.groupTable, row, 3, group.session);
        ui.groupTable->setItem(
            row, 4, new NumericSortItem(formatTotalExposure(group.totalExposureSeconds), group.totalExposureSeconds));
        ImageListProcessHelpers::setTableItemText(ui.groupTable, row, 5, group.outputFileName);
    }
    if (!m_impl->groups.empty())
    {
        const int count = static_cast<int>(m_impl->groups.size());
        // Not TableWidget::selectRow, which moves the keyboard focus to the table.
        ui.groupTable->QTableWidget::selectRow(previousRow >= 0 ? (std::min)(previousRow, count - 1) : 0);
    }
    updateWeightingBanner();
    updateControls();
}

void ImageStackingProcess::updateRejectionMapsPathLabel()
{
    auto& ui = m_impl->ui;
    if (!ui.createRejectionMapsCheck->isChecked())
    {
        ui.rejectionMapsPathLabel->setText(QStringLiteral("Rejection maps are disabled."));
        return;
    }
    const QString directory = ui.outputDirectoryEdit->text().trimmed();
    if (directory.isEmpty())
    {
        ui.rejectionMapsPathLabel->setText(QStringLiteral("Choose an output directory to save rejection maps."));
        return;
    }
    if (m_impl->groups.empty())
    {
        ui.rejectionMapsPathLabel->setText(
            QStringLiteral("Rejection maps will be written next to the stacked outputs."));
        return;
    }
    const QFileInfo output(QDir(directory).filePath(m_impl->groups.front().outputFileName));
    const QString suffix = ui.mergeRejectionMapsCheck->isChecked()
                               ? QStringLiteral("_rej_l+h.%1").arg(output.suffix())
                               : QStringLiteral("_rej_[l|h].%1").arg(output.suffix());
    ui.rejectionMapsPathLabel->setText(
        QStringLiteral("Rejection maps: %1")
            .arg(QDir(output.absolutePath()).filePath(output.completeBaseName() + suffix)));
}

void ImageStackingProcess::updateWeightingBanner()
{
    auto& ui = m_impl->ui;
    const an::ui::StandaloneStackingWeightingMode mode =
        an::ui::standaloneStackingWeightingModeFromText(weightingSettingsText(ui.weightingSelector->currentText()));
    const QString directory = ui.outputDirectoryEdit->text().trimmed();
    const std::uint64_t generation = ++m_impl->bannerGeneration;
    // The weights planned before the stack runs - the registration's figures from the database in the output
    // directory, or each frame's exposure from its header - are said here as soon as a frame lacks them; Noise is
    // measured on the pixels when the stack starts.
    const bool readsMetrics = an::ui::standaloneStackingWeightingRequiresMetrics(mode);
    const bool readsExposures = mode == an::ui::StandaloneStackingWeightingMode::Exposure;
    if ((!readsMetrics && !readsExposures) || m_impl->files->files().empty() || (readsMetrics && directory.isEmpty()))
    {
        ui.weightingBanner->hide();
        return;
    }
    const QString database =
        QDir(directory).filePath(QString::fromLatin1(an::ui::processes::kRegistrationMetricsDatabaseName));
    std::vector<StandaloneStackingFrame> frames;
    for (const WorkflowFile& file : m_impl->files->files())
    {
        frames.push_back(stackingFrameOf(file));
    }
    ++m_impl->bannersInFlight;
    QPointer<ImageStackingProcess> guard(this);
    QThreadPool::globalInstance()->start([guard, generation, database, frames, mode]() {
        const an::ui::StandaloneStackingWeightingResult planned =
            an::ui::planStandaloneStackingWeights(database, frames, mode);
        postToGui([guard, generation, warning = planned.warning]() {
            if (!guard)
            {
                return;
            }
            --guard->m_impl->bannersInFlight;
            if (guard->m_impl->bannerGeneration != generation)
            {
                return;
            }
            auto* banner = guard->m_impl->ui.weightingBanner;
            if (warning.isEmpty())
            {
                banner->hide();
                return;
            }
            banner->setTone(an::ui::AnInfoBanner::Tone::Warning);
            banner->setMessage(warning);
            banner->show();
        });
    });
}

void ImageStackingProcess::updateControls()
{
    auto& ui = m_impl->ui;
    const bool hasRows = !m_impl->files->files().empty();
    const bool editable = !m_impl->running;
    ui.exposureTolerancePercentValue->setEnabled(ui.groupByExposure->isChecked());
    const bool average = ui.stackMethodSelector->currentText() == QStringLiteral("Average");
    const bool rejects = average && ui.rejectionMethodSelector->currentText() != QStringLiteral("None");
    ui.sigmaLowValue->setEnabled(rejects);
    ui.sigmaHighValue->setEnabled(rejects);
    ui.createRejectionMapsCheck->setEnabled(rejects);
    ui.mergeRejectionMapsCheck->setEnabled(rejects && ui.createRejectionMapsCheck->isChecked());
    ui.addFiles->setEnabled(editable);
    ui.addFolder->setEnabled(editable);
    ui.cometStackingModeSelector->setEnabled(editable);
    ui.stackImagesButton->setEnabled(editable && hasRows && !m_impl->groups.empty() &&
                                     !ui.outputDirectoryEdit->text().trimmed().isEmpty() &&
                                     !ui.outputImageNameEdit->text().trimmed().isEmpty());
    if (QPushButton* reset = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        reset->setEnabled(editable);
    }
    updateRejectionMapsPathLabel();
}

void ImageStackingProcess::resetSettings()
{
    auto& ui = m_impl->ui;
    an::ui::resetStandaloneStackingLocalNormalizationSettings(m_impl->localNormalization);
    m_impl->lastRejectionMode.clear();
    setComboText(ui.stackMethodSelector, QStringLiteral("Average"));
    setComboText(ui.rejectionMethodSelector, QStringLiteral("Winsorized Sigma Clipping"));
    setComboText(ui.weightingSelector, QStringLiteral("None (Equal)"));
    setComboText(ui.normalizationMode, QStringLiteral("None"));
    setComboText(ui.cometStackingModeSelector, QStringLiteral("Stars"));
    setComboText(ui.scaleEstimatorSelector, QStringLiteral("IKSS"));
    ui.sigmaLowValue->setValue(3.0);
    ui.sigmaHighValue->setValue(3.0);
    ui.groupByFilter->setChecked(true);
    ui.groupBySession->setChecked(false);
    ui.groupByExposure->setChecked(false);
    ui.exposureTolerancePercentValue->setValue(5.0);
    ui.outputImageNameEdit->setText(kDefaultTemplate);
    ui.openAfterStackCheck->setChecked(true);
    ui.sumExposureTimesCheckBox->setChecked(false);
    ui.process64BitsImageCheckbox->setChecked(false);
    ui.createRejectionMapsCheck->setChecked(false);
    ui.mergeRejectionMapsCheck->setChecked(true);
    ui.limitMemoryUsageGroupBox->setChecked(true);
    ui.maxMemoryMBValue->setValue(1024);
    ui.debayerCFAGroupBox->setChecked(false);
    setComboText(ui.cfaPattern, an::ui::kBayerPatternFromImageText);
    setComboText(ui.debayerMethod, process_utils::DEMOSAIC_RCD);
    on_stackModeChanged();
    on_normalizationModeChanged();
    rebuildGroupTable();
}

void ImageStackingProcess::closeEvent(QCloseEvent* evt)
{
    // Everything the window started stops with the window's token, requested as the base closes, and lands on a window
    // that is gone; nothing is waited for.
    if (m_impl->progress)
    {
        m_impl->progress->requestCancel();
    }
    TopLevelProcessBaseControl::closeEvent(evt);
}

std::string ImageStackingProcess::serialize_json_settings()
{
    const auto& ui = m_impl->ui;
    nlohmann::json document;
    document["version"] = kSettingsVersion;
    document["frames"] = ImageListProcessHelpers::imageFilePathsToJson(m_impl->files->entries());
    auto& params = document["params"];
    params["stackMethod"] = ui.stackMethodSelector->currentText().toStdString();
    params["rejectionMethod"] = ui.rejectionMethodSelector->currentText().toStdString();
    params["weighting"] = ui.weightingSelector->currentText().toStdString();
    params["normalizationMode"] = ui.normalizationMode->currentText().toStdString();
    params["cometStackingMode"] = ui.cometStackingModeSelector->currentText().toStdString();
    params["scaleEstimator"] = ui.scaleEstimatorSelector->currentText().toStdString();
    params["sigmaLow"] = ui.sigmaLowValue->value();
    params["sigmaHigh"] = ui.sigmaHighValue->value();
    params["groupByFilter"] = ui.groupByFilter->isChecked();
    params["groupBySession"] = ui.groupBySession->isChecked();
    params["groupByExposure"] = ui.groupByExposure->isChecked();
    params["exposureTolerancePercent"] = ui.exposureTolerancePercentValue->value();
    params["outputDirectory"] = ui.outputDirectoryEdit->text().toStdString();
    params["outputTemplate"] = ui.outputImageNameEdit->text().toStdString();
    params["openAfterStack"] = ui.openAfterStackCheck->isChecked();
    params["sumExposureTimes"] = ui.sumExposureTimesCheckBox->isChecked();
    params["process64Bits"] = ui.process64BitsImageCheckbox->isChecked();
    params["createRejectionMaps"] = ui.createRejectionMapsCheck->isChecked();
    params["mergeRejectionMaps"] = ui.mergeRejectionMapsCheck->isChecked();
    params["limitMemoryUsage"] = ui.limitMemoryUsageGroupBox->isChecked();
    params["maxMemoryMBValue"] = ui.maxMemoryMBValue->value();
    params["debayerEnabled"] = ui.debayerCFAGroupBox->isChecked();
    params["cfaPattern"] = ui.cfaPattern->currentText().toStdString();
    params["debayerMethod"] = ui.debayerMethod->currentText().toStdString();
    an::ui::serializeStandaloneStackingLocalNormalization(params, m_impl->localNormalization);
    return document.dump();
}

void ImageStackingProcess::deserialize_json_settings(const std::string& json_settings)
{
    if (json_settings.empty())
    {
        return;
    }
    const nlohmann::json document = nlohmann::json::parse(json_settings, nullptr, false);
    if (document.is_discarded() || !document.is_object() || !document.contains("params") ||
        !document["params"].is_object())
    {
        LOG_ERROR("Image Stacking: could not read its settings: {}", json_settings);
        return;
    }
    const int version = document.value("version", 0);
    if (version < 1 || version > kSettingsVersion)
    {
        LOG_WARNING("Image Stacking: unknown settings version {}, read as far as it goes", version);
    }
    auto& ui = m_impl->ui;
    try
    {
        const auto& params = document["params"];
        const auto text = [&params](const char* key, const QString& fallback) {
            return QString::fromStdString(params.value(key, fallback.toStdString()));
        };
        setComboText(ui.stackMethodSelector, text("stackMethod", QStringLiteral("Average")));
        setComboText(ui.rejectionMethodSelector, text("rejectionMethod", QStringLiteral("Winsorized Sigma Clipping")));
        setComboText(ui.weightingSelector, text("weighting", QStringLiteral("None (Equal)")));
        setComboText(ui.normalizationMode, text("normalizationMode", QStringLiteral("None")));
        setComboText(ui.cometStackingModeSelector, text("cometStackingMode", QStringLiteral("Stars")));
        setComboText(ui.scaleEstimatorSelector, text("scaleEstimator", QStringLiteral("IKSS")));
        // The rejection's own numbers are restored after its defaults.
        on_stackModeChanged();
        ui.sigmaLowValue->setValue(params.value("sigmaLow", 3.0));
        ui.sigmaHighValue->setValue(params.value("sigmaHigh", 3.0));
        ui.groupByFilter->setChecked(params.value("groupByFilter", true));
        ui.groupBySession->setChecked(params.value("groupBySession", false));
        ui.groupByExposure->setChecked(params.value("groupByExposure", false));
        ui.exposureTolerancePercentValue->setValue(params.value("exposureTolerancePercent", 5.0));
        ui.outputDirectoryEdit->setText(text("outputDirectory", QString()));
        ui.outputImageNameEdit->setText(text("outputTemplate", kDefaultTemplate));
        ui.openAfterStackCheck->setChecked(params.value("openAfterStack", true));
        ui.sumExposureTimesCheckBox->setChecked(params.value("sumExposureTimes", false));
        ui.process64BitsImageCheckbox->setChecked(params.value("process64Bits", false));
        ui.createRejectionMapsCheck->setChecked(params.value("createRejectionMaps", false));
        ui.mergeRejectionMapsCheck->setChecked(params.value("mergeRejectionMaps", true));
        ui.limitMemoryUsageGroupBox->setChecked(params.value("limitMemoryUsage", true));
        ui.maxMemoryMBValue->setValue(params.value("maxMemoryMBValue", 1024));
        ui.debayerCFAGroupBox->setChecked(params.value("debayerEnabled", false));
        setComboText(ui.cfaPattern, text("cfaPattern", an::ui::kBayerPatternFromImageText));
        // A method the dialog does not offer reads as RCD, the setting's default.
        const QString method = text("debayerMethod", process_utils::DEMOSAIC_RCD);
        const process_utils::DemosaicLabelChoice choice = process_utils::demosaicChoiceFromLabel(method);
        setComboText(ui.debayerMethod, choice.known ? method : process_utils::DEMOSAIC_RCD);
        m_impl->localNormalization = an::ui::deserializeStandaloneStackingLocalNormalization(params);
    }
    catch (const nlohmann::json::exception& error)
    {
        LOG_ERROR("Image Stacking: could not read its settings: {}", error.what());
    }
    m_impl->lastRejectionMode = ui.rejectionMethodSelector->currentText();
    on_normalizationModeChanged();
    rebuildGroupTable();

    if (version >= kSettingsVersion)
    {
        const QStringList frames =
            ImageListProcessHelpers::imageFilePathsFromJson(document.value("frames", nlohmann::json::array()));
        if (!frames.isEmpty())
        {
            m_impl->files->restoreFiles(frames);
        }
    }
}

void ImageStackingProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    const QDialogButtonBox::StandardButton which = m_impl->ui.buttonBox->standardButton(button);
    if (which == QDialogButtonBox::Close)
    {
        close();
    }
    else if (which == QDialogButtonBox::Reset)
    {
        resetSettings();
    }
}
