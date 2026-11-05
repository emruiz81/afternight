// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How the lights stacking stage reads its frames: each light, registered light or comet-aligned light read through the
/// run's codec registry as Float32 fractions of full scale, calibrated by its masters when the run calibrates in
/// memory, demosaiced unless a drizzle reads its filter array - under the pattern the drizzle states, when it states
/// one - and warped onto the registration's plane when the stack reads the registration's inputs rather than its
/// registered lights; the two sources the engine reads them through, one for `executeStack` and one for a drizzle; and
/// the source that restates another's frames by one factor, which brings a drizzled frame back to its lights' level.
///
/// **A frame is read when the stack asks for it, and held to the file it was planned from.** A frame planned with the
/// revision its file had - its size, modification time and platform identity - is refused, naming it, when that file
/// has changed by the time it is read or while it is read, so a stack never combines a light that changed after its
/// fingerprint was taken.
///
/// **The description comes from the first frame.** The stack is told what every frame is before it reads one; a source
/// reads its first frame to learn that, keeps it for the stack's first read, and holds every later frame to it.
///
/// **A sample that is not a number means nothing on every route.** A calibration writes one where its flat is zero or
/// a sample it reads means nothing, and a frame on the registration's plane carries it there. A light as it was read
/// states that every finite sample means something; a frame on the registration's plane, where an exact zero is where
/// the frame does not reach, has every sample that is not a number restated as that zero, so the stack leaves both out
/// where the engine would otherwise refuse the frame.
///
/// **A file read as it is, is read a band at a time.** When every frame of a stack is a file the stack combines as it
/// is stored - not calibrated in memory, not warped, not a mosaic the drizzle keeps or a demosaic makes colour, its
/// samples Float32 - the stack source reads each frame's rows as the stack asks for them, through one row reader per
/// frame straight into the slot the stack lends the read, and the stack streams every frame's band of a tile from
/// disk instead of holding every frame whole: the working set is a tile's bytes, and a stack bounded by memory reads
/// its input once rather than once per tile. The samples are the whole read's, bit for bit.

#include "AfterNightProcessesExport.h"
#include "processes/ImageIntegration/model/FrameId.h"
#include "processes/ImageIntegration/workflow/ArtifactTracker.h"
#include "processes/ImageIntegration/workflow/WorkflowManifest.h"
#include "processes/core/RegistrationRun.h"

#include <AfterNightEngine/Algorithms/Astrometry/SipDistortion.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzleSource.h>
#include <AfterNightEngine/Algorithms/Kernel/SamplingKernel.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationPlan.h>
#include <AfterNightEngine/Algorithms/Registration/RegistrationTransform.h>
#include <AfterNightEngine/Algorithms/Stacking/StackFrameSource.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/IO/CodecIO.h>

#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace an
{
class CodecRegistry;
} // namespace an

namespace an::consumer
{
class OperationScope;
} // namespace an::consumer

namespace core
{
class CalibrationMasterSet;
} // namespace core

namespace stage_utils
{

/// One frame the stack reads.
struct StackFrameRead
{
    /// The light the frame stands for: the one the group lists, which names it in a message and in the normalization
    /// coefficients.
    FrameId logical;

    /// The file the frame's samples are read from.
    QString path;

    /// The light's calibration, when the run calibrates in memory and the file is the light itself.
    const WorkflowManifest::LightsCalibrationParams* calibration = nullptr;

    /// Whether the frame is warped onto the plan's plane after it is read, and through what.
    bool warp = false;
    an::RegistrationTransform transform;
    std::optional<an::SipDistortion> distortion;

    /// The revision the file is held to; nothing holds it to none.
    std::optional<FileRevisionStamp> revision;
};

/// How a stack reads its frames.
struct StackFramePlan
{
    std::vector<StackFrameRead> frames;

    /// The plane a warped frame lands on, and how it is sampled.
    an::RegistrationCanvas canvas{};
    an::KernelInterpolation interpolation = an::KernelInterpolation::Lanczos4;

    /// How a mosaic is demosaiced, and whether it is: a drizzle that reads the filter array keeps the mosaic.
    an::ui::processes::RegistrationDemosaic demosaic;
    bool keepMosaic = false;

    /// The filter array a kept mosaic is read under when the drizzle states one, placed at the light's origin over the
    /// one its header states or where it states none, as the registration applies its own; nothing reads the
    /// header's.
    std::optional<an::CfaPattern> mosaicPattern;

