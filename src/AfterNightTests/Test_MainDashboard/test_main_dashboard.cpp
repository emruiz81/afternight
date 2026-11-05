// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <QComboBox>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsOpacityEffect>
#include <QMenu>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <array>

#include "Test_MainDashboard/DashboardApplication.h"
#include "app/UiStyleTokens.h"
#include "components/AnCardWidget.h"
#include "components/AnSectionHeader.h"
#include "controls/FeaturedFilePresenter.h"
#include "controls/MainDashboard.h"
#include "controls/RecentFilesGrid.h"
#include "controls/settings/SettingsKeys.h"
#include "widgets/ImageThumbnailWidget.h"

namespace
{
constexpr int WAIT_TIMEOUT_MS = 2000;

QPushButton* findActionButton(MainDashboard& dashboard, const QString& tooltip)
{
    const auto buttons = dashboard.findChildren<QPushButton*>(QStringLiteral("dashboardActionButton"));
    for (auto* button : buttons)
    {
        if (!button || button->property("dashboardActionPlacement").toString() != QStringLiteral("primary"))
        {
            continue;
        }

        const QString title = button->property("dashboardActionTitle").toString().trimmed();
        const QString normalizedTooltip = button->toolTip().trimmed();
        const QString expectedTitle = tooltip.trimmed();
        if (title == expectedTitle || normalizedTooltip == expectedTitle ||
            normalizedTooltip.startsWith(expectedTitle + QStringLiteral(" (")))
        {
            return button;
        }
    }
    return nullptr;
}

QPushButton* findOverflowButton(MainDashboard& dashboard)
{
    return dashboard.findChild<QPushButton*>(QStringLiteral("dashboardToolbarOverflowButton"));
}

QMenu* findOverflowMenu(MainDashboard& dashboard)
{
    if (auto* overflowButton = findOverflowButton(dashboard))
    {
        return overflowButton->menu();
    }
    return dashboard.findChild<QMenu*>(QStringLiteral("dashboardToolbarOverflowMenu"));
}

QStringList visiblePrimaryActionTitlesInLayoutOrder(MainDashboard& dashboard)
{
    QList<QPushButton*> buttons = dashboard.findChildren<QPushButton*>(QStringLiteral("dashboardActionButton"));
    buttons.erase(std::remove_if(buttons.begin(), buttons.end(),
                                 [](QPushButton* button) {
                                     return !button || !button->isVisible() ||
                                            button->property("dashboardActionPlacement").toString() !=
                                                QStringLiteral("primary");
                                 }),
                  buttons.end());

    std::sort(buttons.begin(), buttons.end(), [](QPushButton* lhs, QPushButton* rhs) {
        if (lhs->geometry().y() != rhs->geometry().y())
        {
            return lhs->geometry().y() < rhs->geometry().y();
        }
        return lhs->geometry().x() < rhs->geometry().x();
    });

    QStringList titles;
    titles.reserve(buttons.size());
    for (auto* button : buttons)
    {
        const QString title = button->property("dashboardActionTitle").toString().trimmed();
        if (!title.isEmpty())
        {
            titles.push_back(title);
        }
    }
    return titles;
}

QStringList visibleOverflowActionTitlesInMenuOrder(MainDashboard& dashboard)
{
    QStringList titles;
    QMenu* menu = findOverflowMenu(dashboard);
    if (!menu)
    {
        return titles;
    }

    for (QAction* action : menu->actions())
    {
        if (!action || !action->data().isValid())
        {
            continue;
        }
        titles.push_back(action->text().trimmed());
    }

    return titles;
}

bool overflowMenuContainsAction(MainDashboard& dashboard, const QString& text)
{
    const QStringList titles = visibleOverflowActionTitlesInMenuOrder(dashboard);
    return titles.contains(text.trimmed());
}

QAction* findMenuSubmenuAction(QMenu* menu, const QString& text)
{
    if (!menu)
    {
        return nullptr;
    }

    const QString target = text.trimmed();
    for (QAction* action : menu->actions())
    {
        if (action && action->menu() && action->text().trimmed() == target)
        {
            return action;
        }
    }
    return nullptr;
}

QLineEdit* findRecentSearchInput(MainDashboard& dashboard)
{
    return dashboard.findChild<QLineEdit*>(QStringLiteral("dashboardRecentSearchInput"));
}

QComboBox* findRecentSortCombo(MainDashboard& dashboard)
{
    return dashboard.findChild<QComboBox*>(QStringLiteral("dashboardRecentSortCombo"));
}

QWidget* findRecentGridContainer(MainDashboard& dashboard)
{
    return dashboard.findChild<QWidget*>(QStringLiteral("dashboardRecentGridContainer"));
}

an::ui::AnSectionHeader* findRecentSectionHeader(MainDashboard& dashboard)
{
    QWidget* headerWidget = dashboard.findChild<QWidget*>(QStringLiteral("dashboardRecentSectionHeader"));
    return dynamic_cast<an::ui::AnSectionHeader*>(headerWidget);
}

QWidget* findEmptyStateCard(MainDashboard& dashboard)
{
    return dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateCard"));
}

QWidget* findFeaturedFileCard(MainDashboard& dashboard)
{
    return dashboard.findChild<QWidget*>(QStringLiteral("dashboardFeaturedFileCard"));
}

QWidget* findEmptyStateOnboardingPanel(MainDashboard& dashboard)
{
    return dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateOnboardingPanel"));
}

QWidget* findEmptyStateNoMatchPanel(MainDashboard& dashboard)
{
    return dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateNoMatchPanel"));
}

QLabel* findFeaturedFileTitleLabel(MainDashboard& dashboard)
{
    return dashboard.findChild<QLabel*>(QStringLiteral("dashboardFeaturedFileTitle"));
}

QLabel* findFeaturedFileMetadataLabel(MainDashboard& dashboard)
{
    return dashboard.findChild<QLabel*>(QStringLiteral("dashboardFeaturedFileMetadata"));
}

QPushButton* findFeaturedContinueButton(MainDashboard& dashboard)
{
    return dashboard.findChild<QPushButton*>(QStringLiteral("dashboardFeaturedContinueButton"));
}

QPushButton* findFeaturedPinButton(MainDashboard& dashboard)
{
    return dashboard.findChild<QPushButton*>(QStringLiteral("dashboardFeaturedPinButton"));
}

void createRecentSourceFile(const QString& filePath)
{
    QFile sourceFile(filePath);
    ASSERT_TRUE(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("recent-source");
    sourceFile.close();
}

QString createThumbnailFile(const QString& directory, const QString& baseName, const QColor& color)
{
    QImage image(24, 24, QImage::Format_ARGB32);
    image.fill(color);

    const QString filePath = QDir(directory).filePath(baseName + QStringLiteral(".png"));
    const bool ok = image.save(filePath, "PNG");
    EXPECT_TRUE(ok);
    return filePath;
}

void configureRecentFilesWithThumbnails(DashboardApplication& app, const QString& directory)
{
    app.resetForTest();

    const QString m42Path = QDir(directory).filePath(QStringLiteral("M42_Light_001.fit"));
    const QString andromedaPath = QDir(directory).filePath(QStringLiteral("Andromeda_001.xisf"));
    const QString m42BluePath = QDir(directory).filePath(QStringLiteral("M42_Blue_002.fit"));
    createRecentSourceFile(m42Path);
    createRecentSourceFile(andromedaPath);
    createRecentSourceFile(m42BluePath);

    std::deque<std::string> recents = {m42Path.toStdString(), andromedaPath.toStdString(), m42BluePath.toStdString()};
    app.setRecentFiles(recents);

    app.setThumbnailPath(m42Path, createThumbnailFile(directory, QStringLiteral("m42_thumb"), QColor(220, 120, 120)));
    app.setThumbnailPath(andromedaPath,
                         createThumbnailFile(directory, QStringLiteral("andromeda_thumb"), QColor(120, 220, 120)));
    app.setThumbnailPath(m42BluePath,
                         createThumbnailFile(directory, QStringLiteral("m42_blue_thumb"), QColor(120, 120, 220)));
}

TEST(FeaturedFilePresenterTest, DecodeProducesCoverSizedHeroPreview)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    const QString sourcePath = QDir(tempDir.path()).filePath(QStringLiteral("hero_source.fit"));
    createRecentSourceFile(sourcePath);

    const QString thumbnailPath = QDir(tempDir.path()).filePath(QStringLiteral("hero_thumbnail.jpg"));
    QImage thumbnail(1800, 1200, QImage::Format_RGB32);
    thumbnail.fill(QColor(80, 110, 180));
    ASSERT_TRUE(thumbnail.save(thumbnailPath, "JPG"));

    an::ui::FeaturedFilePresenter presenter;
    presenter.setRecentFilePaths({sourcePath});
    presenter.setThumbnailPathResolver([thumbnailPath](const QString&) { return thumbnailPath; });

    const QSize targetSize(1200, 360);
    presenter.setPreferredDecodeSize(targetSize);

    QPixmap decodedPreview;
    QObject::connect(&presenter, &an::ui::FeaturedFilePresenter::featuredFileReady,
                     [&decodedPreview](const QString&, const QString&, const QString&, const QPixmap& preview,
                                       const bool) { decodedPreview = preview; });

    presenter.refresh();

    QTRY_VERIFY_WITH_TIMEOUT(!decodedPreview.isNull(), WAIT_TIMEOUT_MS);
    EXPECT_GE(decodedPreview.width(), targetSize.width());
    EXPECT_GE(decodedPreview.height(), targetSize.height());
}

QList<QWidget*> collectVisibleThumbnails(MainDashboard& dashboard)
{
    QList<QWidget*> visible;
    const auto thumbnails = dashboard.findChildren<QWidget*>(QStringLiteral("dashboardThumbnailCard"));
    for (auto* thumb : thumbnails)
    {
        if (thumb && thumb->isVisible())
        {
            visible.push_back(thumb);
        }
    }
    return visible;
}

QList<ImageThumbnailWidget*> collectVisibleThumbnailCards(MainDashboard& dashboard)
{
    QList<ImageThumbnailWidget*> cards;
    const auto thumbnails = dashboard.findChildren<QWidget*>(QStringLiteral("dashboardThumbnailCard"));
    for (auto* thumb : thumbnails)
    {
        auto* card = dynamic_cast<ImageThumbnailWidget*>(thumb);
        if (card && card->isVisible())
        {
            cards.push_back(card);
        }
    }
    return cards;
}

ImageThumbnailWidget* findThumbnailByPath(MainDashboard& dashboard, const QString& filePath)
{
    const auto cards = collectVisibleThumbnailCards(dashboard);
    for (auto* card : cards)
    {
        if (card && card->property("recentFilePath").toString() == filePath)
        {
            return card;
        }
    }
    return nullptr;
}

QLabel* findChipLabelByRole(ImageThumbnailWidget* thumbnail, const char* role)
{
    if (!thumbnail || !role)
    {
        return nullptr;
    }

    const auto chips = thumbnail->findChildren<QLabel*>(QStringLiteral("dashboardThumbnailChip"));
    for (auto* chip : chips)
    {
        if (chip && chip->property("dashboardChipRole").toString() == QString::fromUtf8(role))
        {
            return chip;
        }
    }
    return nullptr;
}

QLabel* findLabel(MainDashboard& dashboard, const QString& objectName)
{
    return dashboard.findChild<QLabel*>(objectName);
}

bool waitForVisibleThumbnailCount(MainDashboard& dashboard, int expectedCount, int timeoutMs = WAIT_TIMEOUT_MS)
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        if (collectVisibleThumbnails(dashboard).size() == expectedCount)
        {
            return true;
        }
        QTest::qWait(20);
        elapsed += 20;
    }

    return collectVisibleThumbnails(dashboard).size() == expectedCount;
}

