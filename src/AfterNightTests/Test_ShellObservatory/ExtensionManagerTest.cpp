// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Extension Manager in the Observatory shell, and an extension's processes where a person finds them. The
/// manager, its status page and an extension's settings are drawn in the shell's look at both densities, and at the
/// dialog's least size no control is clipped; an extension's details say how it runs and that a panel the application
/// draws opens in the main window; the manager is reached from the palette, the menu tree and Home's More. An
/// extension's process is listed in the palette under its family with the quiet "extension" tag, whose tooltip says it
/// runs in its own process, and in the menus - its category's and its family's - with the tag painted after its text.

#include "Test_ShellObservatory/KeptWindowLook.h"
#include "Test_ShellObservatory/ObservatoryWindowCase.h"
#include "fixtures/ClippedControls.h"
#include "fixtures/WindowLook.h"

#include "chrome/MenuBarOnAlt.h"
#include "pages/HomePage.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "actions/ProcessMenuModel.h"
#include "app/ExtensionManager.h"
#include "app/ProcessRegistry.h"
#include "components/AnCommandPalette.h"
#include "components/AnTag.h"
#include "controls/DashboardPreferences.h"
#include "controls/HomeQuickActions.h"
#include "controls/settings/ExtensionManagerPage.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QTabWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::observatory
{

namespace
{

using an::ui::ExtensionManagerPageInstallRequest;
using an::ui::ExtensionManagerPageInstallResult;
using an::ui::ExtensionManagerPageRepositorySnapshot;
using an::ui::extensions::ExtensionInterface;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionProcessInterface;
using an::ui::extensions::ExtensionRepositoryBrowseEntry;
using an::ui::extensions::ExtensionRepositoryInstallState;

using ExtensionManagerPage = ObservatoryWindowCase;
using ExtensionMenus = ObservatoryWindowCase;
using Palette = ObservatoryWindowCase;

/// Lets the window settle: posted work delivered, the deletions done, the layouts applied.
void settle()
{
    for (int round = 0; round < 3; ++round)
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}

/// Lets the work on the thread pool end and what it hands the GUI thread land - the extensions the application found
/// as it started, a page's default refresh - so that nothing of it lands over what a case puts in place afterwards.
void settleThePool()
{
    for (int round = 0; round < 3; ++round)
    {
        QThreadPool::globalInstance()->waitForDone(30000);
        settle();
    }
}

using an::tests::look::clippedControls;
using an::tests::look::nameOf;
using an::tests::look::textIsMeasured;
using an::tests::look::unsizedScrollContents;

/// Resizes a window to the least size it takes - what it keeps when asked for none - and lays it out there.
QSize toItsLeastSize(QWidget& window)
{
    window.resize(1, 1);
    settle();
    return window.size();
}

QString sanitizedToken(const std::string_view value)
{
    QString sanitized;
    bool separator = false;
    for (const char ch : value)
    {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0)
        {
            sanitized.push_back(QChar::fromLatin1(static_cast<char>(std::tolower(byte))));
            separator = false;
        }
        else if (!separator)
        {
            sanitized.push_back(QLatin1Char('_'));
            separator = true;
        }
    }
    return sanitized;
}

/// The object names the Extension Manager's and its status page's parts carry.
QString managerField(const std::string_view id, const char* field)
{
    return QStringLiteral("anExtensionManager_%1_%2").arg(QString::fromLatin1(field), sanitizedToken(id));
}

QString managerButton(const std::string_view id, const char* button)
{
    return QStringLiteral("anExtensionManagerButton_%1_%2").arg(QString::fromLatin1(button), sanitizedToken(id));
}

QString statusButton(const std::string_view id, const char* button)
{
    return QStringLiteral("anExtensionStatusButton_%1_%2").arg(QString::fromLatin1(button), sanitizedToken(id));
}

/// A package the repository lists, with every detail it has.
ExtensionRepositoryBrowseEntry repositoryEntry(const std::string& id, const std::string& name,
                                               const ExtensionRepositoryInstallState state,
                                               const std::string& installed = {})
{
    ExtensionRepositoryBrowseEntry entry;
    entry.id = id;
    entry.name = name;
    entry.summary = "Statistical stretch, star tools and colour helpers for linear and stretched images.";
    entry.description = "A longer description of what " + name +
                        " does to an image, written as a package author writes it, across more than one line.";
    entry.author = "AfterNight Tests";
    entry.publisher_id = "afternight.tests";
    entry.license = "GPL-3.0-or-later";
    entry.attribution = "AfterNight port of " + name + ", originally authored by its upstream authors.";
    entry.original_author = "Upstream Authors";
    entry.original_project = name;
    entry.original_source_url = "https://example.invalid/upstream/" + id;
    entry.upstream_commit = "0123456789abcdef0123456789abcdef01234567";
    entry.homepage_url = "https://example.invalid/extensions/" + id;
    entry.repository_url = "https://example.invalid/repositories/" + id;
    entry.support_url = "https://example.invalid/extensions/" + id + "/support";
    entry.tags = {"stretch", "stars", "colour"};
    entry.latest_version = "1.4.0";
    entry.installed_version = installed;
    entry.selected_release_version = "1.4.0";
    entry.release_min_app_version = "2.0.0";
    entry.release_package_format_version = 1;
    entry.release_protocol_version = 1;
    entry.release_sdk_version = 1;
    entry.release_sdk_backend = "runtime";
    entry.release_launch_mode = "single_image";
    entry.release_runtime_targets = {"linux-clang-x86_64", "windows-msvc-x86_64"};
    entry.release_changelog = "Adds the star tools and a faster stretch.";
    entry.release_published_at_utc = "2026-11-01T00:00:00Z";
    entry.selected_asset_name = id + "-1.4.0-windows-msvc-x86_64.tar.zst";
    entry.selected_asset_download_url = "archives/" + entry.selected_asset_name;
    entry.selected_asset_package_hash_sha256 = std::string(64, 'a');
    entry.selected_asset_runtime_targets = {"windows-msvc-x86_64"};
    entry.selected_asset_signature_state = ExtensionPackageSignatureState::Unsigned;
    entry.runtime_compatible = state != ExtensionRepositoryInstallState::Incompatible;
    if (!entry.runtime_compatible)
    {
        entry.compatibility_diagnostic = "The latest release needs AfterNight 2.1.0 or later.";
    }
    entry.install_state = state;
    if (!installed.empty())
    {
        entry.installed_source_available = true;
        entry.installed_source = ExtensionPackageSource::Repository;
        entry.installed_status_available = true;
        entry.installed_load_status = ExtensionLoadStatus::Ready;
        entry.installed_sdk_backend = "runtime";
        entry.installed_process_interfaces = {ExtensionProcessInterface{"Stretch", ExtensionInterface::Panel}};
    }
    return entry;
}

/// What the repository lists for the cases: one of each state a card shows.
ExtensionManagerPageRepositorySnapshot repositorySnapshot()
{
    ExtensionManagerPageRepositorySnapshot snapshot;
    snapshot.success = true;
    snapshot.indexUrl = QUrl(QStringLiteral("https://extensions.example.invalid/index.json"));
    snapshot.environment_storage_available = true;
    snapshot.environment_total_bytes = 3ull * 1024ull * 1024ull * 1024ull;
    snapshot.environment_active_bytes = 2ull * 1024ull * 1024ull * 1024ull;
    snapshot.environment_reclaimable_bytes = 1024ull * 1024ull * 1024ull;
    snapshot.environment_total_count = 3;
    snapshot.environment_reclaimable_count = 1;
    snapshot.entries = {
        repositoryEntry("installed_suite", "Installed Suite", ExtensionRepositoryInstallState::Installed, "1.4.0"),
        repositoryEntry("updated_tools", "Comet Tools With A Rather Long Name",
                        ExtensionRepositoryInstallState::UpdateAvailable, "0.9.2"),
        repositoryEntry("new_lab", "Photometry Lab", ExtensionRepositoryInstallState::NotInstalled),
        repositoryEntry("queued_lab", "Queued Lab", ExtensionRepositoryInstallState::NotInstalled),
        repositoryEntry("too_new", "Too New", ExtensionRepositoryInstallState::Incompatible)};
    return snapshot;
}

/// An installed package as the status page lists it, with the shared settings it keeps.
ExtensionLoadResult installedPackage(const std::string& id, const std::string& name)
{
    ExtensionLoadResult result;
    result.source = ExtensionPackageSource::User;
    result.status = ExtensionLoadStatus::Ready;
    result.package_root = "C:/AfterNight/extensions/" + id;
    result.manifest_path = result.package_root / "extension.json";
    an::ui::extensions::ExtensionManifest manifest;
    manifest.id = id;
    manifest.name = name;
    manifest.version = "1.4.0";
    manifest.entry_point = "main";
    manifest.category = "filters";
    manifest.package_root = result.package_root.string();
    manifest.manifest_path = result.manifest_path.string();
    manifest.sdk_backend = an::ui::extensions::ExtensionSdkBackend::Runtime;
    manifest.runtime_targets = {"windows-msvc-x86_64", "linux-clang-x86_64"};
    result.manifest = manifest;
    an::ui::extensions::ExtensionEnvironmentSnapshot environment;
    environment.state = an::ui::extensions::ExtensionEnvironmentState::Ready;
    environment.environment_path = result.package_root / ".venv";
    result.environment = environment;
    an::ui::extensions::ExtensionSettingsSchemaSnapshot settings;
    settings.state = an::ui::extensions::ExtensionSettingsSchemaState::Ready;
    const auto field = [](const char* id, const char* type, const char* label) {
        return QJsonObject{{QStringLiteral("id"), QLatin1String(id)},
                           {QStringLiteral("type"), QLatin1String(type)},
                           {QStringLiteral("label"), QLatin1String(label)}};
    };
    settings.schema = QJsonArray{field("cli_executable", "file_path", "Executable of the command-line tool"),
                                 field("models", "dir_path", "Folder of the downloaded models"),
                                 field("threads", "int", "Threads"), field("verbose", "bool", "Write a detailed log")};
    result.settings_schema = settings;
    return result;
}

/// What the inspection of the Extensions dialog found.
struct Inspection
{
    bool opened = false;
    QSize leastSize;
    QSize settingsLeastSize;
    QStringList clipped;
    QStringList unsized;
    /// What the dialog and an extension's settings lack of the look of a window that stays a window.
    QStringList look;
};

/// Opens the Extensions dialog as the menu tree's Manage Extensions opens it and, at its least size, lists what is
/// clipped: on the Extension Manager with a card of each state, every card's details open, the warning of unsigned
/// packages shown, an install under way and another queued; on the status page with its cards' details open; and in
/// an extension's settings at their own least size.
Inspection inspectTheExtensionsDialog(an::ui::shell::ApplicationCore& core)
{
    Inspection inspection;
    auto release = std::make_shared<std::promise<void>>();
    std::shared_future<void> released = release->get_future().share();
    auto installing = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<an::tests::app::WindowPoll> poll = an::tests::app::answerWhenShown([&](QWidget* shown) {
        auto* const dialog = qobject_cast<QDialog*>(shown);
        if (dialog == nullptr || dialog->objectName() != QLatin1String("extensionsDialog"))
        {
            return false;
        }
        inspection.opened = true;
        auto* const tabs = dialog->findChild<QTabWidget*>(QStringLiteral("anExtensionsTabs"));
        auto* const manager = dialog->findChild<an::ui::ExtensionManagerPage*>();
        auto* const status = dialog->findChild<ExtensionStatusPage*>();
        if (tabs == nullptr || manager == nullptr || status == nullptr)
        {
            inspection.clipped.append(QStringLiteral("the dialog has no tabs, manager or status page"));
            dialog->reject();
            return true;
        }
        // The refresh the dialog began with its default provider lands before the cases' own.
        settleThePool();

        manager->setRepositoryProvider([]() { return repositorySnapshot(); });
        manager->setUnsignedInstallConfirmationProvider([](QWidget*, const QString&, const QString&) { return true; });
        manager->setInstallProvider([released, installing](const ExtensionManagerPageInstallRequest& request) {
            if (request.entry.id == "new_lab")
            {
                installing->store(true);
                released.wait();
            }
            ExtensionManagerPageInstallResult result;
            result.success = false;
            result.diagnostic = QStringLiteral("Stopped by the case.");
            return result;
        });
        manager->loadSettings();
        if (auto* const allow =
                manager->findChild<QCheckBox*>(QStringLiteral("anExtensionManagerAllowUnsignedExtensionsCheck")))
        {
            allow->setChecked(true);
        }
        for (QToolButton* details : manager->findChildren<QToolButton*>())
        {
            if (details->objectName().startsWith(QLatin1String("anExtensionManagerButton_details_")))
            {
                details->setChecked(true);
            }
        }
        if (auto* const install = manager->findChild<QPushButton*>(managerButton("new_lab", "primaryAction")))
        {
            install->click();
        }
        (void)an::tests::app::processEventsUntil([&installing]() { return installing->load(); }, 5000);
        if (auto* const queued = manager->findChild<QPushButton*>(managerButton("queued_lab", "primaryAction")))
        {
            queued->click();
        }
        for (const QString& found : an::tests::observatory::lookProblemsOfAKeptWindow(*dialog))
        {
            inspection.look.append(QStringLiteral("Extensions: ") + found);
        }
        inspection.leastSize = toItsLeastSize(*dialog);
        for (const QString& found : clippedControls(*dialog))
        {
            inspection.clipped.append(QStringLiteral("Browse: ") + found);
        }
        for (const QString& found : unsizedScrollContents(*dialog))
        {
            inspection.unsized.append(QStringLiteral("Browse: ") + found);
        }
        release->set_value();
        auto* const cancel = manager->findChild<QPushButton*>(QStringLiteral("anExtensionManagerCancelButton"));
        (void)an::tests::app::processEventsUntil([cancel]() { return cancel == nullptr || cancel->isHidden(); }, 10000);

        // The status page, read from the cases' packages as its tab is shown.
        status->setDiscoveryProvider([]() {
            return std::vector<ExtensionLoadResult>{installedPackage("rc_tools", "Noise Tools Suite"),
                                                    installedPackage("second", "Second Package")};
        });
        tabs->setCurrentIndex(1);
        settle();
        for (QToolButton* details : status->findChildren<QToolButton*>())
        {
            if (details->objectName().startsWith(QLatin1String("anExtensionStatusButton_details_")))
            {
                details->setChecked(true);
            }
        }
        settle();
        for (const QString& found : clippedControls(*dialog))
        {
            inspection.clipped.append(QStringLiteral("Installed: ") + found);
        }
        for (const QString& found : unsizedScrollContents(*dialog))
        {
            inspection.unsized.append(QStringLiteral("Installed: ") + found);
        }

        // An extension's settings, at their own least size.
        const std::shared_ptr<an::tests::app::WindowPoll> settingsPoll =
            an::tests::app::answerWhenShown([&inspection](QWidget* shownSettings) {
                auto* const settings = qobject_cast<QDialog*>(shownSettings);
                if (settings == nullptr || !settings->windowTitle().endsWith(QLatin1String("Settings")) ||
                    settings->objectName() == QLatin1String("extensionsDialog"))
                {
                    return false;
                }
                for (const QString& found : an::tests::observatory::lookProblemsOfAKeptWindow(*settings))
                {
                    inspection.look.append(QStringLiteral("Settings: ") + found);
                }
                inspection.settingsLeastSize = toItsLeastSize(*settings);
                for (const QString& found : clippedControls(*settings))
                {
                    inspection.clipped.append(QStringLiteral("Settings: ") + found);
                }
                for (const QString& found : unsizedScrollContents(*settings))
                {
                    inspection.unsized.append(QStringLiteral("Settings: ") + found);
                }
                settings->reject();
                return true;
            });
        if (auto* const open = status->findChild<QPushButton*>(statusButton("rc_tools", "settings")))
        {
            open->click();
        }
        else
        {
            inspection.clipped.append(QStringLiteral("the status page offers no Settings for the package"));
        }
        settingsPoll->ended = true;
        dialog->reject();
        return true;
    });
    QAction* const manage = core.actions().action(an::ui::shell::ids::kManageExtensions);
    if (manage != nullptr)
    {
        manage->trigger();
    }
    poll->ended = true;
    settle();
    return inspection;
}

/// Which page of the Extensions dialog the next one shown opens at: the Extension Manager or another.
struct Opened
{
    bool shown = false;
    bool atTheManager = false;
};

std::shared_ptr<an::tests::app::WindowPoll> answerTheExtensionsDialog(Opened& opened)
{
    return an::tests::app::answerWhenShown([&opened](QWidget* shown) {
        auto* const dialog = qobject_cast<QDialog*>(shown);
        if (dialog == nullptr || dialog->objectName() != QLatin1String("extensionsDialog"))
        {
            return false;
        }
        opened.shown = true;
        const auto* const tabs = dialog->findChild<QTabWidget*>(QStringLiteral("anExtensionsTabs"));
        opened.atTheManager =
            tabs != nullptr && dynamic_cast<const an::ui::ExtensionManagerPage*>(tabs->currentWidget()) != nullptr;
        dialog->reject();
        return true;
    });
}

/// An extension of the cases' own, as the application lists one it found: its process registered under its family,
/// "Tagged Tools", and put in the menus. It goes with the case.
class TaggedExtension
{
public:
    static constexpr char kExtensionId[] = "tests.tagged";
    static constexpr char kProcessId[] = "tests.tagged.denoise";
    static constexpr char kActionName[] = "actionTests_Tagged_Denoise";

    explicit TaggedExtension(an::ui::shell::ApplicationCore& core) : m_core(core)
    {
        // The extensions the application found as it started are in place first, so none of that lands over these.
        settleThePool();
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = kProcessId;
        descriptor.display_name = "Tagged Denoise";
        descriptor.process_name = "Tagged Denoise";
        descriptor.category = "denoising";
        descriptor.action_object_name = kActionName;
        descriptor.is_extension_process = true;
        descriptor.extension_id = kExtensionId;
        (void)an::ui::process_registry::ProcessRegistry::instance().registerProcess(descriptor);

        an::ui::extensions::ExtensionManifest manifest;
        manifest.id = kExtensionId;
        manifest.name = "Tagged Tools";
        an::ui::extensions::ExtensionProcessManifest process;
        process.id = kProcessId;
        process.display_name = "Tagged Denoise";
        process.process_name = "Tagged Denoise";
        process.class_name = "TaggedDenoise";
        process.action_object_name = kActionName;
        process.category = "denoising";
        manifest.processes.push_back(process);
        ExtensionLoadResult result;
        result.status = ExtensionLoadStatus::Ready;
        result.manifest = manifest;
        m_core.processMenus().setExtensions({result});
        settle();
    }

    ~TaggedExtension()
    {
        m_core.processMenus().setExtensions({});
        (void)an::ui::process_registry::ProcessRegistry::instance().removeExtensionProcesses();
        settle();
    }

    TaggedExtension(const TaggedExtension&) = delete;
    TaggedExtension& operator=(const TaggedExtension&) = delete;

private:
    an::ui::shell::ApplicationCore& m_core;
};

/// How many pixels of a rectangle of an image differ from a colour.
int pixelsUnlike(const QImage& image, const QRect& rect, const QColor& colour)
{
    int unlike = 0;
    const QRect inside = rect.intersected(image.rect());
    for (int y = inside.top(); y <= inside.bottom(); ++y)
    {
        for (int x = inside.left(); x <= inside.right(); ++x)
        {
            const QColor pixel = image.pixelColor(x, y);
            if (std::abs(pixel.red() - colour.red()) + std::abs(pixel.green() - colour.green()) +
                    std::abs(pixel.blue() - colour.blue()) >
                24)
            {
                ++unlike;
            }
        }
    }
    return unlike;
}

} // namespace

