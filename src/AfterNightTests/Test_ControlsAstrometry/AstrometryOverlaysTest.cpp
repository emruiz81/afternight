// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The overlays a view draws over a solved image, made through the view's companion seam and attached to a view of a
/// document whose cards state a solution: the equatorial grid, the distortion map, the deep-sky and bright-star labels
/// - their catalog queried on a worker and drawn on the paint after it lands - and the detected stars. Every position
/// is drawn where the engine's pixel coordinate puts it, the centre of the first pixel at one half.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "controls/AstrometryOverlayUtils.h"
#include "controls/AstrometryStarDisplayModule.h"
#include "controls/CatalogAnnotationDisplayModule.h"
#include "controls/CoordinateGridDisplayModule.h"
#include "controls/DistortionMapDisplayModule.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QStringList>
#include <QThreadPool>
#include <QWidget>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::fixedCatalogs;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::makeCardDocument;
using an::tests::astrometry::makeView;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::pumpUntil;
using an::tests::astrometry::settle;
using an::tests::astrometry::solutionOf;
using an::tests::astrometry::tangentPlaneCards;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::ViewHandle;

constexpr std::uint32_t kWidth = 400u;
constexpr std::uint32_t kHeight = 300u;

/// A field of 0.01 degrees a pixel about (120, 10), the reference at the frame's centre pixel - 200.5 and 150.5 in the
/// header's one-based centres, 200 and 150 in the engine's.
[[nodiscard]] std::vector<an::CodecCard> fieldCards(const bool sip = false)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(200.5, 150.5, 120.0, 10.0, 0.01), sip);
    addCard(cards, "RADESYS", "'ICRS'");
    return cards;
}

/// A deep-sky catalog of two objects inside the field, one far outside it, and one too faint for the default limit.
[[nodiscard]] QByteArray deepSkyText()
{
    return QByteArray(
        "id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
        "magnitude\n"
        "NGC 1001,Bright One,120.0,10.0,30.0,20.0,10,Galaxy,8.0\n"
        "NGC 1002,,120.5,10.5,0.0,0.0,0,Open Cluster,9.5\n"
        "NGC 1003,,250.0,-40.0,5.0,5.0,0,Galaxy,7.0\n"
        "NGC 1004,,119.6,9.6,2.0,2.0,0,Galaxy,13.5\n");
}

/// A bright-star catalog of one named star and one with a Bayer letter alone inside the field.
[[nodiscard]] QByteArray brightStarText()
{
    return QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                      "HIP 1,Alpha Field,alf Fld,120.2,10.2,1.5,A0V\n"
                      "HIP 2,,bet Fld,119.8,9.8,3.0,K0III\n"
                      "HIP 3,Far Away,,300.0,-50.0,0.5,B1V\n");
}

/// A round galaxy of the deep-sky catalogue, of a size in arcminutes.
[[nodiscard]] an::CatalogEntry galaxyAt(const std::string& identifier, const double rightAscension,
                                        const double declination, const double sizeArcminutes, const double magnitude)
{
    an::CatalogEntry entry;
    entry.identifier = identifier;
    entry.coordinate = {.rightAscensionDegrees = rightAscension, .declinationDegrees = declination};
    entry.extent = an::CatalogExtent{
        .majorAxisArcminutes = sizeArcminutes, .minorAxisArcminutes = sizeArcminutes, .positionAngleDegrees = 0.0};
    entry.objectType = "Galaxy";
    entry.magnitude = magnitude;
    return entry;
}

/// A bright star of a proper name.
[[nodiscard]] an::CatalogEntry starAt(const std::string& identifier, const std::string& name,
                                      const double rightAscension, const double declination, const double magnitude)
{
    an::CatalogEntry entry;
    entry.identifier = identifier;
    entry.commonName = name;
    entry.coordinate = {.rightAscensionDegrees = rightAscension, .declinationDegrees = declination};
    entry.magnitude = magnitude;
    return entry;
}

/// A layer's scene, as an overlay's worker builds it at a level of detail - the standard one, a view's first.
[[nodiscard]] an::ui::AnnotationScene
layerScene(const an::WcsSolution& solution, const an::ui::BundledCatalog catalog,
           const std::vector<an::CatalogEntry>& entries,
           const an::ui::AnnotationDetail detail = an::ui::AnnotationDetail::Standard)
{
    const bool deepSky = catalog == an::ui::BundledCatalog::DeepSky;
    const std::span<const an::CatalogEntry> all(entries);
    return an::ui::buildAnnotationScene(
        an::ui::AnnotationSceneSources{.solution = &solution,
                                       .deepSky = deepSky ? all : std::span<const an::CatalogEntry>(),
                                       .stars = deepSky ? std::span<const an::CatalogEntry>() : all,
                                       .cards = {}},
        an::ui::detail::overlayAnnotationContent(catalog, detail));
}

/// The layout of a deep-sky layer alone at the view's first zoom, labelled as a choice says and holding the objects to
/// a smallest size.
[[nodiscard]] an::ui::detail::OverlayAnnotationLayout
layoutOfDeepSky(const an::ui::AnnotationScene& deepSky,
                const an::ui::AnnotationLabelText labelText = an::ui::AnnotationLabelText::Designation,
                const double smallestMajorPixels = 0.0)
{
    return an::ui::detail::layoutOverlayAnnotations(
        &deepSky, nullptr, 1.0, 11.0, an::ui::defaultAnnotationSettings().style, labelText, smallestMajorPixels);
}

/// The designations a layout keeps a mark of, sorted.
[[nodiscard]] QStringList keptDesignations(const an::ui::detail::OverlayAnnotationLayout& laid)
{
    QStringList kept;
    if (laid.scene != nullptr)
    {
        for (const an::ui::AnnotationMark& mark : laid.scene->marks)
        {
            kept.push_back(mark.designation.isEmpty() ? mark.name : mark.designation);
        }
    }
    kept.sort();
    return kept;
}

/// How many of a widget's pixels are clearly redder than they are blue, and how many clearly bluer.
[[nodiscard]] std::pair<int, int> redAndBluePixels(QWidget& widget)
{
    const QImage image = widget.grab().toImage();
    int red = 0;
    int blue = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor colour = image.pixelColor(x, y);
            if (colour.red() > colour.blue() + 40)
            {
                ++red;
            }
            else if (colour.blue() > colour.red() + 40)
            {
                ++blue;
            }
        }
    }
    return {red, blue};
}

/// Sixteen galaxies three pixels apart about the field's centre.
[[nodiscard]] std::vector<an::CatalogEntry> crowdedGalaxies()
{
    std::vector<an::CatalogEntry> galaxies;
    for (int index = 0; index < 16; ++index)
    {
        galaxies.push_back(galaxyAt(QStringLiteral("NGC 31%1").arg(index, 2, 10, QLatin1Char('0')).toStdString(),
                                    119.97 + 0.03 * (index % 4), 9.97 + 0.03 * (index / 4), 0.6, 10.0));
    }
    return galaxies;
}

