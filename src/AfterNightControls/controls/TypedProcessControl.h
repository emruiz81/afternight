// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "ImageRTPreviewControl.h"

#include <memory>
#include <utility>
#include <vector>

namespace an::ui
{

/// A real-time preview process whose parameters are one value type, snapshotted on the GUI thread before every run.
///
/// The process collects its widgets into a `ParamsT` in `getParams`; every run - a preview, an Apply, a new image, a
/// snapshot, a sequence - carries that snapshot to its worker, so a body never reads a widget. A process that runs on
/// one channel or colour component at a time implements `executeTypedProcessIteration` and calls
/// `executeTypedProcessUsingChannels` from `executeTypedProcessImpl`; one that works on the whole image implements
/// `executeTypedProcessImpl` alone.
template <typename ParamsT> class TypedProcessControl : public ImageRTPreviewControl
{
public:
    using ImageRTPreviewControl::ImageRTPreviewControl;

    /// Collect current UI values into a ParamsT snapshot. Called on the GUI thread.
    [[nodiscard]] virtual ParamsT getParams() const = 0;

protected:
    struct ParamsHolder final : public ProcessExecutionParams
    {
        explicit ParamsHolder(ParamsT value) : params(std::move(value)) {}

        ParamsT params;
    };

    /// The process, on the run's worker, over the snapshotted parameters: reads the source and writes the destination.
    /// The destination starts as a copy of the source unless the run's coverage (`typedProcessBodyCoverage`) declares
    /// that the body writes every sample, when it may start unwritten: `ProcessDestination::holdsSource()` says which,
    /// and `ProcessDestination::holdSource()` copies the source in for a helper that works in place on its destination.
    /// A body reads the destination before writing it only after that check.
    virtual an::Status executeTypedProcessImpl(const ProcessSource& source, ProcessDestination& destination,
                                               const ProcessRun& run, const ParamsT& params, ExecutionScope& scope) = 0;

    /// The process over one plane its channel scope hands over; the default leaves the plane as it starts.
    virtual an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input,
                                                    const an::ChannelScopeOutput& output, const ProcessRun& run,
                                                    const ParamsT& params, ExecutionScope& scope)
    {
        return an::Status::success();
    }

    /// How far the result is blended back toward the source: one keeps the result.
    virtual double getTypedModulationFromParams(const ParamsT& /*params*/)
    {
        return 1.0;
    }

    /// The history step an Apply records for these parameters; the process's name unless the process says otherwise.
    [[nodiscard]] virtual ProcessStep describeTypedProcessStep(const ParamsT& /*params*/) const
    {
        return ProcessStep{.name = getProcessName(), .description = std::string()};
    }

    /// The images a run of these parameters reads beside its target and the target's mask: another view's image the
    /// body reads through `readBeside`. None unless the process says otherwise.
    [[nodiscard]] virtual std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const ParamsT& /*params*/) const
    {
        return {};
    }

    /// What a run of these parameters writes of its destination; see `processBodyCoverage`. A body that returns at once
    /// for an identity setting, leaving the destination as it started, writes part of it for that setting.
    [[nodiscard]] virtual ProcessBodyCoverage typedProcessBodyCoverage(const ParamsT& /*params*/) const
    {
        return ProcessBodyCoverage::WritesPart;
    }

    /// Whether these parameters change nothing - the body's result is its source, sample for sample; see
    /// `isIdentity`. None does unless the process says so.
    [[nodiscard]] virtual bool isTypedIdentity(const ParamsT& /*params*/) const
    {
        return false;
    }

    /// How many pixels beyond the part of the target the preview view shows an interactive frame of these parameters at
    /// full scale reads around it; see `previewRegionMargin`. Zero unless the process says otherwise.
    [[nodiscard]] virtual std::uint32_t typedPreviewRegionMargin(const ParamsT& /*params*/) const
    {
        return 0u;
    }

    /// Runs `executeTypedProcessIteration` over the run's channel of the source, recomposing the destination.
    [[nodiscard]] an::Status executeTypedProcessUsingChannels(const ProcessSource& source,
                                                              ProcessDestination& destination, const ProcessRun& run,
                                                              const ParamsT& params, ExecutionScope& scope)
    {
        const ParamsHolder holder(params);
        return executeProcessUsingChannels(source, destination, run, &holder, scope);
    }

protected:
    ProcessExecutionParamsPtr getProcessParamsForExecution() final
    {
        return std::make_shared<ParamsHolder>(getParams());
    }

    double getModulationFromParams(const ProcessExecutionParams* params) final
    {
        if (!params)
        {
            return 1.0;
        }
        return getTypedModulationFromParams(static_cast<const ParamsHolder*>(params)->params);
    }

    [[nodiscard]] ProcessStep describeProcessStep(const ProcessExecutionParams* params) const final
    {
        if (!params)
        {
            return ImageRTPreviewControl::describeProcessStep(params);
        }
        return describeTypedProcessStep(static_cast<const ParamsHolder*>(params)->params);
    }

    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    imagesRead(const ProcessExecutionParams* params) const final
    {
        if (!params)
        {
            return {};
        }
        return typedImagesRead(static_cast<const ParamsHolder*>(params)->params);
    }

    [[nodiscard]] ProcessBodyCoverage processBodyCoverage(const ProcessExecutionParams* params) const final
    {
        if (!params)
        {
            return ProcessBodyCoverage::WritesPart;
        }
        return typedProcessBodyCoverage(static_cast<const ParamsHolder*>(params)->params);
    }

    [[nodiscard]] bool isIdentity(const ProcessExecutionParams* params) const final
    {
        if (!params)
        {
            return false;
        }
        return isTypedIdentity(static_cast<const ParamsHolder*>(params)->params);
    }

    [[nodiscard]] std::uint32_t previewRegionMargin(const ProcessExecutionParams* params) const final
    {
        if (!params)
        {
            return 0u;
        }
        return typedPreviewRegionMargin(static_cast<const ParamsHolder*>(params)->params);
    }

    an::Status executeProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                       const ProcessRun& run, const ProcessExecutionParams* params,
                                       ExecutionScope& scope) final
    {
        if (!params)
        {
            return an::Status::success();
        }
        return executeTypedProcessIteration(input, output, run, static_cast<const ParamsHolder*>(params)->params,
                                            scope);
    }

    an::Status executeProcessImpl(const ProcessSource& source, ProcessDestination& destination, const ProcessRun& run,
                                  const ProcessExecutionParams* params, ExecutionScope& scope) final
    {
        if (!params)
        {
            return an::Status::success();
        }
        return executeTypedProcessImpl(source, destination, run, static_cast<const ParamsHolder*>(params)->params,
                                       scope);
    }
};

} // namespace an::ui
