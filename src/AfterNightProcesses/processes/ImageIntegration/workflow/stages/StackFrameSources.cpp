// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lights stacking stage's frames, read as the stack or a drizzle asks for them.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackFrameSources.h"

#include "app/DemosaicTextMaps.h"
#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Stacking/StackAccumulators.h>
#include <AfterNightEngine/Algorithms/Stacking/StackSource.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace stage_utils
{

namespace
{

[[nodiscard]] std::string displayName(const QString& path)
{
    return QFileInfo(path).fileName().toUtf8().toStdString();
}

/// A failure that names the file it concerns, keeping the engine's as its cause.
[[nodiscard]] an::Status aboutFile(an::Status cause, const QString& path, const char* what)
{
    if (cause.code() == an::ErrorCode::Cancelled || cause.code() == an::ErrorCode::DeadlineExceeded)
    {
        return cause;
    }
    const an::ErrorCode code = cause.code();
    std::string message = "'" + displayName(path) + "' " + what + " " + cause.message();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

[[nodiscard]] bool unchanged(const StackFrameRead& frame)
{
    if (!frame.revision.has_value())
    {
        return true;
    }
    const std::optional<FileRevisionStamp> now = ArtifactTracker::captureFileRevision(frame.path);
    return now.has_value() && frame.revision->sameRevision(*now);
}

[[nodiscard]] bool sameShape(const an::ImageDescriptor& left, const an::ImageDescriptor& right)
{
    return left.extent() == right.extent() && left.sampleType() == right.sampleType() && left.cfa() == right.cfa();
}

/// States a kept mosaic as the samples of a stated filter array placed at its origin, over the one its header states or
/// where it states none - the registration's rule for a stated pattern, which refuses a two-by-two pattern over an
/// X-Trans array.
[[nodiscard]] an::Status stateMosaicPattern(an::Image<float>& image, const an::CfaPattern pattern)
{
    const std::optional<an::ImageDescriptor> described = image.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const an::Result<an::CfaDescriptor> stated =
        an::ui::resolveBayerPattern(an::ui::BayerPatternChoice{.stated = pattern}, *described);
    if (!stated.hasValue())
    {
        return stated.status();
    }
    if (described->cfa() == *stated)
    {
        return an::Status::success();
    }
    an::ImageDescriptorRequest request = described->toRequest();
    request.cfa = *stated;
    const std::optional<an::ImageDescriptor> restated = an::ImageDescriptor::create(request);
    if (!restated.has_value())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the frame cannot be described as a filter array's samples");
    }
    an::Result<an::ProvenanceEntry> entry = an::ProvenanceEntry::create(
        "stage_utils::StackFrameReader", std::string("read under the drizzle's ") + an::cfaPatternName(pattern));
    if (!entry.hasValue())
    {
        return entry.status();
    }
    return image.transition(*restated, an::MetadataTransition{.record = std::move(*entry)});
}

/// Whether a failure is the operation's stop rather than anything about a frame.
[[nodiscard]] bool stopped(const an::Status& status)
{
    return status.code() == an::ErrorCode::Cancelled || status.code() == an::ErrorCode::DeadlineExceeded;
}

/// Opens a frame's row reader through the run's registry, or the engine context's when it names none.
[[nodiscard]] an::Result<an::ImageRowReader> openRows(const StackFrameBands& bands, const std::string& path,
                                                      const an::ExecutionContext& execution)
{
    const an::CodecReadRequest request{.path = path};
    return bands.registry != nullptr
               ? an::ImageRowReader::open(bands.scope->engine(), execution, *bands.registry, request)
               : an::ImageRowReader::open(bands.scope->engine(), execution, request);
}

/// Restates every sample of a band that is not a finite number as an exact zero, as `zeroNonFiniteSamples` restates a
/// whole frame's.
void zeroNonFiniteBand(std::span<float> samples)
{
    for (float& sample : samples)
    {
        sample = std::isfinite(sample) ? sample : 0.0f;
    }
}

/// Multiplies every sample of a band's rows by a factor, in place.
template <typename SampleT>
[[nodiscard]] an::Status scaleBand(const an::ExecutionContext& execution, std::byte* samples,
                                   const an::ImageLayout& layout, const double factor)
{
    const std::size_t width =
        static_cast<std::size_t>(layout.extent().width()) * static_cast<std::size_t>(layout.samplesPerPixel());
    for (std::uint32_t plane = 0u; plane < layout.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
        {
            if (an::Status stopped = execution.checkCancellation(); stopped.failed())
            {
                return stopped;
            }
            const std::optional<std::uint64_t> offset = layout.rowByteOffset(plane, y);
            if (!offset.has_value())
            {
                return an::Status::failure(an::ErrorCode::Internal, "a scaled frame's row lies outside its band");
            }
            auto* row = reinterpret_cast<SampleT*>(samples + static_cast<std::size_t>(*offset));
            for (std::size_t x = 0u; x < width; ++x)
            {
                row[x] = static_cast<SampleT>(static_cast<double>(row[x]) * factor);
            }
        }
    }
    return an::Status::success();
}

} // namespace

StackFrameReader::StackFrameReader(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry,
                                   const StackFramePlan& plan, core::CalibrationMasterSet& masters)
    : m_scope(scope), m_registry(registry), m_plan(plan), m_masters(masters)
{
}

an::Result<an::Image<float>> StackFrameReader::read(const std::size_t index)
{
    if (index >= m_plan.frames.size())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "the stack asked for a frame the group does not hold");
    }
    const StackFrameRead& frame = m_plan.frames[index];
    if (!unchanged(frame))
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   "'" + displayName(frame.path) + "' changed after the stack's inputs were checked");
    }
    std::shared_ptr<const core::LightCalibration> calibration;
    if (frame.calibration != nullptr)
    {
        an::Result<std::shared_ptr<const core::LightCalibration>> prepared =
            m_masters.prepare(m_scope, *frame.calibration);
        if (!prepared.hasValue())
        {
            return aboutFile(prepared.status(), frame.path, "could not be calibrated:");
        }
        calibration = std::move(*prepared);
    }

    an::Result<an::Image<float>> image = an::Status::failure(an::ErrorCode::Internal, "the frame was not read");
    if (m_plan.keepMosaic)
    {
        an::Result<an::consumer::AnyImage> read = core::readWorkflowImage(m_scope, m_registry, frame.path);
        if (!read.hasValue())
        {
            return aboutFile(read.status(), frame.path, "could not be read:");
        }
        image = core::workingFractions(m_scope, *read);
        if (!image.hasValue())
        {
            return aboutFile(image.status(), frame.path, "could not be widened:");
        }
        if (calibration != nullptr && calibration->calibrates())
        {
            an::Result<core::CalibratedLight> calibrated = core::calibrateLight(m_scope, *calibration, *image);
            if (!calibrated.hasValue())
            {
                return aboutFile(calibrated.status(), frame.path, "could not be calibrated:");
            }
            image = an::Result<an::Image<float>>(std::move(calibrated->image));
        }
        if (m_plan.mosaicPattern.has_value())
        {
            if (an::Status stated = stateMosaicPattern(*image, *m_plan.mosaicPattern); stated.failed())
            {
                return aboutFile(std::move(stated), frame.path, "could not be read under the drizzle's filter array:");
            }
        }
    }
    else
    {
        image = readRegistrationImage(m_scope, m_registry, frame.path, calibration.get(), m_plan.demosaic);
        if (!image.hasValue())
        {
            return image;
        }
    }
    if (frame.warp)
    {
        image = warpRegistrationFrame(m_scope, *image, frame.transform, frame.distortion, m_plan.canvas,
                                      m_plan.interpolation, std::nullopt);
        if (!image.hasValue())
        {
            return aboutFile(image.status(), frame.path, "could not be warped onto the registration's plane:");
        }
    }
    if (!unchanged(frame))
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   "'" + displayName(frame.path) + "' changed while the stack read it");
    }
    return image;
}

