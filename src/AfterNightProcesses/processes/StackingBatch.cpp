// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Stacking's run over the engine: the settings mapped through the integration's closed choice maps, a frame
/// source that decodes each file whole when the stack reads it - an exact zero and a sample that is not a number
/// meaning nothing - a Noise weighting's figures measured on each frame's pixels, what the engine says of a stack
/// logged and kept, and the stacked image and its rejection maps written through the view's file helpers.

#include "stdafx.hpp"
#include "processes/StackingBatch.h"

#include "controls/ImageViewFiles.h"
#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Algorithms/Stacking/StackExecutor.h>
#include <AfterNightEngine/Algorithms/Statistics/AdvancedStatistics.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Algorithms/Stacking/StackWorkingSet.h>
#include <AfterNightEngine/Algorithms/Stacking/StackingModel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/ImageStorage.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Runtime/DiagnosticSink.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <AfterNightPlatform/Logging.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace an::ui::processes
{

namespace
{

using ImageIntegration::PersistedChoice;

constexpr std::uint64_t kBytesPerMebibyte = 1024u * 1024u;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

/// A failure that says which group it stopped, keeping the cause.
[[nodiscard]] an::Status inGroup(an::Status cause, const QString& group)
{
    if (cause.code() == an::ErrorCode::Cancelled)
    {
        return cause;
    }
    const an::ErrorCode code = cause.code();
    std::string message = "Group '" + group.toStdString() + "': " + cause.message();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

[[nodiscard]] bool isText(const QString& text, const char* expected)
{
    return text.trimmed().compare(QLatin1String(expected), Qt::CaseInsensitive) == 0;
}

/// The stack's combination and rejection the method and rejection texts name: a rejection beside an average, or a
/// method text that names a rejection read as an average with it.
struct CombinationChoice
{
    an::StackCombination combination = an::StackCombination::Average;
    an::StackRejection rejection = an::StackRejection::None;
};

[[nodiscard]] an::Result<CombinationChoice> combinationOf(const StackingSettings& settings)
{
    const ImageIntegration::PersistedChoiceValue method =
        ImageIntegration::persistedChoiceFromText(PersistedChoice::StackMethod, settings.stackMethod);
    if (!method.known)
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       "Unknown stacking method '" + settings.stackMethod.toStdString() + "'");
    }
    CombinationChoice choice;
    if (const std::optional<an::StackCombination> combination = ImageIntegration::stackCombinationForKey(method.key))
    {
        choice.combination = *combination;
        if (choice.combination == an::StackCombination::Average)
        {
            const ImageIntegration::PersistedChoiceValue rejection =
                ImageIntegration::persistedChoiceFromText(PersistedChoice::RejectionMethod, settings.rejectionMethod);
            if (!rejection.known)
            {
                return refusal(an::ErrorCode::InvalidArgument,
                               "Unknown rejection method '" + settings.rejectionMethod.toStdString() + "'");
            }
            choice.rejection = ImageIntegration::stackRejectionForKey(rejection.key).value_or(an::StackRejection::None);
        }
        return choice;
    }
    if (const std::optional<an::StackRejection> rejection = ImageIntegration::stackRejectionForKey(method.key))
    {
        choice.rejection = *rejection;
        return choice;
    }
    return refusal(an::ErrorCode::InvalidArgument,
                   "Unknown stacking method '" + settings.stackMethod.toStdString() + "'");
}

/// Whether a combination takes per-frame weights: an average, with or without rejection.
[[nodiscard]] bool takesWeights(const CombinationChoice& choice)
{
    return choice.combination == an::StackCombination::Average;
}

/// Reports a stack's progress to the window as a fraction.
class FractionProgress final : public an::ProgressSink
{
public:
    explicit FractionProgress(std::function<void(double)> report) : m_report(std::move(report)) {}

    void onProgress(const an::ProgressReport& report) noexcept override
    {
        if (m_report && report.total > 0u)
        {
            m_report(static_cast<double>(report.completed) / static_cast<double>(report.total));
        }
    }

private:
    std::function<void(double)> m_report;
};

/// What the engine says of a group's work while it runs - a sum written as it is because no element's sum is
/// positive, whole-frame reads a tiling multiplied, a frame's decode - logged as it is said and kept for the window.
/// The engine's strings are borrowed for the call alone, so each sentence is copied; a sentence said again - a decode
/// warning repeats for every frame and every pass - is said once; and only the first few distinct sentences are kept,
/// the first many logged. Told from any of an operation's workers, and lent to the noise measurement's operation and
/// the stack's, so it is guarded.
class StackDiagnostics final : public an::DiagnosticSink
{
public:
    explicit StackDiagnostics(const QString& group) : m_group(group.toStdString()) {}

    void onDiagnostic(const an::DiagnosticEvent& event) noexcept override
    {
        try
        {
            if (event.severity < an::DiagnosticSeverity::Warning)
            {
                LOG_DEBUG("Image Stacking: group '{}': {} ({})", m_group, event.message, event.operation);
                return;
            }
            const QString sentence = QString::fromUtf8(event.message).trimmed();
            if (sentence.isEmpty())
            {
                return;
            }
            const std::lock_guard<std::mutex> lock(m_mutex);
            if (m_said.contains(sentence) || m_said.size() >= kMaxLoggedSentences)
            {
                return;
            }
            m_said.insert(sentence);
            LOG_WARNING("Image Stacking: group '{}': {} ({})", m_group, event.message, event.operation);
            if (m_kept.size() < kMaxKeptSentences)
            {
                m_kept.push_back(sentence);
            }
        }
        catch (...)
        {
            // A sentence that cannot be copied or logged is lost; the stack is not stopped for it.
        }
    }

    /// The sentences kept, in the order they were said.
    [[nodiscard]] QStringList warnings() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_kept;
    }

private:
    static constexpr qsizetype kMaxLoggedSentences = 64;
    static constexpr qsizetype kMaxKeptSentences = 8;

    std::string m_group;
    mutable std::mutex m_mutex;
    QSet<QString> m_said;
    QStringList m_kept;
};

