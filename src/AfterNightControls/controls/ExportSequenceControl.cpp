// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ExportSequenceControl.h"

#include "app/AppContextHelpers.h"
#include "app/FileDialogUtils.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/ProgressDialogControl.h"
#include "controls/SequenceContainers.h"
#include "session/IEngineSessionProvider.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <AfterNightPlatform/Logging.h>

#include <nlohmann/json.hpp>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

using json = nlohmann::json;

namespace
{

/// The labels the format, coding, resolution and fill selectors show, which the settings persist.
const QString kSerFormat = QStringLiteral("SER");
const QString kRawAviFormat = QStringLiteral("AVI (RAW)");
const QString kVideoFormat = QStringLiteral("MP4");
const QString kH264Codec = QStringLiteral("h264");
const QString kH265Codec = QStringLiteral("h265");
const QString kVp9Codec = QStringLiteral("WEBM");
const QString kPreserveRatio = QStringLiteral("Preserve Ratio");
const QString kFreeRatio = QStringLiteral("Free Ratio");

/// A failure the export decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The process's session, or null when the application lends none.
const an::consumer::EngineSession* currentSession()
{
    if (auto* provider = process_utils::currentEngineSessionProvider())
    {
        return &provider->engineSession();
    }
    return nullptr;
}

/// Runs a function on the GUI thread through the event loop, never waited on.
void postToGui(std::function<void()> function)
{
    if (QCoreApplication* application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(function), Qt::QueuedConnection);
    }
}

/// A stop source stopped by either of two tokens, for as long as it lives.
class LinkedStop
{
public:
    LinkedStop(const std::stop_token& first, const std::stop_token& second)
        : m_first(first, Stopper{&m_source}), m_second(second, Stopper{&m_source})
    {
    }

    [[nodiscard]] std::stop_token token() const noexcept
    {
        return m_source.get_token();
    }

private:
    struct Stopper
    {
        std::stop_source* source;
        void operator()() const noexcept
        {
            source->request_stop();
        }
    };

    std::stop_source m_source;
    std::stop_callback<Stopper> m_first;
    std::stop_callback<Stopper> m_second;
};

/// The container an export's format and coding write.
an::SequenceFormat exportContainer(an::ui::SequenceExportFormat format, an::ui::SequenceExportCodec codec)
{
    switch (format)
    {
    case an::ui::SequenceExportFormat::Ser:
        return an::SequenceFormat::Ser;
    case an::ui::SequenceExportFormat::RawAvi:
        return an::SequenceFormat::Avi;
    case an::ui::SequenceExportFormat::Video:
        break;
    }
    return codec == an::ui::SequenceExportCodec::Vp9 ? an::SequenceFormat::Video : an::SequenceFormat::Mp4;
}

/// The description of an image at another extent, with square pixels: what a resize or a canvas states.
std::optional<an::ImageDescriptor> describedAt(const an::EngineContext& engine, const an::ImageDescriptor& stored,
                                               std::uint32_t width, std::uint32_t height)
{
    an::ImageDescriptorRequest request = stored.toRequest();
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, stored.extent().channelCount(), engine.limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    request.extent = *extent;
    request.pixelAspectRatio = 1.0;
    return an::ImageDescriptor::create(request);
}

/// Resizes a typed image through the engine's Lanczos interpolation, into a planar image with square pixels.
template <typename SampleT>
an::Result<an::consumer::AnyImage> resizeTyped(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                               const an::Image<SampleT>& image, std::uint32_t width,
                                               std::uint32_t height)
{
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    an::ResizeRequest request;
    request.width = width;
    request.height = height;
    request.sampling.interpolation = an::KernelInterpolation::Lanczos4;
    const an::Result<an::GeometryPlan> plan = an::planResize(*stored, request);
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const std::optional<an::ImageDescriptor> described = describedAt(engine, *stored, width, height);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the resized frame has no description");
    }
    an::Result<an::Image<SampleT>> resized = an::Image<SampleT>::createDestination(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!resized.hasValue())
    {
        return resized.status();
    }
    an::Result<an::ValidityMask> written = an::ValidityMask::create(engine, plan->destinationDescriptor().extent());
    if (!written.hasValue())
    {
        return written.status();
    }
    std::optional<an::ValidityMaskView<std::uint8_t>> writtenView = written->view();
    if (!writtenView.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the resized frame's mask holds nothing");
    }
    // A sequence frame carries no mask of its own; a rule that needs one reads every sample as valid.
    an::GeometrySelection selection;
    const an::ValidityDescriptor rule = image.metadata().semantics().validity();
    if (rule.rule() != an::ValidityRule::ExplicitMask)
    {
        selection.validity = rule;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = resized->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the resized frame holds nothing");
        }
        const an::Result<an::GeometryResult> ran =
            an::transformImageGeometry(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                       access->view().data(), access->view().layout(), *plan, selection, *writtenView);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    return an::consumer::AnyImage(std::move(*resized));
}

