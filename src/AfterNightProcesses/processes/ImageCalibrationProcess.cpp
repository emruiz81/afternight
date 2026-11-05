// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Calibration window: the shared file list, the masters and output options, a preview of the selected light,
/// and the run that calibrates every light on the thread pool.

#include "stdafx.hpp"
#include "ImageCalibrationProcess.h"

#include "app/AppContextHelpers.h"
#include "controls/ImageViewControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TableWidgetUtils.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/WorkflowToolFiles.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>

#include <AfterNightPlatform/Logging.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>

#include <nlohmann/json.hpp>

#include <stop_token>
#include <unordered_map>
#include <utility>

using an::ui::processes::CalibrationBatch;
using an::ui::processes::CalibrationBatchSettings;
using an::ui::processes::WorkflowFile;
using an::ui::processes::WorkflowFileColumn;
using an::ui::processes::WorkflowFileList;

namespace
{

constexpr const char* kFilesContext = "__IMAGE_CALIB_ADD_FILES__";
constexpr const char* kBiasContext = "__IMAGE_CALIB_BIAS__";
constexpr const char* kDarkContext = "__IMAGE_CALIB_DARK__";
constexpr const char* kFlatContext = "__IMAGE_CALIB_FLAT__";
constexpr const char* kOutputContext = "__IMAGE_CALIB_FOLDER__";

/// The persisted settings' version: 3 lists the frames beside the settings; 2 had none, and 1 was a stub.
constexpr int kSettingsVersion = 3;

constexpr std::size_t kTypeColumn = 9u;

const QString kPending = QStringLiteral("Pending");
const QString kCalibrated = QStringLiteral("Calibrated");
const QString kFailed = QStringLiteral("Failed");

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

[[nodiscard]] QString messageOf(const an::Status& status)
{
    return QString::fromStdString(status.message());
}

} // namespace

struct ImageCalibrationProcess::Impl
{
    an::ui::processes::ImageCalibrationProcessUiElements ui;
    ImageViewControl* preview = nullptr;
    std::unique_ptr<WorkflowFileList> files;

    /// Each light's status, by its path; a light with none is pending.
    std::unordered_map<QString, QString> statuses;

    bool running = false;
    bool estimating = false;
    QPointer<ProgressDialogControl> progress;
};

ImageCalibrationProcess::ImageCalibrationProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(parent);
    m_impl->ui = an::ui::processes::buildImageCalibrationProcessUi(this);
    setProcessDescription(QStringLiteral(
        "Batch-calibrate light frames using optional bias, dark, and flat masters with cosmetic correction controls."));
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto& ui = m_impl->ui;
    QObject::connect(ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageCalibrationProcess::on_dialog_box_button_clicked);
    QObject::connect(ui.addFiles, &QPushButton::released, this, &ImageCalibrationProcess::on_addFilesButtonPushed);
    QObject::connect(ui.addFolder, &QPushButton::released, this, &ImageCalibrationProcess::on_addFolderButtonPushed);
    QObject::connect(ui.removeSelected, &QPushButton::released, this,
                     [this]() { removeItems(process_utils::selectedFilesFromTable(m_impl->ui.tableWidget)); });
    QObject::connect(ui.moveUpButton, &QPushButton::released, this, [this]() { m_impl->files->moveSelected(-1); });
    QObject::connect(ui.moveDownButton, &QPushButton::released, this, [this]() { m_impl->files->moveSelected(1); });
    QObject::connect(ui.clearAll, &QPushButton::released, this, [this]() {
        if (!m_impl->running)
        {
            m_impl->files->clear();
            m_impl->statuses.clear();
        }
    });
    QObject::connect(ui.calibrateImagesButton, &QPushButton::released, this, &ImageCalibrationProcess::calibrate);
    QObject::connect(ui.estimateCosmeticCorrectionButton, &QPushButton::released, this,
                     &ImageCalibrationProcess::estimateCosmeticCorrection);
    QObject::connect(ui.biasFileButton, &QPushButton::released, this,
                     [this]() { chooseMaster(m_impl->ui.biasFilePath, kBiasContext); });
    QObject::connect(ui.darkFileButton, &QPushButton::released, this,
                     [this]() { chooseMaster(m_impl->ui.darkFilePath, kDarkContext); });
    QObject::connect(ui.flatFileButton, &QPushButton::released, this,
                     [this]() { chooseMaster(m_impl->ui.flatFilePath, kFlatContext); });
    QObject::connect(ui.outputDirectoryButton, &QPushButton::released, this,
                     &ImageCalibrationProcess::on_outputDirectoryButtonPushed);

    for (QGroupBox* box : {ui.biasGroupBox, ui.darkGroupBox, ui.flatGroupBox})
    {
        QObject::connect(box, &QGroupBox::toggled, this, [this]() { updateControls(); });
    }
    for (QLineEdit* field : {ui.biasFilePath, ui.darkFilePath, ui.flatFilePath, ui.outputDirectory})
    {
        QObject::connect(field, &QLineEdit::textChanged, this, [this]() { updateControls(); });
    }
    QObject::connect(ui.darkOptimizationCheck, &QCheckBox::toggled, this, [this]() { updateControls(); });
    QObject::connect(ui.outputFormatSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [this]() { updateControls(); });
    QObject::connect(ui.pedestalValue, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                     [this]() { updateControls(); });

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
                       {QStringLiteral("Depth"), WorkflowFileColumn::Depth},
                       {QStringLiteral("Type"), WorkflowFileColumn::Custom},
                       {QStringLiteral("Status"), WorkflowFileColumn::Custom}};
    options.customText = [this](int, const WorkflowFile& file, const std::size_t column) {
        // The two custom columns: the frame's role, and its status in the last run.
        return column == kTypeColumn ? QStringLiteral("Light") : statusOf(file.path());
    };
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
        updateControls();
    };

    resetSettings();
    initializeTopLevelProcess();
}

