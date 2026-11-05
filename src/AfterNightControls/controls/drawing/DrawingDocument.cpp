// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/drawing/DrawingDocument.h"

#include "controls/drawing/BrushEngine.h"
#include "controls/drawing/CoveragePlane.h"
#include "controls/drawing/ShapeEngine.h"
#include "controls/ProcessExecution.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Color/ColorTransform.h>
#include <AfterNightEngine/Algorithms/Pointwise/LayerBlend.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <span>
#include <utility>
#include <variant>

namespace
{

using an::ui::drawing::CoveragePlane;
using an::ui::drawing::ShapeEngine;
using an::ui::drawing::ShapeLayer;
using an::ui::drawing::ShapeObject;

/// A value read as a coverage: held in the unit interval, and none for one that is not a number.
[[nodiscard]] float heldCoverage(const float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

/// The bounding rectangle of two, either of which may be empty.
[[nodiscard]] QRect unite(const QRect& left, const QRect& right) noexcept
{
    if (left.isEmpty())
    {
        return right;
    }
    return right.isEmpty() ? left : left.united(right);
}

/// The bytes a paint patch of a rectangle holds: its samples before and after.
[[nodiscard]] std::uint64_t patchBytes(const QRect& rect) noexcept
{
    return rect.isEmpty() ? 0u
                          : 2u * static_cast<std::uint64_t>(rect.width()) * static_cast<std::uint64_t>(rect.height()) *
                                sizeof(float);
}

/// The parts of `grown` outside `inner`, which it contains: at most four bands.
[[nodiscard]] std::vector<QRect> growth(const QRect& inner, const QRect& grown)
{
    if (inner.isEmpty())
    {
        return {grown};
    }
    std::vector<QRect> bands;
    if (grown.top() < inner.top())
    {
        bands.emplace_back(QPoint(grown.left(), grown.top()), QPoint(grown.right(), inner.top() - 1));
    }
    if (grown.bottom() > inner.bottom())
    {
        bands.emplace_back(QPoint(grown.left(), inner.bottom() + 1), QPoint(grown.right(), grown.bottom()));
    }
    if (grown.left() < inner.left())
    {
        bands.emplace_back(QPoint(grown.left(), inner.top()), QPoint(inner.left() - 1, inner.bottom()));
    }
    if (grown.right() > inner.right())
    {
        bands.emplace_back(QPoint(inner.right() + 1, inner.top()), QPoint(grown.right(), inner.bottom()));
    }
    return bands;
}

/// The mask's samples under a lease, read-only.
template <typename LeaseT> [[nodiscard]] an::PlaneView<const float> maskPlane(const LeaseT& lease) noexcept
{
    return *std::get<an::Image<float>>(lease.image()).view()->plane(0u);
}

/// The mask's samples under the write lease.
[[nodiscard]] an::PlaneView<float> writableMaskPlane(an::consumer::AssetWriteLease& lease) noexcept
{
    return *lease.write<float>()->view().plane(0u);
}

/// Whether no sample of a plane is above zero.
[[nodiscard]] bool holdsNoCoverage(const an::PlaneView<const float> samples) noexcept
{
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        const std::span<const float> row = *samples.row(y);
        if (std::any_of(row.begin(), row.end(), [](const float value) { return value > 0.0f; }))
        {
            return false;
        }
    }
    return true;
}

/// What a colour image's red, green and blue are taken to be for its luma: linear when it says so, and otherwise the
/// encoding a display shows.
[[nodiscard]] an::ChannelScopeRequest lumaRequest(const an::ImageDescriptor& source) noexcept
{
    an::ColorEncoding encoding{};
    encoding.schema = an::ChannelSchema::Rgb;
    encoding.transfer = source.colorSpace() == an::ColorSpace::LinearRgb ? an::ColorTransferFunction::Linear
                                                                         : an::ColorTransferFunction::Srgb;
    return an::ChannelScopeRequest{.scope = an::ChannelScope::YCbCrLuma,
                                   .sourceEncoding = encoding,
                                   .units = an::ChannelScopeUnits::UnitInterval,
                                   .yCbCr = an::YCbCrVariant::Rec601FullRange};
}

/// An image's samples converted into a tightly packed Float32 plane of fractions of full scale: a grey image's one
/// channel, or a colour image's Rec. 601 luma. Every sample is then held as a coverage.
[[nodiscard]] an::Status importedCoverage(const an::ExecutionContext& execution,
                                          const an::consumer::AssetReadLease& source, std::vector<float>& plane)
{
    return std::visit(
        [&](const auto& image) -> an::Status {
            const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
            const auto view = image.view();
            if (!descriptor.has_value() || !view.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the imported mask holds no samples");
            }
            const std::uint32_t width = descriptor->extent().width();
            const std::uint32_t height = descriptor->extent().height();
            const std::optional<an::PlaneView<float>> destination = an::ui::drawing::packedPlane(plane, width, height);
            if (!destination.has_value())
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "the imported mask is not the drawing's size");
            }
            const an::ImageLayout destinationLayout = destination->layout();
            const std::uint32_t channels = descriptor->extent().channelCount();
            if (channels == 1u)
            {
                an::ConversionCounters counters{};
                return an::convertImageSamples(execution, descriptor->sampleType(), view->data(), view->layout(),
                                               an::SampleType::Float32, destination->data(), destinationLayout,
                                               an::ConversionPolicy{}, counters);
            }
            const an::ChannelSchema schema = descriptor->channelSchema();
            if (schema != an::ChannelSchema::Rgb && schema != an::ChannelSchema::Rgba)
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "an imported mask is grey or colour; this one is neither");
            }
            // A colour file's samples state no full scale; its luma is taken from them read as fractions of full
            // scale, as the view shows them.
            const std::optional<an::ImageDescriptor> reading = an::ui::componentReadingOf(*descriptor);
            if (!reading.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidArgument,
                                           "the imported mask cannot be read as fractions of full scale");
            }
            const an::Result<an::ChannelScopePlan> luma = an::planChannelScope(lumaRequest(*reading));
            if (!luma.hasValue())
            {
                return luma.status();
            }
            const an::Result<an::ImageDescriptor> lumaPlane = an::channelScopePlaneDescriptor(*luma, *reading);
            if (!lumaPlane.hasValue())
            {
                return lumaPlane.status();
            }
            const an::ImageLayout sourceLayout = view->layout();
            const an::ChannelScopeSource scoped{
                .descriptor = &*reading, .samples = view->data(), .layout = &sourceLayout};
            if (lumaPlane->sampleType() == an::SampleType::Float32)
            {
                const an::Result<an::ChannelScopeResult> extracted =
                    an::extractChannelScope(execution, *luma, scoped, destination->data(), destinationLayout);
                return extracted.hasValue() ? an::Status::success() : extracted.status();
            }
            // A wide source's luma is extracted in double and narrowed by the engine's conversion.
            std::vector<double> wide(plane.size(), 0.0);
            const std::optional<an::PlaneView<double>> widePlane = an::PlaneView<double>::create(
                wide, width, height, 1u, static_cast<std::uint64_t>(width) * sizeof(double));
            if (!widePlane.has_value() || lumaPlane->sampleType() != an::SampleType::Float64)
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "the imported mask's luma has a representation the drawing cannot read");
            }
            const an::ImageLayout wideLayout = widePlane->layout();
            const an::Result<an::ChannelScopeResult> extracted =
                an::extractChannelScope(execution, *luma, scoped, widePlane->data(), wideLayout);
            if (!extracted.hasValue())
            {
                return extracted.status();
            }
            an::ConversionCounters counters{};
            return an::convertImageSamples(execution, an::SampleType::Float64, widePlane->data(), wideLayout,
                                           an::SampleType::Float32, destination->data(), destinationLayout,
                                           an::ConversionPolicy{}, counters);
        },
        source.image());
}

