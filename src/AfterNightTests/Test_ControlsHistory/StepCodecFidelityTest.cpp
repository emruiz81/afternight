// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a step codec gives back: a step written and read again is the image it was written from - every sample bit for
/// bit in each of the eight representations, the description, every metadata object, the validity mask and the
/// identity - for the snapshot and for XISF with each compression, an XISF step still an image any reader of the
/// format opens, a step whole at its path or not there, and a record longer than an XISF image carries refused.

#include "Test_ControlsHistory/StepCodecTestSupport.h"

#include "fixtures/ScopedTempPath.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/AcquisitionMetadata.h>
#include <AfterNightEngine/Image/AstrometricSolution.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stop_token>
#include <string>

namespace
{

using namespace an::tests::history;
using an::tests::viewer::Channels;
using an::ui::history::StepCodecPtr;

constexpr std::array<an::SampleType, 8u> kEverySampleType = {
    an::SampleType::UInt8,  an::SampleType::Int8,  an::SampleType::UInt16,  an::SampleType::Int16,
    an::SampleType::UInt32, an::SampleType::Int32, an::SampleType::Float32, an::SampleType::Float64};

/// The bits of the samples a representation is most likely to lose: its extremes, zero, minus one, and for a
/// floating representation both zeros, both infinities, a quiet and a signalling not-a-number with a payload, the
/// smallest subnormal and values outside the unit range. Everything else is mixed bits.
std::uint64_t edgeBits(an::SampleType type, std::uint32_t x, std::uint32_t y, std::uint32_t c)
{
    if (y != 0u)
    {
        return mixedBits(x, y, c);
    }
    if (type == an::SampleType::Float32)
    {
        constexpr std::array<std::uint32_t, 12u> kBits = {0x00000000u, 0x80000000u, 0x7f800000u, 0xff800000u,
                                                          0x7fc12345u, 0xffc00001u, 0x7f812345u, 0x00000001u,
                                                          0x7f7fffffu, 0xff7fffffu, 0x447a0000u, 0xc47a0000u};
        return kBits[x % kBits.size()];
    }
    if (type == an::SampleType::Float64)
    {
        constexpr std::array<std::uint64_t, 12u> kBits = {
            0x0000000000000000ull, 0x8000000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
            0x7ff8000012345678ull, 0xfff8000000000001ull, 0x7ff0000012345678ull, 0x0000000000000001ull,
            0x7fefffffffffffffull, 0xffefffffffffffffull, 0x408f400000000000ull, 0xc08f400000000000ull};
        return kBits[x % kBits.size()];
    }
    constexpr std::array<std::uint64_t, 6u> kBits = {0x0ull,
                                                     0xffffffffffffffffull,
                                                     0x8080808080808080ull,
                                                     0x7f7f7f7f7f7f7f7full,
                                                     0x8000000080000000ull,
                                                     0x7fffffff7fffffffull};
    return kBits[x % kBits.size()];
}

an::CodecCard card(an::CodecNamespace origin, std::string keyword, std::string value, std::string comment = {})
{
    return *an::CodecCard::create(origin, std::move(keyword), std::move(value), std::move(comment));
}

/// Metadata in which every object states everything it can, and the preserved cards hold what an XISF unit cannot
/// spell beside what it can.
an::ImageMetadata everyTypedKind()
{
    an::RawCameraMetadata camera;
    camera.blackLevel = 512u;
    camera.channelBlack[1] = 3u;
    camera.whiteLevel = 16383u;
    camera.blackPatternRows = 2u;
    camera.blackPatternColumns = 2u;
    camera.blackPattern = {1u, 2u, 3u, 4u};
    camera.cameraMultipliers[0] = 2.1484375;
    camera.cameraMultipliers[1] = 1.0;
    camera.cameraMultipliers[2] = 1.4921875;
    camera.daylightMultipliers[0] = 2.25;
    camera.daylightMultipliers[1] = 0.9375;
    camera.daylightMultipliers[2] = 1.375;
    camera.cameraToRgb[0][0] = 1.75;
    camera.cameraToRgb[1][1] = 1.5;
    camera.cameraToRgb[2][2] = 1.25;
    camera.cameraToXyz[0][0] = 0.6;
    camera.cameraToXyz[1][1] = 0.7;
    camera.cameraToXyz[2][2] = 0.8;
    camera.isoSpeed = 800.0;
    camera.focalLengthMillimeters = 530.0;
    camera.aperture = 5.0;
    camera.orientation = 6;
    camera.exifColorSpace = 1;
    camera.asShotWhiteBalanceApplied = true;
    const an::Result<an::AcquisitionMetadata> acquisition = an::AcquisitionMetadata::create(
        {.instrumentName = "Camera Q",
         .telescopeName = "Refractor 106",
         .observerName = "Observer \xc3\x91",
         .objectName = "NGC 7000",
         .filterName = "Ha 3nm",
         .exposureSeconds = 300.25,
         .sensorTemperatureCelsius = -10.5,
         .gainElectronsPerAdu = 0.2734375,
         .binning = an::PixelBinning{.horizontal = 2u, .vertical = 3u},
         .exposureStart = an::UtcInstant{.nanosecondsSinceEpoch = 1789000000123456789ll},
         .rawCamera = camera});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(
        {.transferFunction = an::TransferFunction::PureGamma,
         .gammaExponent = 2.2,
         .processingState = {.biasSubtracted = true,
                             .darkSubtracted = true,
                             .flatFielded = true,
                             .cosmeticCorrected = true,
                             .demosaiced = true,
                             .backgroundExtracted = true,
                             .registered = true,
                             .normalized = true,
                             .colorCalibrated = true},
         .validity = *an::ValidityDescriptor::create(
             {.rule = an::ValidityRule::DomainRange, .lowestValid = -0.125, .highestValid = 1.5})});
    EXPECT_TRUE(semantics.hasValue()) << semantics.status().message();
    const an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(
        {.referencePoint = {.rightAscensionDegrees = 312.7512345678, .declinationDegrees = -44.3312345678},
         .referencePixelX = 6.5,
         .referencePixelY = 3.25,
         .pixelScaleArcsecPerPixel = 1.8734567,
         .orientationDegrees = 181.53125,
         .mirrored = true,
         .hasDistortion = true});
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    an::Provenance provenance = *an::Provenance::create(an::ProvenanceSourceKind::File, "D:/lights/light-001.fits");
    provenance = *provenance.appended(*an::ProvenanceEntry::create("decodeImage", "light-001.fits"));
    provenance = *provenance.appended(*an::ProvenanceEntry::create("Curves", "a line\nand a second line"));
    provenance = *provenance.appended(*an::ProvenanceEntry::create("Renamed", "to \xe6\x98\x9f\xe4\xba\x91"));
    const an::Result<an::CodecMetadata> codec = an::CodecMetadata::create(
        {.sampleFormat = an::RawSampleFormat{.sizeBytes = 2u, .sampleClass = an::RawSampleClass::SignedInteger},
         .byteOrder = an::ByteOrder::Big,
         .scale = 1.0,
         .zeroOffset = 32768.0},
        {// What a FITS header says, its structure and an identity another file was written for among it.
         card(an::CodecNamespace::Fits, "SIMPLE", "T", "conforms"), card(an::CodecNamespace::Fits, "BITPIX", "16"),
         card(an::CodecNamespace::Fits, "BZERO", "32768"),
         card(an::CodecNamespace::Fits, "OBJECT", "'NGC 7000'", "the target"),
         card(an::CodecNamespace::Fits, "EXPTIME", "300.25"),
         card(an::CodecNamespace::Fits, "HISTORY", "", "calibrated by a pipeline"),
         card(an::CodecNamespace::Fits, "HISTORY", "", "and registered"),
         card(an::CodecNamespace::Fits, "ANARTID", "'an-earlier-artifact'"),
         card(an::CodecNamespace::Fits, "ANRUNID", "'an-earlier-run'"),
         // What the container does not spell: a keyword longer than eight characters, text that is not printable
         // ASCII, a control character, a value as long as a card holds.
         card(an::CodecNamespace::Fits, "ESO INS FILT1 NAME", "'H_ALPHA'"),
         card(an::CodecNamespace::Fits, "OBSERVER", "'Observer \xc3\x91'"),
         card(an::CodecNamespace::Fits, "LONGTEXT", std::string(an::kMaxCodecCardTextBytes, 'x')),
         card(an::CodecNamespace::Xisf, "Observation:Object:Name", "NGC 7000", "String"),
         card(an::CodecNamespace::Xisf, "PixInsight:ProcessingHistory", "a line\nand a second\tline", "String"),
         card(an::CodecNamespace::Xisf, "not an identifier", "a property XISF cannot name"),
         card(an::CodecNamespace::Xisf, "Observation:Note", "a note", "a comment with a \x01 control"),
         // What another format's reader preserved.
         card(an::CodecNamespace::Tiff, "Exif.Image.Make", "Maker"),
         card(an::CodecNamespace::Png, "Software", "a program"),
         card(an::CodecNamespace::Jpeg, "Exif.Photo.ExposureTime", "1/250"),
         card(an::CodecNamespace::CameraRaw, "Exif.Photo.ISOSpeedRatings", "800"),
         card(an::CodecNamespace::Ser, "Observer", "someone"), card(an::CodecNamespace::Ffmpeg, "codec_name", "ffv1"),
         card(an::CodecNamespace::Unspecified, "anything", "at all")});
    EXPECT_TRUE(codec.hasValue()) << codec.status().message();
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create({.sampleType = an::SampleType::Int16,
                                  .byteOrder = an::ByteOrder::Big,
                                  .scaling = an::OutputScaling::Explicit,
                                  .scale = 0.5,
                                  .zeroOffset = 32768.0,
                                  .rounding = an::OutputRounding::Floor,
                                  .compression = an::OutputCompression::Zstd,
                                  .compressionLevel = 7u,
                                  .byteShuffle = false,
                                  .compressionBlockBytes = 65536u,
                                  .checksum = an::OutputChecksum::Sha256,
                                  .preserveSourceCards = false,
                                  .cards = {card(an::CodecNamespace::Fits, "CREATOR", "'a test'")}});
    EXPECT_TRUE(intent.hasValue()) << intent.status().message();
    return an::ImageMetadata::create(*acquisition, *semantics, *solution, std::move(provenance), *codec, *intent);
}

/// The fixture of every fidelity case: a session, a directory of the case's own, and the codec the case is run for.
class StepCodecFidelity : public ::testing::TestWithParam<CodecCase>
{
protected:
    void SetUp() override
    {
        m_Provider = openStepCodecSession();
        ASSERT_TRUE(m_Provider.has_value());
        m_Directory = an::tests::fixtures::makeScopedTempDir("afternight-step-codec");
        m_Codec = GetParam().make();
        ASSERT_NE(m_Codec, nullptr);
    }

