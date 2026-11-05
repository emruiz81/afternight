// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view's reads and writes: the one demosaic setting every open honours, a file read into a new asset of the
/// session, a mosaic kept or demosaiced as asked - an X-Trans one by the asked method's tabulated counterpart - a
/// preview light calibrated by its masters, the masters read once, statistics in the image's units, a write that
/// publishes the whole file or none and refuses a mosaic its container cannot state, and a stop observed between steps.

#include "ControlsViewerTestSupport.h"

#include "controls/ImageViewFiles.h"

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace
{

using an::tests::viewer::Channels;
using an::tests::viewer::makeAsset;
using an::tests::viewer::openFileProvider;
using an::ui::ViewReadRequest;

/// A failure as one line: its code's name, its message and its root cause's message.
[[nodiscard]] std::string describe(const an::Status& status)
{
    std::string text =
        std::string(an::errorCodeName(status.code())) + ": " + status.message() + " [" + status.operation() + "]";
    for (an::Status cause = status; cause.failed(); cause = cause.cause())
    {
        text += " <- " + std::string(an::errorCodeName(cause.code())) + " " + cause.message() + " [" +
                cause.operation() + "]";
        if (const an::StatusDetail* detail = cause.detail(); detail != nullptr)
        {
            for (const an::StatusContextField& field : detail->context)
            {
                text += " {" + std::to_string(static_cast<int>(field.key)) + "=" + field.text +
                        std::to_string(field.number) + "}";
            }
        }
    }
    return text;
}

/// An intent that writes the image's own representation.
[[nodiscard]] an::OutputIntent intentFor(an::SampleType type)
{
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(an::OutputIntentRequest{.sampleType = type});
    EXPECT_TRUE(intent.hasValue()) << intent.status().message();
    return *intent;
}

/// Writes an asset to a FITS file; fails the case when it cannot.
void writeFits(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& asset, const QString& path,
               an::SampleType type)
{
    const an::Status written = an::ui::writeViewImage(session, asset, path, intentFor(type), {});
    ASSERT_TRUE(written.ok()) << written.message();
}

/// The Float32 sample at a position and channel of an asset, or NaN.
[[nodiscard]] double floatAt(const an::consumer::ImageAsset& asset, std::uint32_t x, std::uint32_t y,
                             std::uint32_t channel = 0u)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return std::nan("");
    }
    const auto* image = std::get_if<an::Image<float>>(&lease->image());
    if (image == nullptr || !image->view().has_value())
    {
        return std::nan("");
    }
    const float* sample = image->view()->sampleAt(channel, x, y, 0u);
    return sample != nullptr ? static_cast<double>(*sample) : std::nan("");
}

/// Every sample of a sixteen-bit image, plane by plane and row by row.
[[nodiscard]] std::vector<std::uint16_t> uint16Samples(const an::Image<std::uint16_t>& image)
{
    std::vector<std::uint16_t> samples;
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    const auto view = image.view();
    if (!described.has_value() || !view.has_value())
    {
        return samples;
    }
    for (std::uint32_t channel = 0u; channel < described->extent().channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < described->extent().height(); ++y)
        {
            const auto row = view->row(channel, y);
            if (row.has_value())
            {
                samples.insert(samples.end(), row->begin(), row->end());
            }
        }
    }
    return samples;
}

/// Every sample of a sixteen-bit asset, or none when it is not sixteen-bit.
[[nodiscard]] std::vector<std::uint16_t> uint16Samples(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return {};
    }
    const auto* image = std::get_if<an::Image<std::uint16_t>>(&lease->image());
    return image != nullptr ? uint16Samples(*image) : std::vector<std::uint16_t>{};
}

// A view's work that throws on its worker ends as the failure its exception means - an allocation refused for
// `std::bad_alloc`, an external failure with the exception's message for anything else - so its completion lands as a
// failed read, its spinner stopped and its listeners told, instead of being dropped with a wait never ended.
TEST(ViewWorkOutcomeTest, AnExceptionBecomesTheFailureTheWorkWouldHaveReturned)
{
    const an::Result<int> returned = an::ui::viewWorkOutcome([]() -> an::Result<int> { return 7; });
    ASSERT_TRUE(returned.hasValue());
    EXPECT_EQ(*returned, 7);

    const an::Result<int> refused = an::ui::viewWorkOutcome([]() -> an::Result<int> { throw std::bad_alloc(); });
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::AllocationFailed);

    const an::Status failed =
        an::ui::viewWorkOutcome([]() -> an::Status { throw std::runtime_error("the codec gave up"); });
    EXPECT_EQ(failed.code(), an::ErrorCode::ExternalFailure);
    EXPECT_NE(std::string(failed.message()).find("the codec gave up"), std::string::npos);

    const an::Status unknown = an::ui::viewWorkOutcome([]() -> an::Status { throw 42; });
    EXPECT_EQ(unknown.code(), an::ErrorCode::ExternalFailure);
}

} // namespace

