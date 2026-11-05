// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The file and image coordinators' helpers, moved here from the process framework's component suite with the
/// coordinators they test: the recent list, the open progress text and prompt parent, the image operations' guards, the
/// temporary root and the stale-scratch rules, the action-state guards and the persistence coordinator's two helpers;
/// and a file on another machine left out of the recent files and the last folder remembered, in every spelling.

#include "Test_ControlsFiles/FilesTestContext.h"

#include "app/ActionStateCoordinator.h"
#include "app/FileManager.h"
#include "app/FilePolicies.h"
#include "app/FileWorkflowRunner.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/PersistenceCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QDir>
#include <QFile>
#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <string>

namespace
{

/// The recent files, one to a line, for a failure's message.
[[nodiscard]] std::string listed(const std::deque<std::string>& files)
{
    std::string text;
    for (const std::string& file : files)
    {
        text += file;
        text += '\n';
    }
    return text;
}

TEST(FileManagerComponent, FileManager_RejectsInvalidFile)
{
    EXPECT_FALSE(an::ui::isLikelySupportedImagePath(QStringLiteral("notes.txt")));
    EXPECT_TRUE(an::ui::isLikelySupportedImagePath(QStringLiteral("image.fits")));
}

TEST(FileManagerComponent, FileManager_MaintainsRecentFilesList)
{
    std::deque<std::string> recentFiles = {"older.fit", "old.fit"};

    const bool changed = an::ui::updateRecentFileList(recentFiles, "new.fit", 2);

    EXPECT_TRUE(changed);
    ASSERT_EQ(recentFiles.size(), 2u);
    EXPECT_EQ(recentFiles.front(), "new.fit");
    EXPECT_EQ(recentFiles.back(), "older.fit");
}

// A file on another machine is left out of the recent files in every spelling it reaches the list in - the backslashes
// Windows writes, the slashes normalizing turns them into, a file URL naming a host - so the list never holds one.
TEST(FileManagerComponent, FileManager_LeavesANetworkFileOutOfTheRecentFiles)
{
    an::tests::files::FileManagerFixture files;
    files.manager().addRecentFile("\\\\server\\share\\lights\\frame.fits", nullptr);
    files.manager().addRecentFile("//server/share/lights/frame.fits", nullptr);
    files.manager().addRecentFile("file://server/share/lights/frame.fits", nullptr);
    files.manager().addRecentFile("\\\\?\\UNC\\server\\share\\lights\\frame.fits", nullptr);
    EXPECT_TRUE(files.recentFiles.empty()) << listed(files.recentFiles);

    const QString local = files.path(QStringLiteral("local.fits"));
    files.manager().addRecentFile(local.toStdString(), nullptr);
    ASSERT_EQ(files.recentFiles.size(), 1u);
    EXPECT_EQ(QString::fromStdString(files.recentFiles.front()), QDir::cleanPath(local));
}

TEST(FileWorkflowRunnerComponent, FileWorkflowRunner_FormatsSingleFileProgressText)
{
    const QString message = an::ui::buildOpenProgressDescription("frame.fit", 1, 1);
    EXPECT_EQ(message, QStringLiteral("Opening file 'frame.fit'..."));
}

TEST(FileWorkflowRunnerComponent, FileWorkflowRunner_FormatsBulkProgressText)
{
    const QString message = an::ui::buildOpenProgressDescription("frame.fit", 3, 8);
    EXPECT_EQ(message, QStringLiteral("Opening file 3/8: 'frame.fit'..."));
}

TEST(FileWorkflowRunnerComponent, FileWorkflowRunner_PrefersVisibleProgressDialogForBlockingPrompts)
{
    QWidget fallbackParent;
    QWidget progressDialog;

    EXPECT_EQ(an::ui::resolveOpenPromptParent(&progressDialog, &fallbackParent), &progressDialog);
}

TEST(FileWorkflowRunnerComponent, FileWorkflowRunner_FallsBackToDialogParentWithoutProgressDialog)
{
    QWidget fallbackParent;

    EXPECT_EQ(an::ui::resolveOpenPromptParent(nullptr, &fallbackParent), &fallbackParent);
}

TEST(ImageOperationsCoordinatorComponent, ImageOperationsCoordinator_CloseAllOtherFiles_PreservesActiveControl)
{
    EXPECT_TRUE(an::ui::shouldSkipCloseCandidateWhenClosingOthers(true));
    EXPECT_FALSE(an::ui::shouldSkipCloseCandidateWhenClosingOthers(false));
}

TEST(ImageOperationsCoordinatorComponent, ImageOperationsCoordinator_UndoRedo_GuardsWhenNoActiveControl)
{
    EXPECT_FALSE(an::ui::canRunActiveImageOperation(false));
    EXPECT_TRUE(an::ui::canRunActiveImageOperation(true));
}

TEST(ImageOperationsCoordinatorComponent, ImageOperationsCoordinator_SequenceConversions_ValidateSourceType)
{
    EXPECT_FALSE(an::ui::canRunSequenceOperation(false, false));
    EXPECT_FALSE(an::ui::canRunSequenceOperation(true, false));
    EXPECT_TRUE(an::ui::canRunSequenceOperation(true, true));
}

TEST(ImageOperationsCoordinatorComponent, ImageOperationsCoordinator_ZoomAndInfoActions_DelegateToActiveControl)
{
    EXPECT_FALSE(an::ui::canRunFitAllWindows(false));
    EXPECT_TRUE(an::ui::canRunFitAllWindows(true));
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_TempDir_UsesConfiguredPathWhenAvailable)
{
    EXPECT_EQ(an::ui::resolveTempDirPath(QStringLiteral("/tmp/local"), QStringLiteral("/custom/temp")),
              QStringLiteral("/custom/temp"));
    EXPECT_EQ(an::ui::resolveTempDirPath(QStringLiteral("/tmp/local"), QString()), QStringLiteral("/tmp/local/temp"));
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_EnsureDirsCreated_CreatesTempAndThumbnails)
{
    EXPECT_TRUE(an::ui::shouldCreateRuntimeDirectory(false));
    EXPECT_FALSE(an::ui::shouldCreateRuntimeDirectory(true));
}

// The temporary directory a session reclaims is its own: a session directory's name is the prefix and a token, and the
// lock beside it is not one.
TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_SessionDirectory_IsRecognizedByName)
{
    EXPECT_TRUE(an::ui::isSessionDirectoryName("AN_SESSION_3f9c2a10-7d4b-4e3a-9a1b-0c2d3e4f5a6b"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("AN_SESSION_3f9c2a10-7d4b-4e3a-9a1b-0c2d3e4f5a6b.lock"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("AN_HISTORY_3f9c2a10"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("SEQ_SWAP{3f9c2a10}.ser"));
}

TEST(RuntimeStorageCoordinatorComponent,
     RuntimeStorageCoordinator_CreateTempFilePathForSequenceType_ReturnsExpectedExtension)
{
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Ser), ".ser");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::ImageList), ".anseq");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Avi), ".avi");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Mp4), ".mp4");
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_StaleScratch_RecognizesAfterNightOwnedNames)
{
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("AfterNight_drizztmp"));
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("AfterNight_calibrated_stack_spill_1712_ab12cd34_07"));
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("AfterNightStackInput-aB3xQ9.andump"));
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("afternight-extension-host"));
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("afternight-extension-archive-1712-0x7ffd"));
    EXPECT_TRUE(an::ui::isAfterNightScratchEntryName("afternight-retention-metadata-aB3xQ9.html"));
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_StaleScratch_LeavesForeignEntriesAlone)
{
    // The sweep runs over the shared system temp dir, so anything the application does not
    // own must never match.
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName(""));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("AfterNight"));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("Adobe"));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("claude"));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("my-AfterNight_drizztmp-backup"));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("AfterNightStackInput-aB3xQ9.fits"));
    EXPECT_FALSE(an::ui::isAfterNightScratchEntryName("afternight-extension-hosting"));
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_StaleScratch_KeepsEntriesInsideRetentionWindow)
{
    using namespace std::chrono_literals;

    // Scratch belonging to a concurrently running second instance must survive the sweep.
    EXPECT_FALSE(an::ui::isStaleScratchEntry(std::chrono::seconds(0), 24h));
    EXPECT_FALSE(an::ui::isStaleScratchEntry(23h, 24h));
    EXPECT_TRUE(an::ui::isStaleScratchEntry(24h, 24h));
    EXPECT_TRUE(an::ui::isStaleScratchEntry(72h, 24h));
}

