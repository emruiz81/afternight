// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The photometric colour calibration window: the launch through its registry descriptor, what it offers and refuses
/// for a target, the gains and the neutral sky an Apply writes over a field whose colours are planted, the sky region,
/// the mask, the history step, the result's metadata and cards, the star finder and request, the dialog's defaults,
/// every representation an image can hold, a stopped run and a run the catalog cannot support.

#include "Test_ProcessesPhotometric/PhotometricTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "app/ProcessRegistry.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/PhotometricCalibrationSupport.h"

#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::photometric;

constexpr double kRedCast = 1.25;
constexpr double kBlueCast = 0.8;

class PhotometricWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openProcessContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_catalogs.emplace(*m_context);
        m_field = makePhotometricField(kRedCast, kBlueCast);
        ASSERT_TRUE(writeGaiaCatalog(m_catalogs->gaiaDirectory(), m_field));
        m_catalogs->useGaiaCatalog();
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

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    [[nodiscard]] const StarField& field() const
    {
        return m_field;
    }

    [[nodiscard]] StarField& mutableField()
    {
        return m_field;
    }

    [[nodiscard]] InstalledCatalogs& catalogs()
    {
        return *m_catalogs;
    }

    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 4u;
    }

private:
    std::unique_ptr<ProcessTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::optional<InstalledCatalogs> m_catalogs;
    StarField m_field;
};

/// The window over a session one operation wide, where work that holds the only thread while it waits for an
/// image's writer stops everything.
class PhotometricWindowTestAtWidthOne : public PhotometricWindowTest
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

/// A window whose run is stopped as it is issued, so the engine sees a stopped execution at its first checkpoint.
class StoppingPhotometric final : public PhotometricColorCalibrationProcess
{
public:
    using PhotometricColorCalibrationProcess::PhotometricColorCalibrationProcess;

protected:
    void calibrationStarting(std::stop_source& stop) override
    {
        stop.request_stop();
    }
};

[[nodiscard]] QPushButton* buttonOf(QWidget& window, QDialogButtonBox::StandardButton which)
{
    auto* box = window.findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    return box != nullptr ? box->button(which) : nullptr;
}

template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// The red and blue of a star relative to its green over the sky, in planar values.
[[nodiscard]] std::array<double, 2u> starRatios(const StarField& field, const std::vector<double>& values,
                                                const PlantedStar& star)
{
    const std::size_t plane = static_cast<std::size_t>(field.width) * field.height;
    const std::size_t at =
        static_cast<std::size_t>(std::floor(star.y)) * field.width + static_cast<std::size_t>(std::floor(star.x));
    const double red = values[at] - skyMedian(field, values, 0u);
    const double green = values[plane + at] - skyMedian(field, values, 1u);
    const double blue = values[2u * plane + at] - skyMedian(field, values, 2u);
    return {red / green, blue / green};
}

/// The colour model's red and blue relative to green for a star's colour.
[[nodiscard]] std::array<double, 2u> modelRatios(const PlantedStar& star)
{
    const double clamped = std::clamp(star.colorIndex, -0.5, 3.5);
    return {std::exp(0.32 * clamped), std::exp(-0.40 * clamped)};
}

} // namespace

// launch: the registered action opens the window over the active view, attached under its name and needing the view
// to itself.
TEST_F(PhotometricWindowTest, TheRegisteredActionOpensTheWindowOverTheActiveView)
{
    const shared::RegistrySnapshot snapshot;
    const shared::ClosesOpenedWindows closesWindows;
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    context().addViewControl(target.get());
    context().setActiveImageViewControl(target.get());

    const an::ui::process_registry::ProcessDescriptor* descriptor =
        shared::registered("color.actionPhotometric_Color_Calibration");
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->process_name, "Photometric Color Calibration");
    EXPECT_TRUE(descriptor->requires_exclusive_access);
    EXPECT_FALSE(descriptor->requires_still_image);
    ASSERT_TRUE(descriptor->launch_single_image);
    descriptor->launch_single_image(target.get());

    ASSERT_EQ(shared::openedProcessWindows().size(), 1u);
    EXPECT_EQ(shared::openedProcessWindows()[0].className, "PhotometricColorCalibrationProcess");
    ASSERT_EQ(context().openProcesses.size(), 1u) << "the launch opens one window";
    auto* window = dynamic_cast<PhotometricColorCalibrationProcess*>(context().openProcesses.front());
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->attachmentName(), "Photometric Color Calibration");
    EXPECT_TRUE(window->requiresExclusiveAccess());
    EXPECT_TRUE(window->isVisible());
    window->close();
    ASSERT_TRUE(pumpUntil([this]() { return context().openProcesses.empty(); }));
    dropView(context(), target);
}

