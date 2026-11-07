// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Image List Analyzer window: the shared file list, a preview that plays the list as an image-list sequence, and
/// the stretched eight-bit proxies that let it play fast in the autostretch display they hold.

#include "stdafx.hpp"
#include "ImageListAnalyzerProcess.h"

#include "app/AppContextHelpers.h"
#include "app/FileCompanions.h"
#include "app/UiStyleTokens.h"
#include "common.h"
#include "controls/AutostretchApply.h"
#include "controls/ExportSequenceControl.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "controls/SequencePlayerControl.h"
#include "processes/ImageListProcessHelpers.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/WorkflowToolFiles.h"
#include "session/IEngineSessionProvider.h"
#include "theme/AnDesignTokens.h"
#include "widgets/HistogramCompanions.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/ImageListManifest.h>
#include <AfterNightEngine/IO/SequenceOutput.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <AfterNightPlatform/Logging.h>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUuid>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <stop_token>
#include <utility>
#include <vector>

using an::ui::processes::WorkflowFile;
using an::ui::processes::WorkflowFileColumn;
using an::ui::processes::WorkflowFileList;

namespace
{

constexpr const char* kFolderContext = "__IMAGE_ANALYZER_ADD_FOLDER__";
constexpr double kDefaultFramesPerSecond = 1.0;

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

/// The view's automatic stretch as Apply Stretch takes it, for a frame of a sequence.
[[nodiscard]] an::ui::AutostretchApplySettings stretchOf(const AutostretchParams& params)
{
    an::ui::AutostretchApplySettings settings;
    settings.targetBackground = params.targetBackground;
    settings.shadowsClipping = params.shadowsClipping;
    settings.linkedChannels = params.linkedChannels;
    settings.neutralizeBackground = params.neutralizeBackground;
    settings.colorCalibration = params.colorCalibration;
    settings.saturation = params.saturation;
    settings.saturationBackground = params.saturationBackground;
    settings.brightness = params.brightness;
    settings.contrast = params.contrast;
    settings.sequenceFrame = true;
    return settings;
}

/// What an image-list manifest states of its frames: the first frame's size, channels and arrangement.
struct ListFrame
{
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
    std::uint64_t channels = 0u;
    bool mosaiced = false;
};

/// The image-list manifest of a list of frames: the first frame's description, the rate and the paths.
[[nodiscard]] an::ImageListManifest manifestOf(const std::vector<QString>& paths, const ListFrame& first,
                                               const double framesPerSecond)
{
    an::ImageListManifest manifest;
    manifest.timestampSeconds = static_cast<std::uint64_t>((std::max)(qint64{0}, QDateTime::currentSecsSinceEpoch()));
    manifest.width = first.width;
    manifest.height = first.height;
    manifest.channels = first.channels;
    manifest.mosaiced = first.mosaiced;
    manifest.framesPerSecond = framesPerSecond;
    manifest.framePaths.reserve(paths.size());
    for (const QString& path : paths)
    {
        manifest.framePaths.push_back(QFileInfo(path).absoluteFilePath().toUtf8().toStdString());
    }
    return manifest;
}

/// Writes an image-list manifest and opens it as a sequence document, on the calling worker.
[[nodiscard]] an::Result<an::ui::ImageDocument> writeAndOpenList(const an::consumer::EngineSession& session,
                                                                 const QString& manifestPath,
                                                                 const an::ImageListManifest& manifest,
                                                                 const std::stop_token& stop)
{
    {
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        const an::Result<an::SequenceOutputReport> written = an::writeImageListManifest(
            scope->engine(), scope->execution(),
            an::SequenceOutputRequest{.destinationPath = manifestPath.toUtf8().toStdString()}, manifest);
        if (!written.hasValue())
        {
            return written.status();
        }
    }
    return an::ui::openSequenceDocument(session, manifestPath, an::SequenceFormat::ImageList, stop);
}

/// Writes one file's proxy: the file read as the view reads it, stretched by the automatic stretch into fractions of
/// full scale and written as eight-bit FITS.
[[nodiscard]] an::Status writeProxy(const an::consumer::EngineSession& session, const QString& source,
                                    const QString& destination, const an::DemosaicMethod demosaic,
                                    const an::ui::AutostretchApplySettings& stretch, const std::stop_token& stop)
{
    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, an::ui::ViewReadRequest{.path = source, .demosaic = demosaic}, stop, nullptr);
    if (!read.hasValue())
    {
        return read.status();
    }
    an::Result<an::consumer::ImageAsset> stretched = [&]() -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::consumer::OperationScope> scope = session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop});
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::consumer::AssetReadLease> lease = read->asset.read(stop);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<an::consumer::AnyImage> frame =
            an::ui::autostretchFrame(scope->engine(), scope->execution(), lease->image(), lease->validity(), stretch);
        if (!frame.hasValue())
        {
            return frame.status();
        }
        return an::consumer::ImageAsset::adopt(session, std::move(*frame));
    }();
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::UInt8});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    return an::ui::writeViewImage(session, *stretched, destination, *intent, stop);
}