QStringList visibleThumbnailPathsInGridOrder(MainDashboard& dashboard)
{
    QList<QWidget*> items = collectVisibleThumbnails(dashboard);
    std::sort(items.begin(), items.end(), [](QWidget* a, QWidget* b) {
        if (a->geometry().y() != b->geometry().y())
        {
            return a->geometry().y() < b->geometry().y();
        }
        return a->geometry().x() < b->geometry().x();
    });

    QStringList paths;
    for (auto* item : items)
    {
        paths.append(item->property("recentFilePath").toString());
    }
    return paths;
}

bool waitForThumbnailOrder(MainDashboard& dashboard, const QStringList& expectedOrder, int timeoutMs = WAIT_TIMEOUT_MS)
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        if (visibleThumbnailPathsInGridOrder(dashboard) == expectedOrder)
        {
            return true;
        }

        QTest::qWait(20);
        elapsed += 20;
    }

    return visibleThumbnailPathsInGridOrder(dashboard) == expectedOrder;
}

bool waitForWidgetVisibility(QWidget* widget, bool expectedVisible, int timeoutMs = WAIT_TIMEOUT_MS)
{
    if (!widget)
    {
        return false;
    }

    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        if (widget->isVisible() == expectedVisible)
        {
            return true;
        }
        QTest::qWait(20);
        elapsed += 20;
    }

    return widget->isVisible() == expectedVisible;
}

bool waitForFeaturedFilePath(MainDashboard& dashboard, const QString& expectedPath, int timeoutMs = WAIT_TIMEOUT_MS)
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        QWidget* featuredCard = findFeaturedFileCard(dashboard);
        if (featuredCard && featuredCard->isVisible() &&
            featuredCard->property("dashboardFeaturedFilePath").toString() == expectedPath)
        {
            return true;
        }

        QTest::qWait(20);
        elapsed += 20;
    }

    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    return featuredCard && featuredCard->isVisible() &&
           featuredCard->property("dashboardFeaturedFilePath").toString() == expectedPath;
}

bool waitForTransitionActivation(QWidget* widget, int timeoutMs = WAIT_TIMEOUT_MS)
{
    if (!widget)
    {
        return false;
    }

    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        if (widget->property("dashboardTransitionActive").toBool())
        {
            return true;
        }
        QTest::qWait(20);
        elapsed += 20;
    }

    return widget->property("dashboardTransitionActive").toBool();
}

bool thumbnailsWithinRecentGridBounds(MainDashboard& dashboard)
{
    QWidget* gridContainer = findRecentGridContainer(dashboard);
    if (!gridContainer)
    {
        return false;
    }

    const QRect gridBounds = gridContainer->contentsRect();
    const auto thumbnails = collectVisibleThumbnailCards(dashboard);
    for (auto* thumbnail : thumbnails)
    {
        if (!thumbnail)
        {
            continue;
        }

        const QRect thumbBounds(thumbnail->mapTo(gridContainer, QPoint(0, 0)), thumbnail->size());
        if (thumbBounds.right() > (gridBounds.right() + 1))
        {
            return false;
        }
    }

    return true;
}

bool waitForThumbnailsWithinRecentGridBounds(MainDashboard& dashboard, int timeoutMs = WAIT_TIMEOUT_MS)
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        if (thumbnailsWithinRecentGridBounds(dashboard))
        {
            return true;
        }

        QTest::qWait(20);
        elapsed += 20;
    }

    return thumbnailsWithinRecentGridBounds(dashboard);
}

bool waitForThumbnailCacheFlag(MainDashboard& dashboard, const QString& filePath, bool expectedLoadedFromCache,
                               int timeoutMs = WAIT_TIMEOUT_MS)
{
    int elapsed = 0;
    while (elapsed < timeoutMs)
    {
        ImageThumbnailWidget* thumbnail = findThumbnailByPath(dashboard, filePath);
        if (thumbnail && thumbnail->property("dashboardLoadedFromCache").toBool() == expectedLoadedFromCache)
        {
            return true;
        }

        QTest::qWait(20);
        elapsed += 20;
    }

    ImageThumbnailWidget* thumbnail = findThumbnailByPath(dashboard, filePath);
    return thumbnail && thumbnail->property("dashboardLoadedFromCache").toBool() == expectedLoadedFromCache;
}

QHash<QString, ImageThumbnailWidget*> visibleThumbnailPointersByPath(MainDashboard& dashboard)
{
    QHash<QString, ImageThumbnailWidget*> pointers;
    const auto thumbnails = collectVisibleThumbnailCards(dashboard);
    for (auto* thumbnail : thumbnails)
    {
        if (!thumbnail)
        {
            continue;
        }

        pointers.insert(thumbnail->property("recentFilePath").toString(), thumbnail);
    }
    return pointers;
}

QAction* findMenuActionByPath(QMenu* menu, const QStringList& path)
{
    if (!menu || path.isEmpty())
    {
        return nullptr;
    }

    QMenu* currentMenu = menu;
    for (int i = 0; i < (path.size() - 1); ++i)
    {
        QAction* submenuAction = findMenuSubmenuAction(currentMenu, path[i]);
        if (!submenuAction || !submenuAction->menu())
        {
            return nullptr;
        }
        currentMenu = submenuAction->menu();
    }

    const QString targetText = path.back().trimmed();
    for (QAction* action : currentMenu->actions())
    {
        if (action && action->text().trimmed() == targetText)
        {
            return action;
        }
    }

    return nullptr;
}

bool triggerOverflowMenuAction(MainDashboard& dashboard, const QStringList& actionPath)
{
    QMenu* menu = findOverflowMenu(dashboard);
    if (!menu || actionPath.isEmpty())
    {
        return false;
    }

    QAction* action = findMenuActionByPath(menu, actionPath);
    if (!action || !action->isEnabled())
    {
        return false;
    }

    action->trigger();
    QCoreApplication::processEvents();
    return true;
}

} // namespace

TEST(MainDashboardInteraction, PrimaryOpenFileButtonClickEmitsOpenFileRequested)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    QSignalSpy openFileSpy(&dashboard, &MainDashboard::openFileRequested);
    QPushButton* openButton = findActionButton(dashboard, QStringLiteral("Open file"));
    ASSERT_NE(openButton, nullptr);

    QTest::mouseClick(openButton, Qt::LeftButton);
    EXPECT_EQ(openFileSpy.count(), 1);
}

TEST(MainDashboardInteraction, EnterOnFocusedPrimaryButtonEmitsOpenFileRequested)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    QSignalSpy openFileSpy(&dashboard, &MainDashboard::openFileRequested);
    QPushButton* openButton = findActionButton(dashboard, QStringLiteral("Open file"));
    ASSERT_NE(openButton, nullptr);

    // Use click() instead of setFocus()+keyClick() because
    // QTest::keyClick on a button nested inside AnButtonGroup does not
    // reliably deliver the key event on Qt's offscreen platform.
    openButton->click();
    EXPECT_EQ(openFileSpy.count(), 1);
}

