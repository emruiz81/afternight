// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The pointwise and tone processes' shared work: samples in fractions of full scale for an engine operation to work
/// on in place, and the nonlinear record a result keeps.

#include "stdafx.hpp"

#include "processes/PointwiseProcessUtils.h"

#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseCanonicalization.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/ImageTransfer.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/TaskScheduler.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace process_utils
{

namespace
{

/// A failure this unit decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// Whether a representation is a signed integer, which is counted from its smallest value.
[[nodiscard]] constexpr bool isSignedInteger(an::SampleType type) noexcept
{
    return type == an::SampleType::Int8 || type == an::SampleType::Int16 || type == an::SampleType::Int32;
}

/// Whether a representation is floating, which is read as it is.
[[nodiscard]] constexpr bool isFloating(an::SampleType type) noexcept
{
    return type == an::SampleType::Float32 || type == an::SampleType::Float64;
}

/// A signed integer's smallest value and the width of its range, from its smallest value to its largest.
struct SignedRange
{
    double smallest = 0.0;
    double width = 1.0;
};

[[nodiscard]] SignedRange signedRangeOf(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return SignedRange{.smallest = -128.0, .width = 255.0};
    case an::SampleType::Int16:
        return SignedRange{.smallest = -32768.0, .width = 65535.0};
    case an::SampleType::Int32:
        return SignedRange{.smallest = -2147483648.0, .width = 4294967295.0};
    default:
        return SignedRange{};
    }
}

/// Runs a scale and a bias over planar working samples in place, every sample of every channel.
[[nodiscard]] an::Status scaleInPlace(const an::ExecutionContext& execution, an::SampleType working, void* samples,
                                      const an::ImageLayout& layout, double scale, double bias)
{
    const an::Result<an::PointwiseProgram> program =
        an::buildScaleBiasProgram(normalizedPlanShape(working, layout.extent().channelCount()), scale, bias);
    if (!program.hasValue())
    {
        return program.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{an::PointwiseImageView{.samples = samples, .layout = &layout}};
    const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
        execution, *program, sources, an::PointwiseMutableImageView{.samples = samples, .layout = &layout},
        an::KernelValidityView{});
    return ran.hasValue() ? an::Status::success() : ran.status();
}

/// The description an image holds, or nothing.
[[nodiscard]] std::optional<an::ImageDescriptor> descriptorOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) { return typed.descriptor(); }, image);
}

/// The metadata an image holds.
[[nodiscard]] const an::ImageMetadata& metadataOf(const an::consumer::AnyImage& image)
{
    return std::visit([](const auto& typed) -> const an::ImageMetadata& { return typed.metadata(); }, image);
}

} // namespace

an::SampleType normalizedWorkingType(an::SampleType stored) noexcept
{
    switch (stored)
    {
    case an::SampleType::Float64:
    case an::SampleType::Int32:
    case an::SampleType::UInt32:
        return an::SampleType::Float64;
    default:
        return an::SampleType::Float32;
    }
}

an::PointwisePlanShape normalizedPlanShape(an::SampleType working, std::uint32_t channels) noexcept
{
    an::PointwisePlanShape shape;
    shape.sampleType = working;
    shape.numericDomain = an::NumericDomain::NormalizedLinear;
    shape.channelCount = channels;
    if (working == an::SampleType::Float64)
    {
        shape.precision = an::ComputePrecision::Float64;
        shape.precisionJustification = an::Float64Justification::StoredInFloat64;
    }
    return shape;
}

an::TonePlanShape normalizedToneShape(an::SampleType representation, std::uint32_t channels) noexcept
{
    an::TonePlanShape shape;
    const bool table = representation == an::SampleType::UInt8 || representation == an::SampleType::UInt16;
    shape.sampleType = table ? representation : normalizedWorkingType(representation);
    shape.inputDomain = an::NumericDomain::NormalizedLinear;
    shape.outputDomain = an::NumericDomain::EncodedNonlinear;
    shape.channelCount = channels;
    if (shape.sampleType == an::SampleType::Float64)
    {
        shape.precision = an::ComputePrecision::Float64;
        shape.precisionJustification = an::Float64Justification::StoredInFloat64;
    }
    return shape;
}

an::KernelValidityView skippingNonFinite(const an::KernelValidityView& validity) noexcept
{
    an::KernelValidityView skipping = validity;
    if (validity.descriptor.rule() == an::ValidityRule::AllValid)
    {
        const std::optional<an::ValidityDescriptor> finite =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
        if (finite.has_value())
        {
            skipping.descriptor = *finite;
        }
    }
    return skipping;
}