// Every open - bulk, standard and a new sequence - reads the one demosaic setting; a name the product does not know
// reads as RCD, the setting's default.
TEST(ImageViewFilesTest, TheDemosaicSettingNamesOneMethodAndAnUnknownOneReadsAsRcd)
{
    EXPECT_EQ(an::ui::demosaicMethodForSetting("rcd").method, an::DemosaicMethod::Rcd);
    EXPECT_EQ(an::ui::demosaicMethodForSetting("AMaZE").method, an::DemosaicMethod::Amaze);
    EXPECT_EQ(an::ui::demosaicMethodForSetting(" vng4 ").method, an::DemosaicMethod::Vng4);
    EXPECT_EQ(an::ui::demosaicMethodForSetting("lmmse").method, an::DemosaicMethod::Lmmse);
    EXPECT_EQ(an::ui::demosaicMethodForSetting("bilinear").method, an::DemosaicMethod::Fast);
    EXPECT_EQ(an::ui::demosaicMethodForSetting("ahd").method, an::DemosaicMethod::LuminanceAhd);
    for (const char* unknown : {"dcb", "hphd", "igv", "unknown"})
    {
        EXPECT_EQ(an::ui::demosaicMethodForSetting(unknown).method, an::DemosaicMethod::Rcd) << unknown;
    }
}

TEST(ImageViewFilesTest, AWrittenFileIsReadBackIntoANewAssetOfTheSession)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("frame.fits"));
    std::optional<an::consumer::ImageAsset> written =
        makeAsset<float>(session, 6u, 4u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.1f * static_cast<float>(x) + 0.01f * y;
        });
    ASSERT_TRUE(written.has_value());
    writeFits(session, *written, path, an::SampleType::Float32);
    ASSERT_TRUE(QFileInfo(path).isFile());

    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, ViewReadRequest{.path = path}, {}, nullptr);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    EXPECT_TRUE(read->asset.valid());
    EXPECT_NE(read->asset.identity(), written->identity()) << "a read makes a new asset";
    EXPECT_FALSE(read->mosaic);
    EXPECT_FALSE(read->calibrated);
    EXPECT_NEAR(floatAt(read->asset, 3u, 2u), 0.32, 1e-6);
}

