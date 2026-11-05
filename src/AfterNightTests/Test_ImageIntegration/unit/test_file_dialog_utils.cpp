// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <algorithm>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QTimer>

#include "app/FileDialogUtils.h"

namespace
{
class FakeSettings
{
public:
    QString select(const QString&, const QString& defaultValue)
    {
        return m_value.isNull() ? defaultValue : m_value;
    }

    void setValue(const QString& value)
    {
        m_value = value;
    }

private:
    QString m_value;
};
} // namespace

TEST(FileDialogUtils, NullSettingsFallsBackToPlatformDefault)
{
    const bool useSystem = an::ui::dialogs::useSystemFileBrowser(static_cast<FakeSettings*>(nullptr));
    EXPECT_EQ(useSystem, SettingsDefaults::INTERFACE_USE_SYSTEM_FILE_BROWSER);
}

TEST(FileDialogUtils, StoredSettingParsesSystemDialogChoice)
{
    FakeSettings settings;
    settings.setValue(QStringLiteral("1"));
    EXPECT_TRUE(an::ui::dialogs::useSystemFileBrowser(&settings));

    settings.setValue(QStringLiteral("0"));
    EXPECT_FALSE(an::ui::dialogs::useSystemFileBrowser(&settings));
}

TEST(FileDialogUtils, ResolveDialogOptionsAddsQtDialogFlagWhenDisabled)
{
    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));

    const QFileDialog::Options options = an::ui::dialogs::resolveDialogOptions(&settings, QFileDialog::ReadOnly);

    EXPECT_TRUE(options.testFlag(QFileDialog::ReadOnly));
    EXPECT_TRUE(options.testFlag(QFileDialog::DontUseNativeDialog));
}

TEST(FileDialogUtils, ApplyDialogBackendPreferenceTogglesNativeFlag)
{
    QFileDialog dialog;
    dialog.setOptions(QFileDialog::ReadOnly);

    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);
    EXPECT_TRUE(dialog.testOption(QFileDialog::DontUseNativeDialog));
    EXPECT_TRUE(dialog.testOption(QFileDialog::ReadOnly));

    settings.setValue(QStringLiteral("1"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);
    EXPECT_FALSE(dialog.testOption(QFileDialog::DontUseNativeDialog));
    EXPECT_TRUE(dialog.testOption(QFileDialog::ReadOnly));
}

TEST(FileDialogUtils, NonNativeDialogGetsModernChromeHooks)
{
    QFileDialog dialog;

    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);

    EXPECT_TRUE(dialog.testOption(QFileDialog::DontUseNativeDialog));
    EXPECT_TRUE(dialog.testOption(QFileDialog::DontUseCustomDirectoryIcons));
    EXPECT_FALSE(dialog.testOption(QFileDialog::HideNameFilterDetails));
    EXPECT_TRUE(dialog.property("anFileDialogChromeHooksInstalled").toBool());
    EXPECT_FALSE(dialog.property("anFileDialogInitialGeometryClamped").toBool());
}

// The application's own dialog, shown and left alone, lets its event loop rest: its first-size clamp lays the dialog
// out once rather than answering each layout request with another, which kept the loop busy until a click or a key
// and never let a timer fire.
TEST(FileDialogUtils, AShownNonNativeDialogLetsItsEventLoopRest)
{
    QFileDialog dialog;
    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);
    ASSERT_TRUE(dialog.testOption(QFileDialog::DontUseNativeDialog));

    struct LayoutRequests final : QObject
    {
        int count = 0;
        bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::LayoutRequest)
            {
                ++count;
            }
            return false;
        }
    } requests;
    dialog.installEventFilter(&requests);

    bool timerFired = false;
    QTimer::singleShot(100, [&timerFired]() { timerFired = true; });
    dialog.show();
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < 600 && !timerFired)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    EXPECT_TRUE(timerFired) << "a timer never fired while the dialog was shown: " << requests.count
                            << " layout requests reached it";
    EXPECT_LT(requests.count, 50);
    dialog.hide();
}