/// Forwards the preview view's frame changes to the window.
class PreviewListener final : public ImageViewControlListener
{
public:
    PreviewListener(ImageViewControl* view, std::function<void()> shown)
        : ImageViewControlListener(view), m_shown(std::move(shown))
    {
    }

    void onImageLoaded(ImageViewControl* ctrl) override
    {
        Q_UNUSED(ctrl);
        m_shown();
    }

    void onImageUpdated(ImageViewControl* ctrl) override
    {
        Q_UNUSED(ctrl);
        m_shown();
    }

private:
    std::function<void()> m_shown;
};

/// Tells the window when the preview's display changes: its mode, or the settings of its automatic stretch. It draws
/// nothing.
class PreviewDisplayWatcher final : public ImageViewWidgetDisplayModule
{
public:
    explicit PreviewDisplayWatcher(std::function<void()> changed) : m_changed(std::move(changed)) {}

    void onDraw(QPainter& painter) override
    {
        Q_UNUSED(painter);
    }

    void onDisplayModeChanged(const ImageViewMode mode) override
    {
        Q_UNUSED(mode);
        m_changed();
    }

    void onAutostretchContextChanged() override
    {
        m_changed();
    }

private:
    std::function<void()> m_changed;
};

/// A file's proxy: where it was written, and the automatic stretch settings it holds.
struct ProxyFile
{
    QString path;
    AutostretchParams stretch;
};

} // namespace

struct ImageListAnalyzerProcess::Impl
{
    an::ui::processes::ImageListAnalyzerProcessUiElements ui;
    ImageViewControl* preview = nullptr;
    std::unique_ptr<PreviewListener> previewListener;
    std::unique_ptr<PreviewDisplayWatcher> displayWatcher;
    std::unique_ptr<WorkflowFileList> files;
    std::unique_ptr<an::ui::HistogramCountScheduler> histogram;
    std::unique_ptr<QTemporaryDir> temporary;

    /// Each listed file's proxy, by the file's path.
    std::unordered_map<QString, ProxyFile> proxies;

    /// The stretch settings proxies were written through, in the order first used: the proxies of each are written in
    /// a directory of its own, so a proxy of new settings never takes the name of the one it replaces.
    std::vector<AutostretchParams> proxyStretches;

    /// The preview sequence asked for last; a sequence that lands under an older one is dropped.
    std::uint64_t previewGeneration = 0u;
    std::uint64_t previewsInFlight = 0u;
    QString previewManifest;
    /// Whether the preview sequence asked for last plays the proxies.
    bool previewAskedForProxies = false;
    /// Whether the preview shows proxies, and the stretch settings they hold.
    bool previewShowsProxies = false;
    AutostretchParams previewProxyStretch;
    /// Whether a change of the preview's display waits for the event loop to be followed.
    bool displayChangeQueued = false;

    /// The proxies being written, or none.
    bool optimizing = false;
    QPointer<ProgressDialogControl> progress;

    double framesPerSecond = kDefaultFramesPerSecond;
    bool ignoreFrameChange = false;

    /// The directory of the proxies holding a stretch, made when first asked for; empty when it cannot be made.
    [[nodiscard]] QString proxyDirectory(const AutostretchParams& stretch)
    {
        const auto known = std::find(proxyStretches.begin(), proxyStretches.end(), stretch);
        const auto index = static_cast<std::size_t>(std::distance(proxyStretches.begin(), known));
        if (known == proxyStretches.end())
        {
            proxyStretches.push_back(stretch);
        }
        const QString directory = QDir(temporary->path()).filePath(QStringLiteral("stretch_%1").arg(index));
        return QDir().mkpath(directory) ? directory : QString();
    }
};