an::Result<an::ValidityMask> judgeValidAndFinite(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                                 an::SampleType type, const void* samples,
                                                 const an::ImageLayout& layout, const an::KernelValidityView& validity)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, engine.limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the mask of the samples cannot be described");
    }
    const std::optional<an::ValidityDescriptor> finiteOnly =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    if (!finiteOnly.has_value())
    {
        return refusal(an::ErrorCode::Internal, "a rule about finiteness cannot be stated");
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *extent);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> writable = mask->view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> marked = std::as_const(*mask).view();
    if (!writable.has_value() || !marked.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the mask of the samples holds nothing");
    }
    const an::Status finite = an::markImageValidity(execution, type, samples, layout, *finiteOnly, *writable);
    if (finite.failed())
    {
        return finite;
    }

    // What the samples' own rule admits: the plane of their mask that the rule names, or the value rule judged.
    std::optional<an::ValidityMask> judgedRule;
    std::optional<an::ValidityMaskView<const std::uint8_t>> admitted;
    if (validity.mask != nullptr)
    {
        const an::ValidityMaskView<const std::uint8_t>& bits = *validity.mask;
        const std::uint64_t offset = static_cast<std::uint64_t>(validity.maskPlane) * bits.planeStrideBytes();
        const std::optional<an::ImageExtent> plane =
            an::ImageExtent::create(bits.width(), bits.height(), 1u, engine.limits());
        if (!plane.has_value() || validity.maskPlane >= bits.planeCount())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the samples' mask has no plane the rule names");
        }
        admitted = an::ValidityMaskView<const std::uint8_t>::create(
            std::span<const std::uint8_t>(bits.data() + offset, bits.spannedByteCount() - offset), *plane,
            bits.rowStrideBytes(), bits.planeStrideBytes());
    }
    else
    {
        an::Result<an::ValidityMask> ruled = an::ValidityMask::create(engine, *extent);
        if (!ruled.hasValue())
        {
            return ruled.status();
        }
        const std::optional<an::ValidityMaskView<std::uint8_t>> ruledBits = ruled->view();
        if (!ruledBits.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mask of the samples holds nothing");
        }
        const an::Status byRule =
            an::markImageValidity(execution, type, samples, layout, validity.descriptor, *ruledBits);
        if (byRule.failed())
        {
            return byRule;
        }
        judgedRule.emplace(std::move(*ruled));
        admitted = std::as_const(*judgedRule).view();
    }
    if (!admitted.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the samples' rule cannot be read as a mask");
    }
    const an::Status combined =
        an::combineImageMasks(execution, *writable, *marked, *admitted, an::ValidityCombination::Intersection);
    if (combined.failed())
    {
        return combined;
    }
    return std::move(*mask);
}

// ---- NormalizedSamples.

struct NormalizedSamples::State
{
    /// What the samples were opened over.
    an::SampleType stored = an::SampleType::Float32;
    void* storedSamples = nullptr;
    an::ImageLayout storedLayout;

    /// The working image an integer is converted into; none for floating samples.
    std::variant<std::monostate, an::Image<float>, an::Image<double>> working;

    /// The working samples and where they sit.
    an::SampleType workingType = an::SampleType::Float32;
    void* workingSamples = nullptr;
    an::ImageLayout workingLayout;

    /// The rule the work runs under, and the mask it reads: a view of the samples' own, or of one a value rule was
    /// judged into, which the state keeps.
    an::ValidityDescriptor rule;
    std::optional<an::ValidityMask> judged;
    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    std::uint32_t maskPlane = 0u;

    /// The engine context a mask is taken from, and the mask the rule and the samples' finiteness were judged into
    /// together, once an operation that refuses a sample holding no number asked for it.
    const an::EngineContext* engine = nullptr;
    std::optional<an::ValidityMask> finite;
    std::optional<an::ValidityMaskView<const std::uint8_t>> finiteView;

    explicit State(const an::ImageLayout& layout) : storedLayout(layout), workingLayout(layout) {}
};

NormalizedSamples::NormalizedSamples() noexcept = default;

NormalizedSamples::~NormalizedSamples() = default;

NormalizedSamples::NormalizedSamples(NormalizedSamples&& other) noexcept = default;

NormalizedSamples& NormalizedSamples::operator=(NormalizedSamples&& other) noexcept = default;

NormalizedSamples::NormalizedSamples(std::unique_ptr<State> state) noexcept : m_state(std::move(state)) {}

an::Result<NormalizedSamples> NormalizedSamples::open(const an::EngineContext& engine,
                                                      const an::ExecutionContext& execution,
                                                      const an::ImageDescriptor& described, void* samples,
                                                      const an::ImageLayout& layout,
                                                      const an::KernelValidityView& validity)
{
    return open(engine, execution, described, samples, layout, samples, layout, validity);
}

