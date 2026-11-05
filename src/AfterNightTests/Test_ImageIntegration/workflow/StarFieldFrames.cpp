// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The star-field lights the registration and alignment cases write and read back.

#include "Test_ImageIntegration/workflow/StarFieldFrames.h"

#include "fixtures/common/FixtureFits.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <sstream>

namespace an::tests::integration
{
namespace
{

using an::tests::fixtures::FixtureCard;
using an::tests::fixtures::FixtureCardKind;
using an::tests::fixtures::FixtureStar;

/// A small deterministic generator, so a field is the same on every host.
class FieldRandom
{
public:
    explicit FieldRandom(std::uint64_t seed) noexcept : m_state(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}

    /// A value in [0, 1).
    [[nodiscard]] double next() noexcept
    {
        m_state ^= m_state >> 12u;
        m_state ^= m_state << 25u;
        m_state ^= m_state >> 27u;
        const std::uint64_t mixed = m_state * 0x2545F4914F6CDD1Dull;
        return static_cast<double>(mixed >> 11u) / static_cast<double>(1ull << 53u);
    }

private:
    std::uint64_t m_state;
};

[[nodiscard]] std::string formatReal(double value)
{
    std::ostringstream stream;
    stream.precision(17);
    stream << value;
    return stream.str();
}

[[nodiscard]] FixtureCard realCard(const std::string& keyword, double value)
{
    return FixtureCard{keyword, formatReal(value), {}, FixtureCardKind::Real};
}

[[nodiscard]] FixtureCard textCard(const std::string& keyword, const std::string& value)
{
    return FixtureCard{keyword, value, {}, FixtureCardKind::Text};
}

} // namespace

std::vector<FixtureStar> starCatalogue(std::size_t count, std::uint64_t seed)
{
    constexpr double kMargin = 18.0;
    constexpr double kSeparation = 9.0;
    FieldRandom random(seed);
    std::vector<FixtureStar> stars;
    stars.reserve(count);
    std::size_t attempts = 0u;
    while (stars.size() < count && attempts < count * 200u)
    {
        ++attempts;
        const double x = kMargin + random.next() * (static_cast<double>(kStarFieldWidth) - 2.0 * kMargin);
        const double y = kMargin + random.next() * (static_cast<double>(kStarFieldHeight) - 2.0 * kMargin);
        const double peak = 0.25 + 0.6 * random.next();
        bool crowded = false;
        for (const FixtureStar& star : stars)
        {
            if (std::hypot(star.x - x, star.y - y) < kSeparation)
            {
                crowded = true;
                break;
            }
        }
        if (!crowded)
        {
            stars.push_back(FixtureStar{x, y, 1.6, peak});
        }
    }
    return stars;
}

std::vector<FixtureStar> movedStars(const std::vector<FixtureStar>& stars,
                                    const std::function<std::pair<double, double>(double, double)>& motion)
{
    std::vector<FixtureStar> moved = stars;
    for (FixtureStar& star : moved)
    {
        const auto [x, y] = motion(star.x, star.y);
        star.x = x;
        star.y = y;
    }
    return moved;
}

std::vector<FixtureCard> tanSolutionCards(double crpix1, double crpix2, double distortionA20)
{
    const bool distorted = distortionA20 != 0.0;
    std::vector<FixtureCard> cards{
        textCard("CTYPE1", distorted ? "RA---TAN-SIP" : "RA---TAN"),
        textCard("CTYPE2", distorted ? "DEC--TAN-SIP" : "DEC--TAN"),
        realCard("CRPIX1", crpix1),
        realCard("CRPIX2", crpix2),
        realCard("CRVAL1", 83.8),
        realCard("CRVAL2", -5.4),
        realCard("CD1_1", -1.0e-4),
        realCard("CD1_2", 0.0),
        realCard("CD2_1", 0.0),
        realCard("CD2_2", 1.0e-4),
        textCard("RADESYS", "ICRS"),
    };
    if (distorted)
    {
        cards.push_back(FixtureCard{"A_ORDER", "2", {}, FixtureCardKind::Integer});
        cards.push_back(realCard("A_2_0", distortionA20));
        cards.push_back(realCard("A_1_1", 0.0));
        cards.push_back(realCard("A_0_2", 0.0));
        cards.push_back(FixtureCard{"B_ORDER", "2", {}, FixtureCardKind::Integer});
        cards.push_back(realCard("B_2_0", 0.0));
        cards.push_back(realCard("B_1_1", 0.0));
        cards.push_back(realCard("B_0_2", distortionA20));
    }
    return cards;
}

FixtureCard exposureStartCard(const std::string& when)
{
    return textCard("DATE-OBS", when);
}

namespace
{

/// Writes a Float32 light holding the stars over a faint noisy background, read through the filter array stated.
bool writeStarFieldThrough(const an::tests::fixtures::FixtureCfa cfa, const QString& path,
                           const std::vector<FixtureStar>& stars, const std::vector<FixtureCard>& cards,
                           std::uint64_t seed, std::string& error, const double background,
                           const std::vector<StarFieldPixel>& notANumber)
{
    an::tests::fixtures::FixtureImage image;
    image.name = std::filesystem::path(path.toStdWString()).stem().string();
    image.width = kStarFieldWidth;
    image.height = kStarFieldHeight;
    image.sampleType = an::tests::fixtures::FixtureSampleType::Float32;
    image.cfa = cfa;
    image.pattern =
        stars.empty() ? an::tests::fixtures::FixturePattern::Noise : an::tests::fixtures::FixturePattern::StarField;
    image.seed = seed;
    an::tests::fixtures::FixtureChannelLevels levels;
    levels.base = background;
    levels.amplitude = 0.002;
    levels.alternate = stars.empty() ? 0.0 : 1.0;
    image.levels = {levels};
    image.stars = stars;
    image.cards = cards;
    std::optional<an::tests::fixtures::FixtureSamples> samples = an::tests::fixtures::materializeFixture(image, error);
    if (!samples.has_value())
    {
        return false;
    }
    for (const auto& [x, y] : notANumber)
    {
        samples->setValue(x, y, 0u, std::numeric_limits<double>::quiet_NaN());
    }
    return an::tests::fixtures::writeFixtureFits(image, *samples, std::filesystem::path(path.toStdWString()), error);
}

} // namespace

bool writeStarField(const QString& path, const std::vector<FixtureStar>& stars, const std::vector<FixtureCard>& cards,
                    std::uint64_t seed, std::string& error, const double background,
                    const std::vector<StarFieldPixel>& notANumber)
{
    return writeStarFieldThrough(an::tests::fixtures::FixtureCfa::None, path, stars, cards, seed, error, background,
                                 notANumber);
}

bool writeMosaicStarField(const QString& path, const std::vector<FixtureStar>& stars,
                          const std::vector<FixtureCard>& cards, std::uint64_t seed, std::string& error)
{
    return writeStarFieldThrough(an::tests::fixtures::FixtureCfa::Rggb, path, stars, cards, seed, error, 0.05, {});
}

std::optional<BrightestSample> brightestNear(const QString& path, double x, double y, int radius)
{
    std::string error;
    const std::optional<an::tests::fixtures::FixtureFitsFile> file =
        an::tests::fixtures::readFixtureFits(std::filesystem::path(path.toStdWString()), error);
    if (!file.has_value())
    {
        return std::nullopt;
    }
    const an::tests::fixtures::FixtureSamples& samples = file->samples;
    std::optional<BrightestSample> best;
    const int centre_x = static_cast<int>(std::lround(x));
    const int centre_y = static_cast<int>(std::lround(y));
    for (int row = centre_y - radius; row <= centre_y + radius; ++row)
    {
        for (int column = centre_x - radius; column <= centre_x + radius; ++column)
        {
            if (row < 0 || column < 0 || row >= static_cast<int>(samples.height()) ||
                column >= static_cast<int>(samples.width()))
            {
                continue;
            }
            const double value = samples.value(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row), 0u);
            if (std::isfinite(value) && (!best.has_value() || value > best->value))
            {
                best = BrightestSample{static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row), value};
            }
        }
    }
    return best;
}

} // namespace an::tests::integration
