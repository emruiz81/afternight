// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The parity scenarios: what a person does to AfterNight, written once and run against every registered shell, each
/// started as the application starts it - its registry, its choice, the style, the shell made over the core and built -
/// and driven as a person drives it (ShellDriver). A scenario names the shell it ran in after its name:
/// `ShellScenario.OpensAnImageAndShowsItActive/classic`.

#include "ClassicDriver.h"
#include "ShellDriver.h"

#include "ClassicShell.h"

#include "Test_ShellClassic/AppTestSupport.h"
#include "Test_ShellClassic/ApplicationDriving.h"
#include "Test_ShellClassic/InventoryRecords.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "app/ApplicationStart.h"
#include "app/ModernizationFeatureFlags.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationSession.h"
#include "core/IShell.h"
#include "core/ShellRegistry.h"

#include "controls/IAppContext.h"
#include "controls/ImageListTableHelpers.h"
#include "controls/ImageRenameControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/NewSequenceControl.h"
#include "controls/SettingsDialog.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/SettingsPage.h"
#include "fixtures/BaselineRecord.h"
#include "session/ImageDocument.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/core/Group.h>

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QColor>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QPalette>
#include <QPointer>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSemaphore>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>

#include <cmath>
#include <deque>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// How a failed comparison prints the values the scenarios compare.
inline void PrintTo(const QString& text, std::ostream* out)
{
    *out << '"' << text.toStdString() << '"';
}

inline void PrintTo(const QStringList& list, std::ostream* out)
{
    *out << "{" << list.join(QStringLiteral(", ")).toStdString() << "}";
}

namespace
{

using an::tests::parity::ClassicDriver;
using an::tests::parity::ProcessLaunch;
using an::tests::parity::ShellDriver;
namespace app = an::tests::app;
namespace ids = an::ui::shell::ids;

constexpr std::string_view kBrightnessContrast = "tone_brightness.actionAdjustBrightness_Contrast";
constexpr std::string_view kCrop = "geometry.actionCrop";

/// The shells the application registers, as it registers them.
an::ui::shell::ShellRegistry registeredShells()
{
    an::ui::shell::ShellRegistry registry;
    EXPECT_TRUE(registry.add(an::ui::shell::classicShellDescriptor()));
    return registry;
}

/// A started application: its session, its core, its registry and the shell its start made, and the driver of that
/// shell.
struct RunningShell
{
    QString id;
    std::unique_ptr<an::ui::ApplicationSession> session;
    std::unique_ptr<an::ui::shell::ApplicationCore> core;
    std::unique_ptr<an::ui::shell::ShellRegistry> registry;
    an::ui::StartedApplication started;
    std::unique_ptr<ShellDriver> driver;
};

/// Every thread of the application's pool held until it is let go: work handed to the pool meanwhile waits in its
/// queue, so what a person does between handing it over and letting the pool go happens before the work starts.
class PoolHold
{
public:
    PoolHold() : m_state(std::make_shared<State>()), m_threads(QThreadPool::globalInstance()->maxThreadCount())
    {
        for (int thread = 0; thread < m_threads; ++thread)
        {
            QThreadPool::globalInstance()->start([state = m_state]() {
                state->started.release();
                state->letGo.acquire();
            });
        }
        m_held = app::processEventsUntil([this]() { return m_state->started.available() >= m_threads; });
    }

    ~PoolHold()
    {
        release();
    }

    PoolHold(const PoolHold&) = delete;
    PoolHold& operator=(const PoolHold&) = delete;
    PoolHold(PoolHold&&) = delete;
    PoolHold& operator=(PoolHold&&) = delete;

    /// Whether every thread of the pool is held.
    [[nodiscard]] bool held() const noexcept
    {
        return m_held;
    }

    /// Lets the pool go; the work queued meanwhile starts.
    void release()
    {
        if (!m_released)
        {
            m_released = true;
            m_state->letGo.release(m_threads);
        }
    }

private:
    struct State
    {
        QSemaphore started;
        QSemaphore letGo;
    };

    std::shared_ptr<State> m_state;
    int m_threads = 0;
    bool m_held = false;
    bool m_released = false;
};

class ShellScenario : public ::testing::TestWithParam<std::string>
{
protected:
    static void SetUpTestSuite()
    {
        s_files = std::make_unique<QTemporaryDir>();
    }