an::Result<NormalizedSamples>
NormalizedSamples::open(const an::EngineContext& engine, const an::ExecutionContext& execution,
                        const an::ImageDescriptor& described, const void* source, const an::ImageLayout& sourceLayout,
                        void* samples, const an::ImageLayout& layout, const an::KernelValidityView& validity)
{
    if (source == nullptr || samples == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, "there are no samples to work on");
    }
    if (layout.samplesPerPixel() != 1u || sourceLayout.samplesPerPixel() != 1u)
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the samples are not planar");
    }
    if (!(sourceLayout.extent() == layout.extent()))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the samples read and written have different extents");
    }
    if (source != samples && isFloating(described.sampleType()))
    {
        // Floating samples are worked on where the result goes, so they are put there first.
        const an::Status copied =
            an::repackImageSamples(execution, described.sampleType(), source, sourceLayout, samples, layout);
        if (copied.failed())
        {
            return copied;
        }
        source = samples;
    }
    auto state = std::make_unique<State>(layout);
    state->engine = &engine;
    state->stored = described.sampleType();
    state->storedSamples = samples;
    state->rule = validity.descriptor;
    if (validity.mask != nullptr)
    {
        state->maskView.emplace(*validity.mask);
    }
    state->maskPlane = validity.maskPlane;

    if (isFloating(state->stored))
    {
        // Floating samples are fractions of full scale already, and are worked on where they are.
        state->workingType = state->stored;
        state->workingSamples = samples;
        return NormalizedSamples(std::move(state));
    }

    // A rule an integer's value decides is judged on the stored samples, before they are converted, and carried as a
    // mask; an integer is always finite, so a rule about finiteness admits every sample, as the all-valid rule does.
    // Neither is judged: a mask of every pixel would only send the work down the executor's pixel-by-pixel path.
    const an::ValidityRule rule = validity.descriptor.rule();
    if (an::validityRuleIsDecidedByValue(rule) && rule != an::ValidityRule::FiniteOnly &&
        rule != an::ValidityRule::AllValid)
    {
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(layout.extent().width(), layout.extent().height(), 1u, engine.limits());
        if (!extent.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the mask of the samples cannot be described");
        }
        an::Result<an::ValidityMask> mask = an::ValidityMask::create(engine, *extent);
        if (!mask.hasValue())
        {
            return mask.status();
        }
        std::optional<an::ValidityMaskView<std::uint8_t>> writable = mask->view();
        if (!writable.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mask of the samples holds nothing");
        }
        const an::Status marked =
            an::markImageValidity(execution, state->stored, source, sourceLayout, validity.descriptor, *writable);
        if (marked.failed())
        {
            return marked;
        }
        const std::optional<an::ValidityDescriptor> explicitMask =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
        if (!explicitMask.has_value())
        {
            return refusal(an::ErrorCode::Internal, "an explicit mask rule cannot be stated");
        }
        state->judged.emplace(std::move(*mask));
        state->maskView = std::as_const(*state->judged).view();
        state->rule = *explicitMask;
        state->maskPlane = 0u;
    }
    else if (!an::validityRuleNeedsMask(validity.descriptor.rule()))
    {
        state->rule = an::ValidityDescriptor::allValid();
    }

    state->workingType = normalizedWorkingType(state->stored);
    const an::Result<an::ImageDescriptor> workingDescribed = an::describeSamplesAs(described, state->workingType);
    if (!workingDescribed.hasValue())
    {
        return workingDescribed.status();
    }
    an::Status made = an::Status::success();
    auto create = [&]<typename WorkT>() {
        an::Result<an::Image<WorkT>> created =
            an::Image<WorkT>::create(engine, *workingDescribed, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, an::ImageMetadata{});
        if (!created.hasValue())
        {
            made = created.status();
            return;
        }
        // The samples are written through the image the state keeps, which owns them while the work runs.
        an::Image<WorkT>& image = state->working.template emplace<an::Image<WorkT>>(std::move(*created));
        const std::optional<an::ImageWriteAccess<WorkT>> access = image.write();
        if (!access.has_value())
        {
            made = refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
            return;
        }
        state->workingSamples = access->view().data();
        state->workingLayout = access->view().layout();
    };
    if (state->workingType == an::SampleType::Float64)
    {
        create.template operator()<double>();
    }
    else
    {
        create.template operator()<float>();
    }
    if (made.failed())
    {
        return made;
    }

    // An unsigned integer is a fraction of its largest value, which the conversion at full scale gives; a signed one is
    // counted from its smallest value, which its own value, carried across, scaled and shifted gives.
    const bool countedFromSmallest = isSignedInteger(state->stored);
    an::ConversionCounters counters;
    const an::Status converted = an::convertImageSamples(
        execution, state->stored, source, sourceLayout, state->workingType, state->workingSamples, state->workingLayout,
        an::ConversionPolicy{.domain =
                                 countedFromSmallest ? an::DomainMapping::CodeValue : an::DomainMapping::FullScale},
        counters);
    if (converted.failed())
    {
        return converted;
    }
    if (countedFromSmallest)
    {
        const SignedRange range = signedRangeOf(state->stored);
        const an::Status scaled = scaleInPlace(execution, state->workingType, state->workingSamples,
                                               state->workingLayout, 1.0 / range.width, -range.smallest / range.width);
        if (scaled.failed())
        {
            return scaled;
        }
    }
    return NormalizedSamples(std::move(state));
}

bool NormalizedSamples::valid() const noexcept
{
    return m_state != nullptr;
}

an::SampleType NormalizedSamples::sampleType() const noexcept
{
    return m_state->workingType;
}

void* NormalizedSamples::samples() const noexcept
{
    return m_state->workingSamples;
}

const an::ImageLayout& NormalizedSamples::layout() const noexcept
{
    return m_state->workingLayout;
}

an::Result<an::KernelValidityView> NormalizedSamples::skippingNonFinite(const an::ExecutionContext& execution)
{
    const an::KernelValidityView own = validity();
    const an::ValidityRule rule = own.descriptor.rule();
    if (rule == an::ValidityRule::AllValid)
    {
        return process_utils::skippingNonFinite(own);
    }
    // A rule about finiteness already counts such a sample out, and an integer is always a finite number - the
    // working copy of one is floating, so the stored representation is what says which it was.
    if (rule == an::ValidityRule::FiniteOnly || !isFloating(m_state->stored))
    {
        return own;
    }
    if (!m_state->finiteView.has_value())
    {
        an::Result<an::ValidityMask> judged = judgeValidAndFinite(*m_state->engine, execution, m_state->workingType,
                                                                  m_state->workingSamples, m_state->workingLayout, own);
        if (!judged.hasValue())
        {
            return judged.status();
        }
        m_state->finite.emplace(std::move(*judged));
        m_state->finiteView = std::as_const(*m_state->finite).view();
    }
    const std::optional<an::ValidityDescriptor> explicitMask =
        an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    if (!explicitMask.has_value())
    {
        return refusal(an::ErrorCode::Internal, "an explicit mask rule cannot be stated");
    }
    return an::KernelValidityView{.descriptor = *explicitMask, .mask = &*m_state->finiteView, .maskPlane = 0u};
}

an::KernelValidityView NormalizedSamples::validity() const noexcept
{
    an::KernelValidityView view;
    view.descriptor = m_state->rule;
    view.mask = m_state->maskView.has_value() ? &*m_state->maskView : nullptr;
    view.maskPlane = m_state->maskPlane;
    return view;
}

