// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QMargins>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "components/AnCardWidget.h"
#include "components/AnInfoBanner.h"
#include "components/AnStatusBadge.h"
#include "controls/settings/ExtensionStatusPage.h"
#include "theme/AnDesignTokens.h"

namespace
{

using an::ui::extensions::ExtensionDependencyContext;
using an::ui::extensions::ExtensionEnvironmentSnapshot;
using an::ui::extensions::ExtensionEnvironmentState;
using an::ui::extensions::ExtensionLoadResult;
using an::ui::extensions::ExtensionLoadStatus;
using an::ui::extensions::ExtensionManifest;
using an::ui::extensions::ExtensionPackageSignatureState;
using an::ui::extensions::ExtensionPackageSource;
using an::ui::extensions::ExtensionProcessManifest;
using an::ui::extensions::ExtensionRuntimeLaunchOutcome;
using an::ui::extensions::ExtensionRuntimeStatusSnapshot;
using an::ui::extensions::ExtensionRuntimeTerminationKind;
using an::ui::extensions::ExtensionSdkBackend;

void ensureApp()
{
    if (QApplication::instance())
    {
        return;
    }

    static int argc = 0;
    static char* argv[] = {nullptr};
    static QApplication app(argc, argv);
    (void)app;
}

QString fieldObjectName(const std::string_view extension_id, const char* field_name)
{
    QString sanitized;
    sanitized.reserve(static_cast<int>(extension_id.size()));
    bool previous_was_separator = false;
    for (const char ch : extension_id)
    {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0)
        {
            sanitized.push_back(QChar::fromLatin1(static_cast<char>(std::tolower(uch))));
            previous_was_separator = false;
        }
        else if (!previous_was_separator)
        {
            sanitized.push_back(QChar::fromLatin1('_'));
            previous_was_separator = true;
        }
    }

    while (sanitized.endsWith(QChar::fromLatin1('_')))
    {
        sanitized.chop(1);
    }
    if (sanitized.isEmpty())
    {
        sanitized = QStringLiteral("extension");
    }

    return QStringLiteral("anExtensionStatus_%1_%2").arg(QString::fromLatin1(field_name), sanitized);
}

ExtensionManifest makeManifest(const std::string& id, const std::string& name, const std::string& version,
                               const std::string& package_root)
{
    ExtensionManifest manifest;
    manifest.id = id;
    manifest.name = name;
    manifest.version = version;
    manifest.entry_point = "demo";
    manifest.category = "filters";
    manifest.manifest_path = (std::filesystem::path(package_root) / "extension.json").string();
    manifest.package_root = package_root;
    manifest.sdk_backend = ExtensionSdkBackend::Runtime;
    return manifest;
}

ExtensionLoadResult makeReadyResult(const std::string& id, const std::string& name, const std::string& package_root,
                                    const std::string& environment_path, const ExtensionPackageSource source)
{
    ExtensionLoadResult result;
    result.source = source;
    result.status = ExtensionLoadStatus::Ready;
    result.package_root = package_root;
    result.manifest_path = std::filesystem::path(package_root) / "extension.json";
    result.manifest = makeManifest(id, name, "1.0.0", package_root);

    ExtensionEnvironmentSnapshot environment;
    environment.state = ExtensionEnvironmentState::Ready;
    environment.environment_path = environment_path;
    result.environment = environment;
    return result;
}

ExtensionLoadResult makeAttentionResult(const std::string& id, const std::string& name, const std::string& package_root,
                                        const ExtensionEnvironmentState state, const std::string& diagnostic)
{
    ExtensionLoadResult result;
    result.source = ExtensionPackageSource::User;
    result.status = ExtensionLoadStatus::Ready;
    result.package_root = package_root;
    result.manifest_path = std::filesystem::path(package_root) / "extension.json";
    result.manifest = makeManifest(id, name, "2.0.0", package_root);

    ExtensionEnvironmentSnapshot environment;
    environment.state = state;
    environment.diagnostic = diagnostic;
    environment.environment_path = std::filesystem::path(package_root) / ".venv";
    result.environment = environment;
    return result;
}

an::ui::extensions::ExtensionSettingsSchemaSnapshot makeReadySchema(const QJsonArray& schema)
{
    an::ui::extensions::ExtensionSettingsSchemaSnapshot snapshot;
    snapshot.state = an::ui::extensions::ExtensionSettingsSchemaState::Ready;
    snapshot.schema = schema;
    return snapshot;
}

ExtensionProcessManifest makeProcess(const std::string& suffix, const std::string& display_name)
{
    ExtensionProcessManifest process;
    process.id_suffix = suffix;
    process.display_name = display_name;
    process.process_name = display_name;
    process.class_name = "DemoProcess";
    process.category = "Extensions";
    return process;
}

