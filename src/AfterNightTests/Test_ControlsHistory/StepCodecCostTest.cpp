// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a step costs in each codec: the time to write and to read a 60-megapixel Float32 image and the size of its
/// file, each codec beside the snapshot in alternation, the median of the rounds.
///
/// A time is measured and never asserted - it depends on the host - so the case is skipped unless
/// `AFTERNIGHT_TEST_TIMINGS=1` asks for it, and it fails only when a step does not come back as written. The figures
/// are printed and recorded as the case's properties.

#include "Test_ControlsHistory/StepCodecTestSupport.h"

#include "fixtures/ScopedTempPath.h"

#include <QByteArray>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::history;
using an::tests::viewer::Channels;

constexpr std::size_t kRounds = 5u;

/// A sensor of 60.2 megapixels.
constexpr std::uint32_t kWidth = 9504u;
constexpr std::uint32_t kHeight = 6336u;

bool timingsAsked()
{
    return qEnvironmentVariable("AFTERNIGHT_TEST_TIMINGS") == QStringLiteral("1");
}

/// A linear deep-sky frame as a stack leaves one: a faint background with a gradient, noise in every sample and a
/// few thousand stars, so a compression meets what it meets in a real step and not a constant.
StepImageRequest deepSkyFrame(Channels channels)
{
    StepImageRequest request;
    request.type = an::SampleType::Float32;
    request.width = kWidth;
    request.height = kHeight;
    request.channels = channels;
    request.samples = [](std::uint32_t x, std::uint32_t y, std::uint32_t c) -> std::uint64_t {
        const std::uint64_t noise = mixedBits(x, y, c, 3u);
        // The sum of four uniform values is close enough to the bell a sensor's noise has.
        const double bell =
            (static_cast<double>(noise & 0xffffu) + static_cast<double>((noise >> 16u) & 0xffffu) +
             static_cast<double>((noise >> 32u) & 0xffffu) + static_cast<double>((noise >> 48u) & 0xffffu)) /
                65535.0 -
            2.0;
        double value = 0.02 + 0.004 * (static_cast<double>(x) / kWidth) + 0.0015 * bell + 0.001 * c;
        // A star every 211 x 193 pixels, a few pixels wide.
        const double dx = static_cast<double>(x % 211u) - 105.0;
        const double dy = static_cast<double>(y % 193u) - 96.0;
        const double distance = dx * dx + dy * dy;
        if (distance < 64.0)
        {
            value += 0.6 * std::exp(-distance / 6.0);
        }
        const float sample = static_cast<float>(value);
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &sample, sizeof(bits));
        return bits;
    };
    return request;
}

double medianOf(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2u];
}

/// One write of the image as a step of the codec and one read of it back, timed; the file is removed afterwards.
struct Round
{
    double writeMilliseconds = 0.0;
    double readMilliseconds = 0.0;
    std::uintmax_t bytes = 0u;
    bool whole = false;
};

Round runRound(const an::consumer::EngineSession& session, const an::ui::history::StepCodec& codec,
               const an::consumer::ImageAsset& image, const StepState& original, const std::filesystem::path& directory,
               bool compare)
{
    Round round;
    const std::filesystem::path file = directory / ("step" + std::string(codec.fileExtension()));
    const std::string path = utf8Of(file);
    const an::ArtifactIdentity identity = stepIdentityOf("cost");
    {
        an::Result<an::consumer::AssetReadLease> lease = image.read();
        if (!lease.hasValue())
        {
            ADD_FAILURE() << lease.status().message();
            return round;
        }
        const auto started = std::chrono::steady_clock::now();
        const an::Status written = codec.write(session, *lease, path, identity, {});
        round.writeMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (!written.ok())
        {
            ADD_FAILURE() << written.message();
            return round;
        }
    }
    std::error_code error;
    round.bytes = std::filesystem::file_size(file, error);
    {
        const auto started = std::chrono::steady_clock::now();
        an::Result<an::consumer::ImageAsset> read = codec.read(session, path, identity, {});
        round.readMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (!read.hasValue())
        {
            ADD_FAILURE() << read.status().message();
            return round;
        }
        if (compare)
        {
            const StepState restored = stepStateOf(*read);
            round.whole = restored.read && restored.samples.size() == original.samples.size() &&
                          std::memcmp(restored.samples.data(), original.samples.data(), original.samples.size()) == 0 &&
                          restored.metadata == original.metadata;
        }
        else
        {
            round.whole = true;
        }
    }
    std::filesystem::remove(file, error);
    return round;
}