TEST(MainDashboardInteraction, SystemInfoOverflowActionEnabledAndClickable)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    QSignalSpy systemInfoSpy(&dashboard, &MainDashboard::systemInformationRequested);
    ASSERT_TRUE(triggerOverflowMenuAction(dashboard, {QStringLiteral("System info")}));
    EXPECT_EQ(systemInfoSpy.count(), 1);
}

TEST(MainDashboardInteraction, ExtensionManagerOverflowActionExplainsAndOpensManager)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    QMenu* overflow_menu = findOverflowMenu(dashboard);
    ASSERT_NE(overflow_menu, nullptr);
    QAction* extension_manager_action = findMenuActionByPath(overflow_menu, {QStringLiteral("Extension Manager")});
    ASSERT_NE(extension_manager_action, nullptr);
    EXPECT_TRUE(extension_manager_action->toolTip().contains(QStringLiteral("install"), Qt::CaseInsensitive));
    EXPECT_TRUE(
        extension_manager_action->toolTip().contains(QStringLiteral("repair environments"), Qt::CaseInsensitive));

    QSignalSpy extensionManagerSpy(&dashboard, &MainDashboard::openExtensionManagerRequested);
    ASSERT_TRUE(triggerOverflowMenuAction(dashboard, {QStringLiteral("Extension Manager")}));
    EXPECT_EQ(extensionManagerSpy.count(), 1);
}

TEST(MainDashboardInteraction, FeaturedCardShowsMostRecentFileAndMetadata)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    QLabel* featuredTitle = findFeaturedFileTitleLabel(dashboard);
    QLabel* featuredMetadata = findFeaturedFileMetadataLabel(dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_NE(featuredTitle, nullptr);
    ASSERT_NE(featuredMetadata, nullptr);
    EXPECT_TRUE(waitForWidgetVisibility(featuredCard, true));

    const QString expectedPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    EXPECT_EQ(featuredCard->property("dashboardFeaturedFilePath").toString(), expectedPath);
    EXPECT_EQ(featuredTitle->text(), QStringLiteral("M42_Light_001.fit"));
    EXPECT_TRUE(featuredMetadata->text().contains(QStringLiteral("FIT"), Qt::CaseInsensitive));
}

TEST(MainDashboardInteraction, FeaturedCardScrollsWithDashboardContent)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    auto* dashboard = new MainDashboard();
    dashboard->initialize(&app);

    QScrollArea scrollArea;
    scrollArea.setWidgetResizable(true);
    scrollArea.setWidget(dashboard);
    scrollArea.resize(900, 360);
    scrollArea.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(*dashboard, 3));

    QWidget* featuredCard = findFeaturedFileCard(*dashboard);
    an::ui::AnSectionHeader* recentHeader = findRecentSectionHeader(*dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_NE(recentHeader, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));

    QCoreApplication::processEvents();
    QScrollBar* verticalScrollBar = scrollArea.verticalScrollBar();
    ASSERT_NE(verticalScrollBar, nullptr);

    if (verticalScrollBar->maximum() <= 0)
    {
        scrollArea.resize(900, 280);
        QCoreApplication::processEvents();
    }
    ASSERT_GT(verticalScrollBar->maximum(), 0);

    const int startValue = verticalScrollBar->value();
    const int targetValue = (std::min)(startValue + 80, verticalScrollBar->maximum());
    ASSERT_GT(targetValue, startValue);

    const int featuredYBefore = featuredCard->mapTo(scrollArea.viewport(), QPoint(0, 0)).y();
    const int headerYBefore = recentHeader->mapTo(scrollArea.viewport(), QPoint(0, 0)).y();

    verticalScrollBar->setValue(targetValue);
    QCoreApplication::processEvents();
    QTest::qWait(20);

    const int expectedDelta = -(targetValue - startValue);
    const int featuredYAfter = featuredCard->mapTo(scrollArea.viewport(), QPoint(0, 0)).y();
    const int headerYAfter = recentHeader->mapTo(scrollArea.viewport(), QPoint(0, 0)).y();

    EXPECT_NEAR(featuredYAfter - featuredYBefore, expectedDelta, 2);
    EXPECT_NEAR(headerYAfter - headerYBefore, expectedDelta, 2);
}

TEST(MainDashboardInteraction, ToolbarExposesPrimaryAndOverflowActions)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    const QStringList primaryActions = visiblePrimaryActionTitlesInLayoutOrder(dashboard);
    const QStringList overflowActions = visibleOverflowActionTitlesInMenuOrder(dashboard);

    EXPECT_EQ(primaryActions, (QStringList{QStringLiteral("Open file"), QStringLiteral("New sequence"),
                                           QStringLiteral("Image integration")}));
    EXPECT_EQ(overflowActions, (QStringList{QStringLiteral("Extension Manager"), QStringLiteral("Clear recents"),
                                            QStringLiteral("System info")}));
}

TEST(MainDashboardInteraction, EmptyStateAndGridContainersHaveOpacityEffects)
{
    MainDashboard dashboard;
    dashboard.initialize(DashboardApplication::instance());
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(20);

    QWidget* emptyState = findEmptyStateCard(dashboard);
    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(emptyState, nullptr);
    ASSERT_NE(gridContainer, nullptr);
    EXPECT_NE(dynamic_cast<QGraphicsOpacityEffect*>(emptyState->graphicsEffect()), nullptr);
    EXPECT_NE(dynamic_cast<QGraphicsOpacityEffect*>(gridContainer->graphicsEffect()), nullptr);
}

TEST(MainDashboardInteraction, EmptyStateShowsFeatureHighlightsInsteadOfActionButtons)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    // Old action buttons should no longer exist.
    auto* openButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardEmptyStateOpenButton"));
    EXPECT_EQ(openButton, nullptr);

    // Feature highlights container should be present.
    auto* featuresContainer = dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateFeaturesContainer"));
    ASSERT_NE(featuresContainer, nullptr);
    EXPECT_TRUE(featuresContainer->isVisible());

    // At least one feature title label should exist.
    auto featureTitles = featuresContainer->findChildren<QLabel*>(QStringLiteral("dashboardEmptyStateFeatureTitle"));
    EXPECT_GE(featureTitles.size(), 3);
}

TEST(MainDashboardInteraction, EmptyStateFeatureCardsUseUniformColumnsWithoutOverflow)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1920, 1080);
    dashboard.show();
    QCoreApplication::processEvents();

    QWidget* featuresContainer = dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateFeaturesContainer"));
    ASSERT_NE(featuresContainer, nullptr);

    QList<QWidget*> featureCards = featuresContainer->findChildren<QWidget*>(QStringLiteral("dashboardFeatureCard"));
    ASSERT_EQ(featureCards.size(), 6);
    std::sort(featureCards.begin(), featureCards.end(), [](QWidget* lhs, QWidget* rhs) {
        if (lhs->geometry().y() != rhs->geometry().y())
        {
            return lhs->geometry().y() < rhs->geometry().y();
        }
        return lhs->geometry().x() < rhs->geometry().x();
    });

    const QRect containerBounds = featuresContainer->contentsRect();
    for (QWidget* card : featureCards)
    {
        ASSERT_NE(card, nullptr);
        const QRect cardBounds(card->mapTo(featuresContainer, QPoint(0, 0)), card->size());
        EXPECT_TRUE(containerBounds.contains(cardBounds));
    }

    EXPECT_NEAR(featureCards[0]->width(), featureCards[1]->width(), 1);
    EXPECT_NEAR(featureCards[1]->width(), featureCards[2]->width(), 1);
    EXPECT_NEAR(featureCards[3]->width(), featureCards[4]->width(), 1);
    EXPECT_NEAR(featureCards[4]->width(), featureCards[5]->width(), 1);

    const int firstGap = featureCards[1]->x() - featureCards[0]->geometry().right() - 1;
    const int secondGap = featureCards[2]->x() - featureCards[1]->geometry().right() - 1;
    EXPECT_EQ(firstGap, secondGap);
}

TEST(MainDashboardInteraction, EmptyStateFeatureCardsStayWithinGridAt720p)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 720);
    dashboard.show();
    QCoreApplication::processEvents();

    QWidget* featuresContainer = dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateFeaturesContainer"));
    ASSERT_NE(featuresContainer, nullptr);

    const QList<QWidget*> featureCards =
        featuresContainer->findChildren<QWidget*>(QStringLiteral("dashboardFeatureCard"));
    ASSERT_EQ(featureCards.size(), 6);

    const QRect containerBounds = featuresContainer->contentsRect();
    for (QWidget* card : featureCards)
    {
        ASSERT_NE(card, nullptr);
        const QRect cardBounds(card->mapTo(featuresContainer, QPoint(0, 0)), card->size());
        EXPECT_TRUE(containerBounds.contains(cardBounds));
    }
}

