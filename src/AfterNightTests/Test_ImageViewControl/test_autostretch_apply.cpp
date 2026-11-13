// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Apply Stretch: the view's autostretch made the image's own - a stretched copy in the image's representation through
/// the presentation's production plan, recorded in the history before the view shows it, and never shown when the
/// history cannot keep it. The stretched image keeps the frame's header and solution, and the window that applies it
/// is offered only while the view shows its image stretched.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/AutostretchApply.h"
#include "controls/AutostretchSettingsControl.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QAction>
#include <QPushButton>
#include <QToolBar>
#include <QVariant>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::viewer;
using an::ui::AutostretchApplySettings;

/// Every sample of a grey or colour Float32 or UInt16 asset, channel after channel.
[[nodiscard]] std::vector<double> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::vector<double> samples;
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return samples;
    }
    lease->visit([&](const auto& typed) {
        const auto view = typed.view();
        if (!view.has_value())
        {
            return;
        }
        for (std::uint32_t channel = 0u; channel < view->channelCount(); ++channel)
        {
            for (std::uint32_t y = 0u; y < view->height(); ++y)
            {
                for (std::uint32_t x = 0u; x < view->width(); ++x)
                {
                    samples.push_back(static_cast<double>(*view->sampleAt(channel, x, y, 0u)));
                }
            }
        }
    });
    return samples;
}

/// The stretched copy of an asset under its own read lease.
[[nodiscard]] an::Result<an::consumer::ImageAsset> stretchedCopyOf(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const AutostretchApplySettings& settings)
{
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    return an::ui::applyAutostretchToCopy(session, lease->image(), lease->validity(), settings, {});
}

/// A dark grey gradient, as a linear frame is before it is stretched.
[[nodiscard]] std::optional<an::consumer::ImageAsset> darkFrame(const an::consumer::EngineSession& session)
{
    return makeAsset<float>(session, 16u, 16u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return 0.01f + 0.0005f * static_cast<float>(x + 16u * y);
    });
}

/// The header of a solved frame of 16 by 16 pixels: what it shows, its optics, its integration and a tangent-plane
/// solution about its centre.
[[nodiscard]] std::vector<an::CodecCard> solvedFrameCards()
{
    const std::vector<std::pair<std::string, std::string>> stated{
        {"OBJECT", "'NGC 3372'"}, {"TELESCOP", "'Refractor'"}, {"FOCALLEN", "500.0"},    {"XPIXSZ", "3.76"},
        {"EXPTIME", "300.0"},     {"RADESYS", "'ICRS'"},       {"CTYPE1", "'RA---TAN'"}, {"CTYPE2", "'DEC--TAN'"},
        {"CUNIT1", "'deg'"},      {"CUNIT2", "'deg'"},         {"CRPIX1", "8.5"},        {"CRPIX2", "8.5"},
        {"CRVAL1", "161.265"},    {"CRVAL2", "-59.866"},       {"CD1_1", "-0.0005"},     {"CD1_2", "0.0"},
        {"CD2_1", "0.0"},         {"CD2_2", "0.0005"}};
    std::vector<an::CodecCard> cards;
    for (const auto& [keyword, value] : stated)
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, keyword, value, {});
        EXPECT_TRUE(card.hasValue()) << card.status().message();
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }
    return cards;
}