// The check of the cases below sees a clipped control: a button squeezed under its text.
TEST_F(ExtensionManagerPage, TheClippingCheckSeesAControlSqueezedUnderItsText)
{
    QWidget host;
    auto* const row = new QHBoxLayout(&host);
    auto* const button = new QPushButton(QStringLiteral("A button with a rather long text"), &host);
    row->addWidget(button);
    host.resize(400, 60);
    host.show();
    settle();
    EXPECT_TRUE(clippedControls(host).isEmpty()) << clippedControls(host).join(QStringLiteral("; ")).toStdString();
    button->setMinimumWidth(0);
    button->setMaximumWidth(24);
    settle();
    EXPECT_FALSE(clippedControls(host).isEmpty());
}

// A package's name that wraps takes every line it needs, on the Extension Manager's card and on the status page's: the
// card's row grows for it, whatever the font.
TEST_F(ExtensionManagerPage, ALongNameTakesTheLinesItNeeds)
{
    const std::string longName =
        "A Package Whose Name Is Long Enough To Wrap Beside Its Tags And Its Buttons At This Width";
    const auto wholeAndWrapped = [](const QLabel* title) {
        EXPECT_NE(title, nullptr);
        if (title == nullptr)
        {
            return;
        }
        EXPECT_GT(title->heightForWidth(title->width()), title->fontMetrics().height() * 3 / 2)
            << "the name does not wrap at " << title->width() << " px";
        EXPECT_GE(title->height(), title->heightForWidth(title->width())) << "its lines are cut";
    };

    an::ui::ExtensionManagerPage manager(nullptr);
    manager.setRepositoryProvider([&longName]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {
            repositoryEntry("long_name", longName, ExtensionRepositoryInstallState::Installed, "1.4.0")};
        return snapshot;
    });
    manager.loadSettings();
    manager.resize(560, 600);
    manager.show();
    settle();
    wholeAndWrapped(manager.findChild<QLabel*>(managerField("long_name", "title")));
    manager.hide();

    ExtensionStatusPage status(nullptr);
    status.setDiscoveryProvider(
        [&longName]() { return std::vector<ExtensionLoadResult>{installedPackage("long_name", longName)}; });
    status.loadSettings();
    status.resize(560, 600);
    status.show();
    settle();
    wholeAndWrapped(status.findChild<QLabel*>(QStringLiteral("anExtensionStatus_title_long_name")));
    status.hide();
}

