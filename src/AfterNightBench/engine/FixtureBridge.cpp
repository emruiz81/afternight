// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "FixtureBridge.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/GeometryError.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/DescriptorError.h>

#include <cstddef>
#include <utility>

// One translation, written once. The rules it applies are all refusals rather than adjustments, and
// that is the decision worth stating: a benchmark bridge that repaired what it was handed - by
// tightening a stride, by narrowing a 64-bit sample, by swapping bytes in place - would produce a
// measurement of something the scenario did not describe and report it under the scenario's name.
//
// So everything here either reads the fixture exactly as its descriptor states it, or names the
// reason it cannot.

namespace an::bench::engine
{
namespace
{

/// The bytes a descriptor's own strides imply, or zero when the arithmetic does not exist.
///
/// `SampleDescriptor::totalBytes` already answers this and answers it the same way, so this is the
/// bench layer's number rather than a second opinion.
///
/// @param descriptor the fixture description.
/// @return the byte count, or zero when it overflows.
[[nodiscard]] std::uint64_t declaredBytes(const SampleDescriptor& descriptor) noexcept
{
    return descriptor.totalBytes();
}

} // namespace

FixtureSemantics defaultFixtureSemantics(const SampleDescriptor& descriptor) noexcept
{
    an::ChannelSchema schema = an::ChannelSchema::Named;
    if (descriptor.channels == 1u)
    {
        schema = an::ChannelSchema::Mono;
    }
    else if (descriptor.channels == 3u)
    {
        schema = an::ChannelSchema::Rgb;
    }

    return FixtureSemantics{.channelSchema = schema,
                            .colorSpace = an::ColorSpace::Unspecified,
                            .numericDomain = an::NumericDomain::NormalizedLinear,
                            .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                            .pixelAspectRatio = 1.0};
}

std::optional<an::SampleType> toEngineSampleType(SampleType type) noexcept
{
    switch (type)
    {
    case SampleType::UInt8:
        return an::SampleType::UInt8;
    case SampleType::Int8:
        return an::SampleType::Int8;
    case SampleType::UInt16:
        return an::SampleType::UInt16;
    case SampleType::Int16:
        return an::SampleType::Int16;
    case SampleType::UInt32:
        return an::SampleType::UInt32;
    case SampleType::Int32:
        return an::SampleType::Int32;
    case SampleType::Float32:
        return an::SampleType::Float32;
    case SampleType::Float64:
        return an::SampleType::Float64;
    case SampleType::Int64:
    case SampleType::UInt64:
        // Describable at a boundary and not stored by this engine. Refused rather than narrowed.
        return std::nullopt;
    }
    return std::nullopt;
}

SampleType toBenchSampleType(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::UInt8:
        return SampleType::UInt8;
    case an::SampleType::Int8:
        return SampleType::Int8;
    case an::SampleType::UInt16:
        return SampleType::UInt16;
    case an::SampleType::Int16:
        return SampleType::Int16;
    case an::SampleType::UInt32:
        return SampleType::UInt32;
    case an::SampleType::Int32:
        return SampleType::Int32;
    case an::SampleType::Float32:
        return SampleType::Float32;
    case an::SampleType::Float64:
        return SampleType::Float64;
    }
    return SampleType::Float32;
}

std::optional<an::StorageArrangement> toEngineArrangement(Layout layout) noexcept
{
    switch (layout)
    {
    case Layout::Planar:
        return an::StorageArrangement::Planar;
    case Layout::Interleaved:
        return an::StorageArrangement::Interleaved;
    case Layout::PackedCfa:
        // A mosaiced sensor frame is one plane of one channel. What makes it a mosaic is the
        // descriptor's filter array, not the arrangement of its planes.
        return an::StorageArrangement::Planar;
    case Layout::SemiPlanar:
        return std::nullopt;
    }
    return std::nullopt;
}

an::ByteOrder toEngineByteOrder(ByteOrder order) noexcept
{
    switch (order)
    {
    case ByteOrder::Little:
        return an::ByteOrder::Little;
    case ByteOrder::Big:
        return an::ByteOrder::Big;
    case ByteOrder::Native:
        return an::nativeByteOrder();
    }
    return an::nativeByteOrder();
}