an::Status NormalizedSamples::commit(const an::ExecutionContext& execution)
{
    if (m_state == nullptr)
    {
        return refusal(an::ErrorCode::NotInitialized, "there are no samples to commit");
    }
    if (isFloating(m_state->stored))
    {
        return an::Status::success();
    }
    const bool countedFromSmallest = isSignedInteger(m_state->stored);
    if (countedFromSmallest)
    {
        const SignedRange range = signedRangeOf(m_state->stored);
        const an::Status scaled = scaleInPlace(execution, m_state->workingType, m_state->workingSamples,
                                               m_state->workingLayout, range.width, range.smallest);
        if (scaled.failed())
        {
            return scaled;
        }
    }
    an::ConversionCounters counters;
    return an::convertImageSamples(execution, m_state->workingType, m_state->workingSamples, m_state->workingLayout,
                                   m_state->stored, m_state->storedSamples, m_state->storedLayout,
                                   an::ConversionPolicy{.domain = countedFromSmallest ? an::DomainMapping::CodeValue
                                                                                      : an::DomainMapping::FullScale},
                                   counters);
}

an::Result<NormalizedSamples> openNormalizedDestination(const an::ui::ExecutionScope& scope,
                                                        const an::ui::ProcessSource& source,
                                                        an::ui::ProcessDestination& destination)
{
    if (!destination.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no destination");
    }
    const std::optional<an::ImageDescriptor> described = descriptorOf(destination.asset().image());
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the destination holds nothing");
    }
    const std::optional<an::ValidityMaskView<const std::uint8_t>> mask =
        source.validity() != nullptr ? source.validity()->view() : std::nullopt;
    an::KernelValidityView validity;
    validity.descriptor = source.metadata().semantics().validity();
    validity.mask = mask.has_value() ? &*mask : nullptr;
    // A destination that starts unwritten is opened over the source where it lies; one the source cannot be read into
    // that way is brought to hold it first.
    const bool planarSource = std::visit(
        [](const auto& typed) {
            const auto view = typed.view();
            return view.has_value() && view->layout().samplesPerPixel() == 1u;
        },
        source.image());
    if (!destination.holdsSource() && !planarSource)
    {
        if (an::Status held = destination.holdSource(scope.execution(), source); held.failed())
        {
            return held;
        }
    }
    return std::visit(
        [&](const auto& typed) -> an::Result<NormalizedSamples> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageWriteAccess<SampleT>> access = destination.asset().write<SampleT>();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the destination cannot be written");
            }
            if (destination.holdsSource())
            {
                return NormalizedSamples::open(scope.engine(), scope.execution(), *described, access->view().data(),
                                               access->view().layout(), validity);
            }
            const an::Image<SampleT>* read = std::get_if<an::Image<SampleT>>(&source.image());
            const auto readView = read != nullptr ? read->view() : std::nullopt;
            if (!readView.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the source holds nothing in the destination's representation");
            }
            return NormalizedSamples::open(scope.engine(), scope.execution(), *described, readView->data(),
                                           readView->layout(), access->view().data(), access->view().layout(),
                                           validity);
        },
        destination.asset().image());
}

// ---- Chains, and what a step says of its channel.

namespace
{

/// How many bytes of working samples one band holds: few enough that a band's rows, read, worked on and narrowed back,
/// stay in a core's caches between those three passes, and enough that the passes' fixed costs do not show.
constexpr std::uint64_t kBandWorkingBytes = std::uint64_t{512} * 1024u;

/// A layout of rows of a planar image, at its own strides: what a band of it is, once its first byte is found.
[[nodiscard]] an::Result<an::ImageLayout> rowsOf(const an::ImageLayout& layout, std::uint32_t rows,
                                                 const an::EngineLimits& limits)
{
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(layout.extent().width(), rows, layout.extent().channelCount(), limits);
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a band of the samples cannot be described");
    }
    const std::optional<an::ImageLayout> band =
        an::ImageLayout::createWithStrides(an::ImageLayoutRequest{.extent = *extent,
                                                                  .elementSizeBytes = layout.elementSizeBytes(),
                                                                  .samplesPerPixel = 1u,
                                                                  .planeCount = layout.planeCount(),
                                                                  .rowAlignmentBytes = 1u},
                                           layout.rowStrideBytes(), layout.planeStrideBytes(), limits);
    if (!band.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a band of the samples cannot be laid out");
    }
    return *band;
}

/// The first byte of a row of a planar image's first plane.
template <typename ByteT>
[[nodiscard]] ByteT* rowStart(ByteT* samples, const an::ImageLayout& layout, std::uint32_t row)
{
    return samples + static_cast<std::uint64_t>(row) * layout.rowStrideBytes();
}

/// A working band one worker converts its bands into, kept for every band the worker takes.
struct WorkingBand
{
    std::variant<std::monostate, an::Image<float>, an::Image<double>> image;
    void* samples = nullptr;
    std::optional<an::ImageLayout> layout;
};

