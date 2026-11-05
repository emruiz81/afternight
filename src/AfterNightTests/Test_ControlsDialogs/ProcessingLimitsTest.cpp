// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The processing limits: read and stored through their texts - the limits a settings database holds among them - the
/// session settings they give at the next start, and the Processing page that shows what the session in use runs with
/// beside what is stored, saying when a change waits for a restart; and the preview frame rate the page states beside
/// them, which waits for nothing.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/settings/GeneralSettingsPage.h"
#include "controls/settings/ProcessingLimits.h"
#include "controls/settings/ProcessingSettingsPage.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <QCheckBox>
#include <QHash>
#include <QJsonObject>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <QLabel>
#include <QSpinBox>
#include <thread>

#include <gtest/gtest.h>

namespace
{

using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::SettingsStore;
using an::ui::ProcessingLimits;

constexpr std::uint64_t kMebibyte = 1024u * 1024u;

/// A settings reader over texts a case states.
an::ui::SettingReader readerOf(const QHash<QString, QString>& texts)
{
    return [texts](const QString& key, const QString& fallback) { return texts.value(key, fallback); };
}

TEST(ProcessingLimitsTest, TheStoredLimitsAreReadThroughTheirTexts)
{
    const QString threads = QString::fromLatin1(SettingsKeys::PROCESSING_MAX_THREADS_BATCH);
    const QString memory = QString::fromLatin1(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB);

    EXPECT_EQ(an::ui::readProcessingLimits(readerOf({})), ProcessingLimits{});
    EXPECT_EQ(an::ui::readProcessingLimits(readerOf({{threads, "6"}, {memory, " 4096 "}})),
              (ProcessingLimits{6u, 4096u}));
    EXPECT_EQ(an::ui::readProcessingLimits(readerOf({{threads, "many"}, {memory, "-3"}})), ProcessingLimits{});
    EXPECT_EQ(an::ui::readProcessingLimits(readerOf({{threads, "999999"}})).threads, an::kMaxConfigurableConcurrency);
    EXPECT_EQ(an::ui::readProcessingLimits({}), ProcessingLimits{});
}

TEST(ProcessingLimitsTest, TheStoredTextsReadBackAsTheLimitsTheyStore)
{
    QHash<QString, QString> texts;
    an::ui::writeProcessingLimits(ProcessingLimits{3u, 2048u},
                                  [&texts](const QString& key, const QString& value) { texts.insert(key, value); });

    EXPECT_EQ(texts.value(QString::fromLatin1(SettingsKeys::PROCESSING_MAX_THREADS_BATCH)), QStringLiteral("3"));
    EXPECT_EQ(texts.value(QString::fromLatin1(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB)), QStringLiteral("2048"));
    EXPECT_EQ(an::ui::readProcessingLimits(readerOf(texts)), (ProcessingLimits{3u, 2048u}));
}

// The preview frame rate is a whole number of previews a second from five to sixty: thirty when nothing or something
// else is stored, the nearest end of the range for a number outside it.
TEST(ProcessingLimitsTest, ThePreviewFrameRateIsReadAsAWholeNumberFromFiveToSixty)
{
    const QString rate = QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE);

    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({})), 30u);
    EXPECT_EQ(an::ui::readPreviewFrameRate({}), 30u);
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({{rate, " 24 "}})), 24u);
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({{rate, "fast"}})), 30u);
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({{rate, "1"}})), an::ui::kMinPreviewFrameRate);
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({{rate, "-8"}})), an::ui::kMinPreviewFrameRate);
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf({{rate, "240"}})), an::ui::kMaxPreviewFrameRate);
    EXPECT_EQ(an::ui::kMinPreviewFrameRate, 5u);
    EXPECT_EQ(an::ui::kMaxPreviewFrameRate, 60u);

    QHash<QString, QString> texts;
    const an::ui::SettingWriter write = [&texts](const QString& key, const QString& value) {
        texts.insert(key, value);
    };
    an::ui::writePreviewFrameRate(15u, write);
    EXPECT_EQ(texts.value(rate), QStringLiteral("15"));
    EXPECT_EQ(an::ui::readPreviewFrameRate(readerOf(texts)), 15u);
    an::ui::writePreviewFrameRate(1000u, write);
    EXPECT_EQ(texts.value(rate), QStringLiteral("60")) << "a rate outside the range is stored as its nearest end";
}

