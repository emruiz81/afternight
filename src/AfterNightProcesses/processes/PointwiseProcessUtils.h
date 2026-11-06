// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the pointwise and tone processes share: the units they work in, samples held in those units for an engine
/// operation to work on in place, and the record a result keeps of the nonlinear process it went through.
///
/// **The units.** Every one of these processes reads a sample as a fraction of full scale: an unsigned integer divided
/// by its largest value, a signed one counted from its smallest value to its largest, a floating sample as it is. The
/// engine's pointwise programs, stretches and colour adjustments take floating samples, so `NormalizedSamples` holds a
/// plane or an image in those units: floating samples are worked on where they are, and integer ones are converted
/// into a floating working image that `commit` narrows back to their own representation - to the nearest value, ties
/// to even, saturated at the representation's range.
///
/// **Which samples are worked on** is the source's validity, restated for the working image: an explicit mask as it is,
/// and a rule an integer's value decides - exact zero, a stated range - judged on the stored samples once and carried
/// as a mask, so a pixel the rule rejects is left as it was.
///
/// **The output of every one of these processes is recorded nonlinear**: `recordNonlinearResult` states it on the
/// processing-state cards a saved file carries, names the process there and in the result's lineage, and no longer
/// claims a linear encoding for it.
///
/// Everything here runs on a run's worker, under its admitted execution.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Kernel/ReferenceKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/ToneCurve.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ImageViewControl;

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace process_utils
{

/// The floating representation samples of a representation are worked on in: Float64 for Float64 and for the two
/// 32-bit integers, whose neighbouring codes Float32 cannot tell apart, and Float32 for the rest.
///
/// @param stored the samples' own representation.
/// @return Float32 or Float64.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::SampleType normalizedWorkingType(an::SampleType stored) noexcept;

/// The shape of a pointwise program over working samples of a representation: their width, the normalized domain
/// fractions of full scale are in, and Float64 arithmetic only over Float64 samples.
///
/// @param working Float32 or Float64.
/// @param channels the channels the program runs over.
/// @return the shape.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::PointwisePlanShape normalizedPlanShape(an::SampleType working,
                                                                                 std::uint32_t channels) noexcept;

/// The shape of a tone program over working samples of a representation, from the normalized linear domain to an
/// encoded nonlinear one: the representation itself when it is one a tone program reads directly - an unsigned eight-
/// or sixteen-bit integer, which a tone program turns into an exact table over every code - and the working
/// representation otherwise.
///
/// @param representation the samples the program runs over.
/// @param channels the channels it runs over.
/// @return the shape.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::TonePlanShape normalizedToneShape(an::SampleType representation,
                                                                            std::uint32_t channels) noexcept;

/// The same validity with a sample that holds no number counted out, for an operation that refuses one: an image whose
/// rule calls every sample valid is worked on as one whose rule calls only the finite ones valid, so a pixel holding
/// not-a-number or an infinity is left as it was rather than refused.
///
/// @param validity the validity the samples state.
/// @return the validity to work under.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::KernelValidityView
skippingNonFinite(const an::KernelValidityView& validity) noexcept;

/// Judges samples' validity together with their finiteness into one mask: a pixel is admitted when the samples' rule -
/// or the plane of their mask it names - admits it and every one of its samples holds a finite number. What an
/// operation that refuses a sample holding no number works under when the samples state a rule other than all-valid
/// or finite-only, and what a measurement of the same samples is taken under so that it selects what the operation
/// does.
///
/// @param engine the context whose memory holds the mask.
/// @param execution the run's admitted execution.
/// @param type the samples' representation.
/// @param samples the first sample.
/// @param layout where the samples sit.
/// @param validity which samples mean something, as the samples state it; the bits of its mask are read for the call.
/// @return a one-plane mask of the samples' width and height; or what describing, holding or judging it refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ValidityMask>
judgeValidAndFinite(const an::EngineContext& engine, const an::ExecutionContext& execution, an::SampleType type,
                    const void* samples, const an::ImageLayout& layout, const an::KernelValidityView& validity);

/// A plane or an image in fractions of full scale, writable in place, planar.
///
/// Floating samples are lent as they are; integer samples are converted into a working image of
/// `normalizedWorkingType`, which `commit` narrows back into them. Move-only; the samples it was opened over must
/// outlive it.
class AFTERNIGHTPROCESSES_API NormalizedSamples
{
public:
    /// Holds nothing.
    NormalizedSamples() noexcept;

    ~NormalizedSamples();

