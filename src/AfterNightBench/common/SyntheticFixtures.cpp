// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Deterministic raw fixtures, including unit-domain stretch boundary neighbors.
#include "SyntheticFixtures.h"

#include "Digest.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace an::bench
{
namespace
{

/// SplitMix64: a small, fully specified integer mixer.
///
/// Chosen over the standard library's engines and distributions because the mapping from engine
/// output to a value must be identical everywhere. `std::mt19937` is specified exactly, but
/// `std::uniform_real_distribution` is not, so two standard libraries can turn the same bits into
/// different fixtures - and a fixture that differs across platforms turns a correct engine into an
/// apparent correctness failure.
class Mixer
{
public:
    explicit Mixer(std::uint64_t seed) : m_state(seed) {}

    std::uint64_t next()
    {
        m_state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = m_state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    /// A value in [0, 1), from the top 53 bits so every double it can produce is exactly
    /// representable and the mapping is the same on every platform.
    double nextUnit()
    {
        return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0);
    }

    /// A fixed-order, normal-like deviate without platform libm calls.
    double nextNormalLike()
    {
        // The sum of twelve uniform variables is a deterministic approximation to a standard
        // normal. Integer accumulation keeps the byte stream independent of log/cos/sqrt library
        // implementations; conversion happens once, in a fixed expression order.
        std::uint64_t sum = 0;
        for (int index = 0; index < 12; ++index)
        {
            sum += next() >> 48;
        }
        constexpr double kMean = 6.0 * 65535.0;
        constexpr double kScale = 1.0 / 65535.0;
        return (static_cast<double>(sum) - kMean) * kScale;
    }

private:
    std::uint64_t m_state;
};

constexpr std::array<const char*, 15> kGeneratorIds{
    "synthetic.zero",           "synthetic.constant",       "synthetic.ramp",
    "synthetic.gradient",       "synthetic.impulse",        "synthetic.checkerboard",
    "synthetic.extremes",       "synthetic.uniform_noise",  "synthetic.gaussian_noise",
    "synthetic.stars",          "synthetic.nonfinite",      "synthetic.asinh_boundaries",
    "synthetic.ghs_boundaries", "synthetic.mtf_boundaries", "synthetic.statistical_sky",
};

bool isFloatingType(SampleType type)
{
    return type == SampleType::Float32 || type == SampleType::Float64;
}

/// Fold a string into the seed, so two generators with the same seed do not produce the same bytes.
std::uint64_t mixText(std::uint64_t seed, const std::string& text)
{
    std::uint64_t value = seed;
    for (const char character : text)
    {
        value = (value ^ static_cast<unsigned char>(character)) * 0x100000001B3ull;
    }
    return value;
}

/// The seed every generator starts from: the caller's seed, the generator version, the id, and the
/// descriptor's shape. Including the shape means a differently sized request is not a prefix of a
/// larger one, which would make two scenarios share pixel values by accident.
std::uint64_t seedFor(std::uint64_t seed, std::uint32_t version, const std::string& sourceId,
                      const SampleDescriptor& descriptor)
{
    std::uint64_t value = mixText(seed ^ (std::uint64_t{version} << 32), sourceId);
    for (const std::uint64_t part :
         {std::uint64_t{descriptor.width}, std::uint64_t{descriptor.height}, std::uint64_t{descriptor.channels},
          static_cast<std::uint64_t>(descriptor.sampleType), static_cast<std::uint64_t>(descriptor.layout)})
    {
        value = (value ^ part) * 0x100000001B3ull;
    }
    return value == 0 ? 0x9E3779B97F4A7C15ull : value;
}

/// Map a normalized value in [0, 1] onto `type`'s full representable range.
///
/// Floating types take the value unchanged, including values outside [0, 1]: a Float32 processing
/// model has to carry legitimate negative and above-one intermediates, and a fixture that clamped
/// them would hide exactly the case worth testing.
void writeSample(std::uint8_t* destination, SampleType type, ByteOrder order, double value)
{
    std::uint64_t bits = 0;
    std::uint32_t size = 0;

    auto scaleUnsigned = [&](double maximum) { return std::llround((std::clamp)(value, 0.0, 1.0) * maximum); };

    switch (type)
    {
    case SampleType::UInt8:
        bits = static_cast<std::uint64_t>(static_cast<std::uint8_t>(scaleUnsigned(255.0)));
        size = 1;
        break;
    case SampleType::Int8:
        bits =
            static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(scaleUnsigned(255.0) - 128)));
        size = 1;
        break;
    case SampleType::UInt16:
        bits = static_cast<std::uint64_t>(static_cast<std::uint16_t>(scaleUnsigned(65535.0)));
        size = 2;
        break;
    case SampleType::Int16:
        bits = static_cast<std::uint64_t>(
            static_cast<std::uint16_t>(static_cast<std::int16_t>(scaleUnsigned(65535.0) - 32768)));
        size = 2;
        break;
    case SampleType::UInt32:
        bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(scaleUnsigned(4294967295.0)));
        size = 4;
        break;
    case SampleType::Int32:
        bits = static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(scaleUnsigned(4294967295.0) - 2147483648LL)));
        size = 4;
        break;
    case SampleType::UInt64:
        // Scaled through a double, so only the top 53 bits of the range are distinguishable. That
        // is a property of the fixture, not of the engine reading it, and it is stated here so a
        // scenario testing 64-bit precision does not use this generator to do it.
        bits = static_cast<std::uint64_t>((std::clamp)(value, 0.0, 1.0) * 18446744073709549568.0);
        size = 8;
        break;
    case SampleType::Int64: {
        const double scaled = ((std::clamp)(value, 0.0, 1.0) - 0.5) * 18446744073709549568.0;
        bits = static_cast<std::uint64_t>(static_cast<std::int64_t>(scaled));
        size = 8;
        break;
    }
    case SampleType::Float32: {
        const float single = static_cast<float>(value);
        std::uint32_t single_bits = 0;
        std::memcpy(&single_bits, &single, sizeof single_bits);
        bits = single_bits;
        size = 4;
        break;
    }
    case SampleType::Float64:
        std::memcpy(&bits, &value, sizeof bits);
        size = 8;
        break;
    }

    const bool bigEndian =
        order == ByteOrder::Big || (order == ByteOrder::Native && std::endian::native == std::endian::big);
    for (std::uint32_t index = 0; index < size; ++index)
    {
        const std::uint32_t shift = bigEndian ? (size - 1 - index) * 8 : index * 8;
        destination[index] = static_cast<std::uint8_t>((bits >> shift) & 0xFFu);
    }
}

