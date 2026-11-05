// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star-profiler windows over one detected star list: the two filter windows - the same rules behind two layouts,
/// each offering its three figures - previewing, applying and putting back the stars; the selection over a view, which
/// finds the star a click lands on at the engine pixel coordinate it was measured at; and the mask window, which paints
/// the stars through the engine on a worker and shows the mask in a new view beside the source.

#include "Test_ControlsStatistics/StatisticsTestSupport.h"

#include "controls/ImageStarSelectionController.h"
#include "controls/ProfiledStarMaskCreatorControl.h"
#include "controls/StarFilter.h"
#include "controls/StarsFilterControlHandler.h"
#include "controls/StarsFilterControlV1.h"
#include "controls/StarsFilterControlV2.h"
#include "widgets/ImageViewWidget.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::statistics::Channels;
using an::tests::statistics::InstalledContext;
using an::tests::statistics::makeDocument;
using an::tests::statistics::makeView;
using an::tests::statistics::openStatisticsContext;
using an::tests::statistics::pumpQuietly;
using an::tests::statistics::pumpUntil;
using an::tests::statistics::settle;
using an::tests::statistics::starAt;
using an::tests::statistics::StatisticsTestContext;
using an::tests::statistics::ViewHandle;

/// A star-profiler process as the star windows see it: the stars it holds, the view they were detected on, and every
/// selection it was asked to show.
class StandInStarProcess final : public QWidget, public StarsFilterControlHandler
{
public:
    StandInStarProcess(ImageViewControl* view, an::ui::StarListPtr stars) : m_view(view), m_stars(std::move(stars)) {}

    an::ui::StarListPtr getCurrentStarList() override
    {
        return m_stars;
    }

    void setCurrentStarList(const an::ui::StarListPtr& starList, bool) override
    {
        m_stars = starList;
        ++listsSet;
    }

    ImageViewControl* getViewControl() override
    {
        return m_view;
    }

    void updateStarSelection(const an::ui::StarListPtr& starSelection) override
    {
        selections.push_back(starSelection);
    }

    std::vector<an::ui::StarListPtr> selections;
    int listsSet = 0;

private:
    ImageViewControl* m_view;
    an::ui::StarListPtr m_stars;
};

/// Six stars: magnitudes from -10 to -5, full widths from 2 to 4.5 along the major axis.
[[nodiscard]] an::ui::StarListPtr sixStars()
{
    auto stars = std::make_shared<an::ui::StarList>();
    for (int index = 0; index < 6; ++index)
    {
        stars->push_back(starAt(10.0 + 20.0 * index, 10.0, 2.0 + 0.5 * index, 2.0, 0.1 * (index + 1), -10.0 + index));
    }
    return stars;
}

/// Records the star a selection reports.
class Selector : public ImageStarSelectorControl
{
public:
    void onStarSelected(size_t index) override
    {
        selected.push_back(index);
    }

    std::vector<size_t> selected;
};

class StarWindowsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openStatisticsContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("stars");
        std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
            m_context->engineSession(), 160u, 40u, Channels::Grey,
            [](std::uint32_t, std::uint32_t, std::uint32_t) { return static_cast<std::uint16_t>(1000u); });
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*m_view));
    }

    void TearDown() override
    {
        if (m_view)
        {
            (void)settle(*m_view);
        }
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<StatisticsTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
};

} // namespace

// The magnitude window offers the magnitude brightest first and the two full widths; its ranges start at the list's
// limits and keep every star, and narrowing one previews only the stars it holds.
TEST_F(StarWindowsTest, TheMagnitudeWindowPreviewsTheStarsItsRangesHold)
{
    StandInStarProcess process(m_view.get(), sixStars());
    auto* window = new StarsFilterControlV2(&process);
    const auto& ui = window->ui();
    EXPECT_DOUBLE_EQ(ui.magMinValue->value(), -5.0);
    EXPECT_DOUBLE_EQ(ui.magMaxValue->value(), -10.0);
    EXPECT_DOUBLE_EQ(ui.fwhmxMinValue->value(), 2.0);
    EXPECT_DOUBLE_EQ(ui.fwhmxMaxValue->value(), 4.5);
    EXPECT_TRUE(window->filterRequest().ranges.empty());
    EXPECT_EQ(ui.filteredStarsTxt->text(), QStringLiteral("6/6"));

    ui.previewCheck->setChecked(true);
    process.selections.clear();
    ui.fwhmxMaxValue->setValue(3.5);
    ASSERT_FALSE(process.selections.empty());
    EXPECT_EQ(process.selections.back()->size(), 4u);
    EXPECT_EQ(ui.filteredStarsTxt->text(), QStringLiteral("4/6"));

    // The faint end moved toward the bright one leaves the faint stars out.
    ui.magMinValue->setValue(-8.0);
    EXPECT_EQ(process.selections.back()->size(), 3u);
    ASSERT_EQ(window->filterRequest().ranges.size(), 2u);
    window->close();
}