/// A shape's feathered layer, with the bytes admitted for it.
struct CachedLayer
{
    ShapeLayer layer;
    an::consumer::MemoryReservation reservation;
};

using LayerPointer = std::shared_ptr<const CachedLayer>;

/// The paint a step changed: a rectangle, before and after.
struct PaintPatch
{
    QRect rect;
    std::vector<float> before;
    std::vector<float> after;
};

/// One undo step: the paint it changed and the shape list it replaced, with the bytes admitted for the paint.
struct UndoStep
{
    std::optional<PaintPatch> paint;
    bool shapesChanged = false;
    std::vector<ShapeObject> shapesBefore;
    std::vector<LayerPointer> layersBefore;
    std::vector<ShapeObject> shapesAfter;
    std::vector<LayerPointer> layersAfter;
    an::consumer::MemoryReservation reservation;
};

/// The bounding rectangle of a list of layers.
[[nodiscard]] QRect layersBounds(const std::vector<LayerPointer>& layers) noexcept
{
    QRect bounds;
    for (const LayerPointer& layer : layers)
    {
        if (layer)
        {
            bounds = unite(bounds, layer->layer.bounds);
        }
    }
    return bounds;
}

} // namespace

namespace an::ui::drawing
{

struct DrawingDocument::Impl
{
    const an::consumer::EngineSession* session = nullptr;
    int width = 0;
    int height = 0;

    /// What the brush, the fills and the gradients wrote, every sample a coverage.
    CoveragePlane paint;
    /// The paint as it was before the active stroke, valid over the pixels its dabs reached.
    CoveragePlane strokeBase;
    /// The active stroke's dabs, screened together; zero between strokes.
    CoveragePlane stroke;
    /// The paint with the shapes screened over it.
    std::shared_ptr<an::consumer::ImageAsset> mask;

    std::vector<ShapeObject> shapes;
    std::vector<LayerPointer> layers;

    BrushEngine brush;
    std::optional<an::LayerBlendPlan> strokeBlend;
    bool strokeActive = false;
    QRect strokeBounds;
    bool strokeFlattened = false;
    QRect flattenRect;
    std::vector<float> flattenBefore;
    std::vector<ShapeObject> shapesBeforeStroke;
    std::vector<LayerPointer> layersBeforeStroke;