/// Runs a fused chain over planar samples in fractions of full scale, reading `source` and writing `destination`, which
/// may be the same samples.
///
/// Floating samples are in those units: the chain reads them where they are and writes the destination.
/// Integer samples are worked on a band of rows at a time, each band by one worker in a working band of its own: the
/// band's rows converted into it, the chain run over it under the band's validity, and the result narrowed into the
/// destination's rows - the steps `NormalizedSamples` takes over the whole plane set, which a pointwise chain takes
/// sample by sample, so each sample comes out as the whole plane's working copy would have left it, without the copy.
/// A working band has the whole plane's rows, so the executor takes each row as it would there.
[[nodiscard]] an::Status runOverNormalized(const an::ui::ExecutionScope& scope, const an::ImageDescriptor& described,
                                           const void* source, const an::ImageLayout& sourceLayout, void* destination,
                                           const an::ImageLayout& destinationLayout,
                                           const an::KernelValidityView& validity, const an::PointwiseProgram& chain)
{
    const an::EngineContext& engine = scope.engine();
    const an::ExecutionContext& execution = scope.execution();
    const an::SampleType stored = described.sampleType();
    if (sourceLayout.samplesPerPixel() != 1u || destinationLayout.samplesPerPixel() != 1u ||
        !(sourceLayout.extent() == destinationLayout.extent()))
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the samples are not planar samples of one extent");
    }
    if (isFloating(stored))
    {
        const std::array<an::PointwiseImageView, 1u> sources{
            an::PointwiseImageView{.samples = source, .layout = &sourceLayout}};
        const an::Result<an::PointwiseExecutionResult> ran = an::executePointwiseProgram(
            execution, chain, sources,
            an::PointwiseMutableImageView{.samples = destination, .layout = &destinationLayout}, validity);
        return ran.hasValue() ? an::Status::success() : ran.status();
    }

    // Which samples are worked on, as `NormalizedSamples::open` decides it: a rule an integer's value decides judged on
    // the stored samples, once, into a mask; a mask as it is; every sample otherwise.
    const std::uint32_t width = sourceLayout.extent().width();
    const std::uint32_t height = sourceLayout.extent().height();
    const std::uint32_t channels = sourceLayout.extent().channelCount();
    an::ValidityDescriptor rule = an::ValidityDescriptor::allValid();
    std::optional<an::ValidityMask> judged;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
    std::uint32_t maskPlane = 0u;
    const an::ValidityRule stated = validity.descriptor.rule();
    if (an::validityRuleIsDecidedByValue(stated) && stated != an::ValidityRule::FiniteOnly &&
        stated != an::ValidityRule::AllValid)
    {
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, engine.limits());
        if (!extent.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "the mask of the samples cannot be described");
        }
        an::Result<an::ValidityMask> made = an::ValidityMask::create(engine, *extent);
        if (!made.hasValue())
        {
            return made.status();
        }
        std::optional<an::ValidityMaskView<std::uint8_t>> writable = made->view();
        if (!writable.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the mask of the samples holds nothing");
        }
        const an::Status marked =
            an::markImageValidity(execution, stored, source, sourceLayout, validity.descriptor, *writable);
        if (marked.failed())
        {
            return marked;
        }
        const std::optional<an::ValidityDescriptor> explicitMask =
            an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
        if (!explicitMask.has_value())
        {
            return refusal(an::ErrorCode::Internal, "an explicit mask rule cannot be stated");
        }
        judged.emplace(std::move(*made));
        mask = std::as_const(*judged).view();
        rule = *explicitMask;
    }
    else if (an::validityRuleNeedsMask(stated))
    {
        rule = validity.descriptor;
        if (validity.mask != nullptr)
        {
            mask = *validity.mask;
        }
        maskPlane = validity.maskPlane;
    }

    const an::SampleType working = normalizedWorkingType(stored);
    const std::uint64_t workingRowBytes =
        static_cast<std::uint64_t>(width) * channels * an::sampleTypeSizeBytes(working);
    const auto rowsPerBand = static_cast<std::uint32_t>(
        (std::min)(static_cast<std::uint64_t>(height),
                   (std::max)(std::uint64_t{1u}, kBandWorkingBytes / (std::max)(workingRowBytes, std::uint64_t{1u}))));
    const std::uint64_t bands = (static_cast<std::uint64_t>(height) + rowsPerBand - 1u) / rowsPerBand;
    const an::Result<an::ImageDescriptor> workingDescribed = an::describeSamplesAs(described, working);
    const std::optional<an::ImageExtent> bandExtent =
        an::ImageExtent::create(width, rowsPerBand, channels, engine.limits());
    if (!workingDescribed.hasValue() || !bandExtent.has_value())
    {
        return workingDescribed.hasValue() ? refusal(an::ErrorCode::InvalidArgument, "a band cannot be described")
                                           : workingDescribed.status();
    }
    an::ImageDescriptorRequest bandRequest = workingDescribed->toRequest();
    bandRequest.extent = *bandExtent;
    bandRequest.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> bandDescribed = an::ImageDescriptor::create(bandRequest);
    if (!bandDescribed.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "a band cannot be described");
    }

    // A signed integer is counted from its smallest value: carried across as itself, then scaled and shifted, and
    // back the same way before it is narrowed.
    const bool countedFromSmallest = isSignedInteger(stored);
    const an::ConversionPolicy policy{.domain = countedFromSmallest ? an::DomainMapping::CodeValue
                                                                    : an::DomainMapping::FullScale};
    std::optional<an::PointwiseProgram> toFractions;
    std::optional<an::PointwiseProgram> toCodes;
    if (countedFromSmallest)
    {
        const SignedRange range = signedRangeOf(stored);
        an::Result<an::PointwiseProgram> into = an::buildScaleBiasProgram(
            normalizedPlanShape(working, channels), 1.0 / range.width, -range.smallest / range.width);
        an::Result<an::PointwiseProgram> back =
            an::buildScaleBiasProgram(normalizedPlanShape(working, channels), range.width, range.smallest);
        if (!into.hasValue() || !back.hasValue())
        {
            return into.hasValue() ? back.status() : into.status();
        }
        toFractions.emplace(std::move(*into));
        toCodes.emplace(std::move(*back));
    }

    const an::TaskCost cost{.bytesPerItem = static_cast<std::uint64_t>(rowsPerBand) * workingRowBytes * 3u,
                            .operationsPerItem = static_cast<std::uint64_t>(rowsPerBand) * width * channels *
                                                 (chain.steps().size() + 4u)};
    std::vector<WorkingBand> workingBands(an::plannedWorkerCount(execution, bands, cost));
    const auto* const sourceBytes = static_cast<const std::uint8_t*>(source);
    auto* const destinationBytes = static_cast<std::uint8_t*>(destination);
    return an::parallelForWorkers(execution, bands, cost, [&](an::WorkerTask task) -> an::Status {
        WorkingBand& band = workingBands[task.workerSlot];
        if (band.samples == nullptr)
        {
            an::Status made = an::Status::success();
            const auto create = [&]<typename WorkT>() {
                an::Result<an::Image<WorkT>> image = an::Image<WorkT>::create(
                    engine, *bandDescribed, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
                if (!image.hasValue())
                {
                    made = image.status();
                    return;
                }
                an::Image<WorkT>& kept = band.image.template emplace<an::Image<WorkT>>(std::move(*image));
                const std::optional<an::ImageWriteAccess<WorkT>> access = kept.write();
                if (!access.has_value())
                {
                    made = refusal(an::ErrorCode::NotInitialized, "a working band holds nothing");
                    return;
                }
                band.samples = access->view().data();
                band.layout = access->view().layout();
            };
            if (working == an::SampleType::Float64)
            {
                create.template operator()<double>();
            }
            else
            {
                create.template operator()<float>();
            }
            if (made.failed())
            {
                return made;
            }
        }
        for (std::uint64_t index = task.range.begin; index < task.range.end; ++index)
        {
            const auto first = static_cast<std::uint32_t>(index * rowsPerBand);
            const std::uint32_t rows = (std::min)(rowsPerBand, height - first);
            const an::Result<an::ImageLayout> sourceRows = rowsOf(sourceLayout, rows, engine.limits());
            const an::Result<an::ImageLayout> destinationRows = rowsOf(destinationLayout, rows, engine.limits());
            const an::Result<an::ImageLayout> workingRows = rowsOf(*band.layout, rows, engine.limits());
            if (!sourceRows.hasValue() || !destinationRows.hasValue() || !workingRows.hasValue())
            {
                return !sourceRows.hasValue()
                           ? sourceRows.status()
                           : (!destinationRows.hasValue() ? destinationRows.status() : workingRows.status());
            }
            an::KernelValidityView bandValidity{.descriptor = rule, .mask = nullptr, .maskPlane = maskPlane};
            std::optional<an::ValidityMaskView<const std::uint8_t>> bandMask;
            if (mask.has_value())
            {
                const std::uint64_t offset = static_cast<std::uint64_t>(first) * mask->rowStrideBytes();
                const std::optional<an::ImageExtent> maskExtent =
                    an::ImageExtent::create(width, rows, mask->planeCount(), engine.limits());
                if (!maskExtent.has_value())
                {
                    return refusal(an::ErrorCode::InvalidArgument, "a band of the mask cannot be described");
                }
                bandMask = an::ValidityMaskView<const std::uint8_t>::create(
                    std::span<const std::uint8_t>(mask->data() + offset, mask->spannedByteCount() - offset),
                    *maskExtent, mask->rowStrideBytes(), mask->planeStrideBytes());
                if (!bandMask.has_value())
                {
                    return refusal(an::ErrorCode::InvalidArgument, "a band of the mask cannot be viewed");
                }
                bandValidity.mask = &*bandMask;
            }
            an::ConversionCounters counters;
            an::Status done =
                an::convertImageSamples(execution, stored, rowStart(sourceBytes, sourceLayout, first), *sourceRows,
                                        working, band.samples, *workingRows, policy, counters);
            const std::array<an::PointwiseImageView, 1u> workingSources{
                an::PointwiseImageView{.samples = band.samples, .layout = &*workingRows}};
            const an::PointwiseMutableImageView workingDestination{.samples = band.samples, .layout = &*workingRows};
            if (done.ok() && toFractions.has_value())
            {
                const an::Result<an::PointwiseExecutionResult> scaled = an::executePointwiseProgram(
                    execution, *toFractions, workingSources, workingDestination, an::KernelValidityView{});
                done = scaled.hasValue() ? an::Status::success() : scaled.status();
            }
            if (done.ok())
            {
                const an::Result<an::PointwiseExecutionResult> ran =
                    an::executePointwiseProgram(execution, chain, workingSources, workingDestination, bandValidity);
                done = ran.hasValue() ? an::Status::success() : ran.status();
            }
            if (done.ok() && toCodes.has_value())
            {
                const an::Result<an::PointwiseExecutionResult> scaled = an::executePointwiseProgram(
                    execution, *toCodes, workingSources, workingDestination, an::KernelValidityView{});
                done = scaled.hasValue() ? an::Status::success() : scaled.status();
            }
            if (done.ok())
            {
                done = an::convertImageSamples(execution, working, band.samples, *workingRows, stored,
                                               rowStart(destinationBytes, destinationLayout, first), *destinationRows,
                                               policy, counters);
            }
            if (done.failed())
            {
                return done;
            }
        }
        return an::Status::success();
    });
}