an::ValidityDescriptor stackFrameValidity(const an::ValidityRule rule)
{
    if (rule == an::ValidityRule::AllValid)
    {
        return an::ValidityDescriptor{};
    }
    const std::optional<an::ValidityDescriptor> described = an::ValidityDescriptor::create({.rule = rule});
    return described.value_or(an::ValidityDescriptor{});
}

an::Result<std::uint64_t> zeroNonFiniteSamples(an::Image<float>& frame)
{
    std::optional<an::ImageWriteAccess<float>> access = frame.write();
    if (!access.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the frame holds nothing");
    }
    const an::ImageView<float>& view = access->view();
    std::uint64_t restated = 0u;
    for (std::uint32_t plane = 0u; plane < view.planeCount(); ++plane)
    {
        for (std::uint32_t y = 0u; y < view.height(); ++y)
        {
            const std::optional<std::span<float>> row = view.row(plane, y);
            if (!row.has_value())
            {
                return an::Status::failure(an::ErrorCode::Internal, "a frame's row lies outside it");
            }
            // Branchless, so the compiler runs it in vector lanes: every frame on the registration's plane passes
            // through it.
            for (float& sample : *row)
            {
                const bool finite = std::isfinite(sample);
                restated += finite ? 0u : 1u;
                sample = finite ? sample : 0.0f;
            }
        }
    }
    return restated;
}