ImageCalibrationProcess::~ImageCalibrationProcess() = default;

std::string ImageCalibrationProcess::getProcessNameStatic()
{
    return "Image Calibration";
}

void ImageCalibrationProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
    if (m_impl->preview != nullptr)
    {
        m_impl->preview->refreshTheme();
    }
    if (m_impl->ui.frameListCard != nullptr)
    {
        m_impl->ui.frameListCard->refreshTheme();
    }
    m_impl->ui.tableWidget->setupSelectionColors();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_impl->ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        m_impl->ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
    }
}

void ImageCalibrationProcess::addItemsNonBlocking(const QStringList& items)
{
    m_impl->files->addFiles(items);
}

void ImageCalibrationProcess::removeItems(const std::vector<QString>& items)
{
    if (m_impl->running)
    {
        return;
    }
    m_impl->files->remove(items);
}

const WorkflowFileList& ImageCalibrationProcess::fileList() const
{
    return *m_impl->files;
}

ImageViewControl* ImageCalibrationProcess::previewView() const
{
    return m_impl->preview;
}

bool ImageCalibrationProcess::isIdle() const
{
    return m_impl->files->idle() && !m_impl->running && !m_impl->estimating;
}

QString ImageCalibrationProcess::statusOf(const QString& path) const
{
    const auto it = m_impl->statuses.find(path);
    return it == m_impl->statuses.end() ? kPending : it->second;
}

an::ui::processes::ImageCalibrationProcessUiElements& ImageCalibrationProcess::ui()
{
    return m_impl->ui;
}

