// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The split comparison display module over a view: its state; the overlay image it draws over the view, on the
/// overlay's side of the split only; the splitter, its handle and the labels it draws across the whole viewport, and
/// the overlay widget that draws them there; which side of the split a point is on; and the quick-zoom lens of a view,
/// which magnifies an override in place of the view's own image and can be cancelled. The comparison window's own
/// behaviour is held by its process suite.

#include <gtest/gtest.h>

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "theme/AnDesignTokens.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"
#include "widgets/SplitComparisonDisplayModule.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{

std::filesystem::path findRepoRoot()
{
    std::filesystem::path current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (std::filesystem::exists(current / "src" / "AfterNightApp"))
        {
            return current;
        }
        if (current == current.root_path())
        {
            break;
        }
        current = current.parent_path();
    }

    return {};
}

std::string loadTextFile(const std::filesystem::path& relativePath)
{
    const auto root = findRepoRoot();
    if (root.empty())
    {
        return {};
    }

    std::ifstream file(root / relativePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

QImage buildHighFrequencyTestImage(const QSize& size)
{
    QImage image(size, QImage::Format_RGB888);
    for (int y = 0; y < size.height(); ++y)
    {
        for (int x = 0; x < size.width(); ++x)
        {
            const int red = (x * 37 + y * 17 + ((x ^ y) & 0x1f)) % 256;
            const int green = (x * 11 + y * 53 + ((x + y) & 0x0f) * 7) % 256;
            const int blue = (((x * 29) ^ (y * 41)) + (x * y) % 97) % 256;
            image.setPixelColor(x, y, QColor(red, green, blue));
        }
    }

    return image;
}

QImage renderFullImage(const QPixmap& pixmap, const QSize& targetSize)
{
    QImage canvas(targetSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(QPoint(0, 0), pixmap);

    return canvas;
}

QImage renderSplitComposite(const QImage& sourceImage, const QSize& targetSize, double scaleFactor)
{
    const QPixmap basePixmap =
        QPixmap::fromImage(sourceImage).scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QImage canvas = renderFullImage(basePixmap, targetSize);

    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(targetSize);
    viewWidget.scaleImageView(scaleFactor, true);

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setOverlayImage(sourceImage);
    module.setSplitPosition(0.5);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    module.onDraw(painter);

    return canvas;
}

int countDifferingPixels(const QImage& lhs, const QImage& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return -1;
    }

    int differingPixels = 0;
    for (int y = 0; y < lhs.height(); ++y)
    {
        for (int x = 0; x < lhs.width(); ++x)
        {
            if (lhs.pixel(x, y) != rhs.pixel(x, y))
            {
                ++differingPixels;
            }
        }
    }

    return differingPixels;
}

/// The colour a drawing case's canvas starts as, which nothing a module draws is.
constexpr QRgb kCanvas = qRgb(255, 0, 255);

/// A canvas of a size in the canvas colour.
QImage blankCanvas(const QSize& size)
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(QColor::fromRgb(kCanvas));
    return canvas;
}

/// What a module draws over its view's image, painted on a blank canvas.
QImage paintedOver(ImageViewWidgetDisplayModule& module, const QSize& size)
{
    QImage canvas = blankCanvas(size);
    QPainter painter(&canvas);
    module.onDraw(painter);
    painter.end();
    return canvas;
}

/// What a split module draws across a viewport, painted on a blank canvas the viewport's size.
QImage paintedAcross(const an::ui::SplitComparisonDisplayModule& module, const QSize& viewport)
{
    QImage canvas = blankCanvas(viewport);
    QPainter painter(&canvas);
    module.drawViewportOverlay(painter);
    painter.end();
    return canvas;
}

/// What one colour comes to drawn over another, composed as a painter composes it.
QColor composedOver(const QColor& colour, const QColor& background)
{
    QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
    pixel.fill(background);
    QPainter painter(&pixel);
    painter.fillRect(QRect(0, 0, 1, 1), colour);
    painter.end();
    return pixel.pixelColor(0, 0);
}

/// Whether two colours agree to within a rounding step in every channel.
bool closeTo(const QColor& actual, const QColor& expected)
{
    constexpr int kTolerance = 2;
    return std::abs(actual.red() - expected.red()) <= kTolerance &&
           std::abs(actual.green() - expected.green()) <= kTolerance &&
           std::abs(actual.blue() - expected.blue()) <= kTolerance &&
           std::abs(actual.alpha() - expected.alpha()) <= kTolerance;
}

/// A colour's name, for a failure message.
std::string nameOf(const QColor& colour)
{
    return colour.name(QColor::HexArgb).toStdString();
}

/// The colour a widget's painting holds at a point of the widget, whatever the painting's device pixel ratio.
QColor paintedAt(const QImage& painted, int x, int y)
{
    const qreal ratio = painted.devicePixelRatio();
    return painted.pixelColor(static_cast<int>(x * ratio), static_cast<int>(y * ratio));
}

/// An overlay image smaller than the view it is drawn over, red in its left half and blue in its right.
QImage twoToneOverlay()
{
    QImage overlay(20, 12, QImage::Format_RGB888);
    for (int y = 0; y < overlay.height(); ++y)
    {
        for (int x = 0; x < overlay.width(); ++x)
        {
            overlay.setPixelColor(x, y, x < overlay.width() / 2 ? QColor(Qt::red) : QColor(Qt::blue));
        }
    }
    return overlay;
}

/// How many pixels of a row, from a column and stepping one column either way, differ from the canvas before the first
/// that does not: the width of what was drawn there.
int runFrom(const QImage& image, int x, int y, int step)
{
    int run = 0;
    while (x >= 0 && x < image.width() && image.pixel(x, y) != kCanvas)
    {
        ++run;
        x += step;
    }
    return run;
}

/// An application context over one engine session, installed for one case and cleared when the case ends.
class InstalledViewerContext
{
public:
    InstalledViewerContext() : m_context(an::tests::viewer::openViewerContext())
    {
        if (m_context != nullptr)
        {
            m_installed.emplace(*m_context);
        }
    }

    /// Whether the session was opened.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_context != nullptr;
    }

    /// The session the context lends.
    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

private:
    std::unique_ptr<an::tests::viewer::ViewerTestContext> m_context;
    std::optional<an::tests::viewer::InstalledContext> m_installed;
};