// At both densities the Extensions dialog is no smaller than it states, 860 x 640, and no higher than a 1280 x 720
// display leaves a window - under its heading the comfortable density needs a few pixels more than it states - and an
// extension's settings take their stated least size, 500 x 400; at them no control of the Extension Manager - its
// toolbar, its cards with their details open, an install under way and another queued, the warning of unsigned
// packages - nor of the status page with its cards' details open, nor of an extension's settings, is clipped, and
// every scroll area's content is as tall as its width needs. Whether a control is clipped depends on how wide text is,
// and the suites' own platform draws text in a box font about twice the interface's width: there what the dialogs
// hold needs more than they state, and a window takes what it needs as it is first shown, so the dialogs are held to
// be no smaller than they state and the rest is reported skipped; started under the native platform
// (`QT_QPA_PLATFORM=windows`) the case measures text as the interface does, and holds the least sizes.
TEST_F(ExtensionManagerPage, NoControlIsClipped)
{
    ASSERT_EQ(an::ui::styleProfile(), an::ui::StyleProfile::Observatory);
    const bool measured = textIsMeasured();
    const an::ui::ThemeDensity before = an::ui::themeDensity();
    for (const an::ui::ThemeDensity density : {an::ui::ThemeDensity::Compact, an::ui::ThemeDensity::Comfortable})
    {
        an::ui::setThemeDensity(density);
        settle();
        const char* const named = density == an::ui::ThemeDensity::Compact ? "Compact" : "Comfortable";
        const Inspection inspection = inspectTheExtensionsDialog(core());
        ASSERT_TRUE(inspection.opened) << named;
        if (measured)
        {
            EXPECT_EQ(inspection.leastSize.width(), 860) << named;
            EXPECT_GE(inspection.leastSize.height(), 640) << named;
            EXPECT_LE(inspection.leastSize.height(), an::tests::look::kFloorRoom.height())
                << named << ": the dialog's least height is more than a 1280 x 720 display leaves a window";
            EXPECT_EQ(inspection.settingsLeastSize, QSize(500, 400)) << named;
        }
        else
        {
            EXPECT_TRUE(inspection.leastSize.width() >= 860 && inspection.leastSize.height() >= 640)
                << named << ": " << inspection.leastSize.width() << " x " << inspection.leastSize.height();
            EXPECT_TRUE(inspection.settingsLeastSize.width() >= 500 && inspection.settingsLeastSize.height() >= 400)
                << named << ": " << inspection.settingsLeastSize.width() << " x "
                << inspection.settingsLeastSize.height();
        }
        EXPECT_TRUE(inspection.unsized.isEmpty())
            << named << ": " << inspection.unsized.join(QStringLiteral("\n  ")).toStdString();
        // Each is a window that stays a window: a heading, the button that accepts at its footer's right, text that
        // meets the contrast rule, a dialog's surface.
        EXPECT_TRUE(inspection.look.isEmpty())
            << named << ": " << inspection.look.join(QStringLiteral("\n  ")).toStdString();
        if (measured)
        {
            EXPECT_TRUE(inspection.clipped.isEmpty())
                << named << ": " << inspection.clipped.join(QStringLiteral("\n  ")).toStdString();
        }
    }
    an::ui::setThemeDensity(before);
    settle();
    if (!measured && !HasFailure())
    {
        GTEST_SKIP() << "the dialogs are no smaller than they state; their least sizes and whether anything is "
                        "clipped are measured under the native platform, where text is as wide as the interface "
                        "draws it";
    }
}