TEST(ImageViewFilesTest, AWriteToAContainerNothingNamesIsRefusedAndLeavesNoFile)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    std::optional<an::consumer::ImageAsset> image = makeAsset<std::uint16_t>(
        session, 2u, 2u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint16_t{7}; });
    ASSERT_TRUE(image.has_value());
    const QString path = directory.filePath(QStringLiteral("frame.unknownextension"));
    const an::Status refused = an::ui::writeViewImage(session, *image, path, intentFor(an::SampleType::UInt16), {});
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::Unsupported);
    EXPECT_TRUE(QDir(directory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
        << "a refused write leaves nothing behind";
}

TEST(ImageViewFilesTest, AMosaicIsKeptAsRecordedOrDemosaicedByTheMethodAsked)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mosaic.fits"));
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    std::optional<an::consumer::ImageAsset> mosaic = makeAsset<std::uint16_t>(
        session, 8u, 8u, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>((x % 2u == 0u && y % 2u == 0u) ? 40000u : 10000u);
        },
        an::NumericDomain::NormalizedLinear, *rggb);
    ASSERT_TRUE(mosaic.has_value());
    writeFits(session, *mosaic, path, an::SampleType::UInt16);

    an::Result<an::ui::ViewReadResult> kept =
        an::ui::readViewImage(session, ViewReadRequest{.path = path}, {}, nullptr);
    ASSERT_TRUE(kept.hasValue()) << kept.status().message();
    EXPECT_TRUE(kept->mosaic) << "a mosaic opened with no method is the mosaic the sensor recorded";
    EXPECT_FALSE(kept->demosaiced.has_value());
    ASSERT_TRUE(kept->asset.descriptor().has_value());
    EXPECT_TRUE(kept->asset.descriptor()->isMosaiced());

    an::Result<an::ui::ViewReadResult> colour = an::ui::readViewImage(
        session, ViewReadRequest{.path = path, .demosaic = an::DemosaicMethod::Bilinear}, {}, nullptr);
    ASSERT_TRUE(colour.hasValue()) << colour.status().message();
    EXPECT_FALSE(colour->mosaic);
    ASSERT_TRUE(colour->demosaiced.has_value());
    EXPECT_EQ(*colour->demosaiced, an::DemosaicMethod::Bilinear) << "the method asked, not one the read chose";
    ASSERT_TRUE(colour->asset.descriptor().has_value());
    EXPECT_FALSE(colour->asset.descriptor()->isMosaiced());
    EXPECT_EQ(colour->asset.descriptor()->extent().channelCount(), 3u);

    an::Result<an::consumer::ImageAsset> again =
        an::ui::demosaicViewImage(session, kept->asset, an::DemosaicMethod::Rcd, {});
    ASSERT_TRUE(again.hasValue()) << describe(again.status());
    EXPECT_EQ(again->descriptor()->extent().channelCount(), 3u);
    const an::Result<an::consumer::ImageAsset> notMosaic =
        an::ui::demosaicViewImage(session, *again, an::DemosaicMethod::Rcd, {});
    ASSERT_FALSE(notMosaic.hasValue());
    EXPECT_EQ(notMosaic.status().code(), an::ErrorCode::IncompatibleArguments);
}

