// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an integration job's header says of it: its lights, their groups, the time the included ones integrate, the
/// setup they say, what its files weigh and what a run would write, each read from the job's model, and what the disk
/// adds - the bytes of its lights and what is free where its runs write.

#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobFacts.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSize>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace
{

using ImageIntegration::IntegrationJobDisk;
using ImageIntegration::IntegrationJobFacts;

/// The mark between two facts.
const QString kDot = QStringLiteral(" · ");

/// A light frame of a file that need not be there.
[[nodiscard]] FrameInfo light(const QString& file, const int gain = 800, const bool excluded = false)
{
    FrameInfo frame(FrameId(file), file,
                    FrameMetadata(FrameType::Light, QSize(64, 48), 120.0, -10.0, gain, 1, QStringLiteral("L"),
                                  QStringLiteral("M 42"), QDateTime::currentDateTimeUtc(), QString(),
                                  FrameSampleLayout{.channels = 1, .mosaic = false}));
    frame.excluded = excluded;
    return frame;
}

} // namespace

// The facts count every light and their groups, and time only the lights that are included; the setup is what the
// lights say - their size, their gain, their offset.
TEST(IntegrationJobFacts, CountTheLightsTheirGroupsAndTheirTime)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {light(QStringLiteral("/data/a.fit")), light(QStringLiteral("/data/b.fit")),
                                       light(QStringLiteral("/data/c.fit"), 800, true)});
    const IntegrationJobFacts facts = ImageIntegration::integrationJobFacts(model);
    EXPECT_EQ(facts.lights, 3);
    EXPECT_EQ(facts.groups, 1);
    EXPECT_DOUBLE_EQ(facts.integrationSeconds, 240.0);
    EXPECT_EQ(facts.setup,
              QStringLiteral("64x48") + kDot + QStringLiteral("Gain 800") + kDot + QStringLiteral("Offset 1"));
    EXPECT_EQ(facts.lightPaths.size(), 3);

    EXPECT_EQ(ImageIntegration::integrationJobFactsText(facts, IntegrationJobDisk{}),
              QStringLiteral("3 frames") + kDot + QStringLiteral("1 group") + kDot + QStringLiteral("4m 0s") + kDot +
                  facts.setup);
    // What the files weigh is said once the disk was read, after the time.
    EXPECT_EQ(ImageIntegration::integrationJobFactsText(facts, IntegrationJobDisk{.lightBytes = 3 * 512 * 1024}),
              QStringLiteral("3 frames") + kDot + QStringLiteral("1 group") + kDot + QStringLiteral("4m 0s") + kDot +
                  QStringLiteral("1.5 MB") + kDot + facts.setup);
}

// A job with no lights says so, and nothing it cannot say.
TEST(IntegrationJobFacts, AJobWithNoLightsSaysSo)
{
    const IntegrationDataModel model;
    const IntegrationJobFacts facts = ImageIntegration::integrationJobFacts(model);
    EXPECT_EQ(facts.lights, 0);
    EXPECT_EQ(facts.neededBytes, 0);
    EXPECT_EQ(ImageIntegration::integrationJobFactsText(facts, IntegrationJobDisk{}), QStringLiteral("0 frames"));
}

// Lights of several setups are not said as one: the setups are counted.
TEST(IntegrationJobFacts, SeveralSetupsAreCounted)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light,
                    {light(QStringLiteral("/data/a.fit"), 800), light(QStringLiteral("/data/b.fit"), 100),
                     light(QStringLiteral("/data/c.fit"), 100)});
    const IntegrationJobFacts facts = ImageIntegration::integrationJobFacts(model);
    EXPECT_EQ(facts.setup, QStringLiteral("2 setups"));
    EXPECT_EQ(facts.lights, 3);
    EXPECT_TRUE(ImageIntegration::integrationJobFactsText(facts, IntegrationJobDisk{})
                    .endsWith(kDot + QStringLiteral("2 setups")));
}