    /// Takes over another.
    NormalizedSamples(NormalizedSamples&& other) noexcept;

    /// Lets this go and takes over another.
    NormalizedSamples& operator=(NormalizedSamples&& other) noexcept;

    NormalizedSamples(const NormalizedSamples&) = delete;
    NormalizedSamples& operator=(const NormalizedSamples&) = delete;

    /// Opens planar samples for work in place.
    ///
    /// @param engine the run's engine context, whose memory holds a working image.
    /// @param execution the run's admitted execution.
    /// @param described what the samples are.
    /// @param samples the first sample, writable; borrowed until `commit`.
    /// @param layout where the samples sit; planar. Borrowed until `commit`.
    /// @param validity which samples mean something, as the samples state it; the bits of its mask are borrowed until
    ///        `commit`, its view is not.
    /// @return the samples in fractions of full scale; `ErrorCode::IncompatibleArguments` for samples that are not
    ///         planar; or what the working image, the mask a value rule is judged into, or the conversion refused.
    [[nodiscard]] static an::Result<NormalizedSamples>
    open(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::ImageDescriptor& described,
         void* samples, const an::ImageLayout& layout, const an::KernelValidityView& validity);

    /// Opens samples read from one place for work whose result is written to another: integer samples are converted
    /// from `source` into the working image, which `commit` narrows into `samples`; floating ones are copied from
    /// `source` into `samples` and worked on there. What a body handed a destination that starts unwritten opens.
    ///
    /// @param engine the run's engine context, whose memory holds a working image.
    /// @param execution the run's admitted execution.
    /// @param described what the samples are, at both places.
    /// @param source the samples read, planar; borrowed for the call.
    /// @param sourceLayout where they sit.
    /// @param samples where the result goes, writable; borrowed until `commit`.
    /// @param layout where that is; planar, of the source's extent. Borrowed until `commit`.
    /// @param validity which samples mean something, as the source states it; the bits of its mask are borrowed until
    ///        `commit`, its view is not.
    /// @return the samples in fractions of full scale; `ErrorCode::IncompatibleArguments` for samples that are not
    ///         planar or of different extents; or what the working image, the mask, the copy or the conversion
    ///         refused.
    [[nodiscard]] static an::Result<NormalizedSamples>
    open(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::ImageDescriptor& described,
         const void* source, const an::ImageLayout& sourceLayout, void* samples, const an::ImageLayout& layout,
         const an::KernelValidityView& validity);

    /// Whether this holds samples.
    [[nodiscard]] bool valid() const noexcept;

    /// The working representation, Float32 or Float64.
    [[nodiscard]] an::SampleType sampleType() const noexcept;

    /// The working samples, writable.
    [[nodiscard]] void* samples() const noexcept;

    /// Where the working samples sit; planar.
    [[nodiscard]] const an::ImageLayout& layout() const noexcept;

    /// Which working samples mean something: the samples' own validity, with a rule an integer's value decides carried
    /// as a mask. Valid while this lives.
    [[nodiscard]] an::KernelValidityView validity() const noexcept;

    /// Which working samples an operation that refuses a sample holding no number works on: these samples' validity
    /// with every such sample counted out, so a pixel holding not-a-number or an infinity is left as it was whatever
    /// the rule. A rule calling every sample valid becomes one calling the finite ones valid; any other rule over
    /// floating samples is judged with their finiteness, once, into a mask this keeps. Valid while this lives.
    ///
    /// @param execution the run's admitted execution.
    /// @return the validity to work under; or what judging the mask refused.
    [[nodiscard]] an::Result<an::KernelValidityView> skippingNonFinite(const an::ExecutionContext& execution);

    /// Narrows the working samples back into the samples this was opened over, when they were converted; nothing for
    /// floating samples, which were worked on in place.
    ///
    /// @param execution the run's admitted execution.
    /// @return success, or what the narrowing refused.
    [[nodiscard]] an::Status commit(const an::ExecutionContext& execution);

private:
    struct State;