TEST(FileDialogUtils, CompactOpenFilterPresentationPreservesFiltersAcrossDialogBackends)
{
    QFileDialog dialog;
    const QStringList filters = {
        QStringLiteral("All supported formats (*.fit *.fits *.xisf *.mp4 *.avi *.mov *.mkv *.webm)"),
        QStringLiteral("FITS files (*.fit *.fits)")};

    an::ui::dialogs::applyCompactOpenFilterPresentation(dialog);
    dialog.setNameFilters(filters);

    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);
    EXPECT_TRUE(dialog.testOption(QFileDialog::HideNameFilterDetails));
    EXPECT_EQ(dialog.nameFilters(), filters);

    const auto combos = dialog.findChildren<QComboBox*>();
    const auto filterCombo = std::find_if(combos.cbegin(), combos.cend(), [&filters](const QComboBox* combo) {
        return combo && combo->count() == filters.size() &&
               combo->itemText(0).contains(QStringLiteral("All supported formats"), Qt::CaseInsensitive);
    });
    ASSERT_TRUE(filterCombo != combos.cend());
    EXPECT_EQ((*filterCombo)->itemText(0), QStringLiteral("All supported formats"));
    EXPECT_EQ((*filterCombo)->itemText(1), QStringLiteral("FITS files"));

    settings.setValue(QStringLiteral("1"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);
    EXPECT_TRUE(dialog.testOption(QFileDialog::HideNameFilterDetails));
    EXPECT_EQ(dialog.nameFilters(), filters);
}

TEST(FileDialogUtils, NonNativeDialogCompactsLongFilterCombo)
{
    QFileDialog dialog(
        nullptr, QStringLiteral("Select File"), QString(),
        QStringLiteral("All Supported (*.fit *.fits *.xisf *.png *.jpg *.jpeg *.tif *.tiff *.crw *.cr2 *.cr3 *.nef "
                       "*.dng *.arw *.sr2 *.srf *.dcr *.k25 *.kdc *.erf *.mrw *.orf *.pef *.raf *.raw *.x3f);;"
                       "FITS (*.fit *.fits);;XISF (*.xisf)"));

    FakeSettings settings;
    settings.setValue(QStringLiteral("0"));
    an::ui::dialogs::applyDialogBackendPreference(dialog, &settings);

    const auto combos = dialog.findChildren<QComboBox*>();
    bool foundFilterCombo = false;
    const int expectedFilterCount = dialog.nameFilters().size();
    for (QComboBox* combo : combos)
    {
        if (!combo || combo->count() <= 0)
        {
            continue;
        }

        if (combo->count() != expectedFilterCount)
        {
            continue;
        }

        const bool looksLikeFileTypeCombo =
            combo->itemText(0).contains(QStringLiteral("All Supported"), Qt::CaseInsensitive) ||
            combo->itemText(1).contains(QStringLiteral("FITS"), Qt::CaseInsensitive) ||
            combo->itemText(2).contains(QStringLiteral("XISF"), Qt::CaseInsensitive);
        if (!looksLikeFileTypeCombo)
        {
            continue;
        }

        foundFilterCombo = true;
        EXPECT_EQ(combo->sizeAdjustPolicy(), QComboBox::AdjustToMinimumContentsLengthWithIcon);
        EXPECT_GE(combo->minimumContentsLength(), 20);
        EXPECT_GE(combo->maximumWidth(), 360);
    }

    EXPECT_TRUE(foundFilterCombo);
}

TEST(FileDialogUtils, SupportedImageSuffixesAreClassified)
{
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("fit")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("fts")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("FITS")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("png")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("jpe")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("RAW")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("x3f")));

    EXPECT_FALSE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("ser")));
    EXPECT_FALSE(an::ui::dialogs::detail::isSupportedImageSuffix(QStringLiteral("txt")));
}