bool waitFor(const std::function<bool()>& predicate, const int timeout_ms = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout_ms)
    {
        if (predicate())
        {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }

    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

bool hasLabelText(QWidget& root, const QString& expected_text)
{
    const auto labels = root.findChildren<QLabel*>();
    return std::any_of(labels.begin(), labels.end(),
                       [&expected_text](const QLabel* label) { return label && label->text() == expected_text; });
}

bool hasSettingsWidgetById(QWidget* root, const QString& widget_id)
{
    if (!root)
    {
        return false;
    }

    const auto widgets = root->findChildren<QWidget*>();
    return std::any_of(widgets.begin(), widgets.end(), [&widget_id](const QWidget* widget) {
        return widget && widget->property("anExtensionUiWidgetId").toString() == widget_id;
    });
}

/// Holds the application's thread pool back. While the gate is closed the pool starts no task it is handed, so the work
/// a page queues waits until the case has looked at the page in between; the gate opens when it goes.
class ThreadPoolGate
{
public:
    ThreadPoolGate() = default;

    ~ThreadPoolGate()
    {
        open();
    }

    ThreadPoolGate(const ThreadPoolGate&) = delete;
    ThreadPoolGate& operator=(const ThreadPoolGate&) = delete;

    /// Closes the gate: every task the pool is handed from now on waits until the gate opens.
    void close()
    {
        const std::lock_guard lock(m_Mutex);
        if (m_Release)
        {
            return;
        }

        QThreadPool* const pool = QThreadPool::globalInstance();
        m_MaxThreadCount = pool->maxThreadCount();
        m_Release = std::make_shared<std::promise<void>>();
        pool->setMaxThreadCount(1);
        pool->start([released = m_Release->get_future().share()]() { released.wait(); });
    }

    /// Opens the gate and gives the pool back the threads it had.
    void open()
    {
        const std::lock_guard lock(m_Mutex);
        if (!m_Release)
        {
            return;
        }

        m_Release->set_value();
        m_Release.reset();
        QThreadPool::globalInstance()->setMaxThreadCount(m_MaxThreadCount);
    }

private:
    std::mutex m_Mutex;
    std::shared_ptr<std::promise<void>> m_Release;
    int m_MaxThreadCount = 0;
};

/// Lets the work earlier cases left on the thread pool finish and delivers what it reported.
void drainThreadPool()
{
    QThreadPool::globalInstance()->waitForDone(30000);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

std::vector<an::ui::AnStatusBadge*> findStatusBadges(QWidget* root)
{
    std::vector<an::ui::AnStatusBadge*> badges;
    if (!root)
    {
        return badges;
    }

    const auto labels = root->findChildren<QLabel*>();
    for (QLabel* label : labels)
    {
        if (auto* badge = dynamic_cast<an::ui::AnStatusBadge*>(label))
        {
            badges.push_back(badge);
        }
    }
    return badges;
}

QString labelTextWithoutWrapHints(const QLabel* label)
{
    QString text = label ? label->text() : QString();
    text.remove(QChar(0x200b));
    return text;
}

} // anonymous namespace

class ExtensionStatusPageTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensureApp();
    }
};

TEST_F(ExtensionStatusPageTest, LoadSettingsRendersExtensionStatusCardsFromDiscoveryResults)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        results.push_back(makeReadyResult("my_invert", "My Invert", "/tmp/extensions/my_invert",
                                          "/tmp/extensions/my_invert/.venv", ExtensionPackageSource::Bundled));
        results.push_back(makeAttentionResult("sample_ai", "Sample AI", "/tmp/extensions/sample_ai",
                                              ExtensionEnvironmentState::Bootstrapping,
                                              "Provisioning wheels in the bundled environment"));
        return results;
    });

    page.loadSettings();

    auto* source_summary = page.findChild<QLabel*>(fieldObjectName("my_invert", "summary"));
    ASSERT_NE(source_summary, nullptr);
    EXPECT_TRUE(source_summary->text().contains(QStringLiteral("Source: Bundled")));
    EXPECT_TRUE(source_summary->text().contains(QStringLiteral("ID: my_invert")));

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Available")));
    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Bootstrapping")));

    auto* validation_label = page.findChild<QLabel*>(fieldObjectName("sample_ai", "validation"));
    ASSERT_NE(validation_label, nullptr);
    EXPECT_TRUE(validation_label->text().contains(QStringLiteral("Provisioning wheels")));

    auto* summary_banner =
        dynamic_cast<an::ui::AnInfoBanner*>(page.findChild<QWidget*>(QStringLiteral("anExtensionStatusSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    EXPECT_TRUE(summary_banner->message().contains(QStringLiteral("2 extension package(s) discovered")));

    auto* refresh_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusRefreshButton"));
    ASSERT_NE(refresh_button, nullptr);
    EXPECT_TRUE(refresh_button->toolTip().contains(QStringLiteral("discovery"), Qt::CaseInsensitive));
}

TEST_F(ExtensionStatusPageTest, DefaultDiscoveryRunsOffTheUiThread)
{
    drainThreadPool();
    an::ui::invalidateInstalledExtensionStatusCache(false);

    ThreadPoolGate gate;
    gate.close();
    ExtensionStatusPage page(nullptr);
    page.loadSettings();

    // The load returned while the discovery still waits on the pool: the page did not discover on the UI thread.
    auto* summary_banner =
        dynamic_cast<an::ui::AnInfoBanner*>(page.findChild<QWidget*>(QStringLiteral("anExtensionStatusSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    const QString pending = summary_banner->message();
    EXPECT_TRUE(pending == QStringLiteral("Loading installed extension status...") ||
                pending == QStringLiteral("Refreshing installed extension status in the background..."))
        << pending.toStdString();

    gate.open();
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message() != pending; }, 30000))
        << summary_banner->message().toStdString();
    EXPECT_FALSE(summary_banner->message().contains(QStringLiteral("installed extension status")))
        << summary_banner->message().toStdString();
}

TEST_F(ExtensionStatusPageTest, DefaultDiscoveryDefersSettingsSchemaInspection)
{
    EXPECT_FALSE(an::ui::extensions::default_extension_discovery_options().context.eager_settings_schema_inspection);
}

TEST_F(ExtensionStatusPageTest, BackgroundRefreshKeepsCachedStatusStaleUntilNewResultsArePublished)
{
    const QString refreshing = QStringLiteral("Refreshing installed extension status in the background...");
    const auto bannerOf = [](ExtensionStatusPage& page) {
        auto* banner = dynamic_cast<an::ui::AnInfoBanner*>(
            page.findChild<QWidget*>(QStringLiteral("anExtensionStatusSummaryBanner")));
        return banner ? banner->message() : QStringLiteral("<no banner>");
    };
    const auto settled = [&bannerOf, &refreshing](ExtensionStatusPage& page) {
        const QString message = bannerOf(page);
        return message != refreshing && message != QStringLiteral("Loading installed extension status...");
    };

    // A first page publishes the installed status the process caches.
    drainThreadPool();
    ExtensionStatusPage first(nullptr);
    first.loadSettings();
    ASSERT_TRUE(waitFor([&]() { return settled(first); }, 30000)) << bannerOf(first).toStdString();
    const QString published = bannerOf(first);

    {
        ThreadPoolGate gate;
        gate.close();
        auto* refresh_button = first.findChild<QPushButton*>(QStringLiteral("anExtensionStatusRefreshButton"));
        ASSERT_NE(refresh_button, nullptr);
        refresh_button->click();
        EXPECT_EQ(bannerOf(first), refreshing);

        // While that refresh runs the cached status is stale, so a page that loads now shows it as being refreshed
        // rather than as current.
        ExtensionStatusPage second(nullptr);
        second.loadSettings();
        EXPECT_EQ(bannerOf(second), refreshing);

        gate.open();
        ASSERT_TRUE(waitFor([&]() { return settled(second); }, 30000)) << bannerOf(second).toStdString();
        EXPECT_EQ(bannerOf(second), published);
        EXPECT_TRUE(waitFor([&]() { return settled(first); }, 30000)) << bannerOf(first).toStdString();
    }

    // Once the refresh published its results the cache is current, and a page that loads now shows them at once.
    ExtensionStatusPage third(nullptr);
    third.loadSettings();
    EXPECT_EQ(bannerOf(third), published);
}

TEST_F(ExtensionStatusPageTest, InstalledContentUsesScrollAreaToPreserveCardHeights)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        results.push_back(makeReadyResult("first_ext", "First Extension", "/tmp/extensions/first_ext",
                                          "/tmp/extensions/first_ext/.venv", ExtensionPackageSource::Bundled));
        results.push_back(makeAttentionResult("second_ext", "Second Extension", "/tmp/extensions/second_ext",
                                              ExtensionEnvironmentState::Failed, "Environment validation failed"));
        return results;
    });

    page.loadSettings();

    auto* scroll_area = page.findChild<QScrollArea*>(QStringLiteral("anExtensionStatusScrollArea"));
    ASSERT_NE(scroll_area, nullptr);
    EXPECT_TRUE(scroll_area->widgetResizable());
    ASSERT_NE(scroll_area->widget(), nullptr);
    EXPECT_EQ(scroll_area->widget()->objectName(), QStringLiteral("anExtensionStatusScrollContent"));

    auto* first_card = page.findChild<QWidget*>(QStringLiteral("anExtensionStatusCard_first_ext"));
    ASSERT_NE(first_card, nullptr);
    EXPECT_TRUE(scroll_area->widget()->isAncestorOf(first_card));

    auto* status_label = page.findChild<QLabel*>(fieldObjectName("second_ext", "validation"));
    ASSERT_NE(status_label, nullptr);
    EXPECT_TRUE(scroll_area->widget()->isAncestorOf(status_label));
}