    std::deque<UndoStep> steps;
    std::size_t undoIndex = 0u;

    std::uint64_t revision = 0u;
    QRect dirtyRect;
    mutable bool cachedEmpty = true;
    mutable std::uint64_t emptyRevision = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t shapeLayersRendered = 0u;

    [[nodiscard]] QRect fullRect() const noexcept
    {
        return QRect(0, 0, width, height);
    }

    void published(const QRect& rect) noexcept
    {
        ++revision;
        dirtyRect = rect;
    }

    /// Rebuilds the mask over a rectangle: the paint, with every shape's layer screened over it in order.
    [[nodiscard]] an::Status rebuild(const an::ExecutionContext& execution, an::consumer::AssetWriteLease& lease,
                                     const QRect& rect)
    {
        const QRect region = rect.intersected(fullRect());
        if (region.isEmpty())
        {
            return an::Status::success();
        }
        const an::PlaneView<float> composite = writableMaskPlane(lease);
        const std::optional<an::PlaneView<const float>> from = planeRegion(std::as_const(paint).view(), region);
        const std::optional<an::PlaneView<float>> to = planeRegion(composite, region);
        if (!from.has_value() || !to.has_value())
        {
            return an::Status::failure(an::ErrorCode::Internal, "the mask cannot be rebuilt over the region");
        }
        copyPlane(*from, *to);
        for (std::size_t index = 0u; index < shapes.size(); ++index)
        {
            const an::Status screened = ShapeEngine::screenShapeLayer(execution, layers[index]->layer,
                                                                      shapes[index].opacity, composite, region);
            if (screened.failed())
            {
                return screened;
            }
        }
        return an::Status::success();
    }

    /// Makes room for one step by dropping a step: a redo step first, since a new step discards them, then the oldest.
    [[nodiscard]] bool dropForRoom()
    {
        if (undoIndex < steps.size())
        {
            steps.pop_back();
            return true;
        }
        if (!steps.empty())
        {
            steps.pop_front();
            undoIndex -= undoIndex > 0u ? 1u : 0u;
            return true;
        }
        return false;
    }

    /// Admits a step's paint bytes against the undo allowance, dropping steps for room before refusing.
    [[nodiscard]] an::Result<an::consumer::MemoryReservation> admitStep(const std::uint64_t bytes)
    {
        if (bytes == 0u)
        {
            return an::consumer::MemoryReservation{};
        }
        for (;;)
        {
            an::Result<an::consumer::MemoryReservation> reserved =
                session->memory().reserve(an::consumer::MemoryCategory::HistoryCopies, bytes);
            if (reserved.hasValue() || reserved.status().code() != an::ErrorCode::MemoryBudgetExceeded ||
                !dropForRoom())
            {
                return reserved;
            }
        }
    }

    void pushStep(UndoStep&& step)
    {
        while (steps.size() > undoIndex)
        {
            steps.pop_back();
        }
        steps.push_back(std::move(step));
        while (steps.size() > kMaxDrawingUndoDepth)
        {
            steps.pop_front();
        }
        undoIndex = steps.size();
    }

    /// A shape's feathered layer, its bytes admitted against the derived-results allowance before it is made.
    [[nodiscard]] an::Result<LayerPointer> renderLayer(const an::ExecutionContext& execution, const ShapeObject& shape)
    {
        const QRect bounds = ShapeEngine::layerBounds(shape, width, height);
        an::consumer::MemoryReservation reservation;
        if (!bounds.isEmpty())
        {
            an::Result<an::consumer::MemoryReservation> reserved =
                session->memory().reserve(an::consumer::MemoryCategory::DerivedCaches,
                                          static_cast<std::uint64_t>(bounds.width()) *
                                              static_cast<std::uint64_t>(bounds.height()) * sizeof(float));
            if (!reserved.hasValue())
            {
                return reserved.status();
            }
            reservation = std::move(*reserved);
        }
        ShapeLayer layer = ShapeEngine::rasterizeShapeCoverage(shape, bounds);
        const an::Status feathered = ShapeEngine::featherShapeLayer(execution, layer, shape.feather);
        if (feathered.failed())
        {
            return feathered;
        }
        ++shapeLayersRendered;
        return std::make_shared<const CachedLayer>(CachedLayer{std::move(layer), std::move(reservation)});
    }

    void resetStroke() noexcept
    {
        brush.endStroke();
        strokeBlend.reset();
        strokeActive = false;
        strokeBounds = QRect();
        strokeFlattened = false;
        flattenRect = QRect();
        flattenBefore.clear();
        shapesBeforeStroke.clear();
        layersBeforeStroke.clear();
    }

