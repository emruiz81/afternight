// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "OneShotProbe.h"

#include "ProbeSupport.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Statistics/RobustStatistics.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/RawSampleFormat.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <atomic>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// The probe's implementation, written the way a command-line tool is written: top to bottom, one step
// after another, every failure turned into an exit code at the step that met it.
//
// Three habits in here are what the engine asks of a caller, and each is worth a sentence.
//
// **Every step starts its own operation.** An execution context carries the stop token and the
// progress sink, and it holds a share of the thread budget for as long as it lives, so a tool that
// kept one for the whole run would be holding the machine through its own file writes. Each step
// builds one and lets it go.
//
// **The working image is a decision the tool writes down.** A sixteen-bit camera frame is sensor codes;
// an automatic stretch is defined on the unit interval. Nothing in the engine turns one into the other
// behind the caller's back, so the tool states the Float32 normalized description and converts into it
// under a full-scale mapping.
//
// **Every image is a local of the step that needs it.** The engine context refuses to shut down while
// any image, execution or pending output holds a lease on it, so the order of the braces below is what
// lets the last line of the run succeed.

namespace probe::cli
{
namespace
{

/// A progress sink that counts what passes through it and hands each report on.
class CountingForwarder final : public an::ProgressSink
{
public:
    /// Builds a forwarder.
    ///
    /// @param next where each report goes after it is counted, or null.
    explicit CountingForwarder(an::ProgressSink* next) noexcept : m_next(next) {}

    /// Counts one report and forwards it.
    ///
    /// @param report what the operation said.
    void onProgress(const an::ProgressReport& report) noexcept override
    {
        m_count.fetch_add(1u, std::memory_order_relaxed);
        if (m_next != nullptr)
        {
            m_next->onProgress(report);
        }
    }

    /// How many reports passed through.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t count() const noexcept
    {
        return m_count.load(std::memory_order_relaxed);
    }

private:
    /// Where reports go next.
    an::ProgressSink* m_next = nullptr;