    static void TearDownTestSuite()
    {
        const an::Status shut = stopShell();
        EXPECT_TRUE(shut.ok()) << "the session did not shut down cleanly after the shell went: " << shut.message();
        s_files.reset();
    }

    void SetUp() override
    {
        ++app::g_caseGeneration;
        const QString id = QString::fromStdString(GetParam());
        if (s_running == nullptr || s_running->id != id)
        {
            (void)stopShell();
            startShell(id);
        }
        ASSERT_NE(s_running, nullptr);
        ASSERT_NE(s_running->driver, nullptr) << "no driver for the shell " << GetParam();
    }

    void TearDown() override
    {
        if (s_running != nullptr && s_running->driver != nullptr)
        {
            s_running->driver->reset();
        }
        ++app::g_caseGeneration;
    }

    /// Starts a registered shell as the application starts it: the session and the core, the registry, the shell
    /// chosen on the command line, the style before the first widget, the shell made, built and shown. Qt's own file
    /// dialog is used, which a case can answer.
    static void startShell(const QString& id)
    {
        auto running = std::make_unique<RunningShell>();
        running->id = id;
        running->session = std::make_unique<an::ui::ApplicationSession>();
        running->core = std::make_unique<an::ui::shell::ApplicationCore>();
        running->registry = std::make_unique<an::ui::shell::ShellRegistry>(registeredShells());
        running->core->setApplicationSession(running->session.get());
        running->core->setShellRegistry(running->registry.get());
        running->started = an::ui::startApplication(
            *running->core, *running->registry, {QStringLiteral("AfterNight"), QStringLiteral("--shell=") + id},
            QProcessEnvironment(), [](const an::ui::StyleProfile profile) {
                // The shell's profile, put in force in a process whose widgets an earlier shell made, as the suites do.
                an::ui::testing::forceStyleProfile(profile);
                // The style is the process's: one start of the application installs it once.
                if (an::ui::modernization::ModernizationFeatureFlags::uiTokensEnabled() &&
                    an::ui::installedAnProxyStyle() == nullptr)
                {
                    (void)an::ui::installAnProxyStyle();
                }
            });
        ASSERT_NE(running->started.shell, nullptr);
        ASSERT_NE(running->started.choice.descriptor, nullptr);
        ASSERT_EQ(running->started.choice.descriptor->id, id);
        ASSERT_EQ(running->started.choice.source, an::ui::shell::ShellChoiceSource::CommandLine);
        ASSERT_NE(an::ui::appContext(), nullptr);
        an::ui::IGlobalSettingsProvider* const settings = an::ui::appContext()->getGlobalSettings();
        ASSERT_NE(settings, nullptr);
        ASSERT_TRUE(settings->upsert(QString::fromLatin1(SettingsKeys::INTERFACE_USE_SYSTEM_FILE_BROWSER),
                                     QStringLiteral("0")));
        if (id == QLatin1String(an::ui::shell::ClassicShell::kId))
        {
            auto* const classic = dynamic_cast<an::ui::shell::ClassicShell*>(running->started.shell.get());
            ASSERT_NE(classic, nullptr);
            running->driver = std::make_unique<ClassicDriver>(*classic);
        }
        an::ui::showApplication(*running->started.shell);
        s_running = std::move(running);
    }

    /// Ends the running shell as the application ends: its geometry kept, the shell gone, the session shut down.
    static an::Status stopShell()
    {
        if (s_running == nullptr)
        {
            return an::Status::success();
        }
        std::unique_ptr<RunningShell> running = std::move(s_running);
        if (running->driver != nullptr)
        {
            running->driver->reset();
        }
        running->driver.reset();
        const an::Status shut = an::ui::stopApplication(std::move(running->started.shell), *running->session);
        running->core->setShellRegistry(nullptr);
        running->core.reset();
        running->session.reset();
        return shut;
    }

    [[nodiscard]] static ShellDriver& shell()
    {
        return *s_running->driver;
    }

    [[nodiscard]] static an::ui::shell::IShell& contract()
    {
        return *s_running->started.shell;
    }

    [[nodiscard]] static an::ui::shell::ApplicationCore& core()
    {
        return *s_running->core;
    }

    [[nodiscard]] static const an::consumer::EngineSession& session()
    {
        return s_running->session->engineSession();
    }

    [[nodiscard]] static QString filePath(const QString& name)
    {
        return QDir(s_files->path()).filePath(name);
    }

