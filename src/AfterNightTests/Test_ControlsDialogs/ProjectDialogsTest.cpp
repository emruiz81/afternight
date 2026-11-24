// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a project starts from, as the settings store states it, the Projects page that shows and stores it, and the
/// New Project dialog: the folder is the name under the location; a name no folder takes and a folder that holds
/// files are said and Create project is withdrawn; a folder that is a project already is offered to open; with images
/// open the dialog offers to add them and to move their history; and what is accepted is what was chosen.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "components/AnInfoBanner.h"
#include "components/AnLabeledField.h"
#include "components/AnSegmentedControl.h"
#include "controls/settings/PathPickerWidget.h"
#include "controls/settings/ProjectSettings.h"
#include "controls/settings/ProjectsSettingsPage.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"
#include "dialogs/NewProjectDialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTest>

#include <gtest/gtest.h>

#include <map>

namespace
{

using an::tests::dialogs::SettingsStore;
using an::ui::NewProjectChoice;
using an::ui::NewProjectDialog;
using an::ui::NewProjectFolderState;
using an::ui::NewProjectOptions;
using an::ui::ProjectSettings;
using an::ui::ProjectsSettingsPage;
using an::ui::history::StepCompression;

/// The settings a map states, as a reader of the settings store.
[[nodiscard]] an::ui::SettingReader readerOf(const std::map<QString, QString>& values)
{
    return [values](const QString& key, const QString& fallback) {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    };
}

[[nodiscard]] QString key(const char* name)
{
    return QString::fromLatin1(name);
}

/// A location that is a folder's full path on this host, and never made.
[[nodiscard]] QString someLocation()
{
    return QDir::cleanPath(QDir::tempPath() + QStringLiteral("/an-projects-never-made"));
}

[[nodiscard]] NewProjectOptions optionsAt(const QString& location)
{
    NewProjectOptions options;
    options.location = location;
    return options;
}

} // namespace

// ---- The stored settings

// With nothing stored the settings are their defaults: the documents' AfterNight Projects, a project for every
// integration job, LZ4, a history asked to be compacted above ten gigabytes, and no project opened at the start.
TEST(ProjectSettings, NothingStoredReadsAsTheDefaults)
{
    const ProjectSettings defaults = an::ui::readProjectSettings(readerOf({}));
    EXPECT_EQ(defaults.defaultLocation, an::ui::defaultProjectsLocation());
    EXPECT_TRUE(defaults.defaultLocation.endsWith(QStringLiteral("/AfterNight Projects")))
        << defaults.defaultLocation.toStdString();
    EXPECT_TRUE(QDir::isAbsolutePath(defaults.defaultLocation));
    EXPECT_TRUE(defaults.createForIntegrationJobs);
    EXPECT_EQ(defaults.historyCompression, StepCompression::Lz4);
    EXPECT_EQ(defaults.compactAskThresholdGb, 10);
    EXPECT_FALSE(defaults.openLastOnStartup);
    EXPECT_EQ(an::ui::readProjectSettings({}), defaults) << "no store reads as the defaults";

    // The defaults the keys are documented with.
    EXPECT_TRUE(SettingsDefaults::PROJECTS_CREATE_FOR_INTEGRATION_JOBS);
    EXPECT_FALSE(SettingsDefaults::PROJECTS_OPEN_LAST_ON_STARTUP);
    EXPECT_STREQ(SettingsDefaults::HISTORY_COMPRESSION, "lz4");
    EXPECT_EQ(SettingsDefaults::PROJECTS_COMPACT_ASK_THRESHOLD_GB, 10);
    EXPECT_STREQ(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB, "projects/compactAskThresholdGb");
    EXPECT_STREQ(SettingsDefaults::PROJECTS_RECENT, "");
    EXPECT_STREQ(SettingsDefaults::PROJECTS_LAST, "");
    EXPECT_STREQ(SettingsKeys::PROJECTS_DEFAULT_LOCATION, "projects/defaultLocation");
    EXPECT_STREQ(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS, "projects/createForIntegrationJobs");
    EXPECT_STREQ(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP, "projects/openLastOnStartup");
    EXPECT_STREQ(SettingsKeys::PROJECTS_RECENT, "projects/recent");
    EXPECT_STREQ(SettingsKeys::PROJECTS_LAST, "projects/last");
    EXPECT_STREQ(SettingsKeys::HISTORY_COMPRESSION, "history/compression");
}

