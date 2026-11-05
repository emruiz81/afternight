// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MaskDrawingProcess.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/drawing/DrawingCanvasController.h"
#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingTypes.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "session/IEngineSessionProvider.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QCoreApplication>
#include <QPointer>
#include <QShortcut>
#include <QThreadPool>

#include <type_traits>
#include <utility>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// The session every Controls operation runs in, or null when the application has none.
[[nodiscard]] const an::consumer::EngineSession* currentSession()
{
    auto* context = an::ui::appContext();
    auto* provider = context != nullptr ? context->getEngineSessionProvider() : nullptr;
    return provider != nullptr && provider->engineSession().valid() ? &provider->engineSession() : nullptr;
}

/// What a failure says to a person.
[[nodiscard]] QString describeStatus(const an::Status& status)
{
    return QString::fromUtf8(status.message());
}

} // namespace

MaskDrawingProcess::MaskDrawingProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<MaskDrawingProcessParams>(control, getProcessNameStatic()),
      m_edits(std::make_unique<an::ui::drawing::DrawingEditQueue>(
          [this](const an::ui::drawing::DrawingEditReport&) { editsLanded(); }))
{
    m_ui = an::ui::processes::buildMaskDrawingProcessUi(this);
    setProcessDescription(
        QStringLiteral("Paint a grayscale mask directly on the RT preview pane and create it as a new mask image."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);
    setExecutionAutostretchSupported(true);

    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::toolChanged, this,
                     [this](const int toolId) {
                         if (m_canvasController)
                         {
                             m_canvasController->setTool(static_cast<an::ui::drawing::DrawingTool>(toolId));
                         }
                         refreshCanvas();
                     });

    auto syncBrushState = [this]() { syncCanvasBrushState(); };

    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::brushSizeChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::brushHardnessChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::brushOpacityChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::brushFlowChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::brushSpacingChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::stabilizationChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::tipShapeChanged, this,
                     [syncBrushState](const int) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::blendModeChanged, this,
                     [syncBrushState](const int) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::shapeTypeChanged, this,
                     [syncBrushState](const int) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::shapeFeatherChanged, this,
                     [syncBrushState](const double) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::shapeFilledChanged, this,
                     [syncBrushState](const bool) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::previewModeChanged, this,
                     [syncBrushState](const int) { syncBrushState(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::importRequested, this,
                     [this]() { importMask(); });
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::exportRequested, this,
                     [this]() { exportMask(); });

    // The window's own edits land after the strokes before them.
    auto undo = [this]() {
        submitDrawingEdit([](an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution) {
            return document.canUndo() ? document.undo(execution) : an::Status::success();
        });
    };
    auto redo = [this]() {
        submitDrawingEdit([](an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution) {
            return document.canRedo() ? document.redo(execution) : an::Status::success();
        });
    };
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::undoRequested, this, undo);
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::redoRequested, this, redo);
    QObject::connect(m_ui.drawingToolbar, &an::ui::drawing::DrawingToolbar::clearRequested, this, [this]() {
        submitDrawingEdit([](an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution) {
            return document.clear(execution);
        });
    });

    auto* decreaseBrushSize =
        new QShortcut(QKeySequence(Qt::Key_BracketLeft), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(decreaseBrushSize, &QShortcut::activated, this, [this]() {
        if (m_ui.drawingToolbar)
        {
            m_ui.drawingToolbar->stepBrushSize(-1);
        }
    });

    auto* increaseBrushSize =
        new QShortcut(QKeySequence(Qt::Key_BracketRight), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(increaseBrushSize, &QShortcut::activated, this, [this]() {
        if (m_ui.drawingToolbar)
        {
            m_ui.drawingToolbar->stepBrushSize(1);
        }
    });

    auto* toggleBrushTool =
        new QShortcut(QKeySequence(Qt::Key_X), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(toggleBrushTool, &QShortcut::activated, this, [this]() {
        if (m_ui.drawingToolbar)
        {
            m_ui.drawingToolbar->toggleTool();
        }
    });

    auto* activateFillTool =
        new QShortcut(QKeySequence(Qt::Key_F), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(activateFillTool, &QShortcut::activated, this, [this]() {
        if (m_ui.drawingToolbar)
        {
            m_ui.drawingToolbar->setActiveTool(an::ui::drawing::DrawingTool::FloodFill);
            syncCanvasBrushState();
            refreshCanvas();
        }
    });

    auto* activateGradientTool =
        new QShortcut(QKeySequence(Qt::Key_G), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(activateGradientTool, &QShortcut::activated, this, [this]() {
        if (m_ui.drawingToolbar)
        {
            m_ui.drawingToolbar->setActiveTool(an::ui::drawing::DrawingTool::GradientFill);
            syncCanvasBrushState();
            refreshCanvas();
        }
    });

    auto* deleteSelectedShape =
        new QShortcut(QKeySequence(Qt::Key_Delete), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(deleteSelectedShape, &QShortcut::activated, this, [this]() {
        if (m_canvasController)
        {
            m_canvasController->removeSelectedShape();
        }
    });

    auto* undoShortcut = new QShortcut(QKeySequence::Undo, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(undoShortcut, &QShortcut::activated, this, undo);

    auto* redoShortcut = new QShortcut(QKeySequence::Redo, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    QObject::connect(redoShortcut, &QShortcut::activated, this, redo);

    m_SupportMasking = false;
    m_ShowWaitingSpinner = false;
    m_CreatedViewControlsAreNeighbour = true;

    updateWindowTitle();
    setupPreviewControl(false);
    syncDocumentToTargetImage();
    syncUndoRedoState();
    refreshTheme();
}

MaskDrawingProcess::~MaskDrawingProcess() = default;

std::string MaskDrawingProcess::getProcessNameStatic()
{
    return "Mask Drawing";
}

bool MaskDrawingProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void MaskDrawingProcess::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    if (ctrl == m_image_view_control)
    {
        syncDocumentToTargetImage();
    }
}

void MaskDrawingProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    if (ctrl == m_image_view_control)
    {
        syncDocumentToTargetImage();
    }
}

void MaskDrawingProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    syncDocumentToTargetImage();
}

void MaskDrawingProcess::onAlternateImageEnabled(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onAlternateImageEnabled(ctrl);
    if (ctrl == m_rt_preview_view_control && m_canvasController)
    {
        m_canvasController->setOverlayVisible(false);
    }
}

void MaskDrawingProcess::onAlternateImageDisabled(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onAlternateImageDisabled(ctrl);
    if (ctrl == m_rt_preview_view_control && m_canvasController)
    {
        m_canvasController->setOverlayVisible(true);
    }
}

const std::shared_ptr<an::ui::drawing::DrawingDocument>& MaskDrawingProcess::document() const noexcept
{
    return m_document;
}

std::size_t MaskDrawingProcess::pendingEdits() const noexcept
{
    return m_edits->pending() + (m_canvasController ? m_canvasController->pendingEdits() : 0u);
}

bool MaskDrawingProcess::isTransferring() const noexcept
{
    return m_transfers > 0;
}

bool MaskDrawingProcess::isPreviewEnabled() const
{
    return m_ui.preview && m_ui.preview->isChecked();
}

void MaskDrawingProcess::enablePreview(const bool enabled)
{
    if (m_ui.preview)
    {
        m_ui.preview->setChecked(enabled);
    }
}

an::Status MaskDrawingProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                       an::ui::ProcessDestination& destination,
                                                       const an::ui::ProcessRun& run,
                                                       const MaskDrawingProcessParams& params,
                                                       an::ui::ExecutionScope& scope)
{
    // The preview shows the target, which the drawing is painted over.
    if (run.isForPreview())
    {
        return an::Status::success();
    }
    if (params.snapshot.failed())
    {
        return params.snapshot;
    }
    if (!params.outputMask)
    {
        return refusal(an::ErrorCode::NotInitialized, "there is no drawing to create a mask from");
    }
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    const an::EngineContext& engine = scope.engine();
    const an::ExecutionContext& execution = scope.execution();

    // The mask, a grey image of the target's representation holding the drawing's coverages.
    const an::Result<an::ImageDescriptor> maskDescribed =
        process_utils::derivedImageDescriptor(*described, 1u, described->sampleType(), engine.limits());
    if (!maskDescribed.hasValue())
    {
        return maskDescribed.status();
    }
    an::Result<an::consumer::AssetReadLease> drawn = params.outputMask->read(execution.stopToken());
    if (!drawn.hasValue())
    {
        return drawn.status();
    }
    const std::optional<an::ImageDescriptor> drawnDescribed = params.outputMask->descriptor();
    if (!drawnDescribed.has_value() || drawnDescribed->extent().width() != described->extent().width() ||
        drawnDescribed->extent().height() != described->extent().height())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the drawing is not the size of the image");
    }
    an::Result<an::ImageMetadata> metadata = process_utils::derivedImageMetadata(source.metadata(), *maskDescribed, "");
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    an::Result<an::consumer::ImageAsset> mask =
        process_utils::createDerivedImage(scope, *maskDescribed, std::move(*metadata));
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        an::Result<an::consumer::AssetWriteLease> lease = mask->write();
        if (!lease.hasValue())
        {
            return lease.status();
        }
        const an::Status written = lease->visitWrite([&](auto&& access) -> an::Status {
            const an::ImageLayout layout = access.view().layout();
            an::Result<process_utils::NormalizedSamples> output = process_utils::NormalizedSamples::open(
                engine, execution, *maskDescribed, access.view().data(), layout, an::KernelValidityView{});
            if (!output.hasValue())
            {
                return output.status();
            }
            const an::Status copied = drawn->visit([&](const auto& typed) -> an::Status {
                using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                const auto view = typed.view();
                if (!view.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the drawing holds nothing");
                }
                an::ConversionCounters counters;
                return an::convertImageSamples(execution, an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                                               output->sampleType(), output->samples(), output->layout(),
                                               an::ConversionPolicy{}, counters);
            });
            if (copied.failed())
            {
                return copied;
            }
            return output->commit(execution);
        });
        if (written.failed())
        {
            return written;
        }
    }
    if (an::Status replaced = destination.replace(std::move(*mask)); replaced.failed())
    {
        return replaced;
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(
        "an::ui::drawing::DrawingDocument::createOutputMask", describeTypedProcessStep(params).description);
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return destination.asset().transition(an::MetadataTransition{.record = *entry});
}

an::ui::ProcessStep MaskDrawingProcess::describeTypedProcessStep(const MaskDrawingProcessParams&) const
{
    return an::ui::ProcessStep{.name = getProcessName(), .description = "Create painted grayscale mask"};
}

MaskDrawingProcessParams MaskDrawingProcess::getParams() const
{
    MaskDrawingProcessParams params;
    if (!m_document)
    {
        return params;
    }
    // A copy is taken once per change of the drawing, and every run until the next change shares it.
    if (m_outputMask == nullptr || m_outputDocument != m_document.get() || m_outputRevision != m_document->revision())
    {
        an::Result<an::consumer::ImageAsset> copied = m_document->createOutputMask();
        if (!copied.hasValue())
        {
            params.snapshot = copied.status();
            return params;
        }
        m_outputMask = std::make_shared<const an::consumer::ImageAsset>(std::move(*copied));
        m_outputDocument = m_document.get();
        m_outputRevision = m_document->revision();
    }
    params.outputMask = m_outputMask;
    return params;
}

void MaskDrawingProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
    refreshCanvas();
}

void MaskDrawingProcess::resetSettings()
{
    an::ui::drawing::BrushParams defaultBrush;
    defaultBrush.size = 24.0f;
    defaultBrush.hardness = 1.0f;
    defaultBrush.opacity = 1.0f;
    defaultBrush.flow = 1.0f;
    defaultBrush.spacing = 0.25f;
    defaultBrush.stabilization = 0.0f;
    defaultBrush.tipShape = an::ui::drawing::BrushParams::TipShape::Round;
    defaultBrush.blendMode = an::ui::drawing::BrushParams::BlendMode::Normal;

    m_ui.drawingToolbar->setActiveTool(an::ui::drawing::DrawingTool::Brush);
    m_ui.drawingToolbar->setBrushParams(defaultBrush);
    m_ui.drawingToolbar->setPreviewMode(an::ui::drawing::MaskPreviewMode::Red);
    m_ui.drawingToolbar->setShapeType(an::ui::drawing::ShapeObject::Type::Rectangle);
    m_ui.drawingToolbar->setShapeFeather(0.0f);
    m_ui.drawingToolbar->setShapeFilled(true);

    replaceDocument();
    syncCanvasBrushState();
}

void MaskDrawingProcess::settingsUpdatedImpl() {}

void MaskDrawingProcess::enableProcessControlsImpl(const bool enabled)
{
    if (auto* okButton = m_ui.buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setEnabled(enabled);
    }
    if (auto* cancelButton = m_ui.buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setEnabled(enabled);
    }
    if (auto* resetButton = m_ui.buttonBox->button(QDialogButtonBox::Reset))
    {
        resetButton->setEnabled(enabled);
    }

    if (m_ui.drawingToolbar)
    {
        m_ui.drawingToolbar->setEnabled(enabled);
    }

    if (m_canvasController)
    {
        m_canvasController->setListenerEnabled(enabled);
    }
}

void MaskDrawingProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                                     std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = false;
    newImageName = m_image_view_control->getUniqueName() + " - MASK";
}