    /// The count.
    std::atomic<std::uint64_t> m_count{0u};
};

/// A usage failure, which no engine call produced.
///
/// @param message what was wrong with the command line.
/// @return `ErrorCode::InvalidArgument` carrying it.
[[nodiscard]] an::Status usage(std::string message)
{
    return an::Status::failure(an::ErrorCode::InvalidArgument, std::move(message));
}

/// Parses an unsigned decimal option value.
///
/// @param text the value.
/// @return the number, or an empty optional when the text is not one.
[[nodiscard]] std::optional<std::uint64_t> parseUnsigned(std::string_view text) noexcept
{
    std::uint64_t value = 0u;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

/// Which representation the tool writes a container in: the widest one the format is ordinarily
/// read back in by other software.
///
/// This is the tool's policy, not the engine's. The engine will write what it is given and refuses
/// what a codec cannot hold; deciding that a TIFF is sixteen-bit is a product decision.
///
/// @param format the container.
/// @return the representation, or an empty optional for a container this tool does not write.
[[nodiscard]] std::optional<an::SampleType> outputRepresentation(an::CodecFormat format) noexcept
{
    switch (format)
    {
    case an::CodecFormat::Fits:
    case an::CodecFormat::Xisf:
        return an::SampleType::Float32;
    case an::CodecFormat::Tiff:
    case an::CodecFormat::Png:
        return an::SampleType::UInt16;
    case an::CodecFormat::Jpeg:
        return an::SampleType::UInt8;
    default:
        return std::nullopt;
    }
}

/// Starts one step's operation.
///
/// @param engine the context the step runs in.
/// @param stopToken the process's interrupt token.
/// @param progress where the step's progress goes.
/// @return the execution context, or whatever starting it reported.
[[nodiscard]] an::Result<an::ExecutionContext> startStep(const an::EngineContext& engine, std::stop_token stopToken,
                                                         an::ProgressSink& progress)
{
    an::ExecutionOptions options;
    options.stopToken = std::move(stopToken);
    options.progressSink = &progress;
    return an::ExecutionContext::create(engine, options);
}

/// Converts the stretched image into the output representation and publishes it.
///
/// @tparam SampleT the output representation.
/// @param engine the context the conversion and the write run in.
/// @param stopToken the process's interrupt token.
/// @param progress where progress goes.
/// @param stretched the Float32 result.
/// @param outputPath where to write it.
/// @param format the container.
/// @param step set to the exit code of whichever part fails.
/// @param report where the lineage of the written image is recorded.
/// @return success, or the failure.
template <an::ResidentSample SampleT>
[[nodiscard]] an::Status publish(const an::EngineContext& engine, std::stop_token stopToken, an::ProgressSink& progress,
                                 an::Image<float>& stretched, const std::string& outputPath, an::CodecFormat format,
                                 ExitCode& step, RunReport& report)
{
    step = ExitCode::ProcessingFailed;
    const std::optional<an::ImageDescriptor> source = stretched.descriptor();
    const std::optional<an::ImageLayout> sourceLayout = stretched.layout();
    if (!source.has_value() || !sourceLayout.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
    }

    // The output image. For a Float32 container it is the stretched image itself, moved rather than
    // copied; for an integer one it is a conversion into a new image carrying the same metadata.
    an::Image<SampleT> output{};
    if constexpr (std::is_same_v<SampleT, float>)
    {
        output = std::move(stretched);
    }
    else
    {
        const an::Result<an::ImageDescriptor> described =
            an::describeSamplesAs(*source, an::Image<SampleT>::kSampleType);
        if (!described.hasValue())
        {
            return described.status();
        }
        an::Result<an::Image<SampleT>> created = an::Image<SampleT>::createDestination(
            engine, *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, stretched);
        if (!created.hasValue())
        {
            return created.status();
        }
        output = std::move(*created);
        {
            an::Result<an::ExecutionContext> execution = startStep(engine, stopToken, progress);
            if (!execution.hasValue())
            {
                return execution.status();
            }
            const std::optional<an::ImageWriteAccess<SampleT>> access = output.write();
            if (!access.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "the output image holds nothing");
            }
            an::ConversionCounters counters;
            const an::Status converted =
                an::convertImageSamples(*execution, an::SampleType::Float32, stretched.storage().data(), *sourceLayout,
                                        an::Image<SampleT>::kSampleType, access->view().data(), access->view().layout(),
                                        an::conversionPolicyFor(described->numericDomain()), counters);
            if (converted.failed())
            {
                return converted;
            }
        }
        stretched.release();
    }

    step = ExitCode::OutputFailed;
    const an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::Image<SampleT>::kSampleType});
    if (!intent.hasValue())
    {
        return intent.status();
    }
    an::Result<an::RawFrame> frame = an::borrowImageFrame(output);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    an::Result<an::ExecutionContext> execution = startStep(engine, stopToken, progress);
    if (!execution.hasValue())
    {
        return execution.status();
    }
    const an::Result<an::OutputReport> written = an::writeImage(
        engine, *execution, an::OutputRequest{.destinationPath = outputPath, .format = format, .intent = &*intent},
        *frame, output.metadata());
    if (!written.hasValue())
    {
        return written.status();
    }
    // The transaction owns no image, so the lineage entry naming the destination is applied here, to
    // the image that was written - which is the only place a tool would read it back from.
    const an::Status recorded = output.transition(written->transition);
    if (recorded.failed())
    {
        return recorded;
    }
    report.outputFormat = format;
    report.outputSampleType = an::Image<SampleT>::kSampleType;
    for (const an::ProvenanceEntry& entry : output.metadata().provenance().entries())
    {
        report.outputLineage.push_back(entry.operation() + ": " + entry.detail());
    }
    return an::Status::success();
}