std::optional<an::CfaPattern> toEngineCfaPattern(const std::string& pattern) noexcept
{
    if (pattern.empty())
    {
        return an::CfaPattern::None;
    }
    if (pattern == "RGGB")
    {
        return an::CfaPattern::Rggb;
    }
    if (pattern == "BGGR")
    {
        return an::CfaPattern::Bggr;
    }
    if (pattern == "GRBG")
    {
        return an::CfaPattern::Grbg;
    }
    if (pattern == "GBRG")
    {
        return an::CfaPattern::Gbrg;
    }
    return std::nullopt;
}

FixtureBridgeFailure checkFixture(const SampleDescriptor& descriptor, const FixtureSemantics& semantics,
                                  const an::EngineLimits& limits, std::uint64_t payloadBytes) noexcept
{
    if (!descriptor.isValid())
    {
        return FixtureBridgeFailure::DescriptorNotUsable;
    }

    const std::optional<an::SampleType> sampleType = toEngineSampleType(descriptor.sampleType);
    if (!sampleType.has_value())
    {
        return FixtureBridgeFailure::SampleTypeNotResident;
    }

    const std::optional<an::StorageArrangement> arrangement = toEngineArrangement(descriptor.layout);
    if (!arrangement.has_value())
    {
        return FixtureBridgeFailure::ArrangementNotSupported;
    }

    // A multi-byte sample in the other order is real data that this bridge will not read in place.
    // The single-byte types have one order, so asking about theirs would produce two descriptions
    // of identical bytes that compare unequal.
    if (an::isByteOrderMeaningful(*sampleType) && toEngineByteOrder(descriptor.byteOrder) != an::nativeByteOrder())
    {
        return FixtureBridgeFailure::ByteOrderNotNative;
    }

    const std::optional<an::CfaPattern> cfa = toEngineCfaPattern(descriptor.cfaPattern);
    if (!cfa.has_value())
    {
        return FixtureBridgeFailure::CfaPatternNotSupported;
    }

    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(descriptor.width, descriptor.height, descriptor.channels, limits);
    if (!extent.has_value())
    {
        return FixtureBridgeFailure::ExtentRefused;
    }

    const std::optional<an::CfaDescriptor> cfaDescriptor = an::CfaDescriptor::create(*cfa, 0u, 0u);
    if (!cfaDescriptor.has_value())
    {
        return FixtureBridgeFailure::CfaPatternNotSupported;
    }

    const an::ImageDescriptorRequest descriptorRequest{.extent = *extent,
                                                       .sampleType = *sampleType,
                                                       .channelSchema = semantics.channelSchema,
                                                       .colorSpace = semantics.colorSpace,
                                                       .numericDomain = semantics.numericDomain,
                                                       .cfa = *cfaDescriptor,
                                                       .coordinateOrigin = semantics.coordinateOrigin,
                                                       .pixelAspectRatio = semantics.pixelAspectRatio};
    const std::optional<an::ImageDescriptor> engineDescriptor = an::ImageDescriptor::create(descriptorRequest);
    if (!engineDescriptor.has_value())
    {
        return FixtureBridgeFailure::MeaningRefused;
    }

    const std::optional<an::ImageLayoutRequest> layoutRequest =
        an::layoutRequestFor(*engineDescriptor, *arrangement, 1u);
    if (!layoutRequest.has_value())
    {
        return FixtureBridgeFailure::ArrangementNotSupported;
    }

    // Zero means tightly packed in the benchmark vocabulary, and the engine has no such spelling:
    // a layout carries the stride it was built with. So the derived one is computed here from the
    // same numbers `SampleDescriptor::rowBytes` uses.
    const std::uint64_t rowStrideBytes = descriptor.rowStrideBytes != 0u
                                             ? descriptor.rowStrideBytes
                                             : static_cast<std::uint64_t>(descriptor.width) *
                                                   layoutRequest->samplesPerPixel * layoutRequest->elementSizeBytes;
    const std::uint64_t planeStrideBytes = descriptor.planeStrideBytes != 0u
                                               ? descriptor.planeStrideBytes
                                               : rowStrideBytes * static_cast<std::uint64_t>(descriptor.height);

    const an::GeometryError strideError =
        an::ImageLayout::checkWithStrides(*layoutRequest, rowStrideBytes, planeStrideBytes, limits);
    if (strideError != an::GeometryError::None)
    {
        return strideError == an::GeometryError::ImageTooLarge || strideError == an::GeometryError::SizeNotRepresentable
                   ? FixtureBridgeFailure::ExtentRefused
                   : FixtureBridgeFailure::StrideRefused;
    }

    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(*layoutRequest, rowStrideBytes, planeStrideBytes, limits);
    if (!layout.has_value())
    {
        return FixtureBridgeFailure::StrideRefused;
    }
    if (payloadBytes < layout->totalBytes() || payloadBytes < declaredBytes(descriptor))
    {
        return FixtureBridgeFailure::PayloadTooSmall;
    }
    return FixtureBridgeFailure::None;
}