    void TearDown() override
    {
        pumpQuietly();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Provider->engineSession();
    }

    [[nodiscard]] const an::ui::history::StepCodec& codec() const
    {
        return *m_Codec;
    }

    /// The path of a step of this codec named `name`, in the case's directory.
    [[nodiscard]] std::string stepPath(const std::string& name) const
    {
        return utf8Of(m_Directory.path() / (name + std::string(codec().fileExtension())));
    }

    /// Writes `image` as a step named `name` and reads it back; nothing, with the case failed, when either refuses.
    [[nodiscard]] std::optional<an::consumer::ImageAsset> roundTrip(const an::consumer::ImageAsset& image,
                                                                    const std::string& name) const
    {
        const an::ArtifactIdentity identity = stepIdentityOf(name);
        {
            an::Result<an::consumer::AssetReadLease> lease = image.read();
            if (!lease.hasValue())
            {
                ADD_FAILURE() << lease.status().message();
                return std::nullopt;
            }
            const an::Status written = codec().write(session(), *lease, stepPath(name), identity, {});
            if (!written.ok())
            {
                ADD_FAILURE() << "the step was not written: " << written.message();
                return std::nullopt;
            }
        }
        an::Result<an::consumer::ImageAsset> read = codec().read(session(), stepPath(name), identity, {});
        if (!read.hasValue())
        {
            ADD_FAILURE() << "the step was not read: " << read.status().message();
            return std::nullopt;
        }
        return std::move(*read);
    }

