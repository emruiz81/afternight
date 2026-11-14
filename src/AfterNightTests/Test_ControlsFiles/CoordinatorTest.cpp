// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The coordinators the file and image menus run through: the persistence coordinator publishing the RAW policy every
/// later open reads under and building the export intent from the settings database it wrote, and setting a database
/// of another format aside rather than reading it; the runtime storage coordinator's session directory, which it alone
/// reclaims, and its sweep of the directories of sessions that ended; the action state over the image's description and
/// the view's history; the image operations, the rename among them, reaching the view's history through the view; and
/// Show in File Browser, offered while the active view's image is a file and revealing the file its document names.

#include "Test_ControlsFiles/FilesTestContext.h"

#include "app/DemosaicTextMaps.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/PersistenceCoordinator.h"
#include "app/RuntimeStorageCoordinator.h"
#include "controls/FolderTrackerPersistence.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/ImageRenameControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ProcessSettingsPersistence.h"
#include "controls/settings/SettingsKeys.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QLockFile>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <utility>

namespace
{

using an::tests::files::Channels;
using an::tests::files::FilesTestContext;
using an::tests::files::InstalledContext;
using an::tests::files::makeAsset;
using an::tests::files::openFilesContext;
using an::tests::files::pumpQuietly;
using an::tests::files::pumpUntil;

/// A swap file of another program's, left in the temporary folder: an image dump's name and a 40-byte header over a
/// Float32 colour image's samples.
constexpr const char* kForeignDumpName = "IMG_SWAP{6f1c2a7e-3b9d-4e85-a1f2-0c9d7b3e5a41}.dmp";

/// The bytes of that file.
QByteArray foreignDumpBytes()
{
    QByteArray bytes(40, '\0');
    bytes[1] = 7;
    bytes[2] = 2;
    const auto put = [&bytes](const int offset, const quint64 value) {
        for (int index = 0; index < 8; ++index)
        {
            bytes[offset + index] = static_cast<char>((value >> (8 * index)) & 0xffu);
        }
    };
    put(8, 16u);
    put(16, 12u);
    put(24, 3u);
    put(32, 16u * 12u * 3u * 4u);
    bytes.append(QByteArray(16 * 12 * 3 * 4, '\x3f'));
    return bytes;
}

/// A file's bytes; empty when it cannot be read.
QByteArray bytesOf(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// A settings database as a writer of another format would leave it: the tables this version's database has, the
/// format it states - or none - and the RAW settings a Raw Images page stores.
void makeSettingsDatabase(const QString& path, const std::optional<int> format)
{
    const QString connectionName = QStringLiteral("CoordinatorTestSettings");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        if (format.has_value())
        {
            ASSERT_TRUE(query.exec(QStringLiteral("CREATE TABLE migrations (version INTEGER NOT NULL DEFAULT 0)")));
            ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO migrations (version) VALUES (%1)").arg(*format)));
        }
        ASSERT_TRUE(query.exec(
            QStringLiteral("CREATE TABLE global_settings (setting_key TEXT PRIMARY KEY, setting_value TEXT)")));
        ASSERT_TRUE(query.exec(QStringLiteral("INSERT INTO global_settings VALUES ('%1', '4'), ('%2', '1.500000')")
                                   .arg(QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE),
                                        QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R))));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

/// The format a settings database states, or -1 when it states none.
int settingsDatabaseFormat(const QString& path)
{
    const QString connectionName = QStringLiteral("CoordinatorTestSettingsFormat");
    int format = -1;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT version FROM migrations LIMIT 1")) && query.next())
            {
                format = query.value(0).toInt();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return format;
}

/// Names another directory the application's local data while it lives, and the suite's again after.
class ScopedStorageRoot
{
public:
    explicit ScopedStorageRoot(const QString& root) : m_previous(qgetenv(kVariable))
    {
        qputenv(kVariable, QFile::encodeName(root));
    }

    ~ScopedStorageRoot()
    {
        qputenv(kVariable, m_previous);
    }

    ScopedStorageRoot(const ScopedStorageRoot&) = delete;
    ScopedStorageRoot& operator=(const ScopedStorageRoot&) = delete;

private:
    static constexpr const char* kVariable = "AFTERNIGHT_TEST_STORAGE_ROOT";
    QByteArray m_previous;
};

/// The persistence coordinator wired as the application wires it, over the test storage root.
class PersistenceWiring
{
public:
    PersistenceWiring()
    {
        an::ui::PersistenceCoordinator::Dependencies deps;
        deps.localDataPath = &localDataPath;
        deps.lastOpenDirectory = &lastOpenDirectory;
        deps.dbExecutor = &executor;
        deps.thumbnailPersistence = &thumbnails;
        deps.folderTrackerPersistence = &folders;
        deps.processSettingsPersistence = &processSettings;
        deps.globalSettingsPersistence = &globalSettings;
        coordinator = std::make_unique<an::ui::PersistenceCoordinator>(deps);
    }

    ~PersistenceWiring()
    {
        coordinator.reset();
        thumbnails.reset();
        folders.reset();
        processSettings.reset();
        globalSettings.reset();
        executor.reset();
    }

    QString localDataPath;
    std::string lastOpenDirectory;
    std::unique_ptr<PersistenceDbExecutor> executor;
    std::unique_ptr<ThumbnailPersistence> thumbnails;
    std::unique_ptr<FolderTrackerPersistence> folders;
    std::unique_ptr<ProcessSettingsPersistence> processSettings;
    std::unique_ptr<GlobalSettingsPersistence> globalSettings;
    std::unique_ptr<an::ui::PersistenceCoordinator> coordinator;
};

/// A runtime storage coordinator over a data folder, whose tokens are the ones the case names.
std::unique_ptr<an::ui::RuntimeStorageCoordinator> storageOver(QString* localDataPath, std::vector<std::string> tokens)
{
    an::ui::RuntimeStorageCoordinator::Dependencies deps;
    deps.localDataPath = localDataPath;
    auto queue = std::make_shared<std::vector<std::string>>(std::move(tokens));
    deps.createUuidToken = [queue]() {
        if (queue->empty())
        {
            return std::string("spare");
        }
        std::string token = queue->front();
        queue->erase(queue->begin());
        return token;
    };
    return std::make_unique<an::ui::RuntimeStorageCoordinator>(deps);
}

class CoordinatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openFilesContext(2u);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    void TearDown() override
    {
        m_views.clear();
        m_installed.reset();
        m_context.reset();
    }

    /// A view of the case's own showing a grey image of a representation, registered with the context's manager.
    template <typename SampleT> ImageViewControl* showView(const std::string& name, Channels channels = Channels::Grey)
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<SampleT>(
            m_context->engineSession(), 8u, 6u, channels,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<SampleT>(x + y); });
        EXPECT_TRUE(asset.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
        EXPECT_TRUE(document.hasValue());
        an::tests::viewer::ViewHandle view = an::tests::viewer::makeView(name);
        view->setDocument(std::move(*document));
        view->initializeNewImageHistory("made by the case");
        EXPECT_TRUE(an::tests::viewer::settle(*view));
        m_context->addViewControl(view.get());
        m_views.push_back(std::move(view));
        return m_views.back().get();
    }