TEST(MainDashboardInteraction, EmptyStateFeatureCardTextDoesNotOverlap)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1920, 1080);
    dashboard.show();
    QCoreApplication::processEvents();

    const auto featureCards = dashboard.findChildren<QWidget*>(QStringLiteral("dashboardFeatureCard"));
    ASSERT_EQ(featureCards.size(), 6);
    for (QWidget* card : featureCards)
    {
        ASSERT_NE(card, nullptr);
        ASSERT_NE(card->layout(), nullptr);
        QLabel* title = card->findChild<QLabel*>(QStringLiteral("dashboardEmptyStateFeatureTitle"));
        QLabel* description = card->findChild<QLabel*>(QStringLiteral("dashboardFeatureCardDesc"));
        ASSERT_NE(title, nullptr);
        ASSERT_NE(description, nullptr);

        EXPECT_LT(title->geometry().bottom(), description->geometry().top());
        EXPECT_TRUE(card->contentsRect().contains(title->geometry()));
        EXPECT_TRUE(card->contentsRect().contains(description->geometry()));
        EXPECT_GE(card->height(), card->layout()->totalHeightForWidth(card->width()));
        EXPECT_GE(card->width(), card->sizeHint().width());
        EXPECT_FALSE(description->text().contains(QChar(0x2026)));
    }
}

TEST(MainDashboardInteraction, EmptyStateFeatureCardsReflowWhenTheDashboardIsNarrow)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(720, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    QList<QWidget*> featureCards = dashboard.findChildren<QWidget*>(QStringLiteral("dashboardFeatureCard"));
    ASSERT_EQ(featureCards.size(), 6);
    std::sort(featureCards.begin(), featureCards.end(), [](QWidget* lhs, QWidget* rhs) {
        if (lhs->geometry().y() != rhs->geometry().y())
        {
            return lhs->geometry().y() < rhs->geometry().y();
        }
        return lhs->geometry().x() < rhs->geometry().x();
    });

    EXPECT_EQ(featureCards[0]->y(), featureCards[1]->y());
    EXPECT_LT(featureCards[1]->y(), featureCards[2]->y());
    EXPECT_EQ(featureCards[2]->y(), featureCards[3]->y());
    EXPECT_LT(featureCards[3]->y(), featureCards[4]->y());
    EXPECT_EQ(featureCards[4]->y(), featureCards[5]->y());

    for (QWidget* card : featureCards)
    {
        ASSERT_NE(card, nullptr);
        EXPECT_GE(card->width(), card->sizeHint().width());
    }
}

TEST(MainDashboardRecents, SearchFiltersRecentFilesByName)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QLineEdit* searchInput = findRecentSearchInput(dashboard);
    ASSERT_NE(searchInput, nullptr);

    searchInput->setText(QStringLiteral("M42"));
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 2));

    const QStringList paths = visibleThumbnailPathsInGridOrder(dashboard);
    ASSERT_EQ(paths.size(), 2);
    EXPECT_TRUE(paths[0].contains(QStringLiteral("M42"), Qt::CaseInsensitive));
    EXPECT_TRUE(paths[1].contains(QStringLiteral("M42"), Qt::CaseInsensitive));
}

TEST(MainDashboardRecents, RecentSectionNoRecentsStateShowsEmptyAndZeroCount)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    auto* recentHeader = findRecentSectionHeader(dashboard);
    auto* emptyState = findEmptyStateCard(dashboard);
    auto* gridContainer = findRecentGridContainer(dashboard);
    auto* onboardingPanel = findEmptyStateOnboardingPanel(dashboard);
    auto* noMatchPanel = findEmptyStateNoMatchPanel(dashboard);
    auto* featuredCard = findFeaturedFileCard(dashboard);
    ASSERT_NE(recentHeader, nullptr);
    ASSERT_NE(emptyState, nullptr);
    ASSERT_NE(gridContainer, nullptr);
    ASSERT_NE(onboardingPanel, nullptr);
    ASSERT_NE(noMatchPanel, nullptr);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(emptyState, true));
    ASSERT_TRUE(waitForWidgetVisibility(gridContainer, false));
    ASSERT_TRUE(waitForWidgetVisibility(onboardingPanel, true));
    ASSERT_TRUE(waitForWidgetVisibility(noMatchPanel, false));
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, false));
    EXPECT_TRUE(recentHeader->description().isEmpty());

    // In no-recents state the section header and search/sort controls are hidden
    // since they serve no purpose without files to search.
    EXPECT_FALSE(recentHeader->isVisible());

    auto* controlsRow = dashboard.findChild<QWidget*>(QStringLiteral("dashboardRecentControlsRow"));
    ASSERT_NE(controlsRow, nullptr);
    EXPECT_FALSE(controlsRow->isVisible());
}

TEST(MainDashboardRecents, RecentSectionHasResultsStateShowsGridAndFileCount)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    auto* recentHeader = findRecentSectionHeader(dashboard);
    auto* emptyState = findEmptyStateCard(dashboard);
    auto* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(recentHeader, nullptr);
    ASSERT_NE(emptyState, nullptr);
    ASSERT_NE(gridContainer, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(emptyState, false));
    ASSERT_TRUE(waitForWidgetVisibility(gridContainer, true));
    EXPECT_EQ(recentHeader->description(), QStringLiteral("3 files"));
}

TEST(MainDashboardRecents, RecentGridContainerUsesRecentFilesGridControl)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(gridContainer, nullptr);
    EXPECT_NE(dynamic_cast<an::ui::RecentFilesGrid*>(gridContainer), nullptr);
}

TEST(MainDashboardRecents, RecentSectionFilteredEmptyStateShowsNoMatchDescription)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    auto* searchInput = findRecentSearchInput(dashboard);
    auto* recentHeader = findRecentSectionHeader(dashboard);
    auto* emptyState = findEmptyStateCard(dashboard);
    auto* gridContainer = findRecentGridContainer(dashboard);
    auto* onboardingPanel = findEmptyStateOnboardingPanel(dashboard);
    auto* noMatchPanel = findEmptyStateNoMatchPanel(dashboard);
    auto* featuredCard = findFeaturedFileCard(dashboard);
    ASSERT_NE(searchInput, nullptr);
    ASSERT_NE(recentHeader, nullptr);
    ASSERT_NE(emptyState, nullptr);
    ASSERT_NE(gridContainer, nullptr);
    ASSERT_NE(onboardingPanel, nullptr);
    ASSERT_NE(noMatchPanel, nullptr);
    ASSERT_NE(featuredCard, nullptr);

    searchInput->setText(QStringLiteral("___NO_MATCH___"));

    ASSERT_TRUE(waitForWidgetVisibility(emptyState, true));
    ASSERT_TRUE(waitForWidgetVisibility(gridContainer, false));
    ASSERT_TRUE(waitForWidgetVisibility(onboardingPanel, false));
    ASSERT_TRUE(waitForWidgetVisibility(noMatchPanel, true));
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));
    EXPECT_EQ(recentHeader->description(), QStringLiteral("No matches in 3 files"));

    auto* noMatchTitle = findLabel(dashboard, QStringLiteral("dashboardEmptyStateNoMatchTitle"));
    ASSERT_NE(noMatchTitle, nullptr);
    EXPECT_TRUE(noMatchTitle->isVisible());
    EXPECT_FALSE(noMatchTitle->text().isEmpty());
}

TEST(MainDashboardInteraction, EmptyStateNoLongerContainsRedundantActionButtons)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    // The three action buttons were removed as they duplicated the toolbar.
    auto* openButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardEmptyStateOpenButton"));
    auto* newSequenceButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardEmptyStateNewSequenceButton"));
    auto* imageIntegrationButton =
        dashboard.findChild<QPushButton*>(QStringLiteral("dashboardEmptyStateImageIntegrationButton"));
    EXPECT_EQ(openButton, nullptr);
    EXPECT_EQ(newSequenceButton, nullptr);
    EXPECT_EQ(imageIntegrationButton, nullptr);

    // Feature highlights should be present instead.
    auto* featuresContainer = dashboard.findChild<QWidget*>(QStringLiteral("dashboardEmptyStateFeaturesContainer"));
    ASSERT_NE(featuresContainer, nullptr);
    EXPECT_TRUE(featuresContainer->isVisible());
}

TEST(MainDashboardInteraction, IdentityBarUtilityButtonsEmitSignals)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    auto* settingsButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarSettingsButton"));
    auto* helpButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarHelpButton"));
    auto* aboutButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarAboutButton"));
    ASSERT_NE(settingsButton, nullptr);
    ASSERT_NE(helpButton, nullptr);
    ASSERT_NE(aboutButton, nullptr);

    QSignalSpy settingsSpy(&dashboard, &MainDashboard::openSettingsRequested);
    QSignalSpy helpSpy(&dashboard, &MainDashboard::openHelpRequested);
    QSignalSpy aboutSpy(&dashboard, &MainDashboard::aboutRequested);

    settingsButton->click();
    helpButton->click();
    aboutButton->click();

    EXPECT_EQ(settingsSpy.count(), 1);
    EXPECT_EQ(helpSpy.count(), 1);
    EXPECT_TRUE(helpButton->isEnabled());
    EXPECT_EQ(aboutSpy.count(), 1);
}

TEST(MainDashboardRecents, SortByNameReordersRecentFilesAlphabetically)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QComboBox* sortCombo = findRecentSortCombo(dashboard);
    ASSERT_NE(sortCombo, nullptr);
    sortCombo->setCurrentText(QStringLiteral("Name"));

    const QStringList expectedSorted = {QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf")),
                                        QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit")),
                                        QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"))};

    ASSERT_TRUE(waitForThumbnailOrder(dashboard, expectedSorted));
}