    /// Builds the image, takes it through the codec, and holds what comes back to what went in.
    void expectHeldWhole(const StepImageRequest& request, const std::string& name) const
    {
        const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
        ASSERT_NE(image, nullptr);
        const StepState original = stepStateOf(*image);
        const std::optional<an::consumer::ImageAsset> restored = roundTrip(*image, name);
        ASSERT_TRUE(restored.has_value());
        expectSameStep(original, stepStateOf(*restored));
        // The image the step was written from is as it was.
        expectSameStep(original, stepStateOf(*image));
    }

    std::optional<TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Directory;
    StepCodecPtr m_Codec;
};

TEST_P(StepCodecFidelity, EverySampleFormatReadsBackBitForBitInGreyAndInColour)
{
    for (const an::SampleType type : kEverySampleType)
    {
        for (const Channels channels : {Channels::Grey, Channels::Rgb})
        {
            SCOPED_TRACE(std::string(an::sampleTypeName(type)) + (channels == Channels::Grey ? " grey" : " colour"));
            StepImageRequest request;
            request.type = type;
            request.channels = channels;
            request.domain =
                an::isFloatingSampleType(type) ? an::NumericDomain::NormalizedLinear : an::NumericDomain::RawSensorCode;
            request.samples = [type](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                return edgeBits(type, x, y, c);
            };
            expectHeldWhole(request,
                            std::string(an::sampleTypeName(type)) + (channels == Channels::Grey ? "-grey" : "-colour"));
        }
    }
}

TEST_P(StepCodecFidelity, TheDescriptionReadsBackAsWritten)
{
    // What a file of a standard format states of an image is less than its description: each numeric domain, a filter
    // array with its phase, the row the image counts from and a fourth channel come back because the step says them.
    for (const an::NumericDomain domain : {an::NumericDomain::NormalizedLinear, an::NumericDomain::EncodedNonlinear,
                                           an::NumericDomain::RawSensorCode, an::NumericDomain::PhysicalScaled})
    {
        SCOPED_TRACE(static_cast<int>(domain));
        StepImageRequest request;
        request.type = an::SampleType::Float32;
        request.channels = Channels::Rgb;
        request.domain = domain;
        expectHeldWhole(request, "domain-" + std::to_string(static_cast<int>(domain)));
    }
    {
        SCOPED_TRACE("a Bayer mosaic whose block starts off the origin");
        StepImageRequest request;
        request.type = an::SampleType::UInt16;
        request.domain = an::NumericDomain::RawSensorCode;
        request.cfa = *an::CfaDescriptor::create(an::CfaPattern::Grbg, 1u, 1u);
        expectHeldWhole(request, "mosaic");
    }
    {
        SCOPED_TRACE("a six-by-six mosaic");
        StepImageRequest request;
        request.type = an::SampleType::UInt16;
        request.domain = an::NumericDomain::RawSensorCode;
        request.cfa = an::tests::viewer::xtransFilterArray();
        expectHeldWhole(request, "x-trans");
    }
    {
        SCOPED_TRACE("rows counted from the bottom");
        StepImageRequest request;
        request.type = an::SampleType::Int16;
        request.domain = an::NumericDomain::RawSensorCode;
        request.origin = an::CoordinateOrigin::BottomLeft;
        expectHeldWhole(request, "bottom-left");
    }
    {
        SCOPED_TRACE("four channels");
        StepImageRequest request;
        request.type = an::SampleType::UInt8;
        request.channels = Channels::Rgba;
        request.domain = an::NumericDomain::EncodedNonlinear;
        expectHeldWhole(request, "rgba");
    }
}

TEST_P(StepCodecFidelity, EveryMetadataObjectReadsBackAsWritten)
{
    StepImageRequest request;
    request.type = an::SampleType::UInt16;
    request.channels = Channels::Rgb;
    request.domain = an::NumericDomain::RawSensorCode;
    request.metadata = everyTypedKind();
    expectHeldWhole(request, "every-kind");
}

TEST_P(StepCodecFidelity, MetadataThatStatesNothingReadsBackStatingNothing)
{
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    expectHeldWhole(request, "nothing-stated");
    // A step read from a file must not take the file for its source: the lineage stays the one written.
    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
    ASSERT_NE(image, nullptr);
    const std::optional<an::consumer::ImageAsset> restored = roundTrip(*image, "nothing-stated-again");
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(stepStateOf(*restored).metadata == an::ImageMetadata{});
}

TEST_P(StepCodecFidelity, TheValidityMaskReadsBackBitForBit)
{
    {
        SCOPED_TRACE("one mask for a colour image's channels, under the explicit rule");
        StepImageRequest request;
        request.type = an::SampleType::Float32;
        request.channels = Channels::Rgb;
        request.maskPlanes = 1u;
        request.metadata = an::ImageMetadata::create(
            {},
            *an::ImageSemantics::create(
                {.validity = *an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask})}),
            {}, {}, {}, std::nullopt);
        expectHeldWhole(request, "shared-mask");
    }
    {
        SCOPED_TRACE("a mask for each channel");
        StepImageRequest request;
        request.type = an::SampleType::UInt16;
        request.channels = Channels::Rgb;
        request.domain = an::NumericDomain::RawSensorCode;
        request.maskPlanes = 3u;
        expectHeldWhole(request, "channel-masks");
    }
    {
        SCOPED_TRACE("a mask in which nothing is valid, of a width that fills its last byte");
        StepImageRequest request;
        request.type = an::SampleType::Float64;
        request.width = 16u;
        request.maskPlanes = 1u;
        request.mask = [](std::uint32_t, std::uint32_t, std::uint32_t) { return false; };
        expectHeldWhole(request, "nothing-valid");
    }
    {
        SCOPED_TRACE("no mask");
        StepImageRequest request;
        request.type = an::SampleType::Float32;
        const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
        ASSERT_NE(image, nullptr);
        const std::optional<an::consumer::ImageAsset> restored = roundTrip(*image, "no-mask");
        ASSERT_TRUE(restored.has_value());
        EXPECT_FALSE(stepStateOf(*restored).hasMask);
    }
}