/// Whether a chain's answer for a sample depends on the sample's code alone and is the same wherever the code occurs:
/// one source, no step that reads another channel or plane, and only the steps whose compiled run and scalar
/// evaluator agree bit for bit - scales, biases and clamps, uncontracted.
[[nodiscard]] bool answersEveryCodeAlike(const an::PointwiseProgram& chain) noexcept
{
    if (chain.sourceCount() != 1u || chain.contractionAllowed())
    {
        return false;
    }
    for (const an::PointwiseOp& step : chain.steps())
    {
        if (step.kind != an::PointwiseOpKind::Scale && step.kind != an::PointwiseOpKind::Bias &&
            step.kind != an::PointwiseOpKind::Clamp)
        {
            return false;
        }
    }
    return true;
}

/// Runs a chain over an eight- or sixteen-bit plane every sample of which is valid as one lookup of its codes.
///
/// The chain's answer for every code is made once, by the very steps a plane takes - the codes opened as fractions of
/// full scale, the chain run, the results narrowed back - over one row holding every code in every channel,
/// and each sample of the plane is then looked up in it: read where it lies, its answer written to the output. The
/// plane comes out as those steps would have left it, sample for sample, without the working copy they convert it into
/// and out of. A signed code is looked up by its two's-complement bit pattern - the code itself when it is not
/// negative, and the code offset by the representation's span when it is - so the engine's lookup of the unsigned
/// codes of that width reads the samples and writes the answers' bit patterns, which are the answers.
///
/// @param read the samples looked up, planar, of the output's extent.
/// @return true when the chain ran as a lookup; false when it does not apply - a rule or a mask that decides pixel
///         by pixel, a plane smaller than its table, a chain whose answer for a code could depend on where the code
///         is, or channels that answer a code differently; or the failure.
template <typename SampleT>
[[nodiscard]] an::Result<bool> runAsCodeLookup(const an::ui::ExecutionScope& scope, const an::PointwiseImageView& read,
                                               const an::ChannelScopeOutput& output,
                                               const an::KernelValidityView& validity,
                                               const an::PointwiseProgram& chain)
{
    using CodeT = std::make_unsigned_t<SampleT>;
    constexpr std::uint32_t kCodes = std::uint32_t{1} << (8u * sizeof(SampleT));
    const an::ImageExtent extent = output.layout->extent();
    const an::ValidityRule rule = validity.descriptor.rule();
    if ((rule != an::ValidityRule::AllValid && rule != an::ValidityRule::FiniteOnly) || validity.mask != nullptr ||
        static_cast<std::uint64_t>(extent.width()) * extent.height() < kCodes || !answersEveryCodeAlike(chain))
    {
        return false;
    }

    // One row holding every code, in every channel, described as the plane is, in the order of the codes' bits.
    const an::EngineContext& engine = scope.engine();
    const std::optional<an::ImageExtent> rowExtent =
        an::ImageExtent::create(kCodes, 1u, extent.channelCount(), engine.limits());
    if (!rowExtent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the row of every code cannot be described");
    }
    an::ImageDescriptorRequest rowRequest = output.descriptor->toRequest();
    rowRequest.extent = *rowExtent;
    rowRequest.cfa = an::CfaDescriptor{};
    const std::optional<an::ImageDescriptor> rowDescribed = an::ImageDescriptor::create(rowRequest);
    if (!rowDescribed.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the row of every code cannot be described");
    }
    an::Result<an::Image<SampleT>> row = an::Image<SampleT>::create(
        engine, *rowDescribed, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
    if (!row.hasValue())
    {
        return row.status();
    }
    std::optional<an::ImageWriteAccess<SampleT>> access = row->write();
    if (!access.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the row of every code holds nothing");
    }
    const an::ImageLayout rowLayout = access->view().layout();
    for (std::uint32_t channel = 0u; channel < extent.channelCount(); ++channel)
    {
        const auto codes = access->view().row(channel, 0u);
        if (!codes.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the row of every code holds nothing");
        }
        for (std::uint32_t bits = 0u; bits < kCodes; ++bits)
        {
            (*codes)[bits] = std::bit_cast<SampleT>(static_cast<CodeT>(bits));
        }
    }
    if (an::Status answered = runOverNormalized(scope, *rowDescribed, access->view().data(), rowLayout,
                                                access->view().data(), rowLayout, an::KernelValidityView{}, chain);
        answered.failed())
    {
        return answered;
    }

    // One table, when every channel answers every code alike, in fractions of full scale for the lookup to read back.
    const auto answers = access->view().row(0u, 0u);
    for (std::uint32_t channel = 1u; channel < extent.channelCount(); ++channel)
    {
        const auto other = access->view().row(channel, 0u);
        if (!other.has_value() || !std::equal(answers->begin(), answers->end(), other->begin()))
        {
            return false;
        }
    }
    std::vector<double> entries(kCodes);
    for (std::uint32_t bits = 0u; bits < kCodes; ++bits)
    {
        entries[bits] = static_cast<double>(std::bit_cast<CodeT>((*answers)[bits])) / static_cast<double>(kCodes - 1u);
    }
    an::TonePlanShape shape;
    shape.sampleType = an::kSampleTypeOf<CodeT>;
    shape.channelCount = extent.channelCount();
    const an::Result<an::CurveProgramPlan> lookup =
        an::buildLookupProgram(shape, an::LookupProgramRequest{.entries = entries});
    if (!lookup.hasValue())
    {
        return lookup.status();
    }
    const std::array<an::PointwiseImageView, 1u> sources{read};
    const an::Result<an::PointwiseExecutionResult> looked = an::executePointwiseProgram(
        scope.execution(), lookup->program(), sources,
        an::PointwiseMutableImageView{.samples = output.samples, .layout = output.layout}, an::KernelValidityView{});
    if (!looked.hasValue())
    {
        return looked.status();
    }
    return true;
}

} // namespace