    std::unique_ptr<FilesTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    std::vector<an::tests::viewer::ViewHandle> m_views;
};

} // namespace

// The persistence coordinator publishes the policy of the stored RAW settings, and a change publishes another for the
// opens submitted after it: an open holding the first keeps reading under it. The settings are stored, so a coordinator
// made later publishes them again.
TEST_F(CoordinatorTest, ThePersistenceCoordinatorPublishesTheRawPolicyForLaterOpens)
{
    std::shared_ptr<const an::ui::RawOpenPolicy> held;
    {
        PersistenceWiring wiring;
        wiring.coordinator->initializePersistence();
        held = wiring.coordinator->rawOpenPolicy();
        ASSERT_NE(held, nullptr);
        EXPECT_EQ(held->settings().whiteBalance, an::ui::RawWhiteBalanceSetting::Daylight);

        const an::ui::RawOpenSettings custom{.whiteBalance = an::ui::RawWhiteBalanceSetting::Custom,
                                             .customGains = {1.8, 1.0, 1.25},
                                             .autoRotate = false};
        ASSERT_TRUE(wiring.coordinator->setRawOpenSettings(custom).ok());
        const std::shared_ptr<const an::ui::RawOpenPolicy> published = wiring.coordinator->rawOpenPolicy();
        ASSERT_NE(published, nullptr);
        EXPECT_NE(published.get(), held.get());
        EXPECT_EQ(published->settings(), custom);
        // What an open on its way holds is unchanged.
        EXPECT_EQ(held->settings().whiteBalance, an::ui::RawWhiteBalanceSetting::Daylight);
        EXPECT_EQ(wiring.globalSettings->select(QString::fromLatin1(SettingsKeys::RAW_WHITE_BALANCE_MODE)),
                  QStringLiteral("4"));
    }
    PersistenceWiring later;
    later.coordinator->initializePersistence();
    EXPECT_EQ(later.coordinator->rawOpenSettings().whiteBalance, an::ui::RawWhiteBalanceSetting::Custom);
    EXPECT_FALSE(later.coordinator->rawOpenSettings().autoRotate);
    // Restore the stored settings for the cases after this one.
    ASSERT_TRUE(later.coordinator->setRawOpenSettings(an::ui::RawOpenSettings{}).ok());
}