/// A static preview showing a flat grey image, its presentation landed; null, the case failed, when it cannot be shown.
an::tests::viewer::ViewHandle flatPreview(const an::consumer::EngineSession& session, const std::string& name)
{
    an::tests::viewer::ViewHandle view = an::tests::viewer::makeView(name, ViewControlMode::st_preview);
    std::optional<an::ui::ImageDocument> document =
        an::tests::viewer::makeDocument<float>(session, 32u, 32u, an::tests::viewer::Channels::Grey,
                                               [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25f; });
    EXPECT_TRUE(document.has_value());
    if (!document.has_value())
    {
        return nullptr;
    }
    view->setDocument(std::move(*document));
    EXPECT_TRUE(an::tests::viewer::settle(*view));
    return view;
}

/// The colour of presented bytes at their first pixel, grey or colour.
QRgb colourOf(const an::ui::PresentedImage& image)
{
    const std::uint8_t* pixel = image.pixel(0u, 0u);
    return image.bytesPerPixel() >= 3u ? qRgb(pixel[0], pixel[1], pixel[2]) : qRgb(pixel[0], pixel[0], pixel[0]);
}

/// Presented bytes of one colour, as a presentation of a colour image writes them; null, the case failed, when they
/// cannot be allocated.
std::shared_ptr<an::ui::PresentedImage> presentedColour(std::uint32_t width, std::uint32_t height, QRgb colour)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 3u, an::defaultEngineLimits());
    EXPECT_TRUE(extent.has_value());
    if (!extent.has_value())
    {
        return nullptr;
    }
    an::Result<std::shared_ptr<an::ui::PresentedImage>> image =
        an::ui::PresentedImage::allocate(an::ImageLayoutRequest{.extent = *extent,
                                                                .elementSizeBytes = 1u,
                                                                .samplesPerPixel = 3u,
                                                                .planeCount = 1u,
                                                                .rowAlignmentBytes = 4u},
                                         an::defaultEngineLimits(), nullptr);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return nullptr;
    }
    const std::uint64_t stride = (*image)->layout().rowStrideBytes();
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        std::uint8_t* row = (*image)->data() + y * stride;
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            row[3u * x] = static_cast<std::uint8_t>(qRed(colour));
            row[3u * x + 1u] = static_cast<std::uint8_t>(qGreen(colour));
            row[3u * x + 2u] = static_cast<std::uint8_t>(qBlue(colour));
        }
    }
    return std::move(*image);
}