std::optional<BridgedFixture> bridgeFixture(const SampleDescriptor& descriptor, const FixtureSemantics& semantics,
                                            const an::EngineLimits& limits, std::uint64_t payloadBytes) noexcept
{
    if (checkFixture(descriptor, semantics, limits, payloadBytes) != FixtureBridgeFailure::None)
    {
        return std::nullopt;
    }

    const std::optional<an::SampleType> sampleType = toEngineSampleType(descriptor.sampleType);
    const std::optional<an::StorageArrangement> arrangement = toEngineArrangement(descriptor.layout);
    const std::optional<an::CfaPattern> cfa = toEngineCfaPattern(descriptor.cfaPattern);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(descriptor.width, descriptor.height, descriptor.channels, limits);
    const std::optional<an::CfaDescriptor> cfaDescriptor =
        cfa.has_value() ? an::CfaDescriptor::create(*cfa, 0u, 0u) : std::nullopt;
    if (!sampleType.has_value() || !arrangement.has_value() || !extent.has_value() || !cfaDescriptor.has_value())
    {
        return std::nullopt;
    }

    const an::ImageDescriptorRequest descriptorRequest{.extent = *extent,
                                                       .sampleType = *sampleType,
                                                       .channelSchema = semantics.channelSchema,
                                                       .colorSpace = semantics.colorSpace,
                                                       .numericDomain = semantics.numericDomain,
                                                       .cfa = *cfaDescriptor,
                                                       .coordinateOrigin = semantics.coordinateOrigin,
                                                       .pixelAspectRatio = semantics.pixelAspectRatio};
    const std::optional<an::ImageDescriptor> engineDescriptor = an::ImageDescriptor::create(descriptorRequest);
    if (!engineDescriptor.has_value())
    {
        return std::nullopt;
    }

    const std::optional<an::ImageLayoutRequest> layoutRequest =
        an::layoutRequestFor(*engineDescriptor, *arrangement, 1u);
    if (!layoutRequest.has_value())
    {
        return std::nullopt;
    }

    const std::uint64_t rowStrideBytes = descriptor.rowStrideBytes != 0u
                                             ? descriptor.rowStrideBytes
                                             : static_cast<std::uint64_t>(descriptor.width) *
                                                   layoutRequest->samplesPerPixel * layoutRequest->elementSizeBytes;
    const std::uint64_t planeStrideBytes = descriptor.planeStrideBytes != 0u
                                               ? descriptor.planeStrideBytes
                                               : rowStrideBytes * static_cast<std::uint64_t>(descriptor.height);

    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(*layoutRequest, rowStrideBytes, planeStrideBytes, limits);
    if (!layout.has_value())
    {
        return std::nullopt;
    }
    return BridgedFixture{.layout = *layout, .descriptor = *engineDescriptor, .arrangement = *arrangement};
}