/// Rewrites every sample of a packed floating band that is not a finite number as an exact zero, which a band read
/// with an exact zero meaning nothing leaves out with it. Branchless, so the compiler runs it in vector lanes: every
/// frame of a stack passes through it.
template <typename SampleT> void zeroNonFinite(std::byte* const samples, const std::uint64_t count) noexcept
{
    auto* const values = reinterpret_cast<SampleT*>(samples);
    for (std::uint64_t index = 0u; index < count; ++index)
    {
        const SampleT value = values[index];
        values[index] = std::isfinite(value) ? value : SampleT{0};
    }
}

/// A group's frames, each decoded whole by the codec facade when the stack reads it and packed planar into the
/// stack's read buffer, stating which of its samples mean something. Under `ValidityRule::ZeroIsNoData` a floating
/// sample that is not a number is read as an exact zero, so an exact zero and a sample that is not a number are both
/// left out.
class FileFrameSource final : public an::StackFrameSource
{
public:
    FileFrameSource(const an::EngineContext& engine, std::vector<std::string> paths,
                    const an::ImageDescriptor& descriptor, const an::ValidityRule validity)
        : m_engine(engine), m_paths(std::move(paths)), m_descriptor(descriptor), m_validity(validity),
          m_layouts(m_paths.size())
    {
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override
    {
        return an::StackSourceAccess::WholeFrame;
    }

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution,
                                                       const std::uint32_t frameIndex, const std::uint32_t firstRow,
                                                       const std::uint32_t rows,
                                                       const std::span<std::byte> buffer) override
    {
        if (frameIndex >= m_paths.size() || firstRow != 0u || rows != m_descriptor.extent().height())
        {
            return refusal(an::ErrorCode::OutOfRange, "the stack asked for a frame the group does not hold");
        }
        const std::string& path = m_paths[frameIndex];
        an::Result<an::CodecDecodeResult> decoded =
            an::decodeImage(m_engine, execution, an::CodecReadRequest{.path = path});
        if (!decoded.hasValue())
        {
            return decoded.status();
        }
        const std::optional<an::RawFrameDescriptor> frame = decoded->frame.descriptor();
        const std::optional<an::ImageLayout> layout = decoded->frame.layout();
        const std::optional<an::SampleType> stored = frame.has_value() ? frame->residentSampleType() : std::nullopt;
        const std::optional<an::ImageDescriptor> described =
            frame.has_value() ? frame->toImageDescriptor() : std::nullopt;
        if (!layout.has_value() || !stored.has_value() || !described.has_value() ||
            *stored != m_descriptor.sampleType() || described->extent() != m_descriptor.extent() ||
            described->cfa() != m_descriptor.cfa())
        {
            return refusal(an::ErrorCode::IncompatibleArguments,
                           "'" + path +
                               "' does not have the size, channels, depth and filter array of the group's first frame");
        }
        const std::optional<an::ImageLayout> packed =
            an::ImageLayout::create(an::ImageLayoutRequest{.extent = m_descriptor.extent(),
                                                           .elementSizeBytes = static_cast<std::uint32_t>(
                                                               an::sampleTypeSizeBytes(m_descriptor.sampleType())),
                                                           .samplesPerPixel = 1u,
                                                           .planeCount = m_descriptor.extent().channelCount(),
                                                           .rowAlignmentBytes = 1u},
                                    m_engine.limits());
        if (!packed.has_value() || packed->totalBytes() > buffer.size())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "a decoded frame does not fit the stack's read slot");
        }
        if (an::Status copied = an::repackImageSamples(execution, *stored, decoded->frame.bytes().data(), *layout,
                                                       buffer.data(), *packed);
            copied.failed())
        {
            return copied;
        }
        const bool foreignOrder = frame->byteOrder() != an::ByteOrder::Native &&
                                  frame->byteOrder() != an::nativeByteOrder() && an::isByteOrderMeaningful(*stored);
        if (foreignOrder)
        {
            if (an::Status swapped = an::swapImageByteOrder(execution, *stored, buffer.data(), *packed);
                swapped.failed())
            {
                return swapped;
            }
        }
        if (m_validity == an::ValidityRule::ZeroIsNoData)
        {
            const std::uint64_t count = static_cast<std::uint64_t>(m_descriptor.extent().width()) *
                                        m_descriptor.extent().height() * m_descriptor.extent().channelCount();
            if (*stored == an::SampleType::Float32)
            {
                zeroNonFinite<float>(buffer.data(), count);
            }
            else if (*stored == an::SampleType::Float64)
            {
                zeroNonFinite<double>(buffer.data(), count);
            }
        }
        const std::optional<an::ValidityDescriptor> validity = an::ValidityDescriptor::create({.rule = m_validity});
        if (!validity.has_value())
        {
            return refusal(an::ErrorCode::Internal, "a frame's validity rule describes nothing");
        }
        m_layouts[frameIndex] = *packed;
        return an::StackSourceBand{.sampleType = *stored,
                                   .samples = buffer.data(),
                                   .layout = &*m_layouts[frameIndex],
                                   .frameIndex = frameIndex,
                                   .firstRow = 0u,
                                   .validity = an::StackBandValidity{.descriptor = *validity, .mask = nullptr}};
    }

