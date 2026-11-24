// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The photometric and spectrophotometric colour calibrations as mirror pages: each in its column at 600 px of
/// height, in both densities, with every control in the column and reached by Tab and nothing clipped sideways where
/// text is measured; each giving through its page the result its window gives over the same plate-solved field; and
/// the photometric calibration's background region, drawn on the page's view of the target - the same rectangle of the
/// image, the same result - with nothing of it, nor of the matched stars, left on the target's own view; and each
/// calibration's run on its page stopped by the page's Cancel, with no progress window opened.

#include "Test_ProcessesPhotometric/PhotometricTestSupport.h"
#include "Test_ProcessesShared/PageLayoutCheck.h"
#include "Test_ProcessesShared/SelectionDrag.h"

#include "controls/ImageProcessControl.h"

#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::photometric;

constexpr double kRedCast = 1.25;
constexpr double kBlueCast = 0.8;
constexpr const char* kPhotometric = "color.actionPhotometric_Color_Calibration";
constexpr const char* kSpectrophotometric = "color.actionSpectrophotometric_Color_Calibration";

/// A widget of a process by its object name; fails the case when it has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// The cases of a calibration's page, over plate-solved fields whose catalogs are installed as the product installs
/// them: the Gaia catalog of the photometric field, the spectrum catalog and the response repository of the
/// spectrophotometric one. The fields are made once for the suite.
class CalibrationPage : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        s_photometric = std::make_unique<StarField>(makePhotometricField(kRedCast, kBlueCast));
        s_spectrophotometric = std::make_unique<StarField>(makeSpectrophotometricField(kRedCast, kBlueCast));
    }

    static void TearDownTestSuite()
    {
        s_photometric.reset();
        s_spectrophotometric.reset();
    }

    void SetUp() override
    {
        m_context = openProcessContext(4u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_catalogs.emplace(*m_context);
        ASSERT_TRUE(writeGaiaCatalog(m_catalogs->gaiaDirectory(), *s_photometric));
        ASSERT_TRUE(writeSpectrumCatalog(m_catalogs->spectrumDirectory(), *s_spectrophotometric));
        ASSERT_TRUE(writeResponseRepository(m_catalogs->responseDirectory()));
        m_catalogs->useGaiaCatalog();
        m_catalogs->useSpectrumCatalogAndRepository();
    }

    void TearDown() override
    {
        m_catalogs.reset();
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ProcessTestContext& context() const
    {
        return *m_context;
    }

    /// A view of the photometric field, owned by the case.
    [[nodiscard]] ViewHandle photometricTarget(const std::string& name) const
    {
        return fieldView<float>(m_context->engineSession(), *s_photometric, fieldMetadata(*s_photometric), name);
    }

    /// A view of the spectrophotometric field, owned by the case.
    [[nodiscard]] ViewHandle spectrophotometricTarget(const std::string& name) const
    {
        return fieldView<float>(m_context->engineSession(), *s_spectrophotometric, fieldMetadata(*s_spectrophotometric),
                                name);
    }

private:
    static std::unique_ptr<StarField> s_photometric;
    static std::unique_ptr<StarField> s_spectrophotometric;
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::optional<InstalledCatalogs> m_catalogs;
};

std::unique_ptr<StarField> CalibrationPage::s_photometric;
std::unique_ptr<StarField> CalibrationPage::s_spectrophotometric;

/// The two calibrations as pages.
class PageLayout : public CalibrationPage
{
};

/// The photometric calibration's background region on a page, and its run.
class PhotometricColorCalibration : public CalibrationPage
{
};

/// The spectrophotometric calibration's run on a page.
class SpectrophotometricColorCalibration : public CalibrationPage
{
};

/// Holds a calibration's page to its column; reported skipped where text is not measured as the interface measures it.
void fitsItsColumn(ProcessTestContext& context, const char* descriptorId, ViewHandle target)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ASSERT_NE(target, nullptr);
    context.addViewControl(target.get());
    context.setActiveImageViewControl(target.get());
    const bool measured =
        shared::expectPageFitsItsColumn(descriptorId, *target, context, an::ui::ProcessSurface::MirrorPage);
    context.removeViewControl(target.get(), false);
    if (!measured && !::testing::Test::HasFailure())
    {
        GTEST_SKIP() << "Tab reaches every control; whether anything is clipped is measured under the native platform, "
                        "where text is as wide as the interface draws it";
    }
}

/// Checks the photometric calibration's background region and starts marking it on the view the person works on.
void startMarkingTheRegion(QWidget& process)
{
    auto* const region = widgetOf<QGroupBox>(process, "backgroundRoiGroupBox");
    ASSERT_NE(region, nullptr);
    if (!region->isChecked())
    {
        region->setChecked(true);
        emit region->clicked(true);
    }
    auto* const select = widgetOf<QPushButton>(process, "backgroundRoiSelectionButton");
    ASSERT_NE(select, nullptr);
    ASSERT_TRUE(select->isEnabled());
    select->click();
}

} // namespace

TEST_F(PageLayout, PhotometricColorCalibrationFitsItsColumn)
{
    fitsItsColumn(context(), kPhotometric, photometricTarget("page target"));
}

TEST_F(PageLayout, PhotometricColorCalibrationAppliesAsInAWindow)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::expectMirrorPageAppliesAsTheWindow(kPhotometric, context(),
                                               [this]() { return photometricTarget("applied target"); });
}

