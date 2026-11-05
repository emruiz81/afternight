// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - deterministic photometric frame weights and their propagated uncertainty.
//
// The model is evaluated on the calling thread in input order: one scan validates every record and
// finds the smallest zero point, one forms the raw weights, an exact selection finds the medians,
// and the mean normalization follows. Two paths share it. When some measured zero point has no
// uncertainty, nothing is differentiated and the workspace holds a working record and one ranking
// slot per frame; when every one has, each raw weight also carries a sparse gradient over the zero
// points and the first-order uncertainty is propagated through the normalization. Every output
// element is written after the last fallible step, so a failure leaves the caller's span as it was.

#include <AfterNightEngine/Algorithms/Stacking/PhotometricWeighting.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include "kernels/reference/PlaneSourceAccess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace an
{
namespace
{

constexpr const char* kOperation = "an::computePhotometricStackWeights";
constexpr const char* kProgressStage = "computing photometric stack weights";
/// How many records the validation scan visits between cancellation checkpoints.
constexpr std::uint32_t kCancellationStride = 256u;

/// `0.4 * ln(10)`: the derivative of the transparency `10^(0.4 * (z - zMin))` with respect to `z`,
/// per unit of the transparency itself.
constexpr double kMagnitudeDerivative = 0.4 * 2.3025850929940456840179914546844;

/// A malformed measurement detected on the hot validation scan.
enum class MeasurementProblem : std::uint8_t
{
    None,
    NonFiniteZeroPoint,
    UncertaintyWithoutZeroPoint,
    InvalidUncertainty,
    InvalidRootMeanSquare
};

/// One partial derivative with respect to a measured zero point.
struct GradientTerm
{
    /// Which input zero point.
    std::uint32_t inputIndex = 0u;

    /// Raw-weight derivative per magnitude.
    double derivative = 0.0;
};

/// The sparse derivative of one raw weight.
///
/// A measured weight depends on itself and the minimum-zero-point frame. A median substitution is
/// one measured weight or the average of two, and therefore has at most four terms.
struct SparseGradient
{
    /// The possible terms.
    std::array<GradientTerm, 4u> terms{};

    /// How many leading terms are meaningful.
    std::uint32_t count = 0u;
};

/// One frame's state held until the operation can commit every output together.
struct WorkingWeight
{
    /// Raw model value. The uncertainty path overwrites it with the stack-ready value after mean
    /// normalization; the value-only path divides as it writes the output and leaves it raw.
    double raw = 0.0;

    /// Whether the measured-set median supplied `raw`.
    bool substituted = false;

    /// Whether the minimum replaced the evaluated model.
    bool floored = false;
};

/// A measured raw value and its stable input-order tie break.
struct RankedWeight
{
    /// The raw value being ranked.
    double value = 0.0;

    /// Its input position.
    std::uint32_t inputIndex = 0u;
};

static_assert(alignof(WorkingWeight) >= alignof(RankedWeight));
static_assert(alignof(WorkingWeight) >= alignof(SparseGradient));

/// Builds one described failure in the algorithms domain.
///
/// @param code why the operation failed.
/// @param message what a person reads.
/// @return the failure.
[[nodiscard]] Status weightingFailure(ErrorCode code, const char* message)
{
    return Status::failure(
        code, StatusDetail{.message = message, .operation = kOperation, .domain = ErrorDomain::Algorithms});
}

/// Builds a frame-count ceiling failure with machine-readable counts.
///
/// @param requested how many frames the caller supplied.
/// @return the failure.
[[nodiscard]] Status frameLimitFailure(std::uint64_t requested)
{
    return Status::failure(
        ErrorCode::LimitExceeded,
        StatusDetail{.message = "the photometric-weighting frame count exceeds the stated ceiling",
                     .operation = kOperation,
                     .domain = ErrorDomain::Algorithms,
                     .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                 StatusContextField::ofCount(ContextKey::Limit, kMaxPhotometricWeightFrames)}});
}

/// Checks cancellation at a stable coarse input boundary.
///
/// @param execution the running operation.
/// @param index the input position about to be visited.
/// @return success away from a checkpoint, or the execution's cancellation status at one.
[[nodiscard]] Status inputCheckpoint(const ExecutionContext& execution, std::uint32_t index) noexcept
{
    return index % kCancellationStride == 0u ? execution.checkCancellation() : Status::success();
}

/// Adds or merges one term into a sparse gradient.
///
/// @param gradient the derivative being assembled.
/// @param inputIndex which independent measurement.
/// @param derivative the term to add.
void addGradientTerm(SparseGradient& gradient, std::uint32_t inputIndex, double derivative) noexcept
{
    if (derivative == 0.0)
    {
        return;
    }
    for (std::uint32_t term = 0u; term < gradient.count; ++term)
    {
        if (gradient.terms[term].inputIndex == inputIndex)
        {
            gradient.terms[term].derivative += derivative;
            return;
        }
    }
    if (gradient.count < gradient.terms.size())
    {
        gradient.terms[gradient.count++] = GradientTerm{.inputIndex = inputIndex, .derivative = derivative};
    }
}

/// Copies a sparse gradient with a multiplier.
///
/// @param destination where terms are accumulated.
/// @param source which terms to copy.
/// @param scale the multiplier.
void addScaledGradient(SparseGradient& destination, const SparseGradient& source, double scale) noexcept
{
    for (std::uint32_t term = 0u; term < source.count; ++term)
    {
        addGradientTerm(destination, source.terms[term].inputIndex, source.terms[term].derivative * scale);
    }
}

/// The exact median of a non-empty ranked range.
///
/// The selected upper middle is placed at `count / 2`; for an even count the selected lower middle
/// is placed immediately before it. The total value-and-input-index order makes both identities
/// independent of the standard library's partition permutation.
///
/// @param values scratch values that this call may reorder.
/// @param count how many leading records are meaningful, at least one.
/// @return the median in raw-weight space.
[[nodiscard]] double selectRankedMedian(RankedWeight* values, std::uint32_t count) noexcept
{
    const std::uint32_t middle = count / 2u;
    const auto less = [](const RankedWeight& left, const RankedWeight& right) {
        return left.value < right.value || (left.value == right.value && left.inputIndex < right.inputIndex);
    };
    std::nth_element(values, values + middle, values + count, less);
    if ((count % 2u) != 0u)
    {
        return values[middle].value;
    }
    RankedWeight* const lower = std::max_element(values, values + middle, less);
    if (lower != values + middle - 1u)
    {
        std::swap(*lower, values[middle - 1u]);
    }
    return (values[middle - 1u].value + values[middle].value) * 0.5;
}

/// The exact median of a non-empty scalar range.
///
/// @param values scratch values that this call may reorder.
/// @param count how many leading values are meaningful, at least one.
/// @return the median.
[[nodiscard]] double selectScalarMedian(double* values, std::uint32_t count) noexcept
{
    const std::uint32_t middle = count / 2u;
    std::nth_element(values, values + middle, values + count);
    if ((count % 2u) != 0u)
    {
        return values[middle];
    }
    return (*(std::max_element)(values, values + middle) + values[middle]) * 0.5;
}

/// Writes a uniform successful result after every input has already been validated.
///
/// @param execution the running operation.
/// @param output every result element.
/// @param result the counts and disposition to complete.
/// @param disposition why the result is uniform.
/// @param warn whether the no-model fallback is worth reporting.
/// @return the completed result.
[[nodiscard]] PhotometricWeightingResult writeUniform(const ExecutionContext& execution,
                                                      std::span<PhotometricStackWeight> output,
                                                      PhotometricWeightingResult result,
                                                      PhotometricWeightingDisposition disposition, bool warn) noexcept
{
    for (PhotometricStackWeight& weight : output)
    {
        weight = PhotometricStackWeight{.value = 1.0};
    }
    result.disposition = disposition;
    result.uncertaintyComplete = false;
    result.weightSum = static_cast<double>(result.frameCount);
    result.effectiveFrameCount = static_cast<double>(result.frameCount);
    if (warn)
    {
        execution.reportDiagnostic(DiagnosticSeverity::Warning, ErrorDomain::Algorithms, kOperation,
                                   disposition == PhotometricWeightingDisposition::UniformNoZeroPoints
                                       ? "No photometric zero point was supplied; every frame received weight one."
                                       : "The photometric model produced no usable raw weight; every frame received "
                                         "weight one.");
    }
    execution.reportProgress(result.frameCount, result.frameCount, kProgressStage);
    return result;
}

/// Whether a request has finite values inside their declared ranges.
///
/// @param request the policy to check.
/// @return success, or the first invalid term.
[[nodiscard]] Status checkRequest(const PhotometricWeightingRequest& request)
{
    if (!std::isfinite(request.uncertaintyPenaltyPerMagnitude) || request.uncertaintyPenaltyPerMagnitude < 0.0)
    {
        return weightingFailure(ErrorCode::InvalidArgument,
                                "the zero-point uncertainty penalty must be finite and non-negative");
    }
    if (!std::isfinite(request.alignmentPenalty) || request.alignmentPenalty < 0.0)
    {
        return weightingFailure(ErrorCode::InvalidArgument,
                                "the astrometric alignment penalty must be finite and non-negative");
    }
    if (!std::isfinite(request.minimumRawWeight) || request.minimumRawWeight < 0.0 || request.minimumRawWeight > 1.0)
    {
        return weightingFailure(ErrorCode::InvalidArgument,
                                "the minimum raw weight must be finite and between zero and one");
    }
    return Status::success();
}

/// Classifies one optional measurement without constructing a success status on the hot scan.
///
/// @param measurement the record to check.
/// @return `MeasurementProblem::None`, or why the record cannot enter the model.
[[nodiscard]] MeasurementProblem measurementProblem(const PhotometricFrameMeasurement& measurement) noexcept
{
    if (measurement.zeroPointMagnitudes.has_value() && !std::isfinite(*measurement.zeroPointMagnitudes))
    {
        return MeasurementProblem::NonFiniteZeroPoint;
    }
    if (measurement.zeroPointStandardUncertaintyMagnitudes.has_value())
    {
        if (!measurement.zeroPointMagnitudes.has_value())
        {
            return MeasurementProblem::UncertaintyWithoutZeroPoint;
        }
        const double uncertainty = *measurement.zeroPointStandardUncertaintyMagnitudes;
        if (!std::isfinite(uncertainty) || uncertainty < 0.0)
        {
            return MeasurementProblem::InvalidUncertainty;
        }
    }
    if (measurement.astrometricRootMeanSquarePixels.has_value())
    {
        const double rms = *measurement.astrometricRootMeanSquarePixels;
        if (!std::isfinite(rms) || rms <= 0.0)
        {
            return MeasurementProblem::InvalidRootMeanSquare;
        }
    }
    return MeasurementProblem::None;
}

/// Describes a malformed measurement after the hot scan finds one.
///
/// @param problem the non-success classification to describe.
/// @return the public returned failure.
[[nodiscard]] Status measurementFailure(MeasurementProblem problem)
{
    switch (problem)
    {
    case MeasurementProblem::NonFiniteZeroPoint:
        return weightingFailure(ErrorCode::InvalidArgument, "a present photometric zero point must be finite");
    case MeasurementProblem::UncertaintyWithoutZeroPoint:
        return weightingFailure(ErrorCode::IncompatibleArguments,
                                "a zero-point uncertainty cannot be supplied without its zero point");
    case MeasurementProblem::InvalidUncertainty:
        return weightingFailure(ErrorCode::InvalidArgument,
                                "a present zero-point uncertainty must be finite and non-negative");
    case MeasurementProblem::InvalidRootMeanSquare:
        return weightingFailure(ErrorCode::InvalidArgument,
                                "a present astrometric RMS must be finite and strictly positive");
    case MeasurementProblem::None:
        break;
    }
    return weightingFailure(ErrorCode::Internal, "an unknown photometric measurement failure was detected");
}

} // namespace