/// Places a typed image on a black canvas of another size, centred: padded where it is smaller and cropped about its
/// centre where it is larger.
template <typename SampleT>
an::Result<an::consumer::AnyImage> placeOnCanvas(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                                 const an::Image<SampleT>& image, std::uint32_t width,
                                                 std::uint32_t height)
{
    const std::optional<an::ImageDescriptor> stored = image.descriptor();
    const auto view = image.view();
    if (!stored.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const std::optional<an::ImageDescriptor> described = describedAt(engine, *stored, width, height);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the canvas has no description");
    }
    an::Result<an::Image<SampleT>> canvas = an::Image<SampleT>::createDestination(
        engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, image);
    if (!canvas.hasValue())
    {
        return canvas.status();
    }
    const std::uint32_t sourceWidth = stored->extent().width();
    const std::uint32_t sourceHeight = stored->extent().height();
    const std::uint32_t copyWidth = (std::min)(sourceWidth, width);
    const std::uint32_t copyHeight = (std::min)(sourceHeight, height);
    const std::uint32_t sourceX = sourceWidth > width ? (sourceWidth - width) / 2u : 0u;
    const std::uint32_t sourceY = sourceHeight > height ? (sourceHeight - height) / 2u : 0u;
    const std::uint32_t destinationX = width > sourceWidth ? (width - sourceWidth) / 2u : 0u;
    const std::uint32_t destinationY = height > sourceHeight ? (height - sourceHeight) / 2u : 0u;
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = canvas->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the canvas holds nothing");
        }
        const auto from = view->subview(sourceX, sourceY, copyWidth, copyHeight);
        const auto to = access->view().subview(destinationX, destinationY, copyWidth, copyHeight);
        if (!from.has_value() || !to.has_value())
        {
            return refusal(an::ErrorCode::Internal, "the frame does not fit the canvas");
        }
        const an::Status copied = an::repackImageSamples(execution, an::kSampleTypeOf<SampleT>, from->data(),
                                                         from->layout(), to->data(), to->layout());
        if (copied.failed())
        {
            return copied;
        }
    }
    return an::consumer::AnyImage(std::move(*canvas));
}

/// Fits a frame into the size an export asks for, as its fill mode says.
an::Result<an::consumer::AnyImage> fitFrame(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                            an::consumer::AnyImage&& frame, std::uint32_t width, std::uint32_t height,
                                            an::ui::SequenceExportFill fill)
{
    const std::optional<an::ImageDescriptor> stored =
        std::visit([](const auto& typed) { return typed.descriptor(); }, frame);
    if (!stored.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const std::uint32_t sourceWidth = stored->extent().width();
    const std::uint32_t sourceHeight = stored->extent().height();
    if (sourceWidth == width && sourceHeight == height)
    {
        return std::move(frame);
    }
    const auto resize = [&](const an::consumer::AnyImage& image, std::uint32_t toWidth, std::uint32_t toHeight) {
        return std::visit([&](const auto& typed) { return resizeTyped(engine, execution, typed, toWidth, toHeight); },
                          image);
    };
    const auto place = [&](const an::consumer::AnyImage& image) {
        return std::visit([&](const auto& typed) { return placeOnCanvas(engine, execution, typed, width, height); },
                          image);
    };
    const double horizontal = static_cast<double>(width) / static_cast<double>(sourceWidth);
    const double vertical = static_cast<double>(height) / static_cast<double>(sourceHeight);
    switch (fill)
    {
    case an::ui::SequenceExportFill::Stretch:
        return resize(frame, width, height);
    case an::ui::SequenceExportFill::Center:
        return place(frame);
    case an::ui::SequenceExportFill::Fit:
    case an::ui::SequenceExportFill::Fill: {
        const double scale = fill == an::ui::SequenceExportFill::Fit ? (std::min)(horizontal, vertical)
                                                                     : (std::max)(horizontal, vertical);
        const auto scaledWidth = static_cast<std::uint32_t>((std::max)(1.0, std::floor(sourceWidth * scale)));
        const auto scaledHeight = static_cast<std::uint32_t>((std::max)(1.0, std::floor(sourceHeight * scale)));
        an::Result<an::consumer::AnyImage> scaled = resize(frame, scaledWidth, scaledHeight);
        if (!scaled.hasValue())
        {
            return scaled.status();
        }
        return place(*scaled);
    }
    }
    return refusal(an::ErrorCode::InvalidArgument, "no fill mode is named");
}

/// The view's autostretch settings, as the export's stretch takes them.
an::ui::AutostretchApplySettings stretchOf(const AutostretchParams& params)
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

} // namespace