TEST_F(ExtensionStatusPageTest, InstalledExtensionCardsKeepDetailsCollapsedByDefaultAndExplainCompactStatus)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        results.push_back(makeReadyResult("healthy_ext", "Healthy Extension", "/tmp/extensions/healthy_ext",
                                          "/tmp/extensions/healthy_ext/.venv", ExtensionPackageSource::Bundled));
        results.push_back(makeAttentionResult("missing_ext", "Missing Extension", "/tmp/extensions/missing_ext",
                                              ExtensionEnvironmentState::Missing, {}));
        return results;
    });

    page.loadSettings();

    auto* healthy_card = dynamic_cast<an::ui::AnCardWidget*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionStatusCard_healthy_ext")));
    ASSERT_NE(healthy_card, nullptr);
    const auto& spacing = an::ui::tokens().spacing;
    EXPECT_TRUE(healthy_card->property("anCardCompact").toBool());
    EXPECT_TRUE(healthy_card->property("anCardBodyPadded").toBool());
    ASSERT_NE(healthy_card->contentLayout(), nullptr);
    EXPECT_EQ(healthy_card->contentLayout()->contentsMargins(),
              QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));
    auto* healthy_title = page.findChild<QLabel*>(fieldObjectName("healthy_ext", "title"));
    ASSERT_NE(healthy_title, nullptr);
    EXPECT_EQ(healthy_title->text(), QStringLiteral("Healthy Extension"));
    auto* healthy_details = page.findChild<QWidget*>(fieldObjectName("healthy_ext", "detailsPanel"));
    ASSERT_NE(healthy_details, nullptr);
    EXPECT_TRUE(healthy_details->isHidden());
    const auto badges = findStatusBadges(healthy_card);
    ASSERT_FALSE(badges.empty());
    EXPECT_TRUE(std::any_of(badges.begin(), badges.end(), [](const an::ui::AnStatusBadge* badge) {
        return badge && badge->text() == QStringLiteral("Available") &&
               badge->toolTip().contains(QStringLiteral("ready to launch"), Qt::CaseInsensitive);
    }));

    auto* missing_card = dynamic_cast<an::ui::AnCardWidget*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionStatusCard_missing_ext")));
    ASSERT_NE(missing_card, nullptr);
    auto* missing_title = page.findChild<QLabel*>(fieldObjectName("missing_ext", "title"));
    ASSERT_NE(missing_title, nullptr);
    EXPECT_EQ(missing_title->text(), QStringLiteral("Missing Extension"));
    const auto missing_badges = findStatusBadges(missing_card);
    ASSERT_FALSE(missing_badges.empty());
    EXPECT_TRUE(std::any_of(missing_badges.begin(), missing_badges.end(), [](const an::ui::AnStatusBadge* badge) {
        return badge && badge->text() == QStringLiteral("Install Needed") &&
               badge->toolTip().contains(QStringLiteral("runtime environment"), Qt::CaseInsensitive);
    }));
}

TEST_F(ExtensionStatusPageTest, InstalledExtensionStatusHidesUndeclaredOptionalMetadata)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        results.push_back(makeReadyResult("minimal_ext", "Minimal Extension", "/tmp/extensions/minimal_ext",
                                          "/tmp/extensions/minimal_ext/.venv", ExtensionPackageSource::Bundled));
        return results;
    });

    page.loadSettings();

    auto* summary_label = page.findChild<QLabel*>(fieldObjectName("minimal_ext", "summary"));
    ASSERT_NE(summary_label, nullptr);
    EXPECT_TRUE(summary_label->text().contains(QStringLiteral("Source: Bundled")));
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "description")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "tags")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "repositoryAssets")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "dependencies")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "packageHash")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "archiveHash")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "signature")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "signatureDetail")), nullptr);
    EXPECT_EQ(page.findChild<QLabel*>(fieldObjectName("minimal_ext", "settingsSchemaState")), nullptr);
}