// apply: the calibration finds the planted casts - the red and blue gains undo them with green the reference - and the
// sky of every channel lands on one shared level.
TEST_F(PhotometricWindowTest, AnApplyUndoesThePlantedCastsAndNeutralizesTheSky)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    QPushButton* apply = buttonOf(*window, QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    EXPECT_TRUE(apply->isEnabled()) << context().errorTexts.join(QStringLiteral("; ")).toStdString();

    ASSERT_TRUE(applyPhotometric(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> values = documentValues(target->document());
    ASSERT_FALSE(values.empty());
    for (const std::size_t index : {std::size_t{3}, std::size_t{10}, std::size_t{25}})
    {
        const PlantedStar& star = field().stars[index];
        const std::array<double, 2u> measured = starRatios(field(), values, star);
        const std::array<double, 2u> model = modelRatios(star);
        EXPECT_NEAR(measured[0], model[0], 0.03 * model[0]) << "red of star " << index;
        EXPECT_NEAR(measured[1], model[1], 0.03 * model[1]) << "blue of star " << index;
    }
    EXPECT_NEAR(skyMedian(field(), values, 0u), skyMedian(field(), values, 1u), 0.002);
    EXPECT_NEAR(skyMedian(field(), values, 2u), skyMedian(field(), values, 1u), 0.002);
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Photometric Color Calibration")), 1);
    auto* status = widgetOf<QLabel>(*window, "statusLabel");
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("Gains R="))) << status->text().toStdString();
}

// apply: the overlay draws the stars the fit used where the engine measured them, green where the catalog gave the
// colour; the check box shows and hides it.
TEST_F(PhotometricWindowTest, TheMatchedStarsAreShownWhereTheEngineMeasuredThem)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    ASSERT_NE(window->matchedStarOverlay(), nullptr);
    const std::vector<an::PhotometricColorSample> samples = window->matchedStarOverlay()->matchedSamples();
    ASSERT_GE(samples.size(), 10u);
    for (const an::PhotometricColorSample& sample : samples)
    {
        double nearest = 1.0e9;
        for (const PlantedStar& star : field().stars)
        {
            nearest = (std::min)(nearest, std::hypot(sample.position.x - star.x, sample.position.y - star.y));
        }
        EXPECT_LT(nearest, 0.5) << "a matched star is drawn where a planted star is";
        EXPECT_TRUE(sample.colorIndexBpMinusRp.has_value());
    }
    auto* show = widgetOf<QCheckBox>(*window, "showMatchedStars");
    ASSERT_NE(show, nullptr);
    EXPECT_TRUE(window->matchedStarOverlay()->isDisplayModuleEnabled());
    show->setChecked(false);
    EXPECT_FALSE(window->matchedStarOverlay()->isDisplayModuleEnabled());
    EXPECT_EQ(window->matchedStarOverlay()->matchedSamples().size(), samples.size()) << "hiding them keeps them";
    show->setChecked(true);
    EXPECT_TRUE(window->matchedStarOverlay()->isDisplayModuleEnabled());
}