// In this shell a panel the application draws opens in the main window, as a page; an extension that draws a window of
// its own opens in it, and one with no interface nowhere. How it runs follows its host.
TEST_F(ExtensionManagerPage, DetailsSayHowItRunsAndWhereItOpens)
{
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        ExtensionRepositoryBrowseEntry panel =
            repositoryEntry("panel_ext", "Panel Ext", ExtensionRepositoryInstallState::Installed, "1.4.0");
        ExtensionRepositoryBrowseEntry window =
            repositoryEntry("window_ext", "Window Ext", ExtensionRepositoryInstallState::Installed, "1.4.0");
        window.installed_sdk_backend = "protocol";
        window.installed_process_interfaces = {ExtensionProcessInterface{"Viewer", ExtensionInterface::Window}};
        ExtensionRepositoryBrowseEntry none =
            repositoryEntry("none_ext", "None Ext", ExtensionRepositoryInstallState::Installed, "1.4.0");
        none.installed_sdk_backend = "protocol";
        none.installed_process_interfaces = {ExtensionProcessInterface{"Batch", ExtensionInterface::None}};
        snapshot.entries = {panel, window, none};
        return snapshot;
    });
    page.loadSettings();

    const auto said = [&page](const char* id, const char* field) {
        const auto* const label = page.findChild<QLabel*>(managerField(id, field));
        return label != nullptr ? label->text() : QStringLiteral("<none>");
    };
    EXPECT_EQ(said("panel_ext", "runs"), QStringLiteral("With engine access"));
    EXPECT_EQ(said("panel_ext", "opens"), QStringLiteral("In the main window"));
    EXPECT_EQ(said("window_ext", "runs"), QStringLiteral("Without engine access"));
    EXPECT_EQ(said("window_ext", "opens"), QStringLiteral("In a window of its own"));
    EXPECT_EQ(said("none_ext", "opens"), QStringLiteral("Nowhere, it has no interface"));
}

