// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// One run of the command-line tool over the consumer runtime.
//
// The run is written the way a command-line tool is written - top to bottom, one step after another, every failure
// turned into the exit code of the step that met it - and three habits in it are what the runtime asks of a consumer.
//
// **Every step is its own admitted operation.** An operation scope holds a share of the session's threads for as long
// as it lives, so a tool that kept one for the whole run would hold the machine through its own file writes; each step
// is admitted, runs, and ends its scope before the next is admitted.
//
// **Every image is an asset, and is reached under a lease.** An asset counts against the session while it or a lease
// on it lives, and the session refuses to shut down while one does; so each asset is let go as soon as the next step
// no longer needs it, and the last line of the run - the checked shutdown - succeeds only if nothing was kept.
//
// **What was done is recorded where the engine declares it.** The presentation's effect on its result's metadata is
// the one the engine publishes beside the operation, and the change of representation is declared here, so the
// written image's lineage says what was done to it.

#include "OneShotRun.h"

#include "OutputPolicy.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Astrometry/WcsCards.h>
#include <AfterNightEngine/Algorithms/Presentation/DisplayPresentation.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecCapabilities.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/RawCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/OperationEffect.h>
#include <AfterNightEngine/Image/OperationTransition.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <AfterNightPlatform/Logging.h>

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <span>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace an::cli
{
namespace
{

/// What the tool declares about the conversion of its stretched image into the output's representation: the samples
/// are written into an image of another representation, which restates its description, and nothing else about the
/// image changes. The conversion is one the engine performs, and the effect is the tool's to state because the engine
/// publishes none for it.
constexpr an::OperationEffect kRepresentationChangeEffect{.samples = an::SampleEffect::Writes,
                                                          .description = an::DescriptionEffect::Restates};

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

/// Where a run is: the exit code a failure now is reported as, and what the step is, in words.
struct Step
{
    /// The exit code the step owns.
    ExitCode code = ExitCode::EngineFailed;

    /// What the step does, for the message a failure prints.
    std::string text{};
};

/// A failure as one line for a person: the code's name, the message, and the root cause's message when there is one.
///
/// @param status the failure.
/// @return the text. Never parsed.
[[nodiscard]] std::string describeFailure(const an::Status& status)
{
    std::string text = std::string(an::errorCodeName(status.code())) + ": " + status.message();
    const an::Status root = status.rootCause();
    if (root.failed() && std::strcmp(root.message(), status.message()) != 0)
    {
        text += " (because: " + std::string(root.message()) + ")";
    }
    return text;
}

/// A failure the tool decided, which no engine call produced.
[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// What a run holds while it runs: the session, the stop token, the progress sink and the report.
struct RunContext
{
    const an::consumer::EngineSession& session;
    const Invocation& invocation;
    std::stop_token stopToken;
    CountingForwarder& progress;
    RunReport& report;
    Step& step;
};

/// Admits one step's operation, for the purpose a person applying a process has.
///
/// @param run the run.
/// @return the scope; a failure to admit is the engine's, or the interrupt's when it was stopped while it waited.
[[nodiscard]] an::Result<an::consumer::OperationScope> admitStep(RunContext& run)
{
    an::consumer::OperationRequest request;
    request.purpose = an::consumer::OperationPurpose::Apply;
    request.stopToken = run.stopToken;
    request.progressSink = &run.progress;
    an::Result<an::consumer::OperationScope> scope = run.session.admit(request);
    if (!scope.hasValue() && scope.status().code() != an::ErrorCode::Cancelled)
    {
        run.step.code = ExitCode::EngineFailed;
        run.step.text = "admitting the operation for " + run.step.text;
    }
    return scope;
}

/// Takes a decoded frame over as an engine image of its own representation: with no copy when its memory is the
/// engine's and in this host's byte order, and through one copy otherwise.
///
/// @param scope the decode's operation.
/// @param decoded what the decode returned; its frame is taken.
/// @return the image, or what adopting or copying it refused.
[[nodiscard]] an::Result<an::consumer::AnyImage> importFrame(const an::consumer::OperationScope& scope,
                                                             an::CodecDecodeResult&& decoded)
{
    const std::optional<an::RawFrameDescriptor> frameDescriptor = decoded.frame.descriptor();
    const std::optional<an::ImageLayout> frameLayout = decoded.frame.layout();
    const std::optional<an::SampleType> resident =
        frameDescriptor.has_value() ? frameDescriptor->residentSampleType() : std::nullopt;
    const std::optional<an::ImageDescriptor> imageDescriptor =
        frameDescriptor.has_value() ? frameDescriptor->toImageDescriptor() : std::nullopt;
    if (!resident.has_value() || !frameLayout.has_value() || !imageDescriptor.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the input holds samples outside the eight this engine processes");
    }
    const bool adoptable = decoded.frame.checkAdoptable().ok();
    const bool foreignOrder = frameDescriptor->byteOrder() != an::ByteOrder::Native &&
                              frameDescriptor->byteOrder() != an::nativeByteOrder() &&
                              an::isByteOrderMeaningful(*resident);
    an::Result<an::consumer::AnyImage> imported = refusal(an::ErrorCode::Internal, "no representation was imported");
    const bool dispatched = an::dispatchSampleType(*resident, [&]<typename SampleT>() {
        if (adoptable)
        {
            an::Result<an::Image<SampleT>> adopted =
                an::Image<SampleT>::adopt(std::move(decoded.frame), std::move(decoded.metadata));
            imported = adopted.hasValue()
                           ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*adopted)))
                           : an::Result<an::consumer::AnyImage>(adopted.status());
            return;
        }
        an::Result<an::Image<SampleT>> copy =
            an::Image<SampleT>::create(scope.engine(), *imageDescriptor, an::StorageArrangement::Planar,
                                       an::StorageInitialization::Uninitialized, std::move(decoded.metadata));
        if (!copy.hasValue())
        {
            imported = copy.status();
            return;
        }
        {
            const std::optional<an::ImageWriteAccess<SampleT>> access = copy->write();
            if (!access.has_value())
            {
                imported = refusal(an::ErrorCode::NotInitialized, "the copy of the input holds nothing");
                return;
            }
            an::Status copied = an::repackImageSamples(scope.execution(), *resident, decoded.frame.bytes().data(),
                                                       *frameLayout, access->view().data(), access->view().layout());
            if (copied.ok() && foreignOrder)
            {
                copied = an::swapImageByteOrder(scope.execution(), *resident, access->view().data(),
                                                access->view().layout());
            }
            if (copied.failed())
            {
                imported = std::move(copied);
                return;
            }
        }
        decoded.frame.release();
        imported = an::consumer::AnyImage(std::move(*copy));
    });
    if (!dispatched)
    {
        return refusal(an::ErrorCode::Unsupported, "the input holds samples outside the eight this engine processes");
    }
    return imported;
}