// Every method the product offers is written for a two-by-two Bayer block, and every read and demosaic of the view
// asked the engine for it over an X-Trans mosaic too, which the engine refuses, so no X-Trans image was ever shown in
// colour. Each entry point demosaics one by the asked method's tabulated counterpart: the two fast estimates by the
// tabulated fast one, every other method by the three-pass Markesteijn estimate - the engine's own answer, sample for
// sample.
TEST(ImageViewFilesTest, AnXTransMosaicIsDemosaicedByTheTabulatedCounterpartOfTheMethodAsked)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("xtrans.tif"));
    ASSERT_TRUE(an::tests::viewer::writeXTransTiff(session, path));

    an::Result<an::ui::ViewReadResult> kept =
        an::ui::readViewImage(session, ViewReadRequest{.path = path}, {}, nullptr);
    ASSERT_TRUE(kept.hasValue()) << describe(kept.status());
    ASSERT_TRUE(kept->mosaic);
    ASSERT_TRUE(kept->asset.descriptor().has_value());
    ASSERT_TRUE(kept->asset.descriptor()->cfa().hasFilterTable()) << "the TIFF keeps the six-by-six table";

    std::vector<std::uint16_t> byFast;
    for (const an::DemosaicMethod asked : {an::DemosaicMethod::Fast, an::DemosaicMethod::Bilinear})
    {
        SCOPED_TRACE(an::demosaicMethodName(asked));
        an::Result<an::ui::ViewReadResult> colour =
            an::ui::readViewImage(session, ViewReadRequest{.path = path, .demosaic = asked}, {}, nullptr);
        ASSERT_TRUE(colour.hasValue()) << describe(colour.status());
        EXPECT_FALSE(colour->mosaic);
        ASSERT_TRUE(colour->demosaiced.has_value());
        EXPECT_EQ(*colour->demosaiced, an::DemosaicMethod::XTransFast) << "the read names the method that ran";
        ASSERT_TRUE(colour->asset.descriptor().has_value());
        EXPECT_FALSE(colour->asset.descriptor()->isMosaiced());
        EXPECT_EQ(colour->asset.descriptor()->extent().channelCount(), 3u);
        EXPECT_TRUE(an::tests::viewer::lineageMentions(colour->asset, "XTransFast"));
        byFast = uint16Samples(colour->asset);
    }

    an::Result<an::consumer::ImageAsset> byRcd =
        an::ui::demosaicViewImage(session, kept->asset, an::DemosaicMethod::Rcd, {});
    ASSERT_TRUE(byRcd.hasValue()) << describe(byRcd.status());
    ASSERT_TRUE(byRcd->descriptor().has_value());
    EXPECT_EQ(byRcd->descriptor()->extent().channelCount(), 3u);
    EXPECT_TRUE(an::tests::viewer::lineageMentions(*byRcd, "MarkesteijnThreePass"));
    const std::vector<std::uint16_t> threePass = uint16Samples(*byRcd);
    ASSERT_EQ(threePass.size(), 72u * 48u * 3u);
    ASSERT_EQ(byFast.size(), threePass.size());
    ASSERT_NE(byFast, threePass) << "the case is only worth something where the two estimates disagree";

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    ASSERT_TRUE(scope.has_value());
    an::Result<an::consumer::AssetReadLease> lease = kept->asset.read();
    ASSERT_TRUE(lease.hasValue()) << lease.status().message();
    const auto* mosaic = std::get_if<an::Image<std::uint16_t>>(&lease->image());
    ASSERT_NE(mosaic, nullptr);
    {
        // The engine's three-pass estimate of the same samples, asked for directly.
        const auto source = mosaic->view();
        ASSERT_TRUE(source.has_value());
        const an::Result<an::DemosaicPlan> plan = an::planDemosaic(
            an::DemosaicRequest{.method = an::DemosaicMethod::MarkesteijnThreePass, .domain = {.whiteLevel = 65535.0}},
            mosaic->descriptor()->cfa());
        ASSERT_TRUE(plan.hasValue()) << describe(plan.status());
        const std::optional<an::ImageDescriptor> described =
            an::tests::viewer::describeImage(scope->engine(), an::SampleType::UInt16, 72u, 48u, Channels::Rgb);
        ASSERT_TRUE(described.has_value());
        an::Result<an::Image<std::uint16_t>> expected = an::Image<std::uint16_t>::create(
            scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        {
            std::optional<an::ImageWriteAccess<std::uint16_t>> access = expected->write();
            ASSERT_TRUE(access.has_value());
            const an::Result<an::DemosaicResult> ran =
                an::demosaicImage(scope->execution(), an::SampleType::UInt16, source->data(), source->layout(),
                                  access->view().data(), access->view().layout(), *plan);
            ASSERT_TRUE(ran.hasValue()) << describe(ran.status());
        }
        EXPECT_EQ(uint16Samples(*expected), threePass) << "the view's demosaic is the engine's three-pass estimate";
    }

    // The frame entry points, which the processes and the integration demosaic through.
    const an::Result<an::consumer::AnyImage> frame =
        an::ui::demosaicViewFrame(scope->engine(), scope->execution(), lease->image(), an::DemosaicMethod::Vng4);
    ASSERT_TRUE(frame.hasValue()) << describe(frame.status());
    const auto* framed = std::get_if<an::Image<std::uint16_t>>(&*frame);
    ASSERT_NE(framed, nullptr);
    EXPECT_EQ(framed->descriptor()->extent().channelCount(), 3u);
    EXPECT_EQ(uint16Samples(*framed), threePass);
    const an::Result<an::ui::ViewDemosaic> withValidity = an::ui::demosaicViewFrameWithValidity(
        scope->engine(), scope->execution(), lease->image(), nullptr, an::DemosaicMethod::Amaze);
    ASSERT_TRUE(withValidity.hasValue()) << describe(withValidity.status());
    const auto* validated = std::get_if<an::Image<std::uint16_t>>(&withValidity->image);
    ASSERT_NE(validated, nullptr);
    EXPECT_EQ(validated->descriptor()->extent().channelCount(), 3u);
    EXPECT_FALSE(withValidity->validity.has_value()) << "a mosaic every sample of which means something needs no mask";
}