// Applying asks first; on Yes the process holds the filtered stars, on No nothing changes. A window closed without
// applying shows the stars it started from again.
TEST_F(StarWindowsTest, ApplyingAsksAndClosingWithoutApplyingPutsTheStarsBack)
{
    const an::ui::StarListPtr original = sixStars();
    StandInStarProcess process(m_view.get(), original);
    auto* window = new StarsFilterControlV2(&process);
    QPointer<StarsFilterControlV2> guard(window);
    window->show();
    window->ui().fwhmxMaxValue->setValue(3.0);
    QAbstractButton* apply = window->ui().buttonBox->button(QDialogButtonBox::Ok);
    if (apply == nullptr)
    {
        apply = window->ui().buttonBox->button(QDialogButtonBox::Apply);
    }
    ASSERT_NE(apply, nullptr);

    m_context->answerYes = false;
    apply->click();
    EXPECT_EQ(process.listsSet, 0);
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("question:Apply filter")));

    process.selections.clear();
    window->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(guard.isNull());
    ASSERT_FALSE(process.selections.empty());
    EXPECT_EQ(process.selections.back(), original);

    auto* second = new StarsFilterControlV2(&process);
    second->show();
    second->ui().fwhmxMaxValue->setValue(3.0);
    m_context->answerYes = true;
    QAbstractButton* secondApply = second->ui().buttonBox->button(QDialogButtonBox::Ok);
    if (secondApply == nullptr)
    {
        secondApply = second->ui().buttonBox->button(QDialogButtonBox::Apply);
    }
    secondApply->click();
    pumpQuietly();
    EXPECT_EQ(process.listsSet, 1);
    EXPECT_EQ(process.getCurrentStarList()->size(), 3u);
}

// The intensity window offers the fitted amplitude, the roundness figure and the radius - the same stars, the same
// rules, another layout.
TEST_F(StarWindowsTest, TheIntensityWindowOffersItsThreeFiguresOverTheSameStars)
{
    StandInStarProcess process(m_view.get(), sixStars());
    auto* window = new StarsFilterControlV1(&process);
    const auto& ui = window->ui();
    EXPECT_NEAR(ui.intensityMinValue->value(), 0.1, 0.01);
    EXPECT_NEAR(ui.intensityMaxValue->value(), 0.6, 0.01);
    EXPECT_NEAR(ui.radiusMinValue->value(), 1.0, 0.01);
    EXPECT_NEAR(ui.radiusMaxValue->value(), 1.625, 0.01);

    ui.previewCheck->setChecked(true);
    ui.intensityMinValue->setValue(0.4);
    ASSERT_FALSE(process.selections.empty());
    EXPECT_EQ(process.selections.back()->size(), 3u);
    const an::ui::StarFilterRequest request = window->filterRequest();
    ASSERT_EQ(request.ranges.size(), 1u);
    EXPECT_EQ(request.ranges.front().field, an::ui::StarFilterField::Amplitude);
    window->close();
}

// The selection finds the star a position lands on at its engine pixel coordinate times the view's scale, draws them,
// and lets them go when it is turned off.
TEST_F(StarWindowsTest, TheSelectionFindsTheStarAPositionLandsOn)
{
    Selector selector;
    ImageStarSelectionController controller(m_view->getScrollArea(), &selector);
    controller.attachToViewWidget(m_view->getViewWidget());
    controller.updateStarSelection(sixStars());
    ASSERT_EQ(controller.getStarSelection()->size(), 6u);

    // At a zoom other than one, so a position is found at its coordinate times the scale and not at the coordinate.
    m_view->zoomIn();
    m_view->zoomIn();
    ASSERT_TRUE(settle(*m_view));
    const double scale = m_view->getViewWidget()->getCurrentScaleFactor();
    ASSERT_GT(std::abs(scale - 1.0), 0.2);
    const auto widgetPoint = [scale](double x, double y) {
        return QPoint(static_cast<int>(x * scale), static_cast<int>(y * scale));
    };
    EXPECT_EQ(controller.starAt(widgetPoint(50.0, 10.0)), std::optional<size_t>(2u));
    EXPECT_EQ(controller.starAt(widgetPoint(10.0, 10.0)), std::optional<size_t>(0u));
    EXPECT_FALSE(controller.starAt(widgetPoint(150.0, 35.0)).has_value());
    EXPECT_FALSE(m_view->getViewWidget()->grab().toImage().isNull());

    controller.enableStarSelection(false);
    EXPECT_TRUE(controller.getStarSelection()->empty());
    controller.updateStarSelection(nullptr);
    EXPECT_TRUE(controller.getStarSelection()->empty());
}

