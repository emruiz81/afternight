// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// An engine-neutral description of a test image, and the samples it stands for.
//
// A test that builds its inputs through the engine's own image API puts the engine into every fixture and makes
// every expected value whatever the engine happens to compute. The fixture layers separate the three things a test
// needs: this neutral layer says what an input is and what its samples are, and links no engine; the engine adapter
// turns it into the engine's image, in executables of its own; and the harness owns what a suite runs inside.
// Nothing here names the engine, so a description, its samples and its digest mean the same thing to every suite,
// and an expected result computed from them is independent of the engine.
//
// A description is stated in stored units: a sixteen-bit gradient says 1000 plus 500 per column, not a fraction
// of full scale. Materializing it quantizes each value once - nearest, ties to even, saturated to the
// representation's range for an integer type, the IEEE conversion for Float32 - so the adapter copies the same
// bytes every suite computes rather than deciding how to convert.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// What one stored sample is: the eight representations the engine holds in memory.
enum class FixtureSampleType : std::uint8_t
{
    UInt8,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32,
    Float64
};

/// What the channels are.
enum class FixtureChannels : std::uint8_t
{
    Mono,
    Rgb
};

/// The colour filter array over a mono frame, named by the colours of its top-left two-by-two cell.
enum class FixtureCfa : std::uint8_t
{
    None,
    Rggb,
    Bggr,
    Grbg,
    Gbrg
};

/// What the stored numbers mean, stated rather than inferred from the representation.
enum class FixtureDomain : std::uint8_t
{
    /// A fraction of full scale: zero to one for a float, the whole range for an integer.
    NormalizedLinear,
    /// The number a sensor reported, before calibration says what it stands for.
    SensorCode,
    /// A physical quantity; floating representations only.
    PhysicalScaled
};

/// How the samples are generated before the overrides are applied.
enum class FixturePattern : std::uint8_t
{
    /// Every sample of a channel is its `base`.
    Constant,
    /// `base + perColumn * x + perRow * y`.
    Gradient,
    /// `base` on even cells and `alternate` on odd ones, cells `checkerCell` pixels square.
    Checker,
    /// `base + amplitude * u`, with `u` in [-1, 1) from a hash of the seed, the pixel and the channel.
    Noise,
    /// The gradient plus the noise: `base + perColumn * x + perRow * y + amplitude * u`.
    GradientNoise,
    /// The gradient plus the noise plus `alternate` times the sum of the stars' profiles, so a channel scales every
    /// star by its own factor. A star's profile is `peak / (1 + r^2 / width^2)^2` at distance `r` from its centre - a
    /// Moffat profile of index two, built from arithmetic alone so that every host computes the same samples.
    StarField
};

/// One star of a `FixturePattern::StarField`, centred at a pixel position that may fall between pixel centres.
struct FixtureStar
{
    double x = 0.0;
    double y = 0.0;
    /// The profile's width parameter, in pixels; the profile falls to a quarter of its peak at this distance.
    double width = 1.0;
    double peak = 0.0;
};

/// One channel's pattern parameters, in stored units.
struct FixtureChannelLevels
{
    double base = 0.0;
    double perColumn = 0.0;
    double perRow = 0.0;
    double alternate = 0.0;
    double amplitude = 0.0;
};

/// One sample replaced after the pattern: how a fixture states a non-finite value, a signed zero or a single
/// outlier.
struct FixtureSampleOverride
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;
    std::uint32_t channel = 0u;
    double value = 0.0;
};

/// How a header card's value is written.
enum class FixtureCardKind : std::uint8_t
{
    Text,
    Integer,
    Real,
    Logical
};

/// One header card a fixture carries into a file or an engine's metadata.
struct FixtureCard
{
    std::string keyword;
    /// The value as text: the string itself for `Text`, the number for `Integer` and `Real`, `T` or `F` for
    /// `Logical`.
    std::string value;
    std::string comment;
    FixtureCardKind kind = FixtureCardKind::Text;
};

/// Everything that decides one test image. Two equal descriptions materialize to equal bytes on every host.
struct FixtureImage
{
    /// A stable name; expectations captured from this image name it.
    std::string name;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    FixtureChannels channels = FixtureChannels::Mono;
    FixtureSampleType sampleType = FixtureSampleType::Float32;
    FixtureDomain domain = FixtureDomain::NormalizedLinear;
    FixtureCfa cfa = FixtureCfa::None;
    FixturePattern pattern = FixturePattern::Constant;
    /// The checker's cell size in pixels; read by `FixturePattern::Checker` only.
    std::uint32_t checkerCell = 1u;
    /// The noise seed; read by `FixturePattern::Noise` only.
    std::uint64_t seed = 0u;
    /// One entry per channel.
    std::vector<FixtureChannelLevels> levels;
    std::vector<FixtureSampleOverride> overrides;
    std::vector<FixtureCard> cards;
    /// Read by `FixturePattern::StarField` only, and refused on any other pattern.
    std::vector<FixtureStar> stars;
};

/// The samples a description stands for: planar, one plane per channel, rows of exactly the width, in the
/// description's row order (row zero is the top row) and in little-endian byte order.
class FixtureSamples
{
public:
    /// Samples of this shape and representation, every byte zero. Empty when the shape has no pixel.
    FixtureSamples(std::uint32_t width, std::uint32_t height, std::uint32_t channelCount, FixtureSampleType sampleType);