const char* photometricWeightingDispositionName(PhotometricWeightingDisposition disposition) noexcept
{
    switch (disposition)
    {
    case PhotometricWeightingDisposition::Computed:
        return "Computed";
    case PhotometricWeightingDisposition::SingleFrame:
        return "SingleFrame";
    case PhotometricWeightingDisposition::UniformNoZeroPoints:
        return "UniformNoZeroPoints";
    case PhotometricWeightingDisposition::UniformDegenerate:
        return "UniformDegenerate";
    }
    return "UnknownPhotometricWeightingDisposition";
}

Result<PhotometricWeightingResult>
computePhotometricStackWeights(const ExecutionContext& execution,
                               std::span<const PhotometricFrameMeasurement> measurements,
                               const PhotometricWeightingRequest& request, std::span<PhotometricStackWeight> output)
{
    if (!execution.valid())
    {
        return weightingFailure(ErrorCode::NotInitialized,
                                "photometric weighting requires an execution with an engine-context lease");
    }
    if (measurements.empty())
    {
        return weightingFailure(ErrorCode::EmptySelection, "photometric weighting requires at least one frame");
    }
    if (measurements.size() > kMaxPhotometricWeightFrames)
    {
        return frameLimitFailure(measurements.size());
    }
    if (output.size() != measurements.size())
    {
        return weightingFailure(ErrorCode::IncompatibleArguments,
                                "the output span must hold exactly one weight per measurement");
    }
    Status status = checkRequest(request);
    if (status.failed())
    {
        return status;
    }

    const std::uint32_t frameCount = static_cast<std::uint32_t>(measurements.size());
    PhotometricWeightingResult result;
    result.frameCount = frameCount;
    double minimumZeroPoint = (std::numeric_limits<double>::infinity)();
    std::uint32_t minimumZeroPointIndex = 0u;
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        status = inputCheckpoint(execution, index);
        if (status.failed())
        {
            return status;
        }
        const MeasurementProblem problem = measurementProblem(measurements[index]);
        if (problem != MeasurementProblem::None)
        {
            return measurementFailure(problem);
        }
        const PhotometricFrameMeasurement& measurement = measurements[index];
        if (!measurement.zeroPointMagnitudes.has_value())
        {
            continue;
        }
        ++result.measuredFrameCount;
        if (measurement.zeroPointStandardUncertaintyMagnitudes.has_value())
        {
            ++result.uncertaintyMeasurementCount;
        }
        if (measurement.astrometricRootMeanSquarePixels.has_value())
        {
            ++result.alignmentMeasurementCount;
        }
        if (*measurement.zeroPointMagnitudes < minimumZeroPoint)
        {
            minimumZeroPoint = *measurement.zeroPointMagnitudes;
            minimumZeroPointIndex = index;
        }
    }
    status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }

    if (result.measuredFrameCount == 0u)
    {
        return writeUniform(execution, output, result, PhotometricWeightingDisposition::UniformNoZeroPoints, true);
    }
    result.baselineZeroPointMagnitudes = minimumZeroPoint;
    result.uncertaintyComplete = result.uncertaintyMeasurementCount == result.measuredFrameCount;

    if (frameCount == 1u)
    {
        PhotometricStackWeight weight;
        if (result.uncertaintyComplete)
        {
            weight.standardUncertainty = 0.0;
        }
        output.front() = weight;
        result.disposition = PhotometricWeightingDisposition::SingleFrame;
        result.weightSum = 1.0;
        result.effectiveFrameCount = 1.0;
        result.medianAstrometricRootMeanSquarePixels = measurements.front().astrometricRootMeanSquarePixels;
        execution.reportProgress(1u, 1u, kProgressStage);
        return result;
    }

    // Sparse gradients and their aggregate exist only when every measured zero point carries an
    // uncertainty. Keeping the value-only path compact matters to stack preparation, where older
    // catalogs often have incomplete uncertainty metadata and no propagated uncertainty can be
    // promised.
    const std::uint64_t workingBytes = photometricWeightingWorkingBytes(frameCount, result.uncertaintyComplete);
    const std::uint64_t rankingBytesPerFrame = result.uncertaintyComplete ? sizeof(RankedWeight) : sizeof(double);
    Result<plane_detail::ScopedContextMemory> workspaceResult =
        plane_detail::ScopedContextMemory::take(execution, workingBytes, alignof(WorkingWeight), kOperation);
    if (!workspaceResult.hasValue())
    {
        return workspaceResult.status();
    }
    plane_detail::ScopedContextMemory workspace = std::move(workspaceResult).value();
    auto* const working = static_cast<WorkingWeight*>(workspace.data());
    auto* const rankingStorage = reinterpret_cast<std::byte*>(working + frameCount);
    auto* const scalarRanks = reinterpret_cast<double*>(rankingStorage);
    auto* const ranked = result.uncertaintyComplete ? reinterpret_cast<RankedWeight*>(rankingStorage) : nullptr;
    auto* const gradients = result.uncertaintyComplete
                                ? reinterpret_cast<SparseGradient*>(
                                      rankingStorage + static_cast<std::size_t>(frameCount) * rankingBytesPerFrame)
                                : nullptr;
    auto* const aggregateGradient =
        result.uncertaintyComplete ? reinterpret_cast<double*>(gradients + frameCount) : nullptr;
    auto* const propagatedUncertainty =
        result.uncertaintyComplete ? reinterpret_cast<double*>(aggregateGradient + frameCount) : nullptr;
    result.workingBytes = workspace.bytes();

    std::uint32_t rmsCount = 0u;
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        if (result.uncertaintyComplete)
        {
            aggregateGradient[index] = 0.0;
        }
        if (measurements[index].zeroPointMagnitudes.has_value() &&
            measurements[index].astrometricRootMeanSquarePixels.has_value())
        {
            scalarRanks[rmsCount++] = *measurements[index].astrometricRootMeanSquarePixels;
        }
    }
    if (rmsCount > 0u)
    {
        const double medianRms = selectScalarMedian(scalarRanks, rmsCount);
        if (!std::isfinite(medianRms) || medianRms <= 0.0)
        {
            return weightingFailure(ErrorCode::InvalidData,
                                    "the median astrometric RMS is not a finite positive value");
        }
        result.medianAstrometricRootMeanSquarePixels = medianRms;
    }

    std::uint32_t rankedCount = 0u;
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        const PhotometricFrameMeasurement& measurement = measurements[index];
        if (!measurement.zeroPointMagnitudes.has_value())
        {
            continue;
        }

        const double transparency = std::pow(10.0, 0.4 * (*measurement.zeroPointMagnitudes - minimumZeroPoint));
        double confidence = 1.0;
        if (measurement.zeroPointStandardUncertaintyMagnitudes.has_value())
        {
            confidence = 1.0 / (1.0 + request.uncertaintyPenaltyPerMagnitude *
                                          *measurement.zeroPointStandardUncertaintyMagnitudes);
        }
        double alignment = 1.0;
        if (result.medianAstrometricRootMeanSquarePixels.has_value() &&
            measurement.astrometricRootMeanSquarePixels.has_value() &&
            *measurement.astrometricRootMeanSquarePixels > *result.medianAstrometricRootMeanSquarePixels)
        {
            const double excess =
                *measurement.astrometricRootMeanSquarePixels / *result.medianAstrometricRootMeanSquarePixels - 1.0;
            alignment = 1.0 / (1.0 + request.alignmentPenalty * excess * excess);
        }

        double raw = transparency * confidence * alignment;
        if (!std::isfinite(raw) || raw < 0.0)
        {
            return weightingFailure(ErrorCode::InvalidData,
                                    "the photometric model produced an unrepresentable raw weight");
        }
        WorkingWeight& item = working[index];
        item = WorkingWeight{};
        if (result.uncertaintyComplete)
        {
            gradients[index] = SparseGradient{};
        }
        if (raw < request.minimumRawWeight)
        {
            raw = request.minimumRawWeight;
            item.floored = true;
            ++result.minimumAppliedCount;
        }
        else if (result.uncertaintyComplete && raw > 0.0)
        {
            addGradientTerm(gradients[index], index, kMagnitudeDerivative * raw);
            addGradientTerm(gradients[index], minimumZeroPointIndex, -kMagnitudeDerivative * raw);
        }
        item.raw = raw;
        if (result.uncertaintyComplete)
        {
            ranked[rankedCount++] = RankedWeight{.value = raw, .inputIndex = index};
        }
        else
        {
            scalarRanks[rankedCount++] = raw;
        }
    }

    double medianRaw = 0.0;
    if (result.uncertaintyComplete)
    {
        medianRaw = selectRankedMedian(ranked, rankedCount);
    }
    else
    {
        medianRaw = selectScalarMedian(scalarRanks, rankedCount);
    }
    if (!std::isfinite(medianRaw) || medianRaw < 0.0)
    {
        return weightingFailure(ErrorCode::InvalidData,
                                "the measured raw-weight median is not representable in Float64");
    }
    SparseGradient medianGradient;
    if (result.uncertaintyComplete)
    {
        const std::uint32_t middle = rankedCount / 2u;
        if ((rankedCount % 2u) != 0u)
        {
            addScaledGradient(medianGradient, gradients[ranked[middle].inputIndex], 1.0);
        }
        else
        {
            addScaledGradient(medianGradient, gradients[ranked[middle - 1u].inputIndex], 0.5);
            addScaledGradient(medianGradient, gradients[ranked[middle].inputIndex], 0.5);
        }
    }

    double rawSum = 0.0;
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        if (!measurements[index].zeroPointMagnitudes.has_value())
        {
            working[index] = WorkingWeight{.raw = medianRaw, .substituted = true};
            if (result.uncertaintyComplete)
            {
                gradients[index] = medianGradient;
            }
            ++result.medianSubstitutionCount;
        }
        const double next = rawSum + working[index].raw;
        if (!std::isfinite(next))
        {
            return weightingFailure(ErrorCode::InvalidData,
                                    "the input-order raw-weight sum is not representable in Float64");
        }
        rawSum = next;
    }
    if (rawSum <= 0.0)
    {
        status = execution.checkCancellation();
        if (status.failed())
        {
            return status;
        }
        return writeUniform(execution, output, result, PhotometricWeightingDisposition::UniformDegenerate, true);
    }
    const double rawMean = rawSum / static_cast<double>(frameCount);
    if (!std::isfinite(rawMean) || rawMean <= 0.0)
    {
        return weightingFailure(ErrorCode::InvalidData,
                                "the input-order raw-weight mean is not a finite positive value");
    }

    // The ordinary stack-preparation path has incomplete uncertainty metadata. It can commit the
    // normalized values and diagnostics in one pass: every raw value is finite and non-negative,
    // and raw / mean cannot exceed frameCount because mean is rawSum / frameCount. Consequently no
    // fallible arithmetic remains after this cancellation point and failure atomicity is retained.
    if (!result.uncertaintyComplete)
    {
        status = execution.checkCancellation();
        if (status.failed())
        {
            return status;
        }
        double normalizedSum = 0.0;
        double normalizedSquares = 0.0;
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            const WorkingWeight& item = working[index];
            const double normalized = item.raw / rawMean;
            normalizedSum += normalized;
            normalizedSquares += normalized * normalized;
            output[index] = PhotometricStackWeight{
                .value = normalized, .usedMedianSubstitution = item.substituted, .minimumApplied = item.floored};
        }
        result.disposition = PhotometricWeightingDisposition::Computed;
        result.weightSum = normalizedSum;
        result.effectiveFrameCount = normalizedSum * normalizedSum / normalizedSquares;
        execution.reportProgress(frameCount, frameCount, kProgressStage);
        return result;
    }

    double normalizedSum = 0.0;
    double normalizedSquares = 0.0;
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        WorkingWeight& item = working[index];
        item.raw /= rawMean;
        if (!std::isfinite(item.raw) || item.raw < 0.0)
        {
            return weightingFailure(ErrorCode::InvalidData,
                                    "mean normalization produced an unrepresentable stack weight");
        }
        normalizedSum += item.raw;
        normalizedSquares += item.raw * item.raw;
        if (!std::isfinite(normalizedSum) || !std::isfinite(normalizedSquares))
        {
            return weightingFailure(ErrorCode::InvalidData,
                                    "the normalized weight diagnostics are not representable in Float64");
        }
        if (result.uncertaintyComplete)
        {
            for (std::uint32_t term = 0u; term < gradients[index].count; ++term)
            {
                GradientTerm gradient = gradients[index].terms[term];
                aggregateGradient[gradient.inputIndex] += gradient.derivative;
                if (!std::isfinite(aggregateGradient[gradient.inputIndex]))
                {
                    return weightingFailure(ErrorCode::InvalidData,
                                            "the uncertainty gradient is not representable in Float64");
                }
            }
        }
    }

    // First-order propagation through the normalization. With `N` frames, raw weights `r` summing to
    // `S`, and `w_i = N r_i / S`, the derivative of `w_i` by zero point `k` is
    // `(N / S) (g_ik - f_i G_k)`, where `g_ik` is `r_i`'s sparse derivative, `G_k` the aggregate
    // derivative of every raw weight and `f_i = r_i / S = w_i / N`. With independent zero-point
    // errors `s_k` its variance expands to `(N / S)^2 (A_i - 2 f_i B_i + f_i^2 C)` - the sparse
    // sum `A_i` of `g_ik^2 s_k^2`, the cross sum `B_i` of `g_ik G_k s_k^2` over the same few terms,
    // and the common sum `C` of `G_k^2 s_k^2` over every measured frame - so each weight costs its
    // sparse terms and the whole propagation stays linear in the frame count.
    if (result.uncertaintyComplete)
    {
        double commonVariance = 0.0;
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            if (!measurements[index].zeroPointMagnitudes.has_value())
            {
                continue;
            }
            const double sigma = *measurements[index].zeroPointStandardUncertaintyMagnitudes;
            const double contribution = aggregateGradient[index] * sigma;
            commonVariance += contribution * contribution;
            if (!std::isfinite(commonVariance))
            {
                return weightingFailure(ErrorCode::InvalidData,
                                        "the common uncertainty term is not representable in Float64");
            }
        }

        const double normalizationDerivativeScale = static_cast<double>(frameCount) / rawSum;
        for (std::uint32_t index = 0u; index < frameCount; ++index)
        {
            const WorkingWeight& item = working[index];
            const SparseGradient& gradient = gradients[index];
            double sparseVariance = 0.0;
            double crossVariance = 0.0;
            for (std::uint32_t term = 0u; term < gradient.count; ++term)
            {
                const GradientTerm termGradient = gradient.terms[term];
                const double sigma = *measurements[termGradient.inputIndex].zeroPointStandardUncertaintyMagnitudes;
                const double variance = sigma * sigma;
                sparseVariance += termGradient.derivative * termGradient.derivative * variance;
                crossVariance += termGradient.derivative * aggregateGradient[termGradient.inputIndex] * variance;
            }
            const double rawFraction = item.raw / static_cast<double>(frameCount);
            double varianceInside =
                sparseVariance - 2.0 * rawFraction * crossVariance + rawFraction * rawFraction * commonVariance;
            if (!std::isfinite(varianceInside))
            {
                return weightingFailure(ErrorCode::InvalidData,
                                        "a propagated weight uncertainty is not representable in Float64");
            }
            varianceInside = (std::max)(0.0, varianceInside);
            propagatedUncertainty[index] = normalizationDerivativeScale * std::sqrt(varianceInside);
            if (!std::isfinite(propagatedUncertainty[index]))
            {
                return weightingFailure(ErrorCode::InvalidData,
                                        "a propagated weight uncertainty is not representable in Float64");
            }
        }
    }

    status = execution.checkCancellation();
    if (status.failed())
    {
        return status;
    }
    for (std::uint32_t index = 0u; index < frameCount; ++index)
    {
        output[index] = PhotometricStackWeight{
            .value = working[index].raw,
            .standardUncertainty =
                result.uncertaintyComplete ? std::optional<double>{propagatedUncertainty[index]} : std::nullopt,
            .usedMedianSubstitution = working[index].substituted,
            .minimumApplied = working[index].floored};
    }
    result.disposition = PhotometricWeightingDisposition::Computed;
    result.weightSum = normalizedSum;
    result.effectiveFrameCount = normalizedSum * normalizedSum / normalizedSquares;
    execution.reportProgress(frameCount, frameCount, kProgressStage);
    return result;
}

std::uint64_t photometricWeightingWorkingBytes(std::uint32_t frames, bool uncertaintyComplete) noexcept
{
    const std::uint64_t ranking = uncertaintyComplete ? sizeof(RankedWeight) : sizeof(double);
    const std::uint64_t uncertainty = uncertaintyComplete ? sizeof(SparseGradient) + 2u * sizeof(double) : 0u;
    return frames <= 1u ? 0u : static_cast<std::uint64_t>(frames) * (sizeof(WorkingWeight) + ranking + uncertainty);
}

} // namespace an