// detection choices: the window finds stars with a fixed star finder and asks the engine for two thousand stars a
// channel, three-pixel pairing and a ten-sample floor, with the dialog's match, magnitude and sky.
TEST_F(PhotometricWindowTest, TheRunUsesAFixedStarFinderAndItsRequest)
{
    const an::StarDetectionRequest detection = an::ui::processes::photometricStarDetection();
    EXPECT_EQ(detection.searchRadius, 5u);
    EXPECT_DOUBLE_EQ(detection.noiseSigma, 1.0);
    EXPECT_DOUBLE_EQ(detection.minimumRoundness, 0.4);
    EXPECT_EQ(detection.model, an::PsfModel::Gaussian);
    EXPECT_TRUE(detection.relaxedChecks);

    an::ui::processes::PhotometricCalibrationSettings settings;
    const an::PhotometricColorCalibrationRequest defaults =
        an::ui::processes::photometricCalibrationRequest(settings, "L");
    EXPECT_EQ(defaults.maximumStarsPerChannel, 2000u);
    EXPECT_DOUBLE_EQ(defaults.match.matchRadiusArcseconds, 5.0);
    EXPECT_EQ(defaults.match.magnitudeLimit, std::optional<double>(14.0));
    EXPECT_DOUBLE_EQ(defaults.channelMatchRadiusPixels, 3.0);
    EXPECT_EQ(defaults.fit.minimumSamples, 10u);
    EXPECT_TRUE(defaults.background.neutralize);
    EXPECT_FALSE(defaults.background.region.has_value());
    EXPECT_EQ(defaults.filterName, "L");

    settings.magnitudeLimit = 12.5;
    settings.matchRadiusArcseconds = 7.0;
    settings.backgroundRegion = QRect(10, 20, 30, 40);
    const an::PhotometricColorCalibrationRequest asked =
        an::ui::processes::photometricCalibrationRequest(settings, std::string_view());
    EXPECT_EQ(asked.match.magnitudeLimit, std::optional<double>(12.5));
    EXPECT_DOUBLE_EQ(asked.match.matchRadiusArcseconds, 7.0);
    ASSERT_TRUE(asked.background.region.has_value());
    EXPECT_EQ(asked.background.region->originX, 10u);
    EXPECT_EQ(asked.background.region->originY, 20u);
    EXPECT_EQ(asked.background.region->width, 30u);
    EXPECT_EQ(asked.background.region->height, 40u);
    settings.neutralizeBackground = false;
    const an::PhotometricColorCalibrationRequest unneutral =
        an::ui::processes::photometricCalibrationRequest(settings, std::string_view());
    EXPECT_FALSE(unneutral.background.neutralize);
    EXPECT_FALSE(unneutral.background.region.has_value()) << "a region is read only while the sky is neutralized";
}

// persistence: the dialog opens at its defaults, a window opened again does too, and Reset restores them after a
// person changed them.
TEST_F(PhotometricWindowTest, TheDialogOpensAtItsDefaultsAndResetRestoresThem)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    {
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
        const an::ui::processes::PhotometricCalibrationSettings opened = window->settings();
        EXPECT_DOUBLE_EQ(opened.magnitudeLimit, 14.0);
        EXPECT_DOUBLE_EQ(opened.matchRadiusArcseconds, 5.0);
        EXPECT_TRUE(opened.neutralizeBackground);
        EXPECT_TRUE(opened.backgroundRegion.isNull());
        widgetOf<QDoubleSpinBox>(*window, "magnitudeLimitSpin")->setValue(11.0);
        widgetOf<QDoubleSpinBox>(*window, "matchRadiusSpin")->setValue(9.0);
        widgetOf<QCheckBox>(*window, "showMatchedStars")->setChecked(false);
        widgetOf<QGroupBox>(*window, "backgroundRoiGroupBox")->setChecked(true);
        EXPECT_DOUBLE_EQ(window->settings().magnitudeLimit, 11.0);
        buttonOf(*window, QDialogButtonBox::Reset)->click();
        const an::ui::processes::PhotometricCalibrationSettings reset = window->settings();
        EXPECT_DOUBLE_EQ(reset.magnitudeLimit, 14.0);
        EXPECT_DOUBLE_EQ(reset.matchRadiusArcseconds, 5.0);
        EXPECT_TRUE(reset.backgroundRegion.isNull());
        EXPECT_TRUE(widgetOf<QCheckBox>(*window, "showMatchedStars")->isChecked());
        widgetOf<QDoubleSpinBox>(*window, "magnitudeLimitSpin")->setValue(9.5);
    }
    WindowHandle<PhotometricColorCalibrationProcess> again = openPhotometric(*target);
    EXPECT_DOUBLE_EQ(again->settings().magnitudeLimit, 14.0) << "the window stores nothing between openings";
}