/// Sends a mouse event at a point of a widget, as the window system delivers one.
void sendMouse(QWidget& widget, QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons held, const QPoint& at)
{
    QMouseEvent event(type, QPointF(at), QPointF(widget.mapToGlobal(at)), button, held, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &event);
}

/// The quick-zoom lens a press put over a view: the one tool window among the view's own children, or null while there
/// is none.
QWidget* lensOver(const QWidget& view)
{
    const QList<QWidget*> children = view.findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    for (QWidget* child : children)
    {
        if (child->isWindow() && child->windowType() == Qt::Tool)
        {
            return child;
        }
    }
    return nullptr;
}

/// The share of what a widget paints that is exactly one opaque colour.
double shareOf(QWidget& widget, QRgb colour)
{
    const QImage painted = widget.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    if (painted.isNull())
    {
        return 0.0;
    }
    std::int64_t matching = 0;
    for (int y = 0; y < painted.height(); ++y)
    {
        for (int x = 0; x < painted.width(); ++x)
        {
            matching += painted.pixel(x, y) == colour ? 1 : 0;
        }
    }
    return static_cast<double>(matching) / (static_cast<double>(painted.width()) * painted.height());
}

} // namespace

// --- State / lifecycle tests ---

TEST(SplitComparisonDisplayModule, OverlayImageLifecycle)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_FALSE(module.hasOverlayImage()) << "Module must report no overlay image before any image is set";

    const QImage img(64, 64, QImage::Format_RGB888);
    module.setOverlayImage(img);
    EXPECT_TRUE(module.hasOverlayImage()) << "Module must report an overlay image after setOverlayImage()";

    module.clearOverlayImage();
    EXPECT_FALSE(module.hasOverlayImage()) << "Module must report no overlay image after clearOverlayImage()";
}

TEST(SplitComparisonDisplayModule, ClearImageIsIdempotent)
{
    an::ui::SplitComparisonDisplayModule module;

    ASSERT_NO_THROW(module.clearOverlayImage());
    EXPECT_FALSE(module.hasOverlayImage());

    module.clearOverlayImage();
    EXPECT_FALSE(module.hasOverlayImage());
}

TEST(SplitComparisonDisplayModule, DefaultOrientation)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_EQ(module.orientation(), an::ui::SplitOrientation::Horizontal)
        << "Default orientation should be Horizontal (vertical divider, left/right)";
}

TEST(SplitComparisonDisplayModule, OrientationCanBeChanged)
{
    an::ui::SplitComparisonDisplayModule module;

    module.setOrientation(an::ui::SplitOrientation::Vertical);
    EXPECT_EQ(module.orientation(), an::ui::SplitOrientation::Vertical);

    module.setOrientation(an::ui::SplitOrientation::Horizontal);
    EXPECT_EQ(module.orientation(), an::ui::SplitOrientation::Horizontal);
}

TEST(SplitComparisonDisplayModule, DefaultSplitPosition)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_DOUBLE_EQ(module.splitPosition(), 0.5) << "Default split position should be 0.5 (center)";
}

TEST(SplitComparisonDisplayModule, SplitPositionIsClamped)
{
    an::ui::SplitComparisonDisplayModule module;

    module.setSplitPosition(1.5);
    EXPECT_DOUBLE_EQ(module.splitPosition(), 1.0) << "Split position above 1.0 must be clamped to 1.0";

    module.setSplitPosition(-0.5);
    EXPECT_DOUBLE_EQ(module.splitPosition(), 0.0) << "Split position below 0.0 must be clamped to 0.0";

    module.setSplitPosition(0.3);
    EXPECT_DOUBLE_EQ(module.splitPosition(), 0.3) << "In-range split position must be stored unchanged";
}

TEST(SplitComparisonDisplayModule, DragStateDefaultsToFalse)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_FALSE(module.isDragging()) << "Module must not be in dragging state by default";
}