    /// Which samples of a frame mean something: every finite one for a light as it was read, which a calibration leaves
    /// not a number where its flat is zero; everything but an exact zero for a frame on the registration's plane, where
    /// a zero is where the frame does not reach and a sample that is not a number is read as one.
    an::ValidityRule validity = an::ValidityRule::FiniteOnly;
};

/// Reads the frames of a plan, each in the caller's admitted operation.
class AFTERNIGHTPROCESSES_API StackFrameReader
{
public:
    /// @param scope the operation every read runs in; it outlives the reader.
    /// @param registry the run's registry, or null for the engine context's.
    /// @param plan the frames; it outlives the reader.
    /// @param masters the run's masters, read once for every frame that names them.
    StackFrameReader(const an::consumer::OperationScope& scope, const an::CodecRegistry* registry,
                     const StackFramePlan& plan, core::CalibrationMasterSet& masters);

    /// Reads one frame: decoded, widened to Float32 fractions, calibrated, demosaiced unless the plan keeps the mosaic,
    /// and warped when the frame asks for it.
    ///
    /// @param index the frame, an index into the plan's frames.
    /// @return the frame; `ErrorCode::InvalidState` naming the file when it changed since it was planned; or what
    ///         reading, calibrating, demosaicing or warping refused, the file named.
    [[nodiscard]] an::Result<an::Image<float>> read(std::size_t index);

    /// The plan read.
    [[nodiscard]] const StackFramePlan& plan() const noexcept
    {
        return m_plan;
    }

    /// The operation the reads run in.
    [[nodiscard]] const an::consumer::OperationScope& scope() const noexcept
    {
        return m_scope;
    }

    /// The registry the reads select their adapter from, or null for the engine context's.
    [[nodiscard]] const an::CodecRegistry* registry() const noexcept
    {
        return m_registry;
    }

private:
    const an::consumer::OperationScope& m_scope;
    const an::CodecRegistry* m_registry;
    const StackFramePlan& m_plan;
    core::CalibrationMasterSet& m_masters;
};

/// The validity a band of a plan states.
[[nodiscard]] an::ValidityDescriptor stackFrameValidity(an::ValidityRule rule);

/// Restates every sample of a frame that is not a finite number as an exact zero: how a frame read with an exact zero
/// meaning nothing - one on the registration's plane - leaves out a sample a calibration wrote as not a number, with
/// the samples the frame does not reach, where a rule that admits it would have the engine refuse the frame.
///
/// @param frame the frame, changed in place.
/// @return how many samples were restated; or `ErrorCode::NotInitialized` for a frame that holds nothing.
[[nodiscard]] an::Result<std::uint64_t> zeroNonFiniteSamples(an::Image<float>& frame);

/// Reads one frame of a stack or a drizzle, by its index in the frames the engine is told of.
using StackFrameReadFunction = std::function<an::Result<an::Image<float>>(std::uint32_t frame)>;

/// What names a frame in a message.
using StackFrameNameFunction = std::function<QString(std::uint32_t frame)>;

/// The most row readers a source reading bands holds open at once. Each holds its file open - a FITS file through the C
/// runtime's streams, of which a Windows process has 512 by default, and two descriptors of a Linux process's - so the
/// frames past this many read each band through a reader opened for it.
inline constexpr std::uint32_t kMostOpenStackFrameReaders = 128u;

/// What a stack source needs to read its frames a band at a time.
struct StackFrameBands
{
    /// The operation the bands are read in; it outlives the source.
    const an::consumer::OperationScope* scope = nullptr;

    /// The registry the readers select their adapter from, or null for the engine context's; it outlives the source.
    const an::CodecRegistry* registry = nullptr;

    /// Each frame, in the order the stack reads them, borrowed for the source's life.
    std::vector<const StackFrameRead*> frames;

    /// How many readers stay open for the whole stack; the frames past them open one for each band.
    std::uint32_t mostOpenReaders = kMostOpenStackFrameReaders;
};

/// Whether a stack may read its frames a band at a time, and why it reads them whole when it may not.
struct StackFrameBandPlan
{
    /// The band reads, when every frame is a file the stack combines as it is read; null otherwise.
    std::optional<StackFrameBands> bands;