/// A unit ramp whose first samples are each landmark between its two Float32 neighbours.
double landmarkRamp(std::span<const float> landmarks, std::uint32_t x, std::uint32_t width)
{
    if (x < landmarks.size() * 3u)
    {
        const auto center = landmarks[x / 3u];
        if (x % 3u == 0u)
            return std::nextafter(center, 0.0f);
        if (x % 3u == 2u)
            return std::nextafter(center, 1.0f);
        return center;
    }
    return static_cast<double>(x) / (width - 1u);
}

/// One generator's value for the sample at (x, y) of plane `channel`.
///
/// `mixer` is advanced only by the generators that need randomness, and always in the same order,
/// so a fixture's bytes depend on the descriptor and seed and nothing else.
double sampleValue(const std::string& sourceId, const SampleDescriptor& descriptor, std::uint32_t x, std::uint32_t y,
                   std::uint32_t channel, std::uint64_t fixtureSeed, Mixer& mixer)
{
    const double width = static_cast<double>(descriptor.width);
    const double height = static_cast<double>(descriptor.height);
    const double horizontal = descriptor.width > 1 ? static_cast<double>(x) / (width - 1.0) : 0.0;
    const double vertical = descriptor.height > 1 ? static_cast<double>(y) / (height - 1.0) : 0.0;

    if (sourceId == "synthetic.zero")
    {
        return 0.0;
    }
    if (sourceId == "synthetic.constant")
    {
        return 0.5;
    }
    if (sourceId == "synthetic.asinh_boundaries")
    {
        // Float32 landmarks of the protected curves and their normalized images,
        // fixed as literals so fixture bytes do not depend on the platform libm.
        constexpr std::array<float, 16> landmarks{
            0.00000000000000000e+00f, 1.00000000000000000e+00f, 1.25000000000000000e-01f, 3.75000000000000000e-01f,
            7.50000000000000000e-01f, 5.31709417700767517e-02f, 4.69558060169219971e-01f, 9.29073929786682129e-01f,
            1.22026629745960236e-01f, 4.03968036174774170e-01f, 7.97324776649475098e-01f, 8.35771977901458740e-01f,
            7.31025189161300659e-02f, 2.81481584534049034e-03f, 4.98389542102813721e-01f, 9.96246933937072754e-01f};
        return landmarkRamp(landmarks, x, descriptor.width);
    }
    if (sourceId == "synthetic.ghs_boundaries")
    {
        // The protection and symmetry points of every generalized-hyperbolic boundary profile and
        // their normalized images, evaluated from the family's definition in 90-digit decimal
        // arithmetic and fixed as literals, so fixture bytes do not depend on the platform libm.
        constexpr std::array<float, 65> landmarks{
            0.00000000000000000e+00f, 2.93873587705571877e-39f, 5.87747175411143754e-39f, 8.67996508002732980e-11f,
            8.68246100016456523e-11f, 8.68266916698168245e-11f, 2.83683207817375660e-03f, 2.83695640973746777e-03f,
            9.38196759670972824e-03f, 1.76622252911329269e-02f, 2.84531190991401672e-02f, 2.84531489014625549e-02f,
            2.84532010555267334e-02f, 3.79146933555603027e-02f, 5.81818185746669769e-02f, 5.98054751753807068e-02f,
            5.98055385053157806e-02f, 7.43535161018371582e-02f, 7.43535459041595459e-02f, 9.82324928045272827e-02f,
            1.09101124107837677e-01f, 1.09101139008998871e-01f, 1.09101146459579468e-01f, 1.11997134983539581e-01f,
            1.13944515585899353e-01f, 1.13944523036479950e-01f, 1.24999992549419403e-01f, 1.25000000000000000e-01f,
            3.75000000000000000e-01f, 3.75000029802322388e-01f, 3.75000059604644775e-01f, 4.11515235900878906e-01f,
            4.19989258050918579e-01f, 4.34077024459838867e-01f, 4.34077054262161255e-01f, 4.34077084064483643e-01f,
            4.42046225070953369e-01f, 4.65098142623901367e-01f, 4.65098202228546143e-01f, 4.94542270898818970e-01f,
            4.97244268655776978e-01f, 4.98376876115798950e-01f, 4.98376965522766113e-01f, 5.00000000000000000e-01f,
            7.50000000000000000e-01f, 7.50000059604644775e-01f, 8.12326669692993164e-01f, 8.26538860797882080e-01f,
            8.36266994476318359e-01f, 8.36267054080963135e-01f, 8.50031852722167969e-01f, 8.50031912326812744e-01f,
            8.64406764507293701e-01f, 8.66757333278656006e-01f, 9.06976759433746338e-01f, 9.18811142444610596e-01f,
            9.18811202049255371e-01f, 9.18811261653900146e-01f, 9.19223666191101074e-01f, 9.19223785400390625e-01f,
            9.83495235443115234e-01f, 9.91442441940307617e-01f, 9.96217370033264160e-01f, 9.96217548847198486e-01f,
            1.00000000000000000e+00f};
        return landmarkRamp(landmarks, x, descriptor.width);
    }
    if (sourceId == "synthetic.mtf_boundaries")
    {
        // The shadow and highlight points of every midtones transfer boundary profile and the
        // inputs each curve takes to one quarter, one half and three quarters, evaluated in exact
        // rational arithmetic and rounded to Float32, and the capture profiles' midtones balances.
        constexpr std::array<float, 31> landmarks{
            0.00000000000000000e+00f, 3.33333559865423013e-07f, 9.99999997475242708e-07f, 2.99999396702332888e-06f,
            1.99999995529651642e-02f, 2.99999993294477463e-02f, 4.93999980390071869e-02f, 7.50000029802322388e-02f,
            1.03207543492317200e-01f, 1.25000000000000000e-01f, 1.25000208616256714e-01f, 1.25000625848770142e-01f,
            1.25001877546310425e-01f, 1.87500000000000000e-01f, 2.50000000000000000e-01f, 2.81250000000000000e-01f,
            3.75000000000000000e-01f, 4.37500000000000000e-01f, 5.00000000000000000e-01f, 5.00000119209289551e-01f,
            5.00000238418579102e-01f, 5.00000476837158203e-01f, 5.00000953674316406e-01f, 7.49998092651367188e-01f,
            7.49999344348907471e-01f, 7.49999761581420898e-01f, 7.50000000000000000e-01f, 9.99996960163116455e-01f,
            9.99998986721038818e-01f, 9.99999642372131348e-01f, 1.00000000000000000e+00f};
        return landmarkRamp(landmarks, x, descriptor.width);
    }
    if (sourceId == "synthetic.statistical_sky")
    {
        // A registered frame's black margin, a non-quantized background with a normal-like spread,
        // and deterministically placed cold pixels and stars. The margin and the cold pixels lie
        // far below any black point a statistical stretch derives from that background.
        constexpr std::uint32_t kMargin = 16u;
        if (x < kMargin || y < kMargin || x + kMargin >= descriptor.width || y + kMargin >= descriptor.height)
            return 0.0;
        const std::uint64_t index = std::uint64_t{y} * descriptor.width + x;
        if (index % 7919u == 5u)
            return 0.01;
        if (index % 4099u == 17u)
            return 0.3 + 0.7 * mixer.nextUnit();
        return 0.05 + 0.002 * mixer.nextNormalLike();
    }
    if (sourceId == "synthetic.ramp")
    {
        return horizontal;
    }
    if (sourceId == "synthetic.gradient")
    {
        return (horizontal + vertical) * 0.5;
    }
    if (sourceId == "synthetic.impulse")
    {
        return (x == descriptor.width / 2 && y == descriptor.height / 2) ? 1.0 : 0.0;
    }
    if (sourceId == "synthetic.checkerboard")
    {
        return ((x / 8 + y / 8 + channel) % 2 == 0) ? 0.0 : 1.0;
    }
    if (sourceId == "synthetic.extremes")
    {
        // Alternating representable extremes, which is what catches a saturating conversion that
        // is right in the middle of the range and wrong at its ends.
        return ((x + y + channel) % 2 == 0) ? 0.0 : 1.0;
    }
    if (sourceId == "synthetic.uniform_noise")
    {
        return mixer.nextUnit();
    }
    if (sourceId == "synthetic.gaussian_noise")
    {
        return (std::clamp)(0.5 + mixer.nextNormalLike() * 0.15, 0.0, 1.0);
    }
    if (sourceId == "synthetic.stars")
    {
        // One extended, separable PSF per 32x32 tile. Positions, amplitude, and background come
        // from coordinate hashes, so generation order and layout do not affect the sky model.
        constexpr std::uint32_t kTile = 32;
        constexpr std::array<std::uint32_t, 7> kKernel{1, 4, 10, 16, 10, 4, 1};
        const std::uint32_t tileX = x / kTile;
        const std::uint32_t tileY = y / kTile;
        std::uint64_t starBits =
            fixtureSeed ^ (std::uint64_t{tileX} << 1) ^ (std::uint64_t{tileY} << 22) ^ (std::uint64_t{channel} << 43);
        starBits = (starBits ^ (starBits >> 30)) * 0xBF58476D1CE4E5B9ull;
        starBits = (starBits ^ (starBits >> 27)) * 0x94D049BB133111EBull;
        starBits ^= starBits >> 31;
        const std::uint32_t centerX = tileX * kTile + 4u + static_cast<std::uint32_t>(starBits % 24u);
        const std::uint32_t centerY = tileY * kTile + 4u + static_cast<std::uint32_t>((starBits >> 8) % 24u);
        const std::int32_t dx = static_cast<std::int32_t>(x) - static_cast<std::int32_t>(centerX);
        const std::int32_t dy = static_cast<std::int32_t>(y) - static_cast<std::int32_t>(centerY);

        std::uint64_t backgroundBits = fixtureSeed ^ (std::uint64_t{x} << 3) ^ (std::uint64_t{y} << 25) ^
                                       (std::uint64_t{channel} << 47) ^ 0xD1B54A32D192ED03ull;
        backgroundBits = (backgroundBits ^ (backgroundBits >> 30)) * 0xBF58476D1CE4E5B9ull;
        backgroundBits = (backgroundBits ^ (backgroundBits >> 27)) * 0x94D049BB133111EBull;
        backgroundBits ^= backgroundBits >> 31;
        const double background = 0.02 + static_cast<double>((backgroundBits >> 48) & 0xFFFFu) / 6553500.0;

        if (dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3)
        {
            const std::uint32_t weight =
                kKernel[static_cast<std::size_t>(dx + 3)] * kKernel[static_cast<std::size_t>(dy + 3)];
            const double amplitude = 0.65 + static_cast<double>((starBits >> 32) & 0xFFu) / 1020.0;
            return (std::min)(1.0, background + amplitude * static_cast<double>(weight) / 256.0);
        }
        return background;
    }
    if (sourceId == "synthetic.nonfinite")
    {
        // Deterministic placement rather than random: a test asserting how an operation treats a
        // NaN needs to know where the NaN is.
        const std::uint64_t index = std::uint64_t{y} * descriptor.width + x;
        switch (index % 7)
        {
        case 0:
            return std::numeric_limits<double>::quiet_NaN();
        case 1:
            return std::numeric_limits<double>::infinity();
        case 2:
            return -std::numeric_limits<double>::infinity();
        case 3:
            return -0.25; // legitimately below zero for a floating processing model
        case 4:
            return 1.75; // and legitimately above one
        default:
            return horizontal;
        }
    }
    return 0.0;
}

} // namespace

