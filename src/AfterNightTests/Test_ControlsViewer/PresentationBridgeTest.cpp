// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The one presentation bridge: presented bytes are allocated in the layout the engine states and counted against the
/// display allowance while they live; the `QImage` that wraps them, and every copy, pixmap and queued paint made from
/// it, keeps them alive after every other owner has let go, and releases them with the last of those; the
/// blue-first premultiplied bytes a view paints are allocated only as what they are stated to be; and bytes taken from
/// a session's context serve its next presentation of their size while a preview is open.

#include "ControlsViewerTestSupport.h"

#include "widgets/PresentationBridge.h"

#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>

namespace
{

using an::ui::PresentedFormat;
using an::ui::PresentedImage;

/// The layout a presentation states for bytes of a shape: one plane, rows on four-byte boundaries.
[[nodiscard]] an::ImageLayoutRequest byteLayout(std::uint32_t width, std::uint32_t height, std::uint32_t channels)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    return an::ImageLayoutRequest{.extent = *extent,
                                  .elementSizeBytes = 1u,
                                  .samplesPerPixel = channels,
                                  .planeCount = 1u,
                                  .rowAlignmentBytes = 4u};
}

/// Presented bytes of a shape filled with one value; fails the case when they cannot be allocated.
[[nodiscard]] std::shared_ptr<PresentedImage> filledBytes(std::uint32_t width, std::uint32_t height,
                                                          std::uint32_t channels, std::uint8_t value,
                                                          an::consumer::ConsumerMemory* memory = nullptr)
{
    an::Result<std::shared_ptr<PresentedImage>> image =
        PresentedImage::allocate(byteLayout(width, height, channels), an::defaultEngineLimits(), memory);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return nullptr;
    }
    std::fill((*image)->data(), (*image)->data() + (*image)->byteCount(), value);
    return *image;
}

} // namespace

TEST(PresentationBridgeTest, AllocatesTheLayoutTheEngineStatesInOneOfThreeFormats)
{
    for (const auto& [channels, format, qt] : {std::tuple{1u, PresentedFormat::Grey8, QImage::Format_Grayscale8},
                                               std::tuple{3u, PresentedFormat::Rgb8, QImage::Format_RGB888},
                                               std::tuple{4u, PresentedFormat::Rgba8, QImage::Format_RGBA8888}})
    {
        const std::shared_ptr<PresentedImage> image = filledBytes(5u, 3u, channels, 9u);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(image->width(), 5u);
        EXPECT_EQ(image->height(), 3u);
        EXPECT_EQ(image->format(), format);
        EXPECT_EQ(image->bytesPerPixel(), channels);
        EXPECT_EQ(image->layout().rowStrideBytes() % 4u, 0u) << "a toolkit image needs rows on four-byte boundaries";
        EXPECT_GE(image->layout().rowStrideBytes(), 5u * channels);
        EXPECT_EQ(image->byteCount(), image->layout().rowStrideBytes() * 3u)
            << "every row is whole, the last one's padding included, because a QImage reads bytesPerLine of each";
        EXPECT_EQ(an::ui::qtFormatOf(format), qt);
        ASSERT_NE(image->pixel(4u, 2u), nullptr);
        EXPECT_EQ(*image->pixel(4u, 2u), 9u);
        EXPECT_EQ(image->pixel(5u, 0u), nullptr) << "a pixel outside the image has no bytes";
    }
}

// Four bytes a pixel are red-first with straight alpha unless they are stated to be the blue-first premultiplied bytes
// Qt paints without converting, and a format is allocated only over a layout of its own pixel size.
TEST(PresentationBridgeTest, BlueFirstPremultipliedBytesAreAllocatedOnlyAsStated)
{
    an::Result<std::shared_ptr<PresentedImage>> painted = PresentedImage::allocate(
        byteLayout(5u, 3u, 4u), PresentedFormat::Bgra8Premultiplied, an::defaultEngineLimits(), nullptr);
    ASSERT_TRUE(painted.hasValue()) << painted.status().message();
    EXPECT_EQ((*painted)->format(), PresentedFormat::Bgra8Premultiplied);
    EXPECT_EQ(an::ui::qtFormatOf((*painted)->format()), QImage::Format_ARGB32_Premultiplied);
    EXPECT_STREQ(an::ui::presentedFormatName((*painted)->format()), "bgra8-premultiplied");
    EXPECT_EQ(an::ui::presentedQImage(*painted).format(), QImage::Format_ARGB32_Premultiplied);

    const an::Result<std::shared_ptr<PresentedImage>> deduced =
        PresentedImage::allocate(byteLayout(5u, 3u, 4u), an::defaultEngineLimits(), nullptr);
    ASSERT_TRUE(deduced.hasValue());
    EXPECT_EQ((*deduced)->format(), PresentedFormat::Rgba8) << "four bytes a pixel unstated are red-first";

    const an::Result<std::shared_ptr<PresentedImage>> refused = PresentedImage::allocate(
        byteLayout(5u, 3u, 3u), PresentedFormat::Bgra8Premultiplied, an::defaultEngineLimits(), nullptr);
    ASSERT_FALSE(refused.hasValue()) << "three bytes a pixel are not four";
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);
}