    /// Why the frames are read whole, for the run's log; empty when they may be read by bands.
    QString wholeFrameReason;
};

/// How many bytes of every frame's band one tile of a stack whose source reads bands holds at most.
inline constexpr std::uint64_t kStackBandTileBytes = 16u * 1024u * 1024u;

/// The most rows one tile of a stack whose source reads bands may hold.
inline constexpr std::uint32_t kStackBandTileMostRows = 1024u;

/// How many rows of every frame one tile of a stack holds when its source reads bands.
///
/// A source that reads bands decodes every frame's band of a tile into the tile at once, and the stack folds the tile
/// after its last band lands, so a tile is kept to `kStackBandTileBytes` of every frame's band - which keeps a stack
/// that keeps no sample history from holding every frame whole, where the planner gives it no band of its own - and to
/// at least one row per worker, because the fold spreads a tile's rows over them. A stack that keeps a sample history
/// is given a smaller band by the planner, which this does not widen. A source whose files are decoded in pieces that
/// span more rows than that - compressed subblocks - decodes a piece again for every tile that reaches it, so the tile
/// may hold the rows one piece spans, whatever the cache: the planner widens it to them where the memory allows.
///
/// @param frameCount how many frames the stack reads.
/// @param width the frames' width.
/// @param channels the frames' channels.
/// @param workers the operation's admitted width; zero is read as one.
/// @param granularityRows the rows the smallest piece the source decodes whole spans, one for none.
/// @return at least one row, and at most `kStackBandTileMostRows` or the granularity, whichever is more.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::uint32_t stackBandTileRows(std::uint32_t frameCount, std::uint32_t width,
                                                                      std::uint32_t channels, std::uint32_t workers,
                                                                      std::uint32_t granularityRows = 1u);

/// The band plan of a stack over some of a plan's frames: bands when the plan reads its files as they are - no frame
/// calibrated in memory or warped, no mosaic kept for a drizzle - and whole frames, with the reason, otherwise. Whether
/// each file can then be read by bands is the source's to find when it describes the stack.
///
/// @param reader the reader of the plan, whose operation and registry the bands are read through.
/// @param frames the plan's frames the stack reads, in its order.
/// @return the plan.
[[nodiscard]] AFTERNIGHTPROCESSES_API StackFrameBandPlan stackFrameBandPlan(const StackFrameReader& reader,
                                                                            std::span<const std::uint32_t> frames);

/// Frames as a stack source, in one of two forms decided when the stack is described.
///
/// **Whole frames**: each read whole when the stack asks for it and packed planar into the stack's read slot, held to
/// the description of the first. **Bands**, when the source is given band reads and every frame qualifies - a file
/// whose codec reads bands of rows, stored as Float32 in planes, not a mosaic, of the first frame's size and channels,
/// unchanged since it was planned: each frame's rows read through a row reader into the slot the stack lends the read,
/// reads of different frames on the operation's workers at once (`StackSourceAccess::ConcurrentRegion`), and a frame
/// held to its revision when it is opened and when its last band is read. The bands are the rows of the whole read.
///
/// Under `ValidityRule::ZeroIsNoData` a sample that is not a number is read as an exact zero (`zeroNonFiniteSamples`),
/// in both forms.
class AFTERNIGHTPROCESSES_API WorkflowStackFrameSource final : public an::StackFrameSource
{
public:
    /// @param read reads a frame whole.
    /// @param frameCount how many frames there are.
    /// @param validity which samples of a frame mean something.
    /// @param name names a frame in a message.
    /// @param limits the engine's limits, which the read slot's layout is held to.
    /// @param bands whether the frames may be read a band at a time; whole frames when it carries none.
    WorkflowStackFrameSource(StackFrameReadFunction read, std::uint32_t frameCount, an::ValidityRule validity,
                             StackFrameNameFunction name, const an::EngineLimits& limits,
                             StackFrameBandPlan bands = {});

    /// Decides the form: opens every frame's row reader and reads the first frame's header when the bands qualify, and
    /// otherwise reads the first frame whole and keeps it for the stack's first read.
    ///
    /// @return what every frame is, as the whole read describes it in either form; or what reading the first frame
    ///         refused, or the operation's stop.
    [[nodiscard]] an::Result<an::ImageDescriptor> describe();

    /// The metadata of the first frame, once `describe` has read it: its file's, as the whole read states it.
    [[nodiscard]] const an::ImageMetadata* firstMetadata() const noexcept;

    /// Whether the frames are read a band at a time, once `describe` has decided.
    [[nodiscard]] bool readsBands() const noexcept
    {
        return m_readsBands;
    }

    /// Why the frames are read whole, once `describe` has decided; empty when they are read by bands.
    [[nodiscard]] const QString& wholeFrameReason() const noexcept
    {
        return m_bandPlan.wholeFrameReason;
    }

    /// How many rows the smallest piece any frame's reader decodes whole spans, once `describe` has decided on bands:
    /// one for files read as they are stored, more for compressed ones (`an::ImageRowReader::decodeGranularityRows`);
    /// one for whole frames, which are read once each.
    [[nodiscard]] std::uint32_t decodeGranularityRows() const noexcept
    {
        return m_decodeGranularityRows;
    }

    [[nodiscard]] an::StackSourceAccess access() const noexcept override;

    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override;

    /// How many frames were read whole, over every pass.
    [[nodiscard]] std::uint64_t framesRead() const noexcept
    {
        return m_framesRead;
    }

