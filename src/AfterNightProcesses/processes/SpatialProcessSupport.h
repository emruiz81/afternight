// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the spatial and noise processes share: the selection an engine filter reads the plane a channel scope hands a
/// process under, a filter's answer read as a status, and the processing state a process records on its result when it
/// leaves the samples nonlinear.
///
/// **A plane is filtered as the scope hands it over.** The engine's neighbourhood filters take the samples where they
/// lie - every resident representation, any arrangement - so a process hands a channel scope's input and output to its
/// filter directly; nothing is copied or converted on the way. What the scope says about which samples mean something
/// travels with them: a mask with one plane per channel is narrowed to the plane the scope handed over, so the filter
/// reads the channel's own validity rather than the first channel's.
#pragma once

#include "AfterNightProcessesExport.h"
#include "controls/ProcessExecution.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Algorithms/Spatial/SpatialFilter.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ValidityMaskView.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class ImageViewControl;
class QComboBox;

namespace an
{
class EngineContext;
class ExecutionContext;
} // namespace an

namespace process_utils
{

/// The words a history step adds for the channel a colour image's run took: " in channel: " and the selector's text.
/// Empty for a grey image, a view with no image and a dialog with no selector. GUI thread.
///
/// @param target the view the process targets, or null.
/// @param selector the dialog's channel selector, or null.
/// @return the words, or an empty string.
[[nodiscard]] std::string channelStepSuffix(const ImageViewControl* target, const QComboBox* selector);

/// Puts a colour image's channel selector back on the whole image, as a dialog's Reset does; a grey image's selector
/// has nothing else to offer. GUI thread.
///
/// @param target the view the process targets, or null.
/// @param selector the dialog's channel selector, or null.
void selectWholeImageChannel(const ImageViewControl* target, QComboBox* selector);

/// How a selection reads a sample that is not a finite number in an image that states every sample valid.
enum class NonFiniteSamples : std::uint8_t
{
    /// As the image states it: a sample like any other, which a filter that ranks its neighbours refuses.
    AsStated,

    /// As a sample that means nothing: a floating image is read under the rule that a non-finite sample is not valid,
    /// so a filter that ranks its neighbours ranks the finite ones and leaves a non-finite sample where it is. An
    /// integer image, every sample of which is a finite number, is read under its own rule, which reads the same
    /// samples and leaves the engine its faster paths. An image that states a rule of its own is read under that rule.
    MeanNothing
};

/// The selection an engine filter reads one channel scope's input under: the scope's validity rule and its mask, a
/// mask of one plane per channel narrowed to the plane the scope handed over.
///
/// Holds the narrowed view the selection points at, so it is neither copied nor moved; build it where the filter is
/// called and let it go after.
class AFTERNIGHTPROCESSES_API PlaneSelection
{
public:
    /// The selection of a scope's input.
    ///
    /// @param input what the scope handed the process; borrowed, and it must outlive this selection.
    /// @param nonFinite how a non-finite sample of an image that states every sample valid is read.
    explicit PlaneSelection(const an::ChannelScopeInput& input,
                            NonFiniteSamples nonFinite = NonFiniteSamples::AsStated) noexcept;

    PlaneSelection(const PlaneSelection&) = delete;
    PlaneSelection& operator=(const PlaneSelection&) = delete;
    PlaneSelection(PlaneSelection&&) = delete;
    PlaneSelection& operator=(PlaneSelection&&) = delete;

    /// What the filter reads the input under.
    [[nodiscard]] const an::SpatialSelection& selection() const noexcept
    {
        return m_selection;
    }

private:
    std::optional<an::ValidityMaskView<const std::uint8_t>> m_plane;
    an::SpatialSelection m_selection;
};

/// A selection over an image's first planes alone: a mask with a plane per channel narrowed to the planes kept, so a
/// filter over a colour image's red, green and blue reads their validity and not its alpha's.
///
/// Holds the narrowed view the selection points at, so it is neither copied nor moved.
class LeadingPlanesSelection
{
public:
    /// The selection of an image's first planes.
    ///
    /// @param whole the whole image's selection; borrowed, and it must outlive this one.
    /// @param planes how many leading planes are filtered.
    /// @param extent the extent of the planes filtered, their channel count `planes`.
    LeadingPlanesSelection(const an::SpatialSelection& whole, std::uint32_t planes,
                           const an::ImageExtent& extent) noexcept;