    explicit NormalizedSamples(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> m_state;
};

/// The whole of a run's destination in fractions of full scale, under the source's validity, for an operation that
/// works on the whole image rather than on the channel the selector names. The destination must keep its image until
/// the samples are committed.
///
/// A destination that holds the source is opened where it is. One that starts unwritten is opened over the source,
/// read where it lies, and committed into the destination - an integer image's working copy is converted from the
/// source rather than from a copy of it - or, over a source that is not planar, brought to hold the source first.
///
/// @param scope the run's admitted execution.
/// @param source what the run reads, for its validity and, into a destination that starts unwritten, its samples.
/// @param destination what the run writes, planar.
/// @return the samples; or what opening them refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<NormalizedSamples>
openNormalizedDestination(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source,
                          an::ui::ProcessDestination& destination);

/// A chain of pointwise programs built for a plane's working shape, run in that order.
using PointwiseChain = std::vector<an::PointwiseProgram>;

/// Builds a chain for the shape of a plane's working samples.
using PointwiseChainBuilder = std::function<an::Result<PointwiseChain>(const an::PointwisePlanShape& shape)>;

/// Runs a chain of pointwise programs over the plane a channel scope hands a process, in fractions of full scale: the
/// chain built for the plane's working shape is fused into one pass, read from the input where it lies - from the
/// output, which then starts as its copy, when the input is not planar - run under the input's validity and written to
/// every sample of the output it reaches. Floating samples are already in those units and are read and written as
/// they are; integer ones are converted, worked on and narrowed back a band of rows at a time, each band by one worker
/// in a working band of its own, so no working copy of the plane is ever made and every sample comes out as the
/// whole plane's working copy would have left it.
///
/// An eight- or sixteen-bit plane, signed or not, every sample of which is valid, at least as large as its
/// representation has codes, under a chain of scales, biases and clamps that answers a code alike in every channel,
/// takes those same steps once over a row of every code and is then looked up code by code: the same samples, without
/// a working copy. A signed code is looked up by its two's-complement bit pattern, which is the code offset by the
/// representation's span when it is negative.
///
/// @param scope the run's admitted execution.
/// @param input what the scope hands over: the samples read, and their validity.
/// @param output where the process writes, planar.
/// @param build the chain, for the working shape; at least one program.
/// @return success; or what opening the samples, building, fusing, running or narrowing refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status runNormalizedChain(const an::ui::ExecutionScope& scope,
                                                                    const an::ChannelScopeInput& input,
                                                                    const an::ChannelScopeOutput& output,
                                                                    const PointwiseChainBuilder& build);

/// The samples a pointwise body reads of the plane a channel scope hands it: the input where it lies, when it is planar
/// in the output's representation, and otherwise the output, which then starts as the input's copy - over a whole image
/// the framework copied because it is not planar.
///
/// @param input what the scope hands over.
/// @param output where the process writes.
/// @return the samples and where they sit.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::PointwiseImageView
pointwiseReadOf(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output) noexcept;

/// What a history step says of the channel a run ran on: " in channel: " and the selector's label for a colour
/// target, and nothing for a grey one or a target that holds nothing.
///
/// @param target the process's target view, or null.
/// @param scope the channel the selector names.
/// @return the suffix.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::string channelStepSuffix(const ImageViewControl* target,
                                                                    an::ChannelScope scope);

/// Whether a target view holds a colour image: red, green and blue, with or without alpha.
///
/// @param target the view, or null.
/// @return false for a grey image, a view that holds nothing and a null view.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool holdsColourImage(const ImageViewControl* target);

/// What a process says it did, for its result's record.
struct NonlinearResultRecord
{
    /// The name the operation is given on the processing-state cards, `ANOP`: "gamma-correction",
    /// "brightness-contrast" and so on.
    std::string stateOperation;

    /// The engine operation the process ran, as the engine spells it, for the result's lineage.
    std::string engineOperation;

    /// What the process ran it with, for the lineage.
    std::string detail;

    /// The validity the result states instead of its source's - a result whose every sample the process wrote means
    /// something - with the source's packed mask dropped; nothing keeps the source's.
    std::optional<an::ValidityDescriptor> restatedValidity{};
};

/// Records on a run's result that it has been through a nonlinear process: its processing-state cards say nonlinear,
/// stated by a process, in the basis a stretched image of its description is in, and name the operation; its meaning
/// claims no linear encoding any more; and its lineage names the engine operation and the parameters. Everything else -
/// the solution, the acquisition record, every other card, the validity unless the record restates it - is the
/// source's, as the destination started.
///
/// All of it is stated where the result is, in one metadata transition: the samples stay where the process wrote them,
/// and nothing of the result is copied to carry its new cards.
///
/// @param destination the run's result, written; it keeps its image and its samples.
/// @param record what the process did.
/// @return success; or what reading or stating the record refused. On a failure the destination is unchanged.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status recordNonlinearResult(an::ui::ProcessDestination& destination,
                                                                       const NonlinearResultRecord& record);

} // namespace process_utils