// apply: a background region is where the sky is measured - a field whose red sky is brighter over its left three
// quarters becomes neutral in the right quarter when the region is there, and is not when the whole frame is measured.
TEST_F(PhotometricWindowTest, TheBackgroundRegionIsWhereTheSkyIsMeasured)
{
    StarField& lifted = mutableField();
    const std::uint32_t edge = lifted.width * 3u / 4u;
    for (std::uint32_t y = 0u; y < lifted.height; ++y)
    {
        for (std::uint32_t x = 0u; x < edge; ++x)
        {
            lifted.samples[static_cast<std::size_t>(y) * lifted.width + x] += 0.03f;
        }
    }
    const auto quarterSky = [this, edge](const std::vector<double>& values, std::uint32_t channel) {
        const StarField& sky = field();
        std::vector<double> samples;
        for (std::uint32_t y = 0u; y < sky.height; y += 2u)
        {
            for (std::uint32_t x = edge + 4u; x < sky.width; x += 2u)
            {
                const bool nearStar = std::any_of(sky.stars.begin(), sky.stars.end(), [x, y](const PlantedStar& star) {
                    return std::hypot(x + 0.5 - star.x, y + 0.5 - star.y) < 10.0;
                });
                if (!nearStar)
                {
                    samples.push_back(values[(static_cast<std::size_t>(channel) * sky.height + y) * sky.width + x]);
                }
            }
        }
        std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2u),
                         samples.end());
        return samples[samples.size() / 2u];
    };

    ViewHandle whole = fieldView<float>(session(), field(), fieldMetadata(field()), "whole");
    ASSERT_NE(whole, nullptr);
    {
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*whole);
        ASSERT_TRUE(applyPhotometric(*window, *whole));
    }
    const std::vector<double> wholeValues = documentValues(whole->document());

    ViewHandle region = fieldView<float>(session(), field(), fieldMetadata(field()), "region");
    ASSERT_NE(region, nullptr);
    {
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*region);
        widgetOf<QGroupBox>(*window, "backgroundRoiGroupBox")->setChecked(true);
        widgetOf<QSpinBox>(*window, "backgroundRoiLeftValue")->setValue(static_cast<int>(edge));
        widgetOf<QSpinBox>(*window, "backgroundRoiTopValue")->setValue(0);
        widgetOf<QSpinBox>(*window, "backgroundRoiWidthValue")->setValue(static_cast<int>(field().width - edge));
        widgetOf<QSpinBox>(*window, "backgroundRoiHeightValue")->setValue(static_cast<int>(field().height));
        ASSERT_EQ(
            window->settings().backgroundRegion,
            QRect(static_cast<int>(edge), 0, static_cast<int>(field().width - edge), static_cast<int>(field().height)));
        ASSERT_TRUE(applyPhotometric(*window, *region));
    }
    const std::vector<double> regionValues = documentValues(region->document());
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_NEAR(quarterSky(regionValues, 0u), quarterSky(regionValues, 1u), 0.002) << "the region's sky is neutral";
    EXPECT_GT(std::abs(quarterSky(wholeValues, 0u) - quarterSky(wholeValues, 1u)), 0.01)
        << "measured over the whole frame, the right quarter's sky is not";
}

// apply-mask: a mask on the target weighs the result pixel by pixel - the source where it is black, the result where
// it is white.
TEST_F(PhotometricWindowTest, TheTargetsMaskWeighsTheResultPixelByPixel)
{
    ViewHandle plain = fieldView<float>(session(), field(), fieldMetadata(field()), "plain");
    ASSERT_NE(plain, nullptr);
    const std::vector<double> source = documentValues(plain->document());
    {
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*plain);
        ASSERT_TRUE(applyPhotometric(*window, *plain));
    }
    const std::vector<double> unmasked = documentValues(plain->document());

    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "masked");
    ASSERT_NE(target, nullptr);
    const std::uint32_t half = field().width / 2u;
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), field().width, field().height,
                     [half](std::uint32_t x, std::uint32_t) { return x < half ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    {
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
        ASSERT_TRUE(applyPhotometric(*window, *target));
    }
    const std::vector<double> masked = documentValues(target->document());
    ASSERT_EQ(masked.size(), source.size());
    ASSERT_EQ(unmasked.size(), source.size());
    const std::size_t plane = static_cast<std::size_t>(field().width) * field().height;
    double largest = 0.0;
    for (std::size_t index = 0u; index < masked.size(); ++index)
    {
        const std::uint32_t x = static_cast<std::uint32_t>((index % plane) % field().width);
        const double expected = x < half ? source[index] : unmasked[index];
        largest = (std::max)(largest, std::abs(masked[index] - expected));
    }
    EXPECT_LE(largest, 1.0e-6);
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(metadata->semantics().processingState().colorCalibrated) << "the weighed result keeps its record";
    EXPECT_EQ(an::readColorCalibrationCards(metadata->codec()).record.calibration,
              an::RecordedColorCalibration::Photometric);
    target->removeCurrentMask();
}