void MaskDrawingProcess::previewControlStarted()
{
    syncDocumentToTargetImage();

    if (m_canvasController)
    {
        m_canvasController->destroy();
        m_canvasController.reset();
    }

    if (!m_rt_preview_view_control || !m_document)
    {
        return;
    }

    m_canvasController = std::make_unique<an::ui::drawing::DrawingCanvasController>(
        m_rt_preview_view_control->getScrollArea(), m_document);
    m_canvasController->setStrokeCommittedCallback([this]() { syncUndoRedoState(); });
    m_canvasController->setListenerEnabled(true);
    m_canvasController->setOverlayVisible(
        !(m_rt_preview_view_control->getViewWidget() &&
          m_rt_preview_view_control->getViewWidget()->isAlternateImageRenderEnabled()));
    syncCanvasBrushState();
    refreshCanvas();
}

void MaskDrawingProcess::previewControlClosed()
{
    if (m_canvasController)
    {
        m_canvasController->destroy();
        m_canvasController.reset();
    }
}

void MaskDrawingProcess::replaceDocument()
{
    const an::consumer::EngineSession* session = currentSession();
    const std::optional<an::ImageDescriptor> described =
        m_image_view_control != nullptr ? m_image_view_control->imageDescriptor() : std::nullopt;
    m_document.reset();
    if (session != nullptr && described.has_value())
    {
        an::Result<std::shared_ptr<an::ui::drawing::DrawingDocument>> created =
            an::ui::drawing::DrawingDocument::create(*session, static_cast<int>(described->extent().width()),
                                                     static_cast<int>(described->extent().height()));
        if (created.hasValue())
        {
            m_document = std::move(*created);
        }
        else
        {
            LOG_WARNING("Mask Drawing: no drawing could be made for '{}': {}", m_image_view_control->getUniqueName(),
                        created.status().message());
            notifyErrorMessageAsync(QString::fromStdString(getProcessNameStatic()), describeStatus(created.status()));
        }
    }

    m_edits->setDocument(m_document);
    if (m_canvasController)
    {
        m_canvasController->setDocument(m_document);
    }
    syncUndoRedoState();
    refreshCanvas();
}

