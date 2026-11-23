// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The shapes an extension's session draws over a view, painted by the view's shape overlay: a polygon, an open line, a
/// filled polygon and a label lie where their points lie in the image at every zoom and after a scroll; a shape taken
/// away is gone and an owner's shapes taken away draw nothing; under Night they hold no green and no blue; a click over
/// a shape reaches the view's tool; and the shapes go with the session that drew them and with the view.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/ExtensionViewRequests.h"
#include "controls/ImageCropController.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ShapeOverlayDisplayModule.h"
#include "controls/ViewShapes.h"
#include "fixtures/ScopedNightMode.h"
#include "widgets/ImageViewWidget.h"

#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QMouseEvent>
#include <QScrollBar>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::viewer;

constexpr std::uint32_t kImageWidth = 400u;
constexpr std::uint32_t kImageHeight = 300u;

/// The shapes the cases draw: a polygon's outline in red, an open line in green, a polygon filled in blue, and a label
/// in white beside a point.
[[nodiscard]] std::shared_ptr<an::ui::ViewShapes> caseShapes()
{
    auto shapes = std::make_shared<an::ui::ViewShapes>();
    const auto add = [&shapes](std::vector<QPointF> points, const bool closed, const bool filled, const QColor color,
                               const QString& label = {}) {
        an::ui::ViewShape shape;
        shape.points = std::move(points);
        shape.closed = closed;
        shape.filled = filled;
        shape.color = color;
        shape.label = label;
        shape.bounds = an::ui::boundsOfPoints(shape.points);
        shapes->shapes.push_back(std::move(shape));
    };
    add({QPointF(40.0, 40.0), QPointF(120.0, 40.0), QPointF(120.0, 100.0), QPointF(40.0, 100.0)}, true, false,
        QColor(255, 0, 0));
    add({QPointF(200.0, 40.0), QPointF(360.0, 40.0)}, false, false, QColor(0, 255, 0));
    add({QPointF(200.0, 160.0), QPointF(300.0, 160.0), QPointF(300.0, 240.0), QPointF(200.0, 240.0)}, true, true,
        QColor(0, 0, 255));
    add({QPointF(60.0, 220.0)}, false, false, QColor(255, 255, 255), QStringLiteral("M42"));
    return shapes;
}

/// The brightest pixel of each channel around a point of a picture, in its device pixels.
struct Around
{
    int red = 0;
    int green = 0;
    int blue = 0;
};

[[nodiscard]] Around around(const QImage& picture, const QPointF& logical, const int reach = 2)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_ARGB32);
    const double ratio = picture.devicePixelRatio();
    const int cx = static_cast<int>(logical.x() * ratio);
    const int cy = static_cast<int>(logical.y() * ratio);
    Around found;
    for (int y = cy - reach; y <= cy + reach; ++y)
    {
        for (int x = cx - reach; x <= cx + reach; ++x)
        {
            if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height())
            {
                continue;
            }
            const QRgb pixel = pixels.pixel(x, y);
            found.red = (std::max)(found.red, qRed(pixel));
            found.green = (std::max)(found.green, qGreen(pixel));
            found.blue = (std::max)(found.blue, qBlue(pixel));
        }
    }
    return found;
}

/// Whether some pixel of a part of a picture is bright in every channel.
[[nodiscard]] bool brightIn(const QImage& picture, const QRectF& logical)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_ARGB32);
    const double ratio = picture.devicePixelRatio();
    for (int y = static_cast<int>(logical.top() * ratio); y < static_cast<int>(logical.bottom() * ratio); ++y)
    {
        for (int x = static_cast<int>(logical.left() * ratio); x < static_cast<int>(logical.right() * ratio); ++x)
        {
            if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height())
            {
                continue;
            }
            const QRgb pixel = pixels.pixel(x, y);
            if (qRed(pixel) > 128 && qGreen(pixel) > 128 && qBlue(pixel) > 128)
            {
                return true;
            }
        }
    }
    return false;
}