// A bundled package ships with AfterNight and goes only with it. Its card offered Uninstall, whose removal would delete
// the package from AfterNight's own folder where that folder can be written; it offers none.
TEST_F(ExtensionStatusPageTest, ABundledExtensionCardOffersNoUninstall)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        return std::vector<ExtensionLoadResult>{
            makeReadyResult("bundled_ext", "Bundled Extension", "/tmp/extensions/bundled_ext",
                            "/tmp/extensions/bundled_ext/.venv", ExtensionPackageSource::Bundled)};
    });

    page.loadSettings();

    EXPECT_NE(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_revealPackage_bundled_ext")),
              nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_uninstall_bundled_ext")), nullptr);
}

TEST_F(ExtensionStatusPageTest, InstalledExtensionCardKeepsOnlyUsefulActions)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        auto result = makeReadyResult("action_ext", "Action Extension", "/tmp/extensions/action_ext",
                                      "/tmp/extensions/action_ext/.venv", ExtensionPackageSource::User);
        result.settings_schema = makeReadySchema(QJsonArray{});
        return std::vector<ExtensionLoadResult>{result};
    });

    page.loadSettings();

    auto* reveal_package_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_revealPackage_action_ext"));
    ASSERT_NE(reveal_package_button, nullptr);
    EXPECT_TRUE(reveal_package_button->toolTip().contains(QStringLiteral("package folder"), Qt::CaseInsensitive));

    auto* uninstall_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_uninstall_action_ext"));
    ASSERT_NE(uninstall_button, nullptr);
    EXPECT_TRUE(uninstall_button->toolTip().contains(QStringLiteral("removes the installed package files"),
                                                     Qt::CaseInsensitive));
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_revealManifest_action_ext")),
              nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_revealEnvironment_action_ext")),
              nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_retrySchema_action_ext")), nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_settings_action_ext")), nullptr);
}

TEST_F(ExtensionStatusPageTest, UninstallRequiresConfirmationAndRefreshesAfterRemoval)
{
    int discovery_calls = 0;
    int confirmation_calls = 0;
    int uninstall_calls = 0;
    bool confirm_uninstall = false;

    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([&]() {
        ++discovery_calls;
        if (uninstall_calls > 0)
        {
            return std::vector<ExtensionLoadResult>{};
        }
        return std::vector<ExtensionLoadResult>{
            makeReadyResult("removable_ext", "Removable Extension", "/tmp/extensions/removable_ext",
                            "/tmp/extensions/removable_ext/.venv", ExtensionPackageSource::User)};
    });
    page.setUninstallConfirmationProvider([&](const ExtensionLoadResult& result) {
        ++confirmation_calls;
        EXPECT_EQ(result.manifest->id, "removable_ext");
        return confirm_uninstall;
    });
    page.setUninstallProvider([&](const ExtensionLoadResult& result) {
        ++uninstall_calls;
        EXPECT_EQ(result.manifest->id, "removable_ext");
        return an::ui::ExtensionStatusPageUninstallResult{true, {}};
    });

    page.loadSettings();
    auto* uninstall_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_uninstall_removable_ext"));
    ASSERT_NE(uninstall_button, nullptr);

    uninstall_button->click();
    EXPECT_EQ(confirmation_calls, 1);
    EXPECT_EQ(uninstall_calls, 0);
    EXPECT_EQ(discovery_calls, 1);

    confirm_uninstall = true;
    uninstall_button->click();
    EXPECT_EQ(confirmation_calls, 2);
    ASSERT_TRUE(waitFor([&]() { return uninstall_calls == 1; }));
    ASSERT_TRUE(waitFor([&]() { return discovery_calls == 2; }));
}

TEST_F(ExtensionStatusPageTest, UninstallShowsImmediateBusyState)
{
    std::promise<void> release_provider;
    std::shared_future<void> release_future = release_provider.get_future().share();
    std::atomic_bool provider_started = false;

    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        return std::vector<ExtensionLoadResult>{
            makeReadyResult("busy_ext", "Busy Extension", "/tmp/extensions/busy_ext", "/tmp/extensions/busy_ext/.venv",
                            ExtensionPackageSource::User)};
    });
    page.setUninstallConfirmationProvider([](const ExtensionLoadResult&) { return true; });
    page.setUninstallProvider([&](const ExtensionLoadResult&) {
        provider_started = true;
        release_future.wait();
        return an::ui::ExtensionStatusPageUninstallResult{true, QStringLiteral("Removed Busy Extension.")};
    });

    page.loadSettings();
    auto* uninstall_button = page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_uninstall_busy_ext"));
    auto* reveal_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_revealPackage_busy_ext"));
    auto* details_button = page.findChild<QToolButton*>(QStringLiteral("anExtensionStatusButton_details_busy_ext"));
    ASSERT_NE(uninstall_button, nullptr);
    ASSERT_NE(reveal_button, nullptr);
    ASSERT_NE(details_button, nullptr);

    uninstall_button->click();
    ASSERT_TRUE(waitFor([&]() { return provider_started.load(); }));

    EXPECT_EQ(uninstall_button->text(), QStringLiteral("Uninstalling..."));
    EXPECT_FALSE(uninstall_button->isEnabled());
    EXPECT_FALSE(reveal_button->isEnabled());
    EXPECT_FALSE(details_button->isEnabled());

    release_provider.set_value();
    auto* summary_banner =
        dynamic_cast<an::ui::AnInfoBanner*>(page.findChild<QWidget*>(QStringLiteral("anExtensionStatusSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(
        waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Removed Busy Extension")); }));
}

TEST_F(ExtensionStatusPageTest, ManualDeploymentWithMissingEnvironmentIsNotShownAvailable)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        auto result = makeAttentionResult("manual_ext", "Manual Extension", "/tmp/extensions/_repository/manual_ext",
                                          ExtensionEnvironmentState::Missing,
                                          "Extension environment has not been provisioned yet.");
        result.source = ExtensionPackageSource::Repository;
        result.environment->environment_path = "/tmp/extensions/_repository/manual_ext/.missing-env";
        result.manifest->dependencies.requirements_file = "requirements.lock";
        result.manifest->processes.push_back(makeProcess("manual", "Manual Process"));
        return std::vector<ExtensionLoadResult>{result};
    });

    page.loadSettings();

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Install Needed")));

    auto* card = dynamic_cast<an::ui::AnCardWidget*>(
        page.findChild<QWidget*>(QStringLiteral("anExtensionStatusCard_manual_ext")));
    ASSERT_NE(card, nullptr);
    const auto badges = findStatusBadges(card);
    EXPECT_TRUE(std::none_of(badges.begin(), badges.end(), [](const an::ui::AnStatusBadge* badge) {
        return badge && badge->text() == QStringLiteral("Available");
    }));
}