// leases: a run reads the view's mask under a lease it takes before it asks to be admitted, in one order with its
// target's writer: an edit of the mask's view holds that writer and then asks to be admitted, so at a width of one a
// run admitted first would hold the only thread while it waited for the writer, and the edit would wait for it.
TEST_F(PhotometricWindowTestAtWidthOne, ARunWaitingForAnEditOfItsMaskHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "masked");
    ASSERT_NE(target, nullptr);
    const std::uint32_t half = field().width / 2u;
    std::optional<an::ui::ImageDocument> maskDocument =
        greyDocument(session(), field().width, field().height,
                     [half](std::uint32_t x, std::uint32_t) { return x < half ? 0.0f : 1.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    ViewHandle mask = makeTargetView("mask", std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    target->setEnableMask(true);
    ASSERT_TRUE(target->isMasked());
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    window->applyCalibration();
    pumpQuietly();
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the run held while the run waited for the mask's writer";

    ASSERT_TRUE(settlePhotometric(*window, *target));
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_TRUE(target->viewHistory()->hasUndo()) << "the run was not shown";
    window.reset();
    target->removeCurrentMask();
}

// history: an Apply is one step named after the process, and undoing it brings the image back.
TEST_F(PhotometricWindowTest, AnApplyIsOneHistoryStepThatUndoes)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    an::ui::IViewHistory* history = target->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->hasUndo());
    EXPECT_EQ(history->previousStepName(), "Photometric Color Calibration");
    EXPECT_NE(documentValues(target->document()), source);
    window.reset();
    ASSERT_TRUE(history->undo());
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo(); }));
    ASSERT_TRUE(settleView(*target));
    EXPECT_EQ(documentValues(target->document()), source);
}

// metadata: the result means what it meant, colour calibrated; its lineage names the engine operation and the recipe;
// the cards record the calibration - photometric, the gains and offsets, the catalog and the recipe's version - with
// the processing state that says so; the solution and the acquisition record stay.
TEST_F(PhotometricWindowTest, TheResultIsColourCalibratedAndRecordsItsCalibration)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(metadata->semantics().processingState().colorCalibrated);
    ASSERT_FALSE(metadata->provenance().entries().empty());
    const an::ProvenanceEntry& entry = metadata->provenance().entries().back();
    EXPECT_EQ(entry.operation(), "an::calibratePhotometricColor");
    EXPECT_NE(entry.detail().find("pcc-gaia-bp-rp-v1"), std::string::npos) << entry.detail();

    const an::ColorCalibrationReading reading = an::readColorCalibrationCards(metadata->codec());
    EXPECT_EQ(reading.record.calibration, an::RecordedColorCalibration::Photometric);
    EXPECT_NEAR(reading.record.gains[0], 1.0 / kRedCast, 0.03 / kRedCast);
    EXPECT_DOUBLE_EQ(reading.record.gains[1], 1.0);
    EXPECT_NEAR(reading.record.gains[2], 1.0 / kBlueCast, 0.03 / kBlueCast);
    EXPECT_EQ(reading.record.algorithmVersion, "pcc-gaia-bp-rp-v1");
    EXPECT_FALSE(reading.record.catalogFamilyId.empty());
    EXPECT_EQ(reading.record.inputLinearity, an::RecordedLinearity::Linear) << "the state of the image it was run on";
    EXPECT_EQ(reading.record.inputChannelBasis, an::RecordedChannelBasis::LinearRgb);
    EXPECT_EQ(an::readProcessingStateCards(metadata->codec()).colorCalibration,
              an::RecordedColorCalibration::Photometric);
    const std::optional<an::ImageDescriptor> descriptor = target->document().descriptor();
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_TRUE(an::ui::processes::calibrationSolutionOf(*metadata, *descriptor).has_value()) << "the solution stays";
    EXPECT_EQ(metadata->acquisition().instrumentName(), kSpccCameraModel);

    // A saved file carries the calibration: an image opened from it reads the same record.
    QTemporaryDir saved;
    ASSERT_TRUE(saved.isValid());
    const std::optional<an::ImageMetadata> reopened =
        writtenAndReadBack(session(), target->document(), saved.filePath(QStringLiteral("calibrated.fits")));
    ASSERT_TRUE(reopened.has_value());
    const an::ColorCalibrationReading fromFile = an::readColorCalibrationCards(reopened->codec());
    EXPECT_EQ(fromFile.record.calibration, an::RecordedColorCalibration::Photometric);
    EXPECT_EQ(fromFile.record.algorithmVersion, "pcc-gaia-bp-rp-v1");
    EXPECT_EQ(fromFile.record.catalogFamilyId, reading.record.catalogFamilyId);
    for (std::uint32_t channel = 0u; channel < 3u; ++channel)
    {
        EXPECT_NEAR(fromFile.record.gains[channel], reading.record.gains[channel], 1.0e-9) << "channel " << channel;
        EXPECT_NEAR(fromFile.record.offsets[channel], reading.record.offsets[channel], 1.0e-9) << "channel " << channel;
    }
    EXPECT_EQ(fromFile.record.inputChannelBasis, an::RecordedChannelBasis::LinearRgb);
    EXPECT_TRUE(an::ui::processes::calibrationSolutionOf(*reopened, *descriptor).has_value())
        << "the file keeps the solution";
}