/// Every step between starting the engine and stopping it.
///
/// A separate function so that every image, execution and frame it creates is gone when it returns,
/// which is what the shutdown after it needs.
///
/// @param engine the running context.
/// @param invocation what to do.
/// @param stopToken the process's interrupt token.
/// @param progress where progress goes.
/// @param step set to the exit code of whichever step fails.
/// @param report where the run's facts are recorded.
/// @return success, or the failure of the step `step` names.
[[nodiscard]] an::Status process(const an::EngineContext& engine, const Invocation& invocation,
                                 std::stop_token stopToken, an::ProgressSink& progress, ExitCode& step,
                                 RunReport& report)
{
    // Step 0: the output container, decided before anything is read. A run that would compute for a
    // minute and then refuse the file name has wasted the minute.
    step = ExitCode::Usage;
    const an::CodecFormat format = an::codecFormatFromPath(invocation.outputPath);
    const std::optional<an::SampleType> representation = outputRepresentation(format);
    const std::optional<an::CodecCapabilities> capabilities =
        engine.codecRegistry() == nullptr ? std::nullopt : engine.codecRegistry()->capabilities(format);
    if (!representation.has_value() || !capabilities.has_value() ||
        (capabilities->encodeSamples & an::codecSampleBit(an::rawSampleFormatOf(*representation))) == 0u)
    {
        return usage("no installed codec writes '" + invocation.outputPath + "'");
    }

    // Step 1: decode, and widen into the working image.
    step = ExitCode::InputFailed;
    an::Image<float> working{};
    {
        an::Result<an::ExecutionContext> execution = startStep(engine, stopToken, progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(engine, *execution, an::CodecReadRequest{.path = invocation.inputPath});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::RawFrameDescriptor> raw = decoded->frame.descriptor();
        const std::optional<an::ImageLayout> sourceLayout = decoded->frame.layout();
        const std::optional<an::SampleType> sourceType = raw.has_value() ? raw->residentSampleType() : std::nullopt;
        const std::optional<an::ImageDescriptor> source = raw.has_value() ? raw->toImageDescriptor() : std::nullopt;
        if (!sourceLayout.has_value() || !sourceType.has_value() || !source.has_value())
        {
            return an::Status::failure(an::ErrorCode::Unsupported,
                                       "the input holds samples this engine does not process");
        }

        step = ExitCode::ProcessingFailed;
        an::ImageDescriptorRequest request = source->toRequest();
        request.sampleType = an::SampleType::Float32;
        request.numericDomain = an::NumericDomain::NormalizedLinear;
        const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
        if (!described.has_value())
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "the input has no Float32 normalized description");
        }
        an::Result<an::Image<float>> created =
            an::Image<float>::create(engine, *described, an::StorageArrangement::Planar,
                                     an::StorageInitialization::Uninitialized, std::move(decoded->metadata));
        if (!created.hasValue())
        {
            return created.status();
        }
        working = std::move(*created);
        const std::optional<an::ImageWriteAccess<float>> access = working.write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the working image holds nothing");
        }
        an::ConversionPolicy policy;
        policy.domain = an::DomainMapping::FullScale;
        an::ConversionCounters counters;
        const an::Status converted = an::convertImageSamples(
            *execution, *sourceType, decoded->frame.bytes().data(), *sourceLayout, an::SampleType::Float32,
            access->view().data(), access->view().layout(), policy, counters);
        if (converted.failed())
        {
            return converted;
        }
    }

    // Step 2: measure. One median and one deviation per channel, which is what the automatic stretch
    // is derived from.
    const std::optional<an::ImageDescriptor> workingDescriptor = working.descriptor();
    const std::optional<an::ImageLayout> workingLayout = working.layout();
    if (!workingDescriptor.has_value() || !workingLayout.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the working image holds nothing");
    }
    std::vector<an::StretchChannelStatistics> channels;
    {
        an::Result<an::ExecutionContext> execution = startStep(engine, stopToken, progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        for (std::uint32_t channel = 0u; channel < workingDescriptor->extent().channelCount(); ++channel)
        {
            const an::Result<an::RobustStatistics> statistics = an::computeImageRobustStatistics(
                *execution, an::SampleType::Float32, working.storage().data(), *workingLayout, channel,
                an::StatisticsSelection{}, an::RobustStatisticsRequest{.medianAbsoluteDeviation = true});
            if (!statistics.hasValue())
            {
                return statistics.status();
            }
            channels.push_back(an::StretchChannelStatistics{
                .median = statistics->median, .medianAbsoluteDeviation = statistics->medianAbsoluteDeviation});
        }
    }

    // Step 3: stretch into a second image.
    const an::Result<an::MidtonesTransferParameters> transfer =
        an::deriveLinkedAutoStretch(channels, invocation.shadowsClipping, invocation.targetBackground);
    if (!transfer.hasValue())
    {
        return transfer.status();
    }
    report.stretch = *transfer;
    an::Result<an::Image<float>> stretched = an::Image<float>::createDestination(
        engine, *workingDescriptor, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized, working);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }
    {
        an::Result<an::ExecutionContext> execution = startStep(engine, stopToken, progress);
        if (!execution.hasValue())
        {
            return execution.status();
        }
        an::StretchImageRequest request;
        request.stretch.function = an::StretchFunction::ScreenTransfer;
        request.stretch.screenTransfer = {*transfer, *transfer, *transfer};
        const std::optional<an::ImageWriteAccess<float>> access = stretched->write();
        if (!access.has_value())
        {
            return an::Status::failure(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
        }
        const an::Result<an::StretchImageResult> ran =
            an::stretchImage(*execution, an::SampleType::Float32, working.storage().data(), *workingLayout,
                             access->view().data(), access->view().layout(), request);
        if (!ran.hasValue())
        {
            return ran.status();
        }
    }
    // The stretch restates what the samples mean: they are no longer proportional to light. The
    // processing history they carry is unchanged.
    an::ImageSemanticsRequest meaning = working.metadata().semantics().toRequest();
    meaning.transferFunction = an::TransferFunction::Unspecified;
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::MetadataTransition> change = an::operationTransition(
        an::kStretchImageEffect, an::OperationOutcome{.operation = "an::stretchImage",
                                                      .detail = "linked automatic screen transfer",
                                                      .semantics = *semantics});
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status recorded = stretched->transition(*change);
    if (recorded.failed())
    {
        return recorded;
    }
    working.release();

    // Step 4: publish in the container's representation.
    switch (*representation)
    {
    case an::SampleType::UInt8:
        return publish<std::uint8_t>(engine, stopToken, progress, *stretched, invocation.outputPath, format, step,
                                     report);
    case an::SampleType::UInt16:
        return publish<std::uint16_t>(engine, stopToken, progress, *stretched, invocation.outputPath, format, step,
                                      report);
    default:
        return publish<float>(engine, stopToken, progress, *stretched, invocation.outputPath, format, step, report);
    }
}

} // namespace