private:
    const an::EngineContext& m_engine;
    std::vector<std::string> m_paths;
    an::ImageDescriptor m_descriptor;
    an::ValidityRule m_validity = an::ValidityRule::AllValid;
    std::vector<std::optional<an::ImageLayout>> m_layouts;
};

/// What a group's frames are: as their files store them, as the stack reads them - fractions of full scale - and the
/// first frame's metadata, which the stacked image's record starts from.
struct GroupFrames
{
    an::ImageDescriptor stored;
    an::ImageDescriptor input;
    an::ImageMetadata reference;
};

/// The frames' description, read from the first frame's header and held to every other's.
///
/// @param scope the operation the headers are read in.
/// @param inputs the files, the first the reference.
/// @return the description; or what a probe refused, or a frame that differs from the first in its size, channels,
///         depth or filter array, said by name.
[[nodiscard]] an::Result<GroupFrames> describeGroupFrames(const an::consumer::OperationScope& scope,
                                                          const std::vector<std::string>& inputs)
{
    const an::Result<an::CodecImageInfo> first =
        an::probeImage(scope.engine(), scope.execution(), an::CodecReadRequest{.path = inputs.front()});
    if (!first.hasValue())
    {
        return first.status();
    }
    std::optional<an::ImageDescriptor> stored = first->descriptor.toImageDescriptor();
    if (!stored.has_value())
    {
        return refusal(an::ErrorCode::Unsupported, "the first frame holds samples this engine does not stack");
    }
    for (std::size_t index = 1u; index < inputs.size(); ++index)
    {
        const an::Result<an::CodecImageInfo> header = an::probeImage(
            scope.engine(), scope.execution(), an::CodecReadRequest{.path = inputs[index], .validatePayload = false});
        if (!header.hasValue())
        {
            return header.status();
        }
        const std::optional<an::ImageDescriptor> other = header->descriptor.toImageDescriptor();
        if (!other.has_value() || other->extent().width() != stored->extent().width() ||
            other->extent().height() != stored->extent().height())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "frames with mismatched dimensions");
        }
        if (other->extent().channelCount() != stored->extent().channelCount() ||
            other->sampleType() != stored->sampleType())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "frames with mismatched color depth");
        }
        // A mosaic's samples mean a colour each by where they lie: frames of another arrangement, or none, are not
        // the same samples, and the stack's demosaic reads the first frame's.
        if (other->isMosaiced() != stored->isMosaiced() || other->cfa() != stored->cfa())
        {
            return refusal(an::ErrorCode::IncompatibleArguments, "frames with mismatched filter arrays");
        }
    }
    // Every frame is read as fractions of its representation's full scale, whatever domain its header states.
    an::ImageDescriptorRequest described = stored->toRequest();
    described.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> input = an::ImageDescriptor::create(described);
    if (!input.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the frames have no linear description");
    }
    return GroupFrames{.stored = *stored, .input = *input, .reference = first->metadata};
}