    /// A grey Float32 image of 8 x 4 samples of one value, written as a FITS file.
    static QString writeImage(const QString& name, float value)
    {
        const QString path = filePath(name);
        std::optional<an::consumer::ImageAsset> asset = app::filledAsset(session(), value);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return path;
        }
        const std::optional<an::ImageDescriptor> descriptor = asset->descriptor();
        const an::Result<an::OutputIntent> intent =
            an::ui::appContext()->getOutputIntentProvider()->buildOutputIntent(*descriptor, an::CodecFormat::Fits);
        EXPECT_TRUE(intent.hasValue()) << intent.status().message();
        if (intent.hasValue())
        {
            const an::Status written = an::ui::writeViewImage(session(), *asset, path, *intent, {});
            EXPECT_TRUE(written.ok()) << written.message();
        }
        return path;
    }

    /// Opens a new image as a person does, and fails the case when no view came.
    static ImageViewControl* openNew(const QString& name, float value = 0.25f)
    {
        ImageViewControl* const view = shell().openImage(writeImage(name, value));
        EXPECT_NE(view, nullptr) << name.toStdString() << " was not shown";
        return view;
    }

    /// A SER sequence of `frames` grey frames, written from FITS files.
    static QString writeSequence(const QString& name, int frames)
    {
        std::vector<ImageListProcessHelpers::ImageListEntry> entries;
        for (int frame = 0; frame < frames; ++frame)
        {
            const QString path = writeImage(QStringLiteral("%1-%2.fits").arg(name).arg(frame), 0.25f);
            std::optional<an::consumer::ImageAsset> asset = app::filledAsset(session(), 0.25f);
            EXPECT_TRUE(asset.has_value());
            an::Result<an::CodecMetadata> codec =
                an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{});
            EXPECT_TRUE(codec.hasValue());
            if (!asset.has_value() || !codec.hasValue())
            {
                return {};
            }
            entries.push_back(ImageListProcessHelpers::ImageListEntry{
                .path = path,
                .descriptor = *asset->descriptor(),
                .metadata =
                    an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{},
                                              an::AstrometricSolution{}, an::Provenance{}, *codec, std::nullopt)});
        }
        const QString path = filePath(name + QStringLiteral(".ser"));
        const an::Status made = an::ui::makeNewSequence(
            session(), an::ui::NewSequenceRequest{.files = entries, .kind = an::ui::NewSequenceKind::Ser, .path = path},
            {});
        EXPECT_TRUE(made.ok()) << made.message();
        return path;
    }

    /// Moves the Brightness/Contrast window's brightness to `value` as a person does; the window offers Apply only once
    /// a parameter leaves its identity.
    static void setBrightness(QWidget& window, int value)
    {
        auto* const brightness = window.findChild<QSpinBox*>(QStringLiteral("brightnessValue"));
        ASSERT_NE(brightness, nullptr);
        brightness->setValue(value);
    }

    /// The files of the run's temporary folder.
    [[nodiscard]] static QStringList temporaryFiles()
    {
        const QString folder = an::ui::appContext()->getTempFileService()->getTempDirPath();
        return QDir(folder).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    }

    static void settle()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }

    static inline std::unique_ptr<QTemporaryDir> s_files;
    static inline std::unique_ptr<RunningShell> s_running;
};

/// The tab group a view is in.
[[nodiscard]] KDDockWidgets::Core::Group* groupOf(ImageViewControl* view)
{
    return view->getDockWidget() != nullptr ? view->getDockWidget()->group() : nullptr;
}

} // namespace

// An image opened as a person opens one is shown in a view of its own, which becomes the active view.
TEST_P(ShellScenario, OpensAnImageAndShowsItActive)
{
    ImageViewControl* const view = openNew(QStringLiteral("opened.fits"));
    ASSERT_NE(view, nullptr);

    EXPECT_EQ(shell().activeView(), view);
    EXPECT_EQ(shell().viewNames(), QStringList{QString::fromStdString(view->getUniqueName())});
}