ImageListAnalyzerProcess::ImageListAnalyzerProcess(QWidget* parent)
    : TopLevelProcessBaseControl(getProcessNameStatic()), m_impl(std::make_unique<Impl>())
{
    Q_UNUSED(parent);
    m_impl->ui = an::ui::processes::buildImageListAnalyzerProcessUi(this);
    setProcessDescription(
        QStringLiteral("Analyze multi-image lists with statistics, histogram inspection, and sequence export tools."));
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto& ui = m_impl->ui;
    QObject::connect(ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ImageListAnalyzerProcess::on_dialog_box_button_clicked);
    QObject::connect(ui.addFiles, &QPushButton::released, this, &ImageListAnalyzerProcess::on_addFilesButtonPushed);
    QObject::connect(ui.addFolder, &QPushButton::released, this, &ImageListAnalyzerProcess::on_addFolderButtonPushed);
    QObject::connect(ui.removeSelected, &QPushButton::released, this,
                     [this]() { removeItems(process_utils::selectedFilesFromTable(m_impl->ui.tableWidget)); });
    QObject::connect(ui.clearAll, &QPushButton::released, this, [this]() { m_impl->files->clear(); });
    QObject::connect(ui.exportSequenceButton, &QPushButton::released, this, &ImageListAnalyzerProcess::exportSequence);
    QObject::connect(ui.optimizeButton, &QPushButton::released, this, &ImageListAnalyzerProcess::optimize);

    m_impl->preview = process_utils::createStaticPreviewInPlaceholder(ui.imagePreviewWidget, "Image Preview");
    // The preview starts in the autostretch display, the one linear frames are seen in and the one the proxies hold.
    m_impl->preview->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    m_impl->previewListener = std::make_unique<PreviewListener>(m_impl->preview, [this]() { previewFrameShown(); });
    m_impl->displayWatcher = std::make_unique<PreviewDisplayWatcher>([this]() { previewDisplayChanged(); });
    m_impl->displayWatcher->attachToViewWidget(m_impl->preview->getViewWidget());
    m_impl->histogram = std::make_unique<an::ui::HistogramCountScheduler>(ui.histogramWidget);
    ui.histogramWidget->setClippingInfoEnabled(false);

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
    options.offerMoves = false;
    options.offerOpenInEditor = true;
    options.busy = [this]() { return busy(); };
    options.stop = windowStopToken();
    m_impl->files = std::make_unique<WorkflowFileList>(
        this,
        an::ui::processes::WorkflowFileListWidgets{.table = ui.tableWidget,
                                                   .removeSelected = ui.removeSelected,
                                                   .clearAll = ui.clearAll,
                                                   .currentLabel = ui.currentImageLabel,
                                                   .totalLabel = ui.totalImagesLabel},
        std::move(options));
    m_impl->files->onChanged = [this]() { listChanged(); };
    m_impl->files->onSelected = [this](int row) { rowSelected(row); };
    m_impl->files->onImportFinished = [this](const an::ui::processes::WorkflowImportReport& report) {
        an::ui::processes::sayWorkflowImport(this, report);
    };

    m_impl->temporary = std::make_unique<QTemporaryDir>(an::ui::ensureTempDirPathOrSystemTemp() +
                                                        QStringLiteral("/ImageListAnalyzer_") +
                                                        QUuid::createUuid().toString(QUuid::WithoutBraces));

    updateOptimizationControls();
    updateSummaryAndHistogram();
    initializeTopLevelProcess();
}

ImageListAnalyzerProcess::~ImageListAnalyzerProcess()
{
    m_impl->displayWatcher.reset();
    m_impl->previewListener.reset();
}

std::string ImageListAnalyzerProcess::getProcessNameStatic()
{
    return "Image List Analyzer";
}

void ImageListAnalyzerProcess::refreshTheme()
{
    TopLevelProcessBaseControl::refreshTheme();
    m_impl->ui.tableWidget->setupSelectionColors();
    updateOptimizationControls();
}

void ImageListAnalyzerProcess::addItemsNonBlocking(const QStringList& items)
{
    m_impl->files->addFiles(items);
}

void ImageListAnalyzerProcess::removeItems(const std::vector<QString>& items)
{
    if (busy())
    {
        return;
    }
    for (const QString& path : items)
    {
        if (const auto it = m_impl->proxies.find(path); it != m_impl->proxies.end())
        {
            QFile::remove(it->second.path);
            m_impl->proxies.erase(it);
        }
    }
    m_impl->files->remove(items);
}

const WorkflowFileList& ImageListAnalyzerProcess::fileList() const
{
    return *m_impl->files;
}

ImageViewControl* ImageListAnalyzerProcess::previewView() const
{
    return m_impl->preview;
}

bool ImageListAnalyzerProcess::isIdle() const
{
    return m_impl->files->idle() && !m_impl->optimizing && m_impl->previewsInFlight == 0u &&
           !m_impl->displayChangeQueued && (m_impl->histogram == nullptr || m_impl->histogram->isIdle());
}

bool ImageListAnalyzerProcess::imagesOptimized() const
{
    const AutostretchParams shown = m_impl->preview->getAutostretchSettings();
    for (const WorkflowFile& file : m_impl->files->files())
    {
        const auto it = m_impl->proxies.find(file.path());
        if (it == m_impl->proxies.end() || !(it->second.stretch == shown))
        {
            return false;
        }
    }
    return true;
}

QString ImageListAnalyzerProcess::proxyPathOf(const QString& path) const
{
    const auto it = m_impl->proxies.find(path);
    return it == m_impl->proxies.end() ? QString() : it->second.path;
}

QString ImageListAnalyzerProcess::previewSequencePath() const
{
    return m_impl->previewManifest;
}