TEST_F(ExtensionStatusPageTest, StaleManagedEnvironmentStillRequiresRepair)
{
    const std::filesystem::path package_root =
        std::filesystem::temp_directory_path() / "afternight_stale_status_test_ext";
    const std::filesystem::path environment_path = package_root / ".venv";
    std::error_code ec;
    std::filesystem::remove_all(package_root, ec);
    std::filesystem::create_directories(package_root, ec);
    std::filesystem::create_directories(environment_path, ec);
    const std::filesystem::path receipt_path = package_root / "extension_package_receipt.json";
    std::ofstream receipt_stream(receipt_path);
    receipt_stream << "{}\n";
    receipt_stream.close();

    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([package_root, environment_path, receipt_path]() {
        auto result = makeAttentionResult("stale_ext", "Stale Extension", package_root.string(),
                                          ExtensionEnvironmentState::Stale, "Dependency fingerprint changed");
        result.source = ExtensionPackageSource::Repository;
        result.environment->environment_path = environment_path;
        result.provenance.receipt_path = receipt_path;
        result.manifest->processes.push_back(makeProcess("stale", "Stale Process"));
        return std::vector<ExtensionLoadResult>{result};
    });

    page.loadSettings();

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Repair Needed")));

    auto* repair_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_repairEnvironment_stale_ext"));
    ASSERT_NE(repair_button, nullptr);
    EXPECT_EQ(repair_button->text(), QStringLiteral("Repair Environment"));
    EXPECT_TRUE(repair_button->toolTip().contains(QStringLiteral("rebuilds"), Qt::CaseInsensitive));

    std::filesystem::remove_all(package_root, ec);
}

TEST_F(ExtensionStatusPageTest, MissingEnvironmentStateKeepsInstallActionEvenWhenPathExists)
{
    const std::filesystem::path package_root =
        std::filesystem::temp_directory_path() / "afternight_existing_env_status_test";
    const std::filesystem::path environment_path = package_root / ".venv";
    std::error_code ec;
    std::filesystem::remove_all(package_root, ec);
    std::filesystem::create_directories(environment_path, ec);
    const std::filesystem::path receipt_path = package_root / "extension_package_receipt.json";
    std::ofstream receipt_stream(receipt_path);
    receipt_stream << "{}\n";
    receipt_stream.close();

    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([package_root, environment_path, receipt_path]() {
        auto result = makeAttentionResult("existing_env_ext", "Existing Environment Extension", package_root.string(),
                                          ExtensionEnvironmentState::Missing,
                                          "Extension environment has not been provisioned yet.");
        result.source = ExtensionPackageSource::Repository;
        result.environment->environment_path = environment_path;
        result.provenance.receipt_path = receipt_path;
        result.manifest->dependencies.requirements_file = "requirements.lock";
        result.manifest->processes.push_back(makeProcess("existing", "Existing Process"));
        return std::vector<ExtensionLoadResult>{result};
    });

    page.loadSettings();

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Install Needed")));

    EXPECT_NE(
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_installEnvironment_existing_env_ext")),
        nullptr);

    std::filesystem::remove_all(package_root, ec);
}

TEST_F(ExtensionStatusPageTest, ManagerInstalledPackageWithMissingEnvironmentStillNeedsInstall)
{
    const std::filesystem::path package_root =
        std::filesystem::temp_directory_path() / "afternight_managed_missing_status_test";
    std::error_code ec;
    std::filesystem::remove_all(package_root, ec);
    std::filesystem::create_directories(package_root, ec);
    const std::filesystem::path receipt_path = package_root / "extension_package_receipt.json";
    std::ofstream receipt_stream(receipt_path);
    receipt_stream << "{}\n";
    receipt_stream.close();

    std::atomic_int repair_calls = 0;
    std::atomic_bool saw_managed_extension = false;
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([package_root, receipt_path, &repair_calls]() {
        if (repair_calls.load() > 0)
        {
            auto repaired = makeReadyResult("managed_ext", "Managed Extension", package_root.string(),
                                            (package_root / ".venv").string(), ExtensionPackageSource::Repository);
            repaired.provenance.receipt_path = receipt_path;
            repaired.manifest->dependencies.requirements_file = "requirements.lock";
            repaired.manifest->processes.push_back(makeProcess("managed", "Managed Process"));
            return std::vector<ExtensionLoadResult>{repaired};
        }

        auto result = makeAttentionResult("managed_ext", "Managed Extension", package_root.string(),
                                          ExtensionEnvironmentState::Missing,
                                          "Extension environment has not been provisioned yet.");
        result.source = ExtensionPackageSource::Repository;
        result.provenance.receipt_path = receipt_path;
        result.manifest->dependencies.requirements_file = "requirements.lock";
        result.manifest->processes.push_back(makeProcess("managed", "Managed Process"));
        return std::vector<ExtensionLoadResult>{result};
    });
    page.setEnvironmentRepairProvider([&repair_calls, &saw_managed_extension](const ExtensionLoadResult& result) {
        ++repair_calls;
        saw_managed_extension = result.manifest.has_value() && result.manifest->id == "managed_ext";
        an::ui::ExtensionStatusPageEnvironmentRepairResult repair_result;
        repair_result.success = true;
        repair_result.diagnostic = QStringLiteral("Installed environment for Managed Extension.");
        return repair_result;
    });

    page.loadSettings();

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Install Needed")));

    auto* install_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_installEnvironment_managed_ext"));
    ASSERT_NE(install_button, nullptr);
    EXPECT_EQ(install_button->text(), QStringLiteral("Install Environment"));
    EXPECT_TRUE(install_button->toolTip().contains(QStringLiteral("creates"), Qt::CaseInsensitive));
    install_button->click();

    auto* summary_banner =
        dynamic_cast<an::ui::AnInfoBanner*>(page.findChild<QWidget*>(QStringLiteral("anExtensionStatusSummaryBanner")));
    ASSERT_NE(summary_banner, nullptr);
    ASSERT_TRUE(waitFor([&]() { return summary_banner->message().contains(QStringLiteral("Installed environment")); }));
    EXPECT_EQ(repair_calls.load(), 1);
    EXPECT_TRUE(saw_managed_extension.load());

    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Available")));

    std::filesystem::remove_all(package_root, ec);
}