// An extension that added processes while it ran says, in its details, how many it has added - the ones the application
// lists now; one that added none says nothing of it.
TEST_F(ExtensionManagerPage, DetailsSayHowManyProcessesItAdded)
{
    settleThePool();
    auto& registry = an::ui::process_registry::ProcessRegistry::instance();
    for (const auto& [extension, suffix] :
         {std::pair{"panel_ext", "one"}, std::pair{"panel_ext", "two"}, std::pair{"window_ext", "only"}})
    {
        an::ui::process_registry::ProcessDescriptor descriptor;
        descriptor.id = std::string(extension) + "." + suffix;
        descriptor.display_name = suffix;
        descriptor.category = "filters";
        descriptor.is_extension_process = true;
        descriptor.is_contributed_process = true;
        descriptor.extension_id = extension;
        ASSERT_TRUE(registry.registerProcess(descriptor));
    }
    an::ui::ExtensionManagerPage page(nullptr);
    page.setRepositoryProvider([]() {
        ExtensionManagerPageRepositorySnapshot snapshot;
        snapshot.success = true;
        snapshot.entries = {
            repositoryEntry("panel_ext", "Panel Ext", ExtensionRepositoryInstallState::Installed, "1.4.0"),
            repositoryEntry("window_ext", "Window Ext", ExtensionRepositoryInstallState::Installed, "1.4.0"),
            repositoryEntry("none_ext", "None Ext", ExtensionRepositoryInstallState::Installed, "1.4.0")};
        return snapshot;
    });
    page.loadSettings();

    const auto said = [&page](const char* id) {
        const auto* const label = page.findChild<QLabel*>(managerField(id, "added"));
        return label != nullptr ? label->text() : QStringLiteral("<none>");
    };
    EXPECT_EQ(said("panel_ext"), QStringLiteral("2 processes"));
    EXPECT_EQ(said("window_ext"), QStringLiteral("1 process"));
    EXPECT_EQ(said("none_ext"), QStringLiteral("<none>")) << "an extension that added none says nothing of it";
    (void)registry.removeExtensionProcesses();
}