QString ImageListAnalyzerProcess::optimizationStatusText() const
{
    return m_impl->ui.showInfoLabel->text();
}

an::ui::processes::ImageListAnalyzerProcessUiElements& ImageListAnalyzerProcess::ui()
{
    return m_impl->ui;
}

void ImageListAnalyzerProcess::optimize()
{
    const an::consumer::EngineSession* session = currentSession();
    if (busy() || session == nullptr || m_impl->temporary == nullptr || !m_impl->temporary->isValid())
    {
        return;
    }
    // The settings are read here, on the GUI thread, so the workers read none.
    const AutostretchParams shown = m_impl->preview->getAutostretchSettings();
    std::vector<QString> sources;
    for (const WorkflowFile& file : m_impl->files->files())
    {
        // A file whose proxy holds other stretch settings has none for these.
        const auto it = m_impl->proxies.find(file.path());
        if (it == m_impl->proxies.end() || !(it->second.stretch == shown))
        {
            sources.push_back(file.path());
        }
    }
    if (sources.empty())
    {
        updateOptimizationControls();
        return;
    }
    const QString directory = m_impl->proxyDirectory(shown);
    if (directory.isEmpty())
    {
        LOG_WARNING("Image List Analyzer: no folder for the proxies could be made in '{}'",
                    m_impl->temporary->path().toStdString());
        return;
    }
    struct Pending
    {
        QString source;
        QString proxy;
    };
    std::vector<Pending> pending;
    pending.reserve(sources.size());
    for (const QString& source : sources)
    {
        pending.push_back(
            Pending{source, QDir(directory).filePath(ImageListProcessHelpers::optimizedProxyFileName(source))});
    }
    const an::DemosaicMethod demosaic = an::ui::demosaicMethodFromSettings();
    const an::ui::AutostretchApplySettings stretch = stretchOf(shown);

    auto* progress = new ProgressDialogControl(this, QStringLiteral("Optimizing image list"));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->setText(QStringLiteral("Optimizing %1 images").arg(pending.size()));
    progress->show();
    m_impl->progress = progress;
    setBusy(true);
    m_impl->optimizing = true;

    QPointer<ImageListAnalyzerProcess> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    const std::stop_token windowStop = windowStopToken();
    const std::stop_token cancel = progress->stopToken();
    QThreadPool::globalInstance()->start([session, pending, demosaic, stretch, shown, guard, dialog, windowStop,
                                          cancel]() {
        std::stop_source stop;
        const std::stop_callback onClose(windowStop, [&stop]() { stop.request_stop(); });
        const std::stop_callback onCancel(cancel, [&stop]() { stop.request_stop(); });
        auto written = std::make_shared<std::vector<std::pair<QString, QString>>>();
        auto failures = std::make_shared<std::vector<std::pair<QString, an::Status>>>();
        bool stopped = false;
        for (std::size_t index = 0; index < pending.size(); ++index)
        {
            if (stop.stop_requested())
            {
                stopped = true;
                break;
            }
            const an::Status made =
                writeProxy(*session, pending[index].source, pending[index].proxy, demosaic, stretch, stop.get_token());
            if (made.ok())
            {
                written->emplace_back(pending[index].source, pending[index].proxy);
            }
            else if (made.code() == an::ErrorCode::Cancelled)
            {
                stopped = true;
                break;
            }
            else
            {
                LOG_WARNING("Image List Analyzer: no proxy of '{}': {}", pending[index].source.toStdString(),
                            made.message());
                failures->emplace_back(pending[index].source, made);
            }
            const int percent = static_cast<int>(((index + 1u) * 100u) / pending.size());
            postToGui([dialog, percent]() {
                if (dialog)
                {
                    dialog->setValue(percent);
                }
            });
        }
        postToGui([guard, dialog, written, failures, stopped, shown]() {
            if (dialog)
            {
                dialog->closeAsync();
            }
            if (!guard)
            {
                for (const auto& [source, proxy] : *written)
                {
                    QFile::remove(proxy);
                }
                return;
            }
            Impl& impl = *guard->m_impl;
            std::vector<QString> replaced;
            for (auto& [source, proxy] : *written)
            {
                // A file removed while its proxy was being written has no row to show it.
                if (impl.files->rowOf(source) >= 0)
                {
                    if (const auto it = impl.proxies.find(source); it != impl.proxies.end() && it->second.path != proxy)
                    {
                        replaced.push_back(it->second.path);
                    }
                    impl.proxies.insert_or_assign(source, ProxyFile{proxy, shown});
                }
                else
                {
                    QFile::remove(proxy);
                }
            }
            // A proxy of other stretch settings gives way to the one written now, once the preview lets it go: a
            // preview that shows proxies shows every file's.
            if (!replaced.empty() && impl.previewShowsProxies)
            {
                impl.previewShowsProxies = false;
                impl.preview->clear();
                guard->updateSummaryAndHistogram();
            }
            for (const QString& proxy : replaced)
            {
                QFile::remove(proxy);
            }
            impl.optimizing = false;
            impl.progress.clear();
            guard->setBusy(false);
            guard->updateOptimizationControls();
            guard->rebuildPreviewSequence();
            if (!failures->empty() && !stopped)
            {
                process_utils::notifyErrorMessage(
                    guard, QStringLiteral("Image optimization incomplete"),
                    QStringLiteral("%1 image(s) could not be optimized. First error: %2 See the log for per-file "
                                   "details.")
                        .arg(failures->size())
                        .arg(QString::fromStdString(failures->front().second.message())));
            }
        });
    });
}