void MaskDrawingProcess::syncDocumentToTargetImage()
{
    const std::optional<an::ImageDescriptor> described =
        m_image_view_control != nullptr ? m_image_view_control->imageDescriptor() : std::nullopt;
    if (!described.has_value())
    {
        return;
    }
    if (!m_document || m_document->width() != static_cast<int>(described->extent().width()) ||
        m_document->height() != static_cast<int>(described->extent().height()))
    {
        replaceDocument();
    }
}

void MaskDrawingProcess::syncCanvasBrushState()
{
    if (!m_canvasController || !m_ui.drawingToolbar)
    {
        return;
    }

    m_canvasController->setTool(m_ui.drawingToolbar->activeTool());
    m_canvasController->setBrushParams(m_ui.drawingToolbar->currentBrushParams());
    m_canvasController->setPreviewMode(m_ui.drawingToolbar->currentPreviewMode());
    m_canvasController->setShapeType(m_ui.drawingToolbar->currentShapeType());
    m_canvasController->setShapeFeather(m_ui.drawingToolbar->shapeFeather());
    m_canvasController->setShapeFilled(m_ui.drawingToolbar->isShapeFilled());
}

void MaskDrawingProcess::syncUndoRedoState()
{
    if (!m_ui.drawingToolbar)
    {
        return;
    }

    m_ui.drawingToolbar->setUndoEnabled(m_document && m_document->canUndo());
    m_ui.drawingToolbar->setRedoEnabled(m_document && m_document->canRedo());
}