/// Stars of proper names a pixel or two apart among the galaxies.
[[nodiscard]] std::vector<an::CatalogEntry> crowdedStars(const int count)
{
    std::vector<an::CatalogEntry> stars;
    for (int index = 0; index < count; ++index)
    {
        stars.push_back(starAt(QStringLiteral("HIP %1").arg(5000 + index).toStdString(),
                               QStringLiteral("Star %1").arg(index, 3, 10, QLatin1Char('0')).toStdString(),
                               119.99 + 0.01 * (index % 5), 9.99 + 0.01 * ((index / 5) % 5), 4.0));
    }
    return stars;
}

/// A deep-sky catalogue's text of galaxies.
[[nodiscard]] QByteArray deepSkyTextOf(const std::vector<an::CatalogEntry>& galaxies)
{
    QByteArray text("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                    "magnitude\n");
    for (const an::CatalogEntry& galaxy : galaxies)
    {
        text += QStringLiteral("%1,,%2,%3,%4,%4,0,Galaxy,%5\n")
                    .arg(QString::fromStdString(galaxy.identifier))
                    .arg(galaxy.coordinate.rightAscensionDegrees, 0, 'f', 6)
                    .arg(galaxy.coordinate.declinationDegrees, 0, 'f', 6)
                    .arg(galaxy.extent->majorAxisArcminutes, 0, 'f', 2)
                    .arg(*galaxy.magnitude, 0, 'f', 1)
                    .toUtf8();
    }
    return text;
}

/// A bright-star catalogue's text of stars.
[[nodiscard]] QByteArray brightStarTextOf(const std::vector<an::CatalogEntry>& stars)
{
    QByteArray text("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n");
    for (const an::CatalogEntry& star : stars)
    {
        text += QStringLiteral("%1,%2,,%3,%4,%5,G0V\n")
                    .arg(QString::fromStdString(star.identifier), QString::fromStdString(star.commonName))
                    .arg(star.coordinate.rightAscensionDegrees, 0, 'f', 6)
                    .arg(star.coordinate.declinationDegrees, 0, 'f', 6)
                    .arg(*star.magnitude, 0, 'f', 1)
                    .toUtf8();
    }
    return text;
}

/// Whether any two boxes overlap.
[[nodiscard]] bool anyOverlap(const QVector<QRectF>& boxes)
{
    for (qsizetype first = 0; first < boxes.size(); ++first)
    {
        for (qsizetype second = first + 1; second < boxes.size(); ++second)
        {
            if (boxes[first].intersects(boxes[second]))
            {
                return true;
            }
        }
    }
    return false;
}

/// The overlays' cases: an installed context and a view of a document.
class AstrometryOverlaysTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("overlays");
    }

    void TearDown() override
    {
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    /// Shows a document of the field's size carrying cards.
    void show(std::vector<an::CodecCard> cards)
    {
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(m_context->engineSession(), kWidth, kHeight, std::move(cards));
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*m_view));
    }

    /// Paints the view's widget once, which draws every enabled overlay attached to it.
    void paint() const
    {
        (void)m_view->getViewWidget()->grab();
    }

    /// An overlay the view's companion seam makes, attached to the view's widget and enabled.
    [[nodiscard]] std::unique_ptr<::ImageViewWidgetDisplayModule> attached(const an::ui::AstrometryOverlay overlay)
    {
        std::unique_ptr<::ImageViewWidgetDisplayModule> made = an::ui::createAstrometryOverlay(overlay, *m_view);
        EXPECT_NE(made, nullptr);
        if (made)
        {
            made->attachToViewWidget(m_view->getViewWidget());
            made->enableDisplayModule(true);
        }
        return made;
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
};

} // namespace

TEST(AstrometryOverlayGeometry, AnEnginePixelCoordinateIsDrawnAtItsScaledPosition)
{
    const QPointF drawn = an::ui::detail::widgetPoint(an::PixelCoordinate{.x = 199.5, .y = 0.5}, 2.0);
    EXPECT_DOUBLE_EQ(drawn.x(), 399.0);
    EXPECT_DOUBLE_EQ(drawn.y(), 1.0);
}

TEST(AstrometryOverlayGeometry, AnObjectAtTheReferenceIsDrawnOverTheReferencePixelsCentre)
{
    // The header's reference pixel 200.5 is the engine's 200: the centre of column 199 drawn at 200 times the scale.
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    an::CatalogEntry object;
    object.identifier = "NGC 1001";
    object.coordinate = {.rightAscensionDegrees = 120.0, .declinationDegrees = 10.0};

    const an::ui::CatalogAnnotationProjectedShape shape =
        an::ui::detail::projectCatalogObjectAnnotation(*solution, object, 1.5);
    EXPECT_NEAR(shape.center.x(), 200.0 * 1.5, 1e-9);
    EXPECT_NEAR(shape.center.y(), 150.0 * 1.5, 1e-9);

    // A star is drawn at its scene mark's centre times the view's scale.
    an::CatalogEntry star = object;
    star.commonName = "Reference Star";
    star.magnitude = 2.0;
    const std::vector<an::CatalogEntry> stars{star};
    const an::ui::AnnotationScene scene = an::ui::buildAnnotationScene(
        an::ui::AnnotationSceneSources{.solution = &*solution, .deepSky = {}, .stars = stars, .cards = {}},
        an::ui::detail::overlayAnnotationContent(an::ui::BundledCatalog::BrightStars,
                                                 an::ui::AnnotationDetail::Complete));
    ASSERT_EQ(scene.marks.size(), 1u);
    EXPECT_NEAR(scene.marks.front().center.x() * 1.5, 300.0, 1e-9);
    EXPECT_NEAR(scene.marks.front().center.y() * 1.5, 225.0, 1e-9);
}

