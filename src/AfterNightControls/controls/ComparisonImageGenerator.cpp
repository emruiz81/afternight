// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The comparison view's difference image over the engine's image difference.

#include "controls/ComparisonImageGenerator.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Pointwise/ImageDifference.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace an::ui
{

namespace
{

/// What the comparison's difference is: the engine's image difference, with every sample it did not write left not a
/// number, so the result states its validity itself rather than carrying a mask.
inline constexpr an::OperationEffect kComparisonDifferenceEffect{
    .samples = an::SampleEffect::Writes,
    .description = an::DescriptionEffect::Restates,
    .semantics = an::SemanticEffect::Restates,
    .solution = an::SolutionEffect::Preserves,
    .codecMetadata = an::CodecMetadataEffect::Drops,
    .outputIntent = an::OutputIntentEffect::Withdraws,
    .validity = {.policy = an::ValidityPolicy::Regenerate,
                 .produced = *an::ValidityDescriptor::create({.rule = an::ValidityRule::FiniteOnly})}};

/// A failure the comparison decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// One image the difference reads: its description, samples and validity.
struct DifferenceInput
{
    std::optional<an::ImageDescriptor> descriptor;
    an::ImageDifferenceSource source{};
    std::optional<an::ImageLayout> layout;
    std::optional<an::ValidityMaskView<const std::uint8_t>> mask;
};

/// Describes an image under a lease as a difference input. The input borrows the lease's samples, so the lease
/// outlives it.
[[nodiscard]] an::Status describeInput(const an::consumer::AssetReadLease& lease, DifferenceInput& input)
{
    an::Status described = an::Status::success();
    lease.visit([&](const auto& typed) {
        const auto view = typed.view();
        input.descriptor = typed.descriptor();
        if (!view.has_value() || !input.descriptor.has_value())
        {
            described = refusal(an::ErrorCode::NotInitialized, "an image of the comparison holds nothing");
            return;
        }
        input.layout = view->layout();
        input.source.samples = view->data();
        input.source.validity.descriptor = typed.metadata().semantics().validity();
    });
    if (described.failed())
    {
        return described;
    }
    if (const an::ValidityMask* mask = lease.validity(); mask != nullptr)
    {
        input.mask = mask->view();
        input.source.validity.mask = input.mask.has_value() ? &*input.mask : nullptr;
    }
    input.source.descriptor = &*input.descriptor;
    input.source.layout = &*input.layout;
    return an::Status::success();
}

} // namespace

an::Result<an::consumer::ImageAsset> buildDifferenceImage(const an::consumer::EngineSession& session,
                                                          const an::consumer::ImageAsset& left,
                                                          const an::consumer::ImageAsset& right,
                                                          const ComparisonDifferenceOptions& options,
                                                          std::stop_token stop)
{
    an::Result<an::consumer::AssetReadLease> leftLease = left.read(stop);
    if (!leftLease.hasValue())
    {
        return leftLease.status();
    }
    an::Result<an::consumer::AssetReadLease> rightLease = right.read(stop);
    if (!rightLease.hasValue())
    {
        return rightLease.status();
    }
    DifferenceInput leftInput;
    DifferenceInput rightInput;
    if (an::Status described = describeInput(*leftLease, leftInput); described.failed())
    {
        return described;
    }
    if (an::Status described = describeInput(*rightLease, rightInput); described.failed())
    {
        return described;
    }

    an::Result<an::consumer::OperationScope> scope = session.admit(
        an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview, .stopToken = stop});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    const an::Result<an::ImageDifferencePlan> plan = an::planImageDifference(an::ImageDifferenceRequest{
        .measure = options.grayscale ? an::ImageDifferenceMeasure::MeanAbsolute : an::ImageDifferenceMeasure::Absolute,
        .output = an::SampleType::Float32});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    const an::Result<an::ImageDescriptor> described =
        an::imageDifferenceDescriptor(*plan, *leftInput.descriptor, *rightInput.descriptor);
    if (!described.hasValue())
    {
        return described.status();
    }

    return leftLease->visit([&](const auto& leftImage) -> an::Result<an::consumer::ImageAsset> {
        an::Result<an::Image<float>> difference =
            an::Image<float>::createDestination(scope->engine(), *described, an::StorageArrangement::Planar,
                                                an::StorageInitialization::Uninitialized, leftImage);
        if (!difference.hasValue())
        {
            return difference.status();
        }
        {
            const std::optional<an::ImageWriteAccess<float>> access = difference->write();
            if (!access.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the difference holds nothing");
            }
            // A sample the difference does not write is invalid, and says so by not being a number.
            float* samples = access->view().data();
            std::fill_n(samples, access->view().layout().totalBytes() / sizeof(float),
                        std::numeric_limits<float>::quiet_NaN());
            const an::Result<an::ImageDifferenceResult> computed = an::computeImageDifference(
                scope->execution(), *plan, leftInput.source, rightInput.source, samples, access->view().layout());
            if (!computed.hasValue())
            {
                return computed.status();
            }
        }
        an::ImageSemanticsRequest meaning = leftImage.metadata().semantics().toRequest();
        meaning.validity = kComparisonDifferenceEffect.validity.produced;
        const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
        if (!semantics.hasValue())
        {
            return semantics.status();
        }
        const an::Result<an::MetadataTransition> change = an::operationTransition(
            kComparisonDifferenceEffect,
            an::OperationOutcome{.operation = "an::computeImageDifference",
                                 .detail = options.grayscale ? "the comparison's mean absolute difference"
                                                             : "the comparison's absolute difference",
                                 .semantics = *semantics});
        if (!change.hasValue())
        {
            return change.status();
        }
        if (an::Status recorded = difference->transition(*change); recorded.failed())
        {
            return recorded;
        }
        scope->end();
        return an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*difference)));
    });
}

} // namespace an::ui