// FITS states a mosaic's filters in one two-by-two pattern card and its writer refuses a tabulated mosaic, but the
// write took FITS for a container that holds every mosaic, so an X-Trans mosaic written as FITS failed in the codec, in
// the codec's words, after any conversion. It is refused before any work, naming the arrangement and what holds it, and
// leaves nothing; TIFF holds it.
TEST(ImageViewFilesTest, AnXTransMosaicIsRefusedForFitsByNameAndLeavesNoFile)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    std::optional<an::consumer::ImageAsset> mosaic = an::tests::viewer::xtransMosaicAsset(session);
    ASSERT_TRUE(mosaic.has_value());
    const QString fits = directory.filePath(QStringLiteral("xtrans.fits"));
    const an::Status refused = an::ui::writeViewImage(session, *mosaic, fits, intentFor(an::SampleType::UInt16), {});
    ASSERT_TRUE(refused.failed());
    EXPECT_EQ(refused.code(), an::ErrorCode::Unsupported);
    const std::string message = refused.message();
    EXPECT_NE(message.find("X-Trans"), std::string::npos) << message;
    EXPECT_NE(message.find("XISF or TIFF"), std::string::npos) << message;
    EXPECT_TRUE(QDir(directory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
        << "a refused write leaves nothing behind";
    EXPECT_TRUE(an::tests::viewer::writeXTransTiff(session, directory.filePath(QStringLiteral("xtrans.tif"))));
}

// A light calibrated in memory states that its non-finite samples mean nothing, and the integration's alignment
// demosaiced it through demosaicViewFrame, which refused every mosaic stating any rule but every sample meaning
// something, so no one-shot-colour light calibrated in memory was ever aligned. That rule travels in the samples
// themselves: a colour pixel estimated from a non-finite sample is not finite in any colour, and the colour image
// states the mosaic's rule.
TEST(ImageViewFilesTest, AMosaicWhoseNonFiniteSamplesMeanNothingIsDemosaicedUnderTheSameRule)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(provider->engineSession());
    ASSERT_TRUE(scope.has_value());
    const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
    ASSERT_TRUE(rggb.has_value());
    const std::optional<an::ImageDescriptor> described = an::tests::viewer::describeImage(
        scope->engine(), an::SampleType::Float32, 16u, 16u, Channels::Grey, an::NumericDomain::NormalizedLinear, *rggb);
    ASSERT_TRUE(described.has_value());
    an::Result<an::Image<float>> mosaic = an::Image<float>::create(
        scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    ASSERT_TRUE(mosaic.hasValue()) << mosaic.status().message();
    {
        std::optional<an::ImageWriteAccess<float>> access = mosaic->write();
        ASSERT_TRUE(access.has_value());
        for (std::uint32_t y = 0u; y < 16u; ++y)
        {
            const auto row = access->view().row(0u, y);
            ASSERT_TRUE(row.has_value());
            for (std::uint32_t x = 0u; x < 16u; ++x)
            {
                (*row)[x] = (x == 8u && y == 8u) ? std::nanf("") : 0.25f + 0.05f * static_cast<float>((x + y) % 2u);
            }
        }
    }
    {
        an::ImageSemanticsRequest meaning = mosaic->metadata().semantics().toRequest();
        meaning.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly});
        const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
        ASSERT_TRUE(semantics.hasValue()) << semantics.status().message();
        const an::Status stated = mosaic->transition(
            an::MetadataTransition{.semantics = *semantics, .record = *an::ProvenanceEntry::create("calibrated", "")});
        ASSERT_TRUE(stated.ok()) << stated.message();
    }

    for (const an::DemosaicMethod method : {an::DemosaicMethod::Bilinear, an::DemosaicMethod::Rcd})
    {
        SCOPED_TRACE(an::demosaicMethodName(method));
        an::Result<an::Image<float>> copy =
            an::Image<float>::createDestination(scope->engine(), *mosaic->descriptor(), an::StorageArrangement::Planar,
                                                an::StorageInitialization::Zeroed, *mosaic);
        ASSERT_TRUE(copy.hasValue());
        {
            std::optional<an::ImageWriteAccess<float>> access = copy->write();
            ASSERT_TRUE(access.has_value());
            for (std::uint32_t y = 0u; y < 16u; ++y)
            {
                const auto from = mosaic->view()->row(0u, y);
                const auto to = access->view().row(0u, y);
                ASSERT_TRUE(from.has_value() && to.has_value());
                std::copy(from->begin(), from->end(), to->begin());
            }
        }
        const an::Result<an::consumer::AnyImage> colour = an::ui::demosaicViewFrame(
            scope->engine(), scope->execution(), an::consumer::AnyImage(std::move(*copy)), method);
        ASSERT_TRUE(colour.hasValue()) << describe(colour.status());
        const auto* image = std::get_if<an::Image<float>>(&*colour);
        ASSERT_NE(image, nullptr);
        ASSERT_TRUE(image->descriptor().has_value());
        EXPECT_EQ(image->descriptor()->extent().channelCount(), 3u);
        EXPECT_FALSE(image->descriptor()->isMosaiced());
        EXPECT_EQ(image->metadata().semantics().validity().rule(), an::ValidityRule::FiniteOnly);
        const auto view = image->view();
        ASSERT_TRUE(view.has_value());
        for (std::uint32_t channel = 0u; channel < 3u; ++channel)
        {
            EXPECT_FALSE(std::isfinite(*view->sampleAt(channel, 8u, 8u, 0u)))
                << "the pixel estimated from the non-finite sample means nothing in colour " << channel;
            EXPECT_TRUE(std::isfinite(*view->sampleAt(channel, 2u, 2u, 0u)))
                << "a pixel far from it is estimated from samples that all mean something, colour " << channel;
        }
    }
}