std::uint32_t stackBandTileRows(const std::uint32_t frameCount, const std::uint32_t width, const std::uint32_t channels,
                                const std::uint32_t workers, const std::uint32_t granularityRows)
{
    const std::uint64_t rowBytes = static_cast<std::uint64_t>(frameCount) * width * channels * sizeof(float);
    const std::uint64_t cached = rowBytes == 0u ? kStackBandTileMostRows : kStackBandTileBytes / rowBytes;
    const std::uint64_t rows = (std::max)(cached, static_cast<std::uint64_t>((std::max)(workers, 1u)));
    const auto kept = static_cast<std::uint32_t>((std::min)(rows, static_cast<std::uint64_t>(kStackBandTileMostRows)));
    return (std::max)(kept, granularityRows);
}

StackFrameBandPlan stackFrameBandPlan(const StackFrameReader& reader, const std::span<const std::uint32_t> frames)
{
    const StackFramePlan& plan = reader.plan();
    if (plan.keepMosaic || plan.mosaicPattern.has_value())
    {
        return {.bands = std::nullopt, .wholeFrameReason = QStringLiteral("the drizzle keeps each light's mosaic")};
    }
    StackFrameBands bands{.scope = &reader.scope(), .registry = reader.registry(), .frames = {}};
    bands.frames.reserve(frames.size());
    for (const std::uint32_t index : frames)
    {
        if (index >= plan.frames.size())
        {
            return {.bands = std::nullopt,
                    .wholeFrameReason = QStringLiteral("the stack names a frame the plan lacks")};
        }
        const StackFrameRead& frame = plan.frames[index];
        if (frame.calibration != nullptr)
        {
            return {.bands = std::nullopt,
                    .wholeFrameReason = QStringLiteral("the lights are calibrated as they are read")};
        }
        if (frame.warp)
        {
            return {.bands = std::nullopt,
                    .wholeFrameReason =
                        QStringLiteral("the lights are warped onto the registration's plane as they are "
                                       "read")};
        }
        bands.frames.push_back(&frame);
    }
    return {.bands = std::move(bands), .wholeFrameReason = {}};
}

WorkflowStackFrameSource::WorkflowStackFrameSource(StackFrameReadFunction read, const std::uint32_t frameCount,
                                                   const an::ValidityRule validity, StackFrameNameFunction name,
                                                   const an::EngineLimits& limits, StackFrameBandPlan bands)
    : m_read(std::move(read)), m_frameCount(frameCount), m_validity(validity), m_name(std::move(name)),
      m_limits(limits), m_bandPlan(std::move(bands))
{
    if (!m_bandPlan.bands.has_value() && m_bandPlan.wholeFrameReason.isEmpty())
    {
        m_bandPlan.wholeFrameReason = QStringLiteral("the stack's frames are made as they are read");
    }
}