// Two images split into panes of their own, grouped into one tab group, and both fitted to their windows.
TEST_P(ShellScenario, SplitsGroupsAndFitsTwoImages)
{
    ImageViewControl* const first = openNew(QStringLiteral("first.fits"));
    ImageViewControl* const second = openNew(QStringLiteral("second.fits"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    ASSERT_TRUE(shell().runAction(ids::kSplitAllImages));
    settle();
    EXPECT_NE(groupOf(first), groupOf(second));

    ASSERT_TRUE(shell().runAction(ids::kGroupAllImages));
    settle();
    EXPECT_EQ(groupOf(first), groupOf(second));

    std::vector<float> actual;
    for (ImageViewControl* view : {first, second})
    {
        view->zoom100();
        actual.push_back(view->getCurrentZoom());
    }
    ASSERT_TRUE(shell().runAction(ids::kFitImagesToWindow));
    settle();
    std::vector<float> fitted;
    for (ImageViewControl* view : {first, second})
    {
        fitted.push_back(view->getCurrentZoom());
        EXPECT_NE(fitted.back(), actual[fitted.size() - 1u]) << "the fit left a view at its actual size";
    }
    // Each view is at the zoom its own fit gives.
    for (std::size_t index = 0u; index < 2u; ++index)
    {
        ImageViewControl* const view = index == 0u ? first : second;
        view->zoom100();
        view->fitToWindow();
        EXPECT_FLOAT_EQ(view->getCurrentZoom(), fitted[index]);
    }
}

// A locked layout refuses a drag; unlocked, the drag is allowed again.
TEST_P(ShellScenario, ALockedLayoutRefusesADrag)
{
    ImageViewControl* const view = openNew(QStringLiteral("locked.fits"));
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(shell().canDragView(*view));

    ASSERT_TRUE(shell().runAction(ids::kLockLayout));
    EXPECT_FALSE(shell().canDragView(*view));

    ASSERT_TRUE(shell().runAction(ids::kUnlockLayout));
    EXPECT_TRUE(shell().canDragView(*view));
}

// Maximize View maximizes the active view, and a second time restores it.
TEST_P(ShellScenario, MaximizesAndRestoresAView)
{
    ImageViewControl* const view = openNew(QStringLiteral("maximized.fits"));
    ASSERT_NE(view, nullptr);

    ASSERT_TRUE(shell().runAction(ids::kMaximizeView));
    EXPECT_TRUE(app::processEventsUntil([&]() { return view->isMaximized(); }));
    EXPECT_TRUE(contract().viewHost().isMaximizedViewActive());

    ASSERT_TRUE(shell().runAction(ids::kMaximizeView));
    EXPECT_TRUE(app::processEventsUntil([&]() { return !view->isMaximized(); }));
    EXPECT_FALSE(contract().viewHost().isMaximizedViewActive());
}

// Rename names the active view anew through its window; Duplicate makes a second view of the same image.
TEST_P(ShellScenario, RenamesAndDuplicatesAView)
{
    ImageViewControl* const view = openNew(QStringLiteral("named.fits"), 0.5f);
    ASSERT_NE(view, nullptr);

    ASSERT_TRUE(shell().runAction(ids::kRename));
    QWidget* rename = nullptr;
    ASSERT_TRUE(app::processEventsUntil([&]() {
        rename = app::visibleWidget([](QWidget* widget) { return app::isA<ImageRenameControl>(widget); });
        return rename != nullptr;
    }));
    auto* const field = rename->findChild<QLineEdit*>();
    auto* const buttons = rename->findChild<QDialogButtonBox*>();
    ASSERT_NE(field, nullptr);
    ASSERT_NE(buttons, nullptr);
    ASSERT_NE(buttons->button(QDialogButtonBox::Ok), nullptr);
    field->setText(QStringLiteral("renamed"));
    buttons->button(QDialogButtonBox::Ok)->click();
    EXPECT_TRUE(app::processEventsUntil([&]() { return view->getUniqueName() == "renamed"; })) << view->getUniqueName();
    EXPECT_TRUE(shell().viewNames().contains(QStringLiteral("renamed")));

    ASSERT_TRUE(shell().runAction(ids::kDuplicate));
    EXPECT_TRUE(app::processEventsUntil([&]() { return shell().viewNames().size() == 2; }))
        << shell().viewNames().join(QStringLiteral(", ")).toStdString();
}

// An applied process is undone and redone: the image returns to its state before it, and to its state after it.
TEST_P(ShellScenario, UndoesAndRedoesAnAppliedProcess)
{
    ImageViewControl* const view = openNew(QStringLiteral("negated.fits"), 0.25f);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(shell().runAction(ids::kNegativeTransformation));
    ASSERT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.75) < 1e-6; }));

    ASSERT_TRUE(shell().runAction(ids::kUndo));
    EXPECT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.25) < 1e-6; }));

    ASSERT_TRUE(shell().runAction(ids::kRedo));
    EXPECT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.75) < 1e-6; }));
}

