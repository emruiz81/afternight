// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the dashboard's file cards say about a file: its size and the facts its header states, read by a probe that is
/// background work and never waits for a busy session, and the lines the recent-file cards and the featured file show.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/DashboardFileFacts.h"
#include "controls/FeaturedFilePresenter.h"
#include "controls/ImageViewFiles.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Image/OutputIntent.h>

#include <QFile>
#include <QLocale>

#include <gtest/gtest.h>

#include <chrono>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::CaseDirectory;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeCardDocument;

/// Writes a FITS file of an image carrying cards; fails the case when it cannot.
void writeFitsWithCards(const an::consumer::EngineSession& session, std::vector<an::CodecCard> cards,
                        const QString& path)
{
    std::optional<an::ui::ImageDocument> document = makeCardDocument(session, std::move(cards));
    ASSERT_TRUE(document.has_value());
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document->asset();
    ASSERT_NE(asset, nullptr);
    an::Result<an::OutputIntent> intent =
        an::OutputIntent::create(an::OutputIntentRequest{.sampleType = an::SampleType::Float32});
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    const an::Status written = an::ui::writeViewImage(session, *asset, path, *intent, {});
    ASSERT_TRUE(written.ok()) << written.message();
}

/// The header cards of a night's light frame.
std::vector<an::CodecCard> lightFrameCards(const std::string& object = "'M 42'")
{
    return {card(an::CodecNamespace::Fits, "OBJECT", object), card(an::CodecNamespace::Fits, "FILTER", "'Ha'"),
            card(an::CodecNamespace::Fits, "EXPTIME", "300."),
            card(an::CodecNamespace::Fits, "INSTRUME", "'ZWO ASI2600MM'"),
            card(an::CodecNamespace::Fits, "TELESCOP", "'RedCat 51'"),
            card(an::CodecNamespace::Fits, "DATE-OBS", "'2026-01-15T22:30:00'"),
            // When the file was written, which is not the night the frame was taken.
            card(an::CodecNamespace::Fits, "DATE", "'2026-02-01T10:00:00'")};
}

class DashboardFileFactsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
    }

    void TearDown() override
    {
        installed.reset();
        context.reset();
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
    CaseDirectory directory;
};

TEST_F(DashboardFileFactsTest, AProbeReadsTheImagesSizeAndTheFactsItsHeaderStates)
{
    const QString path = directory.path(QStringLiteral("light.fits"));
    writeFitsWithCards(context->engineSession(), lightFrameCards(), path);

    const std::optional<an::ui::DashboardFileFacts> facts = an::ui::probeDashboardFile(context->engineSession(), path);
    ASSERT_TRUE(facts.has_value());
    EXPECT_EQ(facts->size, QSize(16, 12));
    EXPECT_EQ(facts->object, QStringLiteral("M 42"));
    EXPECT_EQ(facts->filter, QStringLiteral("Ha"));
    EXPECT_EQ(facts->camera, QStringLiteral("ZWO ASI2600MM"));
    EXPECT_EQ(facts->telescope, QStringLiteral("RedCat 51"));
    ASSERT_TRUE(facts->exposureSeconds.has_value());
    EXPECT_DOUBLE_EQ(*facts->exposureSeconds, 300.0);
    EXPECT_EQ(facts->date, QDate(2026, 1, 15));
    EXPECT_EQ(an::ui::compactDashboardSummary(*facts), QStringLiteral("M 42 · Ha · 5m · ZWO ASI2600MM"));

    // The context's session is the one the dashboard probes in.
    const std::optional<an::ui::DashboardFileFacts> lent = an::ui::probeDashboardFile(path);
    ASSERT_TRUE(lent.has_value());
    EXPECT_EQ(lent->object, facts->object);
}

TEST_F(DashboardFileFactsTest, AFileThatStatesNothingHasAnEmptySummaryAndAFileThatIsNoImageHasNoFacts)
{
    const QString plain = directory.path(QStringLiteral("plain.fits"));
    writeFitsWithCards(context->engineSession(), {}, plain);
    const std::optional<an::ui::DashboardFileFacts> facts = an::ui::probeDashboardFile(context->engineSession(), plain);
    ASSERT_TRUE(facts.has_value());
    EXPECT_TRUE(an::ui::compactDashboardSummary(*facts).isEmpty());
    EXPECT_TRUE(facts->camera.isEmpty());

    const QString text = directory.path(QStringLiteral("notes.fits"));
    {
        QFile file(text);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(6000, 'n'));
    }
    EXPECT_FALSE(an::ui::probeDashboardFile(context->engineSession(), text).has_value());
    EXPECT_FALSE(an::ui::probeDashboardFile(context->engineSession(), directory.path(QStringLiteral("absent.fits")))
                     .has_value());
}

