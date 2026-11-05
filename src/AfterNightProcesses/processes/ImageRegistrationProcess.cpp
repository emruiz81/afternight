// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image Registration window: the shared file list with each frame's registration figures, the registration and
/// alignment settings, and the runs that register, align and restore on the thread pool.

#include "stdafx.hpp"
#include "ImageRegistrationProcess.h"

#include "app/AppContextHelpers.h"
#include "app/CancellationToken.h"
#include "app/GaiaCatalogSettings.h"
#include "app/ObservationEpochUtils.h"
#include "app/PlateSolveConfiguration.h"
#include "app/PlateSolveService.h"
#include "app/PlateSolveTypes.h"
#include "app/UiStyleTokens.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ProgressDialogControl.h"
#include "controls/TableWidgetUtils.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/WorkflowToolFiles.h"
#include "processes/core/RegistrationRun.h"
#include "processes/core/StandaloneRegistrationPersistence.h"
#include "processes/core/StandaloneRegistrationUtils.h"
#include "processes/core/WorkflowToolCompanions.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

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
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStyle>
#include <QThreadPool>

#include <nlohmann/json.hpp>

#include <map>
#include <stop_token>
#include <unordered_map>
#include <utility>

using an::ui::RegisteredFrame;
using an::ui::RegisteredFrameSet;
using an::ui::RegistrationMetricRecord;
using an::ui::processes::WorkflowFile;
using an::ui::processes::WorkflowFileColumn;
using an::ui::processes::WorkflowFileList;

namespace
{

constexpr const char* kFilesContext = "__IMAGE_REG_ADD_FILES__";
constexpr const char* kOutputContext = "__IMAGE_REG_FOLDER__";

/// The persisted settings' version: 4 holds the whole registration settings record and the frames.
constexpr int kSettingsVersion = 4;

constexpr std::size_t kStatusColumn = 3u;
constexpr std::size_t kStarsColumn = 4u;
constexpr std::size_t kFwhmColumn = 5u;
constexpr std::size_t kSnrColumn = 6u;
constexpr std::size_t kEccentricityColumn = 7u;
constexpr std::size_t kZeroPointColumn = 8u;
constexpr std::size_t kRmsColumn = 9u;

const QString kReferenceMarker = QStringLiteral("   ➡");
const QString kFailedMarker = QStringLiteral("   ❗");
const QString kExcludedMarker = QStringLiteral("   \U0001F6AB");

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

[[nodiscard]] QString frameKey(const QString& path)
{
    return an::ui::registrationCanonicalFrameKey(path);
}

[[nodiscard]] QString formatFigure(const double value, const int precision = 3)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("-");
}

void setComboByData(QComboBox* combo, const QString& data)
{
    const int index = combo->findData(data);
    if (index >= 0)
    {
        combo->setCurrentIndex(index);
    }
}

/// The plate solve settings the application stores, as the plate-solve window reads them.
[[nodiscard]] an::ui::PlateSolveSettings readPlateSolveSettings()
{
    an::ui::PlateSolveSettings settings;
    auto* persistence = process_utils::currentGlobalSettings();
    if (persistence == nullptr)
    {
        return settings;
    }
    settings.defaultSolver =
        persistence->select(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER, SettingsDefaults::PLATESOLVE_DEFAULT_SOLVER);
    settings.astapPath = persistence->select(SettingsKeys::TOOLS_ASTAP_PATH, SettingsDefaults::TOOLS_ASTAP_PATH);
    settings.astrometryLocalPath =
        persistence->select(SettingsKeys::TOOLS_ASTROMETRY_LOCAL_PATH, SettingsDefaults::TOOLS_ASTROMETRY_LOCAL_PATH);
    settings.astrometryApiKey =
        persistence->select(SettingsKeys::TOOLS_ASTROMETRY_API_KEY, SettingsDefaults::TOOLS_ASTROMETRY_API_KEY);
    settings.indexPath =
        persistence->select(SettingsKeys::PLATESOLVE_INDEX_PATH, SettingsDefaults::PLATESOLVE_INDEX_PATH);
    settings.gaiaCatalogDirectory = persistence->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY,
                                                        SettingsDefaults::PLATESOLVE_GAIA_CATALOG_DIRECTORY);
    settings.gaiaCatalogPack =
        persistence->select(SettingsKeys::PLATESOLVE_GAIA_CATALOG_PACK, SettingsDefaults::PLATESOLVE_GAIA_CATALOG_PACK);
    settings.searchRadius = persistence
                                ->select(SettingsKeys::PLATESOLVE_SEARCH_RADIUS,
                                         QString::number(SettingsDefaults::PLATESOLVE_SEARCH_RADIUS))
                                .toDouble();
    settings.downsample =
        persistence
            ->select(SettingsKeys::PLATESOLVE_DOWNSAMPLE, QString::number(SettingsDefaults::PLATESOLVE_DOWNSAMPLE))
            .toInt();
    settings.timeoutSeconds =
        persistence->select(SettingsKeys::PLATESOLVE_TIMEOUT, QString::number(SettingsDefaults::PLATESOLVE_TIMEOUT))
            .toInt();
    settings.useHints =
        persistence->select(SettingsKeys::PLATESOLVE_USE_HINTS,
                            SettingsDefaults::PLATESOLVE_USE_HINTS ? QStringLiteral("1") : QStringLiteral("0")) ==
        QStringLiteral("1");
    const double focalLength = persistence
                                   ->select(SettingsKeys::PLATESOLVE_FOCAL_LENGTH_MM,
                                            QString::number(SettingsDefaults::PLATESOLVE_FOCAL_LENGTH_MM))
                                   .toDouble();
    if (focalLength > 0.0)
    {
        settings.focalLengthMm = focalLength;
    }
    const double pixelSize = persistence
                                 ->select(SettingsKeys::PLATESOLVE_PIXEL_SIZE_UM,
                                          QString::number(SettingsDefaults::PLATESOLVE_PIXEL_SIZE_UM))
                                 .toDouble();
    if (pixelSize > 0.0)
    {
        settings.pixelSizeUm = pixelSize;
    }
    return settings;
}

[[nodiscard]] FrameInfo frameInfoOf(const WorkflowFile& file)
{
    return FrameInfo(FrameId(file.path()), file.path(), file.figures);
}

/// The plate distortion a frame's own cards state, read as the registration reads every frame's: the polynomial of the
/// solution they describe, when it has one.
[[nodiscard]] std::optional<an::SipDistortion> ownDistortionOf(const WorkflowFile& file)
{
    const an::ImageExtent& extent = file.entry.descriptor.extent();
    const an::Result<an::WcsSolution> own =
        an::ui::readImageWcsSolution(file.entry.metadata.codec().cards(), extent.width(), extent.height());
    return own.hasValue() ? an::ui::processes::registrationDistortionOf(*own) : std::nullopt;
}

/// The plate distortion a registered frame's transform was solved over: the model its record states, its own cards'
/// when the record defers to them, and none when the record says none.
[[nodiscard]] an::SipDistortion solvedDistortionOf(const RegisteredFrame& frame, const WorkflowFile& file)
{
    switch (frame.distortion.policy)
    {
    case stage_utils::SourceDistortionPolicy::ExplicitModel:
        return frame.distortion.model.value_or(an::SipDistortion{});
    case stage_utils::SourceDistortionPolicy::InheritMetadata:
        return ownDistortionOf(file).value_or(an::SipDistortion{});
    case stage_utils::SourceDistortionPolicy::ExplicitNone:
        break;
    }
    return an::SipDistortion{};
}

[[nodiscard]] std::vector<FrameInfo> frameInfosOf(const std::vector<WorkflowFile>& files)
{
    std::vector<FrameInfo> frames;
    frames.reserve(files.size());
    for (const WorkflowFile& file : files)
    {
        frames.push_back(frameInfoOf(file));
    }
    return frames;
}

[[nodiscard]] std::vector<an::ui::StandaloneRegistrationFrame> helperFramesOf(const std::vector<WorkflowFile>& files)
{
    std::vector<an::ui::StandaloneRegistrationFrame> frames;
    frames.reserve(files.size());
    for (const WorkflowFile& file : files)
    {
        frames.push_back(an::ui::StandaloneRegistrationFrame{.path = file.path(), .filter = file.figures.filter()});
    }
    return frames;
}

/// A registered frame's record as the metrics table holds it.
[[nodiscard]] RegistrationMetricRecord metricOf(const QString& key, const RegisteredFrame& frame)
{
    RegistrationMetricRecord metric;
    metric.frameId = FrameId(key);
    metric.metrics = frame.metrics;
    metric.metrics.stars = static_cast<int>(frame.stars.size());
    metric.metrics.succeeded = !frame.failed;
    return metric;
}

/// The registered frames by their canonical identity, and their metrics.
void indexRegistration(const RegisteredFrameSet& set, std::unordered_map<QString, RegisteredFrame>& frames,
                       std::unordered_map<QString, RegistrationMetricRecord>& metrics)
{
    frames.clear();
    metrics.clear();
    for (const auto& [path, frame] : set.frames)
    {
        const QString key = frameKey(frame.sourcePath.isEmpty() ? path : frame.sourcePath);
        if (key.isEmpty())
        {
            continue;
        }
        frames.insert_or_assign(key, frame);
        metrics.insert_or_assign(key, metricOf(key, frame));
    }
}

/// What a run hands back to the window.
struct RegistrationOutcome
{
    RegisteredFrameSet registered;
    std::optional<an::ui::StandaloneRegistrationPersistenceContext> context;
    std::unordered_map<QString, RegistrationMetricRecord> metrics;
    QString referencePath;
    QString failure;
    bool stopped = false;
};

} // namespace

struct ImageRegistrationProcess::Impl
{
    an::ui::processes::ImageRegistrationProcessUiElements ui;
    ImageViewControl* preview = nullptr;
    std::unique_ptr<WorkflowFileList> files;

    RegisteredFrameSet registered;
    std::optional<an::ui::StandaloneRegistrationPersistenceContext> context;
    std::unordered_map<QString, RegistrationMetricRecord> metrics;
    std::unordered_map<QString, RegisteredFrame> framesByKey;
    std::unordered_set<QString> manualExcluded;
    std::unordered_set<QString> thresholdExcluded;
    an::ui::StandaloneRegistrationQualityThresholds thresholds;
    RegistrationSettings advanced;

    /// The reference's file; the reference row is where the list holds it.
    QString referencePath;

    /// Solutions a person found for frames through the plate-solve window, by canonical identity.
    std::unordered_map<QString, an::WcsSolution> solvedByHand;

    bool running = false;
    std::uint32_t workersInFlight = 0u;
    std::uint64_t restoreGeneration = 0u;
    QPointer<ProgressDialogControl> progress;
    QString statusText;

    [[nodiscard]] int rowOfReference() const
    {
        return referencePath.isEmpty() ? -1 : files->rowOf(referencePath);
    }

