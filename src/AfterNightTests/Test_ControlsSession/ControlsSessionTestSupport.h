// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the session bridge's cases share: a session lent through the provider interface, images built through the
/// engine as assets of it, documents around them, and bounded waits that keep the GUI thread's event loop turning, so a
/// case whose subject never answers fails instead of hanging the suite.
#pragma once

#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <QCoreApplication>
#include <QEventLoop>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace an::tests::controls
{

/// How long a case waits for something that should happen at once before calling it a failure.
inline constexpr std::chrono::seconds kPatience{10};

/// How long a case waits to be confident that something that should not happen has not.
inline constexpr std::chrono::milliseconds kQuietPeriod{100};

/// The provider an application context would be: it owns the session it lends, as the application does.
class TestSessionProvider final : public an::ui::IEngineSessionProvider
{
public:
    explicit TestSessionProvider(an::consumer::EngineSession session) : m_session(std::move(session)) {}

    [[nodiscard]] const an::consumer::EngineSession& engineSession() const noexcept override
    {
        return m_session;
    }

    [[nodiscard]] an::consumer::EngineSession& owned() noexcept
    {
        return m_session;
    }

private:
    an::consumer::EngineSession m_session;
};

/// A provider over a session of a given width; fails the case when the session cannot be opened.
[[nodiscard]] inline std::optional<TestSessionProvider> openProvider(std::uint32_t width)
{
    an::consumer::EngineSessionSettings settings;
    settings.maxConcurrency = width;
    an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
    EXPECT_TRUE(session.hasValue()) << session.status().message();
    if (!session.hasValue())
    {
        return std::nullopt;
    }
    return std::optional<TestSessionProvider>(std::in_place, std::move(*session));
}

/// The description of a single-channel Float32 image.
[[nodiscard]] inline std::optional<an::ImageDescriptor> monoFloatDescriptor(const an::EngineContext& engine,
                                                                            std::uint32_t width, std::uint32_t height)
{
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, engine.limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    return an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                                  .sampleType = an::SampleType::Float32,
                                                                  .channelSchema = an::ChannelSchema::Mono,
                                                                  .colorSpace = an::ColorSpace::Unspecified,
                                                                  .numericDomain = an::NumericDomain::NormalizedLinear,
                                                                  .cfa = an::CfaDescriptor{},
                                                                  .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                                  .pixelAspectRatio = 1.0});
}

/// A new asset of a session holding a single-channel Float32 image filled with one value; fails the case when it
/// cannot be built.
[[nodiscard]] inline std::optional<an::consumer::ImageAsset> filledAsset(const an::consumer::EngineSession& session,
                                                                         float value, std::uint32_t width = 8u,
                                                                         std::uint32_t height = 4u)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue()) << hold.status().message();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor = monoFloatDescriptor(hold->engine(), width, height);
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, value);
    EXPECT_TRUE(image.hasValue()) << image.status().message();
    if (!image.hasValue())
    {
        return std::nullopt;
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

/// A document around a new filled asset; fails the case when it cannot be built.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> filledDocument(const an::consumer::EngineSession& session,
                                                                         float value, an::ui::DocumentNames names = {},
                                                                         std::uint32_t width = 8u,
                                                                         std::uint32_t height = 4u)
{
    std::optional<an::consumer::ImageAsset> asset = filledAsset(session, value, width, height);
    if (!asset.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset), std::move(names));
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// The sample at a position of a single-channel Float32 image, or NaN.
[[nodiscard]] inline float sampleAt(const an::consumer::AnyImage& image, std::uint32_t x, std::uint32_t y)
{
    const an::Image<float>* typed = std::get_if<an::Image<float>>(&image);
    if (typed == nullptr || !typed->view().has_value())
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto row = typed->view()->row(0u, y);
    return row.has_value() && x < row->size() ? (*row)[x] : std::numeric_limits<float>::quiet_NaN();
}

/// The sample at a position of what a document holds now, read under a lease, or NaN.
[[nodiscard]] inline float documentSampleAt(const an::ui::ImageDocument& document, std::uint32_t x, std::uint32_t y)
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    return lease.hasValue() ? sampleAt(lease->image(), x, y) : std::numeric_limits<float>::quiet_NaN();
}

/// Writes one value at a position of a single-channel Float32 asset through its write lease.
inline void writeSample(an::consumer::AssetWriteLease& lease, std::uint32_t x, std::uint32_t y, float value)
{
    std::optional<an::ImageWriteAccess<float>> access = lease.write<float>();
    ASSERT_TRUE(access.has_value());
    const auto row = access->view().row(0u, y);
    ASSERT_TRUE(row.has_value());
    (*row)[x] = value;
}