// The part of an image a view shows is measured in the image's own pixels - the image's side where it fits the
// viewport - and rounded to a quarter of a doubling: a scroll bar's few pixels change nothing, a zoom step does.
TEST(AstrometryOverlayGeometry, ThePartShownIsMeasuredInTheImagesPixelsAndRoundedToAQuarterDoubling)
{
    const QSize image(400, 300);
    const QSizeF viewport(638.0, 447.0);
    // The whole image, the square root of 400 by 300 rounded to two to the eight and a half.
    EXPECT_NEAR(an::ui::detail::overlayShownReferenceLength(image, viewport, 1.0), std::exp2(8.5), 1e-9);
    // Four times larger, a part of 159.5 by 111.75 pixels: 133.5, rounded to two to the seventh.
    EXPECT_NEAR(an::ui::detail::overlayShownReferenceLength(image, viewport, 4.0), 128.0, 1e-9);
    EXPECT_DOUBLE_EQ(an::ui::detail::overlayShownReferenceLength(image, QSizeF(621.0, 430.0), 4.0),
                     an::ui::detail::overlayShownReferenceLength(image, viewport, 4.0))
        << "a scroll bar's pixels change nothing";
    EXPECT_DOUBLE_EQ(an::ui::detail::overlayShownReferenceLength(image, viewport, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(an::ui::detail::overlayShownReferenceLength(QSize(), viewport, 1.0), 0.0);
}

TEST(AstrometryOverlayGeometry, TheVisibleSkyIsCentredWhereTheSolutionPutsTheRectanglesCentre)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const std::optional<an::ui::detail::VisibleSky> sky =
        an::ui::detail::visibleSky(*solution, QRectF(0.0, 0.0, kWidth, kHeight));
    ASSERT_TRUE(sky.has_value());
    EXPECT_NEAR(sky->center.rightAscensionDegrees, 120.0, 1e-9);
    EXPECT_NEAR(sky->center.declinationDegrees, 10.0, 1e-9);
    // Half the diagonal of a 4 by 3 degree field.
    EXPECT_NEAR(sky->radiusDegrees, 2.5, 0.05);
    EXPECT_FALSE(an::ui::detail::visibleSky(*solution, QRectF()).has_value());
}

TEST_F(AstrometryOverlaysTest, TheGridDrawsLinesOverASolvedImageAndNoneOverAnUnsolvedOne)
{
    show(fieldCards());
    ASSERT_TRUE(m_view->imageSolution().has_value());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    auto* grid = dynamic_cast<an::ui::CoordinateGridDisplayModule*>(overlay.get());
    ASSERT_NE(grid, nullptr);

    paint();
    EXPECT_GT(grid->projectedLineCount(), 2);

    show({});
    EXPECT_FALSE(m_view->imageSolution().has_value());
    paint();
    EXPECT_EQ(grid->projectedLineCount(), 0);
}

// The part of the image a view shows is what its viewport shows at the scroll offsets - the whole widget when it fits -
// whether the scroll bars are drawn or not.
TEST_F(AstrometryOverlaysTest, ThePartShownIsWhatTheViewportShowsAtTheScrollOffsets)
{
    show(fieldCards());
    m_view->getViewWidget()->scaleImageView(1.0, true);
    paint();
    EXPECT_EQ(an::ui::detail::visibleOverlayWidgetRect(m_view.get()), QRectF(0.0, 0.0, kWidth, kHeight));

    // The zoom's scroll range is set once the view has settled at its new size; the offsets follow it.
    m_view->getViewWidget()->scaleImageView(4.0, true);
    paint();
    an::tests::astrometry::pumpQuietly();
    m_view->setViewportOffset(QPoint(150, 120));
    paint();
    const QWidget* viewport = m_view->getScrollArea()->viewport();
    ASSERT_LT(viewport->width(), 4 * static_cast<int>(kWidth) - 150);
    ASSERT_LT(viewport->height(), 4 * static_cast<int>(kHeight) - 120);
    EXPECT_EQ(an::ui::detail::visibleOverlayWidgetRect(m_view.get()),
              QRectF(150.0, 120.0, viewport->width(), viewport->height()));
}

// The view's grid is an annotated picture's grid of the part the view shows: its lines are the picture's, spaced for
// that part, and its labels are written as the picture writes them.
TEST_F(AstrometryOverlaysTest, TheViewsGridIsThePicturesGridOfThePartShown)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    auto* grid = dynamic_cast<an::ui::CoordinateGridDisplayModule*>(overlay.get());
    ASSERT_NE(grid, nullptr);
    m_view->getViewWidget()->scaleImageView(3.0, true);
    paint();
    an::tests::astrometry::pumpQuietly();
    m_view->setViewportOffset(QPoint(240, 180));
    paint();

    const double scale = m_view->getViewWidget()->getCurrentScaleFactor();
    const QRectF visible = an::ui::detail::visibleOverlayWidgetRect(m_view.get());
    ASSERT_EQ(visible.topLeft(), QPointF(240.0, 180.0)) << "a part of the image away from its corner";
    ASSERT_LT(visible.width(), 3.0 * kWidth);
    const QRectF part(visible.left() / scale, visible.top() / scale, visible.width() / scale, visible.height() / scale);
    const std::vector<an::ui::AnnotationGridLine> pictures =
        an::ui::buildAnnotationGrid(*m_view->imageSolution(), part, an::ui::AnnotationGridDensity::Normal);
    QStringList expected;
    for (const an::ui::AnnotationGridLine& line : pictures)
    {
        expected.push_back(line.label);
    }
    ASSERT_FALSE(expected.isEmpty());
    EXPECT_EQ(grid->projectedLineCount(), static_cast<int>(pictures.size()));

    const QVector<QString> labels = grid->projectedLabels();
    ASSERT_FALSE(labels.isEmpty());
    for (const QString& label : labels)
    {
        EXPECT_TRUE(expected.contains(label)) << label.toStdString();
    }
}

// The grid's labels keep their size on the screen at every zoom and lie inside the part of the image the view shows.
TEST_F(AstrometryOverlaysTest, TheGridsLabelsKeepTheirSizeOnTheScreenAndLieInsideThePartShown)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    auto* grid = dynamic_cast<an::ui::CoordinateGridDisplayModule*>(overlay.get());
    ASSERT_NE(grid, nullptr);

    QVector<double> heights;
    for (const double zoom : {1.0, 3.0})
    {
        m_view->getViewWidget()->scaleImageView(zoom, true);
        paint();
        const QVector<QRectF> boxes = grid->projectedLabelBoxes();
        ASSERT_FALSE(boxes.isEmpty()) << "zoom " << zoom;
        const QRectF visible = an::ui::detail::visibleOverlayWidgetRect(m_view.get());
        for (const QRectF& box : boxes)
        {
            EXPECT_TRUE(visible.adjusted(-1.0, -1.0, 1.0, 1.0).contains(box)) << "zoom " << zoom;
        }
        heights.push_back(boxes.front().height());
    }
    EXPECT_NEAR(heights[1], heights[0], 0.5);
}

TEST_F(AstrometryOverlaysTest, TheDistortionMapSamplesAForwardPolynomialAndNothingElse)
{
    show(fieldCards(true));
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::DistortionMap);
    auto* map = dynamic_cast<an::ui::DistortionMapDisplayModule*>(overlay.get());
    ASSERT_NE(map, nullptr);

    paint();
    EXPECT_EQ(map->sampledVectorCount(), 400);

    show(fieldCards(false));
    paint();
    EXPECT_EQ(map->sampledVectorCount(), 0);
}