    [[nodiscard]] const RegisteredFrame* frameOf(const QString& path) const
    {
        const auto it = framesByKey.find(frameKey(path));
        return it == framesByKey.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const RegistrationMetricRecord* metricOfPath(const QString& path) const
    {
        const auto it = metrics.find(frameKey(path));
        return it == metrics.end() ? nullptr : &it->second;
    }
};

ImageRegistrationProcess::ImageRegistrationProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(parent);
    setProcessDescription(QStringLiteral("Register and align multi-frame image sets with configurable star-detection, "
                                         "matching, and alignment settings."));
    m_impl->ui = an::ui::processes::buildImageRegistrationProcessUi(this);
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto& ui = m_impl->ui;
    for (auto [button, variant] :
         {std::pair{ui.registerButton, "primary"}, std::pair{ui.alignButton, "primary"},
          std::pair{ui.clearRegisteredDataButton, "danger"}, std::pair{ui.removeFailedImagesButton, "danger"},
          std::pair{ui.addFiles, "secondary"}, std::pair{ui.removeSelected, "secondary"},
          std::pair{ui.moveUpButton, "secondary"}, std::pair{ui.moveDownButton, "secondary"},
          std::pair{ui.nextFailedButton, "secondary"}, std::pair{ui.blinkCompareButton, "secondary"},
          std::pair{ui.advancedSettingsButton, "secondary"}, std::pair{ui.applyThresholdsButton, "secondary"},
          std::pair{ui.resetThresholdsButton, "secondary"}, std::pair{ui.clearAll, "danger"},
          std::pair{ui.outputDirectoryButton, "secondary"}})
    {
        button->setProperty("anButtonVariant", QString::fromLatin1(variant));
    }

    ui.registrationMethodSelector->addItem(QStringLiteral("Star Matching"), QStringLiteral("star_matching"));
    ui.registrationMethodSelector->addItem(QStringLiteral("Phase Correlation"), QStringLiteral("phase_correlation"));
    ui.registrationMethodSelector->addItem(QStringLiteral("Comet / Asteroid Tracking"),
                                           QStringLiteral("comet_tracking"));
    ui.referenceModeSelector->addItem(QStringLiteral("Manual"), QStringLiteral("manual"));
    ui.referenceModeSelector->addItem(QStringLiteral("Auto"), QStringLiteral("auto"));
    for (const QString* text :
         {&process_utils::INTERP_AUTO, &process_utils::INTERP_NEAREST_NEIGHBOR, &process_utils::INTERP_BILINEAR,
          &process_utils::INTERP_BICUBIC, &process_utils::INTERP_AREA, &process_utils::INTERP_LANCZOS4,
          &process_utils::INTERP_BIT_EXACT_BILINEAR, &process_utils::INTERP_BIT_EXACT_NEAREST_NEIGHBOR})
    {
        ui.interpolationSelector->addItem(*text);
    }
    ui.transformationSelector->addItems({QStringLiteral("Similarity"), QStringLiteral("Affine"),
                                         QStringLiteral("Homography"), QStringLiteral("Polynomial 2"),
                                         QStringLiteral("Polynomial 3"), QStringLiteral("Polynomial 4"),
                                         QStringLiteral("Auto")});

    QObject::connect(ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageRegistrationProcess::on_dialog_box_button_clicked);
    QObject::connect(ui.addFiles, &QPushButton::released, this, &ImageRegistrationProcess::on_addFilesButtonPushed);
    QObject::connect(ui.addFolder, &QPushButton::released, this, &ImageRegistrationProcess::on_addFolderButtonPushed);
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
    QObject::connect(ui.blinkCompareButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_blinkCompareButtonPushed);
    QObject::connect(ui.advancedSettingsButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_advancedSettingsButtonPushed);
    QObject::connect(ui.applyThresholdsButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_applyThresholdsButtonPushed);
    QObject::connect(ui.resetThresholdsButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_resetThresholdsButtonPushed);
    QObject::connect(ui.registrationMethodSelector, &QComboBox::currentTextChanged, this,
                     [this]() { on_registrationMethodChanged(); });
    QObject::connect(ui.referenceModeSelector, &QComboBox::currentTextChanged, this,
                     [this]() { on_referenceModeChanged(); });
    QObject::connect(ui.registerButton, &QPushButton::released, this, &ImageRegistrationProcess::registerFrames);
    QObject::connect(ui.alignButton, &QPushButton::released, this, &ImageRegistrationProcess::alignFrames);
    QObject::connect(ui.clearRegisteredDataButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_clearRegisteredDataButtonPushed);
    QObject::connect(ui.removeFailedImagesButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_removeFailedImagesButtonPushed);
    QObject::connect(ui.nextFailedButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_nextFailedButtonPushed);
    QObject::connect(ui.outputDirectoryButton, &QPushButton::released, this,
                     &ImageRegistrationProcess::on_outputDirectoryButtonPushed);
    QObject::connect(ui.referenceImageValue, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_impl->ui.referenceImageValue->isEnabled())
        {
            setReferenceRow(value - 1);
        }
    });
    // A person's toggle is the stored choice; what a box shows for want of a solver or a catalog is not, and the choice
    // is kept for when one is configured, as the integration's dialog keeps it. Connected before the refresh, which
    // reads the choice.
    QObject::connect(ui.autoSolveUnsolvedFramesCheck, &QCheckBox::toggled, this,
                     [this](bool checked) { m_impl->advanced.autoSolveUnsolvedFrames = checked; });
    QObject::connect(ui.enrichWithGaiaCatalogCheck, &QCheckBox::toggled, this,
                     [this](bool checked) { m_impl->advanced.enrichWithGaiaCatalog = checked; });
    const auto refresh = [this]() { refreshTable(); };
    QObject::connect(ui.minStarParirsValue, &QSpinBox::valueChanged, this, refresh);
    QObject::connect(ui.maxStarParirsValue, &QSpinBox::valueChanged, this, refresh);
    QObject::connect(ui.transformationSelector, &QComboBox::currentTextChanged, this, refresh);
    QObject::connect(ui.interpolationSelector, &QComboBox::currentTextChanged, this, refresh);
    for (QRadioButton* mode : {ui.alignModeStandard, ui.alignModeMosaic, ui.alignModeIntersection})
    {
        QObject::connect(mode, &QRadioButton::toggled, this, refresh);
    }
    for (QCheckBox* box :
         {ui.chromaticAberrationCorrectionCheck, ui.autoSolveUnsolvedFramesCheck, ui.enrichWithGaiaCatalogCheck})
    {
        QObject::connect(box, &QCheckBox::toggled, this, refresh);
    }

    m_impl->preview = process_utils::createStaticPreviewInPlaceholder(ui.imagePreviewWidget, "Image Preview");
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
        ui.nextFailedButton->setIcon(iconProvider->getIcon("priority_high"));
    }

    an::ui::processes::WorkflowFileListOptions options;
    options.columns = {{QStringLiteral("File"), WorkflowFileColumn::File},
                       {QStringLiteral("Filter"), WorkflowFileColumn::Filter},
                       {QStringLiteral("Exposure"), WorkflowFileColumn::Exposure},
                       {QStringLiteral("Status"), WorkflowFileColumn::Custom},
                       {QStringLiteral("Stars"), WorkflowFileColumn::Custom},
                       {QStringLiteral("FWHM"), WorkflowFileColumn::Custom},
                       {QStringLiteral("SNR"), WorkflowFileColumn::Custom},
                       {QStringLiteral("Ecc."), WorkflowFileColumn::Custom},
                       {QStringLiteral("Zero Point"), WorkflowFileColumn::Custom},
                       {QStringLiteral("RMS"), WorkflowFileColumn::Custom}};
    // The frames stay in the order they were added until a column is clicked.
    options.initialSortColumn = std::nullopt;
    options.customText = [this](int, const WorkflowFile& file, const std::size_t column) -> QString {
        const RegistrationMetricRecord* metric = m_impl->metricOfPath(file.path());
        const QString key = frameKey(file.path());
        switch (column)
        {
        case kStatusColumn: {
            const an::ui::StandaloneRegistrationExclusion exclusion =
                an::ui::evaluateStandaloneFrameExclusion(key, m_impl->manualExcluded, m_impl->thresholdExcluded);
            if (exclusion.excluded)
            {
                return exclusion.detail;
            }
            if (metric == nullptr)
            {
                return QStringLiteral("Not registered");
            }
            return metric->metrics.succeeded ? QStringLiteral("Registered") : QStringLiteral("Failed");
        }
        case kStarsColumn:
            return metric != nullptr ? QString::number(metric->metrics.stars) : QStringLiteral("-");
        case kFwhmColumn:
            return metric != nullptr ? formatFigure(metric->metrics.fwhm) : QStringLiteral("-");
        case kSnrColumn:
            return metric != nullptr ? formatFigure(metric->metrics.snr) : QStringLiteral("-");
        case kEccentricityColumn:
            return metric != nullptr ? formatFigure(metric->metrics.eccentricity) : QStringLiteral("-");
        case kZeroPointColumn:
            return metric != nullptr && metric->metrics.photometric_zero_point != 0.0
                       ? formatFigure(metric->metrics.photometric_zero_point)
                       : QStringLiteral("-");
        case kRmsColumn:
            if (metric != nullptr && std::isfinite(metric->metrics.astrometric_rms_arcsec) &&
                metric->metrics.astrometric_rms_arcsec != 0.0)
            {
                return QString::number(metric->metrics.astrometric_rms_arcsec, 'f', 3) + QStringLiteral("″");
            }
            if (metric != nullptr && std::isfinite(metric->metrics.astrometric_rms_pixels) &&
                metric->metrics.astrometric_rms_pixels != 0.0)
            {
                return QString::number(metric->metrics.astrometric_rms_pixels, 'f', 3) + QStringLiteral(" px");
            }
            return QStringLiteral("-");
        default:
            return {};
        }
    };
    options.extendMenu = [this](QMenu& menu, int row) { extendContextMenu(menu, row); };
    options.busy = [this]() { return m_impl->running; };
    options.stop = windowStopToken();
    m_impl->files = std::make_unique<WorkflowFileList>(
        this,
        an::ui::processes::WorkflowFileListWidgets{.table = ui.tableWidget,
                                                   .removeSelected = ui.removeSelected,
                                                   .moveUp = ui.moveUpButton,
                                                   .moveDown = ui.moveDownButton,
                                                   .clearAll = ui.clearAll,
                                                   .currentLabel = ui.currentImageLabel},
        std::move(options));
    m_impl->files->onChanged = [this]() { listChanged(); };
    m_impl->files->onSelected = [this](int row) { rowSelected(row); };
    m_impl->files->onSelectionChanged = [this]() { updateControls(); };
    m_impl->files->onImportFinished = [this](const an::ui::processes::WorkflowImportReport& report) {
        an::ui::processes::sayWorkflowImport(this, report);
        restoreRecordedRegistration();
    };

    resetSettings();
    initializeTopLevelProcess();
}

ImageRegistrationProcess::~ImageRegistrationProcess() = default;

std::string ImageRegistrationProcess::getProcessNameStatic()
{
    return "Image Registration";
}

void ImageRegistrationProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
    if (m_impl->preview != nullptr)
    {
        m_impl->preview->refreshTheme();
    }
    m_impl->ui.tableWidget->setupSelectionColors();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        m_impl->ui.moveUpButton->setIcon(iconProvider->getIcon("up"));
        m_impl->ui.moveDownButton->setIcon(iconProvider->getIcon("down"));
        m_impl->ui.nextFailedButton->setIcon(iconProvider->getIcon("priority_high"));
    }
    refreshTable();
}