    /// How many bands were read, over every pass.
    [[nodiscard]] std::uint64_t bandsRead() const noexcept
    {
        return m_bandsRead.load(std::memory_order_relaxed);
    }

private:
    /// Opens every frame's row reader and checks each against the first; false, with the reason, when one does not
    /// qualify; the operation's stop when it is stopped.
    [[nodiscard]] an::Result<bool> openBands();

    /// Reads one frame's band through its reader, or through one opened for the band past the open ones.
    [[nodiscard]] an::Result<an::StackSourceBand> readBand(const an::ExecutionContext& execution,
                                                           std::uint32_t frameIndex, std::uint32_t firstRow,
                                                           std::uint32_t rows, std::span<std::byte> buffer);

    StackFrameReadFunction m_read;
    std::uint32_t m_frameCount = 0u;
    an::ValidityRule m_validity = an::ValidityRule::AllValid;
    StackFrameNameFunction m_name;
    an::EngineLimits m_limits;
    StackFrameBandPlan m_bandPlan;
    bool m_readsBands = false;
    std::uint32_t m_decodeGranularityRows = 1u;
    std::optional<an::ImageDescriptor> m_descriptor;
    std::optional<an::ImageMetadata> m_firstMetadata;
    std::optional<an::Image<float>> m_first;
    std::optional<an::ImageLayout> m_layout;
    std::uint64_t m_framesRead = 0u;
    /// Each frame's file, as the readers are given it.
    std::vector<std::string> m_paths;
    /// The open readers, one per frame below the open count and none past it.
    std::vector<an::ImageRowReader> m_readers;
    /// Each frame's last band's layout, which the stack borrows until that frame's next read.
    std::vector<std::optional<an::ImageLayout>> m_bandLayouts;
    std::atomic<std::uint64_t> m_bandsRead{0u};
};

/// Frames as a drizzle source: each read whole as the drizzle asks for it, kept until the next read.
class WorkflowDrizzleFrameSource final : public an::DrizzleFrameSource
{
public:
    /// @param read reads a frame, as it is before it is drizzled.
    /// @param frameCount how many frames there are.
    /// @param name names a frame in a message.
    WorkflowDrizzleFrameSource(StackFrameReadFunction read, std::uint32_t frameCount, StackFrameNameFunction name);

    /// Reads the first frame and keeps it for the drizzle's first read.
    ///
    /// @return what every frame is, before it is drizzled; or what reading the first frame refused.
    [[nodiscard]] an::Result<an::ImageDescriptor> describe();

    /// The metadata of the first frame read, once `describe` has read it.
    [[nodiscard]] const an::ImageMetadata* firstMetadata() const noexcept;

    [[nodiscard]] an::DrizzleSourceAccess access() const noexcept override;

    [[nodiscard]] an::Result<an::DrizzleFrameData> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                        std::uint32_t firstRow, std::uint32_t rows,
                                                        const an::DrizzleReadBuffers& buffers) override;

private:
    StackFrameReadFunction m_read;
    std::uint32_t m_frameCount = 0u;
    StackFrameNameFunction m_name;
    std::optional<an::ImageDescriptor> m_descriptor;
    std::optional<an::ImageMetadata> m_firstMetadata;
    std::optional<an::Image<float>> m_first;
    std::optional<an::Image<float>> m_current;
};

/// Another stack source's frames, every sample multiplied by one factor in the slot the stack lends the read: a
/// drizzled frame - which the engine renders at its lights' level divided by the scale squared - restated at its
/// lights' level. A sample no light reached is not a number and stays one. The inner source must answer each read in
/// the slot it is lent, with Float32 or Float64 samples; it is borrowed for this source's life and read where it is,
/// never copied.
class ScaledStackFrameSource final : public an::StackFrameSource
{
public:
    /// @param inner the source read.
    /// @param factor what every sample is multiplied by, finite and positive.
    ScaledStackFrameSource(an::StackFrameSource& inner, double factor) noexcept;

    /// The inner source's access.
    [[nodiscard]] an::StackSourceAccess access() const noexcept override;

    /// Reads the inner source's band and multiplies every sample of it by the factor, in the stack's slot.
    ///
    /// @return the inner band, its samples scaled; what the inner read refused; `ErrorCode::Internal` for a band
    ///         answered outside the slot; or `ErrorCode::Unsupported` for samples that are not floating.
    [[nodiscard]] an::Result<an::StackSourceBand> read(const an::ExecutionContext& execution, std::uint32_t frameIndex,
                                                       std::uint32_t firstRow, std::uint32_t rows,
                                                       std::span<std::byte> buffer) override;

    /// What every sample is multiplied by.
    [[nodiscard]] double factor() const noexcept
    {
        return m_factor;
    }

private:
    an::StackFrameSource& m_inner;
    double m_factor = 1.0;
};

} // namespace stage_utils