TEST_P(StepCodecFidelity, AMaskOfManyCardsReadsBackBitForBit)
{
    // A mask with no pattern to compress, large enough that an XISF image carries it across many of its cards.
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    request.width = 1500u;
    request.height = 1100u;
    request.channels = Channels::Rgb;
    request.maskPlanes = 3u;
    request.mask = [](std::uint32_t x, std::uint32_t y, std::uint32_t plane) {
        return (mixedBits(x, y, plane, 5u) & 1u) != 0u;
    };
    request.metadata = everyTypedKind();
    expectHeldWhole(request, "large-mask");
}

TEST_P(StepCodecFidelity, AStepIsReadOnlyForTheIdentityItWasWrittenFor)
{
    StepImageRequest request;
    request.metadata = everyTypedKind();
    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(roundTrip(*image, "mine").has_value());

    // The metadata's own cards name an earlier artifact; the step is neither that one nor another history's.
    for (const an::ArtifactIdentity& other : {stepIdentityOf("another"), stepIdentityOf("mine", "another-run"),
                                              stepIdentityOf("an-earlier-artifact", "an-earlier-run")})
    {
        const an::Result<an::consumer::ImageAsset> refused = codec().read(session(), stepPath("mine"), other, {});
        ASSERT_FALSE(refused.hasValue());
        EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
    }
}