TEST_F(ExtensionStatusPageTest, LoadSettingsRendersRepositoryMetadataAndProvenance)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        auto result =
            makeReadyResult("repo_ready", "Repository Ready", "/tmp/extensions/_repository/repo_ready",
                            "/tmp/extensions/_repository/repo_ready/.venv", ExtensionPackageSource::Repository);
        result.manifest->repository_metadata.summary = "Repository metadata fixture";
        result.manifest->repository_metadata.description = "Detailed repository description";
        result.manifest->repository_metadata.author = "AfterNight Tests";
        result.manifest->repository_metadata.license = "MIT";
        result.manifest->repository_metadata.publisher_id = "afternight.tests";
        result.manifest->repository_metadata.homepage_url = "https://example.com/home";
        result.manifest->repository_metadata.repository_url = "https://example.com/repo";
        result.manifest->repository_metadata.support_url = "https://example.com/support";
        result.manifest->repository_metadata.icon_path = "media/icon.png";
        result.manifest->repository_metadata.screenshots = {"media/screenshot-a.png", "media/screenshot-b.png"};
        result.manifest->repository_metadata.tags = {"denoise", "repository"};
        result.manifest->repository_metadata.package_hash_sha256 = std::string(64, 'a');
        result.manifest->repository_metadata.signature_detail = "Manifest signature metadata is preserved.";
        result.manifest->runtime_targets = {"linux-clang-x86_64", "windows-msvc-x86_64"};
        result.manifest->binary_vendor_paths = {"bin", "wheelhouse/native"};
        result.manifest->dependencies.dependency_context = ExtensionDependencyContext::SharedGroup;
        result.manifest->dependencies.shared_group = "repository-suite";
        result.manifest->dependencies.requirements_file = "requirements.lock";
        result.manifest->dependencies.pip.find_links = {"wheelhouse"};
        result.manifest->dependencies.pip.index_urls = {"https://packages.example/simple"};
        result.manifest->dependencies.pip.extra_index_urls = {"https://extra.example/simple"};
        result.manifest->dependencies.pip.require_hashes = true;
        result.manifest->dependencies.pip.no_deps = true;
        result.manifest->dependencies.bundled_runtime_packages = {"numpy", "scipy"};
        result.provenance.source = ExtensionPackageSource::Repository;
        result.provenance.source_repository_url = "https://example.com/repo";
        result.provenance.selected_release_asset = "repo_ready-1.0.0.zip";
        result.provenance.archive_hash_sha256 = std::string(64, 'b');
        result.provenance.signature_state = ExtensionPackageSignatureState::Unsigned;
        result.provenance.signature_detail = "Selected asset signature is unsigned.";
        return std::vector<ExtensionLoadResult>{result};
    });

    page.loadSettings();

    auto* summary_label = page.findChild<QLabel*>(fieldObjectName("repo_ready", "summary"));
    ASSERT_NE(summary_label, nullptr);
    EXPECT_TRUE(summary_label->text().contains(QStringLiteral("Source: Repository")));
    EXPECT_TRUE(summary_label->text().contains(QStringLiteral("ID: repo_ready")));

    auto* package_root_label = page.findChild<QLabel*>(fieldObjectName("repo_ready", "packageRoot"));
    ASSERT_NE(package_root_label, nullptr);
    EXPECT_TRUE(labelTextWithoutWrapHints(package_root_label).contains(QStringLiteral("_repository/repo_ready")));

    auto* manifest_label = page.findChild<QLabel*>(fieldObjectName("repo_ready", "manifestPath"));
    ASSERT_NE(manifest_label, nullptr);
    EXPECT_TRUE(labelTextWithoutWrapHints(manifest_label).contains(QStringLiteral("extension.json")));

    auto* runtime_targets_label = page.findChild<QLabel*>(fieldObjectName("repo_ready", "runtimeTargets"));
    ASSERT_NE(runtime_targets_label, nullptr);
    const QString runtime_targets = labelTextWithoutWrapHints(runtime_targets_label);
    EXPECT_TRUE(runtime_targets.contains(QStringLiteral("linux-clang-x86_64")));
    EXPECT_TRUE(runtime_targets.contains(QStringLiteral("windows-msvc-x86_64")));

    auto* validation_label = page.findChild<QLabel*>(fieldObjectName("repo_ready", "validation"));
    ASSERT_NE(validation_label, nullptr);
    EXPECT_TRUE(validation_label->text().contains(QStringLiteral("validation passed"), Qt::CaseInsensitive));
}

TEST_F(ExtensionStatusPageTest, ResetToDefaultsRefreshesTheDiscoverySnapshot)
{
    int call_count = 0;
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([&call_count]() {
        ++call_count;
        std::vector<ExtensionLoadResult> results;
        if (call_count == 1)
        {
            results.push_back(makeAttentionResult("demo_extension", "Demo Extension", "/tmp/extensions/demo_extension",
                                                  ExtensionEnvironmentState::Bootstrapping,
                                                  "Waiting for bundled bootstrap to finish"));
        }
        else
        {
            results.push_back(makeReadyResult("demo_extension", "Demo Extension", "/tmp/extensions/demo_extension",
                                              "/tmp/extensions/demo_extension/.venv", ExtensionPackageSource::Bundled));
        }
        return results;
    });

    page.loadSettings();
    ASSERT_EQ(call_count, 1);
    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Bootstrapping")));

    page.resetToDefaults();
    ASSERT_EQ(call_count, 2);
    EXPECT_TRUE(hasLabelText(page, QStringLiteral("Available")));
    EXPECT_FALSE(page.hasUnsavedChanges());
}