CalibrationBatchSettings ImageCalibrationProcess::settings() const
{
    const auto& ui = m_impl->ui;
    CalibrationBatchSettings settings;
    if (ui.biasGroupBox->isChecked())
    {
        settings.biasPath = ui.biasFilePath->text().trimmed();
    }
    if (ui.darkGroupBox->isChecked())
    {
        settings.darkPath = ui.darkFilePath->text().trimmed();
    }
    if (ui.flatGroupBox->isChecked())
    {
        settings.flatPath = ui.flatFilePath->text().trimmed();
    }
    settings.darkOptimization = ui.darkOptimizationCheck->isChecked();
    settings.darkMultiplicationFactor = ui.darkMultiplicationFactorValue->value();
    settings.normalizeFlats = ui.normalizeFlatsCheck->isChecked();
    settings.cosmeticCorrection = ui.cosmeticCorrectionGroupBox->isChecked();
    settings.coldSigma =
        ui.coldSigmaCheck->isChecked() ? std::optional<double>(ui.coldSigmaValue->value()) : std::nullopt;
    settings.hotSigma = ui.hotSigmaCheck->isChecked() ? std::optional<double>(ui.hotSigmaValue->value()) : std::nullopt;
    settings.pedestalCodes = ui.pedestalValue->value();
    settings.outputDirectory = ui.outputDirectory->text().trimmed();
    settings.output.preserveOriginalName = ui.preserveOriginalNameCheck->isChecked();
    settings.output.filenameSuffix = ui.filenameSuffixEdit->text();
    settings.output.format =
        an::ui::processes::calibrationOutputFormatFromText(ui.outputFormatSelector->currentData().toString());
    settings.output.use32BitFloat = ui.outputPrecisionSelector->currentData().toBool();
    return settings;
}