const char* fixtureBridgeFailureName(FixtureBridgeFailure failure) noexcept
{
    switch (failure)
    {
    case FixtureBridgeFailure::None:
        return "None";
    case FixtureBridgeFailure::DescriptorNotUsable:
        return "DescriptorNotUsable";
    case FixtureBridgeFailure::SampleTypeNotResident:
        return "SampleTypeNotResident";
    case FixtureBridgeFailure::ArrangementNotSupported:
        return "ArrangementNotSupported";
    case FixtureBridgeFailure::ByteOrderNotNative:
        return "ByteOrderNotNative";
    case FixtureBridgeFailure::CfaPatternNotSupported:
        return "CfaPatternNotSupported";
    case FixtureBridgeFailure::ExtentRefused:
        return "ExtentRefused";
    case FixtureBridgeFailure::StrideRefused:
        return "StrideRefused";
    case FixtureBridgeFailure::MeaningRefused:
        return "MeaningRefused";
    case FixtureBridgeFailure::PayloadTooSmall:
        return "PayloadTooSmall";
    }
    return "UnknownFixtureBridgeFailure";
}

std::string describeFixtureBridgeFailure(FixtureBridgeFailure failure, const SampleDescriptor& descriptor)
{
    std::string sentence = "AfterNightEngine cannot read this fixture (";
    sentence += fixtureBridgeFailureName(failure);
    sentence += "): ";
    sentence += std::to_string(descriptor.width);
    sentence += 'x';
    sentence += std::to_string(descriptor.height);
    sentence += 'x';
    sentence += std::to_string(descriptor.channels);
    sentence += ' ';
    sentence += toString(descriptor.sampleType);
    sentence += ' ';
    sentence += toString(descriptor.layout);
    sentence += ' ';
    sentence += toString(descriptor.byteOrder);
    return sentence;
}

BackendId toBenchBackend(const an::Result<an::BackendSelection>& selection) noexcept
{
    if (!selection.hasValue())
    {
        return BackendId::Unspecified;
    }
    switch (selection->backend)
    {
    case an::ComputeBackend::Reference:
        return BackendId::Scalar;
    case an::ComputeBackend::Gpu:
        return BackendId::Gpu;
    case an::ComputeBackend::Cpu:
        switch (selection->instructionSet)
        {
        case an::InstructionSet::Scalar:
            return BackendId::Scalar;
        case an::InstructionSet::Avx2:
            return BackendId::Avx2;
        case an::InstructionSet::Avx512Skylake:
            return BackendId::Avx512;
        case an::InstructionSet::Sse2:
            return BackendId::Sse2;
        case an::InstructionSet::Sse42:
            return BackendId::Sse42;
        case an::InstructionSet::Avx:
            return BackendId::Avx;
        case an::InstructionSet::Neon:
            return BackendId::Neon;
        case an::InstructionSet::Automatic:
            return BackendId::Unspecified;
        }
        return BackendId::Unspecified;
    case an::ComputeBackend::Automatic:
        // An answer, not a request: the dispatch table never reports this, and a runner that saw it
        // would be reporting a selection nobody made.
        return BackendId::Unspecified;
    }
    return BackendId::Unspecified;
}

namespace
{

/// What `describeEngineFailure` last described on this thread, until it is taken.
thread_local std::optional<DescribedEngineFailure> lastDescribedFailure;

} // namespace

std::string describeEngineFailure(const std::string& what, const an::Status& status)
{
    DescribedEngineFailure described{.code = status.code(), .cancelled = false};
    for (an::Status current = status; current.failed(); current = current.cause())
    {
        described.cancelled = described.cancelled || current.code() == an::ErrorCode::Cancelled;
    }
    lastDescribedFailure = described;

    std::string sentence = what;
    sentence += ": ";
    sentence += an::errorCodeName(status.code());
    const char* const message = status.message();
    if (message != nullptr && message[0] != '\0')
    {
        sentence += " - ";
        sentence += message;
    }
    for (an::Status current = status; current.failed(); current = current.cause())
    {
        if (const auto* code = current.findContext(an::ContextKey::LibraryCode);
            code != nullptr && code->kind == an::ContextValueKind::Number)
        {
            sentence += " [LibraryCode=" + std::to_string(code->number) + "]";
            break;
        }
    }
    return sentence;
}

std::optional<DescribedEngineFailure> takeLastDescribedEngineFailure() noexcept
{
    return std::exchange(lastDescribedFailure, std::nullopt);
}

} // namespace an::bench::engine