void measure(Channels channels, const char* label)
{
    if (!timingsAsked())
    {
        GTEST_SKIP() << "a time is measured only when AFTERNIGHT_TEST_TIMINGS=1 asks for it";
    }
    std::optional<TestSessionProvider> provider = openStepCodecSession();
    ASSERT_TRUE(provider.has_value());
    const an::consumer::EngineSession& session = provider->engineSession();
    const an::tests::fixtures::ScopedTempPath directory =
        an::tests::fixtures::makeScopedTempDir("afternight-step-cost");

    const std::shared_ptr<an::consumer::ImageAsset> image = makeStepImage(session, deepSkyFrame(channels));
    ASSERT_NE(image, nullptr);
    const StepState original = stepStateOf(*image);
    ASSERT_TRUE(original.read);
    const double megabytes = static_cast<double>(original.samples.size()) / (1024.0 * 1024.0);

    const an::ui::history::StepCodecPtr snapshot = an::ui::history::snapshotStepCodec();
    std::printf("\n%s, Float32, %u x %u, %.0f MiB of samples; median of %zu rounds, each beside the snapshot\n", label,
                kWidth, kHeight, megabytes, kRounds);
    std::printf("%-18s %10s %10s %10s %10s %10s %8s\n", "codec", "write ms", "snap ms", "ratio", "read ms", "snap ms",
                "MiB");
    for (const CodecCase& candidate : everyStepCodec())
    {
        const an::ui::history::StepCodecPtr codec = candidate.make();
        ASSERT_NE(codec, nullptr);
        // One round of each that is not counted: the first write of a session pays for what the others find ready.
        ASSERT_TRUE(runRound(session, *snapshot, *image, original, directory.path(), false).whole);
        ASSERT_TRUE(runRound(session, *codec, *image, original, directory.path(), true).whole)
            << candidate.name << " did not give the step back as written";
        std::vector<double> writes;
        std::vector<double> reads;
        std::vector<double> snapshotWrites;
        std::vector<double> snapshotReads;
        std::uintmax_t bytes = 0u;
        for (std::size_t round = 0u; round < kRounds; ++round)
        {
            const Round beside = runRound(session, *snapshot, *image, original, directory.path(), false);
            const Round measured = runRound(session, *codec, *image, original, directory.path(), false);
            ASSERT_TRUE(beside.whole);
            ASSERT_TRUE(measured.whole);
            snapshotWrites.push_back(beside.writeMilliseconds);
            snapshotReads.push_back(beside.readMilliseconds);
            writes.push_back(measured.writeMilliseconds);
            reads.push_back(measured.readMilliseconds);
            bytes = measured.bytes;
        }
        const double write = medianOf(writes);
        const double read = medianOf(reads);
        const double snapshotWrite = medianOf(snapshotWrites);
        const double snapshotRead = medianOf(snapshotReads);
        const double megabytesWritten = static_cast<double>(bytes) / (1024.0 * 1024.0);
        std::printf("%-18s %10.0f %10.0f %10.2f %10.0f %10.0f %8.0f\n", candidate.name, write, snapshotWrite,
                    write / snapshotWrite, read, snapshotRead, megabytesWritten);
        const std::string prefix = std::string(label) + "." + candidate.name + ".";
        ::testing::Test::RecordProperty(prefix + "writeMs", std::to_string(write));
        ::testing::Test::RecordProperty(prefix + "snapshotWriteMs", std::to_string(snapshotWrite));
        ::testing::Test::RecordProperty(prefix + "readMs", std::to_string(read));
        ::testing::Test::RecordProperty(prefix + "snapshotReadMs", std::to_string(snapshotRead));
        ::testing::Test::RecordProperty(prefix + "fileMiB", std::to_string(megabytesWritten));
    }
    std::fflush(stdout);
}

TEST(StepCodecCost, WritingAndReadingASixtyMegapixelGreyStep)
{
    measure(Channels::Grey, "Grey");
}

TEST(StepCodecCost, WritingAndReadingASixtyMegapixelColourStep)
{
    measure(Channels::Rgb, "Colour");
}

} // namespace
