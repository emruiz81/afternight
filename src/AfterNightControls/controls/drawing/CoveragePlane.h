// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A plane the drawing tools draw on: one Float32 coverage sample per pixel, zero where nothing is selected and one
/// where everything is, held in an engine image of one channel.
///
/// The drawing tools keep their canvas, their stroke and their undo patches, and every number they compute over a
/// plane is the engine's (`Algorithms/Spatial/DrawingPrimitives.h`, `Algorithms/Pointwise/LayerBlend.h`), which reads
/// and writes `PlaneView<float>`. Holding the samples in an engine image is what makes the engine count their bytes
/// against the session's budget, refuse a plane the budget cannot hold before anything is drawn, and keep the session
/// from replacing its context while a drawing is open. A rectangle of a plane is a `QRect` in image pixels, as the
/// tools and the view address them.
#pragma once

#include "AfterNightControlsExport.h"

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/PlaneView.h>

#include <QRect>

#include <cstdint>
#include <optional>
#include <vector>

namespace an
{
class EngineContext;
} // namespace an

namespace an::ui::drawing
{

/// A grey Float32 image of a size, every sample zero, described as a coverage in the unit interval.
///
/// @param engine the context whose memory the samples come from.
/// @param width the width in pixels, at least one.
/// @param height the height in pixels, at least one.
/// @return the image; or `ErrorCode::InvalidArgument` for a size no image of the context can have; or what the
///         context's memory refuses.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::Image<float>>
createCoverageImage(const an::EngineContext& engine, std::uint32_t width, std::uint32_t height);

/// The part of a plane a rectangle covers.
///
/// @param plane the whole plane.
/// @param rect the rectangle in the plane's pixels.
/// @return the subplane; or no value for an empty rectangle or one that leaves the plane.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::PlaneView<float>> planeRegion(an::PlaneView<float> plane,
                                                                                     const QRect& rect);

/// The part of a read-only plane a rectangle covers.
///
/// @param plane the whole plane.
/// @param rect the rectangle in the plane's pixels.
/// @return the subplane; or no value for an empty rectangle or one that leaves the plane.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::PlaneView<const float>>
planeRegion(an::PlaneView<const float> plane, const QRect& rect);

/// Copies every sample of one plane into another of the same size.
///
/// @param source what is copied.
/// @param destination where it goes; nothing is copied when the sizes differ.
AFTERNIGHTCONTROLS_API void copyPlane(an::PlaneView<const float> source, an::PlaneView<float> destination) noexcept;

/// Writes one value into every sample of a plane.
///
/// @param destination the plane.
/// @param value the value.
AFTERNIGHTCONTROLS_API void fillPlane(an::PlaneView<float> destination, float value) noexcept;

/// The samples of a rectangle of a plane, row by row and tightly packed.
///
/// @param plane the whole plane.
/// @param rect the rectangle, inside the plane.
/// @return the samples, `rect.width() * rect.height()` of them; empty for a rectangle that leaves the plane.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<float> copyPlaneRegion(an::PlaneView<const float> plane,
                                                                        const QRect& rect);

/// Writes tightly packed samples into a rectangle of a plane.
///
/// @param samples `rect.width() * rect.height()` samples, row by row.
/// @param plane the whole plane.
/// @param rect the rectangle, inside the plane; nothing is written for one that leaves it or a count that differs.
AFTERNIGHTCONTROLS_API void writePlaneRegion(const std::vector<float>& samples, an::PlaneView<float> plane,
                                             const QRect& rect) noexcept;

/// A view of tightly packed samples as a plane.
///
/// @param samples the samples, `width * height` of them, row by row.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @return the view, or no value for a count that does not match.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::PlaneView<float>>
packedPlane(std::vector<float>& samples, std::uint32_t width, std::uint32_t height) noexcept;

/// A read-only view of tightly packed samples as a plane.
///
/// @param samples the samples, `width * height` of them, row by row.
/// @param width the width in pixels.
/// @param height the height in pixels.
/// @return the view, or no value for a count that does not match.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<an::PlaneView<const float>>
packedPlane(const std::vector<float>& samples, std::uint32_t width, std::uint32_t height) noexcept;

/// One coverage plane of a fixed size, owned.
///
/// Move-only, as the engine image it holds is. Not thread-safe: the drawing tools use it on one thread.
class AFTERNIGHTCONTROLS_API CoveragePlane
{
public:
    /// A plane that holds nothing.
    CoveragePlane() noexcept = default;

    /// A new plane of a size, every sample zero.
    ///
    /// @param engine the context whose memory the samples come from.
    /// @param width the width in pixels.
    /// @param height the height in pixels.
    /// @return the plane, or what `createCoverageImage` refuses.
    [[nodiscard]] static an::Result<CoveragePlane> create(const an::EngineContext& engine, std::uint32_t width,
                                                          std::uint32_t height);

    /// Whether this holds a plane.
    [[nodiscard]] bool valid() const noexcept;

    /// The width in pixels; zero for a plane that holds nothing.
    [[nodiscard]] int width() const noexcept;

    /// The height in pixels; zero for a plane that holds nothing.
    [[nodiscard]] int height() const noexcept;

    /// The whole plane as a rectangle at the origin.
    [[nodiscard]] QRect rect() const noexcept;

    /// The samples, writable. Calling it on a plane that holds nothing is a defect.
    [[nodiscard]] an::PlaneView<float> view() noexcept;

    /// The samples, read-only. Calling it on a plane that holds nothing is a defect.
    [[nodiscard]] an::PlaneView<const float> view() const noexcept;

    /// One sample.
    ///
    /// @param x the column, inside the plane.
    /// @param y the row, inside the plane.
    /// @return the sample.
    [[nodiscard]] float at(int x, int y) const noexcept;

private:
    explicit CoveragePlane(an::Image<float>&& image) noexcept;

    an::Image<float> m_image;
};

} // namespace an::ui::drawing