void ImageRegistrationProcess::addItemsNonBlocking(const QStringList& items)
{
    m_impl->files->addFiles(items);
}

void ImageRegistrationProcess::removeItems(const std::vector<QString>& items)
{
    if (!m_impl->running)
    {
        m_impl->files->remove(items);
    }
}

const WorkflowFileList& ImageRegistrationProcess::fileList() const
{
    return *m_impl->files;
}

ImageViewControl* ImageRegistrationProcess::previewView() const
{
    return m_impl->preview;
}

bool ImageRegistrationProcess::isIdle() const
{
    return m_impl->files->idle() && !m_impl->running && m_impl->workersInFlight == 0u;
}

QString ImageRegistrationProcess::statusText() const
{
    return m_impl->statusText;
}

an::ui::processes::ImageRegistrationProcessUiElements& ImageRegistrationProcess::ui()
{
    return m_impl->ui;
}

const RegisteredFrameSet& ImageRegistrationProcess::registeredFrames() const
{
    return m_impl->registered;
}

int ImageRegistrationProcess::referenceRow() const
{
    return m_impl->rowOfReference();
}

std::unordered_set<QString> ImageRegistrationProcess::ignoredFrameIds() const
{
    std::unordered_set<QString> ignored = m_impl->manualExcluded;
    ignored.insert(m_impl->thresholdExcluded.begin(), m_impl->thresholdExcluded.end());
    return ignored;
}

void ImageRegistrationProcess::setReferenceRow(const int row)
{
    const auto& files = m_impl->files->files();
    if (files.empty())
    {
        m_impl->referencePath.clear();
        refreshTable();
        return;
    }
    const int clamped = std::clamp(row, 0, static_cast<int>(files.size()) - 1);
    const QString path = files[static_cast<std::size_t>(clamped)].path();
    if (path == m_impl->referencePath)
    {
        return;
    }
    m_impl->referencePath = path;
    auto* spin = m_impl->ui.referenceImageValue;
    const QSignalBlocker blocker(spin);
    spin->setValue(clamped + 1);
    refreshTable();
}

void ImageRegistrationProcess::setFramesExcluded(const std::vector<QString>& paths, const bool excluded)
{
    for (const QString& path : paths)
    {
        const QString key = frameKey(path);
        if (key.isEmpty())
        {
            continue;
        }
        if (excluded)
        {
            m_impl->manualExcluded.insert(key);
        }
        else
        {
            m_impl->manualExcluded.erase(key);
            m_impl->thresholdExcluded.erase(key);
        }
    }
    refreshTable();
}

RegistrationSettings ImageRegistrationProcess::registrationSettings() const
{
    const auto& ui = m_impl->ui;
    RegistrationSettings settings = m_impl->advanced;
    settings.version = 0;
    settings.minStarPairs = ui.minStarParirsValue->value();
    settings.maxStarPairs = ui.maxStarParirsValue->value();
    settings.registrationMethod = ui.registrationMethodSelector->currentData().toString();
    settings.autoReferenceDuringRegistration =
        ui.referenceModeSelector->currentData().toString() == QStringLiteral("auto");
    // A method that needs a fixed reference never chooses one itself, whatever the selector last held.
    if (ImageIntegration::RegistrationSettingsUtils::requiresFixedReference(settings.registrationMethod))
    {
        settings.autoReferenceDuringRegistration = false;
    }
    const QString transform = ui.transformationSelector->currentText().trimmed();
    if (transform.startsWith(QStringLiteral("Polynomial")))
    {
        settings.transformation = QStringLiteral("Homography");
        settings.transformMode = QStringLiteral("polynomial");
        settings.transformOrder = transform.right(1).toInt();
    }
    else if (transform.compare(QStringLiteral("Auto"), Qt::CaseInsensitive) == 0)
    {
        settings.transformation = QStringLiteral("Homography");
        settings.transformMode = QStringLiteral("auto");
        settings.transformOrder = 0;
    }
    else
    {
        settings.transformation = transform;
        settings.transformMode = QStringLiteral("linear");
        settings.transformOrder = 0;
    }
    settings.twoPassRefinementEnabled =
        settings.transformMode == QStringLiteral("polynomial") || settings.transformMode == QStringLiteral("auto");
    settings.chromaticAberration.enabled = ui.chromaticAberrationCorrectionCheck->isChecked();
    settings.alignmentMode =
        ui.alignModeMosaic->isChecked()
            ? QStringLiteral("mosaic")
            : (ui.alignModeIntersection->isChecked() ? QStringLiteral("intersection") : QStringLiteral("standard"));
    settings.interpolation = ui.interpolationSelector->currentText().trimmed();
    settings.referenceImage = m_impl->referencePath;
    return settings;
}

void ImageRegistrationProcess::applyRegistrationSettings(const RegistrationSettings& settings)
{
    auto& ui = m_impl->ui;
    m_impl->advanced = settings;
    ui.minStarParirsValue->setValue(settings.minStarPairs);
    ui.maxStarParirsValue->setValue(settings.maxStarPairs);
    setComboByData(
        ui.registrationMethodSelector,
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod));
    setComboByData(ui.referenceModeSelector,
                   settings.autoReferenceDuringRegistration ? QStringLiteral("auto") : QStringLiteral("manual"));
    QString transformation = settings.transformation;
    if (settings.transformMode.compare(QStringLiteral("polynomial"), Qt::CaseInsensitive) == 0)
    {
        transformation = QStringLiteral("Polynomial %1").arg(settings.transformOrder);
    }
    else if (settings.transformMode.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
    {
        transformation = QStringLiteral("Auto");
    }
    if (ui.transformationSelector->findText(transformation) >= 0)
    {
        ui.transformationSelector->setCurrentText(transformation);
    }
    ui.autoSolveUnsolvedFramesCheck->setChecked(settings.autoSolveUnsolvedFrames);
    ui.enrichWithGaiaCatalogCheck->setChecked(settings.enrichWithGaiaCatalog);
    ui.chromaticAberrationCorrectionCheck->setChecked(settings.chromaticAberration.enabled);
    if (settings.alignmentMode == QStringLiteral("mosaic"))
    {
        ui.alignModeMosaic->setChecked(true);
    }
    else if (settings.alignmentMode == QStringLiteral("intersection"))
    {
        ui.alignModeIntersection->setChecked(true);
    }
    else
    {
        ui.alignModeStandard->setChecked(true);
    }
    if (ui.interpolationSelector->findText(settings.interpolation) >= 0)
    {
        ui.interpolationSelector->setCurrentText(settings.interpolation);
    }
    if (!settings.referenceImage.trimmed().isEmpty())
    {
        m_impl->referencePath = settings.referenceImage;
    }
    on_registrationMethodChanged();
    updateAutoSolveAndEnrich();
}

bool ImageRegistrationProcess::registrationIsCurrent() const
{
    if (!m_impl->context.has_value() || m_impl->registered.frames.empty())
    {
        return false;
    }
    const auto current = an::ui::buildStandaloneRegistrationPersistenceContext(
        m_impl->ui.outputDirectory->text().trimmed(), frameInfosOf(m_impl->files->files()), registrationSettings(),
        nullptr);
    if (!current.has_value())
    {
        return false;
    }
    const auto& recorded = *m_impl->context;
    return current->groupKey == recorded.groupKey && current->settingsVersion == recorded.settingsVersion &&
           current->detectionHash == recorded.detectionHash && current->registrationHash == recorded.registrationHash &&
           current->referenceFrameId == recorded.referenceFrameId &&
           current->lightFilesHash == recorded.lightFilesHash && current->calibrationHash == recorded.calibrationHash;
}