TEST(SplitComparisonDisplayModule, MouseInteractionRequiresOverlay)
{
    an::ui::SplitComparisonDisplayModule module;

    // Without an overlay image, mouse press should not start drag
    EXPECT_FALSE(module.handleMousePress(QPoint(50, 50)));
    EXPECT_FALSE(module.isDragging());
}

TEST(SplitComparisonDisplayModule, SplitterHitZoneExtendsOutsideWidgetBounds)
{
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(QSize(200, 120));

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setOverlayImage(QImage(200, 120, QImage::Format_RGB888));
    module.setSplitPosition(0.5);

    EXPECT_TRUE(module.isNearSplitter(QPoint(100, -24)))
        << "Horizontal split hit-testing should remain active above the image widget";
    EXPECT_TRUE(module.isNearSplitter(QPoint(100, 144)))
        << "Horizontal split hit-testing should remain active below the image widget";

    module.setOrientation(an::ui::SplitOrientation::Vertical);

    EXPECT_TRUE(module.isNearSplitter(QPoint(-24, 60)))
        << "Vertical split hit-testing should remain active left of the image widget";
    EXPECT_TRUE(module.isNearSplitter(QPoint(224, 60)))
        << "Vertical split hit-testing should remain active right of the image widget";
}

TEST(SplitComparisonDisplayModule, MouseReleaseWithNoDragReturnsFalse)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_FALSE(module.handleMouseRelease(QPoint(50, 50))) << "Mouse release without prior drag should return false";
}

TEST(SplitComparisonDisplayModule, RemoveViewportOverlayIsSafeAfterViewportDeletion)
{
    an::ui::SplitComparisonDisplayModule module;

    auto viewport = std::make_unique<QWidget>();
    module.installViewportOverlay(viewport.get());

    viewport.reset();

    EXPECT_NO_FATAL_FAILURE(module.removeViewportOverlay());
}

TEST(SplitComparisonDisplayModule, IdenticalImagesStayPixelMatchedWhenDownscaled)
{
    const QImage sourceImage = buildHighFrequencyTestImage(QSize(257, 193));
    const QSize targetSize(121, 91);

    const QPixmap basePixmap =
        QPixmap::fromImage(sourceImage).scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QImage expected = renderFullImage(basePixmap, targetSize);
    const QImage actual = renderSplitComposite(sourceImage, targetSize, 0.47);

    EXPECT_EQ(countDifferingPixels(expected, actual), 0)
        << "Split rendering should match the normal downscaled render when both images are identical";
}

// --- Drawing over the view ---

// The overlay image is scaled over the view's whole image area and clipped to the overlay's side of the split, so each
// side shows its own image where the view shows that part of it.
TEST(SplitComparisonDisplayModule, TheOverlayIsDrawnOverTheWholeImageAreaButOnlyPastTheSplit)
{
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(QSize(200, 120));

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setOverlayImage(twoToneOverlay());
    module.setSplitPosition(0.25);

    // The split is at x = 50 of the view.
    const QImage painted = paintedOver(module, QSize(200, 120));
    EXPECT_EQ(painted.pixel(40, 60), kCanvas) << "before the split the view's own image is left as it is";
    EXPECT_EQ(painted.pixel(60, 60), qRgb(255, 0, 0)) << "past the split the overlay's left half shows where it falls";
    EXPECT_EQ(painted.pixel(150, 60), qRgb(0, 0, 255)) << "and its right half where the view's right half is";
}