namespace an::ui
{

QString sequenceExportSuffix(SequenceExportFormat format, SequenceExportCodec codec)
{
    switch (format)
    {
    case SequenceExportFormat::Ser:
        return QStringLiteral("ser");
    case SequenceExportFormat::RawAvi:
        return QStringLiteral("avi");
    case SequenceExportFormat::Video:
        break;
    }
    return codec == SequenceExportCodec::Vp9 ? QStringLiteral("webm") : QStringLiteral("mp4");
}

QString normalizeSequenceExportPath(const QString& path, const QString& expectedSuffix)
{
    QString normalizedPath = path.trimmed();
    QString normalizedSuffix = expectedSuffix.trimmed();
    if (normalizedPath.isEmpty() || normalizedSuffix.isEmpty())
    {
        return normalizedPath;
    }
    if (normalizedSuffix.startsWith(QLatin1Char('.')))
    {
        normalizedSuffix.remove(0, 1);
    }
    const QString dottedSuffix = QLatin1Char('.') + normalizedSuffix;
    if (normalizedPath.endsWith(dottedSuffix, Qt::CaseInsensitive))
    {
        return normalizedPath;
    }
    const int forwardSlash = normalizedPath.lastIndexOf(QLatin1Char('/'));
    const int backSlash = normalizedPath.lastIndexOf(QLatin1Char('\\'));
    const int lastSeparator = forwardSlash > backSlash ? forwardSlash : backSlash;
    const int lastDot = normalizedPath.lastIndexOf(QLatin1Char('.'));
    if (lastDot > lastSeparator)
    {
        normalizedPath.truncate(lastDot);
    }
    return normalizedPath + dottedSuffix;
}

an::Status exportSequence(const an::consumer::EngineSession& session, const ExportSequenceRequest& request,
                          std::stop_token stop, const std::function<void(std::uint64_t, std::uint64_t)>& progress)
{
    if (request.outputPath.isEmpty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the export names no file");
    }
    if (request.resize && (request.width == 0u || request.height == 0u))
    {
        return refusal(an::ErrorCode::InvalidArgument, "a resized export needs a width and a height");
    }
    const auto admit = [&]() {
        return session.admit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .stopToken = stop});
    };
    an::Result<an::consumer::OperationScope> opening = admit();
    if (!opening.hasValue())
    {
        return opening.status();
    }
    an::Result<OpenedSequence> opened = openSequenceSource(
        opening->engine(), opening->execution(), request.sequence.sourcePath, request.sequence.descriptor.format);
    if (!opened.hasValue())
    {
        return opened.status();
    }
    opening->end();
    const std::uint64_t total = opened->descriptor.frameCount.value_or(0u);

    SequenceWriteSettings settings;
    settings.container = exportContainer(request.format, request.codec);
    settings.frameRate = sequenceRateOf(request.fps);
    settings.codec = request.codec == SequenceExportCodec::H265  ? an::FfmpegVideoCodec::H265
                     : request.codec == SequenceExportCodec::Vp9 ? an::FfmpegVideoCodec::Vp9
                                                                 : an::FfmpegVideoCodec::H264;
    settings.frameCount = opened->descriptor.frameCount;
    an::Result<SequenceWriter> writer = SequenceWriter::create(request.outputPath, settings);
    if (!writer.hasValue())
    {
        return writer.status();
    }
    std::uint64_t written = 0u;
    for (;;)
    {
        // One admitted step per frame, so the session's other work runs between frames.
        an::Result<an::consumer::OperationScope> scope = admit();
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        if (!read.hasValue())
        {
            return read.status();
        }
        if (!read->has_value())
        {
            break;
        }
        an::consumer::AnyImage frame = std::move((*read)->second);
        const bool mosaic = std::visit(
            [](const auto& typed) {
                const std::optional<an::ImageDescriptor> stored = typed.descriptor();
                return stored.has_value() && stored->isMosaiced();
            },
            frame);
        if (request.debayerCfa && mosaic)
        {
            an::Result<an::consumer::AnyImage> colour =
                demosaicViewFrame(scope->engine(), scope->execution(), frame,
                                  request.sequence.demosaic.value_or(an::DemosaicMethod::Rcd));
            if (!colour.hasValue())
            {
                return colour.status();
            }
            frame = std::move(*colour);
        }
        if (request.autostretch)
        {
            an::Result<an::consumer::AnyImage> stretched =
                autostretchFrame(scope->engine(), scope->execution(), frame, nullptr, request.stretch);
            if (!stretched.hasValue())
            {
                return stretched.status();
            }
            frame = std::move(*stretched);
        }
        if (request.resize)
        {
            an::Result<an::consumer::AnyImage> fitted = fitFrame(scope->engine(), scope->execution(), std::move(frame),
                                                                 request.width, request.height, request.fillMode);
            if (!fitted.hasValue())
            {
                return fitted.status();
            }
            frame = std::move(*fitted);
        }
        if (an::Status appended = writer->append(scope->engine(), scope->execution(), frame); appended.failed())
        {
            return appended;
        }
        ++written;
        if (progress)
        {
            progress(written, total);
        }
    }
    an::Result<an::consumer::OperationScope> scope = admit();
    if (!scope.hasValue())
    {
        return scope.status();
    }
    an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    if (!published.hasValue())
    {
        return published.status();
    }
    LOG_INFO("Export Sequence: wrote '{}' ({} frames)", request.outputPath.toStdString(), written);
    return an::Status::success();
}

} // namespace an::ui