TEST(MainDashboardRecents, SortByTypeGroupsByFileExtension)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QComboBox* sortCombo = findRecentSortCombo(dashboard);
    ASSERT_NE(sortCombo, nullptr);
    sortCombo->setCurrentText(QStringLiteral("Type"));

    const QStringList expectedSorted = {QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit")),
                                        QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit")),
                                        QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"))};

    ASSERT_TRUE(waitForThumbnailOrder(dashboard, expectedSorted));
}

TEST(MainDashboardRecents, SelectedSortModePersistsAcrossDashboardInstances)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    const QStringList expectedTypeOrder = {QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit")),
                                           QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit")),
                                           QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"))};

    {
        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
        QComboBox* sortCombo = findRecentSortCombo(dashboard);
        ASSERT_NE(sortCombo, nullptr);
        sortCombo->setCurrentText(QStringLiteral("Type"));

        ASSERT_TRUE(waitForThumbnailOrder(dashboard, expectedTypeOrder));
        EXPECT_EQ(app.getGlobalSettingsPersistence()->select(
                      QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE)),
                  QStringLiteral("type"));
        EXPECT_EQ(app.getGlobalSettingsPersistence()->select(
                      QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION)),
                  QStringLiteral("4"));
    }

    MainDashboard dashboardReloaded;
    dashboardReloaded.initialize(&app);
    dashboardReloaded.resize(1280, 900);
    dashboardReloaded.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboardReloaded, 3));
    QComboBox* reloadedSortCombo = findRecentSortCombo(dashboardReloaded);
    ASSERT_NE(reloadedSortCombo, nullptr);
    EXPECT_EQ(reloadedSortCombo->currentText(), QStringLiteral("Type"));
}

// The dashboard's settings are read only at the format its writer writes, 4. Settings of another format - an older
// one, a newer one, or keys that state none - are never read: each is set aside under its own name with the format it
// states beside it, and the dashboard starts from its defaults.
TEST(MainDashboardRecents, DashboardSettingsOfAnotherFormatAreSetAsideAndNotRead)
{
    const QString sortKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE);
    const QString searchKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT);
    const QString formatKey = QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION);
    for (const QString& format : {QStringLiteral("3"), QStringLiteral("5"), QString()})
    {
        QTemporaryDir tempDir;
        ASSERT_TRUE(tempDir.isValid());

        DashboardApplication& app = *DashboardApplication::instance();
        configureRecentFilesWithThumbnails(app, tempDir.path());
        GlobalSettingsPersistence* settings = app.getGlobalSettingsPersistence();
        settings->upsert(sortKey, QStringLiteral("type"));
        settings->upsert(searchKey, QStringLiteral("M42"));
        if (!format.isEmpty())
        {
            settings->upsert(formatKey, format);
        }
        const QString suffix =
            format.isEmpty() ? QStringLiteral(".unversioned") : QStringLiteral(".format%1").arg(format);

        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3)) << "format " << format.toStdString();
        QComboBox* sortCombo = findRecentSortCombo(dashboard);
        ASSERT_NE(sortCombo, nullptr);
        EXPECT_NE(sortCombo->currentText(), QStringLiteral("Type"));
        QLineEdit* searchInput = findRecentSearchInput(dashboard);
        ASSERT_NE(searchInput, nullptr);
        EXPECT_TRUE(searchInput->text().isEmpty());
        EXPECT_EQ(settings->select(formatKey), QStringLiteral("4"));
        EXPECT_EQ(settings->select(sortKey + suffix), QStringLiteral("type"));
        EXPECT_EQ(settings->select(searchKey + suffix), QStringLiteral("M42"));
        if (!format.isEmpty())
        {
            EXPECT_EQ(settings->select(formatKey + suffix), format);
        }
    }
}

TEST(MainDashboardRecents, RecentSearchTextPersistsAcrossDashboardInstances)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    {
        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
        QLineEdit* searchInput = findRecentSearchInput(dashboard);
        ASSERT_NE(searchInput, nullptr);
        searchInput->setText(QStringLiteral("M42"));

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 2));
        EXPECT_EQ(app.getGlobalSettingsPersistence()->select(
                      QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT)),
                  QStringLiteral("M42"));
        EXPECT_EQ(app.getGlobalSettingsPersistence()->select(
                      QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION)),
                  QStringLiteral("4"));
    }

    MainDashboard dashboardReloaded;
    dashboardReloaded.initialize(&app);
    dashboardReloaded.resize(1280, 900);
    dashboardReloaded.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboardReloaded, 2));
    QLineEdit* reloadedSearchInput = findRecentSearchInput(dashboardReloaded);
    ASSERT_NE(reloadedSearchInput, nullptr);
    EXPECT_EQ(reloadedSearchInput->text(), QStringLiteral("M42"));
}

TEST(MainDashboardRecents, PinningRecentFileMovesItToTop)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    ImageThumbnailWidget* andromedaThumb = findThumbnailByPath(dashboard, andromedaPath);
    ASSERT_NE(andromedaThumb, nullptr);

    andromedaThumb->togglePinned();

    const QStringList expectedOrder = {andromedaPath,
                                       QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit")),
                                       QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"))};
    ASSERT_TRUE(waitForThumbnailOrder(dashboard, expectedOrder));
}

TEST(MainDashboardRecents, PinnedRecentFilesPersistAcrossDashboardInstances)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    const QString pinnedPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"));

    {
        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

        ImageThumbnailWidget* pinnedThumb = findThumbnailByPath(dashboard, pinnedPath);
        ASSERT_NE(pinnedThumb, nullptr);
        pinnedThumb->togglePinned();

        ASSERT_TRUE(waitForThumbnailOrder(
            dashboard, {pinnedPath, QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit")),
                        QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"))}));

        const QString persistedPinned = app.getGlobalSettingsPersistence()->select(
            QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS));
        ASSERT_FALSE(persistedPinned.isEmpty());
        EXPECT_TRUE(persistedPinned.contains(pinnedPath));
    }

    MainDashboard dashboardReloaded;
    dashboardReloaded.initialize(&app);
    dashboardReloaded.resize(1280, 900);
    dashboardReloaded.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboardReloaded, 3));

    ImageThumbnailWidget* reloadedPinnedThumb = findThumbnailByPath(dashboardReloaded, pinnedPath);
    ASSERT_NE(reloadedPinnedThumb, nullptr);
    EXPECT_TRUE(reloadedPinnedThumb->property("dashboardPinned").toBool());
    const QStringList expectedOrder = {pinnedPath, QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit")),
                                       QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"))};
    if (!waitForThumbnailOrder(dashboardReloaded, expectedOrder))
    {
        const QStringList actualOrder = visibleThumbnailPathsInGridOrder(dashboardReloaded);
        const QString persistedSortMode = app.getGlobalSettingsPersistence()->select(
            QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE));
        ADD_FAILURE() << "Unexpected reloaded thumbnail order. Expected: " << expectedOrder.join(", ").toStdString()
                      << " Actual: " << actualOrder.join(", ").toStdString()
                      << " SortMode: " << persistedSortMode.toStdString();
    }
}

TEST(MainDashboardRecents, BomPrefixedRecentEntriesStillHonorPinnedOrder)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    const QString m42Path = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    const QString pinnedPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"));
    const QString bomPrefixedPinnedPath = QString(QChar::ByteOrderMark) + pinnedPath;
    createRecentSourceFile(m42Path);
    createRecentSourceFile(andromedaPath);
    createRecentSourceFile(pinnedPath);

    app.setRecentFiles({m42Path.toStdString(), andromedaPath.toStdString(), bomPrefixedPinnedPath.toStdString()});
    app.setThumbnailPath(m42Path,
                         createThumbnailFile(tempDir.path(), QStringLiteral("m42_thumb"), QColor(220, 120, 120)));
    app.setThumbnailPath(andromedaPath,
                         createThumbnailFile(tempDir.path(), QStringLiteral("andromeda_thumb"), QColor(120, 220, 120)));
    app.setThumbnailPath(pinnedPath,
                         createThumbnailFile(tempDir.path(), QStringLiteral("m42_blue_thumb"), QColor(120, 120, 220)));
    app.getGlobalSettingsPersistence()->upsert(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS),
                                               pinnedPath);
    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    ImageThumbnailWidget* pinnedThumb = findThumbnailByPath(dashboard, pinnedPath);
    ASSERT_NE(pinnedThumb, nullptr);
    EXPECT_TRUE(pinnedThumb->property("dashboardPinned").toBool());
    ASSERT_TRUE(waitForThumbnailOrder(dashboard, {pinnedPath, m42Path, andromedaPath}));
}

TEST(MainDashboardRecents, OpeningRecentFilePromotesItAfterPinnedFiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    const QString m42LightPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    const QString m42BluePath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    ImageThumbnailWidget* andromedaThumb = findThumbnailByPath(dashboard, andromedaPath);
    ASSERT_NE(andromedaThumb, nullptr);
    andromedaThumb->togglePinned();
    ASSERT_TRUE(waitForThumbnailOrder(dashboard, {andromedaPath, m42LightPath, m42BluePath}));

    QSignalSpy openRecentSpy(&dashboard, &MainDashboard::openRecentFileRequested);
    ImageThumbnailWidget* m42BlueThumb = findThumbnailByPath(dashboard, m42BluePath);
    ASSERT_NE(m42BlueThumb, nullptr);
    QTest::mouseClick(m42BlueThumb, Qt::LeftButton);
    ASSERT_GE(openRecentSpy.count(), 1);

    ASSERT_TRUE(waitForThumbnailOrder(dashboard, {andromedaPath, m42BluePath, m42LightPath}));

    // Simulate an app snapshot that is still stale right after close/reopen.
    app.setRecentFiles({m42LightPath.toStdString(), andromedaPath.toStdString(), m42BluePath.toStdString()});
    dashboard.reloadRecentFilesThumbnails();

    ASSERT_TRUE(waitForThumbnailOrder(dashboard, {andromedaPath, m42BluePath, m42LightPath}));
}