// representation: every representation is calibrated as fractions of full scale - an integer as a fraction of its
// range, a signed one counted from its smallest value - and the result is kept in it.
TEST_F(PhotometricWindowTest, EveryRepresentationIsCalibratedAsFractionsOfFullScaleAndKept)
{
    const auto check = [this]<typename SampleT>(const char* name, double tolerance) {
        SCOPED_TRACE(name);
        ViewHandle target = fieldView<SampleT>(session(), field(), fieldMetadata(field()), name);
        ASSERT_NE(target, nullptr);
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
        ASSERT_TRUE(applyPhotometric(*window, *target));
        EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
        const std::optional<an::ImageDescriptor> descriptor = target->document().descriptor();
        ASSERT_TRUE(descriptor.has_value());
        EXPECT_EQ(descriptor->sampleType(), an::kSampleTypeOf<SampleT>);
        const std::vector<double> values = documentValues(target->document());
        const PlantedStar& star = field().stars[3];
        const std::array<double, 2u> measured = starRatios(field(), values, star);
        const std::array<double, 2u> model = modelRatios(star);
        EXPECT_NEAR(measured[0], model[0], tolerance * model[0]);
        EXPECT_NEAR(measured[1], model[1], tolerance * model[1]);
        window.reset();
        dropView(context(), target);
    };
    check.template operator()<std::uint8_t>("UInt8", 0.08);
    check.template operator()<std::int8_t>("Int8", 0.08);
    check.template operator()<std::uint16_t>("UInt16", 0.03);
    check.template operator()<std::int16_t>("Int16", 0.03);
    check.template operator()<std::uint32_t>("UInt32", 0.03);
    check.template operator()<std::int32_t>("Int32", 0.03);
    check.template operator()<double>("Float64", 0.03);
}

// representation: the photometric calibration holds every result to full scale, a floating one included.
TEST_F(PhotometricWindowTest, EveryResultIsHeldToFullScale)
{
    StarField bright = field();
    const std::size_t plane = static_cast<std::size_t>(bright.width) * bright.height;
    const std::size_t at = static_cast<std::size_t>(2u) * bright.width + 2u;
    bright.samples[2u * plane + at] = 1.6f;
    ViewHandle target = fieldView<float>(session(), bright, fieldMetadata(bright));
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> values = documentValues(target->document());
    ASSERT_EQ(values.size(), 3u * plane);
    EXPECT_DOUBLE_EQ(values[2u * plane + at], 1.0);
}