TEST_F(AstrometryOverlaysTest, DeepSkyLabelsArriveFromAWorkerAndAreDrawnOnTheNextPaint)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));

    // The first paint asks a worker and draws nothing: the GUI thread never waits for the catalog.
    paint();
    EXPECT_EQ(annotations->catalogQueriesAsked(), 1u);
    EXPECT_TRUE(annotations->projectedLabels().isEmpty());

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !annotations->projectedLabels().isEmpty();
    }));
    QVector<QString> labels = annotations->projectedLabels();
    std::sort(labels.begin(), labels.end());
    EXPECT_EQ(labels, (QVector<QString>{QStringLiteral("NGC 1001"), QStringLiteral("NGC 1002")}));

    // Painting again, as a scroll or a zoom does, projects the same answer without asking.
    paint();
    paint();
    EXPECT_EQ(annotations->catalogQueriesAsked(), 1u);

    // Another level of detail is another question: the detailed level names the fainter galaxy too, and the essential
    // level the named object alone.
    annotations->setChoices(an::ui::AnnotationViewChoices{.detail = an::ui::AnnotationDetail::Detailed});
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels().size() == 3;
    }));
    EXPECT_EQ(annotations->catalogQueriesAsked(), 2u);
    annotations->setChoices(an::ui::AnnotationViewChoices{.detail = an::ui::AnnotationDetail::Essential});
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels() == QVector<QString>{QStringLiteral("NGC 1001")};
    }));
    EXPECT_EQ(annotations->catalogQueriesAsked(), 3u);
}

// An object no catalogue measured a magnitude for is named at the levels that mark one - the detailed and the complete
// - as an annotated picture names it: the field's query states no magnitude that would leave it out.
TEST_F(AstrometryOverlaysTest, AnObjectOfNoMeasuredMagnitudeIsNamedAtTheLevelsThatMarkIt)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource(fixedCatalogs(
        QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                   "magnitude\n"
                   "NGC 1001,Bright One,120.0,10.0,30.0,20.0,10,Galaxy,8.0\n"
                   "NGC 1007,,120.2,9.9,5.0,5.0,0,Galaxy,\n"),
        brightStarText()));

    const auto labelsAt = [&](const an::ui::AnnotationDetail detail, const qsizetype count) {
        annotations->setChoices(an::ui::AnnotationViewChoices{.detail = detail});
        EXPECT_TRUE(pumpUntil([&]() {
            paint();
            return annotations->projectedLabels().size() == count;
        })) << "level "
            << static_cast<int>(detail);
        QVector<QString> labels = annotations->projectedLabels();
        std::sort(labels.begin(), labels.end());
        return labels;
    };
    EXPECT_EQ(labelsAt(an::ui::AnnotationDetail::Standard, 1), QVector<QString>{QStringLiteral("NGC 1001")});
    const QVector<QString> both{QStringLiteral("NGC 1001"), QStringLiteral("NGC 1007")};
    EXPECT_EQ(labelsAt(an::ui::AnnotationDetail::Detailed, 2), both);
    EXPECT_EQ(labelsAt(an::ui::AnnotationDetail::Complete, 2), both);
}

// An object too small to show is named once the view shows it large enough: the level's smallest size is a share of the
// part of the image the view shows, so zooming in names the smaller objects, as a map does - a notable one whatever its
// size.
TEST_F(AstrometryOverlaysTest, ASmallObjectIsNamedOnceTheViewShowsItLargeEnough)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    // A galaxy of 24 arcseconds is two thirds of a pixel; the standard level's smallest size is 0.3 % of the part
    // shown.
    annotations->setCatalogSource(fixedCatalogs(
        QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                   "magnitude\n"
                   "NGC 1001,Bright One,120.0,10.0,30.0,20.0,10,Galaxy,8.0\n"
                   "NGC 1006,,120.1,10.1,0.4,0.4,0,Galaxy,10.0\n"
                   "M 98,,119.9,9.9,0.4,0.4,0,Galaxy,10.0\n"),
        brightStarText()));

    m_view->getViewWidget()->scaleImageView(1.0, true);
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels().size() == 2;
    }));
    QVector<QString> fitted = annotations->projectedLabels();
    std::sort(fitted.begin(), fitted.end());
    EXPECT_EQ(fitted, (QVector<QString>{QStringLiteral("M 98"), QStringLiteral("NGC 1001")}));

    m_view->getViewWidget()->scaleImageView(4.0, true);
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels().size() == 3;
    }));
    EXPECT_TRUE(annotations->projectedLabels().contains(QStringLiteral("NGC 1006")));
}

// A star's label says what the view's choice of label says, as an annotated picture's does: its designation, else its
// proper name; its proper name, else its designation; or its designation with its name on a second line. A choice of
// label lays the labels out again and asks the catalogue nothing.
TEST_F(AstrometryOverlaysTest, AStarsLabelSaysWhatTheViewsChoiceOfLabelSays)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* labels = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(labels, nullptr);
    labels->setLayers(false, true);
    labels->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));

    // The last labels are drawn until the new ones land: each choice waits for a layout made after it.
    const auto drawsWith = [&](const an::ui::AnnotationLabelText text, const QVector<QString>& expected) {
        const std::uint64_t before = labels->layoutsMade();
        labels->setChoices(an::ui::AnnotationViewChoices{.labelText = text});
        return pumpUntil([&]() {
            paint();
            QVector<QString> drawn = labels->projectedLabels();
            std::sort(drawn.begin(), drawn.end());
            return labels->layoutsMade() > before && drawn == expected;
        });
    };
    const QVector<QString> designations{QStringLiteral("alf Fld"), QStringLiteral("bet Fld")};
    EXPECT_TRUE(drawsWith(an::ui::AnnotationLabelText::Designation, designations));
    const QVector<QRectF> oneLine = labels->projectedLabelBoxes();
    EXPECT_TRUE(drawsWith(an::ui::AnnotationLabelText::Name,
                          QVector<QString>{QStringLiteral("Alpha Field"), QStringLiteral("bet Fld")}));
    EXPECT_TRUE(drawsWith(an::ui::AnnotationLabelText::DesignationAndName, designations));
    const QVector<QRectF> twoLines = labels->projectedLabelBoxes();
    ASSERT_EQ(oneLine.size(), 2);
    ASSERT_EQ(twoLines.size(), 2);
    // The named star's label is the taller by its second line; the star of a designation alone keeps one.
    const auto tallest = [](const QVector<QRectF>& boxes) { return (std::max)(boxes[0].height(), boxes[1].height()); };
    EXPECT_GT(tallest(twoLines), 1.5 * tallest(oneLine));
    EXPECT_EQ(labels->catalogQueriesAsked(), 1u);
}