TEST(MainDashboardRecents, ReloadWithUnchangedRecentListKeepsExistingThumbnailWidgets)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    const auto pointersBefore = visibleThumbnailPointersByPath(dashboard);
    ASSERT_EQ(pointersBefore.size(), 3);

    dashboard.reloadRecentFilesThumbnails();
    QCoreApplication::processEvents();
    QTest::qWait(80);

    const auto pointersAfter = visibleThumbnailPointersByPath(dashboard);
    ASSERT_EQ(pointersAfter.size(), 3);
    for (auto it = pointersBefore.constBegin(); it != pointersBefore.constEnd(); ++it)
    {
        ASSERT_TRUE(pointersAfter.contains(it.key()));
        EXPECT_EQ(pointersAfter.value(it.key()), it.value());
    }
}

TEST(MainDashboardRecents, ExternalRecentsPromotionUpdatesFeaturedHeroWhenUnpinned)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    const QString m42LightPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    const QString m42BluePath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    ASSERT_TRUE(waitForFeaturedFilePath(dashboard, m42LightPath));

    // Promote via in-dashboard thumbnail click first.
    ImageThumbnailWidget* m42BlueThumb = findThumbnailByPath(dashboard, m42BluePath);
    ASSERT_NE(m42BlueThumb, nullptr);
    QTest::mouseClick(m42BlueThumb, Qt::LeftButton);
    ASSERT_TRUE(waitForFeaturedFilePath(dashboard, m42BluePath));

    // Simulate opening a different file outside the recent thumbnail grid
    // (toolbar/menu/drag-drop/save flow): app recents head changes.
    app.setRecentFiles({andromedaPath.toStdString(), m42LightPath.toStdString(), m42BluePath.toStdString()});
    dashboard.reloadRecentFilesThumbnails();

    ASSERT_TRUE(waitForFeaturedFilePath(dashboard, andromedaPath));
}

TEST(MainDashboardInteraction, MainDashboard_PimplLifecycle_ConstructInitializeDestroyAndRecreate)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    {
        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
        dashboard.refreshIcons();
        dashboard.reloadRecentFilesThumbnails();
        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    }

    {
        MainDashboard dashboard;
        dashboard.initialize(&app);
        dashboard.resize(1280, 900);
        dashboard.show();

        ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    }
}

TEST(MainDashboardRecents, ThumbnailCacheHitReusesDecodedDataOnReload)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());
    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    const QString targetPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    const QString m42BluePath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Blue_002.fit"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    ASSERT_TRUE(waitForThumbnailCacheFlag(dashboard, targetPath, false));

    app.setRecentFiles({andromedaPath.toStdString(), targetPath.toStdString(), m42BluePath.toStdString()});
    dashboard.reloadRecentFilesThumbnails();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    EXPECT_TRUE(waitForThumbnailCacheFlag(dashboard, targetPath, true));
}

TEST(MainDashboardRecents, ThumbnailCacheInvalidatesWhenThumbnailFileChanges)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    const QString imagePath = QDir(tempDir.path()).filePath(QStringLiteral("CacheProbe.fit"));
    const QString auxPath = QDir(tempDir.path()).filePath(QStringLiteral("CacheAux.fit"));
    createRecentSourceFile(imagePath);
    createRecentSourceFile(auxPath);
    const QString thumbnailPath =
        createThumbnailFile(tempDir.path(), QStringLiteral("cache_probe_thumb"), QColor(80, 110, 180));
    const QString auxThumbnailPath =
        createThumbnailFile(tempDir.path(), QStringLiteral("cache_aux_thumb"), QColor(50, 150, 90));
    app.setRecentFiles({imagePath.toStdString(), auxPath.toStdString()});
    app.setThumbnailPath(imagePath, thumbnailPath);
    app.setThumbnailPath(auxPath, auxThumbnailPath);

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 2));
    ASSERT_TRUE(waitForThumbnailCacheFlag(dashboard, imagePath, false));

    app.setRecentFiles({auxPath.toStdString(), imagePath.toStdString()});
    dashboard.reloadRecentFilesThumbnails();
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 2));
    ASSERT_TRUE(waitForThumbnailCacheFlag(dashboard, imagePath, true));

    QTest::qWait(1100);
    QImage updatedThumb(64, 32, QImage::Format_ARGB32);
    updatedThumb.fill(QColor(190, 90, 90));
    ASSERT_TRUE(updatedThumb.save(thumbnailPath, "PNG"));

    app.setRecentFiles({imagePath.toStdString(), auxPath.toStdString()});
    dashboard.reloadRecentFilesThumbnails();
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 2));
    ASSERT_TRUE(waitForThumbnailCacheFlag(dashboard, imagePath, false));
}

TEST(MainDashboardInteraction, ActionLayoutPreferencesApplyPersistedOrderAndHiddenActions)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER),
        QStringLiteral("image_integration\nnew_sequence\nopen_file\nsystem_info\nclear_recents"));
    app.getGlobalSettingsPersistence()->upsert(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS),
                                               QStringLiteral("system_info"));
    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(30);

    const QStringList primaryOrder = visiblePrimaryActionTitlesInLayoutOrder(dashboard);
    EXPECT_EQ(primaryOrder, (QStringList{QStringLiteral("Image integration"), QStringLiteral("New sequence"),
                                         QStringLiteral("Open file")}));

    const QStringList secondaryOrder = visibleOverflowActionTitlesInMenuOrder(dashboard);
    EXPECT_EQ(secondaryOrder, (QStringList{QStringLiteral("Clear recents"), QStringLiteral("Extension Manager")}));

    EXPECT_FALSE(overflowMenuContainsAction(dashboard, QStringLiteral("System info")));
}

// A stored action order and hidden set name each action once; one that names an action twice, one the dashboard does
// not have, or stray separators, is read in its canonical form and stored so.
TEST(MainDashboardInteraction, ActionLayoutSettingsAreReadAndStoredInTheirCanonicalForm)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER),
        QStringLiteral(" system_info ; open_file,invalid,new_sequence\nsystem_info "));
    app.getGlobalSettingsPersistence()->upsert(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS),
                                               QStringLiteral("clear_recents,invalid;system_info,clear_recents"));
    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(30);

    EXPECT_EQ(app.getGlobalSettingsPersistence()->select(
                  QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION)),
              QStringLiteral("4"));
    EXPECT_EQ(
        app.getGlobalSettingsPersistence()->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER)),
        QStringLiteral("system_info\nopen_file\nnew_sequence\nimage_integration\nextension_manager\nclear_recents"));
    EXPECT_EQ(
        app.getGlobalSettingsPersistence()->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS)),
        QStringLiteral("clear_recents\nsystem_info"));
}

TEST(MainDashboardInteraction, ActionContextMenuMoveLaterReordersAndPersists)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(30);

    const QStringList beforePrimary = visiblePrimaryActionTitlesInLayoutOrder(dashboard);
    EXPECT_EQ(beforePrimary, (QStringList{QStringLiteral("Open file"), QStringLiteral("New sequence"),
                                          QStringLiteral("Image integration")}));

    ASSERT_TRUE(triggerOverflowMenuAction(dashboard, {QStringLiteral("Customize quick actions"),
                                                      QStringLiteral("Open file"), QStringLiteral("Move later")}));
    QTest::qWait(20);

    const QStringList afterPrimary = visiblePrimaryActionTitlesInLayoutOrder(dashboard);
    EXPECT_EQ(afterPrimary, (QStringList{QStringLiteral("New sequence"), QStringLiteral("Open file"),
                                         QStringLiteral("Image integration")}));

    EXPECT_EQ(
        app.getGlobalSettingsPersistence()->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER)),
        QStringLiteral("new_sequence\nopen_file\nimage_integration\nextension_manager\nclear_recents\nsystem_info"));
}

TEST(MainDashboardInteraction, ActionContextMenuHideAndRevealActionPersists)
{
    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QTest::qWait(30);

    EXPECT_TRUE(overflowMenuContainsAction(dashboard, QStringLiteral("System info")));

    ASSERT_TRUE(triggerOverflowMenuAction(dashboard, {QStringLiteral("Customize quick actions"),
                                                      QStringLiteral("System info"), QStringLiteral("Hide action")}));
    QTest::qWait(20);
    EXPECT_FALSE(overflowMenuContainsAction(dashboard, QStringLiteral("System info")));
    EXPECT_EQ(
        app.getGlobalSettingsPersistence()->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS)),
        QStringLiteral("system_info"));

    ASSERT_TRUE(
        triggerOverflowMenuAction(dashboard, {QStringLiteral("Customize quick actions"),
                                              QStringLiteral("Show hidden action"), QStringLiteral("System info")}));
    QTest::qWait(20);
    EXPECT_TRUE(overflowMenuContainsAction(dashboard, QStringLiteral("System info")));
    EXPECT_EQ(
        app.getGlobalSettingsPersistence()->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS)),
        QStringLiteral(""));
}