an::Result<Invocation> parseArguments(std::span<const std::string_view> arguments)
{
    Invocation invocation;
    std::vector<std::string_view> positional;
    for (std::size_t index = 0u; index < arguments.size(); ++index)
    {
        const std::string_view argument = arguments[index];
        if (argument == "--threads" || argument == "--memory-mib")
        {
            if (index + 1u >= arguments.size())
            {
                return usage(std::string(argument) + " needs a value");
            }
            const std::optional<std::uint64_t> value = parseUnsigned(arguments[++index]);
            if (!value.has_value())
            {
                return usage(std::string(argument) + " needs a whole number");
            }
            if (argument == "--threads")
            {
                invocation.threads = static_cast<std::uint32_t>(*value);
            }
            else
            {
                invocation.memoryBudgetBytes = *value * 1024u * 1024u;
            }
            continue;
        }
        if (argument.starts_with("--"))
        {
            return usage("unknown option " + std::string(argument));
        }
        positional.push_back(argument);
    }
    if (positional.size() != 2u)
    {
        return usage("expected an input path and an output path");
    }
    invocation.inputPath = std::string(positional[0]);
    invocation.outputPath = std::string(positional[1]);
    return invocation;
}

RunReport runOneShot(const Invocation& invocation, std::stop_token stopToken, an::ProgressSink* progress) noexcept
{
    RunReport report;
    CountingForwarder counted(progress);
    try
    {
        an::Result<an::EngineContext> engine = createProbeEngine(invocation.threads, invocation.memoryBudgetBytes);
        if (!engine.hasValue())
        {
            report.exitCode =
                engine.status().category() == an::ErrorCategory::Argument ? ExitCode::Usage : ExitCode::EngineFailed;
            report.engineCode = engine.status().code();
            report.diagnostic = "starting the engine: " + describeFailure(engine.status());
            return report;
        }

        ExitCode step = ExitCode::Success;
        const an::Status ran = process(*engine, invocation, std::move(stopToken), counted, step, report);
        report.progressReports = counted.count();
        if (ran.failed())
        {
            report.exitCode = ran.code() == an::ErrorCode::Cancelled ? ExitCode::Interrupted : step;
            report.engineCode = ran.code();
            report.diagnostic = describeFailure(ran);
        }

        // Everything the run created is gone, so the context is quiescent and the shutdown is the
        // checked one rather than the destructor's late release.
        const an::Status stopped = engine->shutdown();
        report.engineShutDown = stopped.ok();
        if (stopped.failed() && report.exitCode == ExitCode::Success)
        {
            report.exitCode = ExitCode::EngineFailed;
            report.engineCode = stopped.code();
            report.diagnostic = "stopping the engine: " + describeFailure(stopped);
        }
    }
    catch (...)
    {
        report.exitCode = ExitCode::EngineFailed;
        report.engineCode = an::ErrorCode::Internal;
        report.diagnostic = "the run ended with an exception";
    }
    return report;
}

} // namespace probe::cli
