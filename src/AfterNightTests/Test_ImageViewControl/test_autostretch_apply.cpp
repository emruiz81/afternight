// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Apply Stretch: the view's autostretch made the image's own - a stretched copy in the image's representation through
/// the presentation's production plan, recorded in the history before the view shows it, and never shown when the
/// history cannot keep it.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/AutostretchApply.h"
#include "controls/AutostretchSettingsControl.h"

#include <QPushButton>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
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

} // namespace