SyntheticFixtureSource::SyntheticFixtureSource(std::uint32_t generatorVersion) : m_generatorVersion(generatorVersion) {}

std::vector<std::string> SyntheticFixtureSource::generatorIds()
{
    return std::vector<std::string>(kGeneratorIds.begin(), kGeneratorIds.end());
}

bool SyntheticFixtureSource::knows(const std::string& sourceId)
{
    for (const char* id : kGeneratorIds)
    {
        if (sourceId == id)
        {
            return true;
        }
    }
    return false;
}

bool SyntheticFixtureSource::acquire(const std::string& sourceId, std::uint64_t seed,
                                     const SampleDescriptor& descriptor, Fixture& out, std::string& error)
{
    if (!knows(sourceId))
    {
        error = "'" + sourceId + "' is not a synthetic generator";
        return false;
    }
    if (descriptor.layout != Layout::Planar && descriptor.layout != Layout::Interleaved)
    {
        error = "the synthetic generators produce planar and interleaved data only, not " +
                std::string(toString(descriptor.layout));
        return false;
    }
    if (!descriptor.isValid())
    {
        error = "'" + sourceId + "' was asked for an invalid descriptor";
        return false;
    }
    if (sourceId == "synthetic.nonfinite" && !isFloatingType(descriptor.sampleType))
    {
        error = "'synthetic.nonfinite' has no meaning in " + std::string(toString(descriptor.sampleType)) +
                "; an integer type cannot carry NaN or infinity";
        return false;
    }

    const std::uint64_t total = descriptor.totalBytes();
    out.descriptor = descriptor;
    out.sourceId = sourceId;
    out.generatorVersion = m_generatorVersion;
    // Zeroed first, so the padding a wider stride leaves between rows is defined rather than
    // whatever the allocator last held. A digest over undefined padding is not reproducible.
    out.bytes.assign(static_cast<std::size_t>(total), std::uint8_t{0});

    const std::uint64_t fixtureSeed = seedFor(seed, m_generatorVersion, sourceId, descriptor);
    Mixer mixer(fixtureSeed);

    const std::uint32_t sampleSize = descriptor.sampleSizeBytes();
    const std::uint64_t rowBytes = descriptor.rowBytes();
    const bool planar = descriptor.layout == Layout::Planar;
    const std::uint64_t planeBytes =
        descriptor.planeStrideBytes != 0 ? descriptor.planeStrideBytes : rowBytes * descriptor.height;
    const std::uint32_t planes = planar ? descriptor.channels : 1u;
    const std::uint32_t channelsPerSample = planar ? 1u : descriptor.channels;

    for (std::uint32_t plane = 0; plane < planes; ++plane)
    {
        for (std::uint32_t y = 0; y < descriptor.height; ++y)
        {
            std::uint8_t* row = out.bytes.data() + static_cast<std::size_t>(plane * planeBytes + y * rowBytes);
            for (std::uint32_t x = 0; x < descriptor.width; ++x)
            {
                for (std::uint32_t component = 0; component < channelsPerSample; ++component)
                {
                    const std::uint32_t channel = planar ? plane : component;
                    const double value = sampleValue(sourceId, descriptor, x, y, channel, fixtureSeed, mixer);
                    writeSample(row + static_cast<std::size_t>(x * channelsPerSample + component) * sampleSize,
                                descriptor.sampleType, descriptor.byteOrder, value);
                }
            }
        }
    }

    out.digest = Sha256::of(out.bytes.data(), out.bytes.size());
    return true;
}

} // namespace an::bench