an::Result<bool> WorkflowStackFrameSource::openBands()
{
    const StackFrameBands& bands = *m_bandPlan.bands;
    const auto whole = [this](const QString& reason) {
        m_bandPlan.wholeFrameReason = reason;
        m_readers.clear();
        m_paths.clear();
        return false;
    };
    if (bands.scope == nullptr || bands.frames.size() != m_frameCount)
    {
        return whole(QStringLiteral("the stack's band reads do not name its frames"));
    }
    const an::ExecutionContext& execution = bands.scope->execution();
    m_paths.reserve(m_frameCount);
    m_readers.resize(m_frameCount);
    std::optional<an::RawFrameDescriptor> first;
    // The largest piece any frame's reader decodes whole, which the stack's tiles hold to decode each piece once.
    std::uint32_t granularity = 1u;
    for (std::uint32_t index = 0u; index < m_frameCount; ++index)
    {
        const StackFrameRead& frame = *bands.frames[index];
        const QString name = QFileInfo(frame.path).fileName();
        // A file that changed since it was planned is the whole read's to refuse, by name.
        if (!unchanged(frame))
        {
            return whole(QStringLiteral("'%1' changed after the stack's inputs were checked").arg(name));
        }
        m_paths.push_back(frame.path.toUtf8().toStdString());
        an::Result<an::ImageRowReader> opened = openRows(bands, m_paths.back(), execution);
        if (!opened.hasValue())
        {
            if (stopped(opened.status()))
            {
                return opened.status();
            }
            return whole(QStringLiteral("'%1' is not read by bands: %2")
                             .arg(name, QString::fromUtf8(opened.status().message())));
        }
        const an::CodecImageInfo* info = opened->info();
        const an::Result<an::ImageLayout> rowLayout = opened->rowsLayout(1u);
        if (info == nullptr || !rowLayout.hasValue())
        {
            return whole(QStringLiteral("'%1' describes no band of its rows").arg(name));
        }
        const an::RawFrameDescriptor& stored = info->descriptor;
        if (stored.sampleFormat() != an::RawSampleFormat{4u, an::RawSampleClass::FloatingPoint})
        {
            return whole(QStringLiteral("'%1' is not stored as Float32").arg(name));
        }
        if (stored.cfa().isMosaiced())
        {
            return whole(QStringLiteral("'%1' is a mosaic, demosaiced as it is read").arg(name));
        }
        if (rowLayout->samplesPerPixel() != 1u && stored.extent().channelCount() != 1u)
        {
            return whole(QStringLiteral("'%1' keeps its channels together in each pixel").arg(name));
        }
        if (first.has_value() && stored.extent() != first->extent())
        {
            // The whole read refuses a frame of another size, by name.
            return whole(
                QStringLiteral("'%1' does not have the size and channels of the stack's first frame").arg(name));
        }
        if (!first.has_value())
        {
            first = stored;
        }
        granularity = (std::max)(granularity, opened->decodeGranularityRows());
        if (index < bands.mostOpenReaders)
        {
            m_readers[index] = std::move(*opened);
        }
    }
    m_bandLayouts.assign(m_frameCount, std::nullopt);

    // What every frame is, as the whole read describes the first: Float32 fractions of full scale, which a Float32
    // sample already is, with the file's own metadata.
    const std::optional<an::ImageDescriptor> described = first->toImageDescriptor();
    if (!described.has_value())
    {
        return whole(QStringLiteral("the stack's first frame has no description as an image"));
    }
    an::ImageDescriptorRequest request = described->toRequest();
    request.sampleType = an::SampleType::Float32;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    const std::optional<an::ImageDescriptor> fractions = an::ImageDescriptor::create(request);
    if (!fractions.has_value())
    {
        return whole(QStringLiteral("the stack's first frame has no description as fractions of full scale"));
    }
    const an::CodecReadRequest header{.path = m_paths.front(), .readMetadata = true};
    an::Result<an::CodecImageInfo> probed =
        bands.registry != nullptr ? an::probeImage(bands.scope->engine(), execution, *bands.registry, header)
                                  : an::probeImage(bands.scope->engine(), execution, header);
    if (!probed.hasValue())
    {
        if (stopped(probed.status()))
        {
            return probed.status();
        }
        return whole(QStringLiteral("the header of the stack's first frame could not be read with its metadata"));
    }
    m_descriptor = *fractions;
    m_firstMetadata = std::move(probed->metadata);
    m_decodeGranularityRows = granularity;
    return true;
}