/// Whether every pixel of a picture is red alone.
[[nodiscard]] bool redAlone(const QImage& picture)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb pixel = pixels.pixel(x, y);
            if (qGreen(pixel) != 0 || qBlue(pixel) != 0)
            {
                return false;
            }
        }
    }
    return true;
}

/// A selector the cases' crop tool selects for: it counts what the tool tells it.
class CountingSelector final : public ImageAreaSelectorControl
{
public:
    void selectionAreaUpdated() override
    {
        ++updated;
    }
    void selectionAreaChanged() override
    {
        ++changed;
    }
    void newSelectionFinished() override
    {
        ++finished;
    }

    int updated = 0;
    int changed = 0;
    int finished = 0;
};

class ShapeOverlay : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    /// A view of a black image, shown in its linear form so nothing but the shapes is bright.
    [[nodiscard]] ViewHandle blackView(const std::string& name)
    {
        ViewHandle view = makeView(name);
        std::optional<an::ui::ImageDocument> document =
            makeDocument<float>(m_context->engineSession(), kImageWidth, kImageHeight, Channels::Rgb,
                                [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.0f; });
        EXPECT_TRUE(document.has_value());
        if (!document.has_value())
        {
            return view;
        }
        view->setDocument(std::move(*document));
        view->forceLinearMode();
        view->resize(640, 480);
        view->show();
        view->getViewWidget()->scaleImageView(1.0, true);
        EXPECT_TRUE(settle(*view));
        return view;
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

// Each shape is drawn where its points lie in the image, at every zoom: the polygon's outline and not its inside, the
// line, the filled polygon's inside, the label beside its point; nothing where no shape lies.
TEST_F(ShapeOverlay, ShapesLieWhereTheirPointsLieAtThreeZooms)
{
    ViewHandle view = blackView("zooms");
    ImageViewWidget* const widget = view->getViewWidget();
    view->setOwnedShapes("session", caseShapes());

    for (const double scale : {0.5, 1.0, 2.0})
    {
        SCOPED_TRACE(scale);
        widget->scaleImageView(scale, true);
        ASSERT_TRUE(settle(*view));
        const QImage shot = widget->grab().toImage();
        const auto at = [&shot, scale](const double x, const double y) {
            return around(shot, QPointF(x * scale, y * scale));
        };

        const Around outline = at(80.0, 40.0);
        EXPECT_GT(outline.red, 100) << "the polygon's outline";
        EXPECT_LT(outline.green, 40);
        EXPECT_LT(outline.blue, 40);
        const Around inside = at(80.0, 70.0);
        EXPECT_LT(inside.red, 40) << "an outline polygon is not filled";

        const Around line = at(280.0, 40.0);
        EXPECT_GT(line.green, 100) << "the line";
        EXPECT_LT(line.red, 40);
        EXPECT_LT(at(280.0, 70.0).green, 40) << "the line is drawn nowhere else";

        const Around filled = at(250.0, 200.0);
        EXPECT_GT(filled.blue, 200) << "the filled polygon's inside";
        EXPECT_LT(filled.red, 40);

        EXPECT_TRUE(brightIn(shot, QRectF(60.0 * scale + 3.5, 220.0 * scale, 60.0, 30.0)))
            << "the label beside its point";
        const Around empty = at(150.0, 280.0);
        EXPECT_LT((std::max)({empty.red, empty.green, empty.blue}), 40) << "where no shape lies";
    }
}

// A scroll moves the shapes with the image: in the view's viewport each lies where its point is now shown.
TEST_F(ShapeOverlay, ShapesFollowTheImageWhenItScrolls)
{
    ViewHandle view = blackView("scroll");
    ImageViewWidget* const widget = view->getViewWidget();
    view->setOwnedShapes("session", caseShapes());
    widget->scaleImageView(2.0, true);
    ASSERT_TRUE(settle(*view));
    QWidget* const viewport = view->getScrollArea()->viewport();

    for (const QPoint offset : {QPoint(0, 0), QPoint(120, 40)})
    {
        SCOPED_TRACE(offset.x());
        view->setViewportOffset(offset);
        ASSERT_TRUE(settle(*view));
        ASSERT_EQ(view->getViewportOffset(), offset) << "the view does not scroll that far";
        const QImage shot = viewport->grab().toImage();
        const auto shownAt = [widget, viewport](const double x, const double y) {
            return QPointF(widget->mapTo(viewport, QPointF(x * 2.0, y * 2.0)));
        };
        EXPECT_GT(around(shot, shownAt(80.0, 40.0)).red, 100) << "the polygon's outline, scrolled";
        EXPECT_GT(around(shot, shownAt(280.0, 40.0)).green, 100) << "the line, scrolled";
        EXPECT_GT(around(shot, shownAt(250.0, 200.0)).blue, 200) << "the filled polygon, scrolled";
    }
}

// A shape taken away is gone and the others stay; an owner's shapes taken away draw nothing; another owner's shapes are
// drawn apart from the first's.
TEST_F(ShapeOverlay, AShapeTakenAwayIsGoneAndShapesTakenAwayDrawNothing)
{
    ViewHandle view = blackView("removed");
    ImageViewWidget* const widget = view->getViewWidget();
    std::shared_ptr<an::ui::ViewShapes> shapes = caseShapes();
    view->setOwnedShapes("session", shapes);
    ASSERT_TRUE(settle(*view));
    EXPECT_GT(around(widget->grab().toImage(), QPointF(280.0, 40.0)).green, 100);

    auto fewer = std::make_shared<an::ui::ViewShapes>(*shapes);
    fewer->shapes.erase(fewer->shapes.begin() + 1);
    view->setOwnedShapes("session", fewer);
    ASSERT_TRUE(settle(*view));
    QImage shot = widget->grab().toImage();
    EXPECT_LT(around(shot, QPointF(280.0, 40.0)).green, 40) << "the line was taken away";
    EXPECT_GT(around(shot, QPointF(80.0, 40.0)).red, 100) << "the polygon stays";

    auto other = std::make_shared<an::ui::ViewShapes>();
    other->shapes.push_back(shapes->shapes[1]);
    view->setOwnedShapes("another session", other);
    view->setOwnedShapes("session", nullptr);
    ASSERT_TRUE(settle(*view));
    shot = widget->grab().toImage();
    EXPECT_LT(around(shot, QPointF(80.0, 40.0)).red, 40) << "the first owner's shapes are all gone";
    EXPECT_GT(around(shot, QPointF(280.0, 40.0)).green, 100) << "the other owner's line is its own";

    view->setOwnedShapes("another session", nullptr);
    ASSERT_TRUE(settle(*view));
    shot = widget->grab().toImage();
    for (const QPointF& point : {QPointF(80.0, 40.0), QPointF(280.0, 40.0), QPointF(250.0, 200.0)})
    {
        const Around found = around(shot, point);
        EXPECT_LT((std::max)({found.red, found.green, found.blue}), 40) << "an overlay with no shape draws nothing";
    }
}

// Under Night every shape is drawn in its Night form: the whole view holds no green and no blue, and the shapes are
// still drawn.
TEST_F(ShapeOverlay, UnderNightTheShapesHoldNoGreenAndNoBlue)
{
    ViewHandle view = blackView("night");
    ImageViewWidget* const widget = view->getViewWidget();
    view->setOwnedShapes("session", caseShapes());
    const an::ui::testing::ScopedNightMode night(true);
    QEvent changed(QEvent::PaletteChange);
    QCoreApplication::sendEvent(widget, &changed);
    ASSERT_TRUE(settle(*view));

    const QImage shot = widget->grab().toImage();
    EXPECT_TRUE(redAlone(shot)) << "a shape is drawn with green or blue under Night";
    EXPECT_GT(around(shot, QPointF(280.0, 40.0)).red, 60) << "the green line is drawn, in red";
    EXPECT_GT(around(shot, QPointF(250.0, 200.0)).red, 0) << "the blue polygon is drawn, in red";
}

// The overlay takes no click: a crop tool started on the view draws its area by a drag that begins over a shape, and
// the view still offers its menu.
TEST_F(ShapeOverlay, AClickOverAShapeReachesTheViewsTool)
{
    ViewHandle view = blackView("tool");
    ImageViewWidget* const widget = view->getViewWidget();
    view->setOwnedShapes("session", caseShapes());
    ASSERT_TRUE(settle(*view));
    EXPECT_TRUE(widget->isContextMenuSupported()) << "the shapes take the view's menu away";

    CountingSelector selector;
    ImageCropController tool(view->getScrollArea(), &selector);
    tool.setListenerEnabled(true);
    tool.startNewSelection();
    const auto send = [widget](const QEvent::Type type, const QPointF at, const Qt::MouseButton button,
                               const Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &event);
    };
    // The drag starts on the polygon's outline and ends inside the filled polygon.
    send(QEvent::MouseButtonPress, QPointF(80.0, 40.0), Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, QPointF(160.0, 120.0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseMove, QPointF(250.0, 200.0), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, QPointF(250.0, 200.0), Qt::LeftButton, Qt::NoButton);

    EXPECT_EQ(selector.finished, 1) << "the drag did not reach the tool";
    EXPECT_GT(selector.updated, 0);
    const QRect area = widget->getCurrentSelectionArea();
    EXPECT_NEAR(area.x(), 80, 1);
    EXPECT_NEAR(area.y(), 40, 1);
    EXPECT_NEAR(area.width(), 170, 2);
    EXPECT_NEAR(area.height(), 160, 2);
    tool.destroy();
}

// The shapes a session drew go with the session - its requests let go of them - and with the view: a view closed
// under them takes them, and the session's letting go afterwards finds no view to take them from.
TEST_F(ShapeOverlay, TheShapesGoWithTheSessionAndWithTheView)
{
    ViewHandle view = blackView("session");
    ImageViewWidget* const widget = view->getViewWidget();
    const std::size_t modules = widget->getNumOfDisplayModules();

    an::ui::extensions::ExtensionViewRequests requests;
    requests.drawOn("session", view.get());
    an::ui::extensions::ViewOverlayMessage overlay;
    overlay.session_id = "session";
    overlay.request_id = "overlay";
    an::ui::extensions::ViewOverlayShape line;
    line.points = {{200.0, 40.0}, {360.0, 40.0}};
    line.color = 0xFF00FF00u;
    overlay.add = {line};
    std::string reason;
    ASSERT_TRUE(requests.answerOverlay(overlay, nullptr, &reason)) << reason;
    ASSERT_NE(view->ownedShapes("session"), nullptr);
    ASSERT_TRUE(settle(*view));
    EXPECT_GT(around(widget->grab().toImage(), QPointF(280.0, 40.0)).green, 100) << "the session's line is drawn";

    requests.release();
    EXPECT_EQ(view->ownedShapes("session"), nullptr) << "the session's shapes outlived it";
    EXPECT_EQ(widget->getNumOfDisplayModules(), modules);
    EXPECT_EQ(requests.shapeCount(), 0u);

    // A view closed under a session's shapes takes them with it.
    an::ui::extensions::ExtensionViewRequests later;
    later.drawOn("later", view.get());
    ASSERT_TRUE(later.answerOverlay(overlay, nullptr, &reason)) << reason;
    view.reset();
    EXPECT_EQ(later.drawnOn(), nullptr) << "the session still holds a closed view";
    later.release();
}
