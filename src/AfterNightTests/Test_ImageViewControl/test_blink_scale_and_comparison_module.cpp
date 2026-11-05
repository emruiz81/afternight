// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a comparison of two views relies on in each view: a listener hearing every zoom that changes its scale, its
/// viewport offset as the position it is scrolled to, a static preview's compact minimum size and its toolbar laid out
/// above its image; and the comparison display module, which blends an overlay over the view's image at its opacity.

#include <gtest/gtest.h>

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"
#include "widgets/ComparisonDisplayModule.h"
#include "widgets/ImageViewWidget.h"

#include <QColor>
#include <QImage>
#include <QLayout>
#include <QPainter>
#include <QPoint>
#include <QScrollBar>
#include <QSize>
#include <QToolBar>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace
{

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

/// Keeps every view a listener hears was scaled, in the order it heard them.
class ScaleRecorder final : public ImageViewControlListener
{
public:
    explicit ScaleRecorder(ImageViewControl* view) : ImageViewControlListener(view) {}

    void onImageViewScaled(ImageViewControl* view) override
    {
        scaled.push_back(view);
    }

    /// The view each notification named.
    std::vector<ImageViewControl*> scaled;
};

/// What a module draws over its view's image, painted on an opaque black canvas.
QImage paintedOverBlack(ImageViewWidgetDisplayModule& module, const QSize& size)
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::black);
    QPainter painter(&canvas);
    module.onDraw(painter);
    painter.end();
    return canvas;
}

} // namespace

// A listener hears every zoom that changes the view's scale, whether the widget or the view's own zoom action makes it,
// and none that leaves the scale as it was.
TEST(BlinkScaleAndComparisonModule, AListenerHearsEveryZoomThatChangesTheViewsScaleAndNoOther)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle view = an::tests::viewer::makeView("scaled", ViewControlMode::st_preview);
    ScaleRecorder recorder(view.get());

    view->getViewWidget()->scaleImageView(2.0, true);
    ASSERT_EQ(recorder.scaled.size(), 1u) << "a zoom to twice the size is heard";
    EXPECT_EQ(recorder.scaled.front(), view.get()) << "naming the view that was zoomed";
    EXPECT_FLOAT_EQ(view->getCurrentZoom(), 200.0f);

    view->getViewWidget()->scaleImageView(2.0, true);
    EXPECT_EQ(recorder.scaled.size(), 1u) << "a zoom to the scale already shown tells nobody";

    view->zoom100();
    ASSERT_EQ(recorder.scaled.size(), 2u) << "the view's own zoom action is heard too";
    EXPECT_EQ(recorder.scaled.back(), view.get());
    EXPECT_FLOAT_EQ(view->getCurrentZoom(), 100.0f);
}

// The viewport offset is where the view is scrolled to over its image: setting it scrolls the view, a scroll is read
// back as it, and an offset past the image stops at its far edge.
TEST(BlinkScaleAndComparisonModule, TheViewportOffsetIsWhereTheViewIsScrolledOverItsImage)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle view = an::tests::viewer::makeView("panned", ViewControlMode::st_preview);
    view->setAttribute(Qt::WA_DontShowOnScreen);
    view->show();
    std::optional<an::ui::ImageDocument> document = an::tests::viewer::makeDocument<float>(
        installed.session(), 64u, 64u, an::tests::viewer::Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + y) / 128.0f; });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(an::tests::viewer::settle(*view));

    // Sixteen times its size, the image overflows the view both ways.
    view->getViewWidget()->scaleImageView(16.0, true);
    an::tests::viewer::pumpQuietly();
    QScrollBar* horizontal = view->getScrollArea()->horizontalScrollBar();
    QScrollBar* vertical = view->getScrollArea()->verticalScrollBar();
    ASSERT_GT(horizontal->maximum(), 100);
    ASSERT_GT(vertical->maximum(), 100);

    view->setViewportOffset(QPoint(40, 30));
    EXPECT_EQ(horizontal->value(), 40) << "setting the offset scrolls the view";
    EXPECT_EQ(vertical->value(), 30);
    EXPECT_EQ(view->getViewportOffset(), QPoint(40, 30));

    vertical->setValue(55);
    EXPECT_EQ(view->getViewportOffset(), QPoint(40, 55)) << "a scroll of the view is read back as its offset";

    view->setViewportOffset(QPoint(1000000, 1000000));
    EXPECT_EQ(view->getViewportOffset(), QPoint(horizontal->maximum(), vertical->maximum()))
        << "an offset past the image stops at its far edge";
}

// A static preview fits two side by side in a dialog: it asks for far less room than a standard view does.
TEST(BlinkScaleAndComparisonModule, AStaticPreviewAsksForLessRoomThanAStandardView)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle preview = an::tests::viewer::makeView("preview", ViewControlMode::st_preview);
    an::tests::viewer::ViewHandle standard = an::tests::viewer::makeView("standard");

    EXPECT_EQ(preview->minimumSize(), QSize(240, 180));
    EXPECT_EQ(standard->minimumSize(), QSize(600, 400));
}