TEST_P(StepCodecFidelity, AFileThatIsNotAStepIsRefused)
{
    const an::ArtifactIdentity identity = stepIdentityOf("mine");
    {
        std::ofstream stream(m_Directory.path() / ("text" + std::string(codec().fileExtension())), std::ios::binary);
        stream << "this is not an image of any kind";
    }
    EXPECT_FALSE(codec().read(session(), stepPath("text"), identity, {}).hasValue());
    EXPECT_FALSE(codec().read(session(), stepPath("missing"), identity, {}).hasValue());
}

TEST_P(StepCodecFidelity, AStepIsWholeAtItsPathOrNotThere)
{
    StepImageRequest request;
    request.maskPlanes = 1u;
    request.metadata = everyTypedKind();
    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
    ASSERT_NE(image, nullptr);
    const an::ArtifactIdentity identity = stepIdentityOf("step");
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    ASSERT_TRUE(lease.hasValue());

    // A write stopped before it starts, and one into a directory that is not there, publish nothing.
    std::stop_source stopped;
    stopped.request_stop();
    EXPECT_FALSE(codec().write(session(), *lease, stepPath("step"), identity, stopped.get_token()).ok());
    const std::string nowhere =
        utf8Of(m_Directory.path() / "missing-directory" / ("step" + std::string(codec().fileExtension())));
    EXPECT_FALSE(codec().write(session(), *lease, nowhere, identity, {}).ok());
    EXPECT_TRUE(entriesOf(m_Directory.path()).empty()) << "a refused write left something behind";

    // A write and a read leave the step's file and nothing beside it.
    ASSERT_TRUE(codec().write(session(), *lease, stepPath("step"), identity, {}).ok());
    ASSERT_TRUE(codec().read(session(), stepPath("step"), identity, {}).hasValue());
    EXPECT_EQ(entriesOf(m_Directory.path()), std::vector<std::string>{"step" + std::string(codec().fileExtension())});

    // A step written again at the same path replaces the one there.
    ASSERT_TRUE(codec().write(session(), *lease, stepPath("step"), identity, {}).ok());
    EXPECT_EQ(entriesOf(m_Directory.path()), std::vector<std::string>{"step" + std::string(codec().fileExtension())});
}