    /// The paint as it was before the active stroke over a rectangle: what flattening replaced, what the dabs
    /// replaced, and elsewhere what is there now.
    [[nodiscard]] std::vector<float> strokeBefore(const QRect& rect) const
    {
        std::vector<float> before(static_cast<std::size_t>(rect.width()) * static_cast<std::size_t>(rect.height()));
        const an::PlaneView<const float> base = strokeBase.view();
        const an::PlaneView<const float> current = paint.view();
        for (int y = rect.top(); y <= rect.bottom(); ++y)
        {
            const std::span<const float> baseRow = *base.row(static_cast<std::uint32_t>(y));
            const std::span<const float> currentRow = *current.row(static_cast<std::uint32_t>(y));
            float* out =
                before.data() + static_cast<std::size_t>(y - rect.top()) * static_cast<std::size_t>(rect.width());
            for (int x = rect.left(); x <= rect.right(); ++x)
            {
                float value = currentRow[static_cast<std::size_t>(x)];
                if (strokeFlattened && flattenRect.contains(x, y))
                {
                    value = flattenBefore[static_cast<std::size_t>(y - flattenRect.top()) *
                                              static_cast<std::size_t>(flattenRect.width()) +
                                          static_cast<std::size_t>(x - flattenRect.left())];
                }
                else if (strokeBounds.contains(x, y))
                {
                    value = baseRow[static_cast<std::size_t>(x)];
                }
                out[x - rect.left()] = value;
            }
        }
        return before;
    }

    /// Ends the active stroke and keeps its step, or takes the stroke back when the step cannot be admitted. The
    /// caller holds the mask's write lease.
    [[nodiscard]] an::Status finishStroke(const an::ExecutionContext& execution, an::consumer::AssetWriteLease& lease)
    {
        if (!strokeActive)
        {
            return an::Status::success();
        }
        if (strokeBounds.isEmpty())
        {
            // Nothing was drawn: a flattening is put back, which the mask never showed.
            if (strokeFlattened)
            {
                writePlaneRegion(flattenBefore, paint.view(), flattenRect);
                shapes = std::move(shapesBeforeStroke);
                layers = std::move(layersBeforeStroke);
            }
            resetStroke();
            return an::Status::success();
        }

        const QRect rect = unite(strokeBounds, strokeFlattened ? flattenRect : QRect());
        std::vector<float> before = strokeBefore(rect);
        an::Result<an::consumer::MemoryReservation> reserved = admitStep(patchBytes(rect));
        if (!reserved.hasValue())
        {
            writePlaneRegion(before, paint.view(), rect);
            if (strokeFlattened)
            {
                shapes = std::move(shapesBeforeStroke);
                layers = std::move(layersBeforeStroke);
            }
            const an::Status rebuilt = rebuild(execution, lease, rect);
            published(rect);
            fillPlane(*planeRegion(stroke.view(), strokeBounds), 0.0f);
            resetStroke();
            return rebuilt.failed() ? rebuilt : reserved.status();
        }

        UndoStep step;
        step.paint = PaintPatch{rect, std::move(before), copyPlaneRegion(std::as_const(paint).view(), rect)};
        if (strokeFlattened)
        {
            step.shapesChanged = true;
            step.shapesBefore = std::move(shapesBeforeStroke);
            step.layersBefore = std::move(layersBeforeStroke);
        }
        step.reservation = std::move(*reserved);
        pushStep(std::move(step));
        fillPlane(*planeRegion(stroke.view(), strokeBounds), 0.0f);
        resetStroke();
        return an::Status::success();
    }

    /// Replaces the shapes, by a step forward or back.
    void takeShapes(const std::vector<ShapeObject>& nextShapes, const std::vector<LayerPointer>& nextLayers)
    {
        shapes = nextShapes;
        layers = nextLayers;
    }

    /// Applies a step forward or back under the write lease.
    [[nodiscard]] an::Status applyStep(const an::ExecutionContext& execution, an::consumer::AssetWriteLease& lease,
                                       const UndoStep& step, const bool forward)
    {
        QRect rect;
        if (step.paint.has_value())
        {
            writePlaneRegion(forward ? step.paint->after : step.paint->before, paint.view(), step.paint->rect);
            rect = step.paint->rect;
        }
        if (step.shapesChanged)
        {
            rect = unite(rect, unite(layersBounds(step.layersBefore), layersBounds(step.layersAfter)));
            if (forward)
            {
                takeShapes(step.shapesAfter, step.layersAfter);
            }
            else
            {
                takeShapes(step.shapesBefore, step.layersBefore);
            }
        }
        const an::Status rebuilt = rebuild(execution, lease, rect);
        published(rect);
        return rebuilt;
    }

    /// Flattens the shapes into the paint over the rectangle they reach, from the mask as it is; the mask does not
    /// change.
    void flatten(an::PlaneView<const float> composite)
    {
        const QRect rect = layersBounds(layers).intersected(fullRect());
        if (!rect.isEmpty())
        {
            copyPlane(*planeRegion(composite, rect), *planeRegion(paint.view(), rect));
        }
        shapes.clear();
        layers.clear();
    }