/// The cards that write an input's world coordinates, or none when its header holds no solution this engine reads.
///
/// The presentation keeps the pixel grid, so a solution still describes the stretched image; but the solution's full
/// world coordinate system travels in the header cards the presentation's effect drops, and no codec writes a solution
/// from anything else. So the tool states the cards in its output intent itself. A header whose solution the engine
/// does not read is not a reason to fail a stretch: the output is written without one.
///
/// @param cards the input's header cards.
/// @param width the input's width in pixels.
/// @param height the input's height in pixels.
/// @return the cards, in the FITS namespace.
[[nodiscard]] std::vector<an::CodecCard> worldCoordinateCards(std::span<const an::CodecCard> cards, std::uint32_t width,
                                                              std::uint32_t height)
{
    if (an::checkWcsCards(cards) != an::WcsCardError::None)
    {
        return {};
    }
    const an::Result<an::WcsSolution> solution = an::parseWcsCards(cards, width, height);
    if (!solution.hasValue())
    {
        LOG_WARNING("AfterNightCLI: the input's world coordinates are not written: {}",
                    describeFailure(solution.status()));
        return {};
    }
    an::Result<std::vector<an::CodecCard>> built = an::buildWcsCards(*solution);
    if (!built.hasValue())
    {
        LOG_WARNING("AfterNightCLI: the input's world coordinates are not written: {}",
                    describeFailure(built.status()));
        return {};
    }
    return std::move(*built);
}