/// A grey solved frame of 16 by 16 pixels of the representation `SampleT`, its samples `value(x, y)`; fails the case
/// when it cannot be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
solvedFrame(const an::consumer::EngineSession& session,
            const std::function<SampleT(std::uint32_t, std::uint32_t)>& value)
{
    constexpr std::uint32_t kSide = 16u;
    an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(an::SourceEncoding{}, solvedFrameCards());
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!codec.hasValue() || !hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        describeImage(hold->engine(), an::kSampleTypeOf<SampleT>, kSide, kSide, Channels::Grey);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{}, an::AstrometricSolution{},
                                  an::Provenance{}, std::move(*codec), std::nullopt));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        EXPECT_TRUE(access.has_value());
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t y = 0u; y < kSide; ++y)
        {
            const auto row = access->view().row(0u, y);
            EXPECT_TRUE(row.has_value());
            if (!row.has_value())
            {
                return std::nullopt;
            }
            for (std::uint32_t x = 0u; x < kSide; ++x)
            {
                (*row)[x] = value(x, y);
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A dark grey gradient as a solved Float32 frame.
[[nodiscard]] std::optional<an::consumer::ImageAsset> solvedDarkFrame(const an::consumer::EngineSession& session)
{
    return solvedFrame<float>(
        session, [](std::uint32_t x, std::uint32_t y) { return 0.01f + 0.0005f * static_cast<float>(x + 16u * y); });
}

/// Where the solved frame's own solution says a pixel points.
[[nodiscard]] std::optional<an::SkyCoordinate> whereTheFramePoints(const an::PixelCoordinate pixel)
{
    const std::vector<an::CodecCard> cards = solvedFrameCards();
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, 16u, 16u);
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    if (!solution.hasValue())
    {
        return std::nullopt;
    }
    const an::Result<an::SkyCoordinate> direction = solution->pixelToWorld(pixel);
    EXPECT_TRUE(direction.hasValue()) << direction.status().message();
    if (!direction.hasValue())
    {
        return std::nullopt;
    }
    return *direction;
}

/// Checks that a solution points a pixel of the frame where the solved frame's own does.
void expectPointingAsTheFrame(const an::WcsSolution& solution)
{
    for (const an::PixelCoordinate pixel :
         {an::PixelCoordinate{.x = 3.0, .y = 5.0}, an::PixelCoordinate{.x = 12.5, .y = 9.0}})
    {
        const std::optional<an::SkyCoordinate> expected = whereTheFramePoints(pixel);
        ASSERT_TRUE(expected.has_value());
        const an::Result<an::SkyCoordinate> direction = solution.pixelToWorld(pixel);
        ASSERT_TRUE(direction.hasValue()) << direction.status().message();
        EXPECT_NEAR(direction->rightAscensionDegrees, expected->rightAscensionDegrees, 1e-9);
        EXPECT_NEAR(direction->declinationDegrees, expected->declinationDegrees, 1e-9);
    }
}

/// The value a header states for a keyword, or nothing.
[[nodiscard]] std::optional<std::string> headerValue(const an::CodecMetadata& codec, const std::string_view keyword)
{
    const an::CodecCard* const card = codec.find(an::CodecNamespace::Fits, keyword);
    if (card == nullptr)
    {
        return std::nullopt;
    }
    return std::string(card->value());
}

/// Checks that a stretched copy keeps every card of the solved frame, states a solution pointing where the frame's
/// does, and records its samples as nonlinear.
void expectTheFramesHeaderKept(const an::consumer::ImageAsset& stretched)
{
    an::Result<an::consumer::AssetReadLease> lease = stretched.read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    an::ImageMetadata metadata;
    lease->visit([&metadata](const auto& typed) { metadata = typed.metadata(); });
    const an::CodecMetadata& codec = metadata.codec();
    for (const an::CodecCard& card : solvedFrameCards())
    {
        EXPECT_EQ(headerValue(codec, card.keyword()), std::optional<std::string>(std::string(card.value())))
            << card.keyword() << " is a fact of the frame, which a stretch leaves true";
    }
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(codec.cards(), 16u, 16u);
    ASSERT_TRUE(solution.hasValue()) << solution.status().message();
    expectPointingAsTheFrame(*solution);
    EXPECT_EQ(an::readProcessingStateCards(codec).linearity, an::RecordedLinearity::Nonlinear);
    EXPECT_FALSE(an::ui::isLinearData(metadata)) << "a stretched image is shown as it is";
}

/// The toolbar button of a view that opens its Autostretch Settings window.
[[nodiscard]] QAction* autostretchSettingsButton(ImageViewControl& view)
{
    for (QToolBar* toolbar : view.findChildren<QToolBar*>())
    {
        for (QAction* action : toolbar->actions())
        {
            if (action->text() == QStringLiteral("Autostretch Settings"))
            {
                return action;
            }
        }
    }
    return nullptr;
}

/// Whether a toolbar button is offered: the state the toolbar recorded for it while it is folded into the overflow menu
/// - a hidden action reads as disabled - else its own.
[[nodiscard]] bool isOffered(const QAction& button)
{
    if (button.property("anOverflowHidden").toBool())
    {
        const QVariant recorded = button.property("anOverflowWasEnabled");
        if (recorded.isValid())
        {
            return recorded.toBool();
        }
    }
    return button.isEnabled();
}

/// The Autostretch Settings window open over a view, or null.
[[nodiscard]] AutostretchSettingsControl* openSettingsWindow(ImageViewControl& view)
{
    for (QDialog* dialog : view.findChildren<QDialog*>())
    {
        auto* settings = dynamic_cast<AutostretchSettingsControl*>(dialog);
        if (settings != nullptr && settings->isVisible())
        {
            return settings;
        }
    }
    return nullptr;
}

class AutostretchApply : public ::testing::Test
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

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A view of a kind showing a dark frame.
    [[nodiscard]] ViewHandle viewOfDarkFrame(ViewControlMode mode = ViewControlMode::standard)
    {
        ViewHandle view = makeView("stretched", mode);
        std::optional<an::consumer::ImageAsset> frame = darkFrame(session());
        EXPECT_TRUE(frame.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*frame));
        EXPECT_TRUE(document.hasValue());
        view->setDocument(std::move(*document));
        if (mode == ViewControlMode::standard)
        {
            // As the application's factory does for every view it makes.
            view->initializeNewImageHistory("Image read from a file");
        }
        EXPECT_TRUE(settle(*view));
        return view;
    }

    /// Applies the stretch as the settings window does, and waits for the view.
    [[nodiscard]] bool apply(ImageViewControl& view, const AutostretchApplySettings& settings,
                             std::optional<bool>& shown)
    {
        view.editImage(
            "Autostretch", "Image modified from autostretch control",
            [settings](const an::consumer::EngineSession& session, const an::consumer::AssetWriteLease& image,
                       std::stop_token stop) {
                return an::ui::applyAutostretchToCopy(session, image.image(), image.validity(), settings,
                                                      std::move(stop));
            },
            [&shown](bool done) { shown = done; });
        return settle(view);
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

// The stretch is computed on a copy, never on the shown image, which other threads read.
TEST_F(AutostretchApply, StretchesACopyAndLeavesTheSourceUnchanged)
{
    std::optional<an::consumer::ImageAsset> source = darkFrame(session());
    ASSERT_TRUE(source.has_value());
    const std::vector<double> before = samplesOf(*source);

    an::Result<an::consumer::ImageAsset> stretched = stretchedCopyOf(session(), *source, AutostretchApplySettings{});
    ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();
    EXPECT_NE(stretched->identity(), source->identity());
    EXPECT_EQ(samplesOf(*source), before);

    std::vector<double> after = samplesOf(*stretched);
    ASSERT_EQ(after.size(), before.size());
    std::nth_element(after.begin(), after.begin() + after.size() / 2u, after.end());
    EXPECT_NEAR(after[after.size() / 2u], AutostretchApplySettings{}.targetBackground, 0.02)
        << "the median lands at the target background, as the view shows it";
}

TEST_F(AutostretchApply, ALinkedApplyKeepsTheImagesRepresentationAndAsksThePresentationsBalance)
{
    std::optional<an::consumer::ImageAsset> source = makeAsset<std::uint16_t>(
        session(), 8u, 8u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(500u + 40u * x + 30u * y + 200u * c);
        });
    ASSERT_TRUE(source.has_value());
    AutostretchApplySettings settings;
    settings.linkedChannels = true;
    settings.neutralizeBackground = true;
    settings.colorCalibration = true;
    settings.saturation = 0.5;
    settings.brightness = 51;
    settings.contrast = 20;

    const an::DisplayPresentationRequest request = an::ui::autostretchApplyRequest(settings);
    EXPECT_TRUE(request.linked);
    EXPECT_TRUE(request.neutralize);
    EXPECT_TRUE(request.calibrate);
    EXPECT_EQ(request.output, an::PresentationOutput::WorkingFloat32);
    EXPECT_EQ(request.promotion, an::PresentationPromotion::Gradient) << "an applied stretch is never quantized";
    EXPECT_DOUBLE_EQ(request.brightness, 51.0 / 255.0);

    an::Result<an::consumer::ImageAsset> stretched = stretchedCopyOf(session(), *source, settings);
    ASSERT_TRUE(stretched.hasValue()) << stretched.status().message();
    ASSERT_TRUE(stretched->descriptor().has_value());
    EXPECT_EQ(stretched->descriptor()->sampleType(), an::SampleType::UInt16);
    EXPECT_EQ(stretched->descriptor()->extent().channelCount(), 3u);

    AutostretchApplySettings unlinked = settings;
    unlinked.linkedChannels = false;
    const an::DisplayPresentationRequest unbalanced = an::ui::autostretchApplyRequest(unlinked);
    EXPECT_FALSE(unbalanced.neutralize) << "an unlinked stretch would undo a balance, so none is asked";
    EXPECT_FALSE(unbalanced.calibrate);
}