an::Result<an::ImageDescriptor> WorkflowStackFrameSource::describe()
{
    if (m_descriptor.has_value())
    {
        return *m_descriptor;
    }
    if (m_frameCount == 0u)
    {
        return an::Status::failure(an::ErrorCode::EmptySelection, "the stack has no frame to read");
    }
    if (m_bandPlan.bands.has_value())
    {
        const an::Result<bool> banded = openBands();
        if (!banded.hasValue())
        {
            return banded.status();
        }
        m_readsBands = *banded;
        if (m_readsBands)
        {
            return *m_descriptor;
        }
    }
    an::Result<an::Image<float>> first = m_read(0u);
    if (!first.hasValue())
    {
        return first.status();
    }
    const std::optional<an::ImageDescriptor> described = first->descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the first frame holds nothing");
    }
    m_descriptor = *described;
    m_firstMetadata = first->metadata();
    m_first = std::move(*first);
    return *m_descriptor;
}

const an::ImageMetadata* WorkflowStackFrameSource::firstMetadata() const noexcept
{
    return m_firstMetadata.has_value() ? &*m_firstMetadata : nullptr;
}

an::StackSourceAccess WorkflowStackFrameSource::access() const noexcept
{
    return m_readsBands ? an::StackSourceAccess::ConcurrentRegion : an::StackSourceAccess::WholeFrame;
}

an::Result<an::StackSourceBand> WorkflowStackFrameSource::readBand(const an::ExecutionContext& execution,
                                                                   const std::uint32_t frameIndex,
                                                                   const std::uint32_t firstRow,
                                                                   const std::uint32_t rows,
                                                                   const std::span<std::byte> buffer)
{
    const std::uint32_t height = m_descriptor->extent().height();
    if (frameIndex >= m_frameCount || rows == 0u || firstRow >= height || rows > height - firstRow)
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "the stack asked for rows the group does not hold");
    }
    const StackFrameRead& frame = *m_bandPlan.bands->frames[frameIndex];
    // A reader past the ones held open is opened for this band and closed with it.
    an::ImageRowReader opened;
    an::ImageRowReader* reader = &m_readers[frameIndex];
    if (!reader->valid())
    {
        an::Result<an::ImageRowReader> transient = openRows(*m_bandPlan.bands, m_paths[frameIndex], execution);
        if (!transient.hasValue())
        {
            return aboutFile(transient.status(), frame.path, "could not be read:");
        }
        opened = std::move(*transient);
        reader = &opened;
    }
    const an::Result<an::ImageLayout> layout = reader->readRows(execution, firstRow, rows, buffer);
    if (!layout.hasValue())
    {
        return aboutFile(layout.status(), frame.path, "could not be read:");
    }
    if (m_validity == an::ValidityRule::ZeroIsNoData)
    {
        zeroNonFiniteBand(std::span<float>(reinterpret_cast<float*>(buffer.data()),
                                           static_cast<std::size_t>(layout->totalBytes() / sizeof(float))));
    }
    if (firstRow + rows == height && !unchanged(frame))
    {
        return an::Status::failure(an::ErrorCode::InvalidState,
                                   "'" + displayName(frame.path) + "' changed while the stack read it");
    }
    m_bandLayouts[frameIndex] = *layout;
    m_bandsRead.fetch_add(1u, std::memory_order_relaxed);
    return an::StackSourceBand{
        .sampleType = an::SampleType::Float32,
        .samples = buffer.data(),
        .layout = &*m_bandLayouts[frameIndex],
        .frameIndex = frameIndex,
        .firstRow = firstRow,
        .validity = an::StackBandValidity{.descriptor = stackFrameValidity(m_validity), .mask = nullptr}};
}

