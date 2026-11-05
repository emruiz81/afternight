// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "ProbeSupport.h"

#include <AfterNightEngine/Core/Alignment.h>
#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/IO/FitsCodec.h>
#include <AfterNightEngine/IO/JpegCodec.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/IO/PngCodec.h>
#include <AfterNightEngine/IO/TiffCodec.h>
#include <AfterNightEngine/IO/XisfCodec.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <optional>
#include <random>
#include <system_error>
#include <utility>

namespace probe
{
namespace
{

/// A process-wide counter that makes two scratch directories created in the same instant distinct.
std::atomic<std::uint64_t> g_scratchSequence{0u};

/// The sensor's bias pedestal, in codes.
constexpr double kPedestal = 1000.0;

/// The read noise, in codes.
constexpr double kReadNoise = 6.0;

/// The dark current, in codes per second of exposure.
constexpr double kDarkCurrent = 0.5;

/// The illuminated level of a flat at the optical centre, in codes.
constexpr double kFlatLevel = 24000.0;

/// The sky background of a light at the optical centre, in codes.
constexpr double kSkyLevel = 700.0;

/// The spacing of the star lattice cells, in pixels. One star per cell.
constexpr std::uint32_t kStarCell = 20u;

/// A well-mixed 64-bit hash, for noise and star placement that depend on nothing but their inputs.
[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept
{
    value ^= value >> 30u;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27u;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31u;
    return value;
}

/// A value in `[0, 1)` from a hash.
[[nodiscard]] double unit(std::uint64_t value) noexcept
{
    return static_cast<double>(mix(value) >> 11u) / static_cast<double>(1ull << 53u);
}

/// An approximately standard normal value from a hash: the centred sum of twelve uniforms.
[[nodiscard]] double normal(std::uint64_t value) noexcept
{
    double sum = 0.0;
    for (std::uint64_t index = 0u; index < 12u; ++index)
    {
        sum += unit(value * 13u + index);
    }
    return sum - 6.0;
}

/// The optical system's relative response at a pixel: one at the centre, falling towards the corners.
[[nodiscard]] double vignette(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) noexcept
{
    const double dx = (static_cast<double>(x) + 0.5 - 0.5 * width) / (0.5 * width);
    const double dy = (static_cast<double>(y) + 0.5 - 0.5 * height) / (0.5 * height);
    return 1.0 - 0.12 * (dx * dx + dy * dy);
}

/// One star of the fixed field, before the frame's shift is applied.
struct FieldStar
{
    double x = 0.0;
    double y = 0.0;
    double amplitude = 0.0;
    double sigma = 0.0;
};

/// The fixed star field every light of a set shows, one star per lattice cell.
[[nodiscard]] std::vector<FieldStar> starField(std::uint32_t width, std::uint32_t height)
{
    std::vector<FieldStar> stars;
    for (std::uint32_t cellY = 0u; cellY * kStarCell < height; ++cellY)
    {
        for (std::uint32_t cellX = 0u; cellX * kStarCell < width; ++cellX)
        {
            const std::uint64_t key = (static_cast<std::uint64_t>(cellY) << 20u) ^ cellX ^ 0x5EEDull;
            FieldStar star;
            star.x = (static_cast<double>(cellX) + 0.2 + 0.6 * unit(key + 1u)) * kStarCell;
            star.y = (static_cast<double>(cellY) + 0.2 + 0.6 * unit(key + 2u)) * kStarCell;
            if (star.x < 10.0 || star.y < 10.0 || star.x > width - 10.0 || star.y > height - 10.0)
            {
                continue;
            }
            star.amplitude = 1500.0 + 14000.0 * unit(key + 3u);
            star.sigma = 1.2 + 0.6 * unit(key + 4u);
            stars.push_back(star);
        }
    }
    return stars;
}

} // namespace

ScratchDirectory::ScratchDirectory()
{
    std::error_code error;
    const std::filesystem::path base = std::filesystem::temp_directory_path(error);
    if (error)
    {
        return;
    }
    const std::uint64_t sequence = g_scratchSequence.fetch_add(1u);
    std::random_device entropy;
    const std::uint64_t salt = (static_cast<std::uint64_t>(entropy()) << 32u) ^ entropy();
    m_root =
        base / ("an_probe_" + std::to_string(mix(salt ^ sequence) & 0xFFFFFFFFull) + "_" + std::to_string(sequence));
    m_valid = std::filesystem::create_directories(m_root, error) && !error;
}

ScratchDirectory::~ScratchDirectory()
{
    if (m_valid)
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }
}

bool ScratchDirectory::valid() const noexcept
{
    return m_valid;
}

const std::filesystem::path& ScratchDirectory::root() const noexcept
{
    return m_root;
}

std::string ScratchDirectory::file(std::string_view relative) const
{
    const std::u8string text =
        (m_root / std::filesystem::path(std::u8string(relative.begin(), relative.end()))).lexically_normal().u8string();
    return std::string(text.begin(), text.end());
}

bool ScratchDirectory::makeDirectory(std::string_view relative) const
{
    std::error_code error;
    const std::filesystem::path directory =
        m_root / std::filesystem::path(std::u8string(relative.begin(), relative.end()));
    std::filesystem::create_directories(directory, error);
    return !error && std::filesystem::is_directory(directory, error);
}

std::vector<std::string> ScratchDirectory::fileNames(std::string_view relative) const
{
    std::vector<std::string> names;
    std::error_code error;
    const std::filesystem::path directory =
        relative.empty() ? m_root : m_root / std::filesystem::path(std::u8string(relative.begin(), relative.end()));
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error))
    {
        if (entry->is_regular_file(error))
        {
            const std::u8string name = entry->path().filename().u8string();
            names.emplace_back(name.begin(), name.end());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

an::Result<std::shared_ptr<const an::CodecRegistry>> createProbeCodecRegistry()
{
    const std::array<an::Result<std::shared_ptr<const an::ImageCodec>>, 5u> adapters{
        an::createFitsCodec(), an::createXisfCodec(), an::createTiffCodec(), an::createPngCodec(),
        an::createJpegCodec()};
    std::vector<std::shared_ptr<const an::ImageCodec>> codecs;
    codecs.reserve(adapters.size());
    for (const an::Result<std::shared_ptr<const an::ImageCodec>>& adapter : adapters)
    {
        if (!adapter.hasValue())
        {
            return adapter.status();
        }
        codecs.push_back(*adapter);
    }
    return an::CodecRegistry::create(codecs);
}

an::Result<an::EngineContext> createProbeEngine(std::uint32_t maxConcurrency, std::uint64_t memoryBudgetBytes)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = createProbeCodecRegistry();
    if (!registry.hasValue())
    {
        return registry.status();
    }
    an::EngineContextOptions options;
    options.maxConcurrency = maxConcurrency;
    options.codecRegistry = std::move(*registry);
    if (memoryBudgetBytes != 0u)
    {
        options.limits.memoryBudgetBytes = memoryBudgetBytes;
    }
    return an::EngineContext::create(options);
}

std::string describeFailure(const an::Status& status)
{
    std::string text = std::string(an::errorCodeName(status.code())) + ": " + status.message();
    const an::Status root = status.rootCause();
    if (root.failed() && std::strcmp(root.message(), status.message()) != 0)
    {
        text += " (because: " + std::string(root.message()) + ")";
    }
    return text;
}

std::vector<std::uint16_t> renderSensorFrame(const SensorFrameRequest& request)
{
    const std::size_t count = static_cast<std::size_t>(request.width) * request.height;
    std::vector<double> signal(count, 0.0);
    const double thermal = request.kind == ExposureKind::Bias ? 0.0 : kDarkCurrent * request.exposureSeconds;

    for (std::uint32_t y = 0u; y < request.height; ++y)
    {
        for (std::uint32_t x = 0u; x < request.width; ++x)
        {
            double value = kPedestal + thermal;
            const double response = vignette(x, y, request.width, request.height);
            if (request.kind == ExposureKind::Flat)
            {
                value += kFlatLevel * response;
            }
            else if (request.kind == ExposureKind::Light)
            {
                value += kSkyLevel * request.transparency * response;
            }
            signal[static_cast<std::size_t>(y) * request.width + x] = value;
        }
    }

    if (request.kind == ExposureKind::Light)
    {
        for (const FieldStar& star : starField(request.width, request.height))
        {
            const double centreX = star.x + request.shiftX;
            const double centreY = star.y + request.shiftY;
            const double reach = 5.0 * star.sigma;
            const auto left = static_cast<std::int64_t>(std::floor(centreX - reach));
            const auto right = static_cast<std::int64_t>(std::ceil(centreX + reach));
            const auto top = static_cast<std::int64_t>(std::floor(centreY - reach));
            const auto bottom = static_cast<std::int64_t>(std::ceil(centreY + reach));
            const double inverse = 1.0 / (2.0 * star.sigma * star.sigma);
            for (std::int64_t row = (std::max)(std::int64_t{0}, top);
                 row <= (std::min)(static_cast<std::int64_t>(request.height) - 1, bottom); ++row)
            {
                for (std::int64_t column = (std::max)(std::int64_t{0}, left);
                     column <= (std::min)(static_cast<std::int64_t>(request.width) - 1, right); ++column)
                {
                    const double dx = static_cast<double>(column) + 0.5 - centreX;
                    const double dy = static_cast<double>(row) + 0.5 - centreY;
                    const double response = vignette(static_cast<std::uint32_t>(column),
                                                     static_cast<std::uint32_t>(row), request.width, request.height);
                    signal[static_cast<std::size_t>(row) * request.width + static_cast<std::size_t>(column)] +=
                        star.amplitude * request.transparency * response * std::exp(-(dx * dx + dy * dy) * inverse);
                }
            }
        }
    }

    std::vector<std::uint16_t> codes(count, 0u);
    for (std::size_t index = 0u; index < count; ++index)
    {
        const double shot = std::sqrt((std::max)(signal[index] - kPedestal, 0.0));
        const double noisy =
            signal[index] + (kReadNoise + 0.5 * shot) * normal((request.seed << 24u) ^ index ^ 0xC0DEull);
        codes[index] = static_cast<std::uint16_t>(std::lround(std::clamp(noisy, 0.0, 65535.0)));
    }
    return codes;
}

const char* exposureKindCard(ExposureKind kind) noexcept
{
    switch (kind)
    {
    case ExposureKind::Bias:
        return "Bias Frame";
    case ExposureKind::Dark:
        return "Dark Frame";
    case ExposureKind::Flat:
        return "Flat Field";
    case ExposureKind::Light:
        return "Light Frame";
    }
    return "Unknown";
}

an::Status writeSensorFrame(const an::EngineContext& engine, const std::string& path, const SensorFrameRequest& request)
{
    const std::vector<std::uint16_t> codes = renderSensorFrame(request);
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(request.width, request.height, 1u, engine.limits());
    if (!extent.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the synthetic frame has no valid extent");
    }
    const std::optional<an::RawFrameDescriptor> descriptor = an::RawFrameDescriptor::create(
        an::RawFrameDescriptorRequest{.extent = *extent,
                                      .sampleFormat = an::RawSampleFormat{2u, an::RawSampleClass::UnsignedInteger},
                                      .byteOrder = an::ByteOrder::Native,
                                      .channelSchema = an::ChannelSchema::Mono,
                                      .colorSpace = an::ColorSpace::Unspecified,
                                      .numericDomain = an::NumericDomain::RawSensorCode,
                                      .cfa = an::CfaDescriptor{},
                                      .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                      .pixelAspectRatio = 1.0});
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::create(an::ImageLayoutRequest{.extent = *extent,
                                                       .elementSizeBytes = 2u,
                                                       .samplesPerPixel = 1u,
                                                       .planeCount = 1u,
                                                       .rowAlignmentBytes = an::kDefaultBufferAlignmentBytes},
                                engine.limits());
    if (!descriptor.has_value() || !layout.has_value())
    {
        return an::Status::failure(an::ErrorCode::InvalidArgument, "the synthetic frame has no valid description");
    }
    an::Result<an::SampleBuffer> buffer =
        an::SampleBuffer::createOwned(engine, *layout, an::StorageInitialization::Zeroed);
    if (!buffer.hasValue())
    {
        return buffer.status();
    }
    an::Result<an::RawFrame> frame = an::RawFrame::create(*descriptor, *layout, std::move(*buffer));
    if (!frame.hasValue())
    {
        return frame.status();
    }
    for (std::uint32_t y = 0u; y < request.height; ++y)
    {
        const std::optional<std::span<std::byte>> row = frame->mutableRow(0u, y);
        if (!row.has_value())
        {
            return an::Status::failure(an::ErrorCode::InvalidState, "the synthetic frame has no writable row");
        }
        std::memcpy(row->data(), codes.data() + static_cast<std::size_t>(y) * request.width,
                    static_cast<std::size_t>(request.width) * sizeof(std::uint16_t));
    }

    std::vector<an::CodecCard> cards;
    const std::array<std::array<std::string, 3u>, 3u> spelled{
        std::array<std::string, 3u>{"IMAGETYP", exposureKindCard(request.kind), "type of exposure"},
        std::array<std::string, 3u>{"EXPTIME", std::to_string(request.exposureSeconds), "exposure time in seconds"},
        std::array<std::string, 3u>{"OBJECT", "Probe Field", "target name"}};
    for (const std::array<std::string, 3u>& card : spelled)
    {
        an::Result<an::CodecCard> made = an::CodecCard::create(an::CodecNamespace::Fits, card[0], card[1], card[2]);
        if (!made.hasValue())
        {
            return made.status();
        }
        cards.push_back(std::move(*made));
    }
    const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
        an::OutputIntentRequest{.sampleType = an::SampleType::UInt16, .cards = std::move(cards)});
    if (!intent.hasValue())
    {
        return intent.status();
    }

    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(engine, an::ExecutionOptions{});
    if (!execution.hasValue())
    {
        return execution.status();
    }
    const an::Result<an::OutputReport> written =
        an::writeImage(engine, *execution,
                       an::OutputRequest{.destinationPath = path,
                                         .format = an::CodecFormat::Fits,
                                         .intent = &*intent,
                                         .durability = an::OutputDurability::CloseOnly},
                       *frame, an::ImageMetadata{});
    return written.hasValue() ? an::Status::success() : written.status();
}

