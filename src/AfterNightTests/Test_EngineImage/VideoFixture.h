// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Test-only video writer. FFmpeg encodes and muxes small videos whose every stored byte is a function
// of where it sits, so a case can compare what a source delivered with what was written. The engine
// has no video encoder of its own, and a committed binary fixture would say nothing about how it was
// made; building each one inside the case states it.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace an::test
{

/// One video to write: a primary stream described here, plus optional flat eight-bit grey streams.
struct VideoFixtureSpec
{
    /// FFmpeg's muxer name: `matroska`, `mp4`, `avi`, `nut`.
    std::string muxer = "matroska";

    /// FFmpeg's encoder name: `ffv1`, `rawvideo`, `mpeg4`, `libx264`.
    std::string encoder = "ffv1";

    /// FFmpeg's pixel format name, such as `gray16le`.
    std::string pixelFormat = "gray16le";

    int width = 16;
    int height = 12;
    int frames = 4;

    /// Keyframe interval and B-frame run the encoder is asked for.
    int gop = 1;
    int bframes = 0;

    /// Encoder options as `key=value:key=value`.
    std::string encoderOptions;

    /// Frame `i` has presentation time `i / timeBaseDenominator` seconds in the encoder.
    int timeBaseDenominator = 25;

    /// FFmpeg's `AVColorRange`, `AVColorSpace`, `AVColorPrimaries` and `AVColorTransferCharacteristic`.
    int colorRange = 0;
    int colorSpace = 2;
    int colorPrimaries = 2;
    int colorTransfer = 2;

    /// Sample aspect ratio; `0/1` states none.
    int sarNumerator = 0;
    int sarDenominator = 1;

    /// When not empty, every sample of plane `p` of frame `i` holds `flatLevels[i % size][p]`, an
    /// eight-bit level shifted up for wider components. Otherwise each byte holds `storedByte`.
    std::vector<std::array<int, 4>> flatLevels;

    /// Attaches each frame's palette to its packet, which is how an AVI stores one.
    bool paletteSideData = false;

    /// Writes a palette whose entries are all zero.
    bool zeroPalette = false;

    /// Frame `i` stores palette entry `(e + 17 * i) % 256` at position `e`, so every frame changes it.
    bool varyPalette = false;

    /// How many flat grey `rawvideo` streams follow the primary one; every sample holds 200.
    int extraStreams = 0;
};

/// The byte a patterned frame stores at plane `plane`, row `y`, byte `x` of the row.
[[nodiscard]] std::uint8_t storedByte(int plane, int x, int y, int frame) noexcept;

/// The palette index a paletted frame stores at pixel (`x`, `y`).
[[nodiscard]] std::uint8_t paletteIndex(int x, int y, int frame) noexcept;

/// The palette entry at `index`, as FFmpeg's native-endian `0xAARRGGBB`.
[[nodiscard]] std::uint32_t paletteEntry(int index) noexcept;

/// Whether this build's FFmpeg carries the muxer and encoder a spec names.
[[nodiscard]] bool canWriteVideo(const VideoFixtureSpec& spec);

/// Writes a video. Returns an empty string, or what failed.
[[nodiscard]] std::string writeVideo(const std::filesystem::path& path, const VideoFixtureSpec& spec);

/// Silences FFmpeg's process-wide log for the rest of the test process. A test process owns its own
/// log; the engine never calls this.
void quietFfmpegLog();

/// The device a nominated host requires a video case to have used, named by an environment variable.
///
/// A case that needs a device skips where none ran; the variable turns that skip into a failure, because on a host
/// that has the device a skipped case is a proof that never ran.
///
/// @param variable the variable's name.
/// @return its value when it is set and nonempty, otherwise nothing.
[[nodiscard]] std::optional<std::string> requiredVideoDevice(const char* variable);

} // namespace an::test