TEST_F(AutostretchApply, AMonochromeImageIsStretchedTheSameLinkedOrNot)
{
    std::optional<an::consumer::ImageAsset> source = darkFrame(session());
    ASSERT_TRUE(source.has_value());
    AutostretchApplySettings linked;
    linked.linkedChannels = true;
    an::Result<an::consumer::ImageAsset> withLink = stretchedCopyOf(session(), *source, linked);
    an::Result<an::consumer::ImageAsset> withoutLink = stretchedCopyOf(session(), *source, AutostretchApplySettings{});
    ASSERT_TRUE(withLink.hasValue() && withoutLink.hasValue());
    EXPECT_EQ(samplesOf(*withLink), samplesOf(*withoutLink));
}

// The history records the stretched frame before the view is handed it.
TEST_F(AutostretchApply, TheEditIsRecordedBeforeTheViewIsHandedIt)
{
    ViewHandle view = viewOfDarkFrame();
    const std::shared_ptr<const an::consumer::ImageAsset> shown = view->document().asset();
    const std::vector<double> before = samplesOf(*shown);
    StepShownWith steps(view.get());

    std::optional<bool> applied;
    ASSERT_TRUE(apply(*view, AutostretchApplySettings{}, applied));
    ASSERT_EQ(applied, std::optional<bool>(true));

    ASSERT_FALSE(steps.steps.empty());
    EXPECT_EQ(steps.steps.front(), "Autostretch")
        << "the view was handed the stretched frame before its history held it";
    EXPECT_NE(view->document().asset()->identity(), shown->identity());
    EXPECT_EQ(samplesOf(*shown), before) << "the image the view showed before is never written";
    const std::vector<double> stretched = samplesOf(*view->document().asset());

    // What the history recorded is what the view shows: undo brings back the dark frame and redo the stretch.
    ASSERT_TRUE(undoTo(*view, std::string(), "Autostretch"));
    EXPECT_EQ(samplesOf(*view->document().asset()), before);
    ASSERT_TRUE(view->viewHistory()->redo());
    ASSERT_TRUE(pumpUntil([&view]() { return view->viewHistory()->previousStepName() == "Autostretch"; }));
    EXPECT_EQ(samplesOf(*view->document().asset()), stretched);
}

