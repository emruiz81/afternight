// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Acquisition frames as capture programs write them.

#include "IntegrationFrameFiles.h"

#include "FixtureFits.h"

#include <system_error>
#include <utility>

namespace an::tests::fixtures
{

namespace
{

[[nodiscard]] FixtureCard textCard(std::string keyword, std::string value, std::string comment)
{
    return FixtureCard{std::move(keyword), std::move(value), std::move(comment), FixtureCardKind::Text};
}

[[nodiscard]] FixtureCard realCard(std::string keyword, const double value, std::string comment)
{
    return FixtureCard{std::move(keyword), formatDouble(value), std::move(comment), FixtureCardKind::Real};
}

[[nodiscard]] FixtureCard integerCard(std::string keyword, const std::int64_t value, std::string comment)
{
    return FixtureCard{std::move(keyword), std::to_string(value), std::move(comment), FixtureCardKind::Integer};
}

} // namespace

std::vector<FixtureCard> acquisitionCards(const AcquisitionFacts& facts)
{
    std::vector<FixtureCard> cards;
    if (!facts.kindKeyword.empty() && !facts.kind.empty())
    {
        cards.push_back(textCard(facts.kindKeyword, facts.kind, "frame kind"));
    }
    if (facts.exposureSeconds.has_value())
    {
        cards.push_back(realCard(facts.exposureKeyword, *facts.exposureSeconds, "exposure in seconds"));
    }
    if (facts.sensorTemperatureCelsius.has_value())
    {
        cards.push_back(realCard("CCD-TEMP", *facts.sensorTemperatureCelsius, "sensor temperature in C"));
    }
    if (facts.gain.has_value())
    {
        cards.push_back(integerCard("GAIN", *facts.gain, "camera gain setting"));
    }
    if (facts.isoSpeed.has_value())
    {
        cards.push_back(integerCard("ISOSPEED", *facts.isoSpeed, "ISO speed"));
    }
    if (facts.offset.has_value())
    {
        cards.push_back(integerCard("OFFSET", *facts.offset, "camera offset setting"));
    }
    if (!facts.filter.empty())
    {
        cards.push_back(textCard("FILTER", facts.filter, "filter"));
    }
    if (!facts.object.empty())
    {
        cards.push_back(textCard("OBJECT", facts.object, "target"));
    }
    if (!facts.instrument.empty())
    {
        cards.push_back(textCard("INSTRUME", facts.instrument, "camera"));
    }
    if (!facts.exposureStart.empty())
    {
        cards.push_back(textCard("DATE-OBS", facts.exposureStart, "exposure start, UTC"));
    }
    if (!facts.fileDate.empty())
    {
        cards.push_back(textCard("DATE", facts.fileDate, "file written, UTC"));
    }
    return cards;
}

FixtureImage acquisitionFrame(std::string name, const AcquisitionFacts& facts, const std::uint32_t width,
                              const std::uint32_t height)
{
    FixtureImage frame;
    frame.name = std::move(name);
    frame.width = width;
    frame.height = height;
    frame.channels = FixtureChannels::Mono;
    frame.sampleType = FixtureSampleType::UInt16;
    frame.domain = FixtureDomain::SensorCode;
    frame.pattern = FixturePattern::Constant;
    frame.levels = {FixtureChannelLevels{.base = 1000.0}};
    frame.cards = acquisitionCards(facts);
    return frame;
}

bool writeAcquisitionFrame(const FixtureImage& frame, const std::filesystem::path& path, std::string& error)
{
    const std::optional<FixtureSamples> samples = materializeFixture(frame, error);
    if (!samples.has_value())
    {
        return false;
    }
    std::error_code created;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), created);
        if (created)
        {
            error = "cannot create " + path.parent_path().string() + ": " + created.message();
            return false;
        }
    }
    return writeFixtureFits(frame, *samples, path, error);
}

} // namespace an::tests::fixtures