// The output intent reads the export settings when the write is asked for.
TEST_F(CoordinatorTest, ThePersistenceCoordinatorBuildsTheIntentFromTheExportSettingsNow)
{
    PersistenceWiring wiring;
    wiring.coordinator->initializePersistence();
    an::Result<an::consumer::SessionHold> hold = m_context->engineSession().hold();
    ASSERT_TRUE(hold.hasValue());
    const std::optional<an::ImageDescriptor> grey =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::UInt16, 4u, 4u, Channels::Grey);
    ASSERT_TRUE(grey.has_value());
    wiring.globalSettings->upsert(QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION), QStringLiteral("lzw"));
    an::Result<an::OutputIntent> lzw = wiring.coordinator->buildOutputIntent(*grey, an::CodecFormat::Tiff);
    ASSERT_TRUE(lzw.hasValue()) << lzw.status().message();
    EXPECT_EQ(lzw->compression(), an::OutputCompression::Lzw);
    wiring.globalSettings->upsert(QString::fromLatin1(SettingsKeys::EXPORT_TIFF_COMPRESSION), QStringLiteral("none"));
    an::Result<an::OutputIntent> none = wiring.coordinator->buildOutputIntent(*grey, an::CodecFormat::Tiff);
    ASSERT_TRUE(none.hasValue());
    EXPECT_EQ(none->compression(), an::OutputCompression::None);
    EXPECT_FALSE(wiring.coordinator->buildOutputIntent(*grey, an::CodecFormat::Raw).hasValue());
}