    LeadingPlanesSelection(const LeadingPlanesSelection&) = delete;
    LeadingPlanesSelection& operator=(const LeadingPlanesSelection&) = delete;
    LeadingPlanesSelection(LeadingPlanesSelection&&) = delete;
    LeadingPlanesSelection& operator=(LeadingPlanesSelection&&) = delete;

    /// What the filter reads the planes under.
    [[nodiscard]] const an::SpatialSelection& selection() const noexcept
    {
        return m_selection;
    }

private:
    std::optional<an::ValidityMaskView<const std::uint8_t>> m_planes;
    an::SpatialSelection m_selection;
};

/// How a representation's stored values map onto the unit interval every sample is normalized into for the filters
/// whose parameters are fractions of full scale: a floating sample is already a fraction, and an integer one spans its
/// representation's whole range, the lowest value at zero.
struct UnitScale
{
    /// The stored value a fraction of zero is.
    double offset = 0.0;

    /// How many stored units a fraction of one spans.
    double span = 1.0;
};

/// The unit scale of a representation.
///
/// @param type the representation.
/// @return its scale; a floating representation's is the identity.
[[nodiscard]] UnitScale unitScaleOf(an::SampleType type) noexcept;

/// What an operation over a unit-interval working copy is handed: tightly packed planar Float32 samples - each row
/// right after the one before it and each plane right after the one before it - that it reads and writes in place.
using UnitFloatOperation = std::function<an::Status(float* samples, const an::ImageLayout& layout)>;

/// Runs an operation over planar samples as tightly packed Float32 fractions of full scale and keeps what it wrote.
///
/// Samples that are already Float32 and tightly packed are handed over where they lie. Anything else - another
/// representation, or rows padded to the engine's alignment - is converted into a working copy taken from the engine
/// context, at full scale under the engine's default conversion, handed over, and converted back the same way:
/// rounded to nearest even and held at the representation's extremes.
///
/// @param engine the engine context the working copy is taken from.
/// @param execution the run's admitted execution.
/// @param type what the samples are.
/// @param samples the first sample, read and then written.
/// @param layout where the samples sit; planar, one sample per pixel.
/// @param operation what to run over the Float32 samples.
/// @return success; `ErrorCode::IncompatibleArguments` for a layout that is not planar; or what the copy, the
///         conversion or the operation refused. On a failure the samples may be partly written.
[[nodiscard]] an::Status runOverUnitFloat(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                          an::SampleType type, void* samples, const an::ImageLayout& layout,
                                          const UnitFloatOperation& operation);

/// The layout of a planar image's first planes alone: the same rows and the same planes, fewer of them.
///
/// @param layout a planar layout.
/// @param planes how many of its planes, at most its plane count.
/// @param limits the limits of the engine context the layout describes memory of.
/// @return the layout, or nothing when the layout is not planar or holds fewer planes.
[[nodiscard]] std::optional<an::ImageLayout> leadingPlanesLayout(const an::ImageLayout& layout, std::uint32_t planes,
                                                                 const an::EngineLimits& limits);

/// The exact Gaussian blur of a channel scope's plane at any sigma.
///
/// One plan of the engine's Gaussian runs over the stored values of a Float64 plane, of a Float32 plane whose rows are
/// packed tight, or of a plane under a validity rule or a mask, when its widest kernel reaches the sigma. An integer
/// plane with every sample valid, a Float32 plane of padded rows, and any plane a wider blur is composed over, is
/// worked on a unit-interval Float32 working copy of the output, which starts as the input, and narrowed back to the
/// nearest code: one pass where one kernel reaches the sigma, otherwise as many passes as the widest kernel needs, each
/// a Gaussian whose variance is its share of the sigma's square, each reported as progress. So a Float32 plane is
/// blurred by the same arithmetic whatever its rows' padding.
///
/// @param engine the engine context the working planes are taken from.
/// @param execution the run's admitted execution.
/// @param input what the scope handed the process.
/// @param output where it writes; it starts as a copy of the input.
/// @param sigma the standard deviation, in pixels; above `an::kMinimumGaussianSigma`.
/// @param border how a neighbour outside the image is supplied.
/// @return success; or what a plan, the working planes or a pass refused.
[[nodiscard]] an::Status blurGaussian(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                      const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                      double sigma,
                                      an::KernelBorderMode border = an::KernelBorderMode::ReflectExcludingEdge);

/// How far around a pixel `blurGaussian` reads for a sigma: the radius of the one kernel that reaches the sigma, or the
/// radii of every pass of the composition it runs instead, added up - each pass reads that far around the pass before.
/// What a real-time preview's frame over a part of the image states as its reach.
///
/// @param sigma the standard deviation, in pixels.
/// @param border how a neighbour outside the image is supplied.
/// @return the reach in pixels; zero for a sigma the engine builds no kernel for.
[[nodiscard]] std::uint32_t gaussianReach(double sigma,
                                          an::KernelBorderMode border = an::KernelBorderMode::ReflectExcludingEdge);

/// What one of the two detail filters - the high pass and the unsharp mask - asks for, in the fractions of full scale
/// their dialogs speak.
struct DetailRequest
{
    /// `source - blur + offset`, or `source + amount * (source - blur)`.
    an::DetailFilterMethod method = an::DetailFilterMethod::HighPass;