ExportSequenceControl::ExportSequenceControl(QWidget* parent, an::ui::DocumentSequence sequence,
                                             std::uint32_t frameWidth, std::uint32_t frameHeight, bool mosaic,
                                             const AutostretchParams& autostretchParams)
    : QDialog(parent), m_Sequence(std::move(sequence)), m_FrameWidth(frameWidth), m_FrameHeight(frameHeight),
      m_Mosaic(mosaic), m_Stretch(stretchOf(autostretchParams))
{
    m_ui = an::ui::controls::buildExportSequenceControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &ExportSequenceControl::on_dialog_box_button_clicked);
    QObject::connect(m_ui.outputFileBrowser, &QPushButton::released, this,
                     &ExportSequenceControl::on_outputFileBrowserButtonPushed);
    QObject::connect(m_ui.outputFormatSelector, &QComboBox::currentTextChanged, this,
                     &ExportSequenceControl::on_outputFormatSelector_currentTextChanged);
    QObject::connect(m_ui.codecSelector, &QComboBox::currentTextChanged, this,
                     &ExportSequenceControl::on_codecSelector_currentTextChanged);
    QObject::connect(m_ui.resolutionSelector, &QComboBox::currentTextChanged, this,
                     &ExportSequenceControl::on_resolutionSelector_currentTextChanged);
    QObject::connect(m_ui.widthValue, &QSpinBox::valueChanged, this, &ExportSequenceControl::on_widthValueChanged);
    QObject::connect(m_ui.heightValue, &QSpinBox::valueChanged, this, &ExportSequenceControl::on_heightValueChanged);
    QObject::connect(m_ui.outputFileName, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty() && !m_ExportRunning);
    });

    setWindowTitle(QStringLiteral("Export Sequence"));

    initialize();
}