// The palette finds Manage Extensions and runs it: the Extensions dialog opens at the Extension Manager.
TEST_F(ExtensionManagerPage, OpensFromThePalette)
{
    window().openPalette();
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    QTest::keyClicks(palette.queryField(), QStringLiteral("Manage Extensions"));
    settle();
    const QString id = QStringLiteral("action:") + QString::fromLatin1(an::ui::shell::ids::kManageExtensions.data(),
                                                                       an::ui::shell::ids::kManageExtensions.size());
    ASSERT_TRUE(palette.shownRowIds().contains(id)) << palette.shownRowIds().join(QStringLiteral(", ")).toStdString();
    palette.setSelectedId(id);
    Opened opened;
    const std::shared_ptr<an::tests::app::WindowPoll> poll = answerTheExtensionsDialog(opened);
    QTest::keyClick(palette.queryField(), Qt::Key_Return);
    EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() { return opened.shown; }, 10000));
    poll->ended = true;
    EXPECT_TRUE(opened.atTheManager);
}

// The menu tree's Extensions menu holds Manage Extensions, which opens the Extensions dialog at the Extension Manager.
TEST_F(ExtensionManagerPage, OpensFromTheMenuTree)
{
    QMenu* const extensions = window().menuBarOnAlt().menu("menuExtensions");
    ASSERT_NE(extensions, nullptr);
    QAction* manage = nullptr;
    for (QAction* action : extensions->actions())
    {
        if (action->objectName() ==
            QLatin1String(an::ui::shell::ids::kManageExtensions.data(), an::ui::shell::ids::kManageExtensions.size()))
        {
            manage = action;
        }
    }
    ASSERT_NE(manage, nullptr) << "the Extensions menu holds no Manage Extensions";
    Opened opened;
    const std::shared_ptr<an::tests::app::WindowPoll> poll = answerTheExtensionsDialog(opened);
    manage->trigger();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() { return opened.shown; }, 10000));
    poll->ended = true;
    EXPECT_TRUE(opened.atTheManager);
}