// A static preview lays its toolbar out as the first row of its own layout, above its image, where a standard view
// makes it the layout's menu bar.
TEST(BlinkScaleAndComparisonModule, AStaticPreviewLaysItsToolbarOutAboveItsImageRatherThanAsAMenuBar)
{
    InstalledViewerContext installed;
    ASSERT_TRUE(installed.valid());
    an::tests::viewer::ViewHandle preview = an::tests::viewer::makeView("preview", ViewControlMode::st_preview);
    auto* toolbar = preview->findChild<QToolBar*>(QStringLiteral("anImageViewToolbar"));
    ASSERT_NE(toolbar, nullptr);
    EXPECT_EQ(toolbar->parentWidget(), preview.get()) << "the toolbar belongs to its preview";
    QLayout* layout = preview->layout();
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->menuBar(), nullptr) << "a static preview's toolbar is no menu bar";
    EXPECT_EQ(layout->indexOf(toolbar), 0) << "it is the first row of the preview's layout";
    EXPECT_GT(layout->indexOf(preview->getScrollArea()), 0) << "above the image";

    an::tests::viewer::ViewHandle standard = an::tests::viewer::makeView("standard");
    auto* menuBar = standard->findChild<QToolBar*>(QStringLiteral("anImageViewToolbar"));
    ASSERT_NE(menuBar, nullptr);
    ASSERT_NE(standard->layout(), nullptr);
    EXPECT_EQ(standard->layout()->menuBar(), menuBar) << "a standard view's toolbar is its layout's menu bar";
    EXPECT_EQ(standard->layout()->indexOf(menuBar), -1);
}

TEST(BlinkScaleAndComparisonModule, ComparisonDisplayModuleOpacityIsClamped)
{
    an::ui::ComparisonDisplayModule module;

    module.setOverlayOpacity(2.5);
    EXPECT_DOUBLE_EQ(module.overlayOpacity(), 1.0) << "Opacity above 1.0 must be clamped to 1.0";

    module.setOverlayOpacity(-0.5);
    EXPECT_DOUBLE_EQ(module.overlayOpacity(), 0.0) << "Opacity below 0.0 must be clamped to 0.0";

    module.setOverlayOpacity(0.75);
    EXPECT_DOUBLE_EQ(module.overlayOpacity(), 0.75) << "In-range opacity must be stored unchanged";
}

TEST(BlinkScaleAndComparisonModule, ComparisonDisplayModuleOverlayImageLifecycle)
{
    an::ui::ComparisonDisplayModule module;

    EXPECT_FALSE(module.hasOverlayImage()) << "Module must report no overlay image before any image is set";

    const QImage img(4, 4, QImage::Format_ARGB32);
    module.setOverlayImage(img);
    EXPECT_TRUE(module.hasOverlayImage()) << "Module must report an overlay image after setOverlayImage()";

    module.clearOverlayImage();
    EXPECT_FALSE(module.hasOverlayImage()) << "Module must report no overlay image after clearOverlayImage()";
}

TEST(BlinkScaleAndComparisonModule, ComparisonDisplayModuleClearImageIsIdempotent)
{
    an::ui::ComparisonDisplayModule module;

    // clearOverlayImage() on a null image must not crash (exercises early-return path)
    ASSERT_NO_THROW(module.clearOverlayImage());
    EXPECT_FALSE(module.hasOverlayImage());

    module.clearOverlayImage(); // second call also safe
    EXPECT_FALSE(module.hasOverlayImage());
}

// The overlay is blended over the view's whole image area, and nothing beyond it, at the module's opacity; a module
// with no overlay image or no view draws nothing.
TEST(BlinkScaleAndComparisonModule, ComparisonDisplayModuleBlendsItsOverlayOverTheImageAreaAtItsOpacity)
{
    ImageViewWidget viewWidget(nullptr);
    viewWidget.resize(QSize(200, 120));
    const QSize canvas(240, 160);
    QImage white(4, 4, QImage::Format_ARGB32);
    white.fill(Qt::white);
    constexpr QRgb black = qRgb(0, 0, 0);

    an::ui::ComparisonDisplayModule unattached;
    unattached.setOverlayImage(white);
    unattached.setOverlayOpacity(1.0);
    EXPECT_EQ(paintedOverBlack(unattached, canvas).pixel(100, 60), black) << "a module attached to no view drew";

    an::ui::ComparisonDisplayModule module;
    module.attachToViewWidget(&viewWidget);
    module.setOverlayOpacity(1.0);
    EXPECT_EQ(paintedOverBlack(module, canvas).pixel(100, 60), black) << "a module with no overlay image drew";

    module.setOverlayImage(white);
    const QImage opaque = paintedOverBlack(module, canvas);
    EXPECT_EQ(opaque.pixel(0, 0), qRgb(255, 255, 255)) << "an opaque overlay covers the whole image area";
    EXPECT_EQ(opaque.pixel(199, 119), qRgb(255, 255, 255));
    EXPECT_EQ(opaque.pixel(220, 140), black) << "and nothing beyond it";

    module.setOverlayOpacity(0.5);
    const QImage half = paintedOverBlack(module, canvas);
    EXPECT_NEAR(qRed(half.pixel(100, 60)), 128, 2) << "half opaque, it is blended half way over the image";
    EXPECT_NEAR(qGreen(half.pixel(100, 60)), 128, 2);
    EXPECT_NEAR(qBlue(half.pixel(100, 60)), 128, 2);

    module.setOverlayOpacity(0.0);
    EXPECT_EQ(paintedOverBlack(module, canvas).pixel(100, 60), black) << "fully transparent, it shows nothing";
}