ExportSequenceControl::~ExportSequenceControl()
{
    m_Stop.request_stop();
}

ExportSequenceControl* ExportSequenceControl::openFor(ImageViewControl& view)
{
    const std::optional<an::ui::DocumentSequence> sequence = view.document().sequence();
    const std::optional<an::ImageDescriptor> shown = view.imageDescriptor();
    if (!sequence.has_value() || !shown.has_value())
    {
        return nullptr;
    }
    std::uint32_t width = shown->extent().width();
    std::uint32_t height = shown->extent().height();
    bool mosaic = shown->isMosaiced();
    if (sequence->descriptor.fixedFrame.has_value())
    {
        if (const std::optional<an::ImageDescriptor> frame = sequence->descriptor.fixedFrame->toImageDescriptor();
            frame.has_value())
        {
            width = frame->extent().width();
            height = frame->extent().height();
            mosaic = frame->isMosaiced();
        }
    }
    auto* window = new ExportSequenceControl(&view, *sequence, width, height, mosaic, view.getAutostretchSettings());
    window->show();
    return window;
}

an::ui::controls::ExportSequenceControlUiElements& ExportSequenceControl::ui()
{
    return m_ui;
}

bool ExportSequenceControl::isExportRunning() const
{
    return m_ExportRunning;
}

void ExportSequenceControl::startExport()
{
    if (m_ExportRunning)
    {
        return;
    }
    const an::ui::ExportSequenceRequest request = buildExportRequest();
    if (request.outputPath.isEmpty())
    {
        return;
    }
    const an::consumer::EngineSession* session = currentSession();
    if (session == nullptr)
    {
        completeExport(refusal(an::ErrorCode::NotInitialized, "no engine session is lent"));
        return;
    }

    auto* progress = new ProgressDialogControl(this, tr("Exporting Sequence..."));
    progress->setProgressBarRange(0, 100);
    progress->setValue(0);
    progress->setText(QStringLiteral("Exporting sequence '%1'...").arg(request.outputPath));
    progress->show();
    m_Progress = progress;
    setExportRunning(true);

    QPointer<ExportSequenceControl> guard(this);
    QPointer<ProgressDialogControl> dialog(progress);
    auto shared = std::make_shared<const an::ui::ExportSequenceRequest>(request);
    QThreadPool::globalInstance()->start(
        [session, shared, guard, dialog, windowStop = m_Stop.get_token(), cancel = progress->stopToken()]() {
            const LinkedStop runStop(windowStop, cancel);
            const an::Status outcome = an::ui::exportSequence(
                *session, *shared, runStop.token(), [dialog](std::uint64_t written, std::uint64_t total) {
                    const int percent = total > 0u ? static_cast<int>((written * 100u) / total) : 0;
                    postToGui([dialog, percent]() {
                        if (dialog)
                        {
                            dialog->setValue(percent);
                        }
                    });
                });
            postToGui([guard, dialog, outcome]() {
                if (dialog)
                {
                    dialog->closeAsync();
                }
                if (guard)
                {
                    guard->completeExport(outcome);
                }
            });
        });
}

void ExportSequenceControl::on_dialog_box_button_clicked(QAbstractButton* button)
{
    if (m_ExportRunning)
    {
        return;
    }
    auto* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    const QDialogButtonBox::StandardButton standard = buttonBox->standardButton(button);
    if (standard == QDialogButtonBox::Apply || standard == QDialogButtonBox::Ok)
    {
        startExport();
    }
    else if (standard == QDialogButtonBox::Reset)
    {
        m_ui.resolutionSelector->setCurrentText(kPreserveRatio);
        m_ui.widthValue->setValue(static_cast<int>(m_FrameWidth));
        m_ui.heightValue->setValue(static_cast<int>(m_FrameHeight));
        m_ui.resizeGroupBox->setChecked(false);
    }
    else if (standard == QDialogButtonBox::Close || standard == QDialogButtonBox::Cancel)
    {
        close();
    }
}