TEST(ProcessingLimitsTest, AutomaticLimitsBuildTheSessionWithTheHostsThreadsAndNoMemoryBudget)
{
    an::consumer::EngineSessionSettings base;
    base.admissionQueueCapacity = 7u;

    const an::consumer::EngineSessionSettings automatic = an::ui::sessionSettingsForLimits(ProcessingLimits{}, base);
    EXPECT_EQ(automatic.maxConcurrency, an::kUseHostConcurrency);
    EXPECT_EQ(automatic.limits.memoryBudgetBytes, an::kDefaultMemoryBudgetBytes);
    EXPECT_EQ(automatic.admissionQueueCapacity, 7u);

    const an::consumer::EngineSessionSettings stated =
        an::ui::sessionSettingsForLimits(ProcessingLimits{3u, 2048u}, base);
    EXPECT_EQ(stated.maxConcurrency, 3u);
    EXPECT_EQ(stated.limits.memoryBudgetBytes, 2048u * kMebibyte);
    EXPECT_EQ(an::ui::limitsOfSessionSettings(stated), (ProcessingLimits{3u, 2048u}));
    EXPECT_EQ(an::ui::limitsOfSessionSettings(automatic), ProcessingLimits{});
    EXPECT_TRUE(an::consumer::checkSessionSettings(stated).ok());
}

TEST(ProcessingLimitsTest, AStatedLimitTheSessionDoesNotRunWithWaitsForTheNextStart)
{
    auto context = an::tests::dialogs::openDialogsContext(2u);
    ASSERT_NE(context, nullptr);
    const an::consumer::EngineSession& session = context->engineSession();

    const an::ui::ProcessingLimitsState same = an::ui::processingLimitsState(session, ProcessingLimits{2u, 0u});
    EXPECT_EQ(same.width, 2u);
    EXPECT_EQ(same.memoryBudgetBytes, an::kDefaultMemoryBudgetBytes);
    EXPECT_EQ(same.previewThreads, 1u);
    EXPECT_FALSE(same.threadsPendingRestart);
    EXPECT_FALSE(same.memoryPendingRestart);

    const an::ui::ProcessingLimitsState changed = an::ui::processingLimitsState(session, ProcessingLimits{4u, 2048u});
    EXPECT_TRUE(changed.threadsPendingRestart);
    EXPECT_TRUE(changed.memoryPendingRestart);
    // Nothing the state reads changes the session.
    EXPECT_EQ(session.width(), 2u);
    EXPECT_FALSE(session.settingsPendingRestart());
}

TEST(ProcessingLimitsTest, APreviewIsGivenNoThreadOfItsOwnAtAWidthOfOneAndAQuarterAboveFour)
{
    for (const auto& [width, preview] :
         std::vector<std::pair<std::uint32_t, std::uint32_t>>{{1u, 0u}, {4u, 1u}, {8u, 2u}})
    {
        auto context = an::tests::dialogs::openDialogsContext(width);
        ASSERT_NE(context, nullptr);
        EXPECT_EQ(an::ui::processingLimitsState(context->engineSession(), ProcessingLimits{width, 0u}).previewThreads,
                  preview)
            << width;
    }
}

class ProcessingPageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext(2u);
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
    }

    void TearDown() override
    {
        installed.reset();
        context.reset();
    }

    [[nodiscard]] static QString labelText(ProcessingSettingsPage& page, const char* name)
    {
        auto* label = page.findChild<QLabel*>(QString::fromLatin1(name));
        EXPECT_NE(label, nullptr) << name;
        return label ? label->text() : QString();
    }

    [[nodiscard]] static bool shown(ProcessingSettingsPage& page, const char* name)
    {
        auto* widget = page.findChild<QWidget*>(QString::fromLatin1(name));
        EXPECT_NE(widget, nullptr) << name;
        return widget != nullptr && !widget->isHidden();
    }

    [[nodiscard]] static QSpinBox* spinBox(ProcessingSettingsPage& page, const char* name)
    {
        auto* spin = page.findChild<QSpinBox*>(QString::fromLatin1(name));
        EXPECT_NE(spin, nullptr) << name;
        return spin;
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

TEST_F(ProcessingPageTest, ThePageShowsWhatTheSessionRunsWithAndSaysWhenAChangeWaitsForARestart)
{
    SettingsStore store;
    store.store(SettingsKeys::PROCESSING_MAX_THREADS_BATCH, QStringLiteral("2"));
    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();

    EXPECT_EQ(page.stagedLimits(), (ProcessingLimits{2u, 0u}));
    EXPECT_TRUE(labelText(page, "processingThreadsInUseLabel").contains(QStringLiteral("2 threads")));
    EXPECT_TRUE(labelText(page, "processingMemoryInUseLabel").contains(QStringLiteral("no memory limit")));
    EXPECT_TRUE(labelText(page, "processingPreviewThreadsLabel").contains(QStringLiteral("1 of the 2")));
    EXPECT_FALSE(shown(page, "processingThreadsPendingLabel"));
    EXPECT_FALSE(shown(page, "processingMemoryPendingLabel"));

    // A changed limit is pending as soon as the page states it, and stays pending once stored: the session in use
    // keeps the limits it was built with.
    QSpinBox* threads = spinBox(page, "processingMaxThreadsSpin");
    QSpinBox* memory = spinBox(page, "processingMemoryLimitSpin");
    ASSERT_NE(threads, nullptr);
    ASSERT_NE(memory, nullptr);
    threads->setValue(1);
    EXPECT_TRUE(shown(page, "processingThreadsPendingLabel"));
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::PROCESSING_MAX_THREADS_BATCH), QStringLiteral("1"));
    EXPECT_TRUE(shown(page, "processingThreadsPendingLabel"));
    EXPECT_EQ(context->engineSession().width(), 2u);

    QList<QCheckBox*> checks = page.findChildren<QCheckBox*>();
    QCheckBox* memoryAuto = nullptr;
    for (QCheckBox* check : checks)
    {
        if (check->toolTip().contains(QStringLiteral("memory"), Qt::CaseInsensitive))
        {
            memoryAuto = check;
        }
    }
    ASSERT_NE(memoryAuto, nullptr);
    memoryAuto->setChecked(false);
    memory->setValue(2048);
    EXPECT_TRUE(shown(page, "processingMemoryPendingLabel"));
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB), QStringLiteral("2048"));
}

// Limits an imported settings file stored - three threads and 2048 MB - are read from a copy of the settings database
// as the import left it: they are the limits the session builds with at the next start, the page saying that each
// waits for it.
TEST_F(ProcessingPageTest, TheStoredLimitsAreReadAndWaitForTheNextStart)
{
    QTemporaryDir copies;
    ASSERT_TRUE(copies.isValid());
    const QString database = QDir(copies.path()).filePath(QStringLiteral("settings.db"));
    {
        SettingsStore writer;
        GeneralSettingsPage general(writer.persistence());
        general.loadSettings();
        const QJsonObject processing{{QStringLiteral("maxThreadsBatch"), 3},
                                     {QStringLiteral("maxThreadsPreview"), 2},
                                     {QStringLiteral("gpuEnabled"), false},
                                     {QStringLiteral("memoryLimitMb"), 2048}};
        ASSERT_TRUE(general.stageImportedSettings(QJsonObject{{QStringLiteral("processing"), processing}}));
        general.saveSettings();
        ASSERT_TRUE(writer.copyTo(database));
    }
    SettingsStore store(database);

    const ProcessingLimits limits = an::ui::readProcessingLimits(store.reader());
    EXPECT_EQ(limits, (ProcessingLimits{3u, 2048u}));
    const an::consumer::EngineSessionSettings settings = an::ui::sessionSettingsForLimits(limits);
    EXPECT_EQ(settings.maxConcurrency, 3u);
    EXPECT_EQ(settings.limits.memoryBudgetBytes, 2048u * kMebibyte);
    EXPECT_TRUE(an::consumer::checkSessionSettings(settings).ok());

    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();
    EXPECT_EQ(page.stagedLimits(), limits);
    EXPECT_TRUE(shown(page, "processingThreadsPendingLabel")) << "the session in use runs with two threads";
    EXPECT_TRUE(shown(page, "processingMemoryPendingLabel")) << "the session in use runs with no memory limit";
    EXPECT_TRUE(page.validateSettings().isValid);
}