// The Settings dialog's values live in afternight_data.db under the application's local data. A coordinator started
// over the database another one wrote and closed opens it as it was left - format 2, the one this version writes -
// publishes the RAW policy of the stored RAW settings, and builds an export's intent from the stored export settings.
TEST_F(CoordinatorTest, ThePersistenceCoordinatorOpensTheSettingsDatabaseItWroteAsItWasLeft)
{
    QTemporaryDir data;
    ASSERT_TRUE(data.isValid());
    const ScopedStorageRoot root(data.path());
    const QString database = QDir(data.path()).filePath(QStringLiteral("afternight_data.db"));
    {
        PersistenceWiring first;
        first.coordinator->initializePersistence();
        const an::ui::RawOpenSettings custom{.whiteBalance = an::ui::RawWhiteBalanceSetting::Custom,
                                             .customGains = {1.5, 0.9, 1.25},
                                             .autoRotate = false};
        ASSERT_TRUE(first.coordinator->setRawOpenSettings(custom).ok());
        for (const auto& [key, value] :
             {std::pair{SettingsKeys::RAW_DEMOSAIC_ALGORITHM, "amaze"},
              std::pair{SettingsKeys::EXPORT_TIFF_COMPRESSION, "lzw"},
              std::pair{SettingsKeys::EXPORT_JPEG_QUALITY, "80"}, std::pair{SettingsKeys::EXPORT_PNG_COMPRESSION, "3"},
              std::pair{SettingsKeys::EXPORT_EMBED_EXIF, "0"}, std::pair{SettingsKeys::EXPORT_EMBED_HISTORY, "1"},
              std::pair{SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT, "fits"},
              std::pair{SettingsKeys::GENERAL_RECENT_FILES_LIMIT, "12"}})
        {
            ASSERT_TRUE(first.globalSettings->upsert(QString::fromLatin1(key), QString::fromLatin1(value)));
        }
    }
    EXPECT_EQ(settingsDatabaseFormat(database), 2);

    PersistenceWiring wiring;
    wiring.coordinator->initializePersistence();
    EXPECT_EQ(QDir(wiring.localDataPath).canonicalPath(), QDir(data.path()).canonicalPath());
    EXPECT_TRUE(
        QDir(data.path())
            .entryList({QStringLiteral("afternight_data.format*"), QStringLiteral("afternight_data.unversioned*")},
                       QDir::Files)
            .isEmpty())
        << "the database this version wrote is not set aside";

    const an::ui::RawOpenSettings raw = wiring.coordinator->rawOpenSettings();
    EXPECT_EQ(raw.whiteBalance, an::ui::RawWhiteBalanceSetting::Custom);
    EXPECT_EQ(raw.customGains, (std::array<double, 3u>{1.5, 0.9, 1.25}));
    EXPECT_FALSE(raw.autoRotate);
    const an::ui::DemosaicChoice demosaic = an::ui::demosaicMethodFromSettingsKey(
        wiring.globalSettings->select(QString::fromLatin1(SettingsKeys::RAW_DEMOSAIC_ALGORITHM)));
    EXPECT_EQ(demosaic.method, an::DemosaicMethod::Amaze);

    const an::ui::ExportSettings exported = wiring.coordinator->exportSettings();
    EXPECT_EQ(exported.tiffCompression, an::OutputCompression::Lzw);
    EXPECT_EQ(exported.jpegQuality, 80u);
    EXPECT_EQ(exported.pngCompressionLevel, 3u);
    EXPECT_FALSE(exported.embedExif);
    EXPECT_TRUE(exported.embedHistory);
    an::Result<an::consumer::SessionHold> hold = m_context->engineSession().hold();
    ASSERT_TRUE(hold.hasValue());
    const std::optional<an::ImageDescriptor> grey =
        an::tests::viewer::describeImage(hold->engine(), an::SampleType::UInt16, 4u, 4u, Channels::Grey);
    ASSERT_TRUE(grey.has_value());
    an::Result<an::OutputIntent> intent = wiring.coordinator->buildOutputIntent(*grey, an::CodecFormat::Tiff);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    EXPECT_EQ(intent->compression(), an::OutputCompression::Lzw);

    EXPECT_EQ(wiring.globalSettings->select(QString::fromLatin1(SettingsKeys::GENERAL_DEFAULT_SAVE_FORMAT)),
              QStringLiteral("fits"));
    EXPECT_EQ(wiring.globalSettings->select(QString::fromLatin1(SettingsKeys::GENERAL_RECENT_FILES_LIMIT)),
              QStringLiteral("12"));
}

// A settings database of another format - an older one, a newer one, or one that states none - is never read, written
// or deleted: the coordinator sets it aside beside itself, under a name that states its format, and makes a new one at
// format 2 in its place, so the settings start from their defaults and the one set aside keeps every byte.
TEST_F(CoordinatorTest, ASettingsDatabaseOfAnotherFormatIsSetAsideAndANewOneMadeInItsPlace)
{
    for (const std::optional<int> format : {std::optional<int>(1), std::optional<int>(3), std::optional<int>()})
    {
        QTemporaryDir data;
        ASSERT_TRUE(data.isValid());
        const ScopedStorageRoot root(data.path());
        const QString database = QDir(data.path()).filePath(QStringLiteral("afternight_data.db"));
        makeSettingsDatabase(database, format);
        const QByteArray original = bytesOf(database);
        const QString setAside =
            QDir(data.path())
                .filePath(format.has_value() ? QStringLiteral("afternight_data.format%1.db").arg(*format)
                                             : QStringLiteral("afternight_data.unversioned.db"));

        {
            PersistenceWiring wiring;
            wiring.coordinator->initializePersistence();
            EXPECT_EQ(wiring.coordinator->rawOpenSettings().whiteBalance, an::ui::RawWhiteBalanceSetting::Daylight)
                << "the settings the database of another format holds are not read";
            EXPECT_FALSE(
                wiring.globalSettings->selectOptional(QString::fromLatin1(SettingsKeys::RAW_CUSTOM_WB_R)).has_value());
        }

        EXPECT_TRUE(QFileInfo::exists(setAside)) << setAside.toStdString();
        EXPECT_EQ(bytesOf(setAside), original) << "the database set aside keeps every byte";
        EXPECT_EQ(settingsDatabaseFormat(database), 2);
    }
}