// Swapped, the overlay is drawn before the split instead; turned, the split runs across the view and the overlay is
// drawn below it, or above it once swapped.
TEST(SplitComparisonDisplayModule, SwappingOrTurningTheSplitMovesTheOverlayToTheOtherSide)
{
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(QSize(200, 120));

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setOverlayImage(twoToneOverlay());
    module.setSplitPosition(0.25);

    module.setSwapped(true);
    const QImage swapped = paintedOver(module, QSize(200, 120));
    EXPECT_EQ(swapped.pixel(40, 60), qRgb(255, 0, 0)) << "swapped, the overlay is drawn before the split";
    EXPECT_EQ(swapped.pixel(60, 60), kCanvas) << "and the view's own image is left as it is past it";
    EXPECT_EQ(swapped.pixel(150, 60), kCanvas);

    // Turned, the split is at y = 30 of the view.
    module.setSwapped(false);
    module.setOrientation(an::ui::SplitOrientation::Vertical);
    const QImage turned = paintedOver(module, QSize(200, 120));
    EXPECT_EQ(turned.pixel(60, 20), kCanvas) << "turned, the view's own image is left as it is above the split";
    EXPECT_EQ(turned.pixel(60, 40), qRgb(255, 0, 0)) << "and the overlay is drawn below it";
    EXPECT_EQ(turned.pixel(150, 40), qRgb(0, 0, 255));

    module.setSwapped(true);
    const QImage turnedAndSwapped = paintedOver(module, QSize(200, 120));
    EXPECT_EQ(turnedAndSwapped.pixel(60, 20), qRgb(255, 0, 0)) << "turned and swapped, the overlay is drawn above it";
    EXPECT_EQ(turnedAndSwapped.pixel(60, 40), kCanvas);
}

// A module with no overlay image, or attached to no view, leaves the view's image as it is.
TEST(SplitComparisonDisplayModule, NothingIsDrawnWithoutAnOverlayImageOrAView)
{
    const QSize size(200, 120);
    const QImage blank = blankCanvas(size);
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(size);

    an::ui::SplitComparisonDisplayModule unattached;
    unattached.setOverlayImage(twoToneOverlay());
    EXPECT_EQ(countDifferingPixels(paintedOver(unattached, size), blank), 0) << "a module attached to no view drew";

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    EXPECT_EQ(countDifferingPixels(paintedOver(module, size), blank), 0) << "a module with no overlay image drew";

    module.setOverlayImage(twoToneOverlay());
    EXPECT_GT(countDifferingPixels(paintedOver(module, size), blank), 0) << "with both, the overlay is drawn";

    module.clearOverlayImage();
    EXPECT_EQ(countDifferingPixels(paintedOver(module, size), blank), 0) << "a cleared overlay image is drawn no more";
}

// --- Drawing across the viewport ---

// The splitter runs from edge to edge of the viewport, beyond the view's own area, at the view's split mapped into the
// viewport, with its handle on it at the middle of the viewport.
TEST(SplitComparisonDisplayModule, TheSplitterCrossesTheWholeViewportWithItsHandleAtTheMiddle)
{
    // The view sits inside a larger viewport, as an image smaller than its window does.
    QWidget viewport;
    viewport.resize(300, 200);
    auto* viewWidget = new ImageViewWidget(nullptr);
    viewWidget->setParent(&viewport);
    viewWidget->setGeometry(50, 40, 200, 120);

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(viewWidget);
    module.setSplitPosition(0.5);

    const auto& colours = an::ui::tokens().colors;
    const QColor line = composedOver(colours.textPrimary, QColor::fromRgb(kCanvas));
    const QColor handle = composedOver(colours.bgSurface, line);

    // Half way into the view is x = 150 of the viewport.
    const QImage across = paintedAcross(module, viewport.size());
    EXPECT_TRUE(closeTo(across.pixelColor(150, 5), line)) << "above the view: " << nameOf(across.pixelColor(150, 5));
    EXPECT_TRUE(closeTo(across.pixelColor(150, 195), line)) << "below it: " << nameOf(across.pixelColor(150, 195));
    EXPECT_TRUE(closeTo(across.pixelColor(150, 105), handle))
        << "the handle on the splitter at the viewport's middle: " << nameOf(across.pixelColor(150, 105));
    EXPECT_EQ(across.pixel(140, 5), kCanvas) << "nothing is drawn beside the splitter";
    EXPECT_EQ(across.pixel(160, 195), kCanvas);

    // Turned, half way down the view is y = 100 of the viewport.
    module.setOrientation(an::ui::SplitOrientation::Vertical);
    const QImage down = paintedAcross(module, viewport.size());
    EXPECT_TRUE(closeTo(down.pixelColor(5, 100), line)) << "left of the view: " << nameOf(down.pixelColor(5, 100));
    EXPECT_TRUE(closeTo(down.pixelColor(295, 100), line)) << "right of it: " << nameOf(down.pixelColor(295, 100));
    EXPECT_TRUE(closeTo(down.pixelColor(155, 100), handle))
        << "the handle at the viewport's middle: " << nameOf(down.pixelColor(155, 100));
    EXPECT_EQ(down.pixel(5, 90), kCanvas) << "nothing is drawn beside the splitter";
}