// A thread limit stored above this computer's logical cores - settings brought from a larger computer - is the limit
// the session builds with at the next start. The page states it as stored and says it waits for that start, rather
// than show the core count and say nothing waits, and saving the page keeps it.
TEST_F(ProcessingPageTest, AThreadLimitStoredAboveThisComputersCoresIsStatedAsStoredAndWaitsForTheNextStart)
{
    const std::uint32_t above = (std::max)(std::thread::hardware_concurrency(), 2u) + 1u;
    SettingsStore store;
    store.store(SettingsKeys::PROCESSING_MAX_THREADS_BATCH, QString::number(above));
    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();

    EXPECT_EQ(page.stagedLimits().threads, above);
    EXPECT_EQ(an::ui::sessionSettingsForLimits(page.stagedLimits()).maxConcurrency, above);
    EXPECT_TRUE(shown(page, "processingThreadsPendingLabel")) << "the session in use runs with two threads";
    EXPECT_TRUE(page.validateSettings().isValid);

    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::PROCESSING_MAX_THREADS_BATCH), QString::number(above));
}

// GPU acceleration is planned and not yet there: on a clean installation its box is unchecked as well as disabled, and
// resetting the page or saving it leaves it so.
TEST_F(ProcessingPageTest, GpuAccelerationIsOfferedUncheckedAndDisabledOnACleanInstallation)
{
    SettingsStore store;
    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();
    QCheckBox* gpu = nullptr;
    for (QCheckBox* check : page.findChildren<QCheckBox*>())
    {
        if (check->text().contains(QStringLiteral("GPU")))
        {
            gpu = check;
        }
    }
    ASSERT_NE(gpu, nullptr);
    EXPECT_FALSE(gpu->isEnabled());
    EXPECT_FALSE(gpu->isChecked()) << "a feature that is not there is not shown turned on";
    page.resetToDefaults();
    EXPECT_FALSE(gpu->isChecked());
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::PROCESSING_GPU_ENABLED), QStringLiteral("0"));
}

// The page states the preview frame rate the settings hold, thirty on a clean installation, stores what it is set to
// with no restart pending, and goes back to thirty when reset.
TEST_F(ProcessingPageTest, ThePageStatesThePreviewFrameRateAndStoresItWithNoRestart)
{
    SettingsStore store;
    // The thread limit the session runs with, so nothing waits for a restart before the rate changes.
    store.store(SettingsKeys::PROCESSING_MAX_THREADS_BATCH, QStringLiteral("2"));
    store.store(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE, QStringLiteral("20"));
    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();
    QSpinBox* rate = spinBox(page, "processingPreviewFrameRateSpin");
    ASSERT_NE(rate, nullptr);
    EXPECT_EQ(rate->value(), 20);
    EXPECT_EQ(page.stagedPreviewFrameRate(), 20u);
    EXPECT_EQ(rate->minimum(), 5);
    EXPECT_EQ(rate->maximum(), 60);
    ASSERT_FALSE(shown(page, "processingThreadsPendingLabel"));

    rate->setValue(45);
    EXPECT_TRUE(page.hasUnsavedChanges());
    page.saveSettings();
    EXPECT_EQ(store.stored(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE), QStringLiteral("45"));
    EXPECT_FALSE(shown(page, "processingThreadsPendingLabel")) << "the frame rate waits for no restart";

    page.resetToDefaults();
    EXPECT_EQ(rate->value(), 30);

    SettingsStore clean;
    ProcessingSettingsPage fresh(clean.persistence());
    fresh.loadSettings();
    EXPECT_EQ(fresh.stagedPreviewFrameRate(), 30u);
}

TEST_F(ProcessingPageTest, ALimitTooLowIsRefused)
{
    SettingsStore store;
    store.store(SettingsKeys::PROCESSING_MEMORY_LIMIT_MB, QStringLiteral("600"));
    ProcessingSettingsPage page(store.persistence());
    page.loadSettings();

    EXPECT_FALSE(page.validateSettings().isValid);
}

// A frame rate's text is read as a whole number held to the range, and a text that is no number as nothing, which a
// caller - a settings file imported, a host's launch argument - leaves out rather than storing as the range's lowest
// end.
TEST(ProcessingLimitsTest, AFrameRateTextIsReadAsAWholeNumberInTheRangeOrAsNothing)
{
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral("24")), std::optional<std::uint32_t>(24u));
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral(" 24 ")), std::optional<std::uint32_t>(24u));
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral("1")),
              std::optional<std::uint32_t>(an::ui::kMinPreviewFrameRate));
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral("240")),
              std::optional<std::uint32_t>(an::ui::kMaxPreviewFrameRate));
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral("fast")), std::nullopt);
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QStringLiteral("29.7")), std::nullopt);
    EXPECT_EQ(an::ui::parsePreviewFrameRate(QString()), std::nullopt);
}

} // namespace