void MaskDrawingProcess::refreshCanvas()
{
    if (m_canvasController)
    {
        m_canvasController->refreshView();
    }
}

void MaskDrawingProcess::submitDrawingEdit(an::ui::drawing::DrawingEdit edit)
{
    if (!m_document)
    {
        return;
    }
    if (m_canvasController)
    {
        m_canvasController->submitEdit(std::move(edit));
    }
    else
    {
        m_edits->submit(std::move(edit));
    }
}

void MaskDrawingProcess::editsLanded()
{
    syncUndoRedoState();
    refreshCanvas();
    if (const an::Status& failure = m_edits->lastFailure(); failure.failed())
    {
        LOG_WARNING("Mask Drawing: an edit was refused: {}", failure.message());
    }
}

void MaskDrawingProcess::importMask()
{
    if (!m_document)
    {
        notifyErrorMessageAsync(tr("Import Mask"), tr("Open a target image before importing a mask."));
        return;
    }

    static const QString contextKey = QStringLiteral("__MASK_DRAWING_BROWSE__");
    const auto selectedFiles =
        process_utils::selectFiles(this, tr("Import Mask"), contextKey,
                                   {tr("Mask Images (*.fit *.fits *.tif *.tiff *.png *.xisf)"), tr("All Files (*.*)")});
    const QString path = selectedFiles.isEmpty() ? QString() : selectedFiles.front();
    if (!path.trimmed().isEmpty())
    {
        importMaskFrom(path);
    }
}