TEST_F(AutostretchApply, TheStepIsNamedAutostretch)
{
    ViewHandle view = viewOfDarkFrame();
    view->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    m_context->answerYes = true;

    view->autostretchSettings();
    AutostretchSettingsControl* settings = nullptr;
    for (QDialog* dialog : view->findChildren<QDialog*>())
    {
        settings = settings != nullptr ? settings : dynamic_cast<AutostretchSettingsControl*>(dialog);
    }
    ASSERT_NE(settings, nullptr);
    auto* applyButton = settings->findChild<QPushButton*>(QStringLiteral("applyStretchButton"));
    ASSERT_NE(applyButton, nullptr);
    applyButton->click();
    ASSERT_TRUE(settle(*view));

    ASSERT_NE(view->viewHistory(), nullptr);
    EXPECT_EQ(view->viewHistory()->previousStepName(), "Autostretch");
    EXPECT_EQ(view->getViewWidget()->getViewMode(), ImageViewMode::LINEAR_MODE)
        << "a stretched image is shown as it is, not stretched again";
}

// A session whose pending-snapshot allowance the stretched frame outgrows refuses the copy the history keeps of it.
TEST_F(AutostretchApply, NothingIsShownWhenTheHistoryCannotKeepTheStretchedFrame)
{
    m_installed.reset();
    m_context = openViewerContext(2u, historyCannotKeepEdits());
    ASSERT_NE(m_context, nullptr);
    m_installed.emplace(*m_context);
    ViewHandle view = viewOfDarkFrame();
    const std::uint64_t shown = view->document().asset()->identity();

    std::optional<bool> applied;
    ASSERT_TRUE(apply(*view, AutostretchApplySettings{}, applied));
    EXPECT_EQ(applied, std::optional<bool>(false));
    EXPECT_EQ(view->document().asset()->identity(), shown);
    ASSERT_NE(view->viewHistory(), nullptr);
    EXPECT_FALSE(view->viewHistory()->hasUndo());
    ASSERT_TRUE(pumpUntil([this]() { return m_context->messages.contains(QStringLiteral("error:Image History")); }));
    ASSERT_FALSE(m_context->errorTexts.isEmpty());
    EXPECT_TRUE(m_context->errorTexts.back().contains(QStringLiteral("its snapshot cannot be kept")))
        << m_context->errorTexts.back().toStdString();
}

TEST_F(AutostretchApply, AViewWithoutHistoryIsHandedTheStretchedCopy)
{
    ViewHandle view = viewOfDarkFrame(ViewControlMode::st_preview);
    ASSERT_EQ(view->viewHistory(), nullptr);
    const std::uint64_t shown = view->document().asset()->identity();

    std::optional<bool> applied;
    ASSERT_TRUE(apply(*view, AutostretchApplySettings{}, applied));
    EXPECT_EQ(applied, std::optional<bool>(true));
    EXPECT_NE(view->document().asset()->identity(), shown);
}