INSTANTIATE_TEST_SUITE_P(EveryCodec, StepCodecFidelity, ::testing::ValuesIn(everyStepCodec()),
                         [](const ::testing::TestParamInfo<CodecCase>& info) { return std::string(info.param.name); });

/// The cases of the XISF codec alone: what makes it worth offering is that its steps are images.
class XisfStepCodec : public StepCodecFidelity
{
};

TEST_P(XisfStepCodec, AStepOpensAsAnImageInAnyReaderOfTheFormat)
{
    StepImageRequest request;
    request.type = an::SampleType::UInt16;
    request.channels = Channels::Rgb;
    request.domain = an::NumericDomain::RawSensorCode;
    request.maskPlanes = 1u;
    request.metadata = everyTypedKind();
    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
    ASSERT_NE(image, nullptr);
    const StepState original = stepStateOf(*image);
    ASSERT_TRUE(roundTrip(*image, "image").has_value());

    // Read as any file of the format is read, with no knowledge of a step: the same samples, and the header a person
    // looks for - the object and the exposure - where the format puts them.
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::string path = stepPath("image");
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = path});
    ASSERT_TRUE(decoded.hasValue()) << decoded.status().message();
    const std::optional<an::RawFrameDescriptor> described = decoded->frame.descriptor();
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->residentSampleType(), std::optional<an::SampleType>(an::SampleType::UInt16));
    EXPECT_EQ(described->extent().width(), request.width);
    EXPECT_EQ(described->extent().height(), request.height);
    EXPECT_EQ(described->extent().channelCount(), 3u);
    const std::span<const std::byte> bytes = decoded->frame.bytes();
    ASSERT_EQ(bytes.size(), original.samples.size());
    EXPECT_EQ(std::memcmp(bytes.data(), original.samples.data(), bytes.size()), 0);
    EXPECT_EQ(decoded->metadata.acquisition().objectName(), "NGC 7000");
    EXPECT_EQ(decoded->metadata.acquisition().exposureSeconds(), std::optional<double>(300.25));
    const an::CodecCard* const object = decoded->metadata.codec().find(an::CodecNamespace::Fits, "OBJECT");
    ASSERT_NE(object, nullptr);
    EXPECT_NE(object->value().find("NGC 7000"), std::string::npos);
}