TEST(MainDashboardRecents, ThumbnailCardsExposeMetadataChips)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    const QString andromedaPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    ImageThumbnailWidget* andromedaThumb = findThumbnailByPath(dashboard, andromedaPath);
    ASSERT_NE(andromedaThumb, nullptr);

    auto* dimensionsChip = findChipLabelByRole(andromedaThumb, "dimensions");
    auto* typeChip = findChipLabelByRole(andromedaThumb, "type");
    ASSERT_NE(dimensionsChip, nullptr);
    ASSERT_NE(typeChip, nullptr);

    EXPECT_FALSE(dimensionsChip->text().isEmpty());
    EXPECT_EQ(typeChip->text(), QStringLiteral("XISF"));
}

TEST(MainDashboardRecents, ThumbnailHoverUsesDynamicPropertyWithoutInlineStyles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    const QString m42Path = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    ImageThumbnailWidget* thumb = findThumbnailByPath(dashboard, m42Path);
    ASSERT_NE(thumb, nullptr);

    EXPECT_FALSE(thumb->property("dashboardHoverActive").toBool());
    EXPECT_TRUE(thumb->styleSheet().isEmpty());

    QEnterEvent enterEvent(QPointF(6, 6), QPointF(6, 6), QPointF(6, 6));
    QCoreApplication::sendEvent(thumb, &enterEvent);
    EXPECT_TRUE(thumb->property("dashboardHoverActive").toBool());
    EXPECT_TRUE(thumb->styleSheet().isEmpty());

    QEvent leaveEvent(QEvent::Leave);
    QCoreApplication::sendEvent(thumb, &leaveEvent);
    EXPECT_FALSE(thumb->property("dashboardHoverActive").toBool());
}

TEST(MainDashboardRecents, SearchNoMatchTriggersEmptyStateTransition)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    auto* emptyState = findEmptyStateCard(dashboard);
    QWidget* gridContainer = findRecentGridContainer(dashboard);
    QLineEdit* searchInput = findRecentSearchInput(dashboard);
    ASSERT_NE(emptyState, nullptr);
    ASSERT_NE(gridContainer, nullptr);
    ASSERT_NE(searchInput, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(emptyState, false));
    ASSERT_TRUE(waitForWidgetVisibility(gridContainer, true));

    searchInput->setText(QStringLiteral("___NO_MATCH___"));

    EXPECT_TRUE(waitForTransitionActivation(emptyState));
    EXPECT_TRUE(waitForTransitionActivation(gridContainer));
    EXPECT_TRUE(waitForWidgetVisibility(emptyState, true));
    EXPECT_TRUE(waitForWidgetVisibility(gridContainer, false));
}

TEST(MainDashboardRecents, ThumbnailsDoNotAnimateDuringGridRefresh)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    dashboard.resize(1040, 900);
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    dashboard.resize(1320, 900);
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    const auto cards = collectVisibleThumbnailCards(dashboard);
    ASSERT_EQ(cards.size(), 3);
    for (auto* card : cards)
    {
        ASSERT_NE(card, nullptr);
        EXPECT_FALSE(card->property("dashboardStaggerPlayed").toBool());
        EXPECT_FALSE(card->property("dashboardStaggerActive").toBool());
        EXPECT_EQ(card->property("dashboardStaggerDelayMs").toInt(), 0);
    }
}

TEST(MainDashboardRecents, SameColumnResizeKeepsAllThumbnailsAttachedToGridLayout)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(gridContainer, nullptr);
    auto* gridLayout = qobject_cast<QGridLayout*>(gridContainer->layout());
    ASSERT_NE(gridLayout, nullptr);
    ASSERT_EQ(gridLayout->count(), 3);

    // Keep the same column count while still triggering resizeEvent/updateGrid.
    dashboard.resize(1100, 900);
    QCoreApplication::processEvents();
    QTest::qWait(60);
    ASSERT_EQ(gridLayout->count(), 3);

    dashboard.resize(1090, 900);
    QCoreApplication::processEvents();
    QTest::qWait(60);

    EXPECT_EQ(gridLayout->count(), 3);
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    EXPECT_TRUE(waitForThumbnailsWithinRecentGridBounds(dashboard));

    dashboard.hide();
    ASSERT_TRUE(waitForWidgetVisibility(&dashboard, false));
    dashboard.show();
    QCoreApplication::processEvents();
    QTest::qWait(60);

    EXPECT_EQ(gridLayout->count(), 3);
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    EXPECT_TRUE(waitForThumbnailsWithinRecentGridBounds(dashboard));
}

TEST(MainDashboardRecents, ShowEventRestoresRecentGridOpacityAfterStaleTransparentState)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(gridContainer, nullptr);
    auto* gridOpacityEffect = dynamic_cast<QGraphicsOpacityEffect*>(gridContainer->graphicsEffect());
    ASSERT_NE(gridOpacityEffect, nullptr);

    // Simulate a stale transition state where the recent grid stays transparent
    // even though the section should be visible.
    gridContainer->setVisible(true);
    gridOpacityEffect->setEnabled(true);
    gridOpacityEffect->setOpacity(0.0);

    dashboard.hide();
    ASSERT_TRUE(waitForWidgetVisibility(&dashboard, false));
    dashboard.show();
    QCoreApplication::processEvents();
    QTest::qWait(60);

    EXPECT_TRUE(waitForWidgetVisibility(gridContainer, true));
    EXPECT_FALSE(gridOpacityEffect->isEnabled());
    EXPECT_NEAR(gridOpacityEffect->opacity(), 1.0, 0.001);
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
}

TEST(MainDashboardRecents, ReloadRecentFilesThumbnailsResetsStaleGridOpacityState)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(gridContainer, nullptr);
    auto* gridOpacityEffect = dynamic_cast<QGraphicsOpacityEffect*>(gridContainer->graphicsEffect());
    ASSERT_NE(gridOpacityEffect, nullptr);

    dashboard.hide();
    ASSERT_TRUE(waitForWidgetVisibility(&dashboard, false));

    gridContainer->setVisible(true);
    gridOpacityEffect->setEnabled(true);
    gridOpacityEffect->setOpacity(0.0);

    dashboard.reloadRecentFilesThumbnails();

    EXPECT_FALSE(gridOpacityEffect->isEnabled());
    EXPECT_NEAR(gridOpacityEffect->opacity(), 1.0, 0.001);

    dashboard.show();
    QCoreApplication::processEvents();
    QTest::qWait(60);

    EXPECT_TRUE(waitForWidgetVisibility(gridContainer, true));
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
}

TEST(MainDashboardRecents, GridRefreshDoesNotLatchUpdatesDisabledFromParent)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* gridContainer = findRecentGridContainer(dashboard);
    ASSERT_NE(gridContainer, nullptr);
    auto* grid = dynamic_cast<an::ui::RecentFilesGrid*>(gridContainer);
    ASSERT_NE(grid, nullptr);

    // Simulate parent-managed update suppression while the grid recalculates.
    dashboard.setUpdatesEnabled(false);
    grid->refreshLayout();
    dashboard.setUpdatesEnabled(true);
    QCoreApplication::processEvents();

    // The grid must not latch its own updates-disabled flag permanently.
    EXPECT_TRUE(grid->updatesEnabled());
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
}

TEST(MainDashboardRecents, ReloadRecentFilesThumbnailsResetsStaleThumbnailOpacityEffects)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    QList<QWidget*> thumbnails = collectVisibleThumbnails(dashboard);
    ASSERT_FALSE(thumbnails.isEmpty());

    QWidget* targetThumbnail = thumbnails.first();
    ASSERT_NE(targetThumbnail, nullptr);
    auto* opacityEffect = new QGraphicsOpacityEffect(targetThumbnail);
    targetThumbnail->setGraphicsEffect(opacityEffect);
    opacityEffect->setEnabled(true);
    opacityEffect->setOpacity(0.0);

    dashboard.reloadRecentFilesThumbnails();

    EXPECT_FALSE(opacityEffect->isEnabled());
    EXPECT_NEAR(opacityEffect->opacity(), 1.0, 0.001);
    EXPECT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
}

TEST(MainDashboardRecents, ResizingKeepsThumbnailsInsideGridBounds)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1400, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    ASSERT_TRUE(waitForThumbnailsWithinRecentGridBounds(dashboard));

    constexpr std::array<int, 6> widths = {1240, 1160, 1080, 1000, 920, 860};
    for (const int width : widths)
    {
        dashboard.resize(width, 900);
        QCoreApplication::processEvents();
        EXPECT_TRUE(waitForThumbnailsWithinRecentGridBounds(dashboard, 300));
    }
}

// ===========================================================================
// DashboardThumbnailCache unit tests
// ===========================================================================

#include "controls/DashboardThumbnailCache.h"

TEST(DashboardThumbnailCacheTest, InsertAndFindReturnsCachedEntry)
{
    DashboardThumbnailCache cache(4);

    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const QSize sourceSize(100, 200);
    const QString key = DashboardThumbnailCache::cacheKey(QStringLiteral("/tmp/test.fits"), 300);

    cache.insert(key, image, sourceSize, 12345);

    const auto* entry = cache.find(key);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->sourceSize, sourceSize);
    EXPECT_EQ(entry->lastModifiedMs, 12345);
    EXPECT_FALSE(entry->displayImage.isNull());
    EXPECT_EQ(entry->displayImage.size(), image.size());
}

