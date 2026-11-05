// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the mask drawing cases share: a session of a stated width and memory allowances, an operation admitted for a
/// case that edits a document directly, documents and grey images of a size, and the mask read under a lease.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"

#include "controls/drawing/CoveragePlane.h"
#include "controls/drawing/DrawingDocument.h"
#include "controls/drawing/DrawingTypes.h"

#include <AfterNightConsumerRuntime/ConsumerMemory.h>
#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace an::tests::drawing
{

using an::tests::controls::kPatience;
using an::tests::controls::kQuietPeriod;
using an::tests::controls::pumpQuietly;
using an::tests::controls::pumpUntil;
using an::tests::controls::TestSessionProvider;
using an::tests::viewer::admitted;
using an::tests::viewer::Channels;
using an::tests::viewer::makeAsset;

/// A provider over a session of a width and memory allowances; fails the case when it cannot be opened.
[[nodiscard]] inline std::optional<TestSessionProvider>
openDrawingProvider(std::uint32_t width = 2u, const an::consumer::MemoryAllowances& allowances = {})
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    settings.memory = allowances;
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// A new, empty drawing of a size; fails the case when it cannot be made.
[[nodiscard]] inline std::shared_ptr<an::ui::drawing::DrawingDocument>
makeDrawing(const an::consumer::EngineSession& session, int width, int height)
{
    an::Result<std::shared_ptr<an::ui::drawing::DrawingDocument>> document =
        an::ui::drawing::DrawingDocument::create(session, width, height);
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    return document.hasValue() ? *document : nullptr;
}

/// The mask's sample at a pixel, read under a lease; not a number when it cannot be read.
[[nodiscard]] inline float maskAt(const an::ui::drawing::DrawingDocument& document, int x, int y)
{
    an::Result<an::consumer::AssetReadLease> lease = document.mask()->tryRead();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    return an::tests::controls::sampleAt(lease->image(), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

/// A grey Float32 asset of a session, each sample `value(x, y)`; fails the case when it cannot be built.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset>
greyAsset(const an::consumer::EngineSession& session, int width, int height,
          const std::function<float(std::uint32_t, std::uint32_t)>& value)
{
    return makeAsset<float>(session, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                            Channels::Grey,
                            [&value](std::uint32_t x, std::uint32_t y, std::uint32_t) { return value(x, y); });
}

/// Imports an asset into a document under the case's operation; fails the case when the import is refused.
inline void importInto(an::ui::drawing::DrawingDocument& document, const an::ExecutionContext& execution,
                       const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    const an::Status imported = document.importMask(execution, *lease);
    ASSERT_FALSE(imported.failed()) << imported.message();
}

/// A hard round brush of a size at full opacity and flow, dabbed every quarter of its diameter.
[[nodiscard]] inline an::ui::drawing::BrushParams makeBrush(float size)
{
    an::ui::drawing::BrushParams brush;
    brush.size = size;
    brush.hardness = 1.0f;
    brush.opacity = 1.0f;
    brush.flow = 1.0f;
    brush.spacing = 0.25f;
    brush.blendMode = an::ui::drawing::BrushParams::BlendMode::Normal;
    return brush;
}

/// A filled rectangle of a size about a centre, opaque and unfeathered.
[[nodiscard]] inline an::ui::drawing::ShapeObject
makeRectangle(double centerX, double centerY, double width, double height, float feather = 0.0f, float opacity = 1.0f)
{
    an::ui::drawing::ShapeObject shape;
    shape.type = an::ui::drawing::ShapeObject::Type::Rectangle;
    shape.center = QPointF(centerX, centerY);
    shape.data = an::ui::drawing::ShapeObject::RectData{QSizeF(width, height), 0.0f};
    shape.opacity = opacity;
    shape.feather = feather;
    shape.filled = true;
    return shape;
}

/// The bytes the session's undo allowance holds now.
[[nodiscard]] inline std::uint64_t undoBytesHeld(const an::consumer::EngineSession& session)
{
    return session.memory().counters(an::consumer::MemoryCategory::HistoryCopies).reservedBytes;
}

} // namespace an::tests::drawing
