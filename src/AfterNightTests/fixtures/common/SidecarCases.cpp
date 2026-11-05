// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "SidecarCases.h"

#include <utility>

namespace an::tests::fixtures
{
namespace
{

[[nodiscard]] FixtureCard text(std::string keyword, std::string value, std::string comment = {})
{
    return FixtureCard{std::move(keyword), std::move(value), std::move(comment), FixtureCardKind::Text};
}

[[nodiscard]] FixtureCard real(std::string keyword, std::string value, std::string comment = {})
{
    return FixtureCard{std::move(keyword), std::move(value), std::move(comment), FixtureCardKind::Real};
}

[[nodiscard]] FixtureCard integer(std::string keyword, std::string value, std::string comment = {})
{
    return FixtureCard{std::move(keyword), std::move(value), std::move(comment), FixtureCardKind::Integer};
}

/// A frame of a shape and representation whose samples rise across it, with the cards it states.
[[nodiscard]] FixtureImage frame(std::string name, const std::uint32_t width, const std::uint32_t height,
                                 const FixtureChannels channels, const FixtureSampleType sampleType,
                                 std::vector<FixtureCard> cards, const FixtureCfa cfa = FixtureCfa::None)
{
    FixtureImage image;
    image.name = std::move(name);
    image.width = width;
    image.height = height;
    image.channels = channels;
    image.sampleType = sampleType;
    image.cfa = cfa;
    image.pattern = FixturePattern::Gradient;
    const bool integral = !isFloating(sampleType);
    for (std::uint32_t channel = 0u; channel < channelCountOf(channels); ++channel)
    {
        image.levels.push_back(
            integral ? FixtureChannelLevels{.base = 1000.0 + 500.0 * channel, .perColumn = 97.0, .perRow = 31.0}
                     : FixtureChannelLevels{.base = 0.1 + 0.1 * channel, .perColumn = 0.05, .perRow = 0.02});
    }
    image.cards = std::move(cards);
    return image;
}

/// A plate-solved frame's cards: a target, an exposure, a tangent-plane solution with a second-order distortion and
/// the solver that wrote it.
[[nodiscard]] std::vector<FixtureCard> solvedCards()
{
    return {text("OBJECT", "M42", "target"),
            real("EXPTIME", "120.0", "seconds"),
            integer("GAIN", "100"),
            text("FILTER", "L"),
            text("CTYPE1", "RA---TAN-SIP"),
            text("CTYPE2", "DEC--TAN-SIP"),
            real("EQUINOX", "2000.0"),
            text("RADESYS", "ICRS"),
            real("CRVAL1", "210.123456"),
            real("CRVAL2", "-12.25"),
            real("CRPIX1", "4.5"),
            real("CRPIX2", "3.5"),
            real("CD1_1", "-2.777777777E-04"),
            real("CD1_2", "1.0E-06"),
            real("CD2_1", "-1.0E-06"),
            real("CD2_2", "2.777777777E-04"),
            integer("A_ORDER", "2"),
            real("A_0_2", "1.5E-07"),
            real("A_1_1", "-2.0E-07"),
            real("A_2_0", "3.0E-07"),
            integer("B_ORDER", "2"),
            real("B_0_2", "-1.0E-07"),
            real("B_1_1", "2.5E-07"),
            real("B_2_0", "1.0E-07"),
            text("PLTSOLVR", "ASTAP")};
}

} // namespace

const std::vector<SidecarCase>& sidecarCases()
{
    static const std::vector<SidecarCase> cases = [] {
        const FixtureImage solved =
            frame("sidecar-solved-grey", 8u, 6u, FixtureChannels::Mono, FixtureSampleType::Float32, solvedCards());
        return std::vector<SidecarCase>{
            SidecarCase{.call = "sidecar.solved-grey", .image = solved},
            SidecarCase{.call = "sidecar.colour",
                        .image = frame("sidecar-colour", 4u, 3u, FixtureChannels::Rgb, FixtureSampleType::Float32,
                                       {text("OBJECT", "NGC 7000"), real("EXPTIME", "300.0")})},
            SidecarCase{.call = "sidecar.mosaic",
                        .image = frame("sidecar-mosaic", 6u, 4u, FixtureChannels::Mono, FixtureSampleType::UInt16,
                                       {text("OBJECT", "M31"), integer("GAIN", "120")}, FixtureCfa::Rggb)},
            SidecarCase{.call = "sidecar.solved-grey-region",
                        .image = solved,
                        .region = SidecarRegion{.x = 2u, .y = 1u, .width = 4u, .height = 3u}}};
    }();
    return cases;
}

} // namespace an::tests::fixtures