TEST(FileDialogUtils, SupportedImageOpenFiltersIncludeRawAndLessCommonCodecExtensions)
{
    const QStringList filters = an::ui::dialogs::supportedImageOpenNameFilters();
    ASSERT_GE(filters.size(), 2);

    const QString joined = an::ui::dialogs::supportedImageOpenNameFilter();
    EXPECT_TRUE(joined.contains(QStringLiteral("*.fts")));
    EXPECT_TRUE(joined.contains(QStringLiteral("*.jpe")));
    EXPECT_TRUE(joined.contains(QStringLiteral("*.cr2")));
    EXPECT_TRUE(joined.contains(QStringLiteral("*.dcr")));
    EXPECT_TRUE(joined.contains(QStringLiteral("*.raw")));
    EXPECT_TRUE(joined.contains(QStringLiteral("*.x3f")));
    EXPECT_TRUE(filters.constFirst().contains(QStringLiteral("All Supported Images")));
    EXPECT_FALSE(std::any_of(filters.cbegin(), filters.cend(),
                             [](const QString& filter) { return filter.contains(QStringLiteral("(*)")); }));
}

TEST(FileDialogUtils, SupportedImageOpenPathsExcludeUnsupportedFiles)
{
    const QStringList selected = {QStringLiteral("registered_001.fit"), QStringLiteral("registration.db"),
                                  QStringLiteral("registered_002.XISF"), QStringLiteral("notes.txt")};

    const QStringList supported = an::ui::dialogs::filterSupportedImageOpenPaths(selected);

    EXPECT_EQ(supported, QStringList({QStringLiteral("registered_001.fit"), QStringLiteral("registered_002.XISF")}));
}

TEST(FileDialogUtils, SupportedImageOpenPathsKeepSequencesOnlyWhenEnabled)
{
    const QStringList selected = {QStringLiteral("registered_001.fit"), QStringLiteral("capture.SER"),
                                  QStringLiteral("capture.mp4"), QStringLiteral("registration.db")};

    EXPECT_EQ(an::ui::dialogs::filterSupportedImageOpenPaths(selected),
              QStringList({QStringLiteral("registered_001.fit")}));
    EXPECT_EQ(an::ui::dialogs::filterSupportedImageOpenPaths(selected, true),
              QStringList({QStringLiteral("registered_001.fit"), QStringLiteral("capture.SER"),
                           QStringLiteral("capture.mp4")}));
}

TEST(FileDialogUtils, ApplySupportedImageOpenFiltersUsesRestrictedCompactDefault)
{
    QFileDialog dialog;

    an::ui::dialogs::applySupportedImageOpenFilters(dialog);

    EXPECT_TRUE(dialog.testOption(QFileDialog::HideNameFilterDetails));
    EXPECT_EQ(dialog.nameFilters(), an::ui::dialogs::supportedImageOpenNameFilters());
    EXPECT_EQ(dialog.selectedNameFilter(), dialog.nameFilters().constFirst());
    EXPECT_TRUE(dialog.selectedNameFilter().contains(QStringLiteral("*.fit")));
    EXPECT_FALSE(dialog.selectedNameFilter().contains(QStringLiteral("(*)")));
}

TEST(FileDialogUtils, SupportedFormatsFilterRestrictsTheDefaultSelection)
{
    const QStringList patterns = {QStringLiteral("*.fit"), QStringLiteral("*.mp4")};

    const QString filter = an::ui::dialogs::supportedFormatsNameFilter(patterns);

    EXPECT_TRUE(filter.contains(QStringLiteral("*.fit")));
    EXPECT_TRUE(filter.contains(QStringLiteral("*.mp4")));
    EXPECT_FALSE(filter.contains(QStringLiteral("(*)")));
}

TEST(FileDialogUtils, SupportedSequenceSuffixesAreClassified)
{
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("ser")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("ANSEQ")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("mp4")));
    EXPECT_TRUE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("mkv")));

    EXPECT_FALSE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("fit")));
    EXPECT_FALSE(an::ui::dialogs::detail::isSupportedSequenceSuffix(QStringLiteral("jpg")));

    const QStringList patterns = an::ui::dialogs::supportedSequenceOpenNameFilterPatterns();
    EXPECT_TRUE(patterns.contains(QStringLiteral("*.mpg")));
    EXPECT_TRUE(patterns.contains(QStringLiteral("*.mpeg")));
    EXPECT_FALSE(patterns.contains(QStringLiteral("*.txt")));
}