TEST(PresentationBridgeTest, RefusesALayoutThatIsNotOnePlaneOfOneThreeOrFourBytes)
{
    an::ImageLayoutRequest planar = byteLayout(4u, 4u, 3u);
    planar.planeCount = 3u;
    planar.samplesPerPixel = 1u;
    an::Result<std::shared_ptr<PresentedImage>> refused =
        PresentedImage::allocate(planar, an::defaultEngineLimits(), nullptr);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);

    an::ImageLayoutRequest floats = byteLayout(4u, 4u, 1u);
    floats.elementSizeBytes = 4u;
    refused = PresentedImage::allocate(floats, an::defaultEngineLimits(), nullptr);
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidArgument);

    refused = PresentedImage::allocate(byteLayout(4u, 4u, 2u), an::defaultEngineLimits(), nullptr);
    ASSERT_FALSE(refused.hasValue()) << "two bytes per pixel is no format a view paints";
}

TEST(PresentationBridgeTest, CountsItsBytesAgainstTheDisplayAllowanceWhileTheyLive)
{
    an::consumer::ConsumerMemory memory(
        an::consumer::MemoryAllowances{.displayBuffers = an::consumer::CategoryAllowance{.bytes = 64u}});
    std::shared_ptr<PresentedImage> image = filledBytes(16u, 16u, 1u, 0u, &memory);
    ASSERT_NE(image, nullptr);
    const an::consumer::MemoryCategoryCounters held = memory.counters(an::consumer::MemoryCategory::DisplayBuffers);
    EXPECT_EQ(held.reservedBytes, image->byteCount());
    EXPECT_EQ(held.overrunAdmissions, 1u) << "a buffer a view paints is admitted beyond the allowance and counted";

    QImage wrapped = an::ui::presentedQImage(image);
    image.reset();
    EXPECT_EQ(memory.counters(an::consumer::MemoryCategory::DisplayBuffers).reservedBytes, 256u)
        << "the wrapped image still holds the bytes";
    wrapped = QImage();
    EXPECT_EQ(memory.counters(an::consumer::MemoryCategory::DisplayBuffers).reservedBytes, 0u);
}

TEST(PresentationBridgeTest, TheWrappedImageSharesTheBytesAndKeepsThemAliveAfterEveryOtherOwner)
{
    std::shared_ptr<PresentedImage> image = filledBytes(6u, 2u, 3u, 0u);
    ASSERT_NE(image, nullptr);
    image->data()[0] = 200u;
    image->data()[1] = 100u;
    image->data()[2] = 50u;
    const std::weak_ptr<PresentedImage> watched = image;

    QImage wrapped = an::ui::presentedQImage(image);
    ASSERT_FALSE(wrapped.isNull());
    EXPECT_EQ(wrapped.constBits(), image->data()) << "the bridge wraps, it does not copy";
    EXPECT_EQ(wrapped.format(), QImage::Format_RGB888);
    EXPECT_EQ(wrapped.bytesPerLine(), static_cast<qsizetype>(image->layout().rowStrideBytes()));

    // The view replaces what it shows: its own reference goes first.
    image.reset();
    ASSERT_FALSE(watched.expired()) << "the wrapped image keeps the bytes it shows";
    EXPECT_EQ(wrapped.pixelColor(0, 0), QColor(200, 100, 50));

    QImage copy = wrapped;
    wrapped = QImage();
    ASSERT_FALSE(watched.expired()) << "a copy of the wrapped image keeps them too";
    EXPECT_EQ(copy.pixelColor(0, 0), QColor(200, 100, 50));
    copy = QImage();
    EXPECT_TRUE(watched.expired()) << "the last image sharing the bytes releases them";
}