// A process launched over an image and applied changes the image, and is one step of its history: one undo leaves
// none, and the image as it was.
TEST_P(ShellScenario, AnAppliedProcessIsOneHistoryStepAndChangesTheImage)
{
    ImageViewControl* const view = openNew(QStringLiteral("processed.fits"), 0.25f);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(view->viewHistory(), nullptr);
    ASSERT_FALSE(view->viewHistory()->hasUndo());

    const ProcessLaunch launch = shell().launchProcess(kBrightnessContrast);
    ASSERT_NE(launch.window, nullptr) << launch.refusal.toStdString();
    setBrightness(*launch.window, 51);
    ASSERT_TRUE(shell().pressApply(*launch.window));

    EXPECT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.45) < 1e-3; }, 20000))
        << app::viewSample(*view);
    ASSERT_TRUE(view->viewHistory()->hasUndo());
    launch.window->close();
    settle();

    ASSERT_TRUE(shell().runAction(ids::kUndo));
    EXPECT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.25) < 1e-6; }));
    EXPECT_FALSE(view->viewHistory()->hasUndo()) << "the Apply was more than one step";
}

// A run of a process over the frames of a sequence, cancelled as a person cancels it before its first frame,
// publishes nothing: the view still shows the sequence it was opened on and the run leaves no file behind.
TEST_P(ShellScenario, ARunOverASequenceIsCancelled)
{
    const QString sequence = writeSequence(QStringLiteral("frames"), 8);
    ImageViewControl* const view = shell().openImage(sequence);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(view->isSequence());
    const QStringList temporaryBefore = temporaryFiles();
    const ProcessLaunch launch = shell().launchProcess(kBrightnessContrast);
    ASSERT_NE(launch.window, nullptr) << launch.refusal.toStdString();
    setBrightness(*launch.window, 51);
    // The window offers Apply, and what it started on the pool when it opened has ended.
    ASSERT_TRUE(app::processEventsUntil([&]() {
        return shell().applyOffered(*launch.window) && QThreadPool::globalInstance()->activeThreadCount() == 0;
    }));

    {
        PoolHold hold;
        ASSERT_TRUE(hold.held());
        ASSERT_TRUE(shell().pressApply(*launch.window));
        ASSERT_TRUE(shell().runInProgress(*launch.window)) << "a run over a sequence shows its progress";
        ASSERT_TRUE(shell().cancelRun(*launch.window));
    }

    EXPECT_TRUE(app::processEventsUntil([&]() { return !shell().runInProgress(*launch.window); }, 20000));
    settle();
    ASSERT_TRUE(view->document().sequence().has_value());
    EXPECT_EQ(QDir::cleanPath(view->document().sequence()->sourcePath), QDir::cleanPath(sequence));
    EXPECT_EQ(temporaryFiles(), temporaryBefore);
}

// A process launched with no image is refused, and says why.
TEST_P(ShellScenario, AProcessWithNoImageSaysWhyItCannotRun)
{
    ASSERT_TRUE(shell().viewNames().isEmpty());

    const ProcessLaunch launch = shell().launchProcess(kBrightnessContrast);

    EXPECT_EQ(launch.window, nullptr);
    EXPECT_EQ(launch.refusal, QStringLiteral("No selected image to process"));
}

// A process that needs the image to itself is refused while another process is attached to it, and says why.
TEST_P(ShellScenario, AnExclusiveProcessIsRefusedWhileAnotherIsAttached)
{
    ASSERT_NE(openNew(QStringLiteral("shared.fits")), nullptr);
    const ProcessLaunch attached = shell().launchProcess(kBrightnessContrast);
    ASSERT_NE(attached.window, nullptr) << attached.refusal.toStdString();

    const ProcessLaunch exclusive = shell().launchProcess(kCrop);

    EXPECT_EQ(exclusive.window, nullptr);
    EXPECT_TRUE(
        exclusive.refusal.startsWith(QStringLiteral("This process requires exclusive access to the target image")))
        << exclusive.refusal.toStdString();
}