an::PointwiseImageView pointwiseReadOf(const an::ChannelScopeInput& input,
                                       const an::ChannelScopeOutput& output) noexcept
{
    const bool readsInput = input.samples != nullptr && input.layout != nullptr && input.descriptor != nullptr &&
                            output.descriptor != nullptr && input.layout->samplesPerPixel() == 1u &&
                            input.descriptor->sampleType() == output.descriptor->sampleType();
    if (readsInput)
    {
        return an::PointwiseImageView{.samples = input.samples, .layout = input.layout};
    }
    return an::PointwiseImageView{.samples = output.samples, .layout = output.layout};
}

an::Status runNormalizedChain(const an::ui::ExecutionScope& scope, const an::ChannelScopeInput& input,
                              const an::ChannelScopeOutput& output, const PointwiseChainBuilder& build)
{
    if (output.descriptor == nullptr || output.layout == nullptr)
    {
        return refusal(an::ErrorCode::InvalidArgument, "the plane to work on is not described");
    }
    const an::SampleType stored = output.descriptor->sampleType();
    an::Result<PointwiseChain> chain =
        build(normalizedPlanShape(normalizedWorkingType(stored), output.layout->extent().channelCount()));
    if (!chain.hasValue())
    {
        return chain.status();
    }
    if (chain->empty())
    {
        return refusal(an::ErrorCode::InvalidArgument, "the chain holds no program");
    }
    an::Result<an::PointwiseProgram> fused = chain->front();
    if (chain->size() > 1u)
    {
        fused = an::fusePointwisePrograms(*chain);
        if (!fused.hasValue())
        {
            return fused.status();
        }
    }
    const an::PointwiseImageView read = pointwiseReadOf(input, output);
    // An eight- or sixteen-bit plane is looked up code by code where the chain allows it.
    an::Result<bool> looked = false;
    switch (stored)
    {
    case an::SampleType::UInt16:
        looked = runAsCodeLookup<std::uint16_t>(scope, read, output, input.validity, *fused);
        break;
    case an::SampleType::Int16:
        looked = runAsCodeLookup<std::int16_t>(scope, read, output, input.validity, *fused);
        break;
    case an::SampleType::UInt8:
        looked = runAsCodeLookup<std::uint8_t>(scope, read, output, input.validity, *fused);
        break;
    case an::SampleType::Int8:
        looked = runAsCodeLookup<std::int8_t>(scope, read, output, input.validity, *fused);
        break;
    default:
        break;
    }
    if (!looked.hasValue())
    {
        return looked.status();
    }
    if (*looked)
    {
        return an::Status::success();
    }
    return runOverNormalized(scope, *output.descriptor, read.samples, *read.layout, output.samples, *output.layout,
                             input.validity, *fused);
}