// A rich field shows its stars legibly: the brightest is labelled first, a star apart from them keeps its label, and no
// label covers another - a crowded star's label is set away from it on a leader line or left out.
TEST_F(AstrometryOverlaysTest, CrowdedStarsAreLabelledBrightestFirstAndNoLabelCoversAnother)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* labels = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(labels, nullptr);
    labels->setLayers(false, true);
    // The complete level names every star of the catalogue, the sixth magnitude's among them.
    labels->setChoices(an::ui::AnnotationViewChoices{.detail = an::ui::AnnotationDetail::Complete});
    labels->setCatalogSource(fixedCatalogs(
        deepSkyText(), QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                                  "HIP 11,Faint One,,120.001,10.001,5.0,G0V\n"
                                  "HIP 12,Brightest,,120.0,10.0,2.0,A0V\n"
                                  "HIP 13,Middle One,,120.0015,9.9995,3.5,F0V\n"
                                  "HIP 14,Apart,,119.0,9.0,6.0,K0V\n")));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !labels->projectedLabels().isEmpty();
    }));
    const QVector<QString> drawn = labels->projectedLabels();
    ASSERT_FALSE(drawn.isEmpty());
    EXPECT_EQ(drawn.front(), QStringLiteral("Brightest"));
    EXPECT_TRUE(drawn.contains(QStringLiteral("Apart")));
    EXPECT_FALSE(anyOverlap(labels->projectedLabelBoxes()));
}

// The entries of one object in several catalogues are one label under the designation an annotated picture gives it,
// whichever entry is the brighter: the Carina Nebula listed as NGC 3372 and, a third of a degree away, as C 92, and a
// galaxy listed in NGC and in IC at one position.
TEST_F(AstrometryOverlaysTest, EntriesOfOneObjectAreOneLabelUnderThePicturesDesignation)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource(fixedCatalogs(
        QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                   "magnitude\n"
                   "NGC 3372,Carina Nebula,120.3,10.2,30.0,30.0,0,Bright Nebula,99.0\n"
                   "C 92,Carina Nebula,120.6,10.2,30.0,30.0,0,Bright Nebula,3.0\n"
                   "IC 2001,,119.0,9.2,5.0,5.0,0,Galaxy,9.0\n"
                   "NGC 2001,,119.0,9.2,5.0,5.0,0,Galaxy,9.5\n"),
        brightStarText()));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !annotations->projectedLabels().isEmpty();
    }));
    QVector<QString> labels = annotations->projectedLabels();
    std::sort(labels.begin(), labels.end());
    EXPECT_EQ(labels, (QVector<QString>{QStringLiteral("NGC 2001"), QStringLiteral("NGC 3372")}));
}

// Stars are labelled in the order an annotated picture labels them: a star of a proper name before a brighter one of a
// Bayer letter alone beside it, which takes the place left or none.
TEST_F(AstrometryOverlaysTest, ANamedStarIsLabelledBeforeABrighterStarOfADesignationAlone)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* labels = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(labels, nullptr);
    labels->setLayers(false, true);
    labels->setCatalogSource(fixedCatalogs(
        deepSkyText(), QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                                  "HIP 21,,alf Tst,120.0,10.0,1.0,A0V\n"
                                  "HIP 22,Named Star,,120.001,10.001,4.0,G0V\n")));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !labels->projectedLabels().isEmpty();
    }));
    EXPECT_EQ(labels->projectedLabels().front(), QStringLiteral("Named Star"));
    EXPECT_FALSE(anyOverlap(labels->projectedLabelBoxes()));
}

// What a live overlay's scene holds at a level of detail: its own layer and the level's catalogues, kinds, magnitudes
// and stars - an annotated picture's - but every size: the view holds an object to the level's smallest size for the
// part of the image it shows when it lays the labels out.
TEST(AstrometryOverlayContent, AnOverlaysSceneHoldsItsLayerAtTheLevelAndEverySize)
{
    for (const an::ui::AnnotationDetail detail : an::ui::annotationDetails())
    {
        an::ui::AnnotationContent level = an::ui::defaultAnnotationSettings().content;
        an::ui::applyAnnotationDetail(level, detail);

        const an::ui::AnnotationContent deepSky =
            an::ui::detail::overlayAnnotationContent(an::ui::BundledCatalog::DeepSky, detail);
        EXPECT_EQ(deepSky.layers, (an::ui::AnnotationLayers{.grid = false,
                                                            .deepSky = true,
                                                            .stars = false,
                                                            .compass = false,
                                                            .scaleBar = false,
                                                            .infoPanel = false}));
        an::ui::AnnotationDeepSkySelection everySize = level.deepSky;
        everySize.minimumSize = 0.0;
        EXPECT_EQ(deepSky.deepSky, everySize) << "level " << static_cast<int>(detail);
        EXPECT_DOUBLE_EQ(an::ui::detail::overlaySmallestObject(detail), level.deepSky.minimumSize);

        const an::ui::AnnotationContent stars =
            an::ui::detail::overlayAnnotationContent(an::ui::BundledCatalog::BrightStars, detail);
        EXPECT_TRUE(stars.layers.stars);
        EXPECT_FALSE(stars.layers.deepSky);
        EXPECT_EQ(stars.stars, level.stars) << "level " << static_cast<int>(detail);
    }
}

// A field's query states a magnitude only where it leaves out nothing the level names: no limit for a level that marks
// objects of no measured magnitude or every magnitude, the deep-sky level's limit otherwise, and the stars' limit.
TEST(AstrometryOverlayContent, AFieldsQueryStatesNoLimitThatLeavesOutWhatTheLevelNames)
{
    const auto limitAt = [](const an::ui::BundledCatalog catalog, const an::ui::AnnotationDetail detail) {
        return an::ui::detail::overlayQueryMagnitudeLimit(catalog,
                                                          an::ui::detail::overlayAnnotationContent(catalog, detail));
    };
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::DeepSky, an::ui::AnnotationDetail::Standard),
              std::optional<double>(12.0));
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::DeepSky, an::ui::AnnotationDetail::Essential), std::nullopt);
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::DeepSky, an::ui::AnnotationDetail::Detailed), std::nullopt);
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::DeepSky, an::ui::AnnotationDetail::Complete), std::nullopt);
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::BrightStars, an::ui::AnnotationDetail::Essential),
              std::optional<double>(2.5));
    EXPECT_EQ(limitAt(an::ui::BundledCatalog::BrightStars, an::ui::AnnotationDetail::Complete),
              std::optional<double>(an::ui::kAnnotationFaintestStar));
}

