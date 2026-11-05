// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "controls/drawing/DrawingEditQueue.h"
#include "processes/MaskDrawingProcessUiBuilder.h"

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <memory>
#include <optional>

class ImageViewControl;

namespace an::consumer
{
class ImageAsset;
} // namespace an::consumer

namespace an::ui::drawing
{
class DrawingCanvasController;
class DrawingDocument;
} // namespace an::ui::drawing

struct MaskDrawingProcessParams
{
    /// The mask an Apply creates, a copy of the drawing taken on the GUI thread when the run was asked for; null when
    /// there is no drawing.
    std::shared_ptr<const an::consumer::ImageAsset> outputMask;

    /// Why there is no copy, when the drawing could not be copied.
    an::Status snapshot = an::Status::success();
};

/// Mask Drawing: paints a grey mask on the preview with the drawing tools - brush, fill, gradient and shapes, each edit
/// landing through the canvas's queue under an operation of the session - and creates it as a new image in the
/// target's representation.
class AFTERNIGHTPROCESSES_API MaskDrawingProcess : public an::ui::TypedProcessControl<MaskDrawingProcessParams>
{
public:
    explicit MaskDrawingProcess(ImageViewControl* control);
    ~MaskDrawingProcess() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;
    void onImageUpdated(ImageViewControl* ctrl) override;
    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;
    void onAlternateImageEnabled(ImageViewControl* ctrl) override;
    void onAlternateImageDisabled(ImageViewControl* ctrl) override;

    /// The drawing, or null without a target.
    [[nodiscard]] const std::shared_ptr<an::ui::drawing::DrawingDocument>& document() const noexcept;

    /// How many edits are waiting for the session.
    [[nodiscard]] std::size_t pendingEdits() const noexcept;

    /// Replaces the drawing with an image file, read on a worker; the edit lands after the edits before it.
    ///
    /// @param path the file.
    void importMaskFrom(const QString& path);

    /// Writes the drawing to an image file on a worker, under the export settings.
    ///
    /// @param path the file; its extension names the container.
    void exportMaskTo(const QString& path);

    /// Whether an import or an export asked for is still on its way.
    [[nodiscard]] bool isTransferring() const noexcept;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const MaskDrawingProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const MaskDrawingProcessParams& params) const override;

private:
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    [[nodiscard]] MaskDrawingProcessParams getParams() const override;

    void refreshTheme() override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    void previewControlStarted() override;
    void previewControlClosed() override;

    void replaceDocument();
    void syncDocumentToTargetImage();
    void syncCanvasBrushState();
    void syncUndoRedoState();
    void refreshCanvas();
    void importMask();
    void exportMask();
    void submitDrawingEdit(an::ui::drawing::DrawingEdit edit);
    void editsLanded();

    std::shared_ptr<an::ui::drawing::DrawingDocument> m_document;
    std::unique_ptr<an::ui::drawing::DrawingCanvasController> m_canvasController;

    /// The queue the window's own edits land through while there is no canvas to take them.
    std::unique_ptr<an::ui::drawing::DrawingEditQueue> m_edits;

    /// The last copy of the drawing a run was handed, kept while the drawing has not changed since.
    mutable std::shared_ptr<const an::consumer::ImageAsset> m_outputMask;
    mutable const an::ui::drawing::DrawingDocument* m_outputDocument = nullptr;
    mutable std::uint64_t m_outputRevision = 0u;

    /// Imports and exports on their way.
    int m_transfers = 0;

    an::ui::processes::MaskDrawingProcessUiElements m_ui;
};