// With labels, each is drawn on a backdrop in a corner of its own side: the first at the top left, the second at the
// top right, or at the bottom left once the split is turned. Swapped, the labels change sides with their images.
TEST(SplitComparisonDisplayModule, EachLabelIsDrawnOnABackdropInACornerOfItsOwnSide)
{
    const QSize viewport(480, 200);
    QWidget container;
    container.resize(viewport);
    auto* viewWidget = new ImageViewWidget(nullptr);
    viewWidget->setParent(&container);
    viewWidget->setGeometry(QRect(QPoint(0, 0), viewport));

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(viewWidget);

    const QImage unlabelled = paintedAcross(module, viewport);
    EXPECT_EQ(unlabelled.pixel(17, 17), kCanvas) << "with no labels, nothing is drawn in the corners";
    EXPECT_EQ(unlabelled.pixel(462, 17), kCanvas);

    // The base image's label is short and the overlay's long, so the width of a backdrop says whose label it holds.
    module.setLabels(QStringLiteral("A"), QStringLiteral("OVERLAY"));
    const QImage labelled = paintedAcross(module, viewport);
    EXPECT_NE(labelled.pixel(17, 17), kCanvas) << "the first side's label sits at the top left";
    EXPECT_NE(labelled.pixel(462, 17), kCanvas) << "the second side's label sits at the top right";
    EXPECT_EQ(labelled.pixel(17, 182), kCanvas) << "nothing sits at the bottom left";
    EXPECT_LT(runFrom(labelled, 15, 17, 1), runFrom(labelled, 463, 17, -1))
        << "the base image's label is on the first side and the overlay's on the second";

    module.setSwapped(true);
    const QImage swapped = paintedAcross(module, viewport);
    EXPECT_GT(runFrom(swapped, 15, 17, 1), runFrom(swapped, 463, 17, -1))
        << "swapped, the overlay's label is on the first side and the base image's on the second";

    module.setSwapped(false);
    module.setOrientation(an::ui::SplitOrientation::Vertical);
    const QImage turned = paintedAcross(module, viewport);
    EXPECT_NE(turned.pixel(17, 17), kCanvas) << "turned, the first side's label stays at the top left";
    EXPECT_NE(turned.pixel(17, 182), kCanvas) << "and the second side's sits at the bottom left";
    EXPECT_EQ(turned.pixel(462, 17), kCanvas) << "with nothing at the top right";
}