// At the complete level a star is labelled as an annotated picture names it - its proper name, else its Bayer letter,
// else its catalogue number - and a star of none of them, blank or all spaces, is not labelled at all.
TEST(AstrometryOverlayContent, AStarIsLabelledByItsProperNameThenItsBayerLetterThenItsNumber)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const auto star = [](const char* identifier, const char* name, const char* bayer, const double rightAscension) {
        an::CatalogEntry entry;
        entry.identifier = identifier;
        entry.commonName = name;
        entry.designation = bayer;
        entry.coordinate = {.rightAscensionDegrees = rightAscension, .declinationDegrees = 10.0};
        entry.magnitude = 2.0;
        return entry;
    };
    const std::vector<an::CatalogEntry> stars{star("HIP 32349", "Sirius", "alf CMa", 120.0),
                                              star("HIP 27989", "", "alf Ori", 120.3), star("HIP 12345", "", "", 120.6),
                                              star("   ", "  ", " ", 119.7)};
    const an::ui::AnnotationContent content = an::ui::detail::overlayAnnotationContent(
        an::ui::BundledCatalog::BrightStars, an::ui::AnnotationDetail::Complete);
    const an::ui::AnnotationScene scene = an::ui::buildAnnotationScene(
        an::ui::AnnotationSceneSources{.solution = &*solution, .deepSky = {}, .stars = stars, .cards = {}}, content);

    std::vector<QString> labels;
    for (const an::ui::AnnotationMark& mark : scene.marks)
    {
        labels.push_back(an::ui::annotationLabelLines(mark, an::ui::AnnotationLabelText::Name).first);
    }
    std::sort(labels.begin(), labels.end());
    EXPECT_EQ(labels,
              (std::vector<QString>{QStringLiteral("HIP 12345"), QStringLiteral("Sirius"), QStringLiteral("alf Ori")}));
}

// A view lays its catalogue overlay's layers out together, as an annotated picture lays out its labels, at every zoom:
// no label covers another, the deep-sky objects' and the stars' alike, and a label keeps its size on the screen.
TEST(AstrometryOverlayLayout, NoTwoLabelsOverlapAndALabelKeepsItsSizeOnTheScreenAtEveryZoom)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    // A Messier object among them is placed before any star, so the deep-sky layer has a label at every zoom.
    std::vector<an::CatalogEntry> galaxies = crowdedGalaxies();
    galaxies.push_back(galaxyAt("M 99", 120.015, 10.015, 2.0, 9.0));
    const an::ui::AnnotationScene deepSky = layerScene(*solution, an::ui::BundledCatalog::DeepSky, galaxies);
    const an::ui::AnnotationScene stars = layerScene(*solution, an::ui::BundledCatalog::BrightStars, crowdedStars(12));
    const an::ui::AnnotationStyle style = an::ui::defaultAnnotationSettings().style;

    for (const double scale : {0.5, 1.0, 4.0})
    {
        const an::ui::detail::OverlayAnnotationLayout laid = an::ui::detail::layoutOverlayAnnotations(
            &deepSky, &stars, scale, 11.0, style, an::ui::AnnotationLabelText::Designation, 0.0);
        ASSERT_NE(laid.scene, nullptr);
        ASSERT_NE(laid.layout, nullptr);
        EXPECT_NEAR(laid.layout->metrics.fontSize * scale, 11.0, 1e-9) << "zoom " << scale;

        QVector<QRectF> boxes;
        bool deepSkyLabelled = false;
        bool starLabelled = false;
        for (const an::ui::AnnotationLabel& label : laid.layout->labels)
        {
            if (label.role != an::ui::AnnotationLabelRole::Object)
            {
                continue;
            }
            boxes.push_back(label.box);
            const bool star = laid.scene->marks[label.source].kind == an::ui::AnnotationMarkKind::Star;
            starLabelled = starLabelled || star;
            deepSkyLabelled = deepSkyLabelled || !star;
        }
        EXPECT_TRUE(deepSkyLabelled && starLabelled) << "both layers are laid out together, zoom " << scale;
        EXPECT_FALSE(anyOverlap(boxes)) << "zoom " << scale;
    }
}

// Every deep-sky object keeps its outline whether its label found room or not, as the view drew them; a star whose
// label found no room is left out, as the view's star labels leave it out.
TEST(AstrometryOverlayLayout, EveryObjectKeepsItsOutlineAndAStarWithoutALabelIsLeftOut)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    const an::ui::AnnotationScene deepSky = layerScene(*solution, an::ui::BundledCatalog::DeepSky, crowdedGalaxies());
    const an::ui::AnnotationScene stars = layerScene(*solution, an::ui::BundledCatalog::BrightStars, crowdedStars(120));
    ASSERT_EQ(stars.marks.size(), 120u);

    const an::ui::detail::OverlayAnnotationLayout laid =
        an::ui::detail::layoutOverlayAnnotations(&deepSky, &stars, 1.0, 11.0, an::ui::defaultAnnotationSettings().style,
                                                 an::ui::AnnotationLabelText::Designation, 0.0);
    ASSERT_NE(laid.scene, nullptr);
    ASSERT_NE(laid.layout, nullptr);
    ASSERT_EQ(laid.layout->labelled.size(), laid.scene->marks.size());
    std::size_t galaxies = 0u;
    std::size_t starsDrawn = 0u;
    for (std::size_t index = 0u; index < laid.scene->marks.size(); ++index)
    {
        if (laid.scene->marks[index].kind == an::ui::AnnotationMarkKind::Star)
        {
            ++starsDrawn;
            EXPECT_TRUE(laid.layout->labelled[index]) << "a star drawn is a star labelled";
        }
        else
        {
            ++galaxies;
        }
    }
    EXPECT_EQ(galaxies, 16u);
    EXPECT_GT(starsDrawn, 0u);
    EXPECT_LT(starsDrawn, 120u) << "a crowd of stars cannot all be labelled";
}

