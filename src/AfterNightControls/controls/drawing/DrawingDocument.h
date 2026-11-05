// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask a drawing window draws: a paint plane the brush, the fills and the gradients write, the vector shapes
/// screened over it, and the composite of the two published as an image asset of the session.
///
/// **The mask is an asset, and every change to it is written under its write lease.** The overlay a view paints and a
/// snapshot a process is handed read it under read leases; an edit takes the write lease first and is refused with
/// `ErrorCode::Busy` - changing nothing - while anything reads it, so a stroke never lands under a reader. Every
/// change publishes a new revision. `createOutputMask` is a snapshot: a new asset with an identity of its own that
/// later edits do not reach.
///
/// **Every number is the engine's.** A stroke's dabs are screened into a stroke plane by the brush
/// (`controls/drawing/BrushEngine.h`) and landed on the paint by `blendLayerPlane` against the values the paint held
/// before the stroke began, which the document keeps in a plane of their own; a fill or a gradient blends each pixel
/// with `evaluateLayerBlend`; a shape is the shape engine's feathered layer, kept per shape and screened over the
/// paint inside the rectangle that changed. A coverage sample that is not a number is read as none. Each edit runs
/// under the execution its caller was admitted, and the document holds no operation between edits.
///
/// **Undo keeps patches, and the patches are admitted.** A step holds the paint a change touched - the rectangle it
/// changed, before and after - and the shape list it replaced, never a copy of the whole paint for an edit that did not
/// change all of it; a shape edit holds no paint at all. A step's bytes are admitted against the session's
/// `MemoryCategory::HistoryCopies` allowance before the step is kept, older steps are dropped to make room first, and
/// an edit whose step still cannot be admitted is not made. At most `kMaxDrawingUndoDepth` steps are kept.
///
/// Not thread-safe: a drawing is edited on the GUI thread, and the lease is what keeps another thread's reader safe.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/drawing/DrawingTypes.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <QPoint>
#include <QPointF>
#include <QRect>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace an::ui::drawing
{

/// The most undo steps a document keeps; the oldest goes first.
inline constexpr std::size_t kMaxDrawingUndoDepth = 100u;

/// What a document holds and has done, for a person reading a diagnostic and for its tests.
struct DrawingDocumentCounters
{
    /// Shape layers rasterized and feathered since the document was made.
    std::uint64_t shapeLayersRendered = 0u;

    /// Undo and redo steps held now.
    std::uint64_t undoSteps = 0u;

    /// Paint bytes those steps hold, which is what they reserve against the undo allowance.
    std::uint64_t undoBytes = 0u;

    /// Coverage bytes the shape layers hold, the steps' included.
    std::uint64_t layerBytes = 0u;
};

class AFTERNIGHTCONTROLS_API DrawingDocument
{
public:
    /// A new, empty drawing of an image's size: three planes and the mask, all zero.
    ///
    /// @param session the session whose context the planes and the mask come from; it must outlive the document, as
    ///        the provider that lends it guarantees.
    /// @param width the image's width in pixels.
    /// @param height the image's height in pixels.
    /// @return the document; or `ErrorCode::InvalidArgument` for a size no image of the context can have; or what the
    ///         session or the context's memory refuses.
    [[nodiscard]] static an::Result<std::shared_ptr<DrawingDocument>> create(const an::consumer::EngineSession& session,
                                                                             int width, int height);

    ~DrawingDocument();
    DrawingDocument(const DrawingDocument&) = delete;
    DrawingDocument& operator=(const DrawingDocument&) = delete;
    DrawingDocument(DrawingDocument&&) = delete;
    DrawingDocument& operator=(DrawingDocument&&) = delete;

    /// The drawing's width in pixels.
    [[nodiscard]] int width() const noexcept;

    /// The drawing's height in pixels.
    [[nodiscard]] int height() const noexcept;

    /// The session the document's planes and mask belong to.
    [[nodiscard]] const an::consumer::EngineSession& session() const noexcept;

    /// The mask: a grey Float32 image of the drawing's size, every sample in the unit interval. Read it under a read
    /// lease; the document is the only writer.
    [[nodiscard]] const std::shared_ptr<an::consumer::ImageAsset>& mask() const noexcept;

    /// A copy of the mask as a new asset with an identity of its own, which the document's later edits do not change.
    ///
    /// @return the copy; or `ErrorCode::Busy` while the mask is being written; or what the copy refuses.
    [[nodiscard]] an::Result<an::consumer::ImageAsset> createOutputMask() const;

    /// Moves with every change to the mask.
    [[nodiscard]] std::uint64_t revision() const noexcept;

    /// The rectangle the last change to the mask touched.
    [[nodiscard]] QRect dirtyRect() const noexcept;

    /// Whether no sample of the mask is above zero.
    [[nodiscard]] bool isEmpty() const;

    /// Starts a stroke, ending the active one first. Erasing over shapes flattens them into the paint first, which the
    /// stroke's undo step restores.
    ///
    /// @param execution the operation the edit runs under.
    /// @param brush the brush.
    /// @return success; `ErrorCode::Busy` while the mask is read; or what ending the active stroke refuses; or what
    ///         `planBrush` refuses for the brush.
    [[nodiscard]] an::Status beginStroke(const an::ExecutionContext& execution, const BrushParams& brush);

    /// Adds a point to the active stroke and lands the dabs it places on the mask.
    ///
    /// @param execution the operation the edit runs under.
    /// @param imagePos where the point is, in image pixels.
    /// @param pressure how hard it was pressed; one for a device that reports none.
    /// @return success; `ErrorCode::InvalidState` with no stroke active; `ErrorCode::Busy` while the mask is read,
    ///         in which case the point is not taken; or what the brush or the blend refuses.
    [[nodiscard]] an::Status addStrokePoint(const an::ExecutionContext& execution, const QPointF& imagePos,
                                            float pressure = 1.0f);

    /// Ends the active stroke and keeps its undo step. Ending with no stroke active does nothing.
    ///
    /// @param execution the operation the edit runs under.
    /// @return success; `ErrorCode::Busy` while the mask is read, in which case the stroke stays active; or
    ///         `ErrorCode::MemoryBudgetExceeded` when the step cannot be admitted, in which case the stroke is taken
    ///         back.
    [[nodiscard]] an::Status endStroke(const an::ExecutionContext& execution);

    /// Whether a stroke is active.
    [[nodiscard]] bool hasActiveStroke() const noexcept;

    /// Replaces the paint with an image and removes every shape: a grey image's samples, or a colour image's Rec. 601
    /// luma, each read as a fraction of full scale and held in the unit interval, a sample that is not a number as
    /// none.
    ///
    /// @param execution the operation the edit runs under.
    /// @param source the image, read under the lease the caller holds; its size must be the drawing's.
    /// @return success; `ErrorCode::IncompatibleArguments` for an image of another size or of channels that are
    ///         neither grey nor colour; `ErrorCode::Busy` while the mask is read; or what the conversion or admitting
    ///         the step refuses. Nothing changes on a failure.
    [[nodiscard]] an::Status importMask(const an::ExecutionContext& execution,
                                        const an::consumer::AssetReadLease& source);

    /// Blends a value into the region of the mask connected to a pixel whose values are within a tolerance of its,
    /// flattening any shapes into the paint first.
    ///
    /// @param execution the operation the edit runs under.
    /// @param seedPoint the pixel.
    /// @param fillValue the value, held in the unit interval.
    /// @param tolerance how far a connected value may be from the seed's, held in the unit interval.
    /// @param blendMode how the value lands.
    /// @return success, also when nothing changes, which keeps no step; `ErrorCode::OutOfRange` for a pixel outside
    ///         the drawing; `ErrorCode::Busy` while the mask is read; or what admitting the step refuses.
    [[nodiscard]] an::Status floodFill(const an::ExecutionContext& execution, const QPoint& seedPoint, float fillValue,
                                       float tolerance = 0.05f,
                                       BrushParams::BlendMode blendMode = BrushParams::BlendMode::Normal);

    /// Blends a linear ramp across the whole mask, from one value at a point to another at a second, flattening any
    /// shapes into the paint first.
    ///
    /// @param execution the operation the edit runs under.
    /// @param startPoint where the ramp starts.
    /// @param endPoint where it ends; a point within a thousandth of a pixel of the start makes every pixel the end
    ///        value.
    /// @param startValue the value at the start, held in the unit interval.
    /// @param endValue the value at the end, held in the unit interval.
    /// @param blendMode how the ramp lands.
    /// @return success; `ErrorCode::Busy` while the mask is read; or what admitting the step refuses.
    [[nodiscard]] an::Status applyLinearGradient(const an::ExecutionContext& execution, const QPointF& startPoint,
                                                 const QPointF& endPoint, float startValue, float endValue,
                                                 BrushParams::BlendMode blendMode = BrushParams::BlendMode::Normal);

    /// Adds a shape on top of the others.
    ///
    /// @param execution the operation the edit runs under.
    /// @param shape the shape.
    /// @return the shape's index; `ErrorCode::Busy` while the mask is read; or what rendering its layer refuses.
    [[nodiscard]] an::Result<std::size_t> addShape(const an::ExecutionContext& execution, const ShapeObject& shape);

    /// Replaces a shape.
    ///
    /// @param execution the operation the edit runs under.
    /// @param index which shape.
    /// @param shape what it becomes.
    /// @return success; `ErrorCode::OutOfRange` for an index past the last shape; `ErrorCode::Busy` while the mask is
    ///         read; or what rendering its layer refuses.
    [[nodiscard]] an::Status updateShape(const an::ExecutionContext& execution, std::size_t index,
                                         const ShapeObject& shape);

    /// Removes a shape.
    ///
    /// @param execution the operation the edit runs under.
    /// @param index which shape.
    /// @return success; `ErrorCode::OutOfRange` for an index past the last shape; or `ErrorCode::Busy` while the mask
    ///         is read.
    [[nodiscard]] an::Status removeShape(const an::ExecutionContext& execution, std::size_t index);

    /// The shapes, bottom first.
    [[nodiscard]] const std::vector<ShapeObject>& shapes() const noexcept;

    /// The topmost shape a point hits.
    ///
    /// @param imagePos the point, in image pixels.
    /// @param tolerance how far outside a shape still hits it, in pixels.
    /// @return the shape's index, or no value.
    [[nodiscard]] std::optional<std::size_t> hitTestShape(const QPointF& imagePos, double tolerance = 0.0) const;

    /// Whether a step can be undone.
    [[nodiscard]] bool canUndo() const noexcept;

    /// Whether a step can be redone.
    [[nodiscard]] bool canRedo() const noexcept;

    /// Undoes the last step, ending the active stroke first.
    ///
    /// @param execution the operation the edit runs under.
    /// @return success, also with nothing to undo; or `ErrorCode::Busy` while the mask is read, in which case the step
    ///         stays where it was.
    [[nodiscard]] an::Status undo(const an::ExecutionContext& execution);

    /// Redoes the last undone step, ending the active stroke first.
    ///
    /// @param execution the operation the edit runs under.
    /// @return success, also with nothing to redo; or `ErrorCode::Busy` while the mask is read.
    [[nodiscard]] an::Status redo(const an::ExecutionContext& execution);

    /// Clears the paint and removes every shape, ending the active stroke first; an empty mask keeps no step.
    ///
    /// @param execution the operation the edit runs under.
    /// @return success; `ErrorCode::Busy` while the mask is read; or what admitting the step refuses.
    [[nodiscard]] an::Status clear(const an::ExecutionContext& execution);

    /// What the document holds and has done.
    [[nodiscard]] DrawingDocumentCounters counters() const noexcept;

private:
    struct Impl;

    explicit DrawingDocument(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::drawing