// Home's More holds Extension Manager, which opens the Extensions dialog at it.
TEST_F(ExtensionManagerPage, OpensFromHomesMore)
{
    window().showHome();
    settle();
    an::ui::HomeQuickActions* const actions = window().homePage()->quickActions();
    ASSERT_NE(actions, nullptr);
    const QString title =
        DashboardPreferences::actionTitleForId(QString::fromUtf8(DashboardPreferences::ACTION_ID_EXTENSION_MANAGER));
    QAction* manager = nullptr;
    for (QAction* action : actions->moreMenu()->actions())
    {
        if (action->text() == title)
        {
            manager = action;
        }
    }
    ASSERT_NE(manager, nullptr) << "Home's More holds no " << title.toStdString();
    Opened opened;
    const std::shared_ptr<an::tests::app::WindowPoll> poll = answerTheExtensionsDialog(opened);
    manager->trigger();
    EXPECT_TRUE(an::tests::app::processEventsUntil([&opened]() { return opened.shown; }, 10000));
    poll->ended = true;
    EXPECT_TRUE(opened.atTheManager);
}

// An extension's process is a row of the palette's processes under its family, with the quiet tag "extension" after
// its title - a tag high - whose tooltip says it runs in its own process; a process of the application has none.
TEST_F(Palette, ExtensionProcessesCarryTheirTag)
{
    const TaggedExtension extension(core());
    window().openPalette();
    an::ui::AnCommandPalette& palette = *window().palette();
    ASSERT_TRUE(an::tests::app::processEventsUntil([&]() { return palette.isVisible(); }, 3000));
    QTest::keyClicks(palette.queryField(), QStringLiteral("Tagged Denoise"));
    settle();
    const QString id = QStringLiteral("process:") + QLatin1String(TaggedExtension::kProcessId);
    ASSERT_TRUE(palette.shownRowIds().contains(id)) << palette.shownRowIds().join(QStringLiteral(", ")).toStdString();

    const an::ui::CommandRow* row = nullptr;
    for (const an::ui::CommandRowGroup& group : palette.groups())
    {
        for (const an::ui::CommandRow& candidate : group.rows)
        {
            if (candidate.id == id)
            {
                row = &candidate;
            }
        }
    }
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->subtitle, QStringLiteral("Tagged Tools")) << "the family is its subtitle";
    EXPECT_EQ(row->tag, QStringLiteral("extension"));

    // The tag is drawn right after the title, a tag high, inside the row.
    const QRect rowRect = palette.rowRect(id);
    const QRect tag = palette.tagRect(id);
    ASSERT_FALSE(tag.isEmpty());
    EXPECT_TRUE(rowRect.contains(tag));
    EXPECT_EQ(tag.height(), an::ui::tokens().metrics.tag);
    EXPECT_EQ(tag.width(), an::ui::AnTag::tagSize(row->tag, an::ui::AnTag::Tone::Outlined).width());
    const QImage grabbed = palette.grab().toImage();
    const qreal ratio = grabbed.devicePixelRatio();
    const QRect inImage = QRectF(QPointF(tag.topLeft()) * ratio, QSizeF(tag.size()) * ratio).toAlignedRect();
    const QColor around =
        grabbed.pixelColor(static_cast<int>((tag.right() + 4) * ratio), static_cast<int>(tag.center().y() * ratio));
    EXPECT_GT(pixelsUnlike(grabbed, inImage, around), 20) << "nothing is painted where the tag is";

    // Its tooltip says what it means.
    QWidget* const viewport = palette.list()->viewport();
    const QPoint over = viewport->mapFrom(&palette, tag.center());
    QHelpEvent help(QEvent::ToolTip, over, viewport->mapToGlobal(over));
    QApplication::sendEvent(viewport, &help);
    EXPECT_EQ(QToolTip::text(), QStringLiteral("Runs in its own process"));
    QToolTip::hideText();

    // A process of the application carries none.
    palette.queryField()->clear();
    QTest::keyClicks(palette.queryField(), QStringLiteral("Gaussian Blur"));
    settle();
    const QString builtIn = QStringLiteral("process:filters.actionGaussian_Blur");
    ASSERT_TRUE(palette.shownRowIds().contains(builtIn));
    EXPECT_TRUE(palette.tagRect(builtIn).isEmpty());
    palette.close();
    settle();
}