// An object smaller than the smallest size is left out, as an annotated picture leaves it out; a Messier, Caldwell or
// named object is kept whatever its size, and an object of no stated size is judged by its magnitude alone.
TEST(AstrometryOverlayLayout, AnObjectSmallerThanTheSmallestSizeIsLeftOutUnlessItIsNotable)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    // At 36 arcseconds a pixel: 1.2 arcminutes are two pixels, 6 arcminutes ten.
    an::CatalogEntry point = galaxyAt("NGC 4002", 120.1, 10.0, 0.0, 9.0);
    point.extent.reset();
    an::CatalogEntry named = galaxyAt("NGC 4004", 119.9, 10.1, 1.2, 9.0);
    named.commonName = "Small Named";
    const an::ui::AnnotationScene deepSky =
        layerScene(*solution, an::ui::BundledCatalog::DeepSky,
                   {galaxyAt("NGC 4001", 120.0, 10.1, 1.2, 9.0), galaxyAt("M 98", 120.1, 9.9, 1.2, 9.0), point,
                    galaxyAt("NGC 4003", 119.9, 9.9, 6.0, 9.0), named});
    ASSERT_EQ(deepSky.marks.size(), 5u) << "the scene keeps every size";

    EXPECT_EQ(keptDesignations(layoutOfDeepSky(deepSky, an::ui::AnnotationLabelText::Designation, 5.0)),
              (QStringList{QStringLiteral("M 98"), QStringLiteral("NGC 4002"), QStringLiteral("NGC 4003"),
                           QStringLiteral("NGC 4004")}));
    EXPECT_EQ(keptDesignations(layoutOfDeepSky(deepSky)).size(), 5) << "no smallest size keeps every object";
}

// A star the level marks without naming it - the detailed level marks the unnamed stars - is drawn without a label;
// only a star whose label found no room is left out.
TEST(AstrometryOverlayLayout, AStarTheLevelMarksWithoutANameIsDrawnUnlabelled)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    an::CatalogEntry unnamed = starAt("HIP 7001", "", 120.5, 10.5, 5.0);
    const an::ui::AnnotationScene stars =
        layerScene(*solution, an::ui::BundledCatalog::BrightStars,
                   {starAt("HIP 7002", "Named Star", 119.5, 9.5, 3.0), unnamed}, an::ui::AnnotationDetail::Detailed);
    ASSERT_EQ(stars.marks.size(), 2u);

    const an::ui::detail::OverlayAnnotationLayout laid =
        an::ui::detail::layoutOverlayAnnotations(nullptr, &stars, 1.0, 11.0, an::ui::defaultAnnotationSettings().style,
                                                 an::ui::AnnotationLabelText::Designation, 0.0);
    ASSERT_NE(laid.scene, nullptr);
    ASSERT_EQ(laid.scene->marks.size(), 2u) << "the unnamed star is drawn";
    const auto unlabelled = std::find_if(laid.scene->marks.begin(), laid.scene->marks.end(),
                                         [](const an::ui::AnnotationMark& mark) { return mark.markOnly; });
    ASSERT_NE(unlabelled, laid.scene->marks.end());
    EXPECT_FALSE(laid.layout->labelled[static_cast<std::size_t>(unlabelled - laid.scene->marks.begin())]);
}

// A label says what the choice of label says: a named object's designation alone on one line, or with its name on a
// second line under it.
TEST(AstrometryOverlayLayout, ALabelHoldsTheLinesTheChoiceOfLabelGivesIt)
{
    const std::optional<an::WcsSolution> solution = solutionOf(fieldCards(), kWidth, kHeight);
    ASSERT_TRUE(solution.has_value());
    an::CatalogEntry galaxy = galaxyAt("NGC 4005", 120.0, 10.0, 6.0, 9.0);
    galaxy.commonName = "Test Galaxy";
    const an::ui::AnnotationScene deepSky = layerScene(*solution, an::ui::BundledCatalog::DeepSky, {galaxy});

    const auto secondLines = [&](const an::ui::AnnotationLabelText text) {
        const an::ui::detail::OverlayAnnotationLayout laid = layoutOfDeepSky(deepSky, text);
        int lines = 0;
        for (const an::ui::AnnotationLabel& label : laid.layout->labels)
        {
            lines += label.role == an::ui::AnnotationLabelRole::Object && !label.secondary.isEmpty() ? 1 : 0;
        }
        return lines;
    };
    EXPECT_EQ(secondLines(an::ui::AnnotationLabelText::Designation), 0);
    EXPECT_EQ(secondLines(an::ui::AnnotationLabelText::DesignationAndName), 1);
}

// One overlay draws the deep-sky objects and the bright stars, each a layer it is told to draw, and lays their labels
// out together: no label covers another.
TEST_F(AstrometryOverlaysTest, OneOverlayDrawsBothCataloguesAndNoLabelCoversAnother)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    EXPECT_TRUE(annotations->showsDeepSky());
    EXPECT_TRUE(annotations->showsStars());
    // A Messier object among the galaxies is placed before any star, so the deep-sky layer is labelled too.
    std::vector<an::CatalogEntry> galaxies = crowdedGalaxies();
    galaxies.push_back(galaxyAt("M 99", 120.015, 10.015, 2.0, 9.0));
    annotations->setCatalogSource(fixedCatalogs(deepSkyTextOf(galaxies), brightStarTextOf(crowdedStars(12))));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !annotations->projectedLabels().isEmpty();
    }));
    const QVector<QString> labels = annotations->projectedLabels();
    ASSERT_EQ(labels.size(), annotations->projectedLabelBoxes().size());
    EXPECT_TRUE(labels.contains(QStringLiteral("M 99")));
    EXPECT_TRUE(std::any_of(labels.begin(), labels.end(),
                            [](const QString& label) { return label.startsWith(QStringLiteral("Star")); }));
    EXPECT_FALSE(anyOverlap(annotations->projectedLabelBoxes()));
    EXPECT_EQ(annotations->catalogQueriesAsked(), 2u) << "one query of each catalogue";

    // The stars alone, as the view's toggles ask through the companion seam.
    an::ui::setAstrometryOverlayLayers(*overlay, false, true);
    EXPECT_FALSE(annotations->showsDeepSky());
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        const QVector<QString> drawn = annotations->projectedLabels();
        return !drawn.isEmpty() && std::all_of(drawn.begin(), drawn.end(), [](const QString& label) {
            return label.startsWith(QStringLiteral("Star"));
        });
    }));
}

// The labels are laid out again for a zoom, at which they keep their size on the screen, and not for a scroll.
TEST_F(AstrometryOverlaysTest, AZoomLaysTheLabelsOutAgainAndAScrollDoesNot)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return !annotations->projectedLabels().isEmpty();
    }));
    const std::uint64_t first = annotations->layoutsMade();
    EXPECT_GE(first, 1u);
    ASSERT_FALSE(annotations->projectedLabelBoxes().isEmpty());
    const double height = annotations->projectedLabelBoxes().front().height();

    m_view->getViewWidget()->scaleImageView(4.0, true);
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->layoutsMade() > first;
    }));
    const std::uint64_t zoomed = annotations->layoutsMade();
    ASSERT_FALSE(annotations->projectedLabelBoxes().isEmpty());
    EXPECT_NEAR(annotations->projectedLabelBoxes().front().height(), height, 0.5)
        << "a label keeps its size on the screen";

    m_view->setViewportOffset(QPoint(150, 120));
    paint();
    an::tests::astrometry::pumpQuietly();
    paint();
    EXPECT_EQ(annotations->layoutsMade(), zoomed);
}