    /// The bounding rectangle of the paint's samples above zero.
    [[nodiscard]] QRect paintedBounds() const
    {
        int left = width;
        int top = height;
        int right = -1;
        int bottom = -1;
        const an::PlaneView<const float> samples = paint.view();
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            const std::span<const float> row = *samples.row(y);
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                if (row[x] > 0.0f)
                {
                    left = (std::min)(left, static_cast<int>(x));
                    right = (std::max)(right, static_cast<int>(x));
                    top = (std::min)(top, static_cast<int>(y));
                    bottom = (std::max)(bottom, static_cast<int>(y));
                }
            }
        }
        return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
    }
};

an::Result<std::shared_ptr<DrawingDocument>> DrawingDocument::create(const an::consumer::EngineSession& session,
                                                                     const int width, const int height)
{
    if (width <= 0 || height <= 0)
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a drawing needs a width and a height");
    }
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const auto columns = static_cast<std::uint32_t>(width);
    const auto rows = static_cast<std::uint32_t>(height);
    auto impl = std::make_unique<Impl>();
    impl->session = &session;
    impl->width = width;
    impl->height = height;
    for (CoveragePlane* plane : {&impl->paint, &impl->strokeBase, &impl->stroke})
    {
        an::Result<CoveragePlane> made = CoveragePlane::create(hold->engine(), columns, rows);
        if (!made.hasValue())
        {
            return made.status();
        }
        *plane = std::move(*made);
    }
    an::Result<an::Image<float>> maskImage = createCoverageImage(hold->engine(), columns, rows);
    if (!maskImage.hasValue())
    {
        return maskImage.status();
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*maskImage)));
    if (!asset.hasValue())
    {
        return asset.status();
    }
    impl->mask = std::make_shared<an::consumer::ImageAsset>(std::move(*asset));
    return std::shared_ptr<DrawingDocument>(new DrawingDocument(std::move(impl)));
}

DrawingDocument::DrawingDocument(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

DrawingDocument::~DrawingDocument() = default;

int DrawingDocument::width() const noexcept
{
    return m_impl->width;
}

int DrawingDocument::height() const noexcept
{
    return m_impl->height;
}

const an::consumer::EngineSession& DrawingDocument::session() const noexcept
{
    return *m_impl->session;
}

const std::shared_ptr<an::consumer::ImageAsset>& DrawingDocument::mask() const noexcept
{
    return m_impl->mask;
}

an::Result<an::consumer::ImageAsset> DrawingDocument::createOutputMask() const
{
    const an::Result<an::consumer::AssetReadLease> lease = m_impl->mask->tryRead();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return lease->snapshot();
}

std::uint64_t DrawingDocument::revision() const noexcept
{
    return m_impl->revision;
}

QRect DrawingDocument::dirtyRect() const noexcept
{
    return m_impl->dirtyRect;
}

bool DrawingDocument::isEmpty() const
{
    if (m_impl->emptyRevision == m_impl->revision)
    {
        return m_impl->cachedEmpty;
    }
    const an::Result<an::consumer::AssetReadLease> lease = m_impl->mask->tryRead();
    if (!lease.hasValue())
    {
        return m_impl->cachedEmpty;
    }
    const bool empty = holdsNoCoverage(maskPlane(*lease));
    m_impl->cachedEmpty = empty;
    m_impl->emptyRevision = m_impl->revision;
    return empty;
}

an::Status DrawingDocument::beginStroke(const an::ExecutionContext& execution, const BrushParams& brush)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    const an::Result<an::LayerBlendPlan> blend = an::planLayerBlend(
        an::LayerBlendRequest{.mode = layerBlendModeOf(brush.blendMode), .opacity = heldCoverage(brush.opacity)});
    if (!blend.hasValue())
    {
        return blend.status();
    }
    const an::Status begun = d.brush.beginStroke(brush);
    if (begun.failed())
    {
        return begun;
    }

    // Erasing over shapes erases what the mask shows, so the shapes are flattened into the paint first.
    if (brush.blendMode == BrushParams::BlendMode::Erase && !d.shapes.empty())
    {
        d.flattenRect = layersBounds(d.layers).intersected(d.fullRect());
        d.flattenBefore = copyPlaneRegion(std::as_const(d.paint).view(), d.flattenRect);
        d.shapesBeforeStroke = d.shapes;
        d.layersBeforeStroke = d.layers;
        d.flatten(maskPlane(*lease));
        d.strokeFlattened = true;
    }
    d.strokeBlend = *blend;
    d.strokeActive = true;
    d.strokeBounds = QRect();
    return an::Status::success();
}