void MaskDrawingProcess::importMaskFrom(const QString& path)
{
    const an::consumer::EngineSession* session = currentSession();
    if (!m_document || session == nullptr)
    {
        notifyErrorMessageAsync(tr("Import Mask"), tr("Open a target image before importing a mask."));
        return;
    }

    // The file is read on a worker; the import lands on the GUI thread, after the edits before it.
    ++m_transfers;
    QPointer<MaskDrawingProcess> guard(this);
    const std::weak_ptr<an::ui::drawing::DrawingDocument> document = m_document;
    // The window's stop reaches the read: at the application's exit a file half read is not waited for.
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, path, guard, document, stop]() {
        auto read = std::make_shared<an::Result<an::ui::ViewReadResult>>(
            an::ui::readViewImage(*session, an::ui::ViewReadRequest{.path = path}, stop, nullptr));
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, document, read, path]() {
                if (!guard)
                {
                    return;
                }
                --guard->m_transfers;
                if (!read->hasValue())
                {
                    LOG_ERROR("MaskDrawingProcess: failed to load imported mask '{}': {}", path.toStdString(),
                              read->status().message());
                    guard->notifyErrorMessageAsync(
                        tr("Import Mask"),
                        tr("Failed to import the selected mask.\n%1").arg(describeStatus(read->status())));
                    return;
                }
                const std::shared_ptr<an::ui::drawing::DrawingDocument> current = document.lock();
                if (!current || current != guard->m_document)
                {
                    return;
                }
                const std::optional<an::ImageDescriptor> described = (*read)->asset.descriptor();
                if (!described.has_value() || static_cast<int>(described->extent().width()) != current->width() ||
                    static_cast<int>(described->extent().height()) != current->height())
                {
                    guard->notifyErrorMessageAsync(tr("Import Mask"),
                                                   tr("The imported mask must match the target image size (%1 × %2).")
                                                       .arg(current->width())
                                                       .arg(current->height()));
                    return;
                }
                auto imported = std::make_shared<an::consumer::ImageAsset>(std::move((*read)->asset));
                guard->submitDrawingEdit([imported](an::ui::drawing::DrawingDocument& drawing,
                                                    const an::ExecutionContext& execution) -> an::Status {
                    an::Result<an::consumer::AssetReadLease> lease = imported->read();
                    if (!lease.hasValue())
                    {
                        return lease.status();
                    }
                    return drawing.importMask(execution, *lease);
                });
            },
            Qt::QueuedConnection);
    });
}