// The menu a view shows on a right click is the record's, entry for entry.
TEST_P(ShellScenario, ViewMenuEntries)
{
    ImageViewControl* const view = openNew(QStringLiteral("menu.fits"));
    ASSERT_NE(view, nullptr);

    const QStringList entries = shell().viewMenuEntries(*view);

    ASSERT_FALSE(entries.isEmpty()) << "the view showed no menu";
    an::tests::fixtures::BaselineRecord record(QString::fromLatin1(app::kViewMenuInventoryPath),
                                               app::viewMenuInventoryPreamble());
    EXPECT_TRUE(record.matches(QString::fromLatin1(app::kViewMenuSection), entries));
}

// Every action of the catalogue a person could reach from a menu of 1.1 is reachable in the shell, and offered in the
// catalogue's state for the same state of the views: with no image, and with one.
TEST_P(ShellScenario, EveryCatalogueActionIsReachableInTheCataloguesState)
{
    const an::tests::fixtures::BaselineRecord record(QString::fromLatin1(app::kActionInventoryPath),
                                                     app::actionInventoryPreamble());
    const QStringList actions = record.entries(QString::fromLatin1(app::kActionsSection));
    ASSERT_GT(actions.size(), 170);
    const auto compare = [&](const char* state) {
        int reachable = 0;
        QStringList unreached;
        QStringList differing;
        for (const QString& entry : actions)
        {
            const QStringList fields = an::tests::fixtures::baselineFields(entry);
            if (fields.size() < 8 || fields[7].isEmpty())
            {
                continue; // in no menu of 1.1
            }
            ++reachable;
            const std::string id = fields[0].toStdString();
            const QAction* const catalogued = core().actions().action(id);
            const std::optional<bool> offered = shell().offeredEnabled(id);
            if (catalogued == nullptr || !offered.has_value())
            {
                unreached.append(fields[0]);
            }
            else if (*offered != catalogued->isEnabled())
            {
                differing.append(fields[0]);
            }
        }
        EXPECT_GT(reachable, 170) << state;
        EXPECT_TRUE(unreached.isEmpty()) << state
                                         << ", out of reach: " << unreached.join(QStringLiteral(", ")).toStdString();
        EXPECT_TRUE(differing.isEmpty()) << state << ", not in the catalogue's state: "
                                         << differing.join(QStringLiteral(", ")).toStdString();
    };

    compare("with no image");
    ASSERT_NE(openNew(QStringLiteral("reach.fits")), nullptr);
    settle();
    compare("with one image");
}

// A file opened joins the recent files, first.
TEST_P(ShellScenario, TheRecentFilesGainTheOpenedFile)
{
    ImageViewControl* const view = openNew(QStringLiteral("recent.fits"));
    ASSERT_NE(view, nullptr);

    const std::deque<std::string> recent = an::ui::appContext()->getRecentFilesService()->getRecentFilesList();
    ASSERT_FALSE(recent.empty());
    EXPECT_EQ(QDir::cleanPath(QString::fromStdString(recent.front())),
              QDir::cleanPath(filePath(QStringLiteral("recent.fits"))));
}

// A file dropped on the window opens in a view of its own.
TEST_P(ShellScenario, ADroppedFileOpens)
{
    const QString path = writeImage(QStringLiteral("dropped.fits"), 0.5f);

    ASSERT_TRUE(shell().dropFiles({path}));

    EXPECT_TRUE(app::processEventsUntil(
        [&]() {
            ImageViewControl* const active = shell().activeView();
            return active != nullptr && active->hasImage() &&
                   QDir::cleanPath(active->document().names().sourcePath) == QDir::cleanPath(path);
        },
        20000));
}

// The status line reads the active image's size and kind and its zoom as it changes, and the sky position under the
// cursor while a view reports one.
TEST_P(ShellScenario, TheStatusLineShowsTheImageTheZoomAndTheCoordinates)
{
    ImageViewControl* const view = openNew(QStringLiteral("status.fits"));
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(app::processEventsUntil([&]() {
        return shell().statusText().contains(QStringLiteral("8x4 (Grayscale)"));
    })) << shell().statusText().toStdString();

    ASSERT_TRUE(shell().runAction(ids::kZoomIn));
    const QString zoom = QString::number(view->getCurrentZoom(), 'f', 2) + QStringLiteral(" %");
    EXPECT_TRUE(app::processEventsUntil([&]() { return shell().statusText().contains(zoom); }))
        << shell().statusText().toStdString() << " does not read " << zoom.toStdString();

    // What a view reports as the cursor crosses an image with a sky solution.
    contract().statusDisplay().showCelestialCoordinates(83.633, 22.0145);
    EXPECT_TRUE(shell().statusText().contains(QStringLiteral("RA 05"))) << shell().statusText().toStdString();
    EXPECT_TRUE(shell().statusText().contains(QRegularExpression(QStringLiteral("Dec \\+?22"))))
        << shell().statusText().toStdString();
    contract().statusDisplay().clearCelestialCoordinates();
    EXPECT_FALSE(shell().statusText().contains(QStringLiteral("RA "))) << shell().statusText().toStdString();
}