void ImageListAnalyzerProcess::exportSequence()
{
    const an::consumer::EngineSession* session = currentSession();
    const std::vector<WorkflowFile>& files = m_impl->files->files();
    if (busy() || session == nullptr || files.empty() || m_impl->temporary == nullptr || !m_impl->temporary->isValid())
    {
        return;
    }
    // The export reads the original files, never their proxies.
    std::vector<QString> paths;
    paths.reserve(files.size());
    for (const WorkflowFile& file : files)
    {
        paths.push_back(file.path());
    }
    keepPlayerRate();
    const an::ImageDescriptor first = files.front().entry.descriptor;
    const an::ImageListManifest manifest = manifestOf(
        paths,
        ListFrame{first.extent().width(), first.extent().height(), first.extent().channelCount(), first.isMosaiced()},
        m_impl->framesPerSecond);
    const QString manifestPath =
        QDir(m_impl->temporary->path())
            .filePath(QStringLiteral("export_%1%2")
                          .arg(QUuid::createUuid().toString(QUuid::Id128))
                          .arg(QString::fromLatin1(an::kImageListManifestExtension.data(),
                                                   static_cast<qsizetype>(an::kImageListManifestExtension.size()))));
    const AutostretchParams stretch = m_impl->preview->getAutostretchSettings();
    QPointer<ImageListAnalyzerProcess> guard(this);
    const std::stop_token stop = windowStopToken();
    ++m_impl->previewsInFlight;
    QThreadPool::globalInstance()->start([session, manifestPath, manifest, first, stretch, guard, stop]() {
        auto opened = std::make_shared<an::Result<an::ui::ImageDocument>>(
            writeAndOpenList(*session, manifestPath, manifest, stop));
        postToGui([guard, opened, first, stretch]() {
            if (!guard)
            {
                return;
            }
            --guard->m_impl->previewsInFlight;
            if (!opened->hasValue())
            {
                process_utils::notifyErrorMessage(guard, QStringLiteral("Export Sequence"),
                                                  QString::fromStdString(opened->status().message()));
                return;
            }
            std::optional<an::ui::DocumentSequence> sequence = (*opened)->sequence();
            if (!sequence.has_value())
            {
                return;
            }
            sequence->demosaic = an::ui::demosaicMethodFromSettings();
            auto* window = new ExportSequenceControl(guard, *sequence, first.extent().width(), first.extent().height(),
                                                     first.isMosaiced(), stretch);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
        });
    });
}

void ImageListAnalyzerProcess::closeEvent(QCloseEvent* evt)
{
    // Everything the window started stops with the window's token, requested as the base closes, and lands on a window
    // that is gone; nothing is waited for.
    if (m_impl->progress)
    {
        m_impl->progress->requestCancel();
    }
    if (auto* player = m_impl->preview ? m_impl->preview->sequencePlayer() : nullptr)
    {
        player->shutdown();
    }
    TopLevelProcessBaseControl::closeEvent(evt);
}

std::string ImageListAnalyzerProcess::serialize_json_settings()
{
    keepPlayerRate();
    nlohmann::json document;
    document["version"] = 1;
    document["params"]["fps"] = m_impl->framesPerSecond;
    return document.dump();
}

void ImageListAnalyzerProcess::deserialize_json_settings(const std::string& json_settings)
{
    if (json_settings.empty())
    {
        return;
    }
    try
    {
        const nlohmann::json document = nlohmann::json::parse(json_settings);
        const auto params = document.find("params");
        if (params != document.end() && params->is_object())
        {
            const double fps = params->value("fps", kDefaultFramesPerSecond);
            m_impl->framesPerSecond = std::clamp(fps, 1.0, 60.0);
            // A player already shown takes the restored rate, which it otherwise would hand back on its next read.
            if (auto* player =
                    dynamic_cast<SequencePlayerControl*>(m_impl->preview ? m_impl->preview->sequencePlayer() : nullptr))
            {
                player->setFramesPerSecond(m_impl->framesPerSecond);
            }
        }
    }
    catch (const nlohmann::json::exception& e)
    {
        LOG_ERROR("ImageListAnalyzerProcess: Could NOT deserialize settings: {}", e.what());
    }
}

void ImageListAnalyzerProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    const QDialogButtonBox::StandardButton standard = m_impl->ui.buttonBox->standardButton(button);
    if (standard == QDialogButtonBox::Close)
    {
        close();
    }
    else if (standard == QDialogButtonBox::Reset)
    {
        // The window keeps one setting, the playback rate, which Reset returns to its default.
        m_impl->framesPerSecond = kDefaultFramesPerSecond;
        if (auto* player = dynamic_cast<SequencePlayerControl*>(m_impl->preview->sequencePlayer()))
        {
            player->setFramesPerSecond(kDefaultFramesPerSecond);
        }
    }
}

void ImageListAnalyzerProcess::on_addFilesButtonPushed()
{
    addItemsNonBlocking(
        process_utils::selectImageFiles(this, QStringLiteral("Select Files"), QString::fromLatin1(kFolderContext)));
}

void ImageListAnalyzerProcess::on_addFolderButtonPushed()
{
    const ImageListProcessHelpers::AddFolderResult folder =
        ImageListProcessHelpers::showAddFolderDialog(this, QString::fromLatin1(kFolderContext));
    if (!folder.accepted)
    {
        return;
    }
    m_impl->files->addFiles(folder.files, an::ui::processes::WorkflowFileFilter{.frameType = folder.typeFilter,
                                                                                .filterName = folder.filterName});
}

void ImageListAnalyzerProcess::listChanged()
{
    // A file no longer listed lets its proxy go.
    for (auto it = m_impl->proxies.begin(); it != m_impl->proxies.end();)
    {
        if (m_impl->files->rowOf(it->first) < 0)
        {
            QFile::remove(it->second.path);
            it = m_impl->proxies.erase(it);
        }
        else
        {
            ++it;
        }
    }
    const bool any = !m_impl->files->files().empty();
    m_impl->ui.exportSequenceButton->setEnabled(any && !busy());
    m_impl->ui.histogramWidget->setClippingInfoEnabled(any);
    updateOptimizationControls();
    rebuildPreviewSequence();
}

void ImageListAnalyzerProcess::rowSelected(const int row)
{
    updateSummaryAndHistogram();
    an::ui::IViewSequencePlayer* player = m_impl->preview->sequencePlayer();
    if (row < 0 || player == nullptr || m_impl->ignoreFrameChange)
    {
        return;
    }
    if (player->currentFrame() == static_cast<std::uint64_t>(row))
    {
        return;
    }
    m_impl->ignoreFrameChange = true;
    QPointer<ImageListAnalyzerProcess> guard(this);
    player->setCurrentFrame(static_cast<std::uint64_t>(row), [guard]() {
        if (guard)
        {
            guard->m_impl->ignoreFrameChange = false;
            guard->updateSummaryAndHistogram();
        }
    });
}

void ImageListAnalyzerProcess::previewFrameShown()
{
    an::ui::IViewSequencePlayer* player = m_impl->preview ? m_impl->preview->sequencePlayer() : nullptr;
    if (player != nullptr && !m_impl->ignoreFrameChange)
    {
        const auto frame = static_cast<int>(player->currentFrame());
        if (frame >= 0 && frame < static_cast<int>(m_impl->files->files().size()) &&
            frame != m_impl->files->selectedRow())
        {
            // The player moved: the table follows, without asking the player to move again.
            m_impl->ignoreFrameChange = true;
            m_impl->files->selectRow(frame);
            m_impl->ignoreFrameChange = false;
        }
    }
    updateSummaryAndHistogram();
}

void ImageListAnalyzerProcess::keepPlayerRate()
{
    // The player's control is where the person sets the rate, so what it shows is the rate the window keeps.
    if (auto* player =
            dynamic_cast<SequencePlayerControl*>(m_impl->preview ? m_impl->preview->sequencePlayer() : nullptr))
    {
        m_impl->framesPerSecond = player->framesPerSecond();
    }
}

void ImageListAnalyzerProcess::previewDisplayChanged()
{
    // Heard while the view changes its display - inside its selector's signal, say - where following the change would
    // clear the view, and so rebuild that selector, under its own signal.
    if (m_impl->displayChangeQueued)
    {
        return;
    }
    m_impl->displayChangeQueued = true;
    QMetaObject::invokeMethod(
        this,
        [this]() {
            m_impl->displayChangeQueued = false;
            followPreviewDisplay();
        },
        Qt::QueuedConnection);
}

void ImageListAnalyzerProcess::followPreviewDisplay()
{
    dropProxiesOfAnotherDisplay();
    if (previewPlaysProxies() != m_impl->previewAskedForProxies)
    {
        rebuildPreviewSequence();
    }
    updateOptimizationControls();
}

bool ImageListAnalyzerProcess::proxiesHoldTheDisplay(const AutostretchParams& stretch) const
{
    return m_impl->preview->isAutoStretchEnabled() && m_impl->preview->getAutostretchSettings() == stretch;
}