void ImageCalibrationProcess::calibrate()
{
    const an::consumer::EngineSession* session = currentSession();
    if (m_impl->running || session == nullptr || m_impl->files->files().empty())
    {
        return;
    }
    if (!isIdle())
    {
        // Files still being added, or the cosmetic estimate still reading the dark, would change what a run reads.
        process_utils::notifyInfoMessage(this, QStringLiteral("Image Calibration"),
                                         QStringLiteral("Wait until the files being added and the cosmetic correction "
                                                        "estimate have finished before calibrating."));
        return;
    }
    if (m_impl->ui.outputDirectory->text().trimmed().isEmpty())
    {
        process_utils::notifyInfoMessage(this, QStringLiteral("Image Calibration"),
                                         QStringLiteral("You need to define an output directory before executing "
                                                        "image calibration"));
        return;
    }

    // Everything the run reads is read here, on the GUI thread.
    const CalibrationBatchSettings settings = this->settings();
    const std::vector<ImageListProcessHelpers::ImageListEntry> lights = m_impl->files->entries();
    // A row states what this run made of its file: one the run has not reached, or that a stop left, is pending.
    m_impl->statuses.clear();
    m_impl->files->refreshRows();

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Image Calibration"));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->setText(QStringLiteral("Calibrating %1 images").arg(lights.size()));
    progress->show();
    m_impl->progress = progress;
    m_impl->running = true;
    updateControls();

    QPointer<ImageCalibrationProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = windowStopToken();
    const std::stop_token cancel = progress->stopToken();
    QThreadPool::globalInstance()->start([session, settings, lights, guard, dialog, windowStop, cancel]() {
        std::stop_source stop;
        const std::stop_callback onClose(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback onCancel(cancel, [&stop]() { stop.request_stop(); });
        QElapsedTimer timer;
        timer.start();

        const auto finish = [guard, dialog](QString title, QString message, ProcessMessageTone tone, bool error) {
            // A warning is logged as well as shown, so a run that completed with one says so where it is recorded.
            if (tone == ProcessMessageTone::Warning)
            {
                LOG_WARNING("{}: {}", title.toStdString(), message.toStdString());
            }
            postToGui([guard, dialog, title, message, tone, error]() {
                if (dialog)
                {
                    dialog->closeAsync();
                }
                if (!guard)
                {
                    return;
                }
                guard->runFinished();
                if (message.isEmpty())
                {
                    return;
                }
                if (error)
                {
                    process_utils::notifyErrorMessage(guard, title, message);
                }
                else
                {
                    guard->showProcessMessage(title, message, tone);
                }
            });
        };

        an::Result<CalibrationBatch> batch = CalibrationBatch::open(*session, settings, lights, stop.get_token());
        if (!batch.hasValue())
        {
            const bool stopped = batch.status().code() == an::ErrorCode::Cancelled;
            if (!stopped)
            {
                LOG_ERROR("Image Calibration: {}", batch.status().message());
            }
            finish(QStringLiteral("Error calibrating images"),
                   stopped
                       ? QString()
                       : QStringLiteral("An error occurred when calibrating images: %1").arg(messageOf(batch.status())),
                   ProcessMessageTone::Error, true);
            return;
        }

        std::size_t succeeded = 0u;
        std::size_t failed = 0u;
        bool stopped = false;
        QString firstFailure;
        for (std::size_t index = 0; index < lights.size(); ++index)
        {
            if (stop.stop_requested())
            {
                stopped = true;
                break;
            }
            const QString path = lights[index].path;
            const an::Result<an::ui::processes::CalibrationFrameOutcome> outcome =
                batch->calibrate(path, index, stop.get_token());
            if (!outcome.hasValue() && outcome.status().code() == an::ErrorCode::Cancelled)
            {
                stopped = true;
                break;
            }
            QString status;
            if (outcome.hasValue())
            {
                ++succeeded;
                status = kCalibrated;
                LOG_INFO("Image Calibration: '{}' -> '{}'", path.toStdString(), outcome->output.toStdString());
            }
            else
            {
                ++failed;
                status = kFailed;
                if (firstFailure.isEmpty())
                {
                    firstFailure = messageOf(outcome.status());
                }
                LOG_ERROR("Image Calibration: '{}' failed: {}", path.toStdString(), outcome.status().message());
            }
            const int percent = static_cast<int>(((index + 1u) * 100u) / lights.size());
            postToGui([guard, dialog, path, status, percent]() {
                if (dialog)
                {
                    dialog->setValue(percent);
                }
                if (guard)
                {
                    guard->m_impl->statuses.insert_or_assign(path, status);
                    const int row = guard->m_impl->files->rowOf(path);
                    if (row >= 0)
                    {
                        guard->m_impl->files->refreshRow(row);
                    }
                }
            });
        }
        LOG_INFO("Image Calibration: {} calibrated, {} failed in {:.2f} seconds{}", succeeded, failed,
                 static_cast<double>(timer.elapsed()) / 1000.0, stopped ? " (stopped)" : "");

        // A cosmetic correction with neither sigma checked corrects nothing, which every run that wrote a file says.
        const bool correctsNothing = settings.cosmeticCorrection && !settings.darkPath.isEmpty() &&
                                     !settings.coldSigma.has_value() && !settings.hotSigma.has_value();
        const QString correctsNothingNote = QStringLiteral("Cosmetic correction was on but neither the cold nor the "
                                                           "hot sigma was checked, so no pixel was corrected.");
        if (stopped)
        {
            finish({}, {}, ProcessMessageTone::Info, false);
        }
        else if (succeeded == 0u)
        {
            finish(QStringLiteral("Error calibrating images"),
                   QStringLiteral("Calibration did not produce any output images. First error: %1").arg(firstFailure),
                   ProcessMessageTone::Error, true);
        }
        else if (failed > 0u)
        {
            finish(QStringLiteral("Image Calibration"),
                   QStringLiteral("Calibration completed with warnings. %1 of %2 images succeeded. First error: %3")
                           .arg(succeeded)
                           .arg(lights.size())
                           .arg(firstFailure) +
                       (correctsNothing ? QStringLiteral(" ") + correctsNothingNote : QString()),
                   ProcessMessageTone::Warning, false);
        }
        else if (correctsNothing)
        {
            finish(QStringLiteral("Image Calibration"),
                   QStringLiteral("Calibration completed with warnings. ") + correctsNothingNote,
                   ProcessMessageTone::Warning, false);
        }
        else
        {
            finish(QStringLiteral("Image Calibration"), QStringLiteral("Calibration executed successfully!"),
                   ProcessMessageTone::Info, false);
        }
    });
}

void ImageCalibrationProcess::runFinished()
{
    m_impl->running = false;
    m_impl->progress.clear();
    updateControls();
}

void ImageCalibrationProcess::estimateCosmeticCorrection()
{
    const an::consumer::EngineSession* session = currentSession();
    const CalibrationBatchSettings settings = this->settings();
    if (m_impl->estimating || session == nullptr || settings.darkPath.isEmpty())
    {
        return;
    }
    m_impl->estimating = true;
    updateControls();
    QPointer<ImageCalibrationProcess> guard(this);
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, settings, guard, stop]() {
        const an::Result<an::ui::processes::CosmeticEstimate> estimate = an::ui::processes::estimateCosmeticDefects(
            *session, settings.darkPath, settings.coldSigma, settings.hotSigma, stop);
        if (estimate.hasValue())
        {
            LOG_INFO("Image Calibration: detected {} cold pixels and {} hot pixels", estimate->cold, estimate->hot);
        }
        postToGui([guard, estimate]() {
            if (!guard)
            {
                return;
            }
            guard->m_impl->estimating = false;
            guard->updateControls();
            if (estimate.hasValue())
            {
                process_utils::notifyInfoMessage(
                    guard, QStringLiteral("Cosmetic Correction Estimation"),
                    QStringLiteral("Detected %1 cold pixels and %2 hot pixels").arg(estimate->cold).arg(estimate->hot));
            }
            else if (estimate.status().code() != an::ErrorCode::Cancelled)
            {
                process_utils::notifyErrorMessage(guard, QStringLiteral("Cosmetic Correction Estimation"),
                                                  messageOf(estimate.status()));
            }
        });
    });
}