void ExportSequenceControl::on_outputFileBrowserButtonPushed()
{
    const QString currentPath = m_ui.outputFileName->text().trimmed();
    QString initialPath = currentPath;
    if (!currentPath.isEmpty())
    {
        const QString parentDir = QFileInfo(currentPath).absolutePath();
        if (parentDir.isEmpty() || !QDir(parentDir).exists())
        {
            initialPath.clear();
        }
    }
    auto* context = an::ui::appContext();
    an::ui::IPathMemoryService* pathMemory = context != nullptr ? context->getPathMemoryService() : nullptr;
    if (initialPath.isEmpty() && pathMemory != nullptr)
    {
        initialPath = pathMemory->selectPath(QStringLiteral("__EXPORT_SEQUENCE_CONTROL__"));
    }

    const an::ui::ExportSequenceRequest current = buildExportRequest();
    QString filter;
    switch (current.format)
    {
    case an::ui::SequenceExportFormat::Ser:
        filter = tr("SER files (*.ser)");
        break;
    case an::ui::SequenceExportFormat::RawAvi:
        filter = tr("AVI Files (*.avi)");
        break;
    case an::ui::SequenceExportFormat::Video:
        filter =
            current.codec == an::ui::SequenceExportCodec::Vp9 ? tr("WebM Files (*.webm)") : tr("MP4 Files (*.mp4)");
        break;
    }

    const QString selectedFile = an::ui::dialogs::getSaveFileName(
        context != nullptr ? context->getGlobalSettings() : nullptr, this, tr("Save Sequence..."), initialPath, filter);
    if (!selectedFile.isEmpty())
    {
        if (pathMemory != nullptr)
        {
            pathMemory->upsertPath(QStringLiteral("__EXPORT_SEQUENCE_CONTROL__"),
                                   QFileInfo(selectedFile).absolutePath());
        }
        m_ui.outputFileName->setText(an::ui::normalizeSequenceExportPath(
            selectedFile, an::ui::sequenceExportSuffix(current.format, current.codec)));
    }
}

void ExportSequenceControl::on_outputFormatSelector_currentTextChanged(const QString& text)
{
    m_ui.codecSelector->setEnabled(text == kVideoFormat);
    if (m_ui.outputFileName->text().trimmed().isEmpty())
    {
        return;
    }
    const an::ui::ExportSequenceRequest current = buildExportRequest();
    m_ui.outputFileName->setText(an::ui::normalizeSequenceExportPath(
        m_ui.outputFileName->text(), an::ui::sequenceExportSuffix(current.format, current.codec)));
}

void ExportSequenceControl::on_codecSelector_currentTextChanged(const QString& text)
{
    Q_UNUSED(text);
    if (m_ui.outputFormatSelector->currentText() != kVideoFormat || m_ui.outputFileName->text().trimmed().isEmpty())
    {
        return;
    }
    const an::ui::ExportSequenceRequest current = buildExportRequest();
    m_ui.outputFileName->setText(an::ui::normalizeSequenceExportPath(
        m_ui.outputFileName->text(), an::ui::sequenceExportSuffix(current.format, current.codec)));
}

void ExportSequenceControl::on_resolutionSelector_currentTextChanged(const QString& text)
{
    struct Preset
    {
        const char* name;
        int width;
        int height;
    };
    static constexpr Preset kPresets[] = {{"Full HD", 1920, 1080}, {"4K", 3840, 2160}, {"8K", 7680, 4320}};
    for (const Preset& preset : kPresets)
    {
        if (text == QLatin1String(preset.name))
        {
            m_ui.fillMode->setEnabled(true);
            m_ui.widthValue->setValue(preset.width);
            m_ui.heightValue->setValue(preset.height);
            m_ui.widthValue->setEnabled(false);
            m_ui.heightValue->setEnabled(false);
            return;
        }
    }
    // Preserve Ratio and Free Ratio start from the frame's own size.
    m_ui.fillMode->setEnabled(false);
    m_ui.widthValue->setEnabled(true);
    m_ui.heightValue->setEnabled(true);
    m_ui.widthValue->setValue(static_cast<int>(m_FrameWidth));
    m_ui.heightValue->setValue(static_cast<int>(m_FrameHeight));
}

void ExportSequenceControl::on_widthValueChanged(int value)
{
    if (m_ui.resolutionSelector->currentText() == kPreserveRatio && m_FrameWidth > 0u)
    {
        const double percent = (static_cast<double>(value) * 100.0) / static_cast<double>(m_FrameWidth);
        m_ui.heightValue->blockSignals(true);
        m_ui.heightValue->setValue(static_cast<int>((static_cast<double>(m_FrameHeight) * percent) / 100.0));
        m_ui.heightValue->blockSignals(false);
    }
}