bool ImageListAnalyzerProcess::previewPlaysProxies() const
{
    return !m_impl->files->files().empty() && m_impl->preview->isAutoStretchEnabled() && imagesOptimized();
}

void ImageListAnalyzerProcess::dropProxiesOfAnotherDisplay()
{
    if (m_impl->previewShowsProxies && !proxiesHoldTheDisplay(m_impl->previewProxyStretch))
    {
        // Shown in another display, the proxies would show what it is not: they go at once, and the files take their
        // place when they are read.
        m_impl->previewShowsProxies = false;
        m_impl->preview->clear();
        updateSummaryAndHistogram();
    }
}

void ImageListAnalyzerProcess::rebuildPreviewSequence()
{
    const an::consumer::EngineSession* session = currentSession();
    const std::vector<WorkflowFile>& files = m_impl->files->files();
    const std::uint64_t generation = ++m_impl->previewGeneration;
    keepPlayerRate();
    if (auto* player = m_impl->preview->sequencePlayer())
    {
        player->stop();
    }
    if (files.empty() || session == nullptr || m_impl->temporary == nullptr || !m_impl->temporary->isValid())
    {
        m_impl->previewAskedForProxies = false;
        m_impl->previewShowsProxies = false;
        m_impl->preview->clear();
        m_impl->previewManifest.clear();
        updateSummaryAndHistogram();
        return;
    }
    // The proxies play in the display they hold, every file's or none: a manifest describes its frames by the first
    // one.
    const bool proxies = previewPlaysProxies();
    const AutostretchParams stretch = m_impl->preview->getAutostretchSettings();
    m_impl->previewAskedForProxies = proxies;
    std::vector<QString> paths;
    paths.reserve(files.size());
    for (const WorkflowFile& file : files)
    {
        paths.push_back(proxies ? m_impl->proxies.at(file.path()).path : file.path());
    }
    const an::ImageDescriptor& firstFile = files.front().entry.descriptor;
    ListFrame first{firstFile.extent().width(), firstFile.extent().height(), firstFile.extent().channelCount(),
                    firstFile.isMosaiced()};
    if (proxies)
    {
        // A proxy is demosaiced: three channels for a mosaic or a colour file, one for a grey one.
        first.channels = (first.mosaiced || first.channels >= 3u) ? 3u : 1u;
        first.mosaiced = false;
    }
    const an::ImageListManifest manifest = manifestOf(paths, first, m_impl->framesPerSecond);
    const QString manifestPath =
        QDir(m_impl->temporary->path())
            .filePath(QStringLiteral("preview_%1%2")
                          .arg(generation)
                          .arg(QString::fromLatin1(an::kImageListManifestExtension.data(),
                                                   static_cast<qsizetype>(an::kImageListManifestExtension.size()))));
    const int selected = (std::max)(0, m_impl->files->selectedRow());
    QPointer<ImageListAnalyzerProcess> guard(this);
    const std::stop_token stop = windowStopToken();
    ++m_impl->previewsInFlight;
    QThreadPool::globalInstance()->start(
        [session, manifestPath, manifest, guard, stop, generation, proxies, stretch, selected]() {
            auto opened = std::make_shared<an::Result<an::ui::ImageDocument>>(
                writeAndOpenList(*session, manifestPath, manifest, stop));
            postToGui([guard, opened, generation, proxies, stretch, selected, manifestPath]() {
                if (!guard)
                {
                    return;
                }
                Impl& impl = *guard->m_impl;
                --impl.previewsInFlight;
                if (generation != impl.previewGeneration)
                {
                    QFile::remove(manifestPath);
                    return;
                }
                if (!opened->hasValue())
                {
                    LOG_WARNING("Image List Analyzer: the preview sequence could not be opened: {}",
                                opened->status().message());
                    impl.previewShowsProxies = false;
                    impl.preview->clear();
                    guard->updateSummaryAndHistogram();
                    return;
                }
                if (std::optional<an::ui::DocumentSequence> sequence = (*opened)->sequence(); sequence.has_value())
                {
                    sequence->demosaic = an::ui::demosaicMethodFromSettings();
                    if (static_cast<std::uint64_t>(selected) < sequence->descriptor.frameCount.value_or(0u))
                    {
                        sequence->currentFrame = static_cast<std::uint64_t>(selected);
                    }
                    (*opened)->setSequence(std::move(sequence));
                }
                if (!impl.previewManifest.isEmpty() && impl.previewManifest != manifestPath)
                {
                    QFile::remove(impl.previewManifest);
                }
                impl.previewManifest = manifestPath;
                impl.previewShowsProxies = proxies;
                impl.previewProxyStretch = stretch;
                // A rate the person set while the preview was being made is kept.
                guard->keepPlayerRate();
                // A proxy is the autostretch display it holds, shown as it is; an original file is shown in the
                // display mode the person chose.
                impl.preview->getViewWidget()->setImageHoldsAutostretch(proxies);
                impl.preview->setSequenceDocument(std::move(**opened), true);
                if (auto* player = dynamic_cast<SequencePlayerControl*>(impl.preview->sequencePlayer()))
                {
                    player->setFramesPerSecondControlVisible(true);
                    player->setFramesPerSecond(impl.framesPerSecond);
                }
                guard->updateOptimizationControls();
                guard->updateSummaryAndHistogram();
            });
        });
}