// What is stored is read, what is written reads back, and a value that is none of a setting's reads as its default.
// The location is stored in the host's own separators, as its folder chooser gives it: a backslash separates only on
// Windows, and elsewhere is a character of a file's name.
TEST(ProjectSettings, StoredValuesAreReadAndWrittenBack)
{
    const ProjectSettings stored = an::ui::readProjectSettings(readerOf(
        {{key(SettingsKeys::PROJECTS_DEFAULT_LOCATION), QDir::toNativeSeparators(QStringLiteral("D:/Astro/Projects/"))},
         {key(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS), QStringLiteral("0")},
         {key(SettingsKeys::HISTORY_COMPRESSION), QStringLiteral(" ZSTD ")},
         {key(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QStringLiteral(" 25 ")},
         {key(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("1")}}));
    EXPECT_EQ(stored.defaultLocation, QStringLiteral("D:/Astro/Projects"));
    EXPECT_FALSE(stored.createForIntegrationJobs);
    EXPECT_EQ(stored.historyCompression, StepCompression::Zstd);
    EXPECT_EQ(stored.compactAskThresholdGb, 25);
    EXPECT_TRUE(stored.openLastOnStartup);

    std::map<QString, QString> written;
    an::ui::writeProjectSettings(stored,
                                 [&written](const QString& name, const QString& value) { written[name] = value; });
    EXPECT_EQ(written.size(), 5u);
    EXPECT_EQ(written[key(SettingsKeys::HISTORY_COMPRESSION)], QStringLiteral("zstd"));
    EXPECT_EQ(written[key(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB)], QStringLiteral("25"));
    EXPECT_EQ(an::ui::readProjectSettings(readerOf(written)), stored);

    const ProjectSettings broken = an::ui::readProjectSettings(
        readerOf({{key(SettingsKeys::PROJECTS_DEFAULT_LOCATION), QStringLiteral("   ")},
                  {key(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS), QStringLiteral("maybe")},
                  {key(SettingsKeys::HISTORY_COMPRESSION), QStringLiteral("gzip")},
                  {key(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QStringLiteral("a lot")},
                  {key(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), QStringLiteral("yes")}}));
    EXPECT_EQ(broken, an::ui::readProjectSettings(readerOf({})));
    // A threshold is a whole number of gigabytes, at least one.
    for (const char* none : {"0", "-3", "2.5", ""})
    {
        EXPECT_EQ(an::ui::readProjectSettings(
                      readerOf({{key(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB), QString::fromLatin1(none)}}))
                      .compactAskThresholdGb,
                  10)
            << '"' << none << '"';
    }
}

// A compression is stated by the name the history's codec gives it.
TEST(ProjectSettings, ACompressionIsStatedByItsName)
{
    for (const StepCompression compression : {StepCompression::None, StepCompression::Lz4, StepCompression::Zstd})
    {
        const std::string_view name = an::ui::history::stepCompressionName(compression);
        const std::optional<StepCompression> read =
            an::ui::stepCompressionFromName(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())));
        ASSERT_TRUE(read.has_value()) << name;
        EXPECT_EQ(*read, compression);
    }
    EXPECT_FALSE(an::ui::stepCompressionFromName(QStringLiteral("gzip")).has_value());
    EXPECT_FALSE(an::ui::stepCompressionFromName(QString()).has_value());
}

// A project's name is its folder's, on every system the application runs on.
TEST(ProjectSettings, AProjectsNameIsOneAFolderTakes)
{
    for (const char* name : {"Carina Nebula LRGB", "M 31", "NGC 7000 - 2026.07", "Jupiter (red)", "comet_12P"})
    {
        EXPECT_TRUE(an::ui::isValidProjectName(QString::fromLatin1(name))) << name;
    }
    for (const char* name :
         {"",    " ",    " lead", "trail ", "dot.", ".",   "..",  "a/b",  "a\\b",     "a:b",      "a*b",
          "a?b", "a\"b", "a<b",   "a>b",    "a|b",  "CON", "nul", "COM1", "lpt9.txt", "tab\tname"})
    {
        EXPECT_FALSE(an::ui::isValidProjectName(QString::fromLatin1(name))) << '"' << name << '"';
    }
    EXPECT_TRUE(an::ui::isValidProjectName(QStringLiteral("COM10")));
    EXPECT_TRUE(an::ui::isValidProjectName(QStringLiteral("Console")));
    EXPECT_TRUE(an::ui::isValidProjectName(QString(128, QLatin1Char('a'))));
    EXPECT_FALSE(an::ui::isValidProjectName(QString(129, QLatin1Char('a'))));
}

// ---- The Projects page

// The page states the settings: over a store that holds none, their defaults.
TEST(ProjectsSettingsPage, ItStatesTheSettingsAndTheirDefaults)
{
    const SettingsStore store;
    ProjectsSettingsPage page(store.persistence());
    page.loadSettings();

    EXPECT_EQ(page.pageTitle(), QStringLiteral("Projects"));
    EXPECT_EQ(ProjectsSettingsPage::title(), page.pageTitle());
    EXPECT_FALSE(page.hasUnsavedChanges());
    EXPECT_EQ(QDir::fromNativeSeparators(page.locationPicker()->path()), an::ui::defaultProjectsLocation());
    EXPECT_TRUE(page.createForIntegrationJobsCheck()->isChecked());
    EXPECT_EQ(page.createForIntegrationJobsCheck()->text(),
              QStringLiteral("Create a project for every integration job"));
    EXPECT_EQ(page.compressionCombo()->count(), 3);
    EXPECT_EQ(page.compressionCombo()->itemText(0), QStringLiteral("LZ4 (fast)"));
    EXPECT_EQ(page.compressionCombo()->itemText(1), QStringLiteral("None"));
    EXPECT_EQ(page.compressionCombo()->itemText(2), QStringLiteral("Zstandard (smallest)"));
    EXPECT_EQ(page.compressionCombo()->currentData().toString(), QStringLiteral("lz4"));
    // "Ask to compact a history above 10 GB".
    EXPECT_EQ(page.compactAskThresholdSpin()->value(), 10);
    EXPECT_EQ(page.compactAskThresholdSpin()->suffix(), QStringLiteral(" GB"));
    EXPECT_EQ(page.compactAskThresholdSpin()->minimum(), 1);
    EXPECT_FALSE(page.openLastOnStartupCheck()->isChecked());
    EXPECT_EQ(page.openLastOnStartupCheck()->text(), QStringLiteral("Open the last project on startup"));
    EXPECT_EQ(page.shownSettings(), an::ui::readProjectSettings({}));
    EXPECT_TRUE(page.validateSettings().isValid);

    // Showing them stores nothing: the keys stay absent until the page is saved.
    EXPECT_FALSE(store.stored(SettingsKeys::PROJECTS_DEFAULT_LOCATION).has_value());
    EXPECT_FALSE(store.stored(SettingsKeys::HISTORY_COMPRESSION).has_value());

    // With no store the page shows the defaults and saving does nothing.
    ProjectsSettingsPage bare(nullptr);
    bare.loadSettings();
    EXPECT_EQ(bare.shownSettings(), an::ui::readProjectSettings({}));
    bare.saveSettings();
}

// The compression's tooltip says what the setting reaches: a history kept outside a project, and the compression a new
// project starts from - not a project's own history, which keeps the compression the project was made with.
TEST(ProjectsSettingsPage, TheCompressionsTooltipSaysAProjectKeepsItsOwn)
{
    const SettingsStore store;
    ProjectsSettingsPage page(store.persistence());
    page.loadSettings();
    const QString tip = page.compressionCombo()->toolTip();
    EXPECT_FALSE(tip.contains(QStringLiteral("in a project or not"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("outside a project"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("project keeps"))) << tip.toStdString();
    EXPECT_TRUE(tip.contains(QStringLiteral("new project starts from it"))) << tip.toStdString();
}

// What the page shows is what it stores, under the keys the application reads, and another page reads it back.
TEST(ProjectsSettingsPage, SavingStoresWhatItShows)
{
    const SettingsStore store;
    const QString location = someLocation();
    {
        ProjectsSettingsPage page(store.persistence());
        page.loadSettings();
        page.locationPicker()->setPath(QDir::toNativeSeparators(location));
        page.createForIntegrationJobsCheck()->setChecked(false);
        page.compressionCombo()->setCurrentIndex(2);
        EXPECT_FALSE(page.compactAskThresholdSpin()->signalsBlocked());
        page.compactAskThresholdSpin()->setValue(40);
        page.openLastOnStartupCheck()->setChecked(true);
        EXPECT_TRUE(page.hasUnsavedChanges());
        ASSERT_TRUE(page.validateSettings().isValid);
        page.saveSettings();
        EXPECT_FALSE(page.hasUnsavedChanges());
    }
    EXPECT_EQ(store.stored(SettingsKeys::PROJECTS_DEFAULT_LOCATION), std::optional<QString>(location));
    EXPECT_EQ(store.stored(SettingsKeys::PROJECTS_CREATE_FOR_INTEGRATION_JOBS),
              std::optional<QString>(QStringLiteral("0")));
    EXPECT_EQ(store.stored(SettingsKeys::HISTORY_COMPRESSION), std::optional<QString>(QStringLiteral("zstd")));
    EXPECT_EQ(store.stored(SettingsKeys::PROJECTS_COMPACT_ASK_THRESHOLD_GB),
              std::optional<QString>(QStringLiteral("40")));
    EXPECT_EQ(store.stored(SettingsKeys::PROJECTS_OPEN_LAST_ON_STARTUP), std::optional<QString>(QStringLiteral("1")));

    ProjectsSettingsPage again(store.persistence());
    again.loadSettings();
    const ProjectSettings shown = again.shownSettings();
    EXPECT_EQ(shown.defaultLocation, location);
    EXPECT_FALSE(shown.createForIntegrationJobs);
    EXPECT_EQ(shown.historyCompression, StepCompression::Zstd);
    EXPECT_EQ(shown.compactAskThresholdGb, 40);
    EXPECT_TRUE(shown.openLastOnStartup);

    // Reset Page shows the defaults again, to be saved or discarded.
    again.resetToDefaults();
    EXPECT_EQ(again.shownSettings(), an::ui::readProjectSettings({}));
    EXPECT_TRUE(again.hasUnsavedChanges());
    EXPECT_EQ(store.stored(SettingsKeys::HISTORY_COMPRESSION), std::optional<QString>(QStringLiteral("zstd")));
}

// A location that is no folder's full path is refused when the dialog applies, naming the field.
TEST(ProjectsSettingsPage, ALocationThatIsNoFoldersPathIsRefused)
{
    ProjectsSettingsPage page(nullptr);
    page.loadSettings();
    for (const QString& location : {QString(), QStringLiteral("  "), QStringLiteral("relative/projects")})
    {
        page.locationPicker()->setPath(location);
        const ValidationResult result = page.validateSettings();
        EXPECT_FALSE(result.isValid) << location.toStdString();
        EXPECT_FALSE(result.errorMessage.isEmpty());
        EXPECT_EQ(result.fieldName, QStringLiteral("defaultLocation"));
    }
    page.locationPicker()->setPath(QDir::toNativeSeparators(someLocation()));
    EXPECT_TRUE(page.validateSettings().isValid);
    // The page names a folder and never makes it.
    EXPECT_FALSE(QFileInfo::exists(someLocation()));
}

// ---- The New Project dialog

// The dialog opens 560 by 600 with the location and the compression it was given, nothing named yet, and nothing to
// add when no image is open.
TEST(NewProjectDialog, ItStartsFromWhatItIsGiven)
{
    NewProjectOptions options = optionsAt(someLocation());
    options.compression = StepCompression::None;
    NewProjectDialog dialog(options);

    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("New Project"));
    EXPECT_EQ(dialog.size(), QSize(NewProjectDialog::kWidth, NewProjectDialog::kHeight));
    EXPECT_EQ(NewProjectDialog::kWidth, 560);
    EXPECT_EQ(NewProjectDialog::kHeight, 600);
    EXPECT_TRUE(dialog.nameField()->text().isEmpty());
    EXPECT_EQ(QDir::fromNativeSeparators(dialog.locationPicker()->path()), someLocation());
    EXPECT_TRUE(dialog.projectFolder().isEmpty());
    EXPECT_TRUE(dialog.objectField()->text().isEmpty());

    ASSERT_EQ(dialog.compressionPicker()->count(), 3);
    EXPECT_EQ(dialog.compressionPicker()->segmentText(0), QStringLiteral("LZ4 (fast)"));
    EXPECT_EQ(dialog.compressionPicker()->segmentText(1), QStringLiteral("None"));
    EXPECT_EQ(dialog.compressionPicker()->segmentText(2), QStringLiteral("Zstandard (smallest)"));
    EXPECT_EQ(dialog.compressionPicker()->currentIndex(), 1) << "the setting's compression is the one chosen";

    EXPECT_TRUE(dialog.addOpenImagesCheck()->isHidden());
    EXPECT_TRUE(dialog.moveHistoryCheck()->isHidden());
    EXPECT_FALSE(dialog.createButton()->isEnabled()) << "a project with no name cannot be made";
    EXPECT_EQ(dialog.createButton()->text(), QStringLiteral("Create project"));
    EXPECT_TRUE(dialog.createButton()->isDefault());
    EXPECT_EQ(dialog.cancelButton()->text(), QStringLiteral("Cancel"));
    EXPECT_TRUE(dialog.problem().isEmpty()) << "nothing is wrong yet: nothing was typed";
    EXPECT_TRUE(dialog.messageBanner()->isHidden());

    // The captions the dialog shows: what the steps are, and that the images stay where they are.
    bool saysXisf = false;
    for (const an::ui::AnLabeledField* field : dialog.findChildren<an::ui::AnLabeledField*>())
    {
        saysXisf =
            saysXisf || field->hint() == QStringLiteral("History steps are XISF images that open in other programs");
    }
    EXPECT_TRUE(saysXisf);
    const auto* const note =
        dynamic_cast<an::ui::AnInfoBanner*>(dialog.findChild<QWidget*>(QStringLiteral("newProjectNote")));
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->message(), QStringLiteral("Images stay where they are. The project keeps their history, the "
                                              "window layouts and the integration data."));
}

// The project's folder is its name under the location, shown under the location as it is typed.
TEST(NewProjectDialog, TheFolderIsTheNameUnderTheLocation)
{
    const QString location = someLocation();
    NewProjectDialog dialog(optionsAt(location));
    dialog.nameField()->setText(QStringLiteral("Carina Nebula LRGB"));

    const QString folder = location + QStringLiteral("/Carina Nebula LRGB");
    EXPECT_EQ(dialog.projectFolder(), folder);
    EXPECT_TRUE(dialog.folderLine()->accessibleName().endsWith(QDir::toNativeSeparators(folder)))
        << dialog.folderLine()->accessibleName().toStdString();
    EXPECT_EQ(dialog.folderLine()->toolTip(), QDir::toNativeSeparators(folder));
    EXPECT_TRUE(dialog.createButton()->isEnabled());

    // Another location moves the folder; a name typed with blanks around it is the name without them.
    dialog.locationPicker()->setPath(QDir::toNativeSeparators(location + QStringLiteral("/2026")));
    dialog.nameField()->setText(QStringLiteral("  M 31 "));
    EXPECT_EQ(dialog.projectFolder(), location + QStringLiteral("/2026/M 31"));

    const NewProjectChoice choice = dialog.choice();
    EXPECT_EQ(choice.name, QStringLiteral("M 31"));
    EXPECT_EQ(choice.location, location + QStringLiteral("/2026"));
    EXPECT_EQ(choice.folder, location + QStringLiteral("/2026/M 31"));
    EXPECT_TRUE(choice.object.isEmpty());
    EXPECT_EQ(choice.compression, StepCompression::Lz4);
    EXPECT_FALSE(choice.addOpenImages);
    EXPECT_FALSE(choice.moveHistory);
    EXPECT_FALSE(choice.openExisting);

    // With no location there is no folder, and nothing to make.
    dialog.locationPicker()->setPath(QString());
    EXPECT_TRUE(dialog.projectFolder().isEmpty());
    EXPECT_FALSE(dialog.createButton()->isEnabled());
}

// A name no folder takes, and a location that is no folder's path, are said over the buttons and Create project is
// withdrawn until they are put right.
TEST(NewProjectDialog, ANameNoFolderTakesIsSaidAndCreateIsWithdrawn)
{
    NewProjectDialog dialog(optionsAt(someLocation()));
    for (const char* name : {"a/b", "what?", "NUL", "ends."})
    {
        dialog.nameField()->setText(QString::fromLatin1(name));
        EXPECT_FALSE(dialog.problem().isEmpty()) << name;
        EXPECT_FALSE(dialog.createButton()->isEnabled()) << name;
        EXPECT_FALSE(dialog.messageBanner()->isHidden()) << name;
        EXPECT_EQ(dialog.messageBanner()->message(), dialog.problem());
        EXPECT_EQ(dialog.messageBanner()->tone(), an::ui::AnInfoBanner::Tone::Warning);
        EXPECT_TRUE(dialog.projectFolder().isEmpty()) << name;
    }
    dialog.nameField()->setText(QStringLiteral("M 42"));
    EXPECT_TRUE(dialog.problem().isEmpty());
    EXPECT_TRUE(dialog.messageBanner()->isHidden());
    EXPECT_TRUE(dialog.createButton()->isEnabled());

    dialog.locationPicker()->setPath(QStringLiteral("relative/projects"));
    EXPECT_FALSE(dialog.problem().isEmpty());
    EXPECT_FALSE(dialog.createButton()->isEnabled());
}

// A folder that holds files and is no project is refused; one that is a project already is offered to open.
TEST(NewProjectDialog, ANonEmptyFolderIsRefusedAndAnExistingProjectIsOfferedToOpen)
{
    const QString location = someLocation();
    NewProjectOptions options = optionsAt(location);
    QStringList asked;
    options.folderState = [&asked, location](const QString& folder) {
        asked.append(folder);
        if (folder == location + QStringLiteral("/Lights"))
        {
            return NewProjectFolderState::Occupied;
        }
        return folder == location + QStringLiteral("/Orion") ? NewProjectFolderState::Project
                                                             : NewProjectFolderState::Free;
    };
    NewProjectDialog dialog(options);

    dialog.nameField()->setText(QStringLiteral("Lights"));
    EXPECT_EQ(asked.last(), location + QStringLiteral("/Lights")) << "the folder asked about is the project's";
    EXPECT_FALSE(dialog.problem().isEmpty());
    EXPECT_TRUE(dialog.problem().contains(QStringLiteral("not a project"))) << dialog.problem().toStdString();
    EXPECT_FALSE(dialog.createButton()->isEnabled());
    EXPECT_FALSE(dialog.offersToOpen());
    EXPECT_EQ(dialog.messageBanner()->tone(), an::ui::AnInfoBanner::Tone::Warning);

    dialog.nameField()->setText(QStringLiteral("Orion"));
    EXPECT_TRUE(dialog.problem().isEmpty()) << "a project to open is no problem";
    EXPECT_TRUE(dialog.offersToOpen());
    EXPECT_TRUE(dialog.createButton()->isEnabled());
    EXPECT_EQ(dialog.createButton()->text(), QStringLiteral("Open project"));
    EXPECT_FALSE(dialog.messageBanner()->isHidden());
    EXPECT_EQ(dialog.messageBanner()->tone(), an::ui::AnInfoBanner::Tone::Info);
    EXPECT_TRUE(dialog.messageBanner()->message().contains(QStringLiteral("a project already")));
    dialog.createButton()->click();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(dialog.choice().openExisting);
    EXPECT_EQ(dialog.choice().folder, location + QStringLiteral("/Orion"));

    dialog.nameField()->setText(QStringLiteral("Free"));
    EXPECT_FALSE(dialog.offersToOpen());
    EXPECT_EQ(dialog.createButton()->text(), QStringLiteral("Create project"));
    EXPECT_TRUE(dialog.createButton()->isEnabled());
    EXPECT_TRUE(dialog.messageBanner()->isHidden());
    EXPECT_FALSE(dialog.choice().openExisting);
}

// With images open the dialog offers to add them, and - when their history's size is stated - to move it; a history
// moves only with its image.
TEST(NewProjectDialog, WithImagesOpenItOffersToAddThemAndToMoveTheirHistory)
{
    NewProjectOptions options = optionsAt(someLocation());
    options.openImages = 4;
    options.openHistoryBytes = qint64{1503238554}; // 1.4 GB
    NewProjectDialog dialog(options);
    dialog.nameField()->setText(QStringLiteral("M 42"));

    EXPECT_FALSE(dialog.addOpenImagesCheck()->isHidden());
    EXPECT_EQ(dialog.addOpenImagesCheck()->text(), QStringLiteral("Add the 4 open images to the project"));
    EXPECT_TRUE(dialog.addOpenImagesCheck()->isChecked());
    EXPECT_FALSE(dialog.moveHistoryCheck()->isHidden());
    // The size as the person's locale writes it: 1.4 GB, or 1,4 GB.
    const QString size = QLocale().formattedDataSize(qint64{1503238554}, 1, QLocale::DataSizeTraditionalFormat);
    EXPECT_TRUE(size.endsWith(QStringLiteral("GB"))) << size.toStdString();
    EXPECT_EQ(dialog.moveHistoryCheck()->text(),
              QStringLiteral("Move their undo history into the project (%1)").arg(size));
    EXPECT_TRUE(dialog.moveHistoryCheck()->isChecked());
    EXPECT_TRUE(dialog.choice().addOpenImages);
    EXPECT_TRUE(dialog.choice().moveHistory);

    dialog.addOpenImagesCheck()->setChecked(false);
    EXPECT_FALSE(dialog.moveHistoryCheck()->isEnabled());
    EXPECT_FALSE(dialog.moveHistoryCheck()->isChecked());
    EXPECT_FALSE(dialog.choice().addOpenImages);
    EXPECT_FALSE(dialog.choice().moveHistory);
    dialog.addOpenImagesCheck()->setChecked(true);
    EXPECT_TRUE(dialog.moveHistoryCheck()->isEnabled());
    dialog.moveHistoryCheck()->setChecked(true);
    EXPECT_TRUE(dialog.choice().moveHistory);

    // With no history's size stated there is nothing to move; one image is said as one.
    NewProjectOptions one = optionsAt(someLocation());
    one.openImages = 1;
    NewProjectDialog single(one);
    EXPECT_EQ(single.addOpenImagesCheck()->text(), QStringLiteral("Add the open image to the project"));
    EXPECT_FALSE(single.addOpenImagesCheck()->isHidden());
    EXPECT_TRUE(single.moveHistoryCheck()->isHidden());
    EXPECT_TRUE(single.choice().addOpenImages);
    EXPECT_FALSE(single.choice().moveHistory);
}

// The object is optional; a suggestion fills the field and its caption says where it comes from.
TEST(NewProjectDialog, TheObjectIsOptionalAndASuggestionSaysWhereItComesFrom)
{
    const auto captionOf = [](const NewProjectDialog& dialog) {
        for (const an::ui::AnLabeledField* field : dialog.findChildren<an::ui::AnLabeledField*>())
        {
            if (field->field() == dialog.objectField())
            {
                return field->hint();
            }
        }
        return QStringLiteral("<no object field>");
    };

    NewProjectDialog plain(optionsAt(someLocation()));
    EXPECT_TRUE(captionOf(plain).isEmpty());
    plain.objectField()->setText(QStringLiteral("  NGC 7000 "));
    EXPECT_EQ(plain.choice().object, QStringLiteral("NGC 7000"));

    NewProjectOptions options = optionsAt(someLocation());
    options.suggestedObject = QStringLiteral("Carina Nebula");
    options.objectCaption =
        QStringLiteral("Suggested from the OBJECT card of 718 frames. Leave empty if it does not apply.");
    NewProjectDialog suggested(options);
    EXPECT_EQ(suggested.objectField()->text(), QStringLiteral("Carina Nebula"));
    EXPECT_EQ(captionOf(suggested), options.objectCaption);
    suggested.objectField()->clear();
    EXPECT_TRUE(suggested.choice().object.isEmpty());
}

// The compression chosen is the choice's.
TEST(NewProjectDialog, TheCompressionChosenIsTheChoices)
{
    NewProjectDialog dialog(optionsAt(someLocation()));
    EXPECT_EQ(dialog.compressionPicker()->currentIndex(), 0);
    EXPECT_EQ(dialog.choice().compression, StepCompression::Lz4);
    dialog.compressionPicker()->setCurrentIndex(1);
    EXPECT_EQ(dialog.choice().compression, StepCompression::None);
    dialog.compressionPicker()->segmentButton(2)->click();
    EXPECT_EQ(dialog.choice().compression, StepCompression::Zstd);
}

// Create project accepts, Enter in the name accepts once the project can be made, and Cancel rejects.
TEST(NewProjectDialog, CreateAcceptsAndCancelRejects)
{
    {
        NewProjectDialog dialog(optionsAt(someLocation()));
        dialog.show();
        // Enter with nothing to make does nothing.
        QTest::keyClick(dialog.nameField(), Qt::Key_Return);
        EXPECT_EQ(dialog.result(), QDialog::Rejected);
        EXPECT_TRUE(dialog.isVisible());
        QTest::keyClicks(dialog.nameField(), QStringLiteral("M 42"));
        QTest::keyClick(dialog.nameField(), Qt::Key_Return);
        EXPECT_EQ(dialog.result(), QDialog::Accepted);
        EXPECT_EQ(dialog.choice().name, QStringLiteral("M 42"));
    }
    {
        NewProjectDialog dialog(optionsAt(someLocation()));
        dialog.nameField()->setText(QStringLiteral("M 42"));
        dialog.cancelButton()->click();
        EXPECT_EQ(dialog.result(), QDialog::Rejected);
    }
    // The dialog makes nothing: the location was never made.
    EXPECT_FALSE(QFileInfo::exists(someLocation()));
}