/// Presents a source image as the tool's stretch into a new Float32 image, and records the presentation's effect on it.
///
/// @param scope the presentation's operation.
/// @param image the source.
/// @param validity the source's validity mask, or null.
/// @param run the run, whose report is filled in.
/// @return the stretched image, or what the presentation refused.
template <an::ResidentSample SampleT>
[[nodiscard]] an::Result<an::Image<float>> present(const an::consumer::OperationScope& scope,
                                                   const an::Image<SampleT>& image, const an::ValidityMask* validity,
                                                   RunContext& run)
{
    std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const std::optional<an::ImageView<const SampleT>> view = image.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the input holds nothing");
    }
    // Every floating sample is read as a fraction of full scale: a physical quantity or a sensor code in floating point
    // has no full scale the presentation could present it against, so the tool states that reading.
    if constexpr (std::is_floating_point_v<SampleT>)
    {
        const an::NumericDomain domain = descriptor->numericDomain();
        if (domain == an::NumericDomain::PhysicalScaled || domain == an::NumericDomain::RawSensorCode)
        {
            an::ImageDescriptorRequest restated = descriptor->toRequest();
            restated.numericDomain = an::NumericDomain::NormalizedLinear;
            const std::optional<an::ImageDescriptor> normalized = an::ImageDescriptor::create(restated);
            if (!normalized.has_value())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the floating input has no description as a fraction of full scale");
            }
            descriptor = normalized;
            run.report.inputDomainRestated = true;
            LOG_INFO("AfterNightCLI: the floating input is read as a fraction of full scale");
        }
    }

    an::DisplayPresentationRequest request;
    request.promotion = an::PresentationPromotion::Gradient;
    request.stretch = an::PresentationStretch::Automatic;
    request.linked = false;
    request.shadowsClipping = an::kDisplayShadowsClipping;
    request.targetBackground = an::kDisplayTargetBackground;
    request.output = an::PresentationOutput::WorkingFloat32;
    const an::Result<an::DisplayPresentationPlan> plan = an::planDisplayPresentation(request);
    if (!plan.hasValue())
    {
        return plan.status();
    }

    // The stretched image: the source's extent and channels as Float32 fractions of full scale, carrying what is known
    // about the source until the presentation's effect says what no longer describes it.
    an::ImageDescriptorRequest workingRequest = descriptor->toRequest();
    workingRequest.sampleType = an::SampleType::Float32;
    workingRequest.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> workingDescriptor = an::ImageDescriptor::create(workingRequest);
    if (!workingDescriptor.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the input has no Float32 description");
    }
    an::Result<an::Image<float>> stretched =
        an::Image<float>::createDestination(scope.engine(), *workingDescriptor, an::StorageArrangement::Planar,
                                            an::StorageInitialization::Uninitialized, image);
    if (!stretched.hasValue())
    {
        return stretched.status();
    }

    std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
    an::DisplayPresentationSource source{
        .descriptor = &*descriptor, .samples = view->data(), .layout = &view->layout()};
    source.validity.descriptor = image.metadata().semantics().validity();
    if (validity != nullptr)
    {
        maskView = validity->view();
        source.validity.mask = maskView.has_value() ? &*maskView : nullptr;
    }
    {
        const std::optional<an::ImageWriteAccess<float>> access = stretched->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
        }
        const an::Result<an::DisplayPresentationResult> presented =
            an::presentImage(scope.execution(), *plan, source, access->view().data(), access->view().layout());
        if (!presented.hasValue())
        {
            return presented.status();
        }
        run.report.colorChannels = presented->analysis.colorChannels;
        run.report.transfer = presented->transfer;
    }

    // What the presentation did to the image it wrote, as the engine declares it: the samples are display data rather
    // than measurements, so the meaning is restated and the source's cards no longer describe them; the pixel grid is
    // the source's, so the solution still does.
    an::ImageSemanticsRequest meaning = image.metadata().semantics().toRequest();
    meaning.transferFunction = an::TransferFunction::Unspecified;
    meaning.gammaExponent.reset();
    meaning.validity = an::kDisplayPresentationEffect.validity.produced;
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(an::kDisplayPresentationEffect,
                                an::OperationOutcome{.operation = "an::presentImage",
                                                     .detail = "the automatic screen transfer, each channel its own",
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
    return stretched;
}

/// Converts the stretched image into the output's representation, at full scale and to the nearest value, and records
/// the change.
///
/// @param scope the conversion's operation.
/// @param stretched the Float32 result.
/// @return the converted image, or what the conversion refused.
template <an::ResidentSample SampleT>
[[nodiscard]] an::Result<an::Image<SampleT>> convertStretched(const an::consumer::OperationScope& scope,
                                                              const an::Image<float>& stretched)
{
    const std::optional<an::ImageDescriptor> source = stretched.descriptor();
    const std::optional<an::ImageView<const float>> view = stretched.view();
    if (!source.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the stretched image holds nothing");
    }
    const an::Result<an::ImageDescriptor> described = an::describeSamplesAs(*source, an::Image<SampleT>::kSampleType);
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::Image<SampleT>> converted =
        an::Image<SampleT>::createDestination(scope.engine(), *described, an::StorageArrangement::Planar,
                                              an::StorageInitialization::Uninitialized, stretched);
    if (!converted.hasValue())
    {
        return converted.status();
    }
    {
        const std::optional<an::ImageWriteAccess<SampleT>> access = converted->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the converted image holds nothing");
        }
        an::ConversionCounters counters;
        const an::Status done = an::convertImageSamples(
            scope.execution(), an::SampleType::Float32, view->data(), view->layout(), an::Image<SampleT>::kSampleType,
            access->view().data(), access->view().layout(), an::ConversionPolicy{}, counters);
        if (done.failed())
        {
            return done;
        }
    }
    const an::Result<an::MetadataTransition> change =
        an::operationTransition(kRepresentationChangeEffect,
                                an::OperationOutcome{.operation = "an::convertImageSamples",
                                                     .detail = std::string("Float32 to ") +
                                                               an::sampleTypeName(an::Image<SampleT>::kSampleType) +
                                                               ", nearest value at full scale"});
    if (!change.hasValue())
    {
        return change.status();
    }
    const an::Status recorded = converted->transition(*change);
    if (recorded.failed())
    {
        return recorded;
    }
    return converted;
}