// Each session writes its temporary files into a directory of its own, which it locks while it runs; a temporary
// sequence is named there with its container's extension.
TEST_F(CoordinatorTest, EachSessionHasATemporaryDirectoryOfItsOwn)
{
    QTemporaryDir data;
    QString localData = data.path();
    auto storage = storageOver(&localData, {"alpha", "seq-1"});
    storage->ensureTempDirCreated();
    const QString root = storage->getTempRootPath();
    EXPECT_EQ(root, localData + QStringLiteral("/temp"));
    EXPECT_EQ(storage->getTempDirPath(), root + QStringLiteral("/AN_SESSION_alpha"));
    EXPECT_TRUE(QFileInfo(storage->getTempDirPath()).isDir());
    EXPECT_TRUE(QFileInfo::exists(root + QStringLiteral("/AN_SESSION_alpha.lock")));
    const QString sequence = QString::fromStdString(storage->createTempFilePathForNewSequence(an::SequenceFormat::Ser));
    EXPECT_EQ(sequence, storage->getTempDirPath() + QStringLiteral("/SEQ_SWAPseq-1.ser"));
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::ImageList), ".anseq");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Avi), ".avi");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Mp4), ".mp4");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Video), ".mkv");
    EXPECT_EQ(an::ui::tempSequenceExtensionForFormat(an::SequenceFormat::Fits), "");
}

// The startup sweep reclaims the directory of a session that ended and nothing else: a running session's directory -
// its histories' snapshots among it - a swap file another program left, which is never read or removed, and a user's
// own file in the temporary folder all stay.
TEST_F(CoordinatorTest, TheStartupSweepReclaimsOnlyEndedSessions)
{
    QTemporaryDir data;
    QString localData = data.path();
    auto running = storageOver(&localData, {"running"});
    running->ensureTempDirCreated();
    const QString root = running->getTempRootPath();
    const QString runningHistory = running->getTempDirPath() + QStringLiteral("/AN_HISTORY_live");
    ASSERT_TRUE(QDir().mkpath(runningHistory));
    {
        QFile snapshot(runningHistory + QStringLiteral("/step-1.ansnap"));
        ASSERT_TRUE(snapshot.open(QIODevice::WriteOnly));
    }

    const QString ended = root + QStringLiteral("/AN_SESSION_ended");
    ASSERT_TRUE(QDir().mkpath(ended + QStringLiteral("/AN_HISTORY_old")));
    {
        QFile left(ended + QStringLiteral("/SEQ_SWAPx.ser"));
        ASSERT_TRUE(left.open(QIODevice::WriteOnly));
    }

    const QByteArray dumpBytes = foreignDumpBytes();
    const QString foreign = root + QStringLiteral("/") + QString::fromLatin1(kForeignDumpName);
    {
        QFile dump(foreign);
        ASSERT_TRUE(dump.open(QIODevice::WriteOnly));
        ASSERT_EQ(dump.write(dumpBytes), dumpBytes.size());
    }
    const QString user = root + QStringLiteral("/my-image.fits");
    {
        QFile mine(user);
        ASSERT_TRUE(mine.open(QIODevice::WriteOnly));
        mine.write("SIMPLE");
    }

    auto starting = storageOver(&localData, {"starting"});
    starting->ensureTempDirCreated();
    starting->cleanTemporaryFilesAsync();
    QThreadPool::globalInstance()->waitForDone();

    EXPECT_FALSE(QFileInfo::exists(ended));
    EXPECT_FALSE(QFileInfo::exists(ended + QStringLiteral(".lock")));
    EXPECT_TRUE(QFileInfo::exists(runningHistory + QStringLiteral("/step-1.ansnap")));
    EXPECT_TRUE(QFileInfo(starting->getTempDirPath()).isDir());
    EXPECT_EQ(bytesOf(foreign), dumpBytes);
    EXPECT_TRUE(QFileInfo::exists(user));
}