void RecordingProgress::onProgress(const an::ProgressReport& report) noexcept
{
    try
    {
        const std::scoped_lock lock(m_mutex);
        ++m_reports;
        if (std::find(m_operations.begin(), m_operations.end(), report.operationId) == m_operations.end())
        {
            m_operations.push_back(report.operationId);
        }
        const std::string stage = report.stage == nullptr ? std::string() : std::string(report.stage);
        if (std::find(m_stages.begin(), m_stages.end(), stage) == m_stages.end())
        {
            m_stages.push_back(stage);
        }
        if (report.total != 0u && report.completed > report.total)
        {
            m_overran = true;
        }
    }
    catch (...)
    {
        // A sink must not throw; a report that could not be recorded is a report the test did not
        // see, and the counts it reads say so.
    }
}

std::uint64_t RecordingProgress::reports() const
{
    const std::scoped_lock lock(m_mutex);
    return m_reports;
}

std::size_t RecordingProgress::operations() const
{
    const std::scoped_lock lock(m_mutex);
    return m_operations.size();
}

std::vector<std::string> RecordingProgress::stages() const
{
    const std::scoped_lock lock(m_mutex);
    return m_stages;
}

bool RecordingProgress::overran() const
{
    const std::scoped_lock lock(m_mutex);
    return m_overran;
}

} // namespace probe