/// A transition that states the samples are linear, which changes what the image means.
[[nodiscard]] inline an::MetadataTransition linearTransition()
{
    an::MetadataTransition transition;
    an::Result<an::ImageSemantics> semantics =
        an::ImageSemantics::create(an::ImageSemanticsRequest{.transferFunction = an::TransferFunction::Linear});
    EXPECT_TRUE(semantics.hasValue()) << semantics.status().message();
    if (semantics.hasValue())
    {
        transition.semantics = *semantics;
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create("state the transfer function", "");
    EXPECT_TRUE(entry.hasValue());
    if (entry.hasValue())
    {
        transition.record = *entry;
    }
    return transition;
}

/// A mask covering an image of the given size, every pixel valid.
[[nodiscard]] inline an::Result<an::ValidityMask> allValidMask(const an::consumer::EngineSession& session,
                                                               std::uint32_t width, std::uint32_t height)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return hold.status();
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the mask cannot be described");
    }
    return an::ValidityMask::createAllValid(hold->engine(), *extent);
}

/// Turns the GUI thread's event loop until a condition holds or the patience runs out.
[[nodiscard]] inline bool pumpUntil(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    return condition();
}

/// Turns the GUI thread's event loop for the quiet period, delivering everything queued so far.
inline void pumpQuietly()
{
    const auto until = std::chrono::steady_clock::now() + kQuietPeriod;
    while (std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

/// Whether a future is still not ready after the quiet period.
template <typename ValueT> [[nodiscard]] bool stillWaiting(const std::future<ValueT>& future)
{
    return future.wait_for(kQuietPeriod) == std::future_status::timeout;
}

/// Whether a future becomes ready within the patience.
template <typename ValueT> [[nodiscard]] bool answers(const std::future<ValueT>& future)
{
    return future.wait_for(kPatience) == std::future_status::ready;
}

/// A planar asset of `SampleT`, each sample `value(x, y, channel)`, with a mask when one is given - whose bits
/// `valid(x, y, channel)` states - and the description a schema and a mosaic make.
template <typename SampleT>
[[nodiscard]] inline std::optional<an::consumer::ImageAsset>
assetOf(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, an::ChannelSchema schema,
        const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
        const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)>& valid = {},
        an::CfaDescriptor cfa = an::CfaDescriptor{})
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    EXPECT_TRUE(hold.hasValue());
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::uint32_t channels = schema == an::ChannelSchema::Mono ? 1u : 3u;
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, channels, hold->engine().limits());
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::kSampleTypeOf<SampleT>,
                                                               .channelSchema = schema,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = cfa,
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    EXPECT_TRUE(descriptor.has_value());
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::ImageMetadata metadata;
    if (valid)
    {
        const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(an::ImageSemanticsRequest{
            .validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})});
        EXPECT_TRUE(semantics.hasValue());
        metadata = an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{},
                                             an::Provenance{}, an::CodecMetadata{}, std::nullopt);
    }
    an::Result<an::Image<SampleT>> image = an::Image<SampleT>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed, metadata);
    EXPECT_TRUE(image.hasValue());
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
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
    if (valid)
    {
        an::Result<an::ValidityMask> made = an::ValidityMask::create(hold->engine(), *extent);
        EXPECT_TRUE(made.hasValue());
        std::optional<an::ValidityMaskView<std::uint8_t>> bits = made->view();
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    EXPECT_TRUE(bits->setValid(channel, x, y, valid(x, y, channel)));
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

/// The reference: the mean of a block's valid samples in Float64, returned through the engine's code-value conversion.
template <typename SampleT>
[[nodiscard]] inline std::optional<SampleT>
blockMean(const std::function<SampleT(std::uint32_t, std::uint32_t, std::uint32_t)>& value,
          const std::function<bool(std::uint32_t, std::uint32_t, std::uint32_t)>& valid, std::uint32_t factor,
          std::uint32_t channel, std::uint32_t blockX, std::uint32_t blockY)
{
    double sum = 0.0;
    std::uint32_t counted = 0u;
    for (std::uint32_t y = blockY * factor; y < (blockY + 1u) * factor; ++y)
    {
        for (std::uint32_t x = blockX * factor; x < (blockX + 1u) * factor; ++x)
        {
            if (!valid || valid(x, y, channel))
            {
                sum += static_cast<double>(value(x, y, channel));
                ++counted;
            }
        }
    }
    if (counted == 0u)
    {
        return std::nullopt;
    }
    return an::convertSample<double, SampleT>(sum / counted,
                                              an::ConversionPolicy{.domain = an::DomainMapping::CodeValue})
        .value;
}

} // namespace an::tests::controls