TEST_F(ExtensionStatusPageTest, LoadSettingsRendersRecordedLaunchAndTerminationStatus)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult("demo_extension", "Demo Extension", "/tmp/extensions/demo_extension",
                                      "/tmp/extensions/demo_extension/.venv", ExtensionPackageSource::Bundled);

        ExtensionRuntimeStatusSnapshot runtime_status;
        runtime_status.has_launch_result = true;
        runtime_status.launch_outcome = ExtensionRuntimeLaunchOutcome::Succeeded;
        runtime_status.launch_detail = "Host session launched successfully.";
        runtime_status.has_termination_result = true;
        runtime_status.termination_kind = ExtensionRuntimeTerminationKind::Disconnected;
        runtime_status.termination_detail = "Extension host control channel disconnected";
        result.runtime_status = runtime_status;

        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();

    auto* launch_label = page.findChild<QLabel*>(fieldObjectName("demo_extension", "launch"));
    ASSERT_NE(launch_label, nullptr);
    EXPECT_TRUE(launch_label->text().contains(QStringLiteral("succeeded"), Qt::CaseInsensitive));
    EXPECT_TRUE(launch_label->text().contains(QStringLiteral("Host session launched successfully")));

    auto* crash_label = page.findChild<QLabel*>(fieldObjectName("demo_extension", "crash"));
    ASSERT_NE(crash_label, nullptr);
    EXPECT_TRUE(crash_label->text().contains(QStringLiteral("disconnected"), Qt::CaseInsensitive));
    EXPECT_TRUE(crash_label->text().contains(QStringLiteral("control channel disconnected")));

    auto* recovery_banner = dynamic_cast<an::ui::AnInfoBanner*>(
        page.findChild<QWidget*>(fieldObjectName("demo_extension", "recoveryBanner")));
    ASSERT_NE(recovery_banner, nullptr);
    EXPECT_TRUE(recovery_banner->message().contains(QStringLiteral("relaunch"), Qt::CaseInsensitive));
    EXPECT_TRUE(recovery_banner->message().contains(QStringLiteral("Last Crash / Termination")));
}

// No extension host starts a crash handler - the Lite host links only the protocol library - so the advice after a
// session ended badly sent the reader to crash dumps that do not exist. It names the row that holds the reason, the
// exit code and the host's last output instead.
TEST_F(ExtensionStatusPageTest, TheAdviceAfterABadEndingNamesWhatTheCardRecordsAndNoCrashDump)
{
    for (const ExtensionRuntimeTerminationKind kind :
         {ExtensionRuntimeTerminationKind::Crashed, ExtensionRuntimeTerminationKind::Disconnected,
          ExtensionRuntimeTerminationKind::Unresponsive, ExtensionRuntimeTerminationKind::Failed,
          ExtensionRuntimeTerminationKind::Exited})
    {
        SCOPED_TRACE(static_cast<int>(kind));
        ExtensionStatusPage page(nullptr);
        page.setDiscoveryProvider([kind]() {
            std::vector<ExtensionLoadResult> results;
            auto result = makeReadyResult("demo_extension", "Demo Extension", "/tmp/extensions/demo_extension",
                                          "/tmp/extensions/demo_extension/.venv", ExtensionPackageSource::Bundled);
            ExtensionRuntimeStatusSnapshot runtime_status;
            runtime_status.has_termination_result = true;
            runtime_status.termination_kind = kind;
            runtime_status.termination_detail = "Extension host process ended";
            result.runtime_status = runtime_status;
            results.push_back(std::move(result));
            return results;
        });

        page.loadSettings();

        auto* recovery_banner = dynamic_cast<an::ui::AnInfoBanner*>(
            page.findChild<QWidget*>(fieldObjectName("demo_extension", "recoveryBanner")));
        ASSERT_NE(recovery_banner, nullptr);
        EXPECT_FALSE(recovery_banner->message().contains(QStringLiteral("crash dump"), Qt::CaseInsensitive))
            << recovery_banner->message().toStdString();
        EXPECT_TRUE(recovery_banner->message().contains(QStringLiteral("Last Crash / Termination")))
            << recovery_banner->message().toStdString();
    }
}

TEST_F(ExtensionStatusPageTest, LoadSettingsRendersTerminationDiagnosticsWhenAvailable)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult(
            "demo_extension_diagnostics", "Demo Extension Diagnostics", "/tmp/extensions/demo_extension_diagnostics",
            "/tmp/extensions/demo_extension_diagnostics/.venv", ExtensionPackageSource::Bundled);

        ExtensionRuntimeStatusSnapshot runtime_status;
        runtime_status.has_termination_result = true;
        runtime_status.termination_kind = ExtensionRuntimeTerminationKind::Crashed;
        runtime_status.termination_detail = "Extension host process crashed";
        runtime_status.termination_diagnostics.session_id = "session-123";
        runtime_status.termination_diagnostics.process_exit_code = 134;
        runtime_status.termination_diagnostics.in_flight_request_id = "request-456";
        runtime_status.termination_diagnostics.outstanding_buffer_ids = {"buffer-a", "buffer-b"};
        result.runtime_status = runtime_status;

        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();

    auto* crash_label = page.findChild<QLabel*>(fieldObjectName("demo_extension_diagnostics", "crash"));
    ASSERT_NE(crash_label, nullptr);
    const QString crash_text = labelTextWithoutWrapHints(crash_label);
    EXPECT_TRUE(crash_text.contains(QStringLiteral("session-123")));
    EXPECT_TRUE(crash_text.contains(QStringLiteral("134")));
    EXPECT_TRUE(crash_text.contains(QStringLiteral("request-456")));
    EXPECT_TRUE(crash_text.contains(QStringLiteral("2 outstanding buffer")));
}