TEST(PresentationBridgeTest, AQueuedPaintAndAPixmapKeepTheBytesAliveUntilTheyRun)
{
    std::shared_ptr<PresentedImage> image = filledBytes(4u, 4u, 1u, 77u);
    ASSERT_NE(image, nullptr);
    const std::weak_ptr<PresentedImage> watched = image;

    QImage painted;
    QObject receiver;
    {
        const QImage queued = an::ui::presentedQImage(image);
        QMetaObject::invokeMethod(
            &receiver,
            [queued, &painted]() {
                QImage target(4, 4, QImage::Format_Grayscale8);
                target.fill(0);
                QPainter painter(&target);
                painter.drawImage(0, 0, queued);
                painter.end();
                painted = target;
            },
            Qt::QueuedConnection);
    }
    // The source is replaced and resized before the paint runs.
    image = filledBytes(9u, 9u, 1u, 3u);
    ASSERT_FALSE(watched.expired()) << "the queued paint holds the bytes it was queued with";

    QCoreApplication::processEvents();
    ASSERT_FALSE(painted.isNull());
    EXPECT_EQ(qGray(painted.pixel(2, 2)), 77) << "the paint reads the bytes it was queued with, not the replacement";
    EXPECT_TRUE(watched.expired()) << "the bytes go once the queued paint has run";

    std::shared_ptr<PresentedImage> again = filledBytes(4u, 4u, 1u, 5u);
    const std::weak_ptr<PresentedImage> watchedAgain = again;
    QPixmap pixmap = QPixmap::fromImage(an::ui::presentedQImage(again));
    again.reset();
    EXPECT_FALSE(pixmap.isNull());
    pixmap = QPixmap();
    QCoreApplication::processEvents();
    EXPECT_TRUE(watchedAgain.expired()) << "nothing keeps the bytes once the pixmap is gone";
}

TEST(PresentationBridgeTest, ANullPresentedImageWrapsToANullImage)
{
    EXPECT_TRUE(an::ui::presentedQImage(nullptr).isNull());
}

// A presentation takes its bytes from the engine context it runs in: while a preview is open, the bytes a view lets go
// are the ones its next presentation of that size is handed, still counted against the display allowance while they
// live, and storage for padded rows covers the last row's padding a QImage reads.
TEST(PresentationBridgeTest, BytesFromASessionsContextServeItsNextPresentationOfTheirSizeWhileAPreviewIsOpen)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    an::Result<an::consumer::SessionHold> hold = session.hold();
    ASSERT_TRUE(hold.hasValue()) << hold.status().message();
    an::Result<an::consumer::PreviewReservation> reservation = session.reservePreview();
    ASSERT_TRUE(reservation.hasValue()) << reservation.status().message();
    an::consumer::ConsumerMemory memory{an::consumer::MemoryAllowances{}};

    // What a preview view of 802 by 500 pixels paints: blue-first premultiplied bytes, a megabyte and a half.
    const an::ImageLayoutRequest wanted = byteLayout(802u, 500u, 4u);
    const std::uint8_t* first = nullptr;
    {
        an::Result<std::shared_ptr<PresentedImage>> shown =
            PresentedImage::allocate(hold->engine(), wanted, PresentedFormat::Bgra8Premultiplied, &memory);
        ASSERT_TRUE(shown.hasValue()) << shown.status().message();
        EXPECT_TRUE((*shown)->fromEngineContext());
        EXPECT_EQ(memory.counters(an::consumer::MemoryCategory::DisplayBuffers).reservedBytes, (*shown)->byteCount());
        std::fill((*shown)->data(), (*shown)->data() + (*shown)->byteCount(), std::uint8_t{0x5A});
        EXPECT_EQ(an::ui::presentedQImage(*shown).constScanLine(499)[802 * 4 - 1], 0x5Au);
        first = (*shown)->data();
    }
    EXPECT_EQ(memory.counters(an::consumer::MemoryCategory::DisplayBuffers).reservedBytes, 0u);
    an::Result<std::shared_ptr<PresentedImage>> next =
        PresentedImage::allocate(hold->engine(), wanted, PresentedFormat::Bgra8Premultiplied, &memory);
    ASSERT_TRUE(next.hasValue()) << next.status().message();
    EXPECT_EQ((*next)->data(), first) << "the next presentation of the size is handed the bytes the last one let go";
    EXPECT_GE(session.recycledMemory().reusedBlocks, 1u);

    // Grey rows of five bytes, padded to eight: the engine's layout stops at the last sample, the bytes do not.
    an::Result<std::shared_ptr<PresentedImage>> grey =
        PresentedImage::allocate(hold->engine(), byteLayout(5u, 3u, 1u), &memory);
    ASSERT_TRUE(grey.hasValue()) << grey.status().message();
    EXPECT_TRUE((*grey)->fromEngineContext());
    ASSERT_EQ((*grey)->byteCount(), 24u);
    std::fill((*grey)->data(), (*grey)->data() + (*grey)->byteCount(), std::uint8_t{9u});
    EXPECT_EQ(qGray(an::ui::presentedQImage(*grey).pixel(4, 2)), 9);
}
