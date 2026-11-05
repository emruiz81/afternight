// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Drizzled frames as a stack source: each read renders one frame through its own drizzle, so the
/// stack's combinations, rejection and maps apply to drizzled frames without a dump history.
///
/// `DrizzledStackFrameSource` implements `StackFrameSource` for `executeStack`. Every read runs
/// `executeDrizzle` over one frame of the underlying `DrizzleFrameSource`, as the stack's own
/// execution and within the memory the resident stack has left, copies the result into the stack's
/// loaned slot with unsupported pixels as quiet NaN, and releases every nested buffer before
/// returning. This is a composition the caller chooses rather than aggregate drizzle: where frames
/// have different support, combining per-frame drizzles is not the answer drizzling the sequence
/// jointly gives. `DrizzleStackSourceReport` counts what the renders cost.
#pragma once

#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleExecutor.h>

namespace an
{

/// Work performed by a drizzled stack source, including repeated stack measurement/combine passes.
struct DrizzleStackSourceReport
{
    std::uint64_t renderedFrames = 0u; ///< Complete per-frame drizzles copied into a stack read slot.
    std::uint64_t readRequests = 0u; ///< Underlying stored-band reads across all renders.
    std::uint64_t inputBytesRead = 0u; ///< Logical packed bytes across all renders.
    std::uint64_t maximumDrizzlePlanBytes = 0u; ///< Largest nested drizzle plan, including declared prepared residency.
};

/// A whole-frame source for executeStack, rendering one drizzled frame per read into its loaned buffer.
///
/// Compose this source with the ordinary stack model for median, extrema, auto-adaptive combination,
/// rejection, variance/coverage maps, weighting and normalization of drizzled frames. Direct aggregate
/// drizzle uses executeDrizzle; averaging per-frame means is a separately chosen operation when frames
/// have unequal support. Calibration or normalization in source coordinates belongs in the underlying
/// DrizzleFrameSource before mapping, using the existing prepared source/calibration operations.
///
/// Each read plans and owns one nested drizzle within the context's remaining live budget, copies
/// its output to the stack's slot, then releases it before returning. Missing support is represented
/// there as quiet NaN with explicit FiniteOnly validity; finite zero remains data. No frame history,
/// disk dump or hidden cache is retained. A small remaining budget is a returned memory failure,
/// never a fallback. Both the stack and nested drizzle plans account their own simultaneous buffers.
/// The engine, underlying source and frame records must outlive this source and its reads; the
/// execution does not, because every read is handed the one the stack is running as.
class AFTERNIGHTENGINE_INTERFACE DrizzledStackFrameSource final : public StackFrameSource
{
public:
    /// Builds a borrowed composition after checking geometry against the model. Each render uses a
    /// one-frame copy of the model with no weight map, coverage map or maximum weight, and reads one
    /// frame at a time whatever the request's `concurrentFrameReads` says.
    /// @param engine context owning the nested output allocations and stating the limits.
    /// @param model sequence geometry with WeightedMean; each read renders one frame at the requested
    ///        output precision. PeakNormalizedSum is refused because its peak belongs to the complete sequence.
    /// @param source stored sample reader, kept alive and used exclusively during stack reads.
    /// @param frames ordered geometry and weights, kept immutable throughout this source's use.
    /// @param request nested drizzle caps/residency; artifact identity must be anonymous for intermediates.
    ///        A supplied diagnostics report must outlive this source and its reads; it resets per rendered frame.
    /// @return the source, which takes no context memory; `ErrorCode::NotInitialized` for an
    ///         uninitialized engine; `ErrorCode::IncompatibleArguments` when the frame records do not
    ///         number the model's frames or disagree with its geometry, or the model asks for a
    ///         peak-normalized sum; `ErrorCode::InvalidArgument` for a stated artifact identity;
    ///         `ErrorCode::LimitExceeded` when the output layout exceeds the engine's limits; or any
    ///         refusal `DrizzleModel::create` reports for the one-frame model.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<DrizzledStackFrameSource>
    create(const EngineContext& engine, const DrizzleModel& model, DrizzleFrameSource& source,
           std::span<const DrizzleFrameParameters> frames, const DrizzleExecutionRequest& request = {});
    /// This adapter produces a complete drizzled frame per stack read.
    /// @return WholeFrame.
    [[nodiscard]] AFTERNIGHTENGINE_API StackSourceAccess access() const noexcept override;
    /// Renders one frame, copies it into buffer and releases every nested image before returning.
    /// @param execution the running stack's operation, which the nested drizzle runs as; it takes no
    ///        extra lease and no extra concurrency grant, and its engine context must be this source's.
    /// @param frameIndex ordered frame position.
    /// @param firstRow must be zero.
    /// @param rows must equal the output canvas height.
    /// @param buffer aligned stack slot holding the complete packed planar floating output.
    /// @return stack-owned samples with FiniteOnly validity; `ErrorCode::NotInitialized` for an
    ///         uninitialized execution; `ErrorCode::IncompatibleArguments` for an execution of another
    ///         engine context; `ErrorCode::OutOfRange` for a frame index past the records or a request
    ///         other than the complete frame; `ErrorCode::InvalidArgument` for a null, short or
    ///         misaligned slot; or any failure the nested `executeDrizzle` reports, cancellation and
    ///         memory refusals included.
    [[nodiscard]] AFTERNIGHTENGINE_API Result<StackSourceBand> read(const ExecutionContext& execution,
                                                                    std::uint32_t frameIndex, std::uint32_t firstRow,
                                                                    std::uint32_t rows,
                                                                    std::span<std::byte> buffer) override;
    /// Completed work since construction; no previous operation's figures are inherited.
    /// @return the adapter's actual read and plan counts.
    [[nodiscard]] DrizzleStackSourceReport report() const noexcept
    {
        return m_report;
    }

private:
    /// Retains checked borrowed arguments; allocates nothing.
    /// @param engine memory owner.
    /// @param model one-frame drizzle contract.
    /// @param source underlying reader.
    /// @param frames sequence records.
    /// @param request execution caps.
    /// @param layout complete output layout, stable for the source lifetime.
    DrizzledStackFrameSource(const EngineContext& engine, const DrizzleModel& model, DrizzleFrameSource& source,
                             std::span<const DrizzleFrameParameters> frames, const DrizzleExecutionRequest& request,
                             const ImageLayout& layout)
        : m_engine(&engine), m_model(model), m_source(&source), m_frames(frames), m_request(request), m_layout(layout)
    {
    }
    const EngineContext* m_engine; ///< Borrowed engine owner.
    DrizzleModel m_model; ///< One-frame contract, owned by value.
    DrizzleFrameSource* m_source; ///< Borrowed stored-band reader.
    std::span<const DrizzleFrameParameters> m_frames; ///< Ordered immutable frame records.
    DrizzleExecutionRequest m_request; ///< Nested caps and caller residency.
    ImageLayout m_layout; ///< Stable complete-frame layout for returned stack bands.
    DrizzleStackSourceReport m_report{}; ///< Actual completed work.
};

} // namespace an