// An extension's process is in the menu of its category and in its family's menu of the Extensions menu - the same
// action, which carries the tag - and the style paints the tag after its text, the entry widened for it.
TEST_F(ExtensionMenus, AnExtensionsProcessCarriesItsTagInItsCategoryAndItsFamily)
{
    const TaggedExtension extension(core());
    an::ui::observatory::MenuBarOnAlt& menus = window().menuBarOnAlt();
    QMenu* const noise = menus.menu("menuNoise_Reduction");
    ASSERT_NE(noise, nullptr);
    QAction* tagged = nullptr;
    for (QAction* action : noise->actions())
    {
        if (action->objectName() == QLatin1String(TaggedExtension::kActionName))
        {
            tagged = action;
        }
    }
    ASSERT_NE(tagged, nullptr) << "the category's menu does not list the process";
    EXPECT_EQ(tagged->property(an::ui::parts::kMenuTagProperty).toString(), QStringLiteral("extension"));

    QMenu* const extensions = menus.menu("menuExtensions");
    ASSERT_NE(extensions, nullptr);
    QMenu* family = nullptr;
    for (QAction* action : extensions->actions())
    {
        if (action->menu() != nullptr && action->text() == QLatin1String("Tagged Tools"))
        {
            family = action->menu();
        }
    }
    ASSERT_NE(family, nullptr) << "the Extensions menu has no menu of the family";
    ASSERT_FALSE(family->actions().isEmpty());
    EXPECT_EQ(family->actions().front(), tagged) << "the family's menu lists its process first, the same action";

    // Painted: an entry with the tag is wider than the same entry without it by the tag and the room before it, and
    // what is painted after its text differs.
    QMenu withTag(&window());
    QMenu without(&window());
    QAction* const shownWith = withTag.addAction(QStringLiteral("Tagged Denoise"));
    shownWith->setProperty(an::ui::parts::kMenuTagProperty, QStringLiteral("extension"));
    without.addAction(QStringLiteral("Tagged Denoise"));
    const int tagWidth = an::ui::AnTag::tagSize(QStringLiteral("extension"), an::ui::AnTag::Tone::Outlined).width();
    EXPECT_GE(withTag.sizeHint().width() - without.sizeHint().width(), tagWidth + an::ui::parts::kMenuTagGap);
    withTag.resize(withTag.sizeHint());
    without.resize(withTag.sizeHint());
    const QImage paintedWith = withTag.grab().toImage();
    const QImage paintedWithout = without.grab().toImage();
    ASSERT_EQ(paintedWith.size(), paintedWithout.size());
    EXPECT_NE(paintedWith, paintedWithout) << "the tag is not painted";
}

} // namespace an::tests::observatory