// Closing a view whose image has unsaved changes asks first; answered Yes, the view closes.
TEST_P(ShellScenario, ClosingAViewWithUnsavedChangesAsks)
{
    ImageViewControl* const view = openNew(QStringLiteral("unsaved.fits"), 0.25f);
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(shell().runAction(ids::kNegativeTransformation));
    ASSERT_TRUE(app::processEventsUntil([&]() { return std::abs(app::viewSample(*view) - 0.75) < 1e-6; }));
    ASSERT_TRUE(view->hasPendingChanges());
    const QString name = QString::fromStdString(view->getUniqueName());

    EXPECT_EQ(shell().closeView(*view), QStringLiteral("Close image with unsaved changes"));

    EXPECT_FALSE(shell().viewNames().contains(name));
}

// A change of theme repaints the window at once: no restart.
TEST_P(ShellScenario, AThemeChangeRepaintsWithoutARestart)
{
    an::ui::IInterfaceSettingsRuntimeService* const runtime =
        an::ui::appContext()->getInterfaceSettingsRuntimeService();
    ASSERT_NE(runtime, nullptr);
    const bool dark = an::ui::isDarkTheme();
    const QColor before = shell().window().palette().color(QPalette::Window);

    runtime->applyTheme(dark ? QStringLiteral("light") : QStringLiteral("dark"));
    settle();

    EXPECT_NE(an::ui::isDarkTheme(), dark);
    EXPECT_NE(shell().window().palette().color(QPalette::Window), before);

    runtime->applyTheme(dark ? QStringLiteral("dark") : QStringLiteral("light"));
    settle();
    EXPECT_EQ(an::ui::isDarkTheme(), dark);
    EXPECT_EQ(shell().window().palette().color(QPalette::Window), before);
}

// The settings open at the page asked for.
TEST_P(ShellScenario, TheSettingsDialogOpensAtAPage)
{
    auto page = std::make_shared<QString>();
    (void)app::answerWhenShown([page](QWidget* widget) {
        auto* const dialog = qobject_cast<SettingsDialog*>(widget);
        if (dialog == nullptr)
        {
            return false;
        }
        if (dialog->currentPage() != nullptr)
        {
            *page = dialog->currentPage()->pageTitle();
        }
        dialog->reject();
        return true;
    });

    an::ui::appContext()->getDialogService()->openSettingsAt(QStringLiteral("Interface"));

    EXPECT_TRUE(app::processEventsUntil([&]() { return !page->isEmpty(); }));
    EXPECT_EQ(*page, QStringLiteral("Interface"));
}

// Closing the application with a process window open asks, closes the process window before the application's
// stores go, shuts the session down cleanly and leaves no file in the run's temporary folder.
TEST_P(ShellScenario, ClosingTheApplicationWithAProcessOpenLeavesNothingBehind)
{
    ASSERT_NE(openNew(QStringLiteral("closing.fits")), nullptr);
    const ProcessLaunch launch = shell().launchProcess(kBrightnessContrast);
    ASSERT_NE(launch.window, nullptr) << launch.refusal.toStdString();
    const QPointer<QWidget> watched(launch.window);
    const QString temporary = an::ui::appContext()->getTempFileService()->getTempDirPath();

    ASSERT_TRUE(shell().closeWindow());
    const an::Status shut = stopShell();

    EXPECT_TRUE(shut.ok()) << shut.message();
    settle();
    EXPECT_TRUE(watched.isNull()) << "the process window outlived the application";
    EXPECT_EQ(an::ui::appContext(), nullptr);
    const QStringList left = QDir(temporary).entryList(QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_TRUE(left.isEmpty()) << left.join(QStringLiteral(", ")).toStdString();
}

INSTANTIATE_TEST_SUITE_P(, ShellScenario, ::testing::Values(std::string(an::ui::shell::ClassicShell::kId)),
                         [](const ::testing::TestParamInfo<std::string>& shellId) { return shellId.param; });