TEST(ImageViewFilesTest, APreviewLightIsCalibratedByItsMastersInFractionsOfFullScale)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    const auto flatValue = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5f; };
    const auto constant = [](float value) {
        return [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; };
    };
    std::optional<an::consumer::ImageAsset> light = makeAsset<float>(session, 8u, 8u, Channels::Grey, constant(0.6f));
    std::optional<an::consumer::ImageAsset> dark = makeAsset<float>(session, 8u, 8u, Channels::Grey, constant(0.1f));
    std::optional<an::consumer::ImageAsset> flat = makeAsset<float>(session, 8u, 8u, Channels::Grey, flatValue);
    ASSERT_TRUE(light.has_value() && dark.has_value() && flat.has_value());
    const QString lightPath = directory.filePath(QStringLiteral("light.fits"));
    const QString darkPath = directory.filePath(QStringLiteral("dark.fits"));
    const QString flatPath = directory.filePath(QStringLiteral("flat.fits"));
    writeFits(session, *light, lightPath, an::SampleType::Float32);
    writeFits(session, *dark, darkPath, an::SampleType::Float32);
    writeFits(session, *flat, flatPath, an::SampleType::Float32);

    an::ui::ViewCalibration calibration;
    calibration.darkPath = darkPath;
    calibration.flatPath = flatPath;
    calibration.settings.cosmeticCorrection = false;
    calibration.settings.flatAutoLevel = true;
    an::ui::ViewMasterCache masters;
    an::Result<an::ui::ViewReadResult> calibrated =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, &masters);
    ASSERT_TRUE(calibrated.hasValue()) << describe(calibrated.status());
    EXPECT_TRUE(calibrated->calibrated);
    EXPECT_NEAR(floatAt(calibrated->asset, 4u, 4u), 0.5, 1e-5)
        << "the dark subtracted and the flat, normalized by its central mean, divided";
    EXPECT_EQ(masters.size(), 2u) << "the masters are kept for the next preview";

    // The next preview reads the masters from the cache, not the files.
    ASSERT_TRUE(QFile::remove(darkPath));
    ASSERT_TRUE(QFile::remove(flatPath));
    an::Result<an::ui::ViewReadResult> again =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, &masters);
    ASSERT_TRUE(again.hasValue()) << describe(again.status());
    EXPECT_NEAR(floatAt(again->asset, 1u, 1u), 0.5, 1e-5);

    masters.clear();
    const an::Result<an::ui::ViewReadResult> missing =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, &masters);
    ASSERT_FALSE(missing.hasValue()) << "a master that is gone is reported, not skipped";
}