void ImageCalibrationProcess::on_addFilesButtonPushed()
{
    addItemsNonBlocking(
        process_utils::selectImageFiles(this, QStringLiteral("Select Files"), QString::fromLatin1(kFilesContext)));
}

void ImageCalibrationProcess::on_addFolderButtonPushed()
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

void ImageCalibrationProcess::chooseMaster(QLineEdit* field, const char* context)
{
    const QStringList selected =
        process_utils::selectImageFiles(this, QStringLiteral("Select Files"), QString::fromLatin1(context), false);
    if (!selected.isEmpty() && !selected.first().isEmpty())
    {
        field->setText(selected.first());
    }
    updateControls();
}

void ImageCalibrationProcess::on_outputDirectoryButtonPushed()
{
    QString home = m_impl->ui.outputDirectory->text();
    if (home.isEmpty() || !QDir(home).exists())
    {
        home = process_utils::selectPath(QString::fromLatin1(kOutputContext));
        if ((home.isEmpty() || !QDir(home).exists()) && !m_impl->files->files().empty())
        {
            home = QFileInfo(m_impl->files->files().front().path()).absolutePath();
        }
    }
    const QString directory = process_utils::selectExistingDirectory(
        this, QStringLiteral("Select Directory"), home, QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!directory.isEmpty())
    {
        m_impl->ui.outputDirectory->setText(directory);
        process_utils::rememberPath(QString::fromLatin1(kOutputContext), directory);
    }
    updateControls();
}

void ImageCalibrationProcess::listChanged()
{
    // A light no longer listed lets its status go.
    for (auto it = m_impl->statuses.begin(); it != m_impl->statuses.end();)
    {
        it = m_impl->files->rowOf(it->first) < 0 ? m_impl->statuses.erase(it) : std::next(it);
    }
    if (m_impl->files->files().empty() && m_impl->preview != nullptr)
    {
        m_impl->preview->clear();
    }
    updateControls();
}

void ImageCalibrationProcess::rowSelected(const int row)
{
    const WorkflowFile* file = m_impl->files->selectedFile();
    if (row < 0 || file == nullptr || m_impl->preview == nullptr)
    {
        return;
    }
    // The view reads, demosaics and presents the light on its own workers.
    m_impl->preview->loadImageFromFile(file->path().toUtf8().toStdString());
}