an::Status DrawingDocument::addStrokePoint(const an::ExecutionContext& execution, const QPointF& imagePos,
                                           const float pressure)
{
    Impl& d = *m_impl;
    if (!d.strokeActive || !d.strokeBlend.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidState, "a stroke point needs an active stroke");
    }
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Result<QRect> dirty = d.brush.addStrokePoint(execution, d.stroke.view(), imagePos, pressure);
    if (!dirty.hasValue())
    {
        return dirty.status();
    }
    if (dirty->isEmpty())
    {
        return an::Status::success();
    }

    // The base the stroke lands on is the paint as it was before the stroke: kept for every pixel the stroke reaches,
    // the moment it first reaches it.
    const QRect grown = unite(d.strokeBounds, *dirty);
    for (const QRect& band : growth(d.strokeBounds, grown))
    {
        copyPlane(*planeRegion(std::as_const(d.paint).view(), band), *planeRegion(d.strokeBase.view(), band));
    }
    d.strokeBounds = grown;

    const an::Result<an::LayerBlendResult> landed =
        an::blendLayerPlane(execution, *d.strokeBlend, *planeRegion(std::as_const(d.strokeBase).view(), *dirty),
                            *planeRegion(std::as_const(d.stroke).view(), *dirty), *planeRegion(d.paint.view(), *dirty));
    if (!landed.hasValue())
    {
        return landed.status();
    }
    const an::Status rebuilt = d.rebuild(execution, *lease, *dirty);
    d.published(*dirty);
    return rebuilt;
}

an::Status DrawingDocument::endStroke(const an::ExecutionContext& execution)
{
    if (!m_impl->strokeActive)
    {
        return an::Status::success();
    }
    an::Result<an::consumer::AssetWriteLease> lease = m_impl->mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return m_impl->finishStroke(execution, *lease);
}

bool DrawingDocument::hasActiveStroke() const noexcept
{
    return m_impl->strokeActive;
}

an::Status DrawingDocument::importMask(const an::ExecutionContext& execution,
                                       const an::consumer::AssetReadLease& source)
{
    Impl& d = *m_impl;
    if (!source.valid())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "an imported mask needs a lease on an image");
    }
    const std::optional<an::ImageDescriptor> described =
        std::visit([](const auto& image) { return image.descriptor(); }, source.image());
    if (!described.has_value() || described->extent().width() != static_cast<std::uint32_t>(d.width) ||
        described->extent().height() != static_cast<std::uint32_t>(d.height))
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "the imported mask is not the drawing's size");
    }
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    std::vector<float> imported(static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height), 0.0f);
    const an::Status converted = importedCoverage(execution, source, imported);
    if (converted.failed())
    {
        return converted;
    }
    for (float& value : imported)
    {
        value = heldCoverage(value);
    }
    const QRect rect = d.fullRect();
    an::Result<an::consumer::MemoryReservation> reserved = d.admitStep(patchBytes(rect));
    if (!reserved.hasValue())
    {
        return reserved.status();
    }

    UndoStep step;
    step.paint = PaintPatch{rect, copyPlaneRegion(std::as_const(d.paint).view(), rect), {}};
    step.shapesChanged = true;
    step.shapesBefore = d.shapes;
    step.layersBefore = d.layers;
    writePlaneRegion(imported, d.paint.view(), rect);
    step.paint->after = std::move(imported);
    d.shapes.clear();
    d.layers.clear();
    step.reservation = std::move(*reserved);
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

an::Status DrawingDocument::floodFill(const an::ExecutionContext& execution, const QPoint& seedPoint,
                                      const float fillValue, const float tolerance,
                                      const BrushParams::BlendMode blendMode)
{
    Impl& d = *m_impl;
    const QRect imageRect = d.fullRect();
    if (!imageRect.contains(seedPoint))
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "a flood fill's seed is outside the drawing");
    }
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    const an::Result<an::LayerBlendPlan> blend =
        an::planLayerBlend(an::LayerBlendRequest{.mode = layerBlendModeOf(blendMode), .opacity = 1.0f});
    if (!blend.hasValue())
    {
        return blend.status();
    }

    // The region is found on the mask as it is, before anything changes.
    const an::PlaneView<const float> composite = maskPlane(*lease);
    const auto valueAt = [&composite](const int x, const int y) {
        return *composite.sampleAt(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    };
    const int width = d.width;
    const float targetValue = valueAt(seedPoint.x(), seedPoint.y());
    const float heldTolerance = heldCoverage(tolerance);
    const float heldFill = heldCoverage(fillValue);
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(width) * static_cast<std::size_t>(d.height), 0u);
    const auto indexOf = [width](const QPoint& point) {
        return static_cast<std::size_t>(point.y()) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(point.x());
    };
    std::deque<QPoint> pending{seedPoint};
    QRect dirty;
    while (!pending.empty())
    {
        const QPoint point = pending.front();
        pending.pop_front();
        if (!imageRect.contains(point) || visited[indexOf(point)] != 0u)
        {
            continue;
        }
        visited[indexOf(point)] = 1u;
        const float baseValue = valueAt(point.x(), point.y());
        if (std::abs(baseValue - targetValue) > heldTolerance)
        {
            continue;
        }
        const float filledValue = an::evaluateLayerBlend(*blend, baseValue, heldFill);
        if (!qFuzzyCompare(1.0f + baseValue, 1.0f + filledValue))
        {
            dirty = unite(dirty, QRect(point, QSize(1, 1)));
        }
        for (const QPoint& neighbour :
             {point + QPoint(1, 0), point + QPoint(-1, 0), point + QPoint(0, 1), point + QPoint(0, -1)})
        {
            if (imageRect.contains(neighbour) && visited[indexOf(neighbour)] == 0u)
            {
                pending.push_back(neighbour);
            }
        }
    }
    if (dirty.isEmpty())
    {
        return an::Status::success();
    }

    const QRect rect = unite(dirty, layersBounds(d.layers).intersected(imageRect));
    an::Result<an::consumer::MemoryReservation> reserved = d.admitStep(patchBytes(rect));
    if (!reserved.hasValue())
    {
        return reserved.status();
    }
    UndoStep step;
    step.paint = PaintPatch{rect, copyPlaneRegion(std::as_const(d.paint).view(), rect), {}};
    if (!d.shapes.empty())
    {
        step.shapesChanged = true;
        step.shapesBefore = d.shapes;
        step.layersBefore = d.layers;
        d.flatten(composite);
    }
    const an::PlaneView<float> painted = d.paint.view();
    for (int y = dirty.top(); y <= dirty.bottom(); ++y)
    {
        for (int x = dirty.left(); x <= dirty.right(); ++x)
        {
            const float baseValue = valueAt(x, y);
            if (visited[indexOf(QPoint(x, y))] == 0u || std::abs(baseValue - targetValue) > heldTolerance)
            {
                continue;
            }
            *painted.sampleAt(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) =
                an::evaluateLayerBlend(*blend, baseValue, heldFill);
        }
    }
    step.paint->after = copyPlaneRegion(std::as_const(d.paint).view(), rect);
    step.reservation = std::move(*reserved);
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