void ExportSequenceControl::on_heightValueChanged(int value)
{
    if (m_ui.resolutionSelector->currentText() == kPreserveRatio && m_FrameHeight > 0u)
    {
        const double percent = (static_cast<double>(value) * 100.0) / static_cast<double>(m_FrameHeight);
        m_ui.widthValue->blockSignals(true);
        m_ui.widthValue->setValue(static_cast<int>((static_cast<double>(m_FrameWidth) * percent) / 100.0));
        m_ui.widthValue->blockSignals(false);
    }
}

void ExportSequenceControl::done(int r)
{
    if (m_ExportRunning)
    {
        return;
    }

    json doc;
    doc["version"] = 1;
    doc["params"]["format"] = m_ui.outputFormatSelector->currentText().toStdString();
    doc["params"]["codec"] = m_ui.codecSelector->currentText().toStdString();
    doc["params"]["resolution"] = m_ui.resolutionSelector->currentText().toStdString();
    if (auto* context = an::ui::appContext())
    {
        if (auto* processSettings = context->getProcessSettings())
        {
            processSettings->upsert(QStringLiteral("__EXPORT_SEQUENCE__"), 0, 0, 0, QString::fromStdString(doc.dump()));
        }
    }

    QDialog::done(r);
}

void ExportSequenceControl::closeEvent(QCloseEvent* event)
{
    if (m_ExportRunning)
    {
        // An export under way is stopped and the window closes when it lands: nothing is waited for.
        m_Stop.request_stop();
        if (m_Progress)
        {
            m_Progress->requestCancel();
        }
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

an::ui::ExportSequenceRequest ExportSequenceControl::buildExportRequest() const
{
    an::ui::ExportSequenceRequest request;
    request.sequence = m_Sequence;

    const QString format = m_ui.outputFormatSelector->currentText();
    if (format == kRawAviFormat)
    {
        request.format = an::ui::SequenceExportFormat::RawAvi;
    }
    else if (format == kVideoFormat)
    {
        request.format = an::ui::SequenceExportFormat::Video;
    }

    const QString codec = m_ui.codecSelector->currentText();
    if (codec == kH265Codec)
    {
        request.codec = an::ui::SequenceExportCodec::H265;
    }
    else if (codec == kVp9Codec)
    {
        request.codec = an::ui::SequenceExportCodec::Vp9;
    }

    request.outputPath = an::ui::normalizeSequenceExportPath(
        m_ui.outputFileName->text(), an::ui::sequenceExportSuffix(request.format, request.codec));
    request.fps = m_ui.fpsValue->value();
    request.debayerCfa = m_Mosaic && m_ui.debayerCfaCheckBox->isChecked();
    request.autostretch = m_ui.autoStretchCheckBox->isChecked();
    request.stretch = m_Stretch;
    request.resize = m_ui.resizeGroupBox->isChecked();
    request.width = static_cast<std::uint32_t>((std::max)(0, m_ui.widthValue->value()));
    request.height = static_cast<std::uint32_t>((std::max)(0, m_ui.heightValue->value()));

    const QString fillMode = m_ui.fillMode->currentText();
    if (fillMode == QStringLiteral("Fill"))
    {
        request.fillMode = an::ui::SequenceExportFill::Fill;
    }
    else if (fillMode == QStringLiteral("Stretch"))
    {
        request.fillMode = an::ui::SequenceExportFill::Stretch;
    }
    else if (fillMode == QStringLiteral("Center"))
    {
        request.fillMode = an::ui::SequenceExportFill::Center;
    }
    return request;
}

void ExportSequenceControl::setExportRunning(bool running)
{
    m_ExportRunning = running;
    m_ui.generalSettings->setEnabled(!running);
    m_ui.buttonBox->setEnabled(!running);
}

void ExportSequenceControl::completeExport(const an::Status& outcome)
{
    setExportRunning(false);
    m_Progress.clear();
    if (outcome.code() == an::ErrorCode::Cancelled)
    {
        if (m_Stop.stop_requested())
        {
            close();
        }
        return;
    }
    auto* dialogs = process_utils::currentDialogService();
    if (outcome.failed())
    {
        LOG_WARNING("Export Sequence: {}", outcome.message());
        if (dialogs != nullptr)
        {
            dialogs->notifyErrorMessageAsync(
                this, tr("Export Sequence Error"),
                tr("Could not export sequence:\n%1").arg(QString::fromStdString(outcome.message())));
        }
        return;
    }
    if (dialogs != nullptr)
    {
        dialogs->notifyInfoMessageAsync(this, tr("Export Sequence"), tr("Sequence exported successfully!"));
    }
    accept();
}

void ExportSequenceControl::initialize()
{
    m_ui.outputFormatSelector->addItem(kSerFormat);
    m_ui.outputFormatSelector->addItem(kRawAviFormat);
    m_ui.outputFormatSelector->addItem(kVideoFormat);

    m_ui.codecSelector->addItem(kH264Codec);
    m_ui.codecSelector->addItem(kH265Codec);
    m_ui.codecSelector->addItem(kVp9Codec);

    m_ui.resolutionSelector->addItem(kPreserveRatio);
    m_ui.resolutionSelector->addItem(kFreeRatio);
    m_ui.resolutionSelector->addItem(QStringLiteral("Full HD"));
    m_ui.resolutionSelector->addItem(QStringLiteral("4K"));
    m_ui.resolutionSelector->addItem(QStringLiteral("8K"));

    m_ui.fillMode->addItem(QStringLiteral("Fit"));
    m_ui.fillMode->addItem(QStringLiteral("Fill"));
    m_ui.fillMode->addItem(QStringLiteral("Stretch"));
    m_ui.fillMode->addItem(QStringLiteral("Center"));

    const int width = static_cast<int>((std::max)(m_FrameWidth, 2u));
    const int height = static_cast<int>((std::max)(m_FrameHeight, 2u));
    m_ui.widthValue->setRange(4, (std::max)(width * 2, 7680));
    m_ui.heightValue->setRange(4, (std::max)(height * 2, 4320));
    m_ui.widthValue->setValue(static_cast<int>(m_FrameWidth));
    m_ui.heightValue->setValue(static_cast<int>(m_FrameHeight));

    m_ui.outputFormatSelector->setCurrentText(kSerFormat);
    m_ui.codecSelector->setCurrentText(kH264Codec);
    m_ui.codecSelector->setEnabled(false);
    m_ui.resolutionSelector->setCurrentText(kPreserveRatio);
    m_ui.fillMode->setCurrentText(QStringLiteral("Fit"));
    m_ui.fillMode->setEnabled(false);

    m_ui.debayerCfaCheckBox->setVisible(m_Mosaic);
    m_ui.debayerCfaCheckBox->setEnabled(m_Mosaic);
    m_ui.debayerCfaCheckBox->setChecked(false);

    m_ui.fpsValue->setValue(m_Sequence.descriptor.nominalFrameRate.has_value()
                                ? an::ui::framesPerSecondOf(*m_Sequence.descriptor.nominalFrameRate)
                                : 1.0);

    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    int windowState = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    QString settings;
    auto* context = an::ui::appContext();
    auto* processSettings = context != nullptr ? context->getProcessSettings() : nullptr;
    if (processSettings != nullptr &&
        processSettings->select(QStringLiteral("__EXPORT_SEQUENCE__"), windowState, windowWidth, windowHeight,
                                settings) &&
        !settings.isEmpty())
    {
        try
        {
            const json doc = json::parse(settings.toStdString());
            const json& params = doc.at("params");
            m_ui.outputFormatSelector->setCurrentText(
                QString::fromStdString(params.value("format", kSerFormat.toStdString())));
            m_ui.codecSelector->setCurrentText(QString::fromStdString(params.value("codec", kH264Codec.toStdString())));
            m_ui.resolutionSelector->setCurrentText(
                QString::fromStdString(params.value("resolution", kPreserveRatio.toStdString())));
        }
        catch (const std::exception& exception)
        {
            LOG_ERROR("ExportSequenceControl: Could NOT deserialize settings: {} ({})", settings.toStdString(),
                      exception.what());
        }
    }
}