void ImageCalibrationProcess::updateControls()
{
    auto& ui = m_impl->ui;
    const bool hasRows = !m_impl->files->files().empty();
    const bool editable = !m_impl->running;

    ui.biasGroupBox->setEnabled(hasRows && editable);
    ui.darkGroupBox->setEnabled(hasRows && editable);
    ui.flatGroupBox->setEnabled(hasRows && editable);
    ui.outputDirectory->setEnabled(hasRows && editable);
    ui.outputDirectoryButton->setEnabled(hasRows && editable);
    ui.addFiles->setEnabled(editable);
    ui.addFolder->setEnabled(editable);

    const bool darkReady = hasRows && ui.darkGroupBox->isChecked() && !ui.darkFilePath->text().trimmed().isEmpty();
    ui.cosmeticCorrectionGroupBox->setEnabled(darkReady);
    ui.estimateCosmeticCorrectionButton->setEnabled(darkReady && !m_impl->estimating);
    // The factor scales every light's dark while dark optimization is off, and the dark of each light it cannot measure
    // while it is on.
    ui.darkMultiplicationFactorValue->setEnabled(darkReady);
    ui.darkAdvancedSection->setEnabled(hasRows && ui.darkGroupBox->isChecked());
    ui.flatAdvancedSection->setEnabled(hasRows && ui.flatGroupBox->isChecked());
    ui.outputAdvancedSection->setEnabled(hasRows);

    const bool precision = an::ui::processes::calibrationOutputPrecisionApplies(
        an::ui::processes::calibrationOutputFormatFromText(ui.outputFormatSelector->currentData().toString()));
    ui.outputPrecisionSelector->setEnabled(precision);
    ui.outputPrecisionLabel->setEnabled(precision);

    // A checked master with no file holds Calibrate back; otherwise a master or a pedestal enables it.
    bool missingMaster = false;
    for (auto [box, field] : {std::pair{ui.biasGroupBox, ui.biasFilePath}, std::pair{ui.darkGroupBox, ui.darkFilePath},
                              std::pair{ui.flatGroupBox, ui.flatFilePath}})
    {
        missingMaster = missingMaster || (box->isChecked() && field->text().trimmed().isEmpty());
    }
    const bool calibrates = an::ui::processes::calibrationBatchCalibrates(settings());
    ui.calibrateImagesButton->setEnabled(hasRows && editable && !missingMaster && calibrates);
    if (QPushButton* close = ui.buttonBox->button(QDialogButtonBox::Close))
    {
        close->setEnabled(true);
    }
    if (QPushButton* reset = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        reset->setEnabled(editable);
    }
}

void ImageCalibrationProcess::resetSettings()
{
    auto& ui = m_impl->ui;
    ui.biasFilePath->clear();
    ui.biasGroupBox->setChecked(false);
    ui.darkFilePath->clear();
    ui.darkGroupBox->setChecked(false);
    ui.darkOptimizationCheck->setChecked(false);
    ui.darkMultiplicationFactorValue->setValue(1.0);
    ui.cosmeticCorrectionGroupBox->setChecked(false);
    ui.coldSigmaCheck->setChecked(true);
    ui.coldSigmaValue->setValue(3.0);
    ui.hotSigmaCheck->setChecked(true);
    ui.hotSigmaValue->setValue(3.0);
    ui.flatFilePath->clear();
    ui.flatGroupBox->setChecked(false);
    ui.normalizeFlatsCheck->setChecked(true);
    ui.preserveOriginalNameCheck->setChecked(true);
    ui.filenameSuffixEdit->setText(QStringLiteral("_cal"));
    const int fits = ui.outputFormatSelector->findData(QStringLiteral("fits"));
    ui.outputFormatSelector->setCurrentIndex(fits >= 0 ? fits : 0);
    ui.outputPrecisionSelector->setCurrentIndex(0);
    ui.pedestalValue->setValue(0.0);
    ui.outputDirectory->clear();
    updateControls();
}

void ImageCalibrationProcess::closeEvent(QCloseEvent* evt)
{
    // Everything the window started stops with the window's token, requested as the base closes, and lands on a window
    // that is gone; nothing is waited for.
    if (m_impl->progress)
    {
        m_impl->progress->requestCancel();
    }
    TopLevelProcessBaseControl::closeEvent(evt);
}