void ImageRegistrationProcess::registerFrames()
{
    const an::consumer::EngineSession* session = currentSession();
    if (!isIdle() || session == nullptr || m_impl->files->files().size() < 2u)
    {
        return;
    }
    const RegistrationSettings settings = registrationSettings();
    const QString method =
        ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(settings.registrationMethod);
    if (method == QStringLiteral("comet_tracking"))
    {
        const auto& comet = settings.cometTracking;
        const bool ready = comet.has_value() && !settings.referenceImage.trimmed().isEmpty() &&
                           std::count_if(comet->observations.cbegin(), comet->observations.cend(),
                                         [](const auto& observation) { return observation.enabled; }) >= 2;
        if (!ready)
        {
            process_utils::notifyErrorMessage(
                this, QStringLiteral("Error running image registration"),
                ImageIntegration::RegistrationSettingsUtils::cometTrackingTemporarilyDisabledMessage());
            return;
        }
    }
    if (method == QStringLiteral("phase_correlation") && settings.autoReferenceDuringRegistration)
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Error running image registration"),
                                          QStringLiteral("Auto reference is only available for star-matching "
                                                         "registration."));
        return;
    }
    const std::vector<ImageListProcessHelpers::ImageListEntry> entries = m_impl->files->entries();
    if (!ImageListProcessHelpers::validateImageListSizes(entries) ||
        !ImageListProcessHelpers::validateImageListColorDepth(entries))
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Error running image registration"),
                                          QStringLiteral("Some images in the list have different size or color depth"));
        return;
    }

    // Everything the run reads is read here, on the GUI thread.
    const std::vector<WorkflowFile> files = m_impl->files->files();
    const QString outputDirectory = m_impl->ui.outputDirectory->text().trimmed();
    const bool reuse = m_impl->ui.reprocessAlreadyRegisteredmages->isChecked();
    const std::unordered_set<QString> ignored = ignoredFrameIds();
    const an::ui::StandaloneRegistrationQualityThresholds thresholds = m_impl->thresholds;
    const an::ui::PlateSolveSettings plateSolve = readPlateSolveSettings();
    const QString catalogDirectory = an::ui::resolveGaiaCatalogDirectory(plateSolve).trimmed();
    const std::unordered_map<QString, an::WcsSolution> solvedByHand = m_impl->solvedByHand;

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Registration"));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->show();
    m_impl->progress = progress;
    m_impl->running = true;
    updateControls();

    QPointer<ImageRegistrationProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = windowStopToken();
    const std::stop_token cancel = progress->stopToken();
    QThreadPool::globalInstance()->start([session, settings, method, files, outputDirectory, reuse, ignored, thresholds,
                                          plateSolve, catalogDirectory, solvedByHand, guard, dialog, windowStop,
                                          cancel]() {
        std::stop_source stop;
        const std::stop_callback onClose(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback onCancel(cancel, [&stop]() { stop.request_stop(); });
        const auto report = [dialog](int percent, const QString& text) {
            postToGui([dialog, percent, text]() {
                if (dialog)
                {
                    dialog->setValue(percent);
                    if (!text.isEmpty())
                    {
                        dialog->setText(text);
                    }
                }
            });
        };
        QElapsedTimer timer;
        timer.start();
        auto outcome = std::make_shared<RegistrationOutcome>();
        outcome->referencePath = settings.referenceImage;
        const std::vector<FrameInfo> frameInfos = frameInfosOf(files);

        [&]() {
            QString error;
            outcome->context =
                an::ui::buildStandaloneRegistrationPersistenceContext(outputDirectory, frameInfos, settings, &error);
            if (!outcome->context.has_value())
            {
                outcome->failure = QStringLiteral("Could not build registration persistence context: %1").arg(error);
                return;
            }

            if (method == QStringLiteral("comet_tracking"))
            {
                an::Result<an::ui::processes::StandaloneCometRegistrationOutcome> comet =
                    an::ui::processes::runStandaloneCometRegistration(
                        *session,
                        an::ui::processes::StandaloneCometRegistrationRequest{.frames = frameInfos,
                                                                              .settings = settings,
                                                                              .context = *outcome->context,
                                                                              .ignoredFrameIds = ignored},
                        stop.get_token(), report);
                if (!comet.hasValue())
                {
                    outcome->stopped = comet.status().code() == an::ErrorCode::Cancelled;
                    outcome->failure = QString::fromStdString(comet.status().message());
                    return;
                }
                outcome->registered = std::move(comet->registered);
                return;
            }

            // A recorded registration of the same frames and settings is reused whole; one of fewer frames lends
            // the stars it found.
            std::map<QString, RegisteredFrame> reused;
            if (reuse)
            {
                const an::ui::StandaloneRegistrationReuseCheck check =
                    an::ui::checkReusableStandaloneRegistrationState(*outcome->context, frameInfos, ignored, &error);
                if (check.reusable || check.starDataReusable)
                {
                    const auto loaded = an::ui::loadStandaloneRegistrationState(*outcome->context, frameInfos, &error);
                    if (loaded.has_value() && !loaded->registeredData.frames.empty())
                    {
                        if (check.reusable)
                        {
                            LOG_INFO("Image Registration: reusing the recorded registration");
                            outcome->registered = loaded->registeredData;
                            outcome->context = loaded->context;
                            outcome->referencePath = loaded->context.referenceFrameId;
                            return;
                        }
                        reused = loaded->registeredData.frames;
                        LOG_INFO("Image Registration: reusing the stars of {} recorded frames ({})", reused.size(),
                                 check.reason.toStdString());
                    }
                }
            }

            const an::Result<an::ui::processes::RegistrationModel> model =
                an::ui::processes::registrationModelOf(settings);
            if (!model.hasValue())
            {
                outcome->failure = QString::fromStdString(model.status().message());
                return;
            }
            const an::ui::processes::RegistrationDemosaic demosaic =
                an::ui::processes::registrationDemosaicOf(settings);
            const an::StarDetectionRequest detection = an::ui::processes::registrationStarDetection(settings);
            if (const QString adjusted = an::ui::processes::registrationStarDetectionAdjustment(settings);
                !adjusted.isEmpty())
            {
                LOG_WARNING("Image Registration: {}", adjusted.toStdString());
            }
            const bool enrich = settings.enrichWithGaiaCatalog && an::ui::isGaiaCatalogReady(plateSolve);
            const bool autoSolve =
                settings.autoSolveUnsolvedFrames && an::ui::hasAnyConfiguredPlateSolveSolver(plateSolve);

            // Each frame's solution: its own cards, one a person found, or one the configured solver finds.
            std::map<QString, an::WcsSolution> solutions;
            an::ui::PlateSolveService solver;
            an::ui::CancellationToken solveToken;
            const std::stop_callback onSolveStop(stop.get_token(), [&solveToken]() { solveToken.requestCancel(); });
            for (std::size_t index = 0u; index < files.size(); ++index)
            {
                const WorkflowFile& file = files[index];
                const an::ImageExtent& extent = file.entry.descriptor.extent();
                const an::Result<an::WcsSolution> own =
                    an::ui::readImageWcsSolution(file.entry.metadata.codec().cards(), extent.width(), extent.height());
                if (own.hasValue())
                {
                    solutions.insert_or_assign(file.path(), *own);
                    continue;
                }
                if (const auto hand = solvedByHand.find(frameKey(file.path())); hand != solvedByHand.end())
                {
                    solutions.insert_or_assign(file.path(), hand->second);
                    continue;
                }
                if (!autoSolve || ignored.contains(frameKey(file.path())))
                {
                    continue;
                }
                if (stop.stop_requested())
                {
                    outcome->stopped = true;
                    return;
                }
                report(0, QStringLiteral("Plate-solving %1").arg(QFileInfo(file.path()).fileName()));
                an::ui::PlateSolveRequest request;
                request.imagePath = file.path();
                request.existingMetadata = file.entry.metadata;
                request.imageWidth = static_cast<int>(extent.width());
                request.imageHeight = static_cast<int>(extent.height());
                request.workDirectoryRoot = outputDirectory;
                request.requestTag = QStringLiteral("standalone_registration");
                const an::ui::PlateSolveResult solved = solver.solve(request, plateSolve, solveToken);
                if (solved.succeeded && solved.solution.has_value())
                {
                    solutions.insert_or_assign(file.path(), *solved.solution);
                }
                else if (!solved.errorMessage.isEmpty())
                {
                    LOG_WARNING("Image Registration: plate solve failed for {}: {}", file.path().toStdString(),
                                solved.errorMessage.toStdString());
                }
            }

            // Every frame's stars, from the recorded registration where it holds them.
            std::map<QString, std::vector<an::DetectedStar>> detected;
            for (std::size_t index = 0u; index < files.size(); ++index)
            {
                const QString& path = files[index].path();
                if (stop.stop_requested())
                {
                    outcome->stopped = true;
                    return;
                }
                const auto kept = reused.find(path);
                const bool needsCatalog = enrich && solutions.contains(path) && kept != reused.end() &&
                                          kept->second.metrics.photometric_zero_point == 0.0;
                if (kept != reused.end() && !kept->second.failed && !needsCatalog)
                {
                    outcome->registered.frames.insert_or_assign(path, kept->second);
                    continue;
                }
                report(static_cast<int>((index * 75u) / files.size()),
                       QStringLiteral("Detecting stars on %1").arg(QFileInfo(path).fileName()));
                RegisteredFrame frame;
                frame.sourcePath = path;
                an::Result<an::Image<float>> image =
                    an::ui::processes::readRegistrationFrame(*session, path, demosaic, stop.get_token());
                if (image.hasValue())
                {
                    an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                        .purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop.get_token()});
                    if (!scope.hasValue())
                    {
                        outcome->stopped = scope.status().code() == an::ErrorCode::Cancelled;
                        outcome->failure = QString::fromStdString(scope.status().message());
                        return;
                    }
                    if (settings.chromaticAberration.enabled)
                    {
                        const an::Result<an::ui::processes::ChromaticCorrection> corrected =
                            an::ui::processes::correctChromaticAberration(scope->engine(), scope->execution(), *image,
                                                                          settings.chromaticAberration, detection);
                        if (!corrected.hasValue())
                        {
                            LOG_WARNING("Image Registration: chromatic aberration of {} not corrected: {}",
                                        path.toStdString(), corrected.status().message());
                        }
                    }
                    an::Result<an::ui::processes::FrameDetection> found =
                        an::ui::processes::detectRegistrationStars(scope->execution(), *image, detection);
                    if (found.hasValue())
                    {
                        frame = std::move(found->frame);
                        frame.sourcePath = path;
                        detected.insert_or_assign(path, std::move(found->stars));
                    }
                    else if (found.status().code() == an::ErrorCode::Cancelled)
                    {
                        outcome->stopped = true;
                        return;
                    }
                    else
                    {
                        LOG_ERROR("Image Registration: no stars detected on {}: {}", path.toStdString(),
                                  found.status().message());
                        frame.failed = true;
                        frame.metrics.succeeded = false;
                    }
                }
                else if (image.status().code() == an::ErrorCode::Cancelled)
                {
                    outcome->stopped = true;
                    return;
                }
                else
                {
                    LOG_ERROR("Image Registration: failed to load {}: {}", path.toStdString(),
                              image.status().message());
                    frame.failed = true;
                    frame.metrics.succeeded = false;
                }
                outcome->registered.frames.insert_or_assign(path, std::move(frame));
            }

            // The reference: the best frame by its figures when it is chosen automatically, the row's otherwise.
            if (settings.autoReferenceDuringRegistration && !model->phaseCorrelation)
            {
                std::unordered_map<QString, RegistrationMetricRecord> metrics;
                std::unordered_map<QString, RegisteredFrame> byKey;
                indexRegistration(outcome->registered, byKey, metrics);
                const std::unordered_set<QString> excluded = [&]() {
                    std::unordered_set<QString> all = ignored;
                    const auto overThreshold =
                        an::ui::computeStandaloneThresholdExcludedFrameIds(metrics, helperFramesOf(files), thresholds);
                    all.insert(overThreshold.begin(), overThreshold.end());
                    return all;
                }();
                QString pickError;
                const auto pick =
                    an::ui::pickStandaloneAutoReference(helperFramesOf(files), metrics, excluded, pickError);
                if (pick.has_value())
                {
                    for (const WorkflowFile& file : files)
                    {
                        if (frameKey(file.path()) == pick->id.canonicalPath())
                        {
                            outcome->referencePath = file.path();
                        }
                    }
                }
                else if (!pickError.isEmpty())
                {
                    LOG_WARNING("Image Registration: auto-reference selection failed: {}", pickError.toStdString());
                }
            }
            if (outcome->referencePath.isEmpty() || !outcome->registered.frames.contains(outcome->referencePath))
            {
                outcome->referencePath = files.front().path();
            }

            // The frames matched to the reference.
            const an::ImageExtent& extent = files.front().entry.descriptor.extent();
            if (model->phaseCorrelation)
            {
                an::Result<an::Image<float>> reference = an::ui::processes::readRegistrationFrame(
                    *session, outcome->referencePath, demosaic, stop.get_token());
                if (!reference.hasValue())
                {
                    outcome->stopped = reference.status().code() == an::ErrorCode::Cancelled;
                    outcome->failure = QString::fromStdString(reference.status().message());
                    return;
                }
                std::size_t done = 0u;
                for (auto& [path, frame] : outcome->registered.frames)
                {
                    report(75 + static_cast<int>((done++ * 25u) / outcome->registered.frames.size()),
                           QStringLiteral("Computing transformations..."));
                    // The translation is measured between the frames as they are, so no distortion is undone under it.
                    frame.distortion = stage_utils::SourceDistortionProvenance::explicitNone();
                    if (path == outcome->referencePath)
                    {
                        frame.transform = an::RegistrationTransform{};
                        frame.failed = false;
                        frame.metrics.succeeded = true;
                        continue;
                    }
                    an::Result<an::Image<float>> image =
                        an::ui::processes::readRegistrationFrame(*session, path, demosaic, stop.get_token());
                    an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                        .purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop.get_token()});
                    if (!scope.hasValue() || (!image.hasValue() && image.status().code() == an::ErrorCode::Cancelled))
                    {
                        outcome->stopped = true;
                        return;
                    }
                    const an::Result<an::RegistrationTransform> transform =
                        image.hasValue()
                            ? an::ui::processes::phaseCorrelationTransform(scope->execution(), *reference, *image)
                            : an::Result<an::RegistrationTransform>(image.status());
                    if (transform.hasValue())
                    {
                        frame.transform = *transform;
                        frame.failed = false;
                        frame.metrics.succeeded = true;
                    }
                    else
                    {
                        LOG_WARNING("Image Registration: '{}' was not registered: {}", path.toStdString(),
                                    transform.status().message());
                        frame.transform.reset();
                        frame.failed = true;
                        frame.metrics.succeeded = false;
                    }
                }
            }
            else
            {
                an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                    .purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop.get_token()});
                if (!scope.hasValue())
                {
                    outcome->stopped = scope.status().code() == an::ErrorCode::Cancelled;
                    outcome->failure = QString::fromStdString(scope.status().message());
                    return;
                }
                // Each frame's stars are matched where its own plate distortion corrects them to, and the alignment
                // undoes that distortion: every record states it.
                for (auto& [path, frame] : outcome->registered.frames)
                {
                    const auto file = std::find_if(files.begin(), files.end(),
                                                   [&path](const WorkflowFile& f) { return f.path() == path; });
                    const std::optional<an::SipDistortion> distortion =
                        file != files.end() ? ownDistortionOf(*file) : std::nullopt;
                    frame.distortion = distortion.has_value()
                                           ? stage_utils::SourceDistortionProvenance::explicitModel(*distortion)
                                           : stage_utils::SourceDistortionProvenance::explicitNone();
                }
                report(75, QStringLiteral("Computing transformations..."));
                const an::Status matched = an::ui::processes::matchRegistrationStars(
                    scope->execution(), outcome->registered, outcome->referencePath, *model, extent.width(),
                    extent.height(), [&report](std::size_t done, std::size_t total) {
                        report(75 + static_cast<int>((done * 25u) / (std::max)(total, std::size_t{1})), {});
                    });
                if (matched.failed())
                {
                    outcome->stopped = matched.code() == an::ErrorCode::Cancelled;
                    outcome->failure = QString::fromStdString(matched.message());
                    return;
                }
                // The catalog's figures over each solved frame whose stars were detected in this run.
                if (enrich)
                {
                    int enriched = 0;
                    for (auto& [path, frame] : outcome->registered.frames)
                    {
                        const auto solution = solutions.find(path);
                        const auto stars = detected.find(path);
                        if (frame.failed || solution == solutions.end() || stars == detected.end())
                        {
                            continue;
                        }
                        const auto file = std::find_if(files.begin(), files.end(),
                                                       [&path](const WorkflowFile& f) { return f.path() == path; });
                        const std::optional<double> epoch =
                            file != files.end()
                                ? an::ui::resolveObservationEpochYear(file->entry.metadata.acquisition())
                                : std::nullopt;
                        const an::Result<bool> done = an::ui::processes::enrichRegisteredFrame(
                            scope->execution(), frame, stars->second, solution->second, catalogDirectory, epoch);
                        if (!done.hasValue() && done.status().code() == an::ErrorCode::Cancelled)
                        {
                            outcome->stopped = true;
                            return;
                        }
                        enriched += done.hasValue() && *done ? 1 : 0;
                    }
                    LOG_INFO("Image Registration: catalog enrichment of {} frames", enriched);
                }
            }
            outcome->registered.referencePath = outcome->referencePath;

            // The registration is recorded under the reference it ran with, and read back as recorded.
            RegistrationSettings recorded = settings;
            recorded.referenceImage = outcome->referencePath;
            outcome->context =
                an::ui::buildStandaloneRegistrationPersistenceContext(outputDirectory, frameInfos, recorded, &error);
            if (!outcome->context.has_value())
            {
                outcome->failure = QStringLiteral("Could not finalize registration persistence context: %1").arg(error);
                return;
            }
            // The database lies in the output directory, which a person may name before it exists.
            QDir().mkpath(outputDirectory);
            // Alignment and stacking read the registration from the database, so one that cannot be recorded is a
            // failed run rather than a completed one.
            if (!an::ui::persistStandaloneRegistrationState(*outcome->context, frameInfos, outcome->registered, &error))
            {
                LOG_ERROR("Image Registration: failed to persist registration_metrics.db: {}", error.toStdString());
                outcome->failure =
                    QStringLiteral("The registration could not be recorded in '%1': %2")
                        .arg(QDir(outputDirectory).filePath(QStringLiteral("registration_metrics.db")), error);
                return;
            }
            // What alignment and stacking will read is what the window shows, so a record that cannot be read back
            // fails the run as one that cannot be written does.
            const auto loaded = an::ui::loadStandaloneRegistrationState(*outcome->context, frameInfos, &error);
            if (!loaded.has_value() || !loaded->groupState.has_value())
            {
                LOG_ERROR("Image Registration: failed to read back registration_metrics.db: {}", error.toStdString());
                outcome->failure = QStringLiteral("The registration recorded in '%1' could not be read back: %2")
                                       .arg(QDir(outputDirectory).filePath(QStringLiteral("registration_metrics.db")),
                                            error.isEmpty() ? QStringLiteral("no registration was found") : error);
                return;
            }
            outcome->registered = loaded->registeredData;
            outcome->context = loaded->context;
            outcome->metrics = loaded->metricsByFrameId;
        }();

        const std::size_t failed =
            static_cast<std::size_t>(std::count_if(outcome->registered.frames.begin(), outcome->registered.frames.end(),
                                                   [](const auto& entry) { return entry.second.failed; }));
        LOG_INFO("Image Registration: {} frames, {} failed, in {:.2f} seconds{}", outcome->registered.frames.size(),
                 failed, static_cast<double>(timer.elapsed()) / 1000.0, outcome->stopped ? " (stopped)" : "");
        postToGui([guard, dialog, outcome, failed]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                return;
            }
            Impl& impl = *guard->m_impl;
            impl.running = false;
            impl.progress.clear();
            if (outcome->stopped || !outcome->failure.isEmpty())
            {
                // A stopped or failed run leaves nothing recorded in the window.
                impl.registered = {};
                impl.context.reset();
                impl.metrics.clear();
                impl.framesByKey.clear();
                guard->refreshTable();
                if (!outcome->stopped)
                {
                    process_utils::notifyErrorMessage(
                        guard, QStringLiteral("Error running image registration"),
                        QStringLiteral("An error occurred when running image registration process:\n%1")
                            .arg(outcome->failure));
                }
                return;
            }
            impl.registered = outcome->registered;
            impl.context = outcome->context;
            indexRegistration(impl.registered, impl.framesByKey, impl.metrics);
            for (const auto& [key, metric] : outcome->metrics)
            {
                impl.metrics.insert_or_assign(key, metric);
            }
            if (!outcome->referencePath.isEmpty())
            {
                const int row = impl.files->rowOf(outcome->referencePath);
                if (row >= 0)
                {
                    impl.referencePath = outcome->referencePath;
                    const QSignalBlocker blocker(impl.ui.referenceImageValue);
                    impl.ui.referenceImageValue->setValue(row + 1);
                }
            }
            guard->refreshTable();
            const std::size_t registered = impl.registered.frames.size() - failed;
            if (failed > 0u)
            {
                guard->setStatus(QStringLiteral("Registration completed. %1 images registered. %2 failed.")
                                     .arg(registered)
                                     .arg(failed),
                                 QStringLiteral("warning"));
            }
            else
            {
                guard->setStatus(QStringLiteral("Registration completed. %1 images registered.").arg(registered),
                                 QStringLiteral("success"));
            }
        });
    });
}

