// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What the consumer-shape probes share, and why none of it is the engine's.
//
// Every probe needs somewhere to put files, a set of codecs to read and write them with, and frames
// that look like what a camera writes. None of that is a question about the engine's shape: a CLI, a
// stacking workflow and a preview all have a scratch directory and a sky, and writing those three
// things out again in each probe would bury the orchestration each probe exists to show. So they are
// here, once, written the way a consumer writes its own utilities - against the published include
// root and nothing else.
//
// **The synthetic frames are a camera's, not an operation's.** They are sixteen-bit sensor codes with
// a pedestal, a thermal signal proportional to the exposure, a vignetted flat response, read noise
// from a seeded hash and a field of Gaussian stars shifted by a stated offset. They are written
// through the engine's own output transaction with the cards a capture program writes, so a probe
// that opens one meets exactly what it would meet opening a file from disk.

#pragma once

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ProgressSink.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace probe
{

/// A directory of the probe's own under the system's temporary directory, removed with everything in
/// it when this goes away.
///
/// Unique per instance, so two probes running at once - or two runs of the same probe under a
/// sanitizer's repeat - never meet each other's files.
class ScratchDirectory
{
public:
    /// Creates the directory.
    ///
    /// A failure to create it leaves `valid()` false rather than throwing, so a test can assert on
    /// it with a message instead of dying in a fixture.
    ScratchDirectory();

    /// Removes the directory and everything in it. A removal that fails is ignored: a probe that
    /// passed should not fail because the operating system still held a handle for a moment.
    ~ScratchDirectory();

    /// Copying is deleted: two owners would remove one directory twice.
    ScratchDirectory(const ScratchDirectory&) = delete;

    /// Copying is deleted. See the copy constructor.
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;

    /// Whether the directory exists.
    ///
    /// @return true when creating it succeeded.
    [[nodiscard]] bool valid() const noexcept;

    /// The directory itself.
    ///
    /// @return the absolute path.
    [[nodiscard]] const std::filesystem::path& root() const noexcept;

    /// A path inside the directory, as the UTF-8 text the engine's path parameters take.
    ///
    /// @param relative the name, which may contain a subdirectory.
    /// @return the path.
    [[nodiscard]] std::string file(std::string_view relative) const;

    /// Creates a subdirectory, and every directory above it.
    ///
    /// @param relative the name.
    /// @return true when the subdirectory exists afterwards.
    [[nodiscard]] bool makeDirectory(std::string_view relative) const;

    /// Every file under a directory inside this one, by name, sorted.
    ///
    /// What a test reads to prove that a stopped write left nothing behind: not just no destination,
    /// but no pending sibling either.
    ///
    /// @param relative the subdirectory, or empty for the root.
    /// @return the file names.
    [[nodiscard]] std::vector<std::string> fileNames(std::string_view relative = {}) const;

private:
    /// The directory.
    std::filesystem::path m_root;

    /// Whether creating it succeeded.
    bool m_valid = false;
};

/// The codecs a consumer installs: FITS, XISF, TIFF, PNG and JPEG.
///
/// Built before any engine context exists, because a registry is immutable configuration the context
/// is given rather than a service it starts.
///
/// @return the registry, or whatever creating an adapter or the registry reported.
[[nodiscard]] an::Result<std::shared_ptr<const an::CodecRegistry>> createProbeCodecRegistry();

/// An engine context configured the way a consumer configures one.
///
/// @param maxConcurrency the thread budget, or zero for the host's.
/// @param memoryBudgetBytes the memory budget, or zero for the engine's default.
/// @return the context with the probe codec registry installed, or whatever building either reported.
[[nodiscard]] an::Result<an::EngineContext> createProbeEngine(std::uint32_t maxConcurrency,
                                                              std::uint64_t memoryBudgetBytes = 0u);

/// A failure as one line for a person: the code's name, the message, and the root cause's message
/// when there is one.
///
/// @param status the failure.
/// @return the text. Never parsed.
[[nodiscard]] std::string describeFailure(const an::Status& status);

/// What kind of exposure a synthetic sensor frame is.
enum class ExposureKind : std::uint8_t
{
    /// A zero-length exposure: the pedestal and read noise only.
    Bias,

    /// A dark: pedestal, thermal signal and read noise.
    Dark,

    /// A flat: pedestal, thermal signal and an illuminated vignetted field.
    Flat,

    /// A light: pedestal, thermal signal, a vignetted sky background and stars.
    ///
    /// Deliberately the last enumerator, so a loop over the kinds needs no separate count.
    Light
};

/// Everything that decides one synthetic sensor frame.
struct SensorFrameRequest
{
    /// The frame's width in pixels.
    std::uint32_t width = 160u;

    /// The frame's height in pixels.
    std::uint32_t height = 120u;

    /// What kind of exposure it is.
    ExposureKind kind = ExposureKind::Light;

    /// The exposure length, which scales the thermal signal and is written as a card.
    double exposureSeconds = 120.0;

    /// How far the star field is shifted to the right, in pixels, as a mount's drift shifts it.
    double shiftX = 0.0;

    /// How far the star field is shifted down, in pixels.
    double shiftY = 0.0;

    /// A multiplier on every star's amplitude and on the sky, as transparency changes it.
    double transparency = 1.0;

    /// The noise seed. Two frames with the same request and seed are identical.
    std::uint64_t seed = 1u;
};

/// A synthetic sensor frame's samples, row-major, one channel.
///
/// @param request what to render.
/// @return exactly `width * height` sensor codes.
[[nodiscard]] std::vector<std::uint16_t> renderSensorFrame(const SensorFrameRequest& request);

/// The name a capture program writes into `IMAGETYP` for an exposure kind.
///
/// @param kind the kind.
/// @return a null-terminated literal.
[[nodiscard]] const char* exposureKindCard(ExposureKind kind) noexcept;

/// Renders a synthetic frame and writes it as a sixteen-bit FITS file, the way a capture program does.
///
/// Written through the engine's output transaction with `IMAGETYP`, `EXPTIME` and `OBJECT` cards, so
/// the probe that opens it meets a file rather than a buffer.
///
/// @param engine the context whose codec registry writes it.
/// @param path where to write it.
/// @param request what to render.
/// @return success, or whatever describing or writing the frame reported.
[[nodiscard]] an::Status writeSensorFrame(const an::EngineContext& engine, const std::string& path,
                                          const SensorFrameRequest& request);

/// A progress sink that remembers what it was told, safe to share between operations and threads.
///
/// The display a consumer would draw from, reduced to what a test can read: how many reports arrived,
/// from how many distinct operations, and under which stage names.
class RecordingProgress final : public an::ProgressSink
{
public:
    /// Records one report.
    ///
    /// @param report what the operation said.
    void onProgress(const an::ProgressReport& report) noexcept override;

    /// How many reports arrived.
    ///
    /// @return the count.
    [[nodiscard]] std::uint64_t reports() const;

    /// How many distinct operations reported at least once.
    ///
    /// @return the count.
    [[nodiscard]] std::size_t operations() const;

    /// Every distinct stage name reported, in the order each was first seen.
    ///
    /// @return the names.
    [[nodiscard]] std::vector<std::string> stages() const;

    /// Whether any operation's completed count ever exceeded the total it stated beside it.
    ///
    /// @return true when a report said more was done than there was to do.
    [[nodiscard]] bool overran() const;

private:
    /// Guards every member: reports arrive on whichever thread the operation is using.
    mutable std::mutex m_mutex;

    /// The report count.
    std::uint64_t m_reports = 0u;

    /// The distinct operation identities.
    std::vector<std::uint64_t> m_operations{};

    /// The distinct stage names, in first-seen order.
    std::vector<std::string> m_stages{};

    /// Whether a completed count exceeded its nonzero total.
    bool m_overran = false;
};

} // namespace probe
