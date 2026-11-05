// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - engine-neutral descriptors.
//
// Nothing in this library may include or link AfterNightEngine. Its oracles judge the engine's
// answers, so they must not be computed by it, and its suite tests them without the engine.
//
// These descriptors therefore describe pixel data in the abstract - enough for the runner to build
// the input its engine wants, and enough to prove two runs described the same thing - without
// naming the engine's image type.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace an::bench
{

/// One stored sample's representation: every integer width, signed and unsigned, and both
/// floating-point widths, so a scenario can describe any boundary input.
enum class SampleType : std::uint8_t
{
    UInt8,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    Int64,
    UInt64,
    Float32,
    Float64
};

/// How samples of a multi-channel image are ordered in memory.
enum class Layout : std::uint8_t
{
    Planar, ///< Channel planes back to back; the canonical engine layout.
    Interleaved, ///< Samples of one pixel adjacent; common at codec boundaries.
    SemiPlanar, ///< One plane plus one interleaved remainder, as some video formats use.
    PackedCfa ///< Undemosaiced sensor data; `cfaPattern` carries the mosaic.
};

enum class ByteOrder : std::uint8_t
{
    Native,
    Little,
    Big
};

/// How a scenario's output is judged: exactly, or within a stated tolerance of its oracle. The
/// scenario declares this up front so a run cannot quietly downgrade its own correctness bar.
enum class CorrectnessClass : std::uint8_t
{
    BitExact, ///< Byte-identical output.
    ExactAfterMapping, ///< Identical after a declared integer mapping.
    MaxUlp, ///< Within `tolerance` ULP.
    AbsoluteRelative, ///< Within an absolute plus relative tolerance.
    LsbDistribution, ///< Least-significant-bit error distribution within bounds.
    RmsPsnr, ///< Statistical image similarity, where pixelwise equality is not intended.
    GeometricResidual, ///< Transform or inlier residual, for registration-class results.
    EstimatorTolerance, ///< Statistical estimator agreement.
    MetadataExact ///< Metadata, provenance, and output cards must match exactly.
};

/// Which implementation a scenario may run on. A forced backend that is unavailable must be
/// reported as skipped, never silently replaced by a fallback.
enum class BackendId : std::uint8_t
{
    Unspecified,
    Scalar,
    CompilerVectorized,
    PortableSimd,
    Sse2,
    Sse42,
    Avx,
    Avx2,
    Avx512,
    Neon,
    Gpu
};

/// Size and shape of one benchmark input or output buffer.
///
/// Strides are explicit and may exceed the minimum so a scenario can exercise noncanonical
/// layouts. A zero stride means "tightly packed", resolved by `rowBytes()`.
struct SampleDescriptor
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 1;
    SampleType sampleType = SampleType::Float32;
    Layout layout = Layout::Planar;
    ByteOrder byteOrder = ByteOrder::Native;
    std::uint64_t rowStrideBytes = 0; ///< 0 = tightly packed.
    std::uint64_t planeStrideBytes = 0; ///< 0 = tightly packed; planar layouts only.
    std::string cfaPattern; ///< e.g. "RGGB"; empty when not a CFA image.

    /// Bytes in one sample of `sampleType`.
    [[nodiscard]] std::uint32_t sampleSizeBytes() const;

    /// Bytes in one row, honouring `rowStrideBytes` when it is set.
    [[nodiscard]] std::uint64_t rowBytes() const;

    /// Total bytes this descriptor implies, or 0 if the arithmetic would overflow.
    ///
    /// Overflow returns 0 rather than trapping because a scenario file is untrusted input: the
    /// caller is expected to treat 0 as "reject this descriptor", which `validate` does.
    [[nodiscard]] std::uint64_t totalBytes() const;

    /// True when width, height, channels, and the derived byte counts are all usable.
    [[nodiscard]] bool isValid() const;

    friend bool operator==(const SampleDescriptor& lhs, const SampleDescriptor& rhs);
};

// Enum name mapping. Every enum is written to JSON as its name, never its ordinal, so inserting a
// value cannot silently reinterpret an archived result set - which matters because a baseline
// bundle is read years after it is written.
std::string_view toString(SampleType value);
std::string_view toString(Layout value);
std::string_view toString(ByteOrder value);
std::string_view toString(CorrectnessClass value);
std::string_view toString(BackendId value);

bool fromString(std::string_view text, SampleType& out);
bool fromString(std::string_view text, Layout& out);
bool fromString(std::string_view text, ByteOrder& out);
bool fromString(std::string_view text, CorrectnessClass& out);
bool fromString(std::string_view text, BackendId& out);

} // namespace an::bench