void ImageRegistrationProcess::alignFrames()
{
    const an::consumer::EngineSession* session = currentSession();
    if (!isIdle() || session == nullptr)
    {
        return;
    }
    const QString outputDirectory = m_impl->ui.outputDirectory->text().trimmed();
    if (outputDirectory.isEmpty())
    {
        process_utils::notifyInfoMessage(this, QStringLiteral("Image Alignment"),
                                         QStringLiteral("You need to define an output directory before executing "
                                                        "image alignment"));
        return;
    }
    if (!registrationIsCurrent())
    {
        process_utils::notifyInfoMessage(this, QStringLiteral("Image Alignment"),
                                         QStringLiteral("Registration needs to be updated before alignment can run."));
        return;
    }

    // The included, registered frames in the list's order, and the reference among them.
    const std::unordered_set<QString> ignored = ignoredFrameIds();
    struct AlignedFrame
    {
        QString path;
        an::RegistrationTransform transform;
        an::SipDistortion distortion;
    };
    std::vector<AlignedFrame> frames;
    std::vector<ImageListProcessHelpers::ImageListEntry> entries;
    std::uint32_t referenceIndex = 0u;
    for (const WorkflowFile& file : m_impl->files->files())
    {
        const RegisteredFrame* frame = m_impl->frameOf(file.path());
        if (ignored.contains(frameKey(file.path())) || frame == nullptr || frame->failed ||
            !frame->transform.has_value())
        {
            continue;
        }
        if (file.path() == m_impl->referencePath)
        {
            referenceIndex = static_cast<std::uint32_t>(frames.size());
        }
        frames.push_back(AlignedFrame{
            .path = file.path(), .transform = *frame->transform, .distortion = solvedDistortionOf(*frame, file)});
        entries.push_back(file.entry);
    }
    if (frames.size() < 2u)
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Error aligning images"),
                                          QStringLiteral("No included registered images are available for "
                                                         "alignment"));
        return;
    }
    if (!ImageListProcessHelpers::validateImageListSizes(entries) ||
        !ImageListProcessHelpers::validateImageListColorDepth(entries))
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Error aligning images"),
                                          QStringLiteral("Some images in the list have different size or color "
                                                         "depth"));
        return;
    }
    const RegistrationSettings settings = registrationSettings();
    const an::Result<an::KernelInterpolation> interpolation =
        an::ui::processes::alignmentInterpolationOf(settings.interpolation);
    if (!interpolation.hasValue())
    {
        process_utils::notifyErrorMessage(this, QStringLiteral("Error aligning images"),
                                          QString::fromStdString(interpolation.status().message()));
        return;
    }
    const an::RegistrationCanvasMode mode = an::ui::processes::registrationCanvasModeOf(settings.alignmentMode);
    const an::ui::processes::RegistrationDemosaic demosaic = an::ui::processes::registrationDemosaicOf(settings);
    const an::ImageExtent extent = entries.front().descriptor.extent();

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Image Alignment"));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->show();
    m_impl->progress = progress;
    m_impl->running = true;
    updateControls();

    QPointer<ImageRegistrationProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = windowStopToken();
    const std::stop_token cancel = progress->stopToken();
    const an::KernelInterpolation sampling = *interpolation;
    QThreadPool::globalInstance()->start([session, frames, referenceIndex, extent, mode, sampling, demosaic,
                                          outputDirectory, guard, dialog, windowStop, cancel]() {
        std::stop_source stop;
        const std::stop_callback onClose(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback onCancel(cancel, [&stop]() { stop.request_stop(); });
        QString failure;
        bool stopped = false;
        std::vector<an::RegistrationTransform> transforms;
        std::vector<an::SipDistortion> distortions;
        for (const AlignedFrame& frame : frames)
        {
            transforms.push_back(frame.transform);
            distortions.push_back(frame.distortion);
        }
        const an::Result<an::RegistrationCanvas> canvas = an::ui::processes::planAlignmentCanvas(
            transforms, referenceIndex, extent.width(), extent.height(), mode, distortions);
        std::size_t aligned = 0u;
        if (!canvas.hasValue())
        {
            failure = QString::fromStdString(canvas.status().message());
        }
        else
        {
            QDir().mkpath(outputDirectory);
            for (std::size_t index = 0u; index < frames.size(); ++index)
            {
                if (stop.stop_requested())
                {
                    stopped = true;
                    break;
                }
                postToGui([dialog, index, count = frames.size(), name = QFileInfo(frames[index].path).fileName()]() {
                    if (dialog)
                    {
                        dialog->setText(
                            QStringLiteral("Aligning image %1/%2: %3").arg(index + 1u).arg(count).arg(name));
                        dialog->setValue(static_cast<int>((index * 100u) / count));
                    }
                });
                const an::Status written = an::ui::processes::alignRegisteredFrame(
                    *session, frames[index].path, frames[index].transform, frames[index].distortion, *canvas, sampling,
                    demosaic, an::ui::processes::alignedOutputPath(frames[index].path, outputDirectory),
                    stop.get_token());
                if (written.failed())
                {
                    stopped = written.code() == an::ErrorCode::Cancelled;
                    if (!stopped)
                    {
                        failure = QStringLiteral("%1: %2").arg(QFileInfo(frames[index].path).fileName(),
                                                               QString::fromStdString(written.message()));
                    }
                    break;
                }
                ++aligned;
            }
        }
        LOG_INFO("Image Registration: aligned {} of {} frames into {}", aligned, frames.size(),
                 outputDirectory.toStdString());
        postToGui([guard, dialog, failure, stopped]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                return;
            }
            guard->m_impl->running = false;
            guard->m_impl->progress.clear();
            guard->updateControls();
            if (!failure.isEmpty())
            {
                process_utils::notifyErrorMessage(
                    guard, QStringLiteral("Error aligning images"),
                    QStringLiteral("An error occurred when aligning images: %1").arg(failure));
            }
            else if (!stopped)
            {
                guard->setStatus(QStringLiteral("Registration and alignment completed successfully"),
                                 QStringLiteral("success"));
            }
        });
    });
}