TEST_F(AstrometryOverlaysTest, ACatalogThatCannotBeReadDrawsNothingAndIsNotAskedForAgain)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource([](an::ui::BundledCatalog) -> an::Result<QByteArray> {
        return an::Status::failure(an::ErrorCode::ExternalFailure, "the catalog is not there");
    });

    paint();
    QThreadPool::globalInstance()->waitForDone();
    an::tests::astrometry::pumpQuietly();
    paint();
    paint();
    EXPECT_TRUE(annotations->projectedLabels().isEmpty());
    EXPECT_EQ(annotations->catalogQueriesAsked(), 1u);
}

TEST_F(AstrometryOverlaysTest, ASourceChangedWhileAQueryRunsIsReadByTheNextQuery)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
    annotations->setLayers(true, false);
    annotations->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));
    paint();
    EXPECT_EQ(annotations->catalogQueriesAsked(), 1u);

    // Another catalog arrives before the first answer lands: that answer is dropped and the next paint asks again.
    annotations->setCatalogSource(fixedCatalogs(
        QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,"
                   "magnitude\nIC 9001,,120.1,10.1,0.0,0.0,0,Galaxy,8.0\n"),
        brightStarText()));
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels() == QVector<QString>{QStringLiteral("IC 9001")};
    }));
    EXPECT_EQ(annotations->catalogQueriesAsked(), 2u);
}

TEST_F(AstrometryOverlaysTest, AnAnswerThatLandsAfterTheOverlayIsGoneIsNeverDrawn)
{
    show(fieldCards());
    {
        std::unique_ptr<::ImageViewWidgetDisplayModule> overlay =
            attached(an::ui::AstrometryOverlay::CatalogAnnotations);
        auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
        ASSERT_NE(annotations, nullptr);
        annotations->setLayers(true, false);
        annotations->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));
        paint();
        EXPECT_EQ(annotations->catalogQueriesAsked(), 1u);
    }
    QThreadPool::globalInstance()->waitForDone();
    an::tests::astrometry::pumpQuietly();
    paint();
    SUCCEED();
}

TEST_F(AstrometryOverlaysTest, TheOverlayLetsTheSessionGoOnceTheViewAndItAreGone)
{
    // The catalog an overlay read holds a lease on the session's engine; it goes with the overlay.
    show(fieldCards());
    {
        std::unique_ptr<::ImageViewWidgetDisplayModule> overlay =
            attached(an::ui::AstrometryOverlay::CatalogAnnotations);
        auto* labels = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
        ASSERT_NE(labels, nullptr);
        labels->setLayers(false, true);
        labels->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));
        ASSERT_TRUE(pumpUntil([&]() {
            paint();
            return !labels->projectedLabels().isEmpty();
        }));
    }
    m_view.reset();
    const an::Status shutdown = m_context->shutdownSession();
    EXPECT_TRUE(shutdown.ok()) << shutdown.message();
}

TEST_F(AstrometryOverlaysTest, TheDetectedStarsOverlayDrawsTheStarsItIsHandedAndNoneOnceCleared)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::DetectedStars);
    auto* stars = dynamic_cast<an::ui::AstrometryStarDisplayModule*>(overlay.get());
    ASSERT_NE(stars, nullptr);

    auto detected = std::make_shared<an::ui::ViewOverlayStars>();
    for (int index = 0; index < 3; ++index)
    {
        an::DetectedStar star;
        star.positionX = 10.5 + 20.0 * index;
        star.positionY = 30.5;
        star.psf.fullWidthX = 3.0;
        star.psf.fullWidthY = 2.5;
        detected->detection.detection.stars.push_back(star);
    }
    an::ui::setAstrometryOverlayStars(*overlay, detected);
    EXPECT_EQ(stars->starCount(), 3u);
    paint();

    an::ui::setAstrometryOverlayStars(*overlay, nullptr);
    EXPECT_EQ(stars->starCount(), 0u);
}

// The view's choices reach the overlays that draw them through the companion seam: the catalogue overlay takes them
// all, the grid its palette, and an overlay that draws neither ignores them.
TEST_F(AstrometryOverlaysTest, TheViewsChoicesReachTheOverlaysThatDrawThem)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> annotations =
        attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    std::unique_ptr<::ImageViewWidgetDisplayModule> grid = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    std::unique_ptr<::ImageViewWidgetDisplayModule> map = attached(an::ui::AstrometryOverlay::DistortionMap);
    const an::ui::AnnotationViewChoices choices{.detail = an::ui::AnnotationDetail::Complete,
                                                .labelText = an::ui::AnnotationLabelText::Name,
                                                .palette = an::ui::AnnotationPalette::Atlas};
    an::ui::setAstrometryOverlayChoices(*annotations, choices);
    an::ui::setAstrometryOverlayChoices(*grid, choices);
    an::ui::setAstrometryOverlayChoices(*map, choices);
    EXPECT_EQ(dynamic_cast<an::ui::CatalogAnnotationDisplayModule&>(*annotations).choices(), choices);
    EXPECT_EQ(dynamic_cast<an::ui::CoordinateGridDisplayModule&>(*grid).palette(), an::ui::AnnotationPalette::Atlas);
}

// The grid is drawn in the palette the view's choices name: Aurora's soft blue, or Ember's red for a dark-adapted eye.
TEST_F(AstrometryOverlaysTest, TheGridIsDrawnInThePaletteTheViewsChoicesName)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    auto* grid = dynamic_cast<an::ui::CoordinateGridDisplayModule*>(overlay.get());
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->palette(), an::ui::AnnotationPalette::Aurora);

    const auto [auroraRed, auroraBlue] = redAndBluePixels(*m_view->getViewWidget());
    EXPECT_GT(auroraBlue, 100);
    EXPECT_GT(auroraBlue, 4 * auroraRed);

    an::ui::setAstrometryOverlayChoices(*overlay,
                                        an::ui::AnnotationViewChoices{.palette = an::ui::AnnotationPalette::Ember});
    const auto [emberRed, emberBlue] = redAndBluePixels(*m_view->getViewWidget());
    EXPECT_GT(emberRed, 100);
    EXPECT_GT(emberRed, 4 * emberBlue);
}

TEST_F(AstrometryOverlaysTest, AnImageTheProductSolvedBeforeItStatedTheFrameShowsItsSolution)
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(200.5, 150.5, 120.0, 10.0, 0.01));
    addCard(cards, "EQUINOX", "2000.0");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    show(cards);
    ASSERT_TRUE(m_view->imageSolution().has_value());
    EXPECT_NEAR(m_view->imageSolution()->referencePoint().rightAscensionDegrees, 120.0, 1e-12);
}