std::string ImageCalibrationProcess::serialize_json_settings()
{
    const auto& ui = m_impl->ui;
    nlohmann::json document;
    document["version"] = kSettingsVersion;
    document["frames"] = ImageListProcessHelpers::imageFilePathsToJson(m_impl->files->entries());
    auto& s = document["settings"];
    s["bias_enabled"] = ui.biasGroupBox->isChecked();
    s["bias_path"] = ui.biasFilePath->text().toStdString();
    s["dark_enabled"] = ui.darkGroupBox->isChecked();
    s["dark_path"] = ui.darkFilePath->text().toStdString();
    s["flat_enabled"] = ui.flatGroupBox->isChecked();
    s["flat_path"] = ui.flatFilePath->text().toStdString();
    s["dark_optimization"] = ui.darkOptimizationCheck->isChecked();
    s["dark_multiplication_factor"] = ui.darkMultiplicationFactorValue->value();
    s["cosmetic_correction_enabled"] = ui.cosmeticCorrectionGroupBox->isChecked();
    s["cold_sigma_enabled"] = ui.coldSigmaCheck->isChecked();
    s["cold_sigma"] = ui.coldSigmaValue->value();
    s["hot_sigma_enabled"] = ui.hotSigmaCheck->isChecked();
    s["hot_sigma"] = ui.hotSigmaValue->value();
    s["normalize_flats"] = ui.normalizeFlatsCheck->isChecked();
    s["preserve_original_name"] = ui.preserveOriginalNameCheck->isChecked();
    s["filename_suffix"] = ui.filenameSuffixEdit->text().toStdString();
    s["output_format"] = ui.outputFormatSelector->currentData().toString().toStdString();
    s["use_32bit_float"] = ui.outputPrecisionSelector->currentData().toBool();
    s["pedestal_adu"] = ui.pedestalValue->value();
    s["output_directory"] = ui.outputDirectory->text().toStdString();
    return document.dump();
}

void ImageCalibrationProcess::deserialize_json_settings(const std::string& json_settings)
{
    if (json_settings.empty())
    {
        return;
    }
    nlohmann::json document = nlohmann::json::parse(json_settings, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        LOG_ERROR("Image Calibration: could not read its settings: {}", json_settings);
        return;
    }
    const int version = document.value("version", 1);
    if (version < 2 || !document.contains("settings") || !document["settings"].is_object())
    {
        return;
    }
    try
    {
        const auto& s = document["settings"];
        auto& ui = m_impl->ui;
        ui.biasGroupBox->setChecked(s.value("bias_enabled", false));
        ui.biasFilePath->setText(QString::fromStdString(s.value("bias_path", std::string())));
        ui.darkGroupBox->setChecked(s.value("dark_enabled", false));
        ui.darkFilePath->setText(QString::fromStdString(s.value("dark_path", std::string())));
        ui.darkOptimizationCheck->setChecked(s.value("dark_optimization", false));
        ui.darkMultiplicationFactorValue->setValue(s.value("dark_multiplication_factor", 1.0));
        ui.cosmeticCorrectionGroupBox->setChecked(s.value("cosmetic_correction_enabled", false));
        ui.coldSigmaCheck->setChecked(s.value("cold_sigma_enabled", true));
        ui.coldSigmaValue->setValue(s.value("cold_sigma", 3.0));
        ui.hotSigmaCheck->setChecked(s.value("hot_sigma_enabled", true));
        ui.hotSigmaValue->setValue(s.value("hot_sigma", 3.0));
        ui.flatGroupBox->setChecked(s.value("flat_enabled", false));
        ui.flatFilePath->setText(QString::fromStdString(s.value("flat_path", std::string())));
        ui.normalizeFlatsCheck->setChecked(s.value("normalize_flats", true));
        ui.preserveOriginalNameCheck->setChecked(s.value("preserve_original_name", true));
        ui.filenameSuffixEdit->setText(QString::fromStdString(s.value("filename_suffix", std::string("_cal"))));
        const int format = ui.outputFormatSelector->findData(
            QString::fromStdString(s.value("output_format", std::string("fits"))).trimmed().toLower());
        ui.outputFormatSelector->setCurrentIndex(format >= 0 ? format : 0);
        ui.outputPrecisionSelector->setCurrentIndex(s.value("use_32bit_float", true) ? 0 : 1);
        ui.pedestalValue->setValue(s.value("pedestal_adu", 0.0));
        const std::string directory = s.value("output_directory", std::string());
        if (!directory.empty())
        {
            ui.outputDirectory->setText(QString::fromStdString(directory));
        }
    }
    catch (const nlohmann::json::exception& error)
    {
        LOG_ERROR("Image Calibration: could not read its settings: {}", error.what());
    }
    updateControls();

    if (version >= 3)
    {
        const QStringList frames =
            ImageListProcessHelpers::imageFilePathsFromJson(document.value("frames", nlohmann::json::array()));
        if (!frames.isEmpty())
        {
            m_impl->files->restoreFiles(frames);
        }
    }
}

void ImageCalibrationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
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