void ImageListAnalyzerProcess::updateOptimizationControls()
{
    QLabel* label = m_impl->ui.showInfoLabel;
    const bool any = !m_impl->files->files().empty();
    const bool optimized = imagesOptimized();
    const bool autostretch = m_impl->preview->isAutoStretchEnabled();
    QString text;
    QString tooltip;
    QString playTooltip;
    an::ui::styling::SemanticColorRole role = an::ui::styling::SemanticColorRole::StatusSuccess;
    if (!any)
    {
        text = QStringLiteral("No Optimization Needed");
        tooltip = QStringLiteral("Image list is empty. No optimization is needed");
    }
    else if (!optimized)
    {
        const AutostretchParams shown = m_impl->preview->getAutostretchSettings();
        const bool otherSettings =
            std::any_of(m_impl->proxies.cbegin(), m_impl->proxies.cend(),
                        [&shown](const auto& entry) { return !(entry.second.stretch == shown); });
        text = QStringLiteral("Images Need Optimization");
        tooltip = otherSettings ? QStringLiteral("The images were optimized for other Autostretch settings. Click "
                                                 "Optimize to optimize them for these.")
                                : QStringLiteral("Images need to be optimized for smooth playback. Click Optimize to "
                                                 "start.");
        playTooltip = QStringLiteral("Images must be optimized before playback. Click Optimize to start.");
        role = an::ui::styling::SemanticColorRole::StatusError;
    }
    else if (!autostretch)
    {
        text = QStringLiteral("Fast Play Needs Autostretch");
        tooltip = QStringLiteral("The optimized images hold the Autostretch display, so they play only in it. The "
                                 "Linear display shows the original files, which do not play.");
        playTooltip = QStringLiteral("Fast play shows the optimized images, which hold the Autostretch display. "
                                     "Choose the Autostretch display mode to play.");
        role = an::ui::styling::SemanticColorRole::StatusWarning;
    }
    else
    {
        text = QStringLiteral("Images Already Optimized");
        tooltip = QStringLiteral("Images are already optimized for smooth playback.");
    }
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setToolTip(tooltip);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, an::ui::styling::semanticColor(palette, role));
    label->setPalette(palette);
    QFont font = an::ui::tokens().typography.body;
    font.setWeight(QFont::DemiBold);
    label->setFont(font);

    m_impl->ui.optimizeButton->setEnabled(any && !optimized && !busy());
    if (auto* player = dynamic_cast<SequencePlayerControl*>(m_impl->preview->sequencePlayer()))
    {
        // A list plays only while it plays proxies, which is what makes it fast; they leave any display they do not
        // hold.
        player->setPlayButtonEnabled(any && optimized && m_impl->previewShowsProxies, playTooltip);
    }
}

void ImageListAnalyzerProcess::updateSummaryAndHistogram()
{
    const WorkflowFile* file = m_impl->files->selectedFile();
    if (file == nullptr)
    {
        m_impl->ui.imageSummary->setHtml(QStringLiteral("<h2>The image list is empty</h2>"));
        m_impl->histogram->count(nullptr);
        return;
    }
    m_impl->ui.imageSummary->setHtml(QString::fromUtf8(getImageFullReport(
        file->entry.descriptor, file->entry.metadata, reportFileFacts(file->path().toUtf8().toStdString()))));
    // The histogram counts the frame the preview shows, on the thread pool.
    m_impl->histogram->count(m_impl->preview->hasImage() ? m_impl->preview->document().asset() : nullptr);
}

void ImageListAnalyzerProcess::setBusy(const bool busy)
{
    auto& ui = m_impl->ui;
    ui.tableWidget->setEnabled(!busy);
    ui.addFiles->setEnabled(!busy);
    ui.addFolder->setEnabled(!busy);
    ui.removeSelected->setEnabled(!busy && !m_impl->files->files().empty());
    ui.clearAll->setEnabled(!busy && !m_impl->files->files().empty());
    ui.exportSequenceButton->setEnabled(!busy && !m_impl->files->files().empty());
    if (auto* player = m_impl->preview->sequencePlayer())
    {
        player->setEnabled(!busy);
    }
}

bool ImageListAnalyzerProcess::busy() const
{
    return m_impl->optimizing;
}