TEST(RuntimeStorageCoordinatorComponent, RuntimeStorageCoordinator_StaleScratch_TreatsNegativeAgeAsFresh)
{
    using namespace std::chrono_literals;

    // Clock skew or a future timestamp must never be read as "very old".
    EXPECT_FALSE(an::ui::isStaleScratchEntry(-48h, 24h));
}

TEST(ActionStateCoordinatorComponent, ActionStateCoordinator_DisablesWhenNoActiveImageData)
{
    EXPECT_TRUE(an::ui::shouldDisableImageActions(false, false));
    EXPECT_TRUE(an::ui::shouldDisableImageActions(true, false));
    EXPECT_FALSE(an::ui::shouldDisableImageActions(true, true));
}

TEST(ActionStateCoordinatorComponent, ActionStateCoordinator_TogglesMaskRenderingState)
{
    EXPECT_TRUE(an::ui::shouldEnableMaskRenderingMenu(true));
    EXPECT_FALSE(an::ui::shouldEnableMaskRenderingMenu(false));
}

TEST(PersistenceCoordinatorDecomposition, SkipsUNCPathsWhenPersistingLastDirectory)
{
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\server\\share"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("/home/user/images"));
}

// The folder last opened is not remembered when it is on another machine, in any spelling a network path reaches the
// store in: a folder Qt hands back is slash-separated, so only the backslash spelling was ever left out. A long-path or
// device prefix to a local drive, a local folder and a relative one are remembered.
TEST(PersistenceCoordinatorDecomposition, SkipsANetworkFolderInEverySpelling)
{
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\server\\share\\lights"));
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("//server/share/lights"));
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory(
        QDir::cleanPath(QStringLiteral("\\\\server\\share\\lights")).toStdString()));
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\?\\UNC\\server\\share\\lights"));
    EXPECT_TRUE(an::ui::shouldSkipPersistLastOpenDirectory("file://server/share/lights"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("C:/images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("C:\\images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("/home/user/images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\?\\C:\\images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("\\\\.\\pipe\\name"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("file:///home/user/images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("file://localhost/home/user/images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory("images"));
    EXPECT_FALSE(an::ui::shouldSkipPersistLastOpenDirectory(""));
}

TEST(PersistenceCoordinatorDecomposition, ResolvesWhiteBalanceModeSafely)
{
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("4")), an::ui::RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(an::ui::rawWhiteBalanceFromStored(QStringLiteral("999")), an::ui::RawWhiteBalanceSetting::Daylight);
}

} // anonymous namespace