an::Status DrawingDocument::applyLinearGradient(const an::ExecutionContext& execution, const QPointF& startPoint,
                                                const QPointF& endPoint, const float startValue, const float endValue,
                                                const BrushParams::BlendMode blendMode)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    const an::Result<an::LayerBlendPlan> blend =
        an::planLayerBlend(an::LayerBlendRequest{.mode = layerBlendModeOf(blendMode), .opacity = 1.0f});
    if (!blend.hasValue())
    {
        return blend.status();
    }
    const QRect rect = d.fullRect();
    an::Result<an::consumer::MemoryReservation> reserved = d.admitStep(patchBytes(rect));
    if (!reserved.hasValue())
    {
        return reserved.status();
    }

    const an::PlaneView<const float> composite = maskPlane(*lease);
    UndoStep step;
    step.paint = PaintPatch{rect, copyPlaneRegion(std::as_const(d.paint).view(), rect), {}};
    if (!d.shapes.empty())
    {
        step.shapesChanged = true;
        step.shapesBefore = d.shapes;
        step.layersBefore = d.layers;
        d.flatten(composite);
    }
    const QPointF delta = endPoint - startPoint;
    const double denominator = (delta.x() * delta.x()) + (delta.y() * delta.y());
    const float heldStart = heldCoverage(startValue);
    const float heldEnd = heldCoverage(endValue);
    const an::PlaneView<float> painted = d.paint.view();
    for (std::uint32_t y = 0u; y < painted.height(); ++y)
    {
        const std::span<float> row = *painted.row(y);
        const std::span<const float> under = *composite.row(y);
        for (std::uint32_t x = 0u; x < painted.width(); ++x)
        {
            float t = 1.0f;
            if (denominator > 1.0e-6)
            {
                const QPointF point(static_cast<double>(x), static_cast<double>(y));
                t = clampUnit(static_cast<float>(QPointF::dotProduct(point - startPoint, delta) / denominator));
            }
            const float rampValue = heldStart + ((heldEnd - heldStart) * t);
            row[x] = an::evaluateLayerBlend(*blend, under[x], rampValue);
        }
    }
    step.paint->after = copyPlaneRegion(std::as_const(d.paint).view(), rect);
    step.reservation = std::move(*reserved);
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

an::Result<std::size_t> DrawingDocument::addShape(const an::ExecutionContext& execution, const ShapeObject& shape)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    an::Result<LayerPointer> layer = d.renderLayer(execution, shape);
    if (!layer.hasValue())
    {
        return layer.status();
    }
    UndoStep step;
    step.shapesChanged = true;
    step.shapesBefore = d.shapes;
    step.layersBefore = d.layers;
    const QRect rect = (*layer)->layer.bounds;
    d.shapes.push_back(shape);
    d.layers.push_back(std::move(*layer));
    step.shapesAfter = d.shapes;
    step.layersAfter = d.layers;
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    if (rebuilt.failed())
    {
        return rebuilt;
    }
    return d.shapes.size() - 1u;
}