// What a run would write is every part of its estimate, which is what the job needs of its disk.
TEST(IntegrationJobFacts, TheNeedIsWhatARunWouldWrite)
{
    IntegrationDataModel model;
    model.addFrames(FrameType::Light, {light(QStringLiteral("/data/a.fit")), light(QStringLiteral("/data/b.fit"))});
    const ImageIntegration::IntegrationOutputEstimate estimate = ImageIntegration::estimateIntegrationOutput(model);
    // A light of 64 x 48 in one channel of 32-bit floats: 12288 bytes registered, and a stacked image as large.
    EXPECT_EQ(estimate.includedLights, 2);
    EXPECT_EQ(estimate.registeredBytes, 2 * 64 * 48 * 4);
    EXPECT_EQ(estimate.stackedBytes, static_cast<qint64>(estimate.groups) * 64 * 48 * 4);
    EXPECT_GT(estimate.total(), 0);
    EXPECT_EQ(estimate.total(), estimate.stackedBytes + estimate.rejectionBytes + estimate.calibratedBytes +
                                    estimate.registeredBytes + estimate.drizzlePixmapBytes);
    EXPECT_EQ(ImageIntegration::integrationJobFacts(model).neededBytes, estimate.total());
}

// The disk adds what the lights' files weigh - the ones that are there - and what is free where the runs write, which
// for a folder not made yet is what is free where it will be made.
TEST(IntegrationJobFacts, TheDiskSaysWhatTheFilesWeighAndWhatIsFree)
{
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const QString first = folder.filePath(QStringLiteral("a.fit"));
    const QString second = folder.filePath(QStringLiteral("b.fit"));
    {
        QFile file(first);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(1000, 'x'));
    }
    {
        QFile file(second);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(234, 'x'));
    }
    const IntegrationJobDisk read =
        ImageIntegration::readIntegrationJobDisk({first, second, folder.filePath(QStringLiteral("not-there.fit"))},
                                                 folder.filePath(QStringLiteral("output/not/made/yet")));
    EXPECT_EQ(read.lightBytes, 1234);
    EXPECT_GT(read.freeBytes, 0) << "what is free where the folder will be made";

    const IntegrationJobDisk none = ImageIntegration::readIntegrationJobDisk({}, QString());
    EXPECT_EQ(none.lightBytes, 0);
    EXPECT_LT(none.freeBytes, 0) << "a job with no output folder has no disk";
}

// The disk's line: what is free, and what the job needs once it needs anything; and the share of what is free the need
// takes, whole once it is over it.
TEST(IntegrationJobFacts, TheDiskLineSaysWhatIsFreeAndWhatIsNeeded)
{
    constexpr qint64 kGiB = 1024LL * 1024LL * 1024LL;
    IntegrationJobFacts facts;
    const IntegrationJobDisk disk{.lightBytes = 0, .freeBytes = 200 * kGiB};
    EXPECT_EQ(ImageIntegration::integrationJobDiskText(facts, disk), QStringLiteral("200.00 GB free"));
    EXPECT_DOUBLE_EQ(ImageIntegration::integrationJobDiskShare(facts, disk), 0.0);

    facts.neededBytes = 50 * kGiB;
    EXPECT_EQ(ImageIntegration::integrationJobDiskText(facts, disk),
              QStringLiteral("200.00 GB free") + kDot + QStringLiteral("need ~50.00 GB"));
    EXPECT_DOUBLE_EQ(ImageIntegration::integrationJobDiskShare(facts, disk), 0.25);
    EXPECT_FALSE(ImageIntegration::integrationJobDiskExceeded(facts, disk));

    facts.neededBytes = 300 * kGiB;
    EXPECT_DOUBLE_EQ(ImageIntegration::integrationJobDiskShare(facts, disk), 1.0);
    EXPECT_TRUE(ImageIntegration::integrationJobDiskExceeded(facts, disk));
}