TEST(ImageViewFilesTest, ADarkHoldingItsBiasHasTheBiasTakenOutFirst)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    const auto constant = [](float value) {
        return [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; };
    };
    std::optional<an::consumer::ImageAsset> light = makeAsset<float>(session, 4u, 4u, Channels::Grey, constant(0.7f));
    std::optional<an::consumer::ImageAsset> bias = makeAsset<float>(session, 4u, 4u, Channels::Grey, constant(0.05f));
    std::optional<an::consumer::ImageAsset> dark = makeAsset<float>(session, 4u, 4u, Channels::Grey, constant(0.25f));
    ASSERT_TRUE(light.has_value() && bias.has_value() && dark.has_value());
    const QString lightPath = directory.filePath(QStringLiteral("light.fits"));
    const QString biasPath = directory.filePath(QStringLiteral("bias.fits"));
    const QString darkPath = directory.filePath(QStringLiteral("dark.fits"));
    writeFits(session, *light, lightPath, an::SampleType::Float32);
    writeFits(session, *bias, biasPath, an::SampleType::Float32);
    writeFits(session, *dark, darkPath, an::SampleType::Float32);

    an::ui::ViewCalibration calibration;
    calibration.biasPath = biasPath;
    calibration.darkPath = darkPath;
    calibration.subtractBiasFromDark = true;
    calibration.settings.cosmeticCorrection = false;
    an::Result<an::ui::ViewReadResult> lessBias =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, nullptr);
    ASSERT_TRUE(lessBias.hasValue()) << describe(lessBias.status());
    EXPECT_NEAR(floatAt(lessBias->asset, 2u, 2u), 0.7 - 0.05 - (0.25 - 0.05), 1e-5);

    calibration.subtractBiasFromDark = false;
    an::Result<an::ui::ViewReadResult> darkAlone =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, nullptr);
    ASSERT_TRUE(darkAlone.hasValue()) << describe(darkAlone.status());
    EXPECT_NEAR(floatAt(darkAlone->asset, 2u, 2u), 0.7 - 0.25, 1e-5)
        << "with no bias in the dark, the light is calibrated by the dark alone";

    calibration.darkPath.clear();
    an::Result<an::ui::ViewReadResult> biasAlone =
        an::ui::readViewImage(session, ViewReadRequest{.path = lightPath, .calibration = calibration}, {}, nullptr);
    ASSERT_TRUE(biasAlone.hasValue()) << describe(biasAlone.status());
    EXPECT_NEAR(floatAt(biasAlone->asset, 2u, 2u), 0.7 - 0.05, 1e-5);
}

TEST(ImageViewFilesTest, TheCalibrationKeyChangesWithEveryMasterAndSetting)
{
    an::ui::ViewCalibration base;
    base.darkPath = QStringLiteral("dark.fits");
    const QString key = an::ui::viewCalibrationKey(base);
    an::ui::ViewCalibration changed = base;
    changed.settings.sigmaHot = 4.0;
    EXPECT_NE(key, an::ui::viewCalibrationKey(changed));
    changed = base;
    changed.flatPath = QStringLiteral("flat.fits");
    EXPECT_NE(key, an::ui::viewCalibrationKey(changed));
    changed = base;
    changed.subtractBiasFromDark = true;
    EXPECT_NE(key, an::ui::viewCalibrationKey(changed));
    EXPECT_EQ(key, an::ui::viewCalibrationKey(base));
}

TEST(ImageViewFilesTest, AStoppedReadAnswersCancelledAndMakesNoAsset)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(1u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("frame.fits"));
    std::optional<an::consumer::ImageAsset> written = makeAsset<std::uint8_t>(
        session, 4u, 4u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return std::uint8_t{3}; });
    ASSERT_TRUE(written.has_value());
    writeFits(session, *written, path, an::SampleType::UInt8);

    std::stop_source stop;
    stop.request_stop();
    const an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session, ViewReadRequest{.path = path}, stop.get_token(), nullptr);
    ASSERT_FALSE(read.hasValue());
    EXPECT_EQ(read.status().code(), an::ErrorCode::Cancelled);
}

TEST(ImageViewFilesTest, TheStatisticsAreEachColourChannelsInTheImagesOwnUnits)
{
    std::optional<an::tests::viewer::TestSessionProvider> provider = openFileProvider(2u);
    ASSERT_TRUE(provider.has_value());
    const auto& session = provider->engineSession();
    std::optional<an::consumer::ImageAsset> image = makeAsset<std::uint16_t>(
        session, 4u, 1u, Channels::Rgba, [](std::uint32_t x, std::uint32_t, std::uint32_t channel) {
            return static_cast<std::uint16_t>(channel == 3u ? 9u : (x + 1u) * 100u * (channel + 1u));
        });
    ASSERT_TRUE(image.has_value());
    const an::Result<an::ui::ViewImageStatistics> measured = an::ui::measureViewImage(session, *image, {});
    ASSERT_TRUE(measured.hasValue()) << measured.status().message();
    EXPECT_EQ(measured->channels, 3u) << "alpha is not measured";
    EXPECT_DOUBLE_EQ(measured->channel[0].minimum, 100.0);
    EXPECT_DOUBLE_EQ(measured->channel[0].maximum, 400.0);
    EXPECT_DOUBLE_EQ(measured->channel[0].mean, 250.0);
    EXPECT_DOUBLE_EQ(measured->channel[0].median, 250.0);
    EXPECT_DOUBLE_EQ(measured->channel[2].maximum, 1200.0);
    EXPECT_NEAR(measured->channel[0].standardDeviation, std::sqrt(12500.0), 1e-9);
}