// The stretched image is still the frame - its grid, its optics, its pointing and its solution - so it keeps the
// frame's header, through the Float32 working image and back in the frame's own representation alike.
TEST_F(AutostretchApply, TheStretchedCopyKeepsTheFramesHeaderAndSolutionAndRecordsItNonlinear)
{
    std::optional<an::consumer::ImageAsset> floating = solvedDarkFrame(session());
    ASSERT_TRUE(floating.has_value());
    an::Result<an::consumer::ImageAsset> stretchedFloating =
        stretchedCopyOf(session(), *floating, AutostretchApplySettings{});
    ASSERT_TRUE(stretchedFloating.hasValue()) << stretchedFloating.status().message();
    expectTheFramesHeaderKept(*stretchedFloating);

    std::optional<an::consumer::ImageAsset> integer =
        solvedFrame<std::uint16_t>(session(), [](std::uint32_t x, std::uint32_t y) {
            return static_cast<std::uint16_t>(600u + 20u * x + 9u * y);
        });
    ASSERT_TRUE(integer.has_value());
    an::Result<an::consumer::ImageAsset> stretchedInteger =
        stretchedCopyOf(session(), *integer, AutostretchApplySettings{});
    ASSERT_TRUE(stretchedInteger.hasValue()) << stretchedInteger.status().message();
    ASSERT_TRUE(stretchedInteger->descriptor().has_value());
    EXPECT_EQ(stretchedInteger->descriptor()->sampleType(), an::SampleType::UInt16);
    expectTheFramesHeaderKept(*stretchedInteger);
}

// A solved view stretched from its Autostretch Settings window is still solved.
TEST_F(AutostretchApply, AStretchedViewKeepsItsSolution)
{
    ViewHandle view = makeView("solved", ViewControlMode::standard);
    std::optional<an::consumer::ImageAsset> frame = solvedDarkFrame(session());
    ASSERT_TRUE(frame.has_value());
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*frame));
    ASSERT_TRUE(document.hasValue()) << document.status().message();
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(view->imageSolution().has_value()) << "the frame is solved before it is stretched";
    const std::uint64_t shown = view->document().asset()->identity();

    std::optional<bool> applied;
    ASSERT_TRUE(apply(*view, AutostretchApplySettings{}, applied));
    ASSERT_EQ(applied, std::optional<bool>(true));
    ASSERT_NE(view->document().asset()->identity(), shown);
    ASSERT_TRUE(pumpUntil([&view]() { return view->imageSolution().has_value(); }))
        << "the stretched image is the same frame, so the view still knows where it points";
    expectPointingAsTheFrame(*view->imageSolution());
}

// The window's settings only change what the view shows while it shows its image stretched; so it is offered only
// then, one at a time, and not again once Apply Stretch has shown the stretched image as it is.
TEST_F(AutostretchApply, TheSettingsWindowIsOfferedOnlyWhileTheViewShowsItsImageStretched)
{
    ViewHandle view = viewOfDarkFrame();
    QAction* button = autostretchSettingsButton(*view);
    ASSERT_NE(button, nullptr);

    view->setViewMode(ImageViewMode::LINEAR_MODE);
    ASSERT_TRUE(settle(*view));
    EXPECT_FALSE(isOffered(*button)) << "a linear display uses no autostretch setting";
    view->autostretchSettings();
    EXPECT_EQ(openSettingsWindow(*view), nullptr) << "the shortcut opens nothing either";

    view->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    EXPECT_TRUE(isOffered(*button));
    view->autostretchSettings();
    AutostretchSettingsControl* settings = openSettingsWindow(*view);
    ASSERT_NE(settings, nullptr);
    EXPECT_FALSE(isOffered(*button)) << "one window at a time";
    settings->reject();
    ASSERT_TRUE(settle(*view));
    EXPECT_TRUE(isOffered(*button)) << "closed over a stretched display, the window is offered again";

    m_context->answerYes = true;
    view->autostretchSettings();
    settings = openSettingsWindow(*view);
    ASSERT_NE(settings, nullptr);
    auto* applyButton = settings->findChild<QPushButton*>(QStringLiteral("applyStretchButton"));
    ASSERT_NE(applyButton, nullptr);
    applyButton->click();
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(pumpUntil([&view]() { return openSettingsWindow(*view) == nullptr; }));
    ASSERT_EQ(view->getViewWidget()->getViewMode(), ImageViewMode::LINEAR_MODE);
    EXPECT_FALSE(isOffered(*button)) << "the stretched image is shown linear, so its settings are not offered";
}

} // namespace