// The mask is a grey Float32 image of the frame's size: solid ellipses at one where stars are and zero elsewhere, edges
// softened by a blur, or a radial falloff per star.
TEST_F(StarWindowsTest, TheMaskIsPaintedThroughTheEngine)
{
    const an::ui::StarList stars{starAt(20.5, 20.5, 3.0, 3.0), starAt(100.5, 20.5, 2.0, 2.0)};
    const auto at = [](const an::consumer::ImageAsset& mask, std::uint32_t x, std::uint32_t y) {
        return an::tests::viewer::sampleOf<float>(mask, x, y).value_or(-1.0f);
    };

    an::Result<an::consumer::ImageAsset> solid =
        an::ui::makeProfiledStarMask(m_context->engineSession(), stars, 160u, 40u, an::ui::ProfiledStarMaskRequest{});
    ASSERT_TRUE(solid.hasValue()) << solid.status().message();
    ASSERT_TRUE(solid->descriptor().has_value());
    EXPECT_EQ(solid->descriptor()->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(solid->descriptor()->extent().width(), 160u);
    EXPECT_EQ(solid->descriptor()->extent().channelCount(), 1u);
    EXPECT_FLOAT_EQ(at(*solid, 20u, 20u), 1.0f);
    EXPECT_FLOAT_EQ(at(*solid, 100u, 20u), 1.0f);
    EXPECT_FLOAT_EQ(at(*solid, 60u, 20u), 0.0f);
    EXPECT_FLOAT_EQ(at(*solid, 20u, 38u), 0.0f);

    an::Result<an::consumer::ImageAsset> softened = an::ui::makeProfiledStarMask(
        m_context->engineSession(), stars, 160u, 40u, an::ui::ProfiledStarMaskRequest{.softenEdges = 2.0});
    ASSERT_TRUE(softened.hasValue()) << softened.status().message();
    const float edge = at(*softened, 26u, 20u);
    EXPECT_GT(edge, 0.0f);
    EXPECT_LT(edge, 1.0f);

    an::Result<an::consumer::ImageAsset> shaped = an::ui::makeProfiledStarMask(
        m_context->engineSession(), stars, 160u, 40u,
        an::ui::ProfiledStarMaskRequest{.shaped = true, .shape = an::StarMaskShape::Linear, .sizeScale = 1.0});
    ASSERT_TRUE(shaped.hasValue()) << shaped.status().message();
    EXPECT_GT(at(*shaped, 20u, 20u), at(*shaped, 23u, 20u));
    EXPECT_GT(at(*shaped, 23u, 20u), 0.0f);
}

// The mask window paints on a worker and the mask lands in a new view beside the source, named for it.
TEST_F(StarWindowsTest, TheMaskWindowShowsTheMaskInANewViewBesideTheSource)
{
    StandInStarProcess process(m_view.get(), sixStars());
    auto* window = new ProfiledStarMaskCreatorControl(&process);
    const an::ui::ProfiledStarMaskRequest request = window->maskRequest();
    EXPECT_FALSE(request.shaped);
    EXPECT_DOUBLE_EQ(request.scaleFactor, 1.0);
    EXPECT_DOUBLE_EQ(request.sizeScale, 0.5);
    EXPECT_EQ(request.shape, an::StarMaskShape::TruncatedBell);
    window->ui().shapeSelector->setCurrentText(QStringLiteral("Cubic"));
    EXPECT_EQ(window->maskRequest().shape, an::StarMaskShape::Cubic);

    ASSERT_TRUE(window->generateMask());
    ASSERT_TRUE(pumpUntil([this]() { return !m_context->madeViews().empty(); }));
    ImageViewControl* made = m_context->madeViews().front();
    EXPECT_EQ(made->document().names().displayName, QStringLiteral("stars - STARMASK"));
    EXPECT_EQ(m_context->madeNeighbours.front(), m_view.get());
    EXPECT_EQ(m_context->madeHistories.front(), "Star Profiler: Starmask image created from 'stars'");
    ASSERT_TRUE(made->imageDescriptor().has_value());
    EXPECT_EQ(made->imageDescriptor()->extent().width(), 160u);
    ASSERT_TRUE(settle(*made));
    window->close();
}

TEST(StarMaskShapeTextTest, EveryShapeEntryNamesItsShape)
{
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Bell")), an::StarMaskShape::Bell);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Truncated Bell")), an::StarMaskShape::TruncatedBell);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Linear")), an::StarMaskShape::Linear);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Truncated Linear")), an::StarMaskShape::TruncatedLinear);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Cubic")), an::StarMaskShape::Cubic);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Quadratic")), an::StarMaskShape::Quadratic);
    EXPECT_EQ(an::ui::starMaskShapeFromText(QStringLiteral("Other")), an::StarMaskShape::TruncatedBell);
}