an::Result<an::StackSourceBand> WorkflowStackFrameSource::read(const an::ExecutionContext& execution,
                                                               const std::uint32_t frameIndex,
                                                               const std::uint32_t firstRow, const std::uint32_t rows,
                                                               const std::span<std::byte> buffer)
{
    if (!m_descriptor.has_value())
    {
        const an::Result<an::ImageDescriptor> described = describe();
        if (!described.hasValue())
        {
            return described.status();
        }
    }
    if (m_readsBands)
    {
        return readBand(execution, frameIndex, firstRow, rows, buffer);
    }
    if (frameIndex >= m_frameCount || firstRow != 0u || rows != m_descriptor->extent().height())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "the stack asked for a frame the group does not hold");
    }
    an::Result<an::Image<float>> image = an::Status::failure(an::ErrorCode::Internal, "the frame was not read");
    if (frameIndex == 0u && m_first.has_value())
    {
        image = an::Result<an::Image<float>>(std::move(*m_first));
        m_first.reset();
    }
    else
    {
        image = m_read(frameIndex);
    }
    if (!image.hasValue())
    {
        return image.status();
    }
    if (m_validity == an::ValidityRule::ZeroIsNoData)
    {
        if (const an::Result<std::uint64_t> restated = zeroNonFiniteSamples(*image); !restated.hasValue())
        {
            return restated.status();
        }
    }
    ++m_framesRead;
    const std::optional<an::ImageDescriptor> described = image->descriptor();
    const auto view = std::as_const(*image).view();
    if (!described.has_value() || !view.has_value() || !sameShape(*described, *m_descriptor))
    {
        return an::Status::failure(
            an::ErrorCode::IncompatibleArguments,
            "'" + displayName(m_name ? m_name(frameIndex) : QString()) +
                "' does not have the size, channels and filter array of the stack's first frame");
    }
    const std::optional<an::ImageLayout> packed =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = m_descriptor->extent(),
                                                       .elementSizeBytes = static_cast<std::uint32_t>(sizeof(float)),
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = m_descriptor->extent().channelCount(),
                                                       .rowAlignmentBytes = 1u},
                                m_limits);
    if (!packed.has_value() || packed->totalBytes() > buffer.size())
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments, "a frame does not fit the stack's read slot");
    }
    if (an::Status copied = an::repackImageSamples(execution, an::SampleType::Float32, view->data(), view->layout(),
                                                   buffer.data(), *packed);
        copied.failed())
    {
        return copied;
    }
    m_layout = *packed;
    return an::StackSourceBand{
        .sampleType = an::SampleType::Float32,
        .samples = buffer.data(),
        .layout = &*m_layout,
        .frameIndex = frameIndex,
        .firstRow = 0u,
        .validity = an::StackBandValidity{.descriptor = stackFrameValidity(m_validity), .mask = nullptr}};
}

WorkflowDrizzleFrameSource::WorkflowDrizzleFrameSource(StackFrameReadFunction read, const std::uint32_t frameCount,
                                                       StackFrameNameFunction name)
    : m_read(std::move(read)), m_frameCount(frameCount), m_name(std::move(name))
{
}

an::Result<an::ImageDescriptor> WorkflowDrizzleFrameSource::describe()
{
    if (m_descriptor.has_value())
    {
        return *m_descriptor;
    }
    if (m_frameCount == 0u)
    {
        return an::Status::failure(an::ErrorCode::EmptySelection, "the drizzle has no frame to read");
    }
    an::Result<an::Image<float>> first = m_read(0u);
    if (!first.hasValue())
    {
        return first.status();
    }
    const std::optional<an::ImageDescriptor> described = first->descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the first frame holds nothing");
    }
    m_descriptor = *described;
    m_firstMetadata = first->metadata();
    m_first = std::move(*first);
    return *m_descriptor;
}