void MaskDrawingProcess::exportMask()
{
    if (!m_document)
    {
        notifyErrorMessageAsync(tr("Export Mask"), tr("There is no mask to export yet."));
        return;
    }

    static const QString contextKey = QStringLiteral("__MASK_DRAWING_BROWSE__");
    const auto selectedFiles =
        process_utils::saveFiles(this, tr("Export Mask"), QString(), contextKey,
                                 {tr("Mask Images (*.fit *.fits *.tif *.tiff *.png *.xisf)"), tr("All Files (*.*)")});
    const QString path = selectedFiles.isEmpty() ? QString() : selectedFiles.front();
    if (!path.trimmed().isEmpty())
    {
        exportMaskTo(path);
    }
}

void MaskDrawingProcess::exportMaskTo(const QString& path)
{
    const an::consumer::EngineSession* session = currentSession();
    auto* context = an::ui::appContext();
    auto* intents = context != nullptr ? context->getOutputIntentProvider() : nullptr;
    if (!m_document || session == nullptr || intents == nullptr)
    {
        notifyErrorMessageAsync(tr("Export Mask"), tr("There is no mask to export yet."));
        return;
    }

    // The drawing as it is now is written on a worker, under the export settings.
    an::Result<an::consumer::ImageAsset> copied = m_document->createOutputMask();
    if (!copied.hasValue())
    {
        notifyErrorMessageAsync(tr("Export Mask"),
                                tr("Failed to export the current mask.\n%1").arg(describeStatus(copied.status())));
        return;
    }
    const std::optional<an::ImageDescriptor> described = copied->descriptor();
    an::Result<an::OutputIntent> intent =
        described.has_value()
            ? intents->buildOutputIntent(*described, an::codecFormatFromPath(path.toStdString()))
            : an::Result<an::OutputIntent>(refusal(an::ErrorCode::NotInitialized, "the drawing holds nothing"));
    if (!intent.hasValue())
    {
        notifyErrorMessageAsync(tr("Export Mask"),
                                tr("Failed to export the current mask.\n%1").arg(describeStatus(intent.status())));
        return;
    }

    ++m_transfers;
    QPointer<MaskDrawingProcess> guard(this);
    auto mask = std::make_shared<an::consumer::ImageAsset>(std::move(*copied));
    auto written = std::make_shared<an::OutputIntent>(std::move(*intent));
    // The window's stop reaches the write: at the application's exit a file half written is not waited for.
    const std::stop_token stop = windowStopToken();
    QThreadPool::globalInstance()->start([session, path, guard, mask, written, stop]() {
        const an::Status status = an::ui::writeViewImage(*session, *mask, path, *written, stop);
        QCoreApplication* application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [guard, status, path]() {
                if (!guard)
                {
                    return;
                }
                --guard->m_transfers;
                if (status.failed())
                {
                    LOG_ERROR("MaskDrawingProcess: export failed for '{}': {}", path.toStdString(), status.message());
                    guard->notifyErrorMessageAsync(
                        tr("Export Mask"), tr("Failed to export the current mask.\n%1").arg(describeStatus(status)));
                }
            },
            Qt::QueuedConnection);
    });
}
