// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// One plane matched linearly to another inside the match's limits.

#include "stdafx.hpp"
#include "processes/core/LinearChannelMatch.h"

#include <AfterNightEngine/Algorithms/Normalization/NormalizationMethod.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <optional>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Result<an::NormalizationFrameView> frameViewOf(const LinearMatchPlane& plane,
                                                                 const an::KernelValidityView& validity)
{
    if (plane.samples == nullptr || plane.layout == nullptr || plane.descriptor == nullptr)
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "a plane the match reads holds nothing");
    }
    return an::NormalizationFrameView::create(plane.samples, plane.layout->totalBytes(), *plane.layout,
                                              *plane.descriptor, validity);
}

} // namespace

an::Result<an::FrameNormalization> measureLinearChannelMatch(const an::ExecutionContext& execution,
                                                             const LinearMatchPlane& reference,
                                                             const LinearMatchPlane& component)
{
    const std::optional<an::ValidityDescriptor> inside =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::DomainRange,
                                        .lowestValid = kLinearMatchLowestSample,
                                        .highestValid = kLinearMatchHighestSample});
    if (!inside.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the match's limits cannot be stated");
    }
    an::KernelValidityView validity;
    validity.descriptor = *inside;
    const an::Result<an::NormalizationFrameView> referenceView = frameViewOf(reference, validity);
    if (!referenceView.hasValue())
    {
        return referenceView.status();
    }
    const an::Result<an::NormalizationFrameView> componentView = frameViewOf(component, validity);
    if (!componentView.hasValue())
    {
        return componentView.status();
    }
    const an::StatisticsRegion whole{.originX = 0u,
                                     .originY = 0u,
                                     .width = reference.descriptor->extent().width(),
                                     .height = reference.descriptor->extent().height()};
    const an::Result<an::OverlapMeasurement> overlap = an::measureOverlapStatistics(
        execution, 0u, *referenceView, whole, 1u, *componentView, whole, an::ScaleEstimator::MedianAbsoluteDeviation);
    if (!overlap.hasValue())
    {
        return overlap.status();
    }
    an::NormalizationReference pairedReference;
    pairedReference.estimator = overlap->estimator;
    pairedReference.channelCount = overlap->channelCount;
    pairedReference.memberCount = 1u;
    pairedReference.channels[0] = overlap->a[0];
    an::NormalizationFrameStatistics pairedComponent;
    pairedComponent.estimator = overlap->estimator;
    pairedComponent.channelCount = overlap->channelCount;
    pairedComponent.channels[0] = overlap->b[0];
    return an::estimateGlobalNormalization(pairedReference, pairedComponent, an::NormalizationMethod::AdditiveScaling);
}

an::Status applyLinearChannelMatch(const an::ExecutionContext& execution, const an::FrameNormalization& match,
                                   const LinearMatchPlane& component, void* destination,
                                   const an::ImageLayout& destinationLayout)
{
    // The whole plane is matched by the coefficients, not only the samples that decided them.
    const an::Result<an::NormalizationFrameView> wholeView = frameViewOf(component, an::KernelValidityView{});
    if (!wholeView.hasValue())
    {
        return wholeView.status();
    }
    const an::Result<an::NormalizationApplyResult> applied = an::applyGlobalNormalization(
        execution, match, *wholeView, destination, destinationLayout.totalBytes(), destinationLayout);
    return applied.hasValue() ? an::Status::success() : applied.status();
}

} // namespace an::ui::processes