TEST(DashboardFileFactsSessionTest, ABusySessionRefusesTheProbeAtOnce)
{
    auto context = an::tests::dialogs::openDialogsContext(1u);
    ASSERT_NE(context, nullptr);
    CaseDirectory directory;
    const QString path = directory.path(QStringLiteral("light.fits"));
    writeFitsWithCards(context->engineSession(), lightFrameCards(), path);

    // The session's one thread is held by another operation: the probe is refused rather than queued behind it.
    std::optional<an::consumer::OperationScope> busy = an::tests::viewer::admitted(context->engineSession());
    ASSERT_TRUE(busy.has_value());
    const auto started = std::chrono::steady_clock::now();
    EXPECT_FALSE(an::ui::probeDashboardFile(context->engineSession(), path).has_value());
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(2));
    busy.reset();

    EXPECT_TRUE(an::ui::probeDashboardFile(context->engineSession(), path).has_value());
}

TEST(DashboardFileFactsTextTest, AnExposureIsShownInMinutesAndSecondsFromAMinuteUp)
{
    EXPECT_EQ(an::ui::dashboardExposureText(300.0), QStringLiteral("5m"));
    EXPECT_EQ(an::ui::dashboardExposureText(150.0), QStringLiteral("2m 30s"));
    EXPECT_EQ(an::ui::dashboardExposureText(60.0), QStringLiteral("1m"));
    EXPECT_EQ(an::ui::dashboardExposureText(0.25), QStringLiteral("0.25s"));
    EXPECT_EQ(an::ui::dashboardExposureText(30.0), QStringLiteral("30s"));
}

TEST(DashboardFileFactsTextTest, TheCardsReadTheHeaderOfTheAstronomicalFormatsTheyAlwaysRead)
{
    for (const char* suffix : {"fits", "FIT", "xisf", "tif", "TIFF", "cr2", "CR3", "nef", "arw", "dng", "raf"})
    {
        EXPECT_TRUE(an::ui::isDashboardAstroFormat(QString::fromLatin1(suffix))) << suffix;
    }
    for (const char* suffix : {"png", "jpg", "jpeg", "ser", ""})
    {
        EXPECT_FALSE(an::ui::isDashboardAstroFormat(QString::fromLatin1(suffix))) << suffix;
    }
}

// The featured file shows the night the light was taken, not the day the file was written (DATE).
TEST_F(DashboardFileFactsTest, TheFeaturedFileNamesTheTargetTheNightAndTheEquipment)
{
    const QString path = directory.path(QStringLiteral("light.fits"));
    writeFitsWithCards(context->engineSession(), lightFrameCards(), path);

    an::ui::FeaturedFilePresenter presenter;
    const QString text = presenter.metadataTextForFile(path, QSize(16, 12));

    for (const QString& expected :
         {QStringLiteral("M 42"), QStringLiteral("Ha"), QStringLiteral("5m"), QStringLiteral("ZWO ASI2600MM"),
          QStringLiteral("RedCat 51"), QLocale().toString(QDate(2026, 1, 15), QLocale::ShortFormat)})
    {
        EXPECT_TRUE(text.contains(expected)) << expected.toStdString() << " in " << text.toStdString();
    }
}

TEST_F(DashboardFileFactsTest, TheFeaturedFileLeavesOutATargetTheHeaderLeftAsAPlaceholder)
{
    const QString path = directory.path(QStringLiteral("light.fits"));
    writeFitsWithCards(context->engineSession(), lightFrameCards("'N/A'"), path);

    an::ui::FeaturedFilePresenter presenter;
    const QString text = presenter.metadataTextForFile(path, QSize(16, 12));

    EXPECT_FALSE(text.contains(QStringLiteral("N/A"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("Ha"))) << text.toStdString();
}

} // namespace