TEST_P(XisfStepCodec, AnImageOfTheFormatThatIsNoStepIsRefused)
{
    // The same image written as the application saves one: a good file of the format, and not a step.
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session(), request);
    ASSERT_NE(image, nullptr);
    const std::string path = stepPath("saved");
    {
        std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue());
        const an::OutputIntent intent = *an::OutputIntent::create({.sampleType = an::SampleType::Float32});
        const an::Status written = lease->visit([&](const auto& typed) -> an::Status {
            const an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            an::OutputRequest output;
            output.destinationPath = path;
            output.intent = &intent;
            const an::Result<an::OutputReport> report =
                an::writeImage(scope->engine(), scope->execution(), output, *frame, typed.metadata());
            return report.hasValue() ? an::Status::success() : report.status();
        });
        ASSERT_TRUE(written.ok()) << written.message();
    }
    const an::Result<an::consumer::ImageAsset> refused = codec().read(session(), path, stepIdentityOf("saved"), {});
    ASSERT_FALSE(refused.hasValue());
    EXPECT_EQ(refused.status().code(), an::ErrorCode::InvalidData);
}

TEST_P(XisfStepCodec, AnImageReadFromAStepIsWrittenAsANewStepWithItsOwnRecord)
{
    // A step's file opened as any image carries that step's record among its cards. Kept as a step of another
    // history, it must come back as itself and not as the step it was read from.
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    request.maskPlanes = 1u;
    request.metadata = everyTypedKind();
    const std::shared_ptr<an::consumer::ImageAsset> first = makeStepImage(session(), request);
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(roundTrip(*first, "first").has_value());

    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session());
    ASSERT_TRUE(scope.has_value());
    const std::string path = stepPath("first");
    an::Result<an::CodecDecodeResult> opened =
        an::decodeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = path});
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    scope.reset();

    // The image a view would hold after opening that file: its own samples, and the file's cards as its metadata.
    StepImageRequest second;
    second.type = an::SampleType::Float32;
    second.samples = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) { return mixedBits(x, y, c, 21u); };
    second.metadata = opened->metadata;
    expectHeldWhole(second, "second");
}

INSTANTIATE_TEST_SUITE_P(EveryCompression, XisfStepCodec, ::testing::ValuesIn(everyXisfStepCodec()),
                         [](const ::testing::TestParamInfo<CodecCase>& info) { return std::string(info.param.name); });

/// The bound of an XISF step, which is its cards'.
class XisfStepCodecBound : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_Provider = openStepCodecSession();
        ASSERT_TRUE(m_Provider.has_value());
        m_Directory = an::tests::fixtures::makeScopedTempDir("afternight-step-bound");
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_Provider->engineSession();
    }

    /// An eight-bit grey image of zeros whose mask has no pattern at all, so its bits are what its record weighs.
    /// The width must fill its last byte.
    [[nodiscard]] std::optional<an::consumer::ImageAsset> patternlessMask(std::uint32_t width,
                                                                          std::uint32_t height) const
    {
        an::Result<an::consumer::SessionHold> hold = session().hold();
        if (!hold.hasValue())
        {
            ADD_FAILURE() << hold.status().message();
            return std::nullopt;
        }
        const std::optional<an::ImageDescriptor> descriptor = an::tests::viewer::describeImage(
            hold->engine(), an::SampleType::UInt8, width, height, Channels::Grey, an::NumericDomain::RawSensorCode);
        if (!descriptor.has_value())
        {
            ADD_FAILURE() << "the image could not be described";
            return std::nullopt;
        }
        an::Result<an::Image<std::uint8_t>> image = an::Image<std::uint8_t>::create(
            hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
        an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(
            hold->engine(), *an::ImageExtent::create(width, height, 1u, hold->engine().limits()));
        if (!image.hasValue() || !mask.hasValue())
        {
            ADD_FAILURE() << "the image or its mask could not be made";
            return std::nullopt;
        }
        {
            const auto view = mask->view();
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                std::uint8_t* const row = view->data() + y * view->rowStrideBytes();
                for (std::uint32_t byte = 0u; byte < width / 8u; ++byte)
                {
                    row[byte] = static_cast<std::uint8_t>(mixedBits(byte, y, 0u, 31u));
                }
            }
        }
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(session(), an::consumer::AnyImage{std::move(*image)}, std::move(*mask));
        if (!asset.hasValue())
        {
            ADD_FAILURE() << asset.status().message();
            return std::nullopt;
        }
        return std::move(*asset);
    }

    std::optional<TestSessionProvider> m_Provider;
    an::tests::fixtures::ScopedTempPath m_Directory;
};