// Installed on a viewport, the overlay is one widget over all of it that leaves every press to the widget beneath it,
// and draws the splitter at the view's split while the module has an overlay image to compare.
TEST(SplitComparisonDisplayModule, TheViewportOverlayCoversTheViewportLeavesPressesBeneathAndDrawsTheSplitter)
{
    QWidget viewport;
    viewport.resize(300, 200);
    auto* beneath = new QWidget(&viewport);
    beneath->setGeometry(viewport.rect());
    beneath->show();
    // The view inside the viewport is never painted here, so it stays hidden.
    auto* viewWidget = new ImageViewWidget(nullptr);
    viewWidget->setParent(&viewport);
    viewWidget->setGeometry(50, 40, 200, 120);
    viewWidget->hide();

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(viewWidget);
    module.setSplitPosition(0.5);
    module.installViewportOverlay(&viewport);

    QWidget* overlay = nullptr;
    for (QWidget* child : viewport.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
    {
        overlay = child != beneath && child != viewWidget ? child : overlay;
    }
    ASSERT_NE(overlay, nullptr) << "installing puts a widget over the viewport";
    EXPECT_EQ(overlay->geometry(), viewport.rect());
    EXPECT_EQ(viewport.childAt(QPoint(150, 100)), beneath) << "a press over the overlay reaches the widget beneath it";

    const QImage withoutImage = viewport.grab().toImage();
    const QColor background = paintedAt(withoutImage, 5, 190);
    EXPECT_EQ(paintedAt(withoutImage, 150, 10), background) << "with nothing to compare, the overlay draws nothing";

    module.setOverlayImage(twoToneOverlay());
    const QImage withImage = viewport.grab().toImage();
    const QColor line = composedOver(an::ui::tokens().colors.textPrimary, background);
    EXPECT_TRUE(closeTo(paintedAt(withImage, 150, 10), line))
        << "the overlay draws the splitter at the view's split: " << nameOf(paintedAt(withImage, 150, 10));
    EXPECT_EQ(paintedAt(withImage, 140, 10), background) << "and nothing beside it";

    module.removeViewportOverlay();
    EXPECT_EQ(viewport.findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size(), 2) << "removing takes it away";
}

// The overlay follows every resize of its viewport, so the splitter and the labels always span what is visible.
TEST(SplitComparisonDisplayModule, TheViewportOverlayFollowsEveryResizeOfTheViewport)
{
    QWidget viewport;
    viewport.setAttribute(Qt::WA_DontShowOnScreen);
    viewport.resize(300, 200);

    an::ui::SplitComparisonDisplayModule module;
    module.installViewportOverlay(&viewport);
    viewport.show();

    const QList<QWidget*> children = viewport.findChildren<QWidget*>(Qt::FindDirectChildrenOnly);
    ASSERT_EQ(children.size(), 1) << "installing puts one widget over the viewport";
    QPointer<QWidget> overlay = children.front();
    EXPECT_EQ(overlay->geometry(), QRect(0, 0, 300, 200));

    viewport.resize(480, 260);
    EXPECT_EQ(overlay->geometry(), QRect(0, 0, 480, 260)) << "a viewport that grows is still covered whole";
    viewport.resize(160, 120);
    EXPECT_EQ(overlay->geometry(), QRect(0, 0, 160, 120)) << "and one that shrinks is not overhung";

    module.removeViewportOverlay();
    EXPECT_TRUE(overlay.isNull()) << "removing destroys the overlay";
    viewport.resize(200, 150);
    EXPECT_TRUE(viewport.findChildren<QWidget*>().isEmpty()) << "and a later resize finds nothing to follow it";
}

// --- Source verification of the comparison process ---

// --- Swapped state tests ---

TEST(SplitComparisonDisplayModule, DefaultSwappedIsFalse)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_FALSE(module.isSwapped()) << "Default swapped state should be false";
}

TEST(SplitComparisonDisplayModule, SwappedCanBeToggled)
{
    an::ui::SplitComparisonDisplayModule module;

    module.setSwapped(true);
    EXPECT_TRUE(module.isSwapped());

    module.setSwapped(false);
    EXPECT_FALSE(module.isSwapped());
}

TEST(SplitComparisonDisplayModule, LabelsCanBeSet)
{
    an::ui::SplitComparisonDisplayModule module;

    module.setLabels(QStringLiteral("IMAGE 1"), QStringLiteral("IMAGE 2"));
    // Labels are stored internally — no getter, but verify no crash
    SUCCEED();
}

// --- Source verification of the comparison process's split interaction ---

// --- isPointOnOverlaySide tests ---

TEST(SplitComparisonDisplayModule, IsPointOnOverlaySideReturnsFalseWithoutWidget)
{
    an::ui::SplitComparisonDisplayModule module;

    EXPECT_FALSE(module.isPointOnOverlaySide(QPoint(50, 50)))
        << "isPointOnOverlaySide must return false when no widget is attached";
}

// A point is on the overlay's side past the split, or before it once the images are swapped; turned, past the split is
// below it.
TEST(SplitComparisonDisplayModule, APointIsOnTheOverlaySidePastTheSplitOrBeforeItOnceSwapped)
{
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(QSize(200, 120));

    an::ui::SplitComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setSplitPosition(0.25);

    // The split is at x = 50 of the view.
    EXPECT_FALSE(module.isPointOnOverlaySide(QPoint(40, 60))) << "before the split is the base image's side";
    EXPECT_TRUE(module.isPointOnOverlaySide(QPoint(60, 60))) << "past it is the overlay's";
    EXPECT_TRUE(module.isPointOnOverlaySide(QPoint(150, 60)));

    module.setSwapped(true);
    EXPECT_TRUE(module.isPointOnOverlaySide(QPoint(40, 60))) << "swapped, before the split is the overlay's side";
    EXPECT_FALSE(module.isPointOnOverlaySide(QPoint(60, 60)));

    // Turned, the split is at y = 30 of the view.
    module.setSwapped(false);
    module.setOrientation(an::ui::SplitOrientation::Vertical);
    EXPECT_FALSE(module.isPointOnOverlaySide(QPoint(150, 20))) << "turned, above the split is the base image's side";
    EXPECT_TRUE(module.isPointOnOverlaySide(QPoint(150, 40))) << "and below it the overlay's";

    module.setSwapped(true);
    EXPECT_TRUE(module.isPointOnOverlaySide(QPoint(150, 20))) << "turned and swapped, above it is the overlay's";
    EXPECT_FALSE(module.isPointOnOverlaySide(QPoint(150, 40)));
}

