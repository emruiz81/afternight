// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The engine fixture adapter: neutral descriptions into the engine's images, and its results back into neutral
// samples.
//
// It links the engine and the consumer runtime, and runs only in executables the registry classes as linking the
// engine. It holds its engine the way a product consumer does: an engine session owns the context, every read and
// write runs in an operation scope the session admits, and an image a case keeps is an image asset. The engine's
// published surface does the rest - a checked descriptor, an image of the stated representation, the codec facade
// and the output transaction. It translates and refuses, and never repairs: a frame whose representation is outside
// the eight, or whose channels the neutral layer does not describe, is refused by name.

#include "../common/FixtureExpectation.h"
#include "../common/FixtureImage.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>

namespace an::tests::fixtures::engine
{

/// The engine's version, as the loaded library reports it: what an engine suite's crash dumps carry.
[[nodiscard]] std::string loadedEngineVersion();

/// Where a result the engine produced in this executable came from.
[[nodiscard]] ExpectationProvenance engineProvenance(const std::string& commit = "-");

/// Whether a recorded result was produced by something other than the engine under test: another producer, or
/// another version of this one. A record the engine under test produced would hold it to its own output.
[[nodiscard]] bool producedElsewhere(const ExpectationProvenance& recorded);

/// An engine session as a fixture needs one: the FITS codec registered, the given thread budget, the engine's default
/// memory budget.
[[nodiscard]] an::Result<an::consumer::EngineSession> createFixtureSession(std::uint32_t maxConcurrency = 1u);

/// Admits one operation of a fixture's session, the whole width it may have, or refuses as busy rather than waiting:
/// a case that asks for a second operation while it holds the first has a defect, and a refusal names it where a
/// wait would hang.
[[nodiscard]] an::Result<an::consumer::OperationScope>
admitFixtureOperation(const an::consumer::EngineSession& session);

/// An image of any of the eight representations.
using EngineImage = an::consumer::AnyImage;

/// The engine's name for a fixture's representation, meaning, filter array and channels, and back where a decode
/// states one.
[[nodiscard]] an::SampleType engineSampleType(FixtureSampleType sampleType) noexcept;
[[nodiscard]] std::optional<FixtureSampleType> fixtureSampleTypeOf(an::SampleType sampleType) noexcept;
[[nodiscard]] an::NumericDomain engineNumericDomain(FixtureDomain domain) noexcept;
[[nodiscard]] std::optional<FixtureDomain> fixtureDomainOf(an::NumericDomain domain) noexcept;
[[nodiscard]] an::CfaPattern engineCfaPattern(FixtureCfa cfa) noexcept;
[[nodiscard]] std::optional<FixtureCfa> fixtureCfaOf(an::CfaPattern pattern) noexcept;
[[nodiscard]] an::ChannelSchema engineChannelSchema(FixtureChannels channels) noexcept;

/// A description as the engine states one: its extent, representation, channels and meaning, its filter array at
/// phase zero, the top-left origin and square pixels, with no colour basis claimed.
[[nodiscard]] an::Result<an::ImageDescriptor> describeEngineImage(const an::EngineContext& engine,
                                                                  const FixtureImage& image);

/// A planar image holding a description's samples exactly.
[[nodiscard]] an::Result<EngineImage> makeEngineImage(const an::EngineContext& engine, const FixtureImage& image,
                                                      const FixtureSamples& samples);

/// A description's samples as a new asset of a session.
[[nodiscard]] an::Result<an::consumer::ImageAsset>
makeImageAsset(const an::consumer::EngineSession& session, const FixtureImage& image, const FixtureSamples& samples);

/// An image's samples, top row first. Empty, with the reason in `error`, for an image that holds nothing or whose
/// channels the neutral layer does not describe.
[[nodiscard]] std::optional<FixtureSamples> readImageSamples(const EngineImage& image, std::string& error);

/// An asset's samples, read under a read lease. Empty, with the reason in `error`, as `readImageSamples` answers.
[[nodiscard]] std::optional<FixtureSamples> readAssetSamples(const an::consumer::ImageAsset& asset, std::string& error);

/// What decoding a file produced, in neutral terms: the samples top row first, and what the decode said they mean.
struct DecodedFile
{
    FixtureSamples samples;
    FixtureCfa cfa = FixtureCfa::None;
    FixtureDomain domain = FixtureDomain::NormalizedLinear;
};

/// A file decoded through the engine's codec facade in an operation the session admits, turned top-down when the
/// file stores its rows bottom-up.
[[nodiscard]] an::Result<DecodedFile> decodeEngineFile(const an::consumer::EngineSession& session,
                                                       const std::filesystem::path& path);

/// A description's samples written as FITS through the engine's output transaction in an operation the session
/// admits, in their own representation and with the description's cards.
[[nodiscard]] an::Status writeEngineFits(const an::consumer::EngineSession& session, const FixtureImage& image,
                                         const FixtureSamples& samples, const std::filesystem::path& path);

} // namespace an::tests::fixtures::engine