// cancel: a run stopped as it is issued publishes nothing, leaves the image and its history as they were and shows no
// error.
TEST_F(PhotometricWindowTest, AStoppedRunPublishesNothing)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<PhotometricColorCalibrationProcess> window(new StoppingPhotometric(target.get()));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    EXPECT_EQ(documentValues(target->document()), source);
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Photometric Color Calibration")), 0);
}

// parameter-snapshot: a run reads the dialog as it was when Apply was pressed - a limit no star passes, set right
// after, does not reach it.
TEST_F(PhotometricWindowTest, ARunReadsTheDialogAsItWasWhenApplyWasPressed)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    window->applyCalibration();
    widgetOf<QDoubleSpinBox>(*window, "magnitudeLimitSpin")->setValue(6.0);
    ASSERT_TRUE(settlePhotometric(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<an::ImageMetadata> metadata = documentMetadata(target->document());
    ASSERT_TRUE(metadata.has_value());
    EXPECT_TRUE(metadata->semantics().processingState().colorCalibrated);
}

// apply: a sky region the person turned on but never drew - no width, no height - is refused by name rather than
// measured over the whole frame.
TEST_F(PhotometricWindowTest, AnEmptyBackgroundRegionIsRefusedByName)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    widgetOf<QGroupBox>(*window, "backgroundRoiGroupBox")->setChecked(true);
    widgetOf<QSpinBox>(*window, "backgroundRoiWidthValue")->setValue(0);
    widgetOf<QSpinBox>(*window, "backgroundRoiHeightValue")->setValue(0);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    ASSERT_EQ(context().errorCount(), 1);
    EXPECT_EQ(context().errorTexts.back(),
              QStringLiteral("The PCC background ROI must be fully inside the target image and have a non-zero size."));
    EXPECT_EQ(documentValues(target->document()), source);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
}

// cancel: closing the window while its run works stops the run; nothing is published and no error is shown.
TEST_F(PhotometricWindowTest, ClosingTheWindowStopsItsRun)
{
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    window->applyCalibration();
    window->close();
    ASSERT_TRUE(settlePhotometric(*window, *target));
    EXPECT_EQ(documentValues(target->document()), source);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    EXPECT_EQ(context().messages.count(QStringLiteral("info:Photometric Color Calibration")), 0);
}

// error: a catalog holding too few of the field's stars for the fit refuses the run, which says why, publishes nothing
// and leaves the history as it was.
TEST_F(PhotometricWindowTest, ARunTheCatalogCannotSupportSaysWhyAndChangesNothing)
{
    ASSERT_TRUE(writeGaiaCatalog(catalogs().gaiaDirectory(), field(), 4u));
    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()));
    ASSERT_NE(target, nullptr);
    const std::vector<double> source = documentValues(target->document());
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    EXPECT_EQ(documentValues(target->document()), source);
    EXPECT_FALSE(target->viewHistory()->hasUndo());
    ASSERT_EQ(context().errorCount(), 1);
    EXPECT_EQ(context().messages.back(), QStringLiteral("error:Error running photometric color calibration"));
}