/// Each frame's background noise, for its weight: every channel's first-difference estimate
/// (`computeImageAdvancedStatistics`) over the frame's samples as its file stores them - a floating frame's numbers
/// alone - averaged over the channels that have one. The figure is in the representation's units, which every frame of
/// a group shares and a weighting divides out.
///
/// @param scope the operation the frames are read in.
/// @param inputs the files.
/// @param stored what every frame is, as stored.
/// @return each frame's noise in the group's order, or nothing for a frame no channel of which has one; or what reading
///         a frame or measuring it refused.
[[nodiscard]] an::Result<std::vector<std::optional<double>>>
measureBackgroundNoise(const an::consumer::OperationScope& scope, const std::vector<std::string>& inputs,
                       const an::ImageDescriptor& stored)
{
    // The noise is measured over the samples as the file holds them, an exact zero among them.
    FileFrameSource source(scope.engine(), inputs, stored, an::ValidityRule::AllValid);
    const std::uint64_t bytes = static_cast<std::uint64_t>(stored.extent().width()) * stored.extent().height() *
                                stored.extent().channelCount() * an::sampleTypeSizeBytes(stored.sampleType());
    // One slot every frame is read into, held in doubles so its first byte is aligned for every representation.
    std::vector<double> slot(static_cast<std::size_t>((bytes + sizeof(double) - 1u) / sizeof(double)));
    const std::span<std::byte> buffer(reinterpret_cast<std::byte*>(slot.data()), slot.size() * sizeof(double));
    an::StatisticsSelection selection;
    if (an::isFloatingSampleType(stored.sampleType()))
    {
        selection.validity =
            *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::FiniteOnly});
    }
    std::vector<std::optional<double>> noise;
    noise.reserve(inputs.size());
    for (std::size_t frame = 0u; frame < inputs.size(); ++frame)
    {
        if (an::Status stopped = scope.execution().checkCancellation(); stopped.failed())
        {
            return stopped;
        }
        an::Result<an::StackSourceBand> band =
            source.read(scope.execution(), static_cast<std::uint32_t>(frame), 0u, stored.extent().height(), buffer);
        if (!band.hasValue())
        {
            return band.status();
        }
        double sum = 0.0;
        std::uint32_t measured = 0u;
        for (std::uint32_t channel = 0u; channel < stored.extent().channelCount(); ++channel)
        {
            const an::Result<an::AdvancedStatistics> statistics = an::computeImageAdvancedStatistics(
                scope.execution(), band->sampleType, band->samples, *band->layout, channel, selection);
            if (!statistics.hasValue())
            {
                if (statistics.status().code() == an::ErrorCode::EmptySelection)
                {
                    continue;
                }
                return statistics.status();
            }
            if (std::isfinite(statistics->backgroundNoise) && statistics->backgroundNoise > 0.0)
            {
                sum += statistics->backgroundNoise;
                ++measured;
            }
        }
        noise.push_back(measured > 0u ? std::optional<double>(sum / static_cast<double>(measured)) : std::nullopt);
    }
    return noise;
}

/// The stacked image of a result, in its floating representation, with the reference frame's metadata moved by the
/// stack's transition.
template <typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage> stackedImage(an::StackResult& result, const an::StackModel& model,
                                                              const an::ImageMetadata& reference)
{
    an::Result<an::ImageStorage<SampleT>> storage = an::ImageStorage<SampleT>::fromBytes(std::move(result.image));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    an::Result<an::Image<SampleT>> image =
        an::Image<SampleT>::fromStorage(std::move(*storage), result.descriptor, reference);
    if (!image.hasValue())
    {
        return image.status();
    }
    const an::Result<an::MetadataTransition> change = an::stackTransition(model, result.report, reference.semantics());
    if (!change.hasValue())
    {
        return change.status();
    }
    if (an::Status stated = image->transition(*change); stated.failed())
    {
        return stated;
    }
    return an::consumer::AnyImage(std::move(*image));
}

/// A rejection map: each element's count of rejected samples as a Float32 fraction of the group's frames, held at one.
[[nodiscard]] an::Result<an::consumer::AnyImage>
rejectionFractions(const an::EngineContext& engine, an::ImageStorageBytes&& counts, const an::ImageDescriptor& stacked,
                   const an::ImageMetadata& reference, const std::uint32_t frames)
{
    an::Result<an::ImageStorage<std::uint32_t>> storage = an::ImageStorage<std::uint32_t>::fromBytes(std::move(counts));
    if (!storage.hasValue())
    {
        return storage.status();
    }
    const std::optional<an::ImageView<const std::uint32_t>> source = std::as_const(*storage).view();
    an::ImageDescriptorRequest request = stacked.toRequest();
    request.sampleType = an::SampleType::Float32;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> described = an::ImageDescriptor::create(request);
    if (!source.has_value() || !described.has_value())
    {
        return refusal(an::ErrorCode::Internal, "a rejection map has no description");
    }
    an::Result<an::Image<float>> map = an::Image<float>::create(engine, *described, an::StorageArrangement::Planar,
                                                                an::StorageInitialization::Uninitialized, reference);
    if (!map.hasValue())
    {
        return map.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = map->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "a rejection map holds nothing");
        }
        const double count = static_cast<double>((std::max)(frames, 1u));
        for (std::uint32_t plane = 0u; plane < source->planeCount(); ++plane)
        {
            for (std::uint32_t y = 0u; y < source->height(); ++y)
            {
                const std::optional<std::span<const std::uint32_t>> in = source->row(plane, y);
                const std::optional<std::span<float>> out = access->view().row(plane, y);
                if (!in.has_value() || !out.has_value())
                {
                    return refusal(an::ErrorCode::Internal, "a rejection map row is missing");
                }
                for (std::size_t x = 0u; x < out->size(); ++x)
                {
                    (*out)[x] = static_cast<float>((std::min)(static_cast<double>((*in)[x]) / count, 1.0));
                }
            }
        }
    }
    return an::consumer::AnyImage(std::move(*map));
}

/// A card's real number as six decimals in the C locale, which is what std::to_string writes there: the process
/// locale of a Qt application on a host configured with a decimal comma would write "180,000000", a card no reader
/// takes for a number.
[[nodiscard]] std::string realCardText(const double value)
{
    char buffer[64];
    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 6);
    return error == std::errc() ? std::string(buffer, end) : std::string();
}

} // namespace