// --- Switch button and lens source verification tests ---

// --- The quick-zoom lens ---

// A press puts the lens over the view magnifying the bytes set in place of the view's own, until the override is
// cleared and it magnifies the view's image again.
TEST(SplitComparisonDisplayModule, TheLensMagnifiesAnOverrideInPlaceOfTheViewsImageUntilItIsCleared)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle view = flatPreview(installed.session(), "lens");
    ASSERT_NE(view, nullptr);
    ImageViewWidget* widget = view->getViewWidget();
    ASSERT_NE(widget->presentedImage(), nullptr);
    const QRgb shown = colourOf(*widget->presentedImage());
    const QRgb overriding = qRgb(0, 200, 0);
    ASSERT_NE(shown, overriding);
    const std::shared_ptr<an::ui::PresentedImage> replacement = presentedColour(32u, 32u, overriding);
    ASSERT_NE(replacement, nullptr);

    widget->setQuickZoomImageOverride(replacement);
    sendMouse(*widget, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, QPoint(8, 8));
    QWidget* lens = lensOver(*view);
    ASSERT_NE(lens, nullptr) << "a press puts the lens over the view";
    EXPECT_GT(shareOf(*lens, overriding), 0.5) << "the lens magnifies the override";
    EXPECT_LT(shareOf(*lens, shown), 0.5) << "and not the view's own image";
    sendMouse(*widget, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, QPoint(8, 8));

    widget->clearQuickZoomImageOverride();
    sendMouse(*widget, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, QPoint(8, 8));
    lens = lensOver(*view);
    ASSERT_NE(lens, nullptr);
    EXPECT_GT(shareOf(*lens, shown), 0.5) << "once the override is cleared, the lens magnifies the view's image";
    EXPECT_LT(shareOf(*lens, overriding), 0.5);
    sendMouse(*widget, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, QPoint(8, 8));
}

// Cancelling the quick zoom hides the lens and the view lets it go, and a lens still waiting for its delay never shows,
// even when the cursor moves on.
TEST(SplitComparisonDisplayModule, CancellingTheQuickZoomHidesTheLensAndKeepsAWaitingOneFromShowing)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle view = flatPreview(installed.session(), "cancelled");
    ASSERT_NE(view, nullptr);
    ImageViewWidget* widget = view->getViewWidget();
    widget->setQuickZoomDelayMs(1);

    sendMouse(*widget, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, QPoint(8, 8));
    const QPointer<QWidget> lens = lensOver(*view);
    ASSERT_FALSE(lens.isNull()) << "a press puts the lens over the view";
    ASSERT_TRUE(an::tests::viewer::pumpUntil([&lens]() { return !lens.isNull() && lens->isVisible(); }))
        << "the lens shows once its delay has passed";

    widget->cancelQuickZoom();
    ASSERT_FALSE(lens.isNull());
    EXPECT_FALSE(lens->isVisible()) << "a cancelled lens is hidden";
    EXPECT_EQ(lensOver(*view), nullptr) << "and the view lets it go";

    sendMouse(*widget, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, QPoint(8, 8));
    widget->cancelQuickZoom();
    an::tests::viewer::pumpQuietly();
    sendMouse(*widget, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, QPoint(10, 10));
    an::tests::viewer::pumpQuietly();
    ASSERT_FALSE(lens.isNull());
    EXPECT_FALSE(lens->isVisible()) << "a lens cancelled before its delay never shows, even as the cursor moves on";
    sendMouse(*widget, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, QPoint(10, 10));
}