TEST_F(XisfStepCodecBound, ARecordNearlyAsLongAsTheCardsCarryReadsBackBitForBit)
{
    // 134 megapixels of mask with no pattern: 16.8 MB of bits, some 21 MiB of the 24 the cards carry.
    const std::optional<an::consumer::ImageAsset> image = patternlessMask(12000u, 11200u);
    ASSERT_TRUE(image.has_value());
    const StepState original = stepStateOf(*image);
    const StepCodecPtr codec = an::ui::history::xisfStepCodec(an::ui::history::StepCompression::None);
    const std::string path = utf8Of(m_Directory.path() / "step.xisf");
    const an::ArtifactIdentity identity = stepIdentityOf("step");
    {
        an::Result<an::consumer::AssetReadLease> lease = image->read();
        ASSERT_TRUE(lease.hasValue());
        const an::Status written = codec->write(session(), *lease, path, identity, {});
        ASSERT_TRUE(written.ok()) << written.message();
    }
    an::Result<an::consumer::ImageAsset> restored = codec->read(session(), path, identity, {});
    ASSERT_TRUE(restored.hasValue()) << restored.status().message();
    expectSameStep(original, stepStateOf(*restored));
}

TEST_F(XisfStepCodecBound, ARecordLongerThanTheCardsCarryIsRefusedAndNothingIsPublished)
{
    // 156 megapixels of mask with no pattern: more bits than the cards of one image carry.
    const std::optional<an::consumer::ImageAsset> image = patternlessMask(13000u, 12000u);
    ASSERT_TRUE(image.has_value());
    an::Result<an::consumer::AssetReadLease> lease = image->read();
    ASSERT_TRUE(lease.hasValue());
    const StepCodecPtr codec = an::ui::history::xisfStepCodec(an::ui::history::StepCompression::None);
    const an::Status refused =
        codec->write(session(), *lease, utf8Of(m_Directory.path() / "step.xisf"), stepIdentityOf("step"), {});
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.code(), an::ErrorCode::LimitExceeded);
    EXPECT_TRUE(entriesOf(m_Directory.path()).empty()) << "a refused step left something behind";

    // The snapshot holds the same step.
    const StepCodecPtr snapshot = an::ui::history::snapshotStepCodec();
    EXPECT_TRUE(
        snapshot->write(session(), *lease, utf8Of(m_Directory.path() / "step.ansnap"), stepIdentityOf("step"), {})
            .ok());
}

/// What a codec says of itself, which a history names its files and a setting its choice by.
TEST(StepCodecNames, EachCodecStatesItsNameAndItsExtension)
{
    using an::ui::history::StepCompression;
    EXPECT_EQ(an::ui::history::snapshotStepCodec()->name(), "snapshot");
    EXPECT_EQ(an::ui::history::snapshotStepCodec()->fileExtension(), ".ansnap");
    EXPECT_EQ(an::ui::history::xisfStepCodec(StepCompression::Lz4)->name(), "xisf");
    EXPECT_EQ(an::ui::history::xisfStepCodec(StepCompression::Lz4)->fileExtension(), ".xisf");
    EXPECT_EQ(an::ui::history::stepCompressionName(StepCompression::None), "none");
    EXPECT_EQ(an::ui::history::stepCompressionName(StepCompression::Lz4), "lz4");
    EXPECT_EQ(an::ui::history::stepCompressionName(StepCompression::Zstd), "zstd");
}

} // namespace