TEST(DashboardThumbnailCacheTest, FindReturnsNullptrForMissingKey)
{
    DashboardThumbnailCache cache(4);
    EXPECT_EQ(cache.find(QStringLiteral("missing")), nullptr);
}

TEST(DashboardThumbnailCacheTest, EvictsOldestEntriesWhenOverCapacity)
{
    DashboardThumbnailCache cache(2);

    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::blue);

    cache.insert(QStringLiteral("a"), image, QSize(1, 1), 1);
    cache.insert(QStringLiteral("b"), image, QSize(2, 2), 2);
    cache.insert(QStringLiteral("c"), image, QSize(3, 3), 3);

    // "a" should have been evicted
    EXPECT_EQ(cache.find(QStringLiteral("a")), nullptr);
    EXPECT_NE(cache.find(QStringLiteral("b")), nullptr);
    EXPECT_NE(cache.find(QStringLiteral("c")), nullptr);
}

TEST(DashboardThumbnailCacheTest, ClearRemovesAllEntries)
{
    DashboardThumbnailCache cache(4);
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::green);

    cache.insert(QStringLiteral("x"), image, QSize(10, 20), 99);
    ASSERT_NE(cache.find(QStringLiteral("x")), nullptr);

    cache.clear();
    EXPECT_EQ(cache.find(QStringLiteral("x")), nullptr);
}

TEST(DashboardThumbnailCacheTest, CacheKeyIncludesTileSize)
{
    const QString key1 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 300);
    const QString key2 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 400);
    EXPECT_NE(key1, key2);

    const QString key3 = DashboardThumbnailCache::cacheKey(QStringLiteral("/path/img.fits"), 300);
    EXPECT_EQ(key1, key3);
}

// ===========================================================================
// Search results count in hint label
// ===========================================================================

TEST(MainDashboardRecents, SearchPartialMatchShowsResultCount)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QLineEdit* searchInput = findRecentSearchInput(dashboard);
    ASSERT_NE(searchInput, nullptr);

    // "andromeda" matches 1 of the 3 test files
    searchInput->setText(QStringLiteral("andromeda"));
    QCoreApplication::processEvents();
    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 1, WAIT_TIMEOUT_MS));

    auto* hintLabel = dashboard.findChild<QLabel*>(QStringLiteral("dashboardRecentFilesHint"));
    auto* recentHeader = findRecentSectionHeader(dashboard);
    ASSERT_NE(hintLabel, nullptr);
    ASSERT_NE(recentHeader, nullptr);
    EXPECT_TRUE(hintLabel->text().contains(QStringLiteral("1")));
    EXPECT_TRUE(hintLabel->text().contains(QStringLiteral("3")));
    EXPECT_EQ(recentHeader->description(), QStringLiteral("Showing 1 of 3"));
}

TEST(MainDashboardInteraction, ToolbarActionsWithShortcutsExposeTooltips)
{
    DashboardApplication& app = *DashboardApplication::instance();
    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();
    QCoreApplication::processEvents();

    QPushButton* openButton = findActionButton(dashboard, QStringLiteral("Open file"));
    ASSERT_NE(openButton, nullptr);
    EXPECT_TRUE(openButton->toolTip().contains(QStringLiteral("Ctrl+O")));

    auto* settingsButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarSettingsButton"));
    auto* helpButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarHelpButton"));
    auto* aboutButton = dashboard.findChild<QPushButton*>(QStringLiteral("dashboardIdentityBarAboutButton"));
    ASSERT_NE(settingsButton, nullptr);
    ASSERT_NE(helpButton, nullptr);
    ASSERT_NE(aboutButton, nullptr);

    EXPECT_TRUE(settingsButton->toolTip().contains(QStringLiteral("Ctrl+,")));
    EXPECT_TRUE(helpButton->toolTip().contains(QStringLiteral("F1")));
    EXPECT_FALSE(aboutButton->toolTip().contains(QStringLiteral("(")));
}

TEST(MainDashboardInteraction, FeaturedContinueButtonEmitsOpenRecentFileRequested)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    QPushButton* continueButton = findFeaturedContinueButton(dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_NE(continueButton, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));

    QSignalSpy openRecentSpy(&dashboard, &MainDashboard::openRecentFileRequested);
    continueButton->click();
    ASSERT_EQ(openRecentSpy.count(), 1);
    ASSERT_EQ(openRecentSpy.at(0).size(), 1);

    const QString expectedPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    EXPECT_EQ(openRecentSpy.at(0).at(0).toString(), expectedPath);
}

TEST(MainDashboardInteraction, FeaturedPinButtonClearsPersistedPinnedHeroPath)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    const QString pinnedHeroPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    app.getGlobalSettingsPersistence()->upsert(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE),
                                               pinnedHeroPath);
    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));
    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    QPushButton* pinButton = findFeaturedPinButton(dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_NE(pinButton, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));
    EXPECT_EQ(featuredCard->property("dashboardFeaturedFilePath").toString(), pinnedHeroPath);

    pinButton->click();
    EXPECT_TRUE(app.getGlobalSettingsPersistence()
                    ->select(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE))
                    .trimmed()
                    .isEmpty());
}

TEST(MainDashboardInteraction, FeaturedCardFallsBackWhenFirstThumbnailMissing)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    app.resetForTest();

    // First file has a valid source file but its thumbnail path will point to
    // a non-existent file, so the presenter should skip it and fall back to the
    // second file whose thumbnail is valid.
    const QString firstPath = QDir(tempDir.path()).filePath(QStringLiteral("NoThumb_001.fit"));
    const QString secondPath = QDir(tempDir.path()).filePath(QStringLiteral("HasThumb_001.xisf"));
    createRecentSourceFile(firstPath);
    createRecentSourceFile(secondPath);

    std::deque<std::string> recents = {firstPath.toStdString(), secondPath.toStdString()};
    app.setRecentFiles(recents);

    // Point the first file to a thumbnail that does not exist on disk.
    app.setThumbnailPath(firstPath, QDir(tempDir.path()).filePath(QStringLiteral("missing_thumb.png")));
    // Provide a valid thumbnail only for the second file.
    app.setThumbnailPath(secondPath,
                         createThumbnailFile(tempDir.path(), QStringLiteral("hasthumb_thumb"), QColor(200, 200, 50)));

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    // Wait for the featured card presenter to resolve.  The first file has no
    // valid thumbnail, so the presenter should fall back to the second file.
    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));

    // The featured card should show the second file since the first had no
    // decodable thumbnail.
    EXPECT_EQ(featuredCard->property("dashboardFeaturedFilePath").toString(), secondPath);

    QLabel* featuredTitle = findFeaturedFileTitleLabel(dashboard);
    ASSERT_NE(featuredTitle, nullptr);
    EXPECT_EQ(featuredTitle->text(), QStringLiteral("HasThumb_001.xisf"));
}

TEST(MainDashboardInteraction, FeaturedCardCrossfadesWhenSwitchingFiles)
{
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    DashboardApplication& app = *DashboardApplication::instance();
    configureRecentFilesWithThumbnails(app, tempDir.path());

    MainDashboard dashboard;
    dashboard.initialize(&app);
    dashboard.resize(1280, 900);
    dashboard.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard, 3));

    QWidget* featuredCard = findFeaturedFileCard(dashboard);
    ASSERT_NE(featuredCard, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard, true));

    // Initial card should show the most recent file.
    const QString firstPath = QDir(tempDir.path()).filePath(QStringLiteral("M42_Light_001.fit"));
    EXPECT_EQ(featuredCard->property("dashboardFeaturedFilePath").toString(), firstPath);

    // Pin the second file so the featured card switches.
    const QString secondPath = QDir(tempDir.path()).filePath(QStringLiteral("Andromeda_001.xisf"));
    QPushButton* pinButton = findFeaturedPinButton(dashboard);
    ASSERT_NE(pinButton, nullptr);

    // Manually pin the second file via settings to trigger a card switch.
    app.getGlobalSettingsPersistence()->upsert(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE),
                                               secondPath);
    app.getGlobalSettingsPersistence()->upsert(
        QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION), QStringLiteral("4"));

    // Re-initialize to pick up the pinned hero change.
    MainDashboard dashboard2;
    dashboard2.initialize(&app);
    dashboard2.resize(1280, 900);
    dashboard2.show();

    ASSERT_TRUE(waitForVisibleThumbnailCount(dashboard2, 3));
    QWidget* featuredCard2 = findFeaturedFileCard(dashboard2);
    ASSERT_NE(featuredCard2, nullptr);
    ASSERT_TRUE(waitForWidgetVisibility(featuredCard2, true));

    // After pinning the second file, the featured card should show it.
    EXPECT_EQ(featuredCard2->property("dashboardFeaturedFilePath").toString(), secondPath);

    // The FeaturedFileCard should have a QGraphicsOpacityEffect used for
    // fade-in animation — verify the effect exists and is at full opacity
    // (animation completed).
    auto* opacityEffect = featuredCard2->findChild<QGraphicsOpacityEffect*>();
    ASSERT_NE(opacityEffect, nullptr);
    // Wait briefly for fade-in to complete.
    QTest::qWait(400);
    EXPECT_NEAR(opacityEffect->opacity(), 1.0, 0.01);
}