TEST_F(ExtensionStatusPageTest, LoadSettingsRendersExtensionSettingsActionFromCachedSchema)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult("custom_ui_demo", "Custom UI Demo", "/tmp/extensions/custom_ui_demo",
                                      "/tmp/extensions/custom_ui_demo/.venv", ExtensionPackageSource::Bundled);
        result.settings_schema =
            makeReadySchema(QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("use_gpu")},
                                                   {QStringLiteral("type"), QStringLiteral("bool")},
                                                   {QStringLiteral("label"), QStringLiteral("Use GPU")},
                                                   {QStringLiteral("default"), true},
                                                   {QStringLiteral("group"), QStringLiteral("Execution")}},
                                       QJsonObject{{QStringLiteral("id"), QStringLiteral("model_path")},
                                                   {QStringLiteral("type"), QStringLiteral("file_path")},
                                                   {QStringLiteral("label"), QStringLiteral("Model Path")},
                                                   {QStringLiteral("default"), QStringLiteral("/tmp/model.onnx")},
                                                   {QStringLiteral("group"), QStringLiteral("Execution")}}});
        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();

    EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_custom_ui_demo")), nullptr);
    EXPECT_FALSE(hasLabelText(page, QStringLiteral("Shared Extension Settings")));

    auto* settings_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_settings_custom_ui_demo"));
    ASSERT_NE(settings_button, nullptr);
    EXPECT_TRUE(settings_button->toolTip().contains(QStringLiteral("shared extension settings"), Qt::CaseInsensitive));
    EXPECT_FALSE(page.openExtensionSettings("custom_ui_demo"));
    EXPECT_FALSE(page.openExtensionSettings("missing_extension"));
}

TEST_F(ExtensionStatusPageTest, OpenExtensionSettingsDialogUsesLoadedSchemaWithoutRediscovery)
{
    ExtensionStatusPage page(nullptr);
    int discovery_calls = 0;
    page.setDiscoveryProvider([&discovery_calls]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult("rc_astro", "RC-Astro XTerminators", "/tmp/extensions/rc_astro",
                                      "/tmp/extensions/rc_astro/.venv", ExtensionPackageSource::Bundled);

        ++discovery_calls;
        if (discovery_calls == 1)
        {
            result.settings_schema =
                makeReadySchema(QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("cli_executable")},
                                                       {QStringLiteral("type"), QStringLiteral("file_path")},
                                                       {QStringLiteral("label"), QStringLiteral("Executable")},
                                                       {QStringLiteral("default"), QStringLiteral("/tmp/rc-astro")}}});
        }
        else
        {
            result.settings_schema =
                makeReadySchema(QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("slow_refresh_marker")},
                                                       {QStringLiteral("type"), QStringLiteral("string")},
                                                       {QStringLiteral("label"), QStringLiteral("Slow Refresh Marker")},
                                                       {QStringLiteral("default"), QStringLiteral("unexpected")}}});
        }

        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();
    ASSERT_EQ(discovery_calls, 1);

    bool saw_fresh_schema = false;
    bool saw_slow_refresh_schema = false;
    QTimer::singleShot(0, [&]() {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal)
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (widget && widget->windowTitle().contains(QStringLiteral("RC-Astro XTerminators Settings")))
                {
                    modal = widget;
                    break;
                }
            }
        }

        saw_fresh_schema = hasSettingsWidgetById(modal, QStringLiteral("cli_executable"));
        saw_slow_refresh_schema = hasSettingsWidgetById(modal, QStringLiteral("slow_refresh_marker"));
        if (auto* dialog = qobject_cast<QDialog*>(modal))
        {
            dialog->reject();
        }
        else if (modal)
        {
            modal->close();
        }
    });

    EXPECT_TRUE(page.openExtensionSettingsDialog("rc_astro"));
    EXPECT_EQ(discovery_calls, 1);
    EXPECT_TRUE(saw_fresh_schema);
    EXPECT_FALSE(saw_slow_refresh_schema);
}

TEST_F(ExtensionStatusPageTest, EmptySharedExtensionSettingsSchemaDoesNotRenderBlankSection)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult("veralux", "VeraLux Suite", "/tmp/extensions/veralux",
                                      "/tmp/extensions/veralux/.venv", ExtensionPackageSource::Repository);
        auto schema = makeReadySchema(QJsonArray{});
        schema.process_schemas["VeraLuxAlchemyExtension"] =
            QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("strength")},
                                   {QStringLiteral("type"), QStringLiteral("double")},
                                   {QStringLiteral("label"), QStringLiteral("Strength")},
                                   {QStringLiteral("default"), 0.5}}};
        result.settings_schema = std::move(schema);
        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();

    EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionSettingsSection_veralux")), nullptr);
    EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_veralux")), nullptr);
    EXPECT_EQ(page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_settings_veralux")), nullptr);
    EXPECT_FALSE(page.openExtensionSettings("veralux"));
}

TEST_F(ExtensionStatusPageTest, ConditionalExtensionSettingsSchemaRendersSettingsActionOnly)
{
    ExtensionStatusPage page(nullptr);
    page.setDiscoveryProvider([]() {
        std::vector<ExtensionLoadResult> results;
        auto result = makeReadyResult("rt_preview_demo", "RT Preview Demo", "/tmp/extensions/rt_preview_demo",
                                      "/tmp/extensions/rt_preview_demo/.venv", ExtensionPackageSource::Bundled);
        result.settings_schema = makeReadySchema(
            QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("advanced_mode")},
                                   {QStringLiteral("type"), QStringLiteral("bool")},
                                   {QStringLiteral("label"), QStringLiteral("Advanced Mode")},
                                   {QStringLiteral("default"), false}},
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("expert_path")},
                                   {QStringLiteral("type"), QStringLiteral("dir_path")},
                                   {QStringLiteral("label"), QStringLiteral("Expert Folder")},
                                   {QStringLiteral("default"), QStringLiteral("/tmp/expert")},
                                   {QStringLiteral("visible_when"), QStringLiteral("advanced_mode == true")}},
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("quality_override")},
                                   {QStringLiteral("type"), QStringLiteral("string")},
                                   {QStringLiteral("label"), QStringLiteral("Quality Override")},
                                   {QStringLiteral("default"), QStringLiteral("balanced")},
                                   {QStringLiteral("enabled_when"), QStringLiteral("advanced_mode == true")}}});
        results.push_back(std::move(result));
        return results;
    });

    page.loadSettings();

    EXPECT_EQ(page.findChild<QWidget*>(QStringLiteral("anExtensionSettingsHost_rt_preview_demo")), nullptr);

    auto* settings_button =
        page.findChild<QPushButton*>(QStringLiteral("anExtensionStatusButton_settings_rt_preview_demo"));
    ASSERT_NE(settings_button, nullptr);
    EXPECT_TRUE(settings_button->toolTip().contains(QStringLiteral("configure"), Qt::CaseInsensitive));
}
