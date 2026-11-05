// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - reading a drizzle source pixel: the typed readers of the eight resident
// representations, the five validity rules, and the gather that turns one pixel into scaled samples
// and effective weights.
//
// Samples are widened to double without rescaling, so the terms keep the source representation's
// units. The effective weight is `((pixelWeight * flatWeight) * weightScale) * channelWeight *
// frameWeight`, multiplied in that order; a zero pixel weight excludes the sample rather than
// failing, but the auxiliary weights are checked even where validity excludes the sample, so an
// invalid weight plane cannot hide behind the mask.

#include "algorithms/drizzle/DrizzleAccumulationInternal.h"

#include <cmath>

namespace an::drizzle_detail
{
namespace
{

template <typename T> double readValue(const std::byte* bytes) noexcept
{
    return static_cast<double>(*reinterpret_cast<const T*>(bytes));
}

bool selected(const DrizzleModel& model, const DrizzleInputBand& input, double value, std::uint32_t x, std::uint32_t y,
              std::uint32_t channel) noexcept
{
    const auto& validity = model.request().validity;
    switch (validity.rule())
    {
    case ValidityRule::AllValid:
        return true;
    case ValidityRule::FiniteOnly:
        return std::isfinite(value);
    case ValidityRule::ZeroIsNoData:
        return value != 0.0;
    case ValidityRule::DomainRange:
        return value >= *validity.lowestValid() && value <= *validity.highestValid();
    case ValidityRule::ExplicitMask: {
        const auto& mask = *input.mask;
        const auto offset = channel * mask.planeStrideBytes() + y * mask.rowStrideBytes() + x / 8u;
        return (mask.data()[offset] & (1u << (x % 8u))) != 0u;
    }
    }
    return false;
}

} // namespace

Status accumulationFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code,
        StatusDetail{.message = message, .operation = "an::accumulateDrizzleBand", .domain = ErrorDomain::Algorithms});
}

SampleReader readerFor(const DrizzleSampleBand* band) noexcept
{
    if (band == nullptr)
        return {};
    switch (band->sampleType)
    {
    case SampleType::UInt8:
        return {band, readValue<std::uint8_t>};
    case SampleType::Int8:
        return {band, readValue<std::int8_t>};
    case SampleType::UInt16:
        return {band, readValue<std::uint16_t>};
    case SampleType::Int16:
        return {band, readValue<std::int16_t>};
    case SampleType::UInt32:
        return {band, readValue<std::uint32_t>};
    case SampleType::Int32:
        return {band, readValue<std::int32_t>};
    case SampleType::Float32:
        return {band, readValue<float>};
    case SampleType::Float64:
        return {band, readValue<double>};
    }
    return {};
}

double SampleReader::at(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const noexcept
{
    if (band == nullptr)
        return 1.0;
    const auto& layout = band->layout;
    if (layout.extent().channelCount() == 1u)
        channel = 0u;
    const auto offset =
        (channel / layout.samplesPerPixel()) * layout.planeStrideBytes() + y * layout.rowStrideBytes() +
        (static_cast<std::uint64_t>(x) * layout.samplesPerPixel() + channel % layout.samplesPerPixel()) *
            layout.elementSizeBytes();
    return read(band->samples.data() + offset);
}

Status gatherDrizzleValues(const DrizzleModel& model, const DrizzleInputBand& input, const DrizzleReaders& readers,
                           std::uint32_t x, std::uint32_t y, DrizzleValues& values, DrizzleAccumulationReport* report)
{
    const auto& request = model.request();
    const auto& parameters = request.parameters;
    values = {};
    values.firstChannel =
        model.usesCfa() ? static_cast<std::uint32_t>(request.input.cfa().colorAt(x, y + input.firstRow)) : 0u;
    values.channelCount = request.input.extent().channelCount();
    for (std::uint32_t channel = 0u; channel < values.channelCount; ++channel)
    {
        const auto target = model.usesCfa() ? values.firstChannel : channel;
        const double pixel = readers.pixels.at(x, y, channel);
        const double weight = readers.weights.at(x, y, channel);
        const double flat = readers.flats.at(x, y, channel);
        if (!std::isfinite(weight) || weight < 0.0 || !std::isfinite(flat) || flat <= 0.0)
            return accumulationFailure(ErrorCode::InvalidData,
                                       "pixel weights must be nonnegative and flats positive, all finite");
        if (!selected(model, input, pixel, x, y, channel))
        {
            if (report != nullptr)
                ++report->excluded;
            continue;
        }
        if (!std::isfinite(pixel))
        {
            if (parameters.nonFinite == DrizzleNonFinitePolicy::Reject)
                return accumulationFailure(ErrorCode::InvalidData, "source validity admitted a non-finite sample");
            if (report != nullptr)
            {
                ++report->excluded;
                ++report->nonFinite;
            }
            continue;
        }
        const double channelWeight = target < 3u ? parameters.channelWeights[target] : 1.0;
        if (weight == 0.0)
        {
            if (report != nullptr)
                ++report->excluded;
            continue;
        }
        const double effective =
            ((weight * flat) * parameters.weightScale) * channelWeight * input.frameWeights[target];
        const double scaled = pixel / (parameters.scale * parameters.scale);
        if (!std::isfinite(effective) || effective <= 0.0 || !std::isfinite(scaled * effective))
            return accumulationFailure(ErrorCode::InvalidData,
                                       "effective weight or weighted flux is not representable");
        values.samples[target] = scaled;
        values.weights[target] = effective;
        if (report != nullptr)
            ++report->selected;
    }
    return Status::success();
}

} // namespace an::drizzle_detail