an::Status DrawingDocument::updateShape(const an::ExecutionContext& execution, const std::size_t index,
                                        const ShapeObject& shape)
{
    Impl& d = *m_impl;
    if (index >= d.shapes.size())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "no shape has that index");
    }
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    an::Result<LayerPointer> layer = d.renderLayer(execution, shape);
    if (!layer.hasValue())
    {
        return layer.status();
    }
    UndoStep step;
    step.shapesChanged = true;
    step.shapesBefore = d.shapes;
    step.layersBefore = d.layers;
    const QRect rect = unite(d.layers[index]->layer.bounds, (*layer)->layer.bounds);
    d.shapes[index] = shape;
    d.layers[index] = std::move(*layer);
    step.shapesAfter = d.shapes;
    step.layersAfter = d.layers;
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

an::Status DrawingDocument::removeShape(const an::ExecutionContext& execution, const std::size_t index)
{
    Impl& d = *m_impl;
    if (index >= d.shapes.size())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "no shape has that index");
    }
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    UndoStep step;
    step.shapesChanged = true;
    step.shapesBefore = d.shapes;
    step.layersBefore = d.layers;
    const QRect rect = d.layers[index]->layer.bounds;
    d.shapes.erase(d.shapes.begin() + static_cast<std::ptrdiff_t>(index));
    d.layers.erase(d.layers.begin() + static_cast<std::ptrdiff_t>(index));
    step.shapesAfter = d.shapes;
    step.layersAfter = d.layers;
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

const std::vector<ShapeObject>& DrawingDocument::shapes() const noexcept
{
    return m_impl->shapes;
}

std::optional<std::size_t> DrawingDocument::hitTestShape(const QPointF& imagePos, const double tolerance) const
{
    for (std::size_t index = m_impl->shapes.size(); index-- > 0u;)
    {
        if (ShapeEngine::hitTest(m_impl->shapes[index], imagePos, tolerance))
        {
            return index;
        }
    }
    return std::nullopt;
}

bool DrawingDocument::canUndo() const noexcept
{
    return m_impl->undoIndex > 0u;
}

bool DrawingDocument::canRedo() const noexcept
{
    return m_impl->undoIndex < m_impl->steps.size();
}

an::Status DrawingDocument::undo(const an::ExecutionContext& execution)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    if (!canUndo())
    {
        return an::Status::success();
    }
    --d.undoIndex;
    return d.applyStep(execution, *lease, d.steps[d.undoIndex], false);
}

an::Status DrawingDocument::redo(const an::ExecutionContext& execution)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    if (!canRedo())
    {
        return an::Status::success();
    }
    const an::Status applied = d.applyStep(execution, *lease, d.steps[d.undoIndex], true);
    ++d.undoIndex;
    return applied;
}

an::Status DrawingDocument::clear(const an::ExecutionContext& execution)
{
    Impl& d = *m_impl;
    an::Result<an::consumer::AssetWriteLease> lease = d.mask->tryWrite();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    const an::Status finished = d.finishStroke(execution, *lease);
    if (finished.failed())
    {
        return finished;
    }
    if (holdsNoCoverage(maskPlane(*lease)))
    {
        return an::Status::success();
    }
    const QRect painted = d.paintedBounds();
    an::Result<an::consumer::MemoryReservation> reserved = d.admitStep(patchBytes(painted));
    if (!reserved.hasValue())
    {
        return reserved.status();
    }
    UndoStep step;
    if (!painted.isEmpty())
    {
        step.paint = PaintPatch{
            painted, copyPlaneRegion(std::as_const(d.paint).view(), painted),
            std::vector<float>(static_cast<std::size_t>(painted.width()) * static_cast<std::size_t>(painted.height()),
                               0.0f)};
        fillPlane(*planeRegion(d.paint.view(), painted), 0.0f);
    }
    const QRect rect = unite(painted, layersBounds(d.layers));
    if (!d.shapes.empty())
    {
        step.shapesChanged = true;
        step.shapesBefore = d.shapes;
        step.layersBefore = d.layers;
        d.shapes.clear();
        d.layers.clear();
    }
    step.reservation = std::move(*reserved);
    const an::Status rebuilt = d.rebuild(execution, *lease, rect);
    d.published(rect);
    d.pushStep(std::move(step));
    return rebuilt;
}

DrawingDocumentCounters DrawingDocument::counters() const noexcept
{
    DrawingDocumentCounters counters;
    counters.shapeLayersRendered = m_impl->shapeLayersRendered;
    counters.undoSteps = m_impl->steps.size();
    std::vector<const CachedLayer*> seen;
    const auto countLayers = [&seen, &counters](const std::vector<LayerPointer>& layers) {
        for (const LayerPointer& layer : layers)
        {
            if (layer && std::find(seen.begin(), seen.end(), layer.get()) == seen.end())
            {
                seen.push_back(layer.get());
                counters.layerBytes += layer->layer.bytes();
            }
        }
    };
    countLayers(m_impl->layers);
    for (const UndoStep& step : m_impl->steps)
    {
        if (step.paint.has_value())
        {
            counters.undoBytes +=
                static_cast<std::uint64_t>(step.paint->before.size() + step.paint->after.size()) * sizeof(float);
        }
        countLayers(step.layersBefore);
        countLayers(step.layersAfter);
    }
    return counters;
}

} // namespace an::ui::drawing