StackingSettings sanitizeStandaloneStackingSettings(StackingSettings settings)
{
    const an::Result<CombinationChoice> choice = combinationOf(settings);
    const bool weighted = !choice.hasValue() || takesWeights(*choice);
    const bool rejects = choice.hasValue() && choice->rejection != an::StackRejection::None;
    if (!weighted || isText(settings.weighting, "None"))
    {
        settings.weighting = QStringLiteral("None");
        settings.perChannelFwhmWeighting = false;
        settings.localWeighting.enabled = false;
    }
    if (!rejects)
    {
        if (choice.hasValue() && choice->combination != an::StackCombination::Average)
        {
            settings.rejectionMethod = QStringLiteral("None");
        }
        settings.createRejectionMaps = false;
    }
    if (!settings.createRejectionMaps)
    {
        settings.mergeRejectionMaps = false;
    }
    if (!isText(settings.cometStackingMode, "Stars"))
    {
        settings.weighting = QStringLiteral("None");
        settings.perChannelFwhmWeighting = false;
        settings.localWeighting.enabled = false;
    }
    if (settings.normalizationMode != NormalizationMode::Local)
    {
        settings.localNormalization.enabled = false;
    }
    return settings;
}

an::LocalNormalizationRequest standaloneLocalNormalizationRequest(const LocalNormalizationSettings& settings)
{
    an::LocalNormalizationRequest request;
    request.gridSize = static_cast<std::uint32_t>(
        std::clamp(settings.gridSize, 1, static_cast<int>(an::kMaxLocalNormalizationGridSize)));
    request.interpolation = isText(settings.gridInterpolation, "Bilinear") ? an::LocalGridInterpolation::Bilinear
                                                                           : an::LocalGridInterpolation::Bicubic;
    if (isText(settings.backgroundEstimator, "Percentile"))
    {
        request.estimator = an::LocalBackgroundEstimator::Percentile;
    }
    else if (isText(settings.backgroundEstimator, "Sigma Clipped Mode"))
    {
        request.estimator = an::LocalBackgroundEstimator::SigmaClippedMode;
    }
    else
    {
        request.estimator = an::LocalBackgroundEstimator::SigmaClippedMedian;
    }
    request.backgroundPercentile = std::clamp(settings.backgroundPercentile, 0.0, 1.0);
    request.sigmaClip = std::clamp(settings.sigmaClipK > 0.0 ? settings.sigmaClipK : 3.0,
                                   an::kMinLocalNormalizationSigmaClip, an::kMaxLocalNormalizationSigmaClip);
    request.sigmaClipIterations = static_cast<std::uint32_t>(
        std::clamp(settings.sigmaClipIterations, 1, static_cast<int>(an::kMaxLocalNormalizationSigmaClipIterations)));
    request.backgroundSmoothingRadius = static_cast<std::uint32_t>((std::max)(0, settings.backgroundSmoothingRadius));
    request.lowClip = settings.lowClip;
    request.highClip = settings.highClip;
    request.scaleMode = isText(settings.scaleMode, "Per-Channel") || isText(settings.scaleMode, "per_channel")
                            ? an::LocalScaleMode::PerChannel
                            : an::LocalScaleMode::Linked;
    request.localScaleSmoothingRadius = static_cast<std::uint32_t>((std::max)(0, settings.localScaleSmoothingRadius));
    return request;
}

an::Result<an::StackRequest> standaloneStackRequest(const StackingSettings& settings, const an::ImageDescriptor& input,
                                                    const std::uint32_t frameCount, const an::StackWeighting weighting,
                                                    const bool output64)
{
    const an::Result<CombinationChoice> choice = combinationOf(settings);
    if (!choice.hasValue())
    {
        return choice.status();
    }
    an::StackRequest request{.input = an::StackInputDescription{.frameCount = frameCount, .descriptor = input},
                             .combination = choice->combination};
    request.rejection.method = choice->rejection;
    if (an::stackRejectionUsesSigmaThresholds(choice->rejection))
    {
        request.rejection.sigma = an::SigmaClipThresholds{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesMedianFractions(choice->rejection))
    {
        request.rejection.medianFractions =
            an::MedianClipFractions{.low = settings.sigmaLow, .high = settings.sigmaHigh};
    }
    else if (an::stackRejectionUsesGesdtParameters(choice->rejection))
    {
        request.rejection.gesdt =
            an::GesdtParameters{.maximumOutlierFraction = settings.sigmaLow, .significance = settings.sigmaHigh};
    }
    request.weighting.method = takesWeights(*choice) ? weighting : an::StackWeighting::Uniform;
    request.weighting.minimumWeight = settings.minWeightFloor;

    switch (settings.normalizationMode)
    {
    case NormalizationMode::None:
        break;
    case NormalizationMode::Local:
        request.normalization.scope = an::StackNormalizationScope::Local;
        break;
    case NormalizationMode::Global: {
        request.normalization.scope = an::StackNormalizationScope::Global;
        const ImageIntegration::PersistedChoiceValue method = ImageIntegration::persistedChoiceFromInteger(
            PersistedChoice::GlobalNormalizationMethod, static_cast<int>(settings.globalNormalizationMethod));
        request.normalization.globalMethod = ImageIntegration::normalizationMethodForKey(method.key);
        if (!request.normalization.globalMethod.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "Unknown global normalization method");
        }
        break;
    }
    }
    if (request.normalization.scope != an::StackNormalizationScope::None)
    {
        const ImageIntegration::PersistedChoiceValue estimator = ImageIntegration::persistedChoiceFromInteger(
            PersistedChoice::ScaleEstimator, static_cast<int>(settings.scaleEstimator));
        const std::optional<an::ScaleEstimator> scale = ImageIntegration::scaleEstimatorForKey(estimator.key);
        if (!scale.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, "Unknown scale estimator");
        }
        request.normalization.scaleEstimator = *scale;
    }

    request.output.sampleType = output64 ? an::SampleType::Float64 : an::SampleType::Float32;
    if (settings.createRejectionMaps && choice->rejection != an::StackRejection::None)
    {
        request.output.rejectionMaps =
            settings.mergeRejectionMaps ? an::RejectionMapForm::Merged : an::RejectionMapForm::LowAndHigh;
    }

    const an::StackModelError refused = an::StackModel::check(request);
    if (refused != an::StackModelError::None)
    {
        return refusal(an::stackModelErrorCode(refused),
                       std::string("The stack cannot be made: ") + an::stackModelErrorName(refused));
    }
    return request;
}