std::string channelStepSuffix(const ImageViewControl* target, an::ChannelScope scope)
{
    if (!holdsColourImage(target))
    {
        return std::string();
    }
    return " in channel: " + an::ui::processChannelLabel(scope).toStdString();
}

bool holdsColourImage(const ImageViewControl* target)
{
    if (target == nullptr)
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = target->imageDescriptor();
    return described.has_value() && (described->channelSchema() == an::ChannelSchema::Rgb ||
                                     described->channelSchema() == an::ChannelSchema::Rgba);
}

// ---- The nonlinear record.

an::Status recordNonlinearResult(an::ui::ProcessDestination& destination, const NonlinearResultRecord& record)
{
    if (!destination.valid())
    {
        return refusal(an::ErrorCode::NotInitialized, "the run has no result to record");
    }
    const an::consumer::AnyImage& image = destination.asset().image();
    const std::optional<an::ImageDescriptor> described = descriptorOf(image);
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
    }
    const an::ImageMetadata& metadata = metadataOf(image);

    // The cards say of the result what they say of every one of these processes' results: nonlinear.
    const an::ProcessingStateRecord state =
        an::nonlinearState(an::readProcessingStateCards(metadata.codec()), *described, record.stateOperation);
    an::Result<an::CodecMetadata> cards = an::withProcessingStateCards(metadata.codec(), state);
    if (!cards.hasValue())
    {
        return cards.status();
    }

    // A result is no longer the linear encoding its source may have claimed; an encoding it was in, it still is.
    an::ImageSemantics semantics = metadata.semantics();
    if (semantics.transferFunction() == an::TransferFunction::Linear)
    {
        an::ImageSemanticsRequest meaning = semantics.toRequest();
        meaning.transferFunction = an::TransferFunction::Unspecified;
        meaning.gammaExponent.reset();
        an::Result<an::ImageSemantics> restated = an::ImageSemantics::create(meaning);
        if (!restated.hasValue())
        {
            return restated.status();
        }
        semantics = std::move(*restated);
    }
    if (record.restatedValidity.has_value() && !(semantics.validity() == *record.restatedValidity))
    {
        an::ImageSemanticsRequest meaning = semantics.toRequest();
        meaning.validity = *record.restatedValidity;
        an::Result<an::ImageSemantics> restated = an::ImageSemantics::create(meaning);
        if (!restated.hasValue())
        {
            return restated.status();
        }
        semantics = std::move(*restated);
    }

    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(record.engineOperation, record.detail);
    if (!entry.hasValue())
    {
        return entry.status();
    }
    // The new cards and meaning are stated where the result is, in one transition that names what ran: the samples
    // stay where the process wrote them, and nothing of the result is copied to carry them.
    an::MetadataTransition change{.record = std::move(*entry)};
    if (!(semantics == metadata.semantics()))
    {
        change.semantics = std::move(semantics);
    }
    if (!(*cards == metadata.codec()))
    {
        change.codec = std::move(*cards);
    }
    if (an::Status stated = destination.asset().transition(change); stated.failed())
    {
        return stated;
    }
    // Under a restated validity no sample is read through the source's packed mask, so it is let go - after the
    // transition, the one step that can be refused, so a refusal leaves the destination as it was.
    if (record.restatedValidity.has_value())
    {
        destination.asset().clearValidity();
    }
    return an::Status::success();
}

} // namespace process_utils
