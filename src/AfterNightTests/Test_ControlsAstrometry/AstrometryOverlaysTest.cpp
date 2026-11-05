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
#include "controls/BrightStarAnnotationDisplayModule.h"
#include "controls/CatalogAnnotationDisplayModule.h"
#include "controls/CoordinateGridDisplayModule.h"
#include "controls/DistortionMapDisplayModule.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidget.h"

#include <AfterNightEngine/Algorithms/Catalog/CatalogRecord.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetectionChannels.h>

#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
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

    const an::ui::BrightStarAnnotationProjectedPoint point =
        an::ui::detail::projectBrightStarAnnotation(*solution, object, 1.5);
    ASSERT_TRUE(point.isValid());
    EXPECT_NEAR(point.center.x(), 300.0, 1e-9);
    EXPECT_NEAR(point.center.y(), 225.0, 1e-9);
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

    // A fainter limit is another question.
    annotations->setMagnitudeLimit(14.0);
    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return annotations->projectedLabels().size() == 3;
    }));
    EXPECT_EQ(annotations->catalogQueriesAsked(), 2u);
}

TEST_F(AstrometryOverlaysTest, BrightStarLabelsPreferTheProperNameThenTheBayerLetter)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::BrightStarLabels);
    auto* labels = dynamic_cast<an::ui::BrightStarAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(labels, nullptr);
    labels->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));

    ASSERT_TRUE(pumpUntil([&]() {
        paint();
        return labels->projectedLabels().size() == 2;
    }));
    QVector<QString> drawn = labels->projectedLabels();
    std::sort(drawn.begin(), drawn.end());
    EXPECT_EQ(drawn, (QVector<QString>{QStringLiteral("Alpha Field"), QStringLiteral("bet Fld")}));
    EXPECT_EQ(labels->catalogQueriesAsked(), 1u);
}

TEST_F(AstrometryOverlaysTest, ACatalogThatCannotBeReadDrawsNothingAndIsNotAskedForAgain)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    auto* annotations = dynamic_cast<an::ui::CatalogAnnotationDisplayModule*>(overlay.get());
    ASSERT_NE(annotations, nullptr);
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
        std::unique_ptr<::ImageViewWidgetDisplayModule> overlay = attached(an::ui::AstrometryOverlay::BrightStarLabels);
        auto* labels = dynamic_cast<an::ui::BrightStarAnnotationDisplayModule*>(overlay.get());
        ASSERT_NE(labels, nullptr);
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

TEST_F(AstrometryOverlaysTest, TheMagnitudeLimitReachesTheLabelledOverlaysOnly)
{
    show(fieldCards());
    std::unique_ptr<::ImageViewWidgetDisplayModule> annotations =
        attached(an::ui::AstrometryOverlay::CatalogAnnotations);
    std::unique_ptr<::ImageViewWidgetDisplayModule> grid = attached(an::ui::AstrometryOverlay::CoordinateGrid);
    an::ui::setAstrometryOverlayMagnitudeLimit(*annotations, 10.5);
    an::ui::setAstrometryOverlayMagnitudeLimit(*grid, 10.5);
    EXPECT_DOUBLE_EQ(dynamic_cast<an::ui::CatalogAnnotationDisplayModule&>(*annotations).magnitudeLimit(), 10.5);
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
