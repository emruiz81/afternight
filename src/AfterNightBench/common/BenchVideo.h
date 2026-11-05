// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace an::bench
{
/// Writes a lossless FFV1 video in Matroska whose every frame holds the same sixteen-bit little-endian
/// mono samples, with FFmpeg's bitexact flags so two processes linking one FFmpeg write identical bytes.
/// Used by both benchmark runners to prepare the same fixture before timing; engine-neutral.
///
/// @param path where to write; UTF-8 through `std::filesystem::path`.
/// @param samples one frame of `width * height` little-endian sixteen-bit samples.
/// @param width frame width.
/// @param height frame height.
/// @param frames how many frames to write, each a keyframe.
/// @param slices how many independent slices each frame is coded in.
/// @return an empty string, or what failed.
[[nodiscard]] std::string writeFfv1Video(const std::filesystem::path& path, std::span<const std::byte> samples,
                                         int width, int height, int frames, int slices);

/// What an uncompressed AVI stores, read without trusting its header layout.
struct AviFrames
{
    std::string digest; ///< SHA-256 over every frame chunk's samples, in file order.
    std::uint64_t frames = 0u; ///< How many frame chunks the movie lists hold.
    bool everyFrameIs = false; ///< Whether every frame chunk holds exactly the expected samples.
};

/// Walks every RIFF section's `movi` list of an AVI file and hashes its frame chunks, whatever the writer put around
/// them. Engine-neutral.
///
/// @param path the container.
/// @param expected the samples every frame is expected to store, in file order.
/// @param out receives the digest, the count and the comparison.
/// @return an empty string, or what failed.
[[nodiscard]] std::string readAviFrames(const std::filesystem::path& path, std::span<const std::byte> expected,
                                        AviFrames& out);

/// What a coded video stream holds, read through FFmpeg without its container.
struct VideoPackets
{
    std::string digest; ///< SHA-256 over every packet of the first video stream, in demux order.
    std::uint64_t packets = 0u; ///< How many packets the stream holds.
    std::uint64_t frames = 0u; ///< How many frames they decode to.
    double lumaRms = 0.0; ///< Root-mean-square distance of every decoded luma sample from its limited-range code.
    std::string codec; ///< FFmpeg's name for the stream's codec.
};

/// Demuxes and decodes a video file's first video stream. Engine-neutral.
///
/// @param path the container.
/// @param grey one frame of the eight-bit grey samples every frame was encoded from, top row first.
/// @param width the frame width.
/// @param height the frame height.
/// @param out receives the digest, the counts, the luma distance and the codec.
/// @return an empty string, or what failed.
[[nodiscard]] std::string readVideoPackets(const std::filesystem::path& path, std::span<const std::byte> grey,
                                           int width, int height, VideoPackets& out);
} // namespace an::bench