// Ending a session removes its own directory, everything in it, and its lock, and nothing of another session's.
TEST_F(CoordinatorTest, EndingASessionRemovesOnlyItsOwnDirectory)
{
    QTemporaryDir data;
    QString localData = data.path();
    auto first = storageOver(&localData, {"first"});
    auto second = storageOver(&localData, {"second"});
    first->ensureTempDirCreated();
    second->ensureTempDirCreated();
    // A user's own file in the temporary folder, named to come first in any listing of it.
    const QString userFile = first->getTempRootPath() + QStringLiteral("/0-user-notes.txt");
    {
        QFile notes(userFile);
        ASSERT_TRUE(notes.open(QIODevice::WriteOnly));
        notes.write("mine");
    }
    {
        QFile file(first->getTempDirPath() + QStringLiteral("/scratch.bin"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    }
    {
        QFile file(second->getTempDirPath() + QStringLiteral("/scratch.bin"));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    }
    first->cleanTemporaryFiles();
    EXPECT_FALSE(QFileInfo::exists(first->getTempDirPath()));
    EXPECT_FALSE(QFileInfo::exists(first->getTempRootPath() + QStringLiteral("/AN_SESSION_first.lock")));
    EXPECT_TRUE(QFileInfo::exists(second->getTempDirPath() + QStringLiteral("/scratch.bin")));
    EXPECT_TRUE(QFileInfo::exists(userFile));

    // A session that ran without the clean-on-exit preference leaves its directory unlocked when it goes, and the next
    // startup sweep reclaims it.
    second.reset();
    auto third = storageOver(&localData, {"third"});
    third->ensureTempDirCreated();
    third->cleanTemporaryFilesAsync();
    QThreadPool::globalInstance()->waitForDone();
    EXPECT_FALSE(QFileInfo::exists(localData + QStringLiteral("/temp/AN_SESSION_second")));
    EXPECT_TRUE(an::ui::isSessionDirectoryName("AN_SESSION_third"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("AN_SESSION_third.lock"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("AN_SESSION_"));
    EXPECT_FALSE(an::ui::isSessionDirectoryName("IMG_SWAP{x}.dmp"));
}

// The image menu reaches the view's history through the view, opens the export dialog through what the application
// wires, and says a FITS sequence is not available.
TEST_F(CoordinatorTest, TheImageOperationsReachTheViewsHistoryThroughTheView)
{
    ImageViewControl* view = showView<float>("operated");
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    ASSERT_TRUE(history->registerShownState("Brighten", "made brighter"));
    const auto windows = [view]() {
        int count = 0;
        for (QWidget* child : view->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            count += child->isWindow() && child->isVisible() ? 1 : 0;
        }
        return count;
    };
    const int before = windows();
    std::vector<ImageViewControl*> exported;
    QStringList errors;
    an::ui::ImageOperationsCoordinator::Dependencies deps;
    deps.activeViewControl = [view]() { return view; };
    deps.listViewControls = [view]() { return std::vector<ImageViewControl*>{view}; };
    deps.notifyErrorMessage = [&errors](const QString& title, const QString&) { errors.push_back(title); };
    deps.exportSequence = [&exported](ImageViewControl* control) { exported.push_back(control); };
    an::ui::ImageOperationsCoordinator coordinator(deps);

    coordinator.imageHistory();
    EXPECT_EQ(windows(), before + 1) << "the history opens its window over the view";
    coordinator.undo();
    ASSERT_TRUE(pumpUntil([history]() { return !history->hasUndo() && history->nextStepName() == "Brighten"; }))
        << "undo reaches the view's history";
    coordinator.redo();
    ASSERT_TRUE(pumpUntil([history]() { return history->previousStepName() == "Brighten"; }))
        << "redo reaches the view's history";

    // A sequence operation of a still image says so.
    coordinator.exportSeq();
    EXPECT_TRUE(exported.empty());
    EXPECT_EQ(errors.size(), 1);
    coordinator.convertToFITSSeq();
    EXPECT_EQ(errors.size(), 2);
}

// Renaming a view records the rename in its history and refuses a name another view has.
TEST_F(CoordinatorTest, ARenameIsRecordedInTheViewsHistory)
{
    ImageViewControl* view = showView<float>("before");
    ASSERT_NE(view->viewHistory(), nullptr);
    (void)showView<float>("taken");
    auto* dialog = new ImageRenameControl(view);
    auto* name = dialog->findChild<QLineEdit*>();
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    ASSERT_NE(name, nullptr);
    ASSERT_NE(buttons, nullptr);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    if (ok == nullptr)
    {
        ok = buttons->button(QDialogButtonBox::Apply);
    }
    ASSERT_NE(ok, nullptr);

    name->setText(QStringLiteral("taken"));
    ok->click();
    EXPECT_EQ(view->getUniqueName(), "before");
    EXPECT_FALSE(view->viewHistory()->hasUndo()) << "a refused rename records nothing";
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("warning:Image Rename")));

    name->setText(QStringLiteral("after"));
    ok->click();
    EXPECT_EQ(view->getUniqueName(), "after");
    EXPECT_EQ(view->viewHistory()->previousStepName(), "Rename");
    pumpQuietly();
}

// The action hands the file the active view's document names to the reveal function the application wires - a
// sequence's file for a sequence - and reveals nothing, saying nothing, for an image that has never been a file.
TEST_F(CoordinatorTest, ShowInFileBrowserRevealsTheFileTheActiveViewsDocumentNames)
{
    QTemporaryDir files;
    ASSERT_TRUE(files.isValid());
    const auto touch = [&files](const QString& name) {
        const QString path = files.filePath(name);
        QFile file(path);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("x");
        return path;
    };
    const QString imagePath = touch(QStringLiteral("read.fits"));
    const QString sequencePath = touch(QStringLiteral("frames.ser"));

    ImageViewControl* image = showView<float>("read");
    an::ui::DocumentNames names = image->document().names();
    names.sourcePath = imagePath;
    image->document().setNames(std::move(names));

    ImageViewControl* sequence = showView<float>("frames");
    an::ui::DocumentSequence frames;
    frames.sourcePath = sequencePath;
    sequence->document().setSequence(frames);

    ImageViewControl* made = showView<float>("made");

    ImageViewControl* active = image;
    QStringList revealed;
    QStringList errors;
    an::ui::ImageOperationsCoordinator::Dependencies deps;
    deps.activeViewControl = [&active]() { return active; };
    deps.listViewControls = [&active]() { return std::vector<ImageViewControl*>{active}; };
    deps.notifyErrorMessage = [&errors](const QString& title, const QString&) { errors.push_back(title); };
    deps.revealInFileBrowser = [&revealed](const QString& path) {
        revealed.push_back(path);
        return true;
    };
    an::ui::ImageOperationsCoordinator coordinator(deps);

    coordinator.showInFileBrowser();
    ASSERT_EQ(revealed.size(), 1);
    EXPECT_EQ(revealed.back(), QFileInfo(imagePath).absoluteFilePath());

    active = sequence;
    coordinator.showInFileBrowser();
    ASSERT_EQ(revealed.size(), 2);
    EXPECT_EQ(revealed.back(), QFileInfo(sequencePath).absoluteFilePath());

    active = made;
    coordinator.showInFileBrowser();
    EXPECT_EQ(revealed.size(), 2) << "an image that has never been a file reveals nothing";
    EXPECT_TRUE(errors.empty());
    pumpQuietly();
}

// A file moved or deleted since it was opened is not handed to the file manager: a warning over the window says the
// file is not on disk - the one the dashboard's recent files give - and the action returns without waiting for it.
TEST_F(CoordinatorTest, ShowInFileBrowserWarnsWhenTheFileIsNoLongerOnDisk)
{
    QTemporaryDir files;
    ASSERT_TRUE(files.isValid());
    const QString gone = files.filePath(QStringLiteral("gone.fits"));
    ImageViewControl* view = showView<float>("gone");
    an::ui::DocumentNames names = view->document().names();
    names.sourcePath = gone;
    view->document().setNames(std::move(names));

    QMainWindow window;
    QStringList revealed;
    an::ui::ImageOperationsCoordinator::Dependencies deps;
    deps.activeViewControl = [view]() { return view; };
    deps.listViewControls = [view]() { return std::vector<ImageViewControl*>{view}; };
    deps.messageParent = [&window]() -> QWidget* { return &window; };
    deps.revealInFileBrowser = [&revealed](const QString& path) {
        revealed.push_back(path);
        return true;
    };
    an::ui::ImageOperationsCoordinator coordinator(deps);

    coordinator.showInFileBrowser();

    EXPECT_TRUE(revealed.empty());
    auto* box = window.findChild<QMessageBox*>();
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->isVisible());
    EXPECT_EQ(box->windowTitle(), QStringLiteral("File Not Found"));
    EXPECT_TRUE(box->text().contains(gone)) << box->text().toStdString();
    box->close();
    pumpQuietly();
}