/// Every step between opening the session and shutting it down.
///
/// A separate function so that every asset, lease and scope it creates is gone when it returns, which is what the
/// shutdown after it needs.
///
/// @param run the run.
/// @return success, or the failure of the step `run.step` names.
[[nodiscard]] an::Status process(RunContext& run)
{
    const Invocation& invocation = run.invocation;

    // The output's container, decided before anything is read: a run that would read and stretch for a minute and then
    // refuse the file name has wasted the minute.
    run.step = {ExitCode::Usage, "choosing how to write '" + invocation.outputPath + "'"};
    const an::CodecFormat format = an::codecFormatFromPath(invocation.outputPath);
    std::optional<an::CodecCapabilities> capabilities;
    {
        an::Result<an::consumer::SessionHold> hold = run.session.hold();
        if (!hold.hasValue())
        {
            run.step.code = ExitCode::EngineFailed;
            return hold.status();
        }
        const an::CodecRegistry* registry = hold->engine().codecRegistry();
        capabilities = registry == nullptr ? std::nullopt : registry->capabilities(format);
    }
    if (!capabilities.has_value() || capabilities->encodeSamples == 0u)
    {
        return refusal(an::ErrorCode::Unsupported, "no installed codec writes '" + invocation.outputPath +
                                                       "'; name a .fits, .xisf, .tif, .png or .jpg file");
    }

    // The input's representation, from its header, and the output's from it.
    run.step = {ExitCode::InputFailed, "reading '" + invocation.inputPath + "'"};
    LOG_INFO("AfterNightCLI: reading {}", invocation.inputPath);
    std::optional<an::SampleType> outputType;
    {
        an::Result<an::consumer::OperationScope> scope = admitStep(run);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        const an::Result<an::CodecImageInfo> info = an::probeImage(
            scope->engine(), scope->execution(),
            an::CodecReadRequest{.path = invocation.inputPath, .readMetadata = false, .validatePayload = false});
        if (!info.hasValue())
        {
            return info.status();
        }
        const std::optional<an::SampleType> resident = info->descriptor.residentSampleType();
        if (!resident.has_value())
        {
            return refusal(an::ErrorCode::Unsupported,
                           "the input holds samples outside the eight this engine processes");
        }
        const an::CfaPattern recorded = info->descriptor.cfa().pattern();
        if (info->descriptor.cfa().isMosaiced() && !holdsFilterArrayMosaic(format, recorded))
        {
            run.step = {ExitCode::Usage, "choosing how to write '" + invocation.outputPath + "'"};
            if (an::cfaPatternIsTabulated(recorded))
            {
                return refusal(an::ErrorCode::Unsupported,
                               std::string(an::codecFormatName(format)) +
                                   " cannot state an X-Trans filter array, and the input is the mosaic its sensor "
                                   "recorded; write a .xisf or .tif file");
            }
            return refusal(an::ErrorCode::Unsupported,
                           std::string(an::codecFormatName(format)) +
                               " cannot state a colour filter array, and the input is the mosaic its sensor recorded; "
                               "write a .fits, .xisf or .tif file");
        }
        run.report.inputSampleType = *resident;
        run.report.width = info->descriptor.extent().width();
        run.report.height = info->descriptor.extent().height();
        run.report.channels = info->descriptor.extent().channelCount();
        outputType = chooseOutputSampleType(*resident, invocation.sampleType, capabilities->encodeSamples);
    }
    if (!outputType.has_value())
    {
        run.step = {ExitCode::Usage, "choosing how to write '" + invocation.outputPath + "'"};
        return refusal(an::ErrorCode::Unsupported,
                       std::string(an::codecFormatName(format)) + " holds " +
                           describeEncodableSampleTypes(capabilities->encodeSamples) + " samples, not " +
                           sampleTypeArgument(invocation.sampleType.value_or(run.report.inputSampleType)));
    }

    // The input, decoded into an asset.
    an::consumer::ImageAsset input;
    {
        an::Result<an::consumer::OperationScope> scope = admitStep(run);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(scope->engine(), scope->execution(), an::CodecReadRequest{.path = invocation.inputPath});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        an::Result<an::consumer::AnyImage> imported = importFrame(*scope, std::move(*decoded));
        if (!imported.hasValue())
        {
            return imported.status();
        }
        an::Result<an::consumer::ImageAsset> adopted =
            an::consumer::ImageAsset::adopt(run.session, std::move(*imported));
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        input = std::move(*adopted);
    }

    // The stretch, into a Float32 asset; the input is let go as soon as it has been read.
    run.step = {ExitCode::ProcessingFailed, "stretching the image"};
    an::consumer::ImageAsset stretched;
    std::vector<an::CodecCard> worldCoordinates;
    {
        an::Result<an::consumer::AssetReadLease> lease = input.read(run.stopToken);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        const an::ImageMetadata& inputMetadata =
            lease->visit([](const auto& image) -> const an::ImageMetadata& { return image.metadata(); });
        worldCoordinates = worldCoordinateCards(inputMetadata.codec().cards(), run.report.width, run.report.height);
        an::Result<an::consumer::OperationScope> scope = admitStep(run);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::Image<float>> result =
            lease->visit([&](const auto& image) { return present(*scope, image, lease->validity(), run); });
        if (!result.hasValue())
        {
            return result.status();
        }
        an::Result<an::consumer::ImageAsset> adopted =
            an::consumer::ImageAsset::adopt(run.session, an::consumer::AnyImage(std::move(*result)));
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        stretched = std::move(*adopted);
    }
    input = an::consumer::ImageAsset{};

    // The output's representation, when it is not the stretch's own.
    an::consumer::ImageAsset output;
    if (*outputType == an::SampleType::Float32)
    {
        output = std::move(stretched);
    }
    else
    {
        run.step = {ExitCode::ProcessingFailed, "converting the image to " + sampleTypeArgument(*outputType)};
        an::Result<an::consumer::AssetReadLease> lease = stretched.read(run.stopToken);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<an::consumer::OperationScope> scope = admitStep(run);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        const an::Image<float>* working = std::get_if<an::Image<float>>(&lease->image());
        if (working == nullptr)
        {
            return refusal(an::ErrorCode::Internal, "the stretched image is not Float32");
        }
        an::Result<an::consumer::AnyImage> converted = refusal(an::ErrorCode::Internal, "no conversion ran");
        const bool dispatched = an::dispatchSampleType(*outputType, [&]<typename SampleT>() {
            an::Result<an::Image<SampleT>> typed = convertStretched<SampleT>(*scope, *working);
            converted = typed.hasValue() ? an::Result<an::consumer::AnyImage>(an::consumer::AnyImage(std::move(*typed)))
                                         : an::Result<an::consumer::AnyImage>(typed.status());
        });
        if (!dispatched)
        {
            return refusal(an::ErrorCode::Internal, "the output representation is not one of the eight");
        }
        if (!converted.hasValue())
        {
            return converted.status();
        }
        an::Result<an::consumer::ImageAsset> adopted =
            an::consumer::ImageAsset::adopt(run.session, std::move(*converted));
        if (!adopted.hasValue())
        {
            return adopted.status();
        }
        output = std::move(*adopted);
        lease->release();
        scope->end();
        stretched = an::consumer::ImageAsset{};
    }

    // The write: an explicit intent, through the output transaction, so the destination is whole or absent.
    run.step = {ExitCode::OutputFailed, "writing '" + invocation.outputPath + "'"};
    LOG_INFO("AfterNightCLI: writing {} as {} {}", invocation.outputPath, an::codecFormatName(format),
             sampleTypeArgument(*outputType));
    an::OutputIntentRequest intentRequest{.sampleType = *outputType, .compression = outputCompression(format)};
    // A world coordinate system is a FITS header's, so it is written where FITS keywords are.
    const bool writesWorldCoordinates =
        !worldCoordinates.empty() && (format == an::CodecFormat::Fits || format == an::CodecFormat::Xisf);
    if (writesWorldCoordinates)
    {
        intentRequest.cards = std::move(worldCoordinates);
    }
    an::Result<an::OutputIntent> intent = an::OutputIntent::create(intentRequest);
    if (!intent.hasValue())
    {
        return intent.status();
    }
    an::MetadataTransition published;
    {
        an::Result<an::consumer::AssetReadLease> lease = output.read(run.stopToken);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        an::Result<an::consumer::OperationScope> scope = admitStep(run);
        if (!scope.hasValue())
        {
            return scope.status();
        }
        an::Result<an::OutputReport> written = lease->visit([&](const auto& image) -> an::Result<an::OutputReport> {
            an::Result<an::RawFrame> frame = an::borrowImageFrame(image);
            if (!frame.hasValue())
            {
                return frame.status();
            }
            an::OutputRequest request;
            request.destinationPath = invocation.outputPath;
            request.format = format;
            request.intent = &*intent;
            request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
            return an::writeImage(scope->engine(), scope->execution(), request, *frame, image.metadata());
        });
        if (!written.hasValue())
        {
            return written.status();
        }
        run.report.outputFormat = format;
        run.report.outputSampleType = *outputType;
        run.report.bytesWritten = written->bytesWritten;
        run.report.worldCoordinatesWritten = writesWorldCoordinates;
        published = std::move(written->transition);
    }

    // The transaction owns no image, so the lineage entry naming the destination is applied here, to the image that was
    // written - which is where a caller reading back what was written would look.
    {
        an::Result<an::consumer::AssetWriteLease> lease = output.write(run.stopToken);
        if (!lease.hasValue())
        {
            return lease.status();
        }
        const an::Status recorded = lease->transition(published);
        if (recorded.failed())
        {
            return recorded;
        }
        std::visit(
            [&](const auto& image) {
                for (const an::ProvenanceEntry& entry : image.metadata().provenance().entries())
                {
                    run.report.outputLineage.push_back(entry.operation() + ": " + entry.detail());
                }
            },
            lease->image());
    }
    LOG_INFO("AfterNightCLI: wrote {} ({} bytes)", invocation.outputPath, run.report.bytesWritten);
    return an::Status::success();
}

} // namespace