    /// The blur's standard deviation, in pixels.
    double sigma = 1.0;

    /// The unsharp mask's amount; any finite value, a negative one blurring.
    double amount = 1.0;

    /// The high pass's offset, as a fraction of full scale.
    double offset = 0.0;

    /// How a neighbour outside the image is supplied to the blur.
    an::KernelBorderMode border = an::KernelBorderMode::ReflectExcludingEdge;
};

/// One of the two detail filters over a channel scope's plane, its result held to full scale.
///
/// The engine's detail filter runs it over the stored values when one plan can: a sigma its widest kernel reaches and
/// an amount that is not negative. Otherwise it is composed over a unit-interval working copy of the output, which
/// starts as the input: the exact Gaussian in as many passes as the sigma needs - each a Gaussian whose variance is
/// its share of the sigma's square - then the difference, the amount or the offset, and the clamp.
///
/// @param engine the engine context the working planes are taken from.
/// @param execution the run's admitted execution.
/// @param input what the scope handed the process.
/// @param output where it writes; it starts as a copy of the input.
/// @param request the filter.
/// @return success; or what a plan, the working planes or a pass refused.
[[nodiscard]] an::Status filterDetail(const an::EngineContext& engine, const an::ExecutionContext& execution,
                                      const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                      const DetailRequest& request);

/// How far around a pixel `filterDetail` reads for a request: the radius of the engine's detail filter's blur where one
/// plan takes the request, otherwise the reach of the composed blur, as `gaussianReach` states it. What a real-time
/// preview's frame over a part of the image states as its reach.
///
/// @param request the filter.
/// @return the reach in pixels; zero for a sigma the engine builds no kernel for.
[[nodiscard]] std::uint32_t detailReach(const DetailRequest& request);

/// What an engine call answered, as a status: success when it produced its value, its failure otherwise.
///
/// @param result the call's answer.
/// @return the status.
template <typename ValueT> [[nodiscard]] an::Status statusOf(const an::Result<ValueT>& result)
{
    return result.hasValue() ? an::Status::success() : result.status();
}

/// Records on a run's result that the process left its samples nonlinear: the product's processing-state cards say
/// nonlinear, in the channel basis a stretch leaves, under the operation named, and the lineage names the process.
/// Both are stated where the result is, in one metadata transition: the samples stay where the process wrote them,
/// and nothing of the result is copied to carry its new cards.
///
/// @param destination the run's result; it keeps its image and its samples.
/// @param operation the operation the cards and the lineage name.
/// @return success; or what the cards or the lineage refused. On a failure the destination is unchanged.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Status markProcessResultNonlinear(an::ui::ProcessDestination& destination,
                                                                            std::string_view operation);

} // namespace process_utils