std::optional<double> standaloneStackExposure(const std::vector<std::optional<double>>& exposures,
                                              const std::size_t reference, const bool sum)
{
    const std::optional<double> referenceExposure =
        reference < exposures.size() ? exposures[reference] : std::optional<double>{};
    if (!sum)
    {
        return referenceExposure;
    }
    double total = 0.0;
    std::size_t stated = 0u;
    std::size_t missing = 0u;
    for (const std::optional<double>& exposure : exposures)
    {
        if (exposure.has_value())
        {
            total += *exposure;
            ++stated;
        }
        else
        {
            ++missing;
        }
    }
    if (stated == 0u)
    {
        return std::nullopt;
    }
    if (missing > 0u && referenceExposure.has_value())
    {
        total += *referenceExposure * static_cast<double>(missing);
    }
    return total;
}

QString standaloneRejectionMapPath(const QString& outputPath, const QString& suffix)
{
    const QFileInfo info(outputPath);
    return QDir(info.absolutePath())
        .filePath(QStringLiteral("%1_%2.%3").arg(info.completeBaseName(), suffix, info.suffix()));
}

an::Result<StackedGroup> stackStandaloneGroup(const an::consumer::EngineSession& session,
                                              const StandaloneStackingGroup& group, const StandaloneStackRun& run,
                                              std::stop_token stop, std::function<void(double)> progress)
{
    if (group.images.empty())
    {
        return inGroup(refusal(an::ErrorCode::EmptySelection, "the group holds no frame"), group.label);
    }
    const QString database = QDir(run.outputDirectory).filePath(QString::fromLatin1(kRegistrationMetricsDatabaseName));

    // The frames the run's comet mode reads: the files themselves, or the comet-aligned products the registration
    // database names for them.
    StandaloneStackingTransformRoleRequest role;
    role.registrationMetricsDbPath = database;
    role.stackingMode = run.stacking.cometStackingMode;
    for (const StandaloneStackingFrame& frame : group.images)
    {
        role.frameIds.emplace_back(frame.entry.path);
    }
    const StandaloneStackingTransformRoleResult roles = validateStandaloneStackingTransformRoleSelection(role);
    if (!roles.allowed)
    {
        return inGroup(refusal(an::ErrorCode::InvalidArgument, roles.error.toStdString()), group.label);
    }
    std::vector<std::string> inputs;
    inputs.reserve(group.images.size());
    if (!roles.resolvedInputPaths.empty())
    {
        if (roles.resolvedInputPaths.size() != group.images.size())
        {
            return inGroup(refusal(an::ErrorCode::IncompatibleArguments,
                                   "the registration names " + std::to_string(roles.resolvedInputPaths.size()) +
                                       " comet products for " + std::to_string(group.images.size()) + " frames"),
                           group.label);
        }
        for (const QString& path : roles.resolvedInputPaths)
        {
            inputs.push_back(path.toUtf8().toStdString());
        }
    }
    else
    {
        for (const StandaloneStackingFrame& frame : group.images)
        {
            inputs.push_back(frame.entry.path.toUtf8().toStdString());
        }
    }

    // The weights the mode reads, from the registration database; a comet mode stacks with equal weights.
    StandaloneStackingWeightingResult weights;
    if (isText(run.stacking.cometStackingMode, "Stars"))
    {
        weights = planStandaloneStackingWeights(database, group.images,
                                                standaloneStackingWeightingModeFromText(run.stacking.weighting));
    }

    // What the engine says of the group's work; declared before the operations it is lent to, so it outlives them.
    StackDiagnostics diagnostics(group.label);

    // A Noise-weighted average's figures, from each frame's pixels. They are measured in an operation of their own, let
    // go before the stack's is admitted - the session admits one at a time - and with no progress sink: the decodes
    // report their rows, which would run ahead of the stack's own reports and hold its progress back.
    std::optional<GroupFrames> frames;
    if (weights.method == an::StackWeighting::Noise)
    {
        an::Result<an::consumer::OperationScope> measuring = session.admit(an::consumer::OperationRequest{
            .purpose = an::consumer::OperationPurpose::Batch, .stopToken = stop, .diagnosticSink = &diagnostics});
        if (!measuring.hasValue())
        {
            return measuring.status();
        }
        an::Result<GroupFrames> described = describeGroupFrames(*measuring, inputs);
        if (!described.hasValue())
        {
            return inGroup(described.status(), group.label);
        }
        an::Result<std::vector<std::optional<double>>> noise =
            measureBackgroundNoise(*measuring, inputs, described->stored);
        if (!noise.hasValue())
        {
            return inGroup(noise.status(), group.label);
        }
        weights = standaloneNoiseWeights(group.images, *noise);
        frames.emplace(std::move(*described));
    }
    if (!weights.warning.isEmpty())
    {
        LOG_WARNING("Image Stacking: group '{}': {}", group.label.toStdString(), weights.warning.toStdString());
    }

    FractionProgress sink(std::move(progress));
    // The stack's operation; let go before the files are written, each of which the write admits on its own.
    std::optional<an::consumer::OperationScope> scope;
    {
        an::Result<an::consumer::OperationScope> admitted =
            session.admit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Batch,
                                                         .stopToken = stop,
                                                         .progressSink = &sink,
                                                         .diagnosticSink = &diagnostics});
        if (!admitted.hasValue())
        {
            return admitted.status();
        }
        scope.emplace(std::move(*admitted));
    }

    // The frames' description, read from the first frame's header and held to every other's, unless the noise
    // measurement read it.
    if (!frames.has_value())
    {
        an::Result<GroupFrames> described = describeGroupFrames(*scope, inputs);
        if (!described.hasValue())
        {
            return inGroup(described.status(), group.label);
        }
        frames.emplace(std::move(*described));
    }
    const an::ImageDescriptor& stored = frames->stored;
    const an::ImageDescriptor& input = frames->input;
    const an::ImageMetadata& reference = frames->reference;

    const auto frameCount = static_cast<std::uint32_t>(inputs.size());
    an::Result<an::StackRequest> request =
        standaloneStackRequest(run.stacking, input, frameCount, weights.method, run.output64);
    if (!request.hasValue())
    {
        return inGroup(request.status(), group.label);
    }
    const std::optional<an::StackModel> model = an::StackModel::create(*request);
    if (!model.has_value())
    {
        return inGroup(refusal(an::ErrorCode::InvalidArgument, "the stack model refused the request"), group.label);
    }
    an::StackExecutionRequest execution;
    execution.measurements = weights.measurements;
    if (run.stacking.limitMemory && run.stacking.maxMemoryMb > 0)
    {
        execution.workingSetLimitBytes = static_cast<std::uint64_t>(run.stacking.maxMemoryMb) * kBytesPerMebibyte;
    }
    if (run.stacking.normalizationMode == NormalizationMode::Local)
    {
        execution.localNormalization = standaloneLocalNormalizationRequest(run.stacking.localNormalization);
        execution.fitLocalScaleGrid = run.stacking.localNormalization.localScaleEnabled;
    }
    // An exact zero - where an alignment left a frame's border - and a sample that is not a number mean nothing in any
    // frame, so neither is combined or judged by a rejection.
    FileFrameSource source(scope->engine(), inputs, stored, an::ValidityRule::ZeroIsNoData);
    an::Result<an::StackResult> stacked = an::executeStack(
        scope->engine(), scope->execution(), *model, source,
        an::StackSourceContract{.state = an::StackFrameState::Calibrated}, execution, an::StackExecutionOutputs{});
    if (!stacked.hasValue())
    {
        return inGroup(stacked.status(), group.label);
    }

    an::Result<an::consumer::AnyImage> image = run.output64 ? stackedImage<double>(*stacked, *model, reference)
                                                            : stackedImage<float>(*stacked, *model, reference);
    if (!image.hasValue())
    {
        return inGroup(image.status(), group.label);
    }
    // The stack states which of its pixels any frame reached; the mask travels with the image its rule describes.
    std::optional<an::ValidityMask> mask;
    if (stacked->mask.valid())
    {
        mask = std::move(stacked->mask);
    }
    if (run.debayer)
    {
        const std::optional<an::ImageDescriptor> result =
            std::visit([](const auto& typed) { return typed.descriptor(); }, *image);
        if (result.has_value() && result->isMosaiced())
        {
            const an::Result<an::CfaDescriptor> pattern = an::ui::resolveBayerPattern(run.cfaPattern, *result);
            if (!pattern.hasValue())
            {
                return inGroup(pattern.status(), group.label);
            }
            an::Result<an::ui::ViewDemosaic> colour = an::ui::demosaicViewFrameWithValidity(
                scope->engine(), scope->execution(), *image, mask.has_value() ? &*mask : nullptr, run.demosaicMethod,
                *pattern);
            if (!colour.hasValue())
            {
                return inGroup(colour.status(), group.label);
            }
            image = std::move(colour->image);
            // The demosaic answers for the colour image with a mask of its own, combined from the stack's over each
            // estimate's footprint; the stack's mask describes the mosaic it replaced.
            mask = std::move(colour->validity);
        }
    }

    // The rejection maps, as fractions of the group's frames, each with the map it is.
    struct RejectionMapFile
    {
        QString path;
        an::StackOutputMap which;
        an::consumer::AnyImage image;
    };
    std::vector<RejectionMapFile> maps;
    const QString outputPath = QDir(run.outputDirectory).filePath(group.outputFileName);
    const auto takeMap = [&](an::StackOutputMap which, const QString& suffix) -> an::Status {
        an::ImageStorageBytes& counts = stacked->maps[static_cast<std::size_t>(which)];
        if (!counts.valid())
        {
            return an::Status::success();
        }
        an::Result<an::consumer::AnyImage> map =
            rejectionFractions(scope->engine(), std::move(counts), stacked->descriptor, reference, frameCount);
        if (!map.hasValue())
        {
            return map.status();
        }
        maps.push_back(RejectionMapFile{
            .path = standaloneRejectionMapPath(outputPath, suffix), .which = which, .image = std::move(*map)});
        return an::Status::success();
    };
    for (const auto& [which, suffix] : {std::pair{an::StackOutputMap::RejectionMerged, QStringLiteral("rej_l+h")},
                                        std::pair{an::StackOutputMap::RejectionLow, QStringLiteral("rej_l")},
                                        std::pair{an::StackOutputMap::RejectionHigh, QStringLiteral("rej_h")}})
    {
        if (an::Status taken = takeMap(which, suffix); taken.failed())
        {
            return inGroup(std::move(taken), group.label);
        }
    }
    scope.reset();

    // The exposure the stack states: the reference frame's, or the sum.
    std::vector<std::optional<double>> exposures;
    exposures.reserve(group.images.size());
    for (const StandaloneStackingFrame& frame : group.images)
    {
        exposures.push_back(frame.exposureSeconds > 0.0 ? std::optional<double>(frame.exposureSeconds) : std::nullopt);
    }
    std::vector<an::CodecCard> cards;
    if (const std::optional<double> exposure =
            standaloneStackExposure(exposures, request->normalization.referenceFrame, run.sumExposureTimes))
    {
        an::Result<an::CodecCard> card = an::CodecCard::create(an::CodecNamespace::Fits, "EXPTIME",
                                                               realCardText(*exposure), "exposure time in seconds");
        if (card.hasValue())
        {
            cards.push_back(std::move(*card));
        }
    }

    StackedGroup outcome;
    outcome.frames = frameCount;
    outcome.weightingWarning = weights.warning;
    outcome.warnings = diagnostics.warnings();
    const auto write = [&](an::consumer::AnyImage&& written, std::optional<an::ValidityMask> validity,
                           const QString& path, an::SampleType representation, std::vector<an::CodecCard> fileCards,
                           QString description) -> an::Status {
        an::Result<an::consumer::ImageAsset> asset =
            an::consumer::ImageAsset::adopt(session, std::move(written), std::move(validity));
        if (!asset.hasValue())
        {
            return asset.status();
        }
        const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
            an::OutputIntentRequest{.sampleType = representation, .cards = std::move(fileCards)});
        if (!intent.hasValue())
        {
            return intent.status();
        }
        if (an::Status saved = an::ui::writeViewImage(session, *asset, path, *intent, stop); saved.failed())
        {
            return saved;
        }
        LOG_INFO("Image Stacking: saved '{}' to {}", description.toStdString(), path.toStdString());
        outcome.files.push_back(
            StackedFile{.path = path, .description = std::move(description), .asset = std::move(*asset)});
        return an::Status::success();
    };
    if (!QDir().mkpath(run.outputDirectory))
    {
        return inGroup(refusal(an::ErrorCode::InvalidArgument,
                               "Failed to create output directory: " + run.outputDirectory.toStdString()),
                       group.label);
    }
    // The maps first and the master last, so a master on disk always has its maps beside it: a map that cannot be
    // written, or a master that is not, takes back the maps already written.
    const auto takeBack = [&outcome]() {
        for (const StackedFile& file : outcome.files)
        {
            QFile::remove(file.path);
        }
        outcome.files.clear();
    };
    for (RejectionMapFile& map : maps)
    {
        const QString what = map.which == an::StackOutputMap::RejectionMerged ? QStringLiteral("Merged rejection map")
                             : map.which == an::StackOutputMap::RejectionLow  ? QStringLiteral("Low rejection map")
                                                                              : QStringLiteral("High rejection map");
        if (an::Status written = write(std::move(map.image), std::nullopt, map.path, an::SampleType::Float32, {}, what);
            written.failed())
        {
            takeBack();
            return inGroup(std::move(written), group.label);
        }
    }
    const QString description =
        QStringLiteral("Stacked image created from %1 frame(s) using %2").arg(frameCount).arg(run.stacking.stackMethod);
    if (an::Status written =
            write(std::move(*image), std::move(mask), outputPath, request->output.sampleType, cards, description);
        written.failed())
    {
        takeBack();
        return inGroup(std::move(written), group.label);
    }
    // The stacked image comes first among the files, then its maps.
    std::rotate(outcome.files.begin(), outcome.files.end() - 1, outcome.files.end());
    return outcome;
}

} // namespace an::ui::processes