// The background region drawn on the page's view of the field is the region the same drag draws on the field's own view
// in a window, and the page calibrates the field as the window does.
TEST_F(PhotometricColorCalibration, SelectionOnThePageGivesTheWindowsResult)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    const QRect drawn(40, 30, 200, 150);
    std::optional<fixtures::FixtureSamples> results[2];
    QRect read[2];
    for (const bool asPage : {false, true})
    {
        SCOPED_TRACE(asPage ? "in a page" : "in a window");
        ViewHandle field = photometricTarget("marked field");
        ASSERT_NE(field, nullptr);
        context().addViewControl(field.get());
        context().setActiveImageViewControl(field.get());
        PagePresenter presenter;
        presenter.adopting = asPage;
        context().presenter = &presenter;
        const QPointer<ImageProcessControl> process =
            dynamic_cast<ImageProcessControl*>(shared::launchWindowOf(kPhotometric, *field));
        ASSERT_NE(process, nullptr);
        ASSERT_EQ(process->hasPageLayout(), asPage);
        ImageViewControl* const marked = process->interactionView();
        ASSERT_NE(marked, nullptr);
        EXPECT_EQ(marked == field.get(), !asPage) << "a page marks on its own view of the field";
        ASSERT_TRUE(settleView(*marked));

        startMarkingTheRegion(*process);
        shared::dragNewSelection(*marked, drawn);
        EXPECT_EQ(marked->getViewWidget()->getCurrentSelectionArea(), drawn) << "the rectangle of the image drawn";
        read[asPage ? 1 : 0] = QRect(widgetOf<QSpinBox>(*process, "backgroundRoiLeftValue")->value(),
                                     widgetOf<QSpinBox>(*process, "backgroundRoiTopValue")->value(),
                                     widgetOf<QSpinBox>(*process, "backgroundRoiWidthValue")->value(),
                                     widgetOf<QSpinBox>(*process, "backgroundRoiHeightValue")->value());
        widgetOf<QPushButton>(*process, "backgroundRoiSelectionButton")->click();

        ASSERT_TRUE(pumpUntil([&process]() { return process.isNull() || process->triggerPrimaryAction(); }));
        ASSERT_TRUE(pumpUntil([&]() { return shared::mirrorRunEnded(process, *field); }));
        if (const std::shared_ptr<const an::consumer::ImageAsset> asset = field->document().asset())
        {
            std::string error;
            results[asPage ? 1 : 0] = fixtures::engine::readAssetSamples(*asset, error);
            EXPECT_TRUE(results[asPage ? 1 : 0].has_value()) << error;
        }
        shared::closeOpenedProcessWindows();
        context().presenter = nullptr;
        dropView(context(), field);
    }
    EXPECT_EQ(read[0], drawn) << "the window's dialog reads the rectangle drawn";
    EXPECT_EQ(read[1], read[0]) << "the page's dialog reads the same rectangle of the image";
    ASSERT_TRUE(results[0].has_value() && results[1].has_value());
    EXPECT_TRUE(fixtures::sameSamples(*results[0], *results[1])) << "the page calibrates as the window does";
}

// On a page the background region and the matched stars are drawn on the page's view only: the target's own view shows
// no selection and carries no overlay of the calibration.
TEST_F(PhotometricColorCalibration, TargetViewShowsNoSelectionInPageMode)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle field = photometricTarget("page field");
    ASSERT_NE(field, nullptr);
    context().addViewControl(field.get());
    context().setActiveImageViewControl(field.get());
    {
        PagePresenter presenter;
        context().presenter = &presenter;
        auto* const process =
            dynamic_cast<PhotometricColorCalibrationProcess*>(shared::launchWindowOf(kPhotometric, *field));
        ASSERT_NE(process, nullptr);
        ASSERT_TRUE(process->hasPageLayout());
        ImageViewControl* const marked = process->interactionView();
        ASSERT_NE(marked, field.get());
        ASSERT_NE(process->matchedStarOverlay(), nullptr);
        EXPECT_EQ(process->matchedStarOverlay()->getViewWidget(), marked->getViewWidget())
            << "the matched stars are drawn on the page's view";

        startMarkingTheRegion(*process);
        shared::dragNewSelection(*marked, QRect(20, 20, 100, 80));
        EXPECT_TRUE(marked->getViewWidget()->isSelectionAreaEnabled()) << "the page's view shows the region";
        EXPECT_FALSE(field->getViewWidget()->isSelectionAreaEnabled()) << "the target's view shows none";
        EXPECT_NE(field->getViewWidget()->getImageViewSelectionStyle(), ImageViewSelectionStyle::AREA_SELECTION_STYLE);
        shared::closeOpenedProcessWindows();
        context().presenter = nullptr;
    }
    dropView(context(), field);
}

// A calibration on the page shows its run in the page's row and opens no progress window; the page's Cancel stops it,
// and the field is left as it was.
TEST_F(PhotometricColorCalibration, ARunOnThePageStopsAtItsCancel)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle field = photometricTarget("stopped field");
    ASSERT_NE(field, nullptr);
    context().addViewControl(field.get());
    shared::expectMirrorPageRunStopsAtItsCancel(kPhotometric, context(), *field);
    dropView(context(), field);
}

// The spectrophotometric calibration on the page stops at the page's Cancel too, the field left as it was.
TEST_F(SpectrophotometricColorCalibration, ARunOnThePageStopsAtItsCancel)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle field = spectrophotometricTarget("stopped field");
    ASSERT_NE(field, nullptr);
    context().addViewControl(field.get());
    shared::expectMirrorPageRunStopsAtItsCancel(kSpectrophotometric, context(), *field);
    dropView(context(), field);
}

TEST_F(PageLayout, SpectrophotometricColorCalibrationFitsItsColumn)
{
    fitsItsColumn(context(), kSpectrophotometric, spectrophotometricTarget("page target"));
}

TEST_F(PageLayout, SpectrophotometricColorCalibrationAppliesAsInAWindow)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    shared::expectMirrorPageAppliesAsTheWindow(kSpectrophotometric, context(),
                                               [this]() { return spectrophotometricTarget("applied target"); });
}
