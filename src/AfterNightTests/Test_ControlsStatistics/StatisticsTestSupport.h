// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the statistics suite's cases share: the viewer's application context with a view factory a mask window makes
/// its view through, views shown over documents of the viewer's images, the standard fixtures as assets of the session,
/// and detected stars a case states by their position and widths.
#pragma once

#include "Test_ControlsViewer/ControlsViewerTestSupport.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "fixtures/common/FixtureCatalogue.h"
// The neutral image description arrives through the adapter, the one place it becomes the engine's image.
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Stars/StarProfile.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace an::tests::statistics
{

using an::tests::viewer::Channels;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeAsset;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::settle;
using an::tests::viewer::ViewHandle;

/// The viewer's context, with a view factory whose views it keeps and deletes with itself.
class StatisticsTestContext final : public an::tests::viewer::ViewerTestContext, public an::ui::IViewFactory
{
public:
    explicit StatisticsTestContext(an::consumer::EngineSession session) : ViewerTestContext(std::move(session)) {}

    ~StatisticsTestContext() override
    {
        // The views it made go before the session the base lets go.
        m_made.clear();
    }

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromImage(ImageViewControl*,
                                                                       an::ui::ImageDocument&& document, QWidget*,
                                                                       const std::string& historyDesc,
                                                                       ViewControlMode mode,
                                                                       ImageViewControl* neighbour, bool, bool) override
    {
        auto view = makeView(document.names().displayName.toStdString(), mode);
        view->setDocument(std::move(document));
        madeHistories.push_back(historyDesc);
        madeNeighbours.push_back(neighbour);
        m_made.push_back(std::move(view));
        return m_made.back().get();
    }

    [[nodiscard]] ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl*, an::ui::ImageDocument&&,
                                                                          QWidget*, bool, const std::string&,
                                                                          ViewControlMode, ImageViewControl*, bool,
                                                                          bool) override
    {
        return nullptr;
    }

    /// The views the factory made, in order.
    [[nodiscard]] std::vector<ImageViewControl*> madeViews() const
    {
        std::vector<ImageViewControl*> views;
        for (const auto& view : m_made)
        {
            views.push_back(view.get());
        }
        return views;
    }

    /// The history descriptions the factory's views were made with.
    std::vector<std::string> madeHistories;

    /// The view each made view was asked to sit beside.
    std::vector<ImageViewControl*> madeNeighbours;

private:
    std::vector<ViewHandle> m_made;
};

/// A statistics context over a session of a given width; fails the case when the session cannot be opened.
[[nodiscard]] inline std::unique_ptr<StatisticsTestContext> openStatisticsContext(std::uint32_t width = 2u)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = an::tests::viewer::openFileProvider(width);
    if (!provider.has_value())
    {
        return nullptr;
    }
    return std::make_unique<StatisticsTestContext>(std::move(provider->owned()));
}

/// A standard fixture's samples and the asset the session holds them as; fails the case when either cannot be made.
struct FixtureAsset
{
    an::tests::fixtures::FixtureSamples samples;
    std::shared_ptr<const an::consumer::ImageAsset> asset;
};

/// A standard fixture as an asset of a session.
[[nodiscard]] inline std::optional<FixtureAsset> fixtureAsset(const an::consumer::EngineSession& session,
                                                              const an::tests::fixtures::FixtureImage& image)
{
    std::string error;
    std::optional<an::tests::fixtures::FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::engine::makeImageAsset(session, image, *samples);
    EXPECT_TRUE(asset.hasValue()) << image.name << ": " << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return FixtureAsset{std::move(*samples), std::make_shared<const an::consumer::ImageAsset>(std::move(*asset))};
}

/// A planar asset whose image states a validity rule, carrying a mask of `maskPlanes` planes - one shared by every
/// channel, or one per channel - when the rule names one, each bit `valid(plane, x, y)`; fails the case when it cannot
/// be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset>
assetWithValidity(const an::consumer::EngineSession& session, const std::uint32_t width, const std::uint32_t height,
                  const Channels channels,
                  const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
                  const an::ValidityDescriptor& validity, const std::uint32_t maskPlanes = 0u,
                  const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)>& valid = {})
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::tests::viewer::describeImage(hold->engine(), an::kSampleTypeOf<SampleT>, width, height, channels);
    const an::Result<an::ImageSemantics> semantics =
        an::ImageSemantics::create(an::ImageSemanticsRequest{.validity = validity});
    EXPECT_TRUE(descriptor.has_value() && semantics.hasValue());
    if (!descriptor.has_value() || !semantics.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
        an::ImageMetadata::create({}, *semantics, {}, {}, {}, std::nullopt));
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        for (std::uint32_t channel = 0u; channel < an::tests::viewer::channelCountOf(channels); ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                const auto row = access->view().row(channel, y);
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (*row)[x] = value(x, y, channel);
                }
            }
        }
    }
    std::optional<an::ValidityMask> mask;
    if (maskPlanes > 0u)
    {
        an::Result<an::ValidityMask> made = an::ValidityMask::createAllValid(
            hold->engine(), *an::ImageExtent::create(width, height, maskPlanes, hold->engine().limits()));
        EXPECT_TRUE(made.hasValue()) << made.status().message();
        if (!made.hasValue())
        {
            return std::nullopt;
        }
        const std::optional<an::ValidityMaskView<std::uint8_t>> bits = made->view();
        for (std::uint32_t plane = 0u; plane < maskPlanes; ++plane)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    (void)bits->setValid(plane, x, y, valid(plane, x, y));
                }
            }
        }
        mask.emplace(std::move(*made));
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)}, std::move(mask));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// A detected star at an engine pixel coordinate with stated full widths, amplitude and magnitude.
[[nodiscard]] inline an::DetectedStar starAt(const double x, const double y, const double fullWidthX = 2.0,
                                             const double fullWidthY = 2.0, const double amplitude = 0.5,
                                             const std::optional<double> magnitude = std::nullopt)
{
    an::DetectedStar star;
    star.positionX = x;
    star.positionY = y;
    star.psf.fullWidthX = fullWidthX;
    star.psf.fullWidthY = fullWidthY;
    star.psf.amplitude = amplitude;
    star.psf.angleDegrees = 0.0;
    star.metrics.meanFullWidth = (fullWidthX + fullWidthY) / 2.0;
    star.metrics.roundness = fullWidthX > 0.0 ? fullWidthY / fullWidthX : 0.0;
    star.metrics.circularity =
        amplitude / (0.1 + (fullWidthX > fullWidthY ? fullWidthX - fullWidthY : fullWidthY - fullWidthX));
    star.instrumentalMagnitude = magnitude;
    return star;
}

} // namespace an::tests::statistics