const an::ImageMetadata* WorkflowDrizzleFrameSource::firstMetadata() const noexcept
{
    return m_firstMetadata.has_value() ? &*m_firstMetadata : nullptr;
}

an::DrizzleSourceAccess WorkflowDrizzleFrameSource::access() const noexcept
{
    return an::DrizzleSourceAccess::WholeFrame;
}

an::Result<an::DrizzleFrameData> WorkflowDrizzleFrameSource::read(const an::ExecutionContext& execution,
                                                                  const std::uint32_t frameIndex,
                                                                  const std::uint32_t firstRow,
                                                                  const std::uint32_t rows,
                                                                  const an::DrizzleReadBuffers& buffers)
{
    (void)buffers;
    if (an::Status stopped = execution.checkCancellation(); stopped.failed())
    {
        return stopped;
    }
    if (!m_descriptor.has_value())
    {
        const an::Result<an::ImageDescriptor> described = describe();
        if (!described.hasValue())
        {
            return described.status();
        }
    }
    if (frameIndex >= m_frameCount || firstRow != 0u || rows != m_descriptor->extent().height())
    {
        return an::Status::failure(an::ErrorCode::OutOfRange, "the drizzle asked for a frame the group does not hold");
    }
    m_current.reset();
    if (frameIndex == 0u && m_first.has_value())
    {
        m_current = std::move(*m_first);
        m_first.reset();
    }
    else
    {
        an::Result<an::Image<float>> image = m_read(frameIndex);
        if (!image.hasValue())
        {
            return image.status();
        }
        m_current = std::move(*image);
    }
    const std::optional<an::ImageDescriptor> described = m_current->descriptor();
    const auto view = std::as_const(*m_current).view();
    if (!described.has_value() || !view.has_value() || !sameShape(*described, *m_descriptor))
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "'" + displayName(m_name ? m_name(frameIndex) : QString()) +
                                       "' does not have the size, channels and filter array of the drizzle's first "
                                       "frame");
    }
    const std::span<const std::byte> samples(reinterpret_cast<const std::byte*>(view->data()),
                                             static_cast<std::size_t>(view->layout().totalBytes()));
    return an::DrizzleFrameData{
        .pixels =
            an::DrizzleSampleBand{.layout = view->layout(), .samples = samples, .sampleType = an::SampleType::Float32}};
}

ScaledStackFrameSource::ScaledStackFrameSource(an::StackFrameSource& inner, const double factor) noexcept
    : m_inner(inner), m_factor(factor)
{
}

an::StackSourceAccess ScaledStackFrameSource::access() const noexcept
{
    return m_inner.access();
}

an::Result<an::StackSourceBand> ScaledStackFrameSource::read(const an::ExecutionContext& execution,
                                                             const std::uint32_t frameIndex,
                                                             const std::uint32_t firstRow, const std::uint32_t rows,
                                                             const std::span<std::byte> buffer)
{
    an::Result<an::StackSourceBand> band = m_inner.read(execution, frameIndex, firstRow, rows, buffer);
    if (!band.hasValue())
    {
        return band;
    }
    if (band->samples != static_cast<const void*>(buffer.data()) || band->layout == nullptr)
    {
        return an::Status::failure(an::ErrorCode::Internal, "a scaled frame was answered outside the stack's slot");
    }
    an::Status scaled = an::Status::success();
    switch (band->sampleType)
    {
    case an::SampleType::Float32:
        scaled = scaleBand<float>(execution, buffer.data(), *band->layout, m_factor);
        break;
    case an::SampleType::Float64:
        scaled = scaleBand<double>(execution, buffer.data(), *band->layout, m_factor);
        break;
    default:
        scaled = an::Status::failure(an::ErrorCode::Unsupported, "only floating frames are scaled");
        break;
    }
    if (scaled.failed())
    {
        return scaled;
    }
    return band;
}

} // namespace stage_utils