    FixtureSamples() = default;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t channelCount() const noexcept;
    [[nodiscard]] FixtureSampleType sampleType() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    /// Every sample's bytes, plane after plane.
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::span<std::byte> mutableBytes() noexcept;

    /// One channel's plane; empty for a channel outside the image.
    [[nodiscard]] std::span<const std::byte> plane(std::uint32_t channel) const noexcept;
    [[nodiscard]] std::span<std::byte> mutablePlane(std::uint32_t channel) noexcept;

    /// One sample as a double, which holds every value of all eight representations exactly.
    [[nodiscard]] double value(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const;

    /// Stores a value already representable in this image's type - an integer inside its range, or any
    /// double for Float64 and any float value for Float32. The caller quantizes; `quantizeSample` is the rule.
    void setValue(std::uint32_t x, std::uint32_t y, std::uint32_t channel, double value);

    /// The SHA-256 of `bytes()`, as lowercase hexadecimal.
    [[nodiscard]] std::string digest() const;

private:
    std::uint32_t m_width = 0u;
    std::uint32_t m_height = 0u;
    std::uint32_t m_channelCount = 0u;
    FixtureSampleType m_sampleType = FixtureSampleType::Float32;
    std::vector<std::byte> m_bytes;
};

[[nodiscard]] std::uint32_t channelCountOf(FixtureChannels channels) noexcept;
[[nodiscard]] std::size_t sampleSizeOf(FixtureSampleType sampleType) noexcept;
[[nodiscard]] bool isFloating(FixtureSampleType sampleType) noexcept;

/// The smallest and largest value an integer representation holds; the float limits for a floating one.
[[nodiscard]] double lowestOf(FixtureSampleType sampleType) noexcept;
[[nodiscard]] double highestOf(FixtureSampleType sampleType) noexcept;

/// The names the canonical text and the expectation records use: `uint8`, `int8`, ... `float32`, `float64`.
[[nodiscard]] std::string_view sampleTypeName(FixtureSampleType sampleType) noexcept;
[[nodiscard]] std::optional<FixtureSampleType> sampleTypeFromName(std::string_view name) noexcept;
[[nodiscard]] std::string_view channelsName(FixtureChannels channels) noexcept;
[[nodiscard]] std::string_view domainName(FixtureDomain domain) noexcept;
[[nodiscard]] std::string_view patternName(FixturePattern pattern) noexcept;

/// The pattern's name as a FITS BAYERPAT card writes it - `RGGB` and so on - or empty for `FixtureCfa::None`.
[[nodiscard]] std::string_view cfaName(FixtureCfa cfa) noexcept;
[[nodiscard]] std::optional<FixtureCfa> cfaFromName(std::string_view name) noexcept;

/// One value as a representation stores it: nearest with ties to even, saturated to the range, for an integer
/// type; the IEEE conversion for Float32, a finite value beyond its range becoming the infinity of its sign; unchanged
/// for Float64. An integer type maps NaN to zero and an infinity to the end of its range, though `checkFixture`
/// refuses both before a description reaches here.
[[nodiscard]] double quantizeSample(double value, FixtureSampleType sampleType) noexcept;

/// Why a description cannot describe an image the engine holds unchanged, or nothing when it can.
///
/// Refused: a name that is not letters, digits, dots, hyphens and underscores; an empty extent; a level count other
/// than the channel count; a non-finite pattern parameter; a checker cell of zero; a physical domain on an integer
/// type; a filter array on anything but a mono image; an override outside the image or non-finite on an integer
/// type; a card keyword that is not one to eight upper-case letters, digits, hyphens and underscores, or is one the
/// file layout writes itself or a commentary keyword - SIMPLE, BITPIX, NAXIS and NAXISn, EXTEND, BZERO, BSCALE,
/// BLANK, BLOCKED, ROWORDER, BAYERPAT, COMMENT, HISTORY, CONTINUE and END; a value longer than 32 printable ASCII
/// characters or a comment longer than 24, which is what every recorded header carries unchanged; a text value
/// that is empty or starts or ends with a space; and an integer, real or logical value that is not one.
[[nodiscard]] std::optional<std::string> checkFixture(const FixtureImage& image);

/// Every sample of a description. Empty, with the reason in `error`, when `checkFixture` refuses it.
[[nodiscard]] std::optional<FixtureSamples> materializeFixture(const FixtureImage& image, std::string& error);

/// The canonical text of a description: one line per field, every field included, doubles written so they
/// read back exactly. Equal descriptions give equal text, and changing any field changes it.
[[nodiscard]] std::string describeFixture(const FixtureImage& image);

/// The SHA-256 of `describeFixture`, as lowercase hexadecimal: a description's identity in a provenance record.
[[nodiscard]] std::string fixtureDigest(const FixtureImage& image);

/// Whether two sample sets are the same: the same shape and representation, integer samples equal, and float
/// samples bit-identical except that any NaN matches any NaN. A signed zero is not the other zero.
[[nodiscard]] bool sameSamples(const FixtureSamples& left, const FixtureSamples& right);

/// A shortest round-trip text for a double: `std::to_chars` output, `nan` for any NaN, `inf` and `-inf`.
[[nodiscard]] std::string formatDouble(double value);

/// The double `formatDouble` wrote; empty for text that is not one.
[[nodiscard]] std::optional<double> parseDouble(std::string_view text) noexcept;

} // namespace an::tests::fixtures