void ImageRegistrationProcess::restoreRecordedRegistration()
{
    // A recorded registration of these frames in the output directory is read on the thread pool and shown when it
    // answers the frames and settings as they still are.
    const QString outputDirectory = m_impl->ui.outputDirectory->text().trimmed();
    const std::vector<WorkflowFile> files = m_impl->files->files();
    if (outputDirectory.isEmpty() || files.empty() || m_impl->running)
    {
        return;
    }
    RegistrationSettings settings = registrationSettings();
    const std::uint64_t generation = ++m_impl->restoreGeneration;
    ++m_impl->workersInFlight;
    QPointer<ImageRegistrationProcess> guard(this);
    QThreadPool::globalInstance()->start([guard, generation, outputDirectory, files, settings]() mutable {
        const std::vector<FrameInfo> frames = frameInfosOf(files);
        QString error;
        std::optional<an::ui::StandaloneRegistrationPersistenceState> restored;
        auto context = an::ui::buildStandaloneRegistrationPersistenceContext(outputDirectory, frames, settings, &error);
        if (context.has_value())
        {
            auto loaded = an::ui::loadStandaloneRegistrationState(*context, frames, &error);
            if (loaded.has_value() && loaded->groupState.has_value())
            {
                bool matched = an::ui::standaloneRegistrationStateMatchesContext(*context, *loaded->groupState);
                // A reference chosen automatically is recorded, and the comparison is retried with it.
                if (!matched && context->referenceFrameId.isEmpty() && !loaded->groupState->referenceFrameId.isEmpty())
                {
                    settings.referenceImage = loaded->groupState->referenceFrameId;
                    const auto retried = an::ui::buildStandaloneRegistrationPersistenceContext(outputDirectory, frames,
                                                                                               settings, nullptr);
                    matched = retried.has_value() &&
                              an::ui::standaloneRegistrationStateMatchesContext(*retried, *loaded->groupState);
                }
                if (matched)
                {
                    restored = std::move(*loaded);
                }
            }
        }
        postToGui([guard, generation, restored = std::move(restored)]() {
            if (!guard)
            {
                return;
            }
            Impl& impl = *guard->m_impl;
            --impl.workersInFlight;
            if (impl.restoreGeneration != generation || impl.running)
            {
                guard->updateControls();
                return;
            }
            if (restored.has_value())
            {
                impl.registered = restored->registeredData;
                impl.context = restored->context;
                indexRegistration(impl.registered, impl.framesByKey, impl.metrics);
                for (const auto& [key, metric] : restored->metricsByFrameId)
                {
                    impl.metrics.insert_or_assign(key, metric);
                }
                if (!restored->context.referenceFrameId.trimmed().isEmpty() &&
                    impl.files->rowOf(restored->context.referenceFrameId) >= 0)
                {
                    impl.referencePath = restored->context.referenceFrameId;
                }
            }
            guard->refreshTable();
        });
    });
}

void ImageRegistrationProcess::on_addFilesButtonPushed()
{
    addItemsNonBlocking(
        process_utils::selectImageFiles(this, QStringLiteral("Select Files"), QString::fromLatin1(kFilesContext)));
}

void ImageRegistrationProcess::on_addFolderButtonPushed()
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

void ImageRegistrationProcess::on_outputDirectoryButtonPushed()
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
    if (directory.isEmpty())
    {
        return;
    }
    m_impl->ui.outputDirectory->setText(directory);
    process_utils::rememberPath(QString::fromLatin1(kOutputContext), directory);
    restoreRecordedRegistration();
    updateControls();
}

void ImageRegistrationProcess::on_registrationMethodChanged()
{
    auto& ui = m_impl->ui;
    const QString method = ImageIntegration::RegistrationSettingsUtils::normalizeRegistrationMethod(
        ui.registrationMethodSelector->currentData().toString());
    ui.transformationSelector->setEnabled(method == QStringLiteral("star_matching") ||
                                          method == QStringLiteral("comet_tracking"));
    const bool fixedReference = ImageIntegration::RegistrationSettingsUtils::requiresFixedReference(method);
    if (fixedReference && ui.referenceModeSelector->currentData().toString() == QStringLiteral("auto"))
    {
        ui.referenceModeSelector->setCurrentIndex(0);
    }
    ui.referenceModeSelector->setEnabled(!fixedReference);
    on_referenceModeChanged();
}

void ImageRegistrationProcess::on_referenceModeChanged()
{
    const bool automatic = m_impl->ui.referenceModeSelector->currentData().toString() == QStringLiteral("auto");
    m_impl->ui.referenceImageValue->setEnabled(!automatic);
    if (automatic)
    {
        applyAutoReferenceSelection();
    }
    refreshTable();
}

void ImageRegistrationProcess::applyAutoReferenceSelection()
{
    if (m_impl->ui.referenceModeSelector->currentData().toString() != QStringLiteral("auto"))
    {
        return;
    }
    QString error;
    const auto pick = an::ui::pickStandaloneAutoReference(helperFramesOf(m_impl->files->files()), m_impl->metrics,
                                                          ignoredFrameIds(), error);
    if (!pick.has_value())
    {
        return;
    }
    for (const WorkflowFile& file : m_impl->files->files())
    {
        if (frameKey(file.path()) == pick->id.canonicalPath())
        {
            setReferenceRow(m_impl->files->rowOf(file.path()));
            return;
        }
    }
}