// launch: what the window offers follows its target - an unsolved image is refused with the plate solve offered, a
// narrowband one and one marked nonlinear are refused by name, a grey one for its channels, and a missing catalog
// offers its settings page.
TEST_F(PhotometricWindowTest, WhatTheWindowOffersFollowsTheTarget)
{
    struct Case
    {
        const char* name;
        FieldMetadataOptions options;
        const char* banner;
        const char* action;
    };
    FieldMetadataOptions unsolved;
    unsolved.solved = false;
    FieldMetadataOptions narrowband;
    narrowband.filter = "Ha";
    FieldMetadataOptions nonlinear;
    nonlinear.state = an::ProcessingStateRecord{.linearity = an::RecordedLinearity::Nonlinear,
                                                .channelBasis = an::RecordedChannelBasis::LinearRgb};
    const std::vector<Case> cases{
        {"unsolved", unsolved, "not plate-solved", "Plate Solve..."},
        {"narrowband", narrowband, "narrowband", ""},
        {"nonlinear", nonlinear, "marked nonlinear", ""},
    };
    for (const Case& refused : cases)
    {
        SCOPED_TRACE(refused.name);
        ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field(), refused.options), refused.name);
        ASSERT_NE(target, nullptr);
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
        auto* banner = window->findChild<QWidget*>(QStringLiteral("warningBanner"));
        ASSERT_NE(banner, nullptr);
        EXPECT_FALSE(banner->isHidden());
        auto* action = widgetOf<QPushButton>(*window, "actionButton");
        ASSERT_NE(action, nullptr);
        EXPECT_EQ(!action->isHidden(), std::string(refused.action).size() > 0u);
        if (!action->isHidden())
        {
            EXPECT_EQ(action->text().toStdString(), refused.action);
        }
        EXPECT_FALSE(buttonOf(*window, QDialogButtonBox::Apply)->isEnabled());
        const int errors = context().errorCount();
        window->applyCalibration();
        EXPECT_EQ(context().errorCount(), errors + 1) << "an Apply asked for anyway is refused by name";
        window.reset();
        dropView(context(), target);
    }

    // A grey image carrying the field's solution is refused for its channels alone.
    {
        ViewHandle view = greyFieldView(session(), field(), fieldMetadata(field()), "grey");
        ASSERT_NE(view, nullptr);
        WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*view);
        auto* banner = window->findChild<QWidget*>(QStringLiteral("warningBanner"));
        ASSERT_NE(banner, nullptr);
        EXPECT_FALSE(banner->isHidden());
        EXPECT_TRUE(widgetOf<QPushButton>(*window, "actionButton")->isHidden()) << "no plate solve is offered";
        EXPECT_FALSE(buttonOf(*window, QDialogButtonBox::Apply)->isEnabled());
        const int errors = context().errorCount();
        window->applyCalibration();
        ASSERT_EQ(context().errorCount(), errors + 1);
        EXPECT_EQ(context().errorTexts.back(),
                  QStringLiteral("Photometric color calibration only supports RGB images."));
        window.reset();
        dropView(context(), view);
    }

    ViewHandle target = fieldView<float>(session(), field(), fieldMetadata(field()), "uncatalogued");
    ASSERT_NE(target, nullptr);
    (void)context().upsert(QString::fromLatin1(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY),
                           catalogs().gaiaDirectory() + QStringLiteral("-missing"));
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    auto* action = widgetOf<QPushButton>(*window, "actionButton");
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->isHidden());
    EXPECT_EQ(action->text(), QStringLiteral("Configure Gaia Catalog..."));
    EXPECT_FALSE(buttonOf(*window, QDialogButtonBox::Apply)->isEnabled());
    action->click();
    EXPECT_TRUE(context().messages.contains(QStringLiteral("settings:Plate Solving")));
}

// sequence: over a sequence the window calibrates the frame it shows - the one whose plate solution the window read -
// as an edit the history records; the recording is not rewritten and no new view is made.
TEST_F(PhotometricWindowTest, OverASequenceTheFrameShownIsCalibrated)
{
    QTemporaryDir recordings;
    ASSERT_TRUE(recordings.isValid());
    const QString path = recordings.path() + QStringLiteral("/frames.ser");
    writeFieldRecording(session(), path, field(), 2u);
    ViewHandle target = fieldSequenceView(session(), path, field(), fieldMetadata(field()), 2u);
    ASSERT_NE(target, nullptr);
    WindowHandle<PhotometricColorCalibrationProcess> window = openPhotometric(*target);
    ASSERT_TRUE(applyPhotometric(*window, *target));
    EXPECT_EQ(context().errorCount(), 0) << context().errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::vector<double> values = documentValues(target->document());
    const PlantedStar& star = field().stars[10];
    const std::array<double, 2u> measured = starRatios(field(), values, star);
    const std::array<double, 2u> model = modelRatios(star);
    EXPECT_NEAR(measured[0], model[0], 0.03 * model[0]);
    EXPECT_NEAR(measured[1], model[1], 0.03 * model[1]);
    ASSERT_NE(target->viewHistory(), nullptr);
    EXPECT_TRUE(target->viewHistory()->hasUndo());
    const std::optional<an::ui::DocumentSequence> sequence = target->document().sequence();
    ASSERT_TRUE(sequence.has_value());
    EXPECT_EQ(sequence->sourcePath, path);
    EXPECT_TRUE(context().madeViews.empty());
    window.reset();
    dropView(context(), target);
}