an::Result<std::shared_ptr<const an::CodecRegistry>> createToolCodecRegistry()
{
    const std::array<an::Result<std::shared_ptr<const an::ImageCodec>>, 6u> adapters{
        an::createFitsCodec(), an::createXisfCodec(), an::createTiffCodec(),
        an::createPngCodec(),  an::createJpegCodec(), an::createRawCodec()};
    std::vector<std::shared_ptr<const an::ImageCodec>> codecs;
    codecs.reserve(adapters.size());
    for (const an::Result<std::shared_ptr<const an::ImageCodec>>& adapter : adapters)
    {
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        codecs.push_back(*adapter);
    }
    return an::CodecRegistry::create(codecs);
}

RunReport runOneShot(const Invocation& invocation, std::stop_token stopToken, an::ProgressSink* progress) noexcept
{
    RunReport report;
    try
    {
        CountingForwarder counted(progress);

        // The session: the invocation's ceilings, checked first so a ceiling no engine context may have is the
        // command line's mistake rather than the engine's.
        an::consumer::EngineSessionSettings settings;
        settings.maxConcurrency = invocation.threads;
        if (invocation.memoryBudgetMib != 0u)
        {
            settings.limits.memoryBudgetBytes = invocation.memoryBudgetMib * 1024u * 1024u;
        }
        settings.codecRegistry = [] { return createToolCodecRegistry(); };
        const an::Status checked = an::consumer::checkSessionSettings(settings);
        if (checked.failed())
        {
            report.exitCode = ExitCode::Usage;
            report.engineCode = checked.code();
            report.diagnostic = "the engine cannot run with these settings: " + describeFailure(checked);
            return report;
        }
        an::Result<an::consumer::EngineSession> session = an::consumer::EngineSession::open(settings);
        if (!session.hasValue())
        {
            report.exitCode = ExitCode::EngineFailed;
            report.engineCode = session.status().code();
            report.diagnostic = "starting the engine: " + describeFailure(session.status());
            return report;
        }

        Step step;
        RunContext run{*session, invocation, std::move(stopToken), counted, report, step};
        const an::Status ran = process(run);
        report.progressReports = counted.count();
        if (ran.failed())
        {
            const bool interrupted = ran.code() == an::ErrorCode::Cancelled;
            report.exitCode = interrupted ? ExitCode::Interrupted : step.code;
            report.engineCode = ran.code();
            report.diagnostic = interrupted ? "interrupted while " + step.text + "; nothing was written"
                                            : step.text + ": " + describeFailure(ran);
            LOG_WARNING("AfterNightCLI: {}", report.diagnostic);
        }

        // Everything the run created is gone, so the session is quiescent and the shutdown is the checked one rather
        // than the destructor's late release.
        const an::Status stopped = session->shutdown();
        report.sessionShutDown = stopped.ok();
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

std::string describeRun(const Invocation& invocation, const RunReport& report)
{
    std::ostringstream line;
    line << "wrote " << invocation.outputPath << ": " << an::codecFormatName(report.outputFormat) << " "
         << sampleTypeArgument(report.outputSampleType) << ", " << report.width << " x " << report.height << ", "
         << report.channels << (report.channels == 1u ? " channel" : " channels");
    if (report.inputDomainRestated)
    {
        line << "; the floating input was read as a fraction of full scale";
    }
    return line.str();
}

} // namespace an::cli