void ImageRegistrationProcess::on_advancedSettingsButtonPushed()
{
    an::ui::processes::StandaloneRegistrationSettingsRequest request;
    request.settings = registrationSettings();
    request.frames = m_impl->files->paths();
    QPointer<ImageRegistrationProcess> guard(this);
    // Auto-Assign reads the figures this window holds and detects the frames it holds none for, on the thread pool.
    request.autoAssign = [guard](const RegistrationSettings& shown,
                                 std::function<void(QString reference, QString reason)> chosen) {
        const an::consumer::EngineSession* session = currentSession();
        if (!guard || session == nullptr)
        {
            chosen({}, QStringLiteral("The engine is not available."));
            return;
        }
        const std::vector<WorkflowFile> files = guard->m_impl->files->files();
        std::unordered_map<QString, RegistrationMetricRecord> metrics = guard->m_impl->metrics;
        const std::unordered_set<QString> ignored = guard->ignoredFrameIds();
        const an::ui::StandaloneRegistrationQualityThresholds thresholds = guard->m_impl->thresholds;
        const std::stop_token stop = guard->windowStopToken();
        ++guard->m_impl->workersInFlight;
        QThreadPool::globalInstance()->start([session, shown, files, metrics, ignored, thresholds, stop, guard,
                                              chosen]() mutable {
            const an::StarDetectionRequest detection = an::ui::processes::registrationStarDetection(shown);
            const an::ui::processes::RegistrationDemosaic demosaic = an::ui::processes::registrationDemosaicOf(shown);
            for (const WorkflowFile& file : files)
            {
                const QString key = frameKey(file.path());
                if (const auto it = metrics.find(key); it != metrics.end() && it->second.metrics.succeeded)
                {
                    continue;
                }
                an::Result<an::Image<float>> image =
                    an::ui::processes::readRegistrationFrame(*session, file.path(), demosaic, stop);
                if (!image.hasValue())
                {
                    continue;
                }
                an::Result<an::consumer::OperationScope> scope = session->admit(an::consumer::OperationRequest{
                    .purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
                if (!scope.hasValue())
                {
                    break;
                }
                const an::Result<an::ui::processes::FrameDetection> found =
                    an::ui::processes::detectRegistrationStars(scope->execution(), *image, detection);
                if (found.hasValue())
                {
                    metrics.insert_or_assign(key, metricOf(key, found->frame));
                }
            }
            std::unordered_set<QString> excluded = ignored;
            const auto overThreshold =
                an::ui::computeStandaloneThresholdExcludedFrameIds(metrics, helperFramesOf(files), thresholds);
            excluded.insert(overThreshold.begin(), overThreshold.end());
            QString error;
            const auto pick = an::ui::pickStandaloneAutoReference(helperFramesOf(files), metrics, excluded, error);
            QString reference;
            QString reason = error;
            if (pick.has_value())
            {
                reference = pick->id.canonicalPath();
                for (const WorkflowFile& file : files)
                {
                    if (frameKey(file.path()) == reference)
                    {
                        reference = file.path();
                    }
                }
                LOG_INFO("Image Registration: Auto-Assign selected '{}'", reference.toStdString());
            }
            else if (reason.isEmpty())
            {
                reason = QStringLiteral("Could not determine best image from available data.");
            }
            postToGui([guard, chosen, reference, reason]() {
                if (guard)
                {
                    --guard->m_impl->workersInFlight;
                }
                chosen(reference, reason);
            });
        });
    };
    an::ui::processes::openStandaloneRegistrationSettings(this, std::move(request),
                                                          [guard](const RegistrationSettings& accepted) {
                                                              if (guard)
                                                              {
                                                                  guard->applyRegistrationSettings(accepted);
                                                                  guard->refreshTable();
                                                              }
                                                          });
}

void ImageRegistrationProcess::on_blinkCompareButtonPushed()
{
    const std::vector<QString> selected = process_utils::selectedFilesFromTable(m_impl->ui.tableWidget);
    if (selected.size() < 2u)
    {
        return;
    }
    QStringList paths;
    for (const QString& path : selected)
    {
        paths << path;
    }
    QPointer<ImageRegistrationProcess> guard(this);
    an::ui::processes::openStandaloneBlinkComparator(this, paths, [guard](const QStringList& excluded) {
        if (guard)
        {
            guard->setFramesExcluded(std::vector<QString>(excluded.begin(), excluded.end()), true);
        }
    });
}

void ImageRegistrationProcess::on_applyThresholdsButtonPushed()
{
    m_impl->thresholds.minStars = m_impl->ui.qualityMinStarsValue->value();
    m_impl->thresholds.maxFwhm = m_impl->ui.qualityMaxFwhmValue->value();
    m_impl->thresholds.maxEccentricity = m_impl->ui.qualityMaxEccentricityValue->value();
    refreshTable();
}

void ImageRegistrationProcess::on_resetThresholdsButtonPushed()
{
    m_impl->ui.qualityMinStarsValue->setValue(0);
    m_impl->ui.qualityMaxFwhmValue->setValue(0.0);
    m_impl->ui.qualityMaxEccentricityValue->setValue(0.0);
    m_impl->thresholds = {};
    m_impl->thresholdExcluded.clear();
    refreshTable();
}

void ImageRegistrationProcess::on_clearRegisteredDataButtonPushed()
{
    QString error;
    auto context = m_impl->context;
    if (!context.has_value())
    {
        context = an::ui::buildStandaloneRegistrationPersistenceContext(m_impl->ui.outputDirectory->text().trimmed(),
                                                                        frameInfosOf(m_impl->files->files()),
                                                                        registrationSettings(), &error);
    }
    if (context.has_value() && !an::ui::clearStandaloneRegistrationState(*context, &error) && !error.isEmpty())
    {
        LOG_WARNING("Image Registration: failed to clear the recorded registration: {}", error.toStdString());
    }
    m_impl->registered = {};
    m_impl->context.reset();
    m_impl->metrics.clear();
    m_impl->framesByKey.clear();
    refreshTable();
}

void ImageRegistrationProcess::on_removeFailedImagesButtonPushed()
{
    std::vector<QString> failed;
    for (const WorkflowFile& file : m_impl->files->files())
    {
        const RegisteredFrame* frame = m_impl->frameOf(file.path());
        if (frame != nullptr && frame->failed)
        {
            failed.push_back(file.path());
        }
    }
    if (failed.empty())
    {
        return;
    }
    removeItems(failed);
    process_utils::notifyInfoMessage(
        this, QStringLiteral("Remove non registered images"),
        QStringLiteral("%1 failed images have been removed from the table (not from the disk)").arg(failed.size()));
}

void ImageRegistrationProcess::on_nextFailedButtonPushed()
{
    const auto& files = m_impl->files->files();
    const int count = static_cast<int>(files.size());
    const int start = (std::max)(m_impl->files->selectedRow(), 0);
    for (int step = 1; step <= count; ++step)
    {
        const int row = (start + step) % count;
        const RegisteredFrame* frame = m_impl->frameOf(files[static_cast<std::size_t>(row)].path());
        if (frame != nullptr && frame->failed)
        {
            m_impl->files->selectRow(row);
            return;
        }
    }
}

void ImageRegistrationProcess::extendContextMenu(QMenu& menu, const int row)
{
    const std::vector<QString> selected = process_utils::selectedFilesFromTable(m_impl->ui.tableWidget);
    QAction* reference = menu.addAction(QStringLiteral("Set as reference"));
    reference->setEnabled(row != m_impl->rowOfReference());
    QObject::connect(reference, &QAction::triggered, this, [this, row]() { setReferenceRow(row); });
    QAction* exclude = menu.addAction(QStringLiteral("Exclude selected frames"));
    exclude->setEnabled(!selected.empty());
    QObject::connect(exclude, &QAction::triggered, this, [this, selected]() { setFramesExcluded(selected, true); });
    QAction* include = menu.addAction(QStringLiteral("Include selected frames"));
    include->setEnabled(std::any_of(selected.begin(), selected.end(), [this](const QString& path) {
        const QString key = frameKey(path);
        return m_impl->manualExcluded.contains(key) || m_impl->thresholdExcluded.contains(key);
    }));
    QObject::connect(include, &QAction::triggered, this, [this, selected]() { setFramesExcluded(selected, false); });
    QAction* solve = menu.addAction(QStringLiteral("Solve..."));
    QObject::connect(solve, &QAction::triggered, this, [this, row]() { openPlateSolveForRow(row); });
    QAction* blink = menu.addAction(QStringLiteral("Blink Compare..."));
    blink->setEnabled(selected.size() >= 2u);
    QObject::connect(blink, &QAction::triggered, this, [this]() { on_blinkCompareButtonPushed(); });
    menu.addSeparator();
}

void ImageRegistrationProcess::openPlateSolveForRow(const int row)
{
    const auto& files = m_impl->files->files();
    if (row < 0 || row >= static_cast<int>(files.size()) || m_impl->preview == nullptr)
    {
        return;
    }
    const QString path = files[static_cast<std::size_t>(row)].path();
    m_impl->files->selectRow(row);
    m_impl->preview->loadImageFromFile(path.toUtf8().toStdString());
    QWidget* tool = an::ui::openViewTool(an::ui::ViewTool::PlateSolve, *m_impl->preview);
    if (tool == nullptr)
    {
        return;
    }
    // The solution the window applies to the preview is the frame's for this registration: read when it closes.
    QPointer<ImageRegistrationProcess> guard(this);
    QPointer<ImageViewControl> preview(m_impl->preview);
    QObject::connect(tool, &QObject::destroyed, this, [guard, preview, path]() {
        if (!guard || !preview)
        {
            return;
        }
        const an::Result<an::consumer::AssetReadLease> lease = preview->document().tryRead();
        if (!lease.hasValue())
        {
            return;
        }
        const an::ui::ImageDocument& document = preview->document();
        const std::optional<an::ImageDescriptor> descriptor = document.descriptor();
        if (!descriptor.has_value() || document.names().sourcePath != path)
        {
            return;
        }
        const an::ImageMetadata& metadata =
            std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, lease->image());
        const an::Result<an::WcsSolution> solution = an::ui::readImageWcsSolution(
            metadata.codec().cards(), descriptor->extent().width(), descriptor->extent().height());
        if (solution.hasValue())
        {
            guard->m_impl->solvedByHand.insert_or_assign(frameKey(path), *solution);
        }
    });
}

void ImageRegistrationProcess::listChanged()
{
    const auto& files = m_impl->files->files();
    if (files.empty())
    {
        m_impl->referencePath.clear();
        if (m_impl->preview != nullptr)
        {
            m_impl->preview->clear();
        }
        // An emptied list forgets what the window held of its frames - their exclusions and registration - and a
        // recorded registration being read lands as nothing. One recorded in the output directory is read back when
        // frames are listed again.
        ++m_impl->restoreGeneration;
        m_impl->registered = {};
        m_impl->context.reset();
        m_impl->metrics.clear();
        m_impl->framesByKey.clear();
        m_impl->manualExcluded.clear();
        m_impl->thresholdExcluded.clear();
    }
    else if (m_impl->rowOfReference() < 0)
    {
        m_impl->referencePath = files.front().path();
    }
    auto* spin = m_impl->ui.referenceImageValue;
    const QSignalBlocker blocker(spin);
    spin->setMinimum(files.empty() ? 0 : 1);
    spin->setMaximum(static_cast<int>(files.size()));
    spin->setValue(m_impl->rowOfReference() + 1);
    refreshTable();
}

void ImageRegistrationProcess::rowSelected(const int row)
{
    const WorkflowFile* file = m_impl->files->selectedFile();
    if (row >= 0 && file != nullptr && m_impl->preview != nullptr)
    {
        // The view reads, demosaics and presents the frame on its own workers.
        m_impl->preview->loadImageFromFile(file->path().toUtf8().toStdString());
    }
    updateControls();
}

void ImageRegistrationProcess::refreshTable()
{
    m_impl->thresholdExcluded = an::ui::computeStandaloneThresholdExcludedFrameIds(
        m_impl->metrics, helperFramesOf(m_impl->files->files()), m_impl->thresholds);
    m_impl->files->refreshRows();
    TableWidget* table = m_impl->ui.tableWidget;
    const int referenceRow = m_impl->rowOfReference();
    const auto& files = m_impl->files->files();
    for (int row = 0; row < static_cast<int>(files.size()); ++row)
    {
        const QString& path = files[static_cast<std::size_t>(row)].path();
        const an::ui::StandaloneRegistrationExclusion exclusion =
            an::ui::evaluateStandaloneFrameExclusion(frameKey(path), m_impl->manualExcluded, m_impl->thresholdExcluded);
        const RegistrationMetricRecord* metric = m_impl->metricOfPath(path);
        if (exclusion.excluded)
        {
            table->highlightRow(row, an::ui::semanticColor(an::ui::SemanticColorRole::StatusWarning));
            table->setRowMarked(row, true, kExcludedMarker, exclusion.detail);
        }
        else if (metric != nullptr && !metric->metrics.succeeded)
        {
            table->highlightRow(row, an::ui::semanticColor(an::ui::SemanticColorRole::StatusError));
            table->setRowMarked(row, true, kFailedMarker, QStringLiteral("Image registration failed"));
        }
        else
        {
            table->clearHighlightRow(row);
            if (row == referenceRow)
            {
                table->setRowMarked(row, true, kReferenceMarker, QStringLiteral("This is the reference image"));
            }
            else
            {
                table->setRowMarked(row, false);
            }
        }
    }
    m_impl->ui.referenceImageLabel->setText(QString::number(referenceRow + 1));
    updateControls();
}

void ImageRegistrationProcess::setStatus(const QString& text, const QString& tone)
{
    QLabel* label = m_impl->ui.showInfoLabel;
    m_impl->statusText = text;
    if (label->property("anStatusTone").toString() != tone)
    {
        label->setProperty("anStatusTone", tone);
        if (QStyle* style = label->style())
        {
            style->unpolish(label);
            style->polish(label);
        }
    }
    label->setText(text);
}

void ImageRegistrationProcess::updateControls()
{
    auto& ui = m_impl->ui;
    const auto& files = m_impl->files->files();
    const int rows = static_cast<int>(files.size());
    const bool editable = !m_impl->running;
    ui.addFiles->setEnabled(editable);
    ui.addFolder->setEnabled(editable);
    if (rows == 0)
    {
        ui.registrationGroupBox->setEnabled(false);
        ui.registrationGroupBox->setToolTip(QStringLiteral("Add at least two frames to enable registration"));
        ui.alignmentGroupBox->setEnabled(false);
        ui.alignmentGroupBox->setToolTip(
            QStringLiteral("Add at least two frames and register them to enable alignment"));
        ui.advancedCard->setEnabled(false);
        ui.advancedCard->setToolTip(QStringLiteral("Add frames to enable quality thresholds"));
        ui.advancedSettingsButton->setEnabled(false);
        ui.removeFailedImagesButton->setEnabled(false);
        ui.nextFailedButton->setEnabled(false);
        ui.blinkCompareButton->setEnabled(false);
        ui.clearRegisteredDataButton->setEnabled(false);
        ui.registeredImagesLabel->setText(QStringLiteral("0/0"));
        ui.referenceImageLabel->setText(QStringLiteral("0"));
        setStatus(QString(), QString());
        return;
    }
    int registered = 0;
    int failed = 0;
    int excluded = 0;
    int included = 0;
    const std::unordered_set<QString> ignored = ignoredFrameIds();
    for (const WorkflowFile& file : files)
    {
        const QString key = frameKey(file.path());
        const bool isIgnored = ignored.contains(key);
        excluded += isIgnored ? 1 : 0;
        const RegisteredFrame* frame = m_impl->frameOf(file.path());
        if (frame == nullptr)
        {
            continue;
        }
        (frame->failed ? failed : registered) += 1;
        included += !frame->failed && !isIgnored ? 1 : 0;
    }
    ui.registrationGroupBox->setEnabled(editable && rows > 1);
    ui.registrationGroupBox->setToolTip(rows > 1 ? QString()
                                                 : QStringLiteral("Add at least two frames to enable registration"));
    ui.advancedSettingsButton->setEnabled(editable);
    ui.removeFailedImagesButton->setEnabled(editable && failed > 0);
    ui.nextFailedButton->setEnabled(failed > 0);
    ui.blinkCompareButton->setEnabled(process_utils::selectedFilesFromTable(ui.tableWidget).size() >= 2u);
    ui.clearRegisteredDataButton->setEnabled(editable && !m_impl->registered.frames.empty());
    QString summary = QStringLiteral("%1/%2").arg(registered).arg(rows);
    if (failed > 0)
    {
        summary += QStringLiteral(" (%1 failed)").arg(failed);
    }
    if (excluded > 0)
    {
        summary += QStringLiteral(" (%1 excluded)").arg(excluded);
    }
    ui.registeredImagesLabel->setText(summary);

    const bool current = registrationIsCurrent() && registered + failed == rows;
    if (!current)
    {
        setStatus(QStringLiteral("Registration needs update"), QStringLiteral("error"));
        ui.alignmentGroupBox->setEnabled(false);
        ui.alignmentGroupBox->setToolTip(QStringLiteral("Registration needs to be run or updated before alignment"));
        ui.advancedCard->setEnabled(false);
        ui.advancedCard->setToolTip(
            QStringLiteral("Registration must be completed for all frames before applying quality thresholds"));
    }
    else if (included < 2)
    {
        setStatus(QStringLiteral("Select at least two included, successfully registered frames to align"),
                  QStringLiteral("warning"));
        ui.alignmentGroupBox->setEnabled(false);
        ui.alignmentGroupBox->setToolTip(
            QStringLiteral("At least two included, successfully registered frames are required"));
        ui.advancedCard->setEnabled(editable);
        ui.advancedCard->setToolTip(QString());
    }
    else
    {
        if (m_impl->statusText.isEmpty() || m_impl->statusText == QStringLiteral("Registration needs update") ||
            m_impl->statusText.startsWith(QStringLiteral("Select at least")))
        {
            setStatus(QStringLiteral("Images ready to align"), QStringLiteral("success"));
        }
        ui.alignmentGroupBox->setEnabled(editable);
        ui.alignmentGroupBox->setToolTip(QString());
        ui.advancedCard->setEnabled(editable);
        ui.advancedCard->setToolTip(QString());
    }
    ui.alignButton->setEnabled(editable && current && included >= 2 && !ui.outputDirectory->text().trimmed().isEmpty());
    if (QPushButton* reset = ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        reset->setEnabled(editable);
    }
}

void ImageRegistrationProcess::updateAutoSolveAndEnrich()
{
    const an::ui::PlateSolveSettings settings = readPlateSolveSettings();
    const bool solverConfigured = an::ui::hasAnyConfiguredPlateSolveSolver(settings);
    const bool gaiaReady = an::ui::isGaiaCatalogReady(settings);
    auto& ui = m_impl->ui;
    // Each box shows what a run does: off without what it needs, and the stored choice otherwise, which showing it off
    // does not change.
    ui.autoSolveUnsolvedFramesCheck->setEnabled(solverConfigured);
    {
        const QSignalBlocker blocker(ui.autoSolveUnsolvedFramesCheck);
        ui.autoSolveUnsolvedFramesCheck->setChecked(solverConfigured && m_impl->advanced.autoSolveUnsolvedFrames);
    }
    if (solverConfigured)
    {
        ui.autoSolveUnsolvedFramesCheck->setToolTip(
            QStringLiteral("Automatically plate-solve frames that lack WCS astrometric metadata before registration"));
    }
    else
    {
        ui.autoSolveUnsolvedFramesCheck->setToolTip(
            QStringLiteral("No plate solver is configured.\nConfigure ASTAP, local/online astrometry.net, or the "
                           "internal Gaia solver\nin Settings > Plate Solving / External Tools.\nThe choice stored "
                           "for this window is kept for when one is."));
    }
    ui.enrichWithGaiaCatalogCheck->setEnabled(gaiaReady);
    {
        const QSignalBlocker blocker(ui.enrichWithGaiaCatalogCheck);
        ui.enrichWithGaiaCatalogCheck->setChecked(gaiaReady && m_impl->advanced.enrichWithGaiaCatalog);
    }
    if (gaiaReady)
    {
        ui.enrichWithGaiaCatalogCheck->setToolTip(
            QStringLiteral("After registration, match detected stars against the Gaia catalog to compute\n"
                           "photometric zero point, limiting magnitude, and catalog-matched star count.\n"
                           "Requires plate-solved frames (WCS metadata) and a configured Gaia catalog."));
    }
    else
    {
        ui.enrichWithGaiaCatalogCheck->setToolTip(
            QStringLiteral("No Gaia catalog is configured or the catalog directory is not ready.\n"
                           "Configure the Gaia catalog directory in Settings > Plate Solving.\nThe choice stored for "
                           "this window is kept for when one is."));
    }
}

void ImageRegistrationProcess::resetSettings()
{
    auto& ui = m_impl->ui;
    m_impl->advanced = RegistrationSettings{};
    ui.minStarParirsValue->setValue(m_impl->advanced.minStarPairs);
    ui.maxStarParirsValue->setValue(m_impl->advanced.maxStarPairs);
    ui.transformationSelector->setCurrentText(QStringLiteral("Homography"));
    ui.registrationMethodSelector->setCurrentIndex(0);
    ui.referenceModeSelector->setCurrentIndex(0);
    ui.reprocessAlreadyRegisteredmages->setChecked(true);
    ui.autoSolveUnsolvedFramesCheck->setChecked(false);
    ui.enrichWithGaiaCatalogCheck->setChecked(true);
    ui.chromaticAberrationCorrectionCheck->setChecked(false);
    ui.alignModeStandard->setChecked(true);
    ui.interpolationSelector->setCurrentText(process_utils::INTERP_AUTO);
    ui.qualityMinStarsValue->setValue(0);
    ui.qualityMaxFwhmValue->setValue(0.0);
    ui.qualityMaxEccentricityValue->setValue(0.0);
    m_impl->thresholds = {};
    m_impl->thresholdExcluded.clear();
    updateAutoSolveAndEnrich();
    on_registrationMethodChanged();
}

void ImageRegistrationProcess::closeEvent(QCloseEvent* evt)
{
    // Everything the window started stops with the window's token, requested as the base closes, and lands on a window
    // that is gone; nothing is waited for.
    if (m_impl->progress)
    {
        m_impl->progress->requestCancel();
    }
    TopLevelProcessBaseControl::closeEvent(evt);
}

std::string ImageRegistrationProcess::serialize_json_settings()
{
    const auto& ui = m_impl->ui;
    nlohmann::json document;
    document["version"] = kSettingsVersion;
    document["frames"] = ImageListProcessHelpers::imageFilePathsToJson(m_impl->files->entries());
    auto& params = document["params"];
    params["outputDirectory"] = ui.outputDirectory->text().toStdString();
    params["qualityMinStars"] = ui.qualityMinStarsValue->value();
    params["qualityMaxFwhm"] = ui.qualityMaxFwhmValue->value();
    params["qualityMaxEccentricity"] = ui.qualityMaxEccentricityValue->value();
    params["reprocessAlreadyRegistered"] = ui.reprocessAlreadyRegisteredmages->isChecked();
    const QJsonObject settings =
        ImageIntegration::RegistrationHashUtils::registrationSettingsToJson(registrationSettings());
    params["registrationSettings"] =
        nlohmann::json::parse(QJsonDocument(settings).toJson(QJsonDocument::Compact).toStdString());
    return document.dump();
}

void ImageRegistrationProcess::deserialize_json_settings(const std::string& json_settings)
{
    if (json_settings.empty())
    {
        return;
    }
    const nlohmann::json document = nlohmann::json::parse(json_settings, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        LOG_ERROR("Image Registration: could not read its settings: {}", json_settings);
        return;
    }
    auto& ui = m_impl->ui;
    const int version = document.value("version", 1);
    const nlohmann::json params =
        document.contains("params") && document["params"].is_object() ? document["params"] : nlohmann::json::object();
    try
    {
        if (version >= 2)
        {
            const QString directory = QString::fromStdString(params.value("outputDirectory", std::string()));
            ui.outputDirectory->setText(directory);
            if (!directory.isEmpty())
            {
                process_utils::rememberPath(QString::fromLatin1(kOutputContext), directory);
            }
        }
        if (version >= 4 && params.contains("registrationSettings") && params["registrationSettings"].is_object())
        {
            const QJsonDocument stored =
                QJsonDocument::fromJson(QByteArray::fromStdString(params["registrationSettings"].dump()));
            if (stored.isObject())
            {
                applyRegistrationSettings(
                    ImageIntegration::RegistrationHashUtils::registrationSettingsFromJson(stored.object()));
            }
        }
        else if (version >= 3)
        {
            RegistrationSettings settings = registrationSettings();
            settings.registrationMethod = QString::fromStdString(params.value("registrationMethod", std::string()));
            settings.autoReferenceDuringRegistration =
                QString::fromStdString(params.value("referenceMode", std::string())) == QStringLiteral("auto");
            settings.chromaticAberration.enabled = params.value("chromaticAberrationCorrection", false);
            settings.enrichWithGaiaCatalog = params.value("enrichWithGaiaCatalog", true);
            applyRegistrationSettings(settings);
        }
        if (version >= 3)
        {
            ui.qualityMinStarsValue->setValue(params.value("qualityMinStars", 0));
            ui.qualityMaxFwhmValue->setValue(params.value("qualityMaxFwhm", 0.0));
            ui.qualityMaxEccentricityValue->setValue(params.value("qualityMaxEccentricity", 0.0));
            m_impl->thresholds.minStars = ui.qualityMinStarsValue->value();
            m_impl->thresholds.maxFwhm = ui.qualityMaxFwhmValue->value();
            m_impl->thresholds.maxEccentricity = ui.qualityMaxEccentricityValue->value();
        }
        if (version >= 4)
        {
            ui.reprocessAlreadyRegisteredmages->setChecked(params.value("reprocessAlreadyRegistered", true));
            const QStringList frames =
                ImageListProcessHelpers::imageFilePathsFromJson(document.value("frames", nlohmann::json::array()));
            if (!frames.isEmpty())
            {
                m_impl->files->restoreFiles(frames);
            }
        }
    }
    catch (const nlohmann::json::exception& error)
    {
        LOG_ERROR("Image Registration: could not read its settings: {}", error.what());
    }
    updateAutoSolveAndEnrich();
    updateControls();
}

void ImageRegistrationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
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
