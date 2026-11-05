// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MainDashboard.h"
#include "controls/IAppContext.h"
#include "controls/FeaturedFilePresenter.h"
#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardIdentityBar.h"
#include "controls/DashboardOnboardingPanel.h"
#include "controls/DashboardToolbar.h"
#include "controls/RecentFilesGrid.h"
#include "controls/DashboardPreferences.h"
#include "controls/DashboardUtils.h"
#include "controls/settings/SettingsKeys.h"
#include "components/AnButtonGroup.h"
#include "components/AnCardWidget.h"
#include "components/AnSectionHeader.h"
#include "widgets/FeaturedFileCard.h"
#include "app/UiStyleTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightPlatform/Logging.h>

#include <QFileInfo>
#include <QDir>
#include <QFrame>
#include <QMimeData>
#include <QMenu>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <vector>

W_OBJECT_IMPL(MainDashboard)

using DashboardPreferences::ACTION_ID_ABOUT;
using DashboardPreferences::ACTION_ID_CLEAR_RECENTS;
using DashboardPreferences::ACTION_ID_EXTENSION_MANAGER;
using DashboardPreferences::ACTION_ID_HELP;
using DashboardPreferences::ACTION_ID_IMAGE_INTEGRATION;
using DashboardPreferences::ACTION_ID_NEW_SEQUENCE;
using DashboardPreferences::ACTION_ID_OPEN_FILE;
using DashboardPreferences::ACTION_ID_SETTINGS;
using DashboardPreferences::ACTION_ID_SYSTEM_INFO;
using DashboardPreferences::actionTitleForId;
using DashboardPreferences::canonicalizeActionOrder;
using DashboardPreferences::canonicalizeHiddenActionIds;
using DashboardPreferences::dashboardActionIds;
using DashboardPreferences::deserializeActionIdList;
using DashboardPreferences::deserializeRecentSortMode;
using DashboardPreferences::hiddenActionIdsInCanonicalOrder;
using DashboardPreferences::normalizeActionIdToken;
using DashboardPreferences::serializeActionIdList;
using DashboardPreferences::serializeRecentSortMode;

using DashboardUtils::normalizeRecentFilePath;
using DashboardUtils::scaledPixels;
using DashboardUtils::scaledSystemFont;

namespace
{
constexpr int BASE_TILE_SIZE = 300;
constexpr int BASE_TILE_DISTANCE = 14;
constexpr int BASE_MAIN_MARGIN_LEFT = 20;
constexpr int BASE_MAIN_MARGIN_TOP = 25;
constexpr int BASE_MAIN_MARGIN_RIGHT = 18;
constexpr int BASE_MAIN_MARGIN_BOTTOM = 25;
constexpr int RECENT_SECTION_FADE_DURATION_MS = 170;
constexpr int RECENT_SECTION_SLIDE_DURATION_MS = 210;
constexpr int RECENT_SECTION_SLIDE_OFFSET_PX = 12;

constexpr int RECENT_SORT_RECENT = DashboardPreferences::RECENT_SORT_RECENT;
constexpr int RECENT_SORT_NAME = DashboardPreferences::RECENT_SORT_NAME;
constexpr int RECENT_SORT_TYPE = DashboardPreferences::RECENT_SORT_TYPE;
constexpr int DASHBOARD_SETTINGS_SCHEMA_VERSION = 4;
constexpr auto DASHBOARD_SETTINGS_SCHEMA_VERSION_KEY = SettingsKeys::INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION;
constexpr auto DASHBOARD_ACTION_ORDER_KEY = SettingsKeys::INTERFACE_DASHBOARD_ACTION_ORDER;
constexpr auto DASHBOARD_HIDDEN_ACTIONS_KEY = SettingsKeys::INTERFACE_DASHBOARD_HIDDEN_ACTIONS;
constexpr auto DASHBOARD_PINNED_HERO_FILE_KEY = SettingsKeys::INTERFACE_DASHBOARD_PINNED_HERO_FILE;
constexpr auto DASHBOARD_RECENT_SORT_MODE_KEY = SettingsKeys::INTERFACE_DASHBOARD_RECENT_SORT_MODE;
constexpr auto DASHBOARD_RECENT_SEARCH_TEXT_KEY = SettingsKeys::INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT;
constexpr auto DASHBOARD_LAST_ACTION_KEY = SettingsKeys::INTERFACE_DASHBOARD_LAST_ACTION;

/// Every setting the dashboard's format governs, the format itself among them.
constexpr const char* DASHBOARD_SETTINGS_KEYS[] = {
    DASHBOARD_SETTINGS_SCHEMA_VERSION_KEY, DASHBOARD_ACTION_ORDER_KEY,
    DASHBOARD_HIDDEN_ACTIONS_KEY,          SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS,
    DASHBOARD_PINNED_HERO_FILE_KEY,        DASHBOARD_RECENT_SORT_MODE_KEY,
    DASHBOARD_RECENT_SEARCH_TEXT_KEY,      DASHBOARD_LAST_ACTION_KEY};

an::ui::IGlobalSettingsProvider* globalSettings()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getGlobalSettings();
    }

    return nullptr;
}

an::ui::IIconProviderPtr iconProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getIconProvider();
    }

    return {};
}

an::ui::IRecentFilesService* recentFilesService()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getRecentFilesService();
    }

    return nullptr;
}

an::ui::IThumbnailPathProvider* thumbnailPathProvider()
{
    if (auto* context = an::ui::appContext())
    {
        return context->getThumbnailPathProvider();
    }

    return nullptr;
}

QString selectGlobalSetting(const QString& key, const QString& defaultValue = {})
{
    if (auto* settings = globalSettings())
    {
        return settings->select(key, defaultValue);
    }

    return defaultValue;
}

void upsertGlobalSetting(const QString& key, const QString& value)
{
    if (auto* settings = globalSettings())
    {
        settings->upsert(key, value);
    }
}

QIcon monoSvgIcon(const QString& resourcePath, const QSize& size)
{
    if (const auto provider = iconProvider())
    {
        return QIcon(provider->loadSvgFileAsPixmap(resourcePath, size, provider->getColorNormal()));
    }

    return QIcon(resourcePath);
}

std::deque<std::string> currentRecentFiles()
{
    if (auto* service = recentFilesService())
    {
        return service->getRecentFilesList();
    }

    return {};
}

QString selectThumbnailPath(const QString& filePath)
{
    if (auto* provider = thumbnailPathProvider())
    {
        return provider->selectThumbnailPath(filePath);
    }

    return {};
}

void openDroppedFilesViaContext(const QStringList& files)
{
    if (auto* service = recentFilesService())
    {
        service->openDroppedFiles(files);
    }
}

} // anonymous namespace

struct MainDashboard::Impl
{
    QVBoxLayout* m_MainLayout = nullptr;
    QStringList m_ActionOrderPreference;
    QSet<QString> m_HiddenActionIds;
    an::ui::DashboardIdentityBar* m_DashboardIdentityBar = nullptr;
    an::ui::DashboardToolbar* m_DashboardToolbar = nullptr;

    std::deque<QString> m_AllRecentFilePaths;
    QSet<QString> m_PinnedRecentFiles;
    qreal m_DpiScale = 1.0;
    int m_TileSize = 300;
    int m_TileDistance = 10;
    int m_FeaturedThumbnailCacheTileSize = 360;
    QString m_PendingPromotedRecentFilePath;
    std::deque<QString> m_PendingPromotionBaselineRecents;

    an::ui::AnSectionHeader* m_RecentSectionHeader = nullptr;
    QWidget* m_RecentControlsRow = nullptr;
    QLabel* m_RecentFilesHint = nullptr;
    QLineEdit* m_RecentSearchInput = nullptr;
    QAction* m_SearchAction = nullptr;
    QComboBox* m_RecentSortCombo = nullptr;
    QWidget* m_EmptyStateCard = nullptr;
    an::ui::RecentFilesGrid* m_RecentFilesGrid = nullptr;
    QGraphicsOpacityEffect* m_EmptyStateOpacityEffect = nullptr;
    QGraphicsOpacityEffect* m_RecentGridOpacityEffect = nullptr;
    QPointer<QParallelAnimationGroup> m_EmptyStateTransitionGroup;
    QPointer<QParallelAnimationGroup> m_RecentGridTransitionGroup;
    bool m_RecentSectionStateInitialized = false;
    MainDashboard::RecentSectionState m_RecentSectionState = MainDashboard::RecentSectionState::NoRecents;
    an::ui::DashboardOnboardingPanel* m_EmptyStateOnboardingPanel = nullptr;
    an::ui::AnCardWidget* m_EmptyStateNoMatchCard = nullptr;
    QLabel* m_EmptyStateNoMatchTitle = nullptr;
    QLabel* m_EmptyStateNoMatchDescription = nullptr;

    an::ui::FeaturedFileCard* m_FeaturedFileCard = nullptr;
    an::ui::FeaturedFilePresenter* m_FeaturedFilePresenter = nullptr;
    QFrame* m_HeroRecentDivider = nullptr;
    QString m_PinnedHeroFilePath;
    QString m_PreferredHeroFilePath;
};

MainDashboard::MainDashboard(QWidget* parent, Qt::WindowFlags f) : QWidget(parent, f), m_Impl(std::make_unique<Impl>())
{
}

MainDashboard::~MainDashboard() = default;

bool MainDashboard::isDarkMode() const
{
    return an::ui::isDarkTheme();
}

void MainDashboard::initializeDpiAwareSizing()
{
    qreal logicalDpi = 96.0;
    if (QScreen* screenHandle = screen())
    {
        logicalDpi = screenHandle->logicalDotsPerInch();
    }

    m_Impl->m_DpiScale = std::clamp(logicalDpi / 96.0, 0.85, 1.80);
    m_Impl->m_TileSize = (std::max)(220, static_cast<int>(std::lround(BASE_TILE_SIZE * m_Impl->m_DpiScale)));
    m_Impl->m_TileDistance = (std::max)(8, static_cast<int>(std::lround(BASE_TILE_DISTANCE * m_Impl->m_DpiScale)));
}

void MainDashboard::initialize()
{
    initializeDpiAwareSizing();

    setObjectName("mainDashboardRoot");
    setAcceptDrops(true);

    m_Impl->m_MainLayout = new QVBoxLayout(this);
    m_Impl->m_MainLayout->setSpacing(scaledPixels(m_Impl->m_DpiScale, 10, 8));

    ensureDashboardSettingsSchema();
    loadPinnedRecentFilesPreference();
    loadPinnedHeroFilePreference();

    initializeIdentityBarSection();
    initializeHeaderSection();
    initializeActionButtonsSection();

    // Subtle horizontal divider between toolbar and recent section (E)
    m_Impl->m_HeroRecentDivider = new QFrame(this);
    m_Impl->m_HeroRecentDivider->setObjectName(QStringLiteral("dashboardHeroRecentDivider"));
    m_Impl->m_HeroRecentDivider->setFrameShape(QFrame::HLine);
    m_Impl->m_HeroRecentDivider->setFrameShadow(QFrame::Plain);
    m_Impl->m_HeroRecentDivider->setFixedHeight(1);
    m_Impl->m_MainLayout->addWidget(m_Impl->m_HeroRecentDivider);

    initializeRecentFilesSection();

    m_Impl->m_MainLayout->addStretch();
    m_Impl->m_MainLayout->setContentsMargins(scaledPixels(m_Impl->m_DpiScale, BASE_MAIN_MARGIN_LEFT, 12),
                                             scaledPixels(m_Impl->m_DpiScale, BASE_MAIN_MARGIN_TOP, 12),
                                             scaledPixels(m_Impl->m_DpiScale, BASE_MAIN_MARGIN_RIGHT, 8),
                                             scaledPixels(m_Impl->m_DpiScale, BASE_MAIN_MARGIN_BOTTOM, 12));
    setLayout(m_Impl->m_MainLayout);
    setMinimumWidth(scaledPixels(m_Impl->m_DpiScale, 600, 520));

    updateThemeStyles();
}

void MainDashboard::initialize(QWidget* parent)
{
    Q_UNUSED(parent);
    initialize();
}

void MainDashboard::initializeIdentityBarSection()
{
    m_Impl->m_DashboardIdentityBar = new an::ui::DashboardIdentityBar(this);

    connect(m_Impl->m_DashboardIdentityBar, &an::ui::DashboardIdentityBar::openSettingsRequested, this, [this]() {
        recordDashboardLastAction(tr("Open settings"));
        emit openSettingsRequested();
    });
    connect(m_Impl->m_DashboardIdentityBar, &an::ui::DashboardIdentityBar::aboutRequested, this, [this]() {
        recordDashboardLastAction(tr("Open about dialog"));
        emit aboutRequested();
    });
    connect(m_Impl->m_DashboardIdentityBar, &an::ui::DashboardIdentityBar::openHelpRequested, this, [this]() {
        recordDashboardLastAction(tr("Open help"));
        emit openHelpRequested();
    });

    m_Impl->m_MainLayout->addWidget(m_Impl->m_DashboardIdentityBar);
}

void MainDashboard::initializeHeaderSection()
{
    m_Impl->m_FeaturedFileCard = new an::ui::FeaturedFileCard(this);
    const int featuredCardHeight = std::clamp(static_cast<int>(std::lround(180.0 * m_Impl->m_DpiScale)), 150, 280);
    m_Impl->m_FeaturedThumbnailCacheTileSize = (std::max)(96, featuredCardHeight * 2);
    m_Impl->m_FeaturedFileCard->setFixedHeight(featuredCardHeight);
    m_Impl->m_FeaturedFileCard->hide();

    m_Impl->m_FeaturedFilePresenter = new an::ui::FeaturedFilePresenter(this);
    m_Impl->m_FeaturedFilePresenter->setThumbnailCacheTileSize(m_Impl->m_FeaturedThumbnailCacheTileSize);
    m_Impl->m_FeaturedFilePresenter->setPreferredDecodeSize(
        QSize((std::max)(640, static_cast<int>(std::lround(1200.0 * m_Impl->m_DpiScale))), featuredCardHeight * 2));
    m_Impl->m_FeaturedFilePresenter->setPinnedHeroFilePath(m_Impl->m_PinnedHeroFilePath);

    m_Impl->m_FeaturedFilePresenter->setThumbnailPathResolver(
        [](const QString& filePath) { return selectThumbnailPath(filePath); });

    connect(m_Impl->m_FeaturedFilePresenter, &an::ui::FeaturedFilePresenter::featuredFileReady, this,
            [this](const QString& filePath, const QString& displayName, const QString& metadataText,
                   const QPixmap& backgroundPixmap, const bool pinned) {
                if (!m_Impl->m_FeaturedFileCard)
                {
                    return;
                }

                m_Impl->m_FeaturedFileCard->setFeaturedFile(filePath, displayName, metadataText, backgroundPixmap,
                                                            pinned);
                m_Impl->m_FeaturedFileCard->show();
            });

    connect(m_Impl->m_FeaturedFilePresenter, &an::ui::FeaturedFilePresenter::noFeaturedFileAvailable, this, [this]() {
        if (!m_Impl->m_FeaturedFileCard)
        {
            return;
        }

        m_Impl->m_FeaturedFileCard->clearFeaturedFile();
        m_Impl->m_FeaturedFileCard->hide();
    });

    connect(m_Impl->m_FeaturedFileCard, &an::ui::FeaturedFileCard::continueRequested, this,
            [this](const QString& filePath) {
                recordDashboardLastAction(tr("Continue featured file"));
                promoteRecentFileToFront(filePath);
                emit openRecentFileRequested(filePath);
            });

    connect(m_Impl->m_FeaturedFileCard, &an::ui::FeaturedFileCard::pinToggled, this,
            [this](const QString& filePath, const bool pinned) { setPinnedHeroFile(filePath, pinned); });

    m_Impl->m_MainLayout->addWidget(m_Impl->m_FeaturedFileCard);
    refreshFeaturedFileCard();
}

QString MainDashboard::actionTitleForId(const QString& actionId) const
{
    return DashboardPreferences::actionTitleForId(actionId);
}

void MainDashboard::applyActionLayoutPreferences()
{
    const QStringList allActionIds = dashboardActionIds();
    m_Impl->m_ActionOrderPreference = canonicalizeActionOrder(allActionIds, m_Impl->m_ActionOrderPreference);
    m_Impl->m_HiddenActionIds = canonicalizeHiddenActionIds(
        allActionIds, hiddenActionIdsInCanonicalOrder(allActionIds, m_Impl->m_HiddenActionIds));

    if (!m_Impl->m_DashboardToolbar)
    {
        return;
    }

    m_Impl->m_DashboardToolbar->setActionOrder(m_Impl->m_ActionOrderPreference);
    m_Impl->m_DashboardToolbar->setHiddenActionIds(m_Impl->m_HiddenActionIds);
    m_Impl->m_DashboardToolbar->setActionEnabled(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),
                                                 !m_Impl->m_AllRecentFilePaths.empty());
}

void MainDashboard::populateQuickActionCustomizationMenu(QMenu& menu)
{
    const QStringList allActionIds = dashboardActionIds();
    QMenu* customizeMenu = menu.addMenu(tr("Customize quick actions"));
    if (!customizeMenu)
    {
        return;
    }

    int visibleActionCount = 0;
    for (const QString& actionId : m_Impl->m_ActionOrderPreference)
    {
        if (!m_Impl->m_HiddenActionIds.contains(actionId))
        {
            ++visibleActionCount;
        }
    }

    const auto applyAndPersist = [this, allActionIds]() {
        applyActionLayoutPreferences();
        persistActionLayoutPreferences(allActionIds);
        update();
    };

    for (const QString& actionId : m_Impl->m_ActionOrderPreference)
    {
        const int actionIndex = m_Impl->m_ActionOrderPreference.indexOf(actionId);
        QMenu* actionMenu = customizeMenu->addMenu(actionTitleForId(actionId));
        if (!actionMenu)
        {
            continue;
        }

        QAction* moveEarlierAction = actionMenu->addAction(tr("Move earlier"));
        QAction* moveLaterAction = actionMenu->addAction(tr("Move later"));
        moveEarlierAction->setEnabled(actionIndex > 0);
        moveLaterAction->setEnabled(actionIndex >= 0 && actionIndex < (m_Impl->m_ActionOrderPreference.size() - 1));

        const bool isHidden = m_Impl->m_HiddenActionIds.contains(actionId);
        QAction* hideOrShowAction = actionMenu->addAction(isHidden ? tr("Show action") : tr("Hide action"));
        hideOrShowAction->setEnabled(isHidden || visibleActionCount > 1);

        connect(moveEarlierAction, &QAction::triggered, this, [this, actionIndex, applyAndPersist]() {
            if (actionIndex <= 0)
            {
                return;
            }
            m_Impl->m_ActionOrderPreference.swapItemsAt(actionIndex, actionIndex - 1);
            applyAndPersist();
        });
        connect(moveLaterAction, &QAction::triggered, this, [this, actionIndex, applyAndPersist]() {
            if (actionIndex < 0 || actionIndex >= (m_Impl->m_ActionOrderPreference.size() - 1))
            {
                return;
            }
            m_Impl->m_ActionOrderPreference.swapItemsAt(actionIndex, actionIndex + 1);
            applyAndPersist();
        });
        connect(hideOrShowAction, &QAction::triggered, this,
                [this, actionId, isHidden, visibleActionCount, applyAndPersist]() {
                    if (isHidden)
                    {
                        m_Impl->m_HiddenActionIds.remove(actionId);
                        applyAndPersist();
                        return;
                    }

                    if (visibleActionCount <= 1)
                    {
                        return;
                    }

                    m_Impl->m_HiddenActionIds.insert(actionId);
                    applyAndPersist();
                });
    }

    QMenu* hiddenActionsMenu = customizeMenu->addMenu(tr("Show hidden action"));
    hiddenActionsMenu->setEnabled(!m_Impl->m_HiddenActionIds.isEmpty());
    for (const QString& actionId : m_Impl->m_ActionOrderPreference)
    {
        if (!m_Impl->m_HiddenActionIds.contains(actionId))
        {
            continue;
        }

        QAction* revealAction = hiddenActionsMenu->addAction(actionTitleForId(actionId));
        connect(revealAction, &QAction::triggered, this, [this, actionId, applyAndPersist]() {
            m_Impl->m_HiddenActionIds.remove(actionId);
            applyAndPersist();
        });
    }

    customizeMenu->addSeparator();
    QAction* resetLayoutAction = customizeMenu->addAction(tr("Reset quick actions"));
    connect(resetLayoutAction, &QAction::triggered, this, [this, allActionIds, applyAndPersist]() {
        const QStringList canonicalOrder = canonicalizeActionOrder(allActionIds, allActionIds);
        if (m_Impl->m_ActionOrderPreference == canonicalOrder && m_Impl->m_HiddenActionIds.isEmpty())
        {
            return;
        }

        m_Impl->m_ActionOrderPreference = canonicalOrder;
        m_Impl->m_HiddenActionIds.clear();
        applyAndPersist();
    });
}

void MainDashboard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    synchronizeRecentSectionVisualState();

    // Defer a grid update so thumbnails loaded before the widget was shown
    // (while the grid container had zero width) get placed correctly once
    // Qt has finished computing geometry for the visible layout.
    if (m_Impl->m_RecentFilesGrid)
    {
        QTimer::singleShot(0, m_Impl->m_RecentFilesGrid, [this]() {
            if (m_Impl->m_RecentFilesGrid)
            {
                m_Impl->m_RecentFilesGrid->refreshLayout();
            }
        });
    }
}

void MainDashboard::synchronizeRecentSectionVisualState()
{
    // If the dashboard was hidden while a recent-section transition was in
    // progress, the grid can come back with a stale opacity/effect state.
    // Normalize both containers so recents never remain clickable but
    // visually transparent after view switches/maximize cycles.
    auto stopTransitionGroup = [](QPointer<QParallelAnimationGroup>& transitionGroup) {
        if (!transitionGroup)
        {
            return;
        }

        transitionGroup->stop();
        transitionGroup->deleteLater();
        transitionGroup = nullptr;
    };
    stopTransitionGroup(m_Impl->m_EmptyStateTransitionGroup);
    stopTransitionGroup(m_Impl->m_RecentGridTransitionGroup);

    const bool showRecentGrid = m_Impl->m_RecentSectionState == RecentSectionState::HasResults;
    if (m_Impl->m_EmptyStateCard)
    {
        m_Impl->m_EmptyStateCard->setProperty("dashboardTransitionActive", false);
        m_Impl->m_EmptyStateCard->setVisible(!showRecentGrid);
    }
    if (m_Impl->m_EmptyStateOpacityEffect)
    {
        m_Impl->m_EmptyStateOpacityEffect->setOpacity(showRecentGrid ? 0.0 : 1.0);
        m_Impl->m_EmptyStateOpacityEffect->setEnabled(false);
    }
    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setProperty("dashboardTransitionActive", false);
        m_Impl->m_RecentFilesGrid->setVisible(showRecentGrid);
    }
    if (m_Impl->m_RecentGridOpacityEffect)
    {
        m_Impl->m_RecentGridOpacityEffect->setOpacity(showRecentGrid ? 1.0 : 0.0);
        m_Impl->m_RecentGridOpacityEffect->setEnabled(false);
    }

    if (m_Impl->m_RecentFilesGrid)
    {
        const auto thumbnailCards =
            m_Impl->m_RecentFilesGrid->findChildren<QWidget*>(QStringLiteral("dashboardThumbnailCard"));
        for (QWidget* thumbnailCard : thumbnailCards)
        {
            if (!thumbnailCard)
            {
                continue;
            }

            if (auto* opacityEffect = qobject_cast<QGraphicsOpacityEffect*>(thumbnailCard->graphicsEffect()))
            {
                opacityEffect->setOpacity(1.0);
                opacityEffect->setEnabled(false);
            }

            thumbnailCard->update();
        }
    }
}

void MainDashboard::initializeActionButtonsSection()
{
    const QStringList allActionIds = dashboardActionIds();
    loadActionLayoutPreferences(allActionIds);

    m_Impl->m_DashboardToolbar = new an::ui::DashboardToolbar(this);
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::openFileRequested, this, [this]() {
        recordDashboardLastAction(tr("Open file"));
        emit openFileRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::newSequenceRequested, this, [this]() {
        recordDashboardLastAction(tr("New sequence"));
        emit newSequenceRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::imageIntegrationRequested, this, [this]() {
        recordDashboardLastAction(tr("Image integration"));
        emit imageIntegrationRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::openExtensionManagerRequested, this, [this]() {
        recordDashboardLastAction(tr("Open Extension Manager"));
        emit openExtensionManagerRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::clearRecentFilesRequested, this, [this]() {
        recordDashboardLastAction(tr("Clear recent files"));
        emit clearRecentFilesRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::openSettingsRequested, this, [this]() {
        recordDashboardLastAction(tr("Open settings"));
        emit openSettingsRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::systemInformationRequested, this, [this]() {
        recordDashboardLastAction(tr("Open system info"));
        emit systemInformationRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::aboutRequested, this, [this]() {
        recordDashboardLastAction(tr("Open about dialog"));
        emit aboutRequested();
    });
    connect(m_Impl->m_DashboardToolbar, &an::ui::DashboardToolbar::openHelpRequested, this, [this]() {
        recordDashboardLastAction(tr("Open help"));
        emit openHelpRequested();
    });

    m_Impl->m_DashboardToolbar->setCustomizationMenuBuilder(
        [this](QMenu& menu) { populateQuickActionCustomizationMenu(menu); });
    applyActionLayoutPreferences();
    applyToolbarIcons();
    m_Impl->m_DashboardToolbar->setActionEnabled(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),
                                                 !m_Impl->m_AllRecentFilePaths.empty());

    m_Impl->m_MainLayout->addWidget(m_Impl->m_DashboardToolbar);
}

void MainDashboard::updateRecentSectionState(RecentSectionState state, int visibleCount, int totalCount,
                                             bool hasActiveFilter)
{
    if (!m_Impl->m_RecentFilesHint || !m_Impl->m_EmptyStateCard || !m_Impl->m_RecentFilesGrid)
    {
        return;
    }

    auto applyImmediateState = [](QWidget* widget, QGraphicsOpacityEffect* opacityEffect, bool visible,
                                  QPointer<QParallelAnimationGroup>& transitionGroup) {
        if (transitionGroup)
        {
            transitionGroup->stop();
            transitionGroup->deleteLater();
            transitionGroup = nullptr;
        }

        if (!widget)
        {
            return;
        }

        widget->setProperty("dashboardTransitionActive", false);
        widget->setVisible(visible);

        if (opacityEffect)
        {
            opacityEffect->setOpacity(visible ? 1.0 : 0.0);
            opacityEffect->setEnabled(false);
        }
    };

    if (m_Impl->m_RecentSectionHeader)
    {
        switch (state)
        {
        case RecentSectionState::NoRecents:
            m_Impl->m_RecentSectionHeader->setDescription(QString());
            m_Impl->m_RecentSectionHeader->hide();
            break;
        case RecentSectionState::FilteredEmpty:
            m_Impl->m_RecentSectionHeader->setDescription(tr("No matches in %1 files").arg(totalCount));
            m_Impl->m_RecentSectionHeader->show();
            break;
        case RecentSectionState::HasResults:
            if (hasActiveFilter)
            {
                m_Impl->m_RecentSectionHeader->setDescription(tr("Showing %1 of %2").arg(visibleCount).arg(totalCount));
            }
            else
            {
                m_Impl->m_RecentSectionHeader->setDescription(tr("%1 files").arg(totalCount));
            }
            m_Impl->m_RecentSectionHeader->show();
            break;
        }
    }

    // Hide the search/sort controls row when there are no recent files —
    // searching an empty set makes no sense and clutters the welcome screen.
    const bool showRecentControls = state != RecentSectionState::NoRecents;
    if (m_Impl->m_RecentControlsRow)
    {
        m_Impl->m_RecentControlsRow->setVisible(showRecentControls);
    }
    m_Impl->m_RecentFilesHint->setVisible(showRecentControls);

    switch (state)
    {
    case RecentSectionState::NoRecents:
        m_Impl->m_RecentFilesHint->setText(tr("No recent files found."));
        break;
    case RecentSectionState::FilteredEmpty:
        m_Impl->m_RecentFilesHint->setText(tr("No recent files match the current search."));
        break;
    case RecentSectionState::HasResults:
        if (hasActiveFilter)
        {
            m_Impl->m_RecentFilesHint->setText(tr("Showing %1 of %2 files").arg(visibleCount).arg(totalCount));
        }
        else
        {
            m_Impl->m_RecentFilesHint->setText(tr("Click a thumbnail to open. Right-click for quick actions."));
        }
        break;
    }

    const bool showOnboardingEmpty = state == RecentSectionState::NoRecents;
    const bool showNoMatchEmpty = state == RecentSectionState::FilteredEmpty;
    if (m_Impl->m_EmptyStateOnboardingPanel)
    {
        m_Impl->m_EmptyStateOnboardingPanel->setVisible(showOnboardingEmpty);
    }
    if (m_Impl->m_EmptyStateNoMatchCard)
    {
        m_Impl->m_EmptyStateNoMatchCard->setVisible(showNoMatchEmpty);
    }

    const bool showRecentGrid = state == RecentSectionState::HasResults;
    const bool showEmptyState = !showRecentGrid;

    if (!m_Impl->m_RecentSectionStateInitialized)
    {
        applyImmediateState(m_Impl->m_EmptyStateCard, m_Impl->m_EmptyStateOpacityEffect, showEmptyState,
                            m_Impl->m_EmptyStateTransitionGroup);
        applyImmediateState(m_Impl->m_RecentFilesGrid, m_Impl->m_RecentGridOpacityEffect, showRecentGrid,
                            m_Impl->m_RecentGridTransitionGroup);
        m_Impl->m_RecentSectionStateInitialized = true;
        m_Impl->m_RecentSectionState = state;
        return;
    }

    if (m_Impl->m_RecentSectionState == state)
    {
        // Text/description updates already applied above; widget visibility is unchanged.
        return;
    }

    const bool previousShowRecentGrid = m_Impl->m_RecentSectionState == RecentSectionState::HasResults;
    const bool previousShowEmptyState = !previousShowRecentGrid;
    const RecentSectionState previousState = m_Impl->m_RecentSectionState;
    m_Impl->m_RecentSectionState = state;

    if (previousShowRecentGrid == showRecentGrid && previousShowEmptyState == showEmptyState)
    {
        applyImmediateState(m_Impl->m_EmptyStateCard, m_Impl->m_EmptyStateOpacityEffect, showEmptyState,
                            m_Impl->m_EmptyStateTransitionGroup);
        applyImmediateState(m_Impl->m_RecentFilesGrid, m_Impl->m_RecentGridOpacityEffect, showRecentGrid,
                            m_Impl->m_RecentGridTransitionGroup);
        return;
    }

    // Search-driven transitions (HasResults <-> FilteredEmpty) can fire in
    // quick succession while the user types/backspaces. Running the opacity
    // effect animation for both large container widgets in that hot path can
    // trigger painter re-entrancy warnings inside Qt's effect renderer.
    const bool filterOnlyTransition =
        previousState != RecentSectionState::NoRecents && state != RecentSectionState::NoRecents;
    if (filterOnlyTransition)
    {
        const int transitionFlagDurationMs = an::ui::AnimationUtils::duration(RECENT_SECTION_FADE_DURATION_MS);

        applyImmediateState(m_Impl->m_EmptyStateCard, m_Impl->m_EmptyStateOpacityEffect, showEmptyState,
                            m_Impl->m_EmptyStateTransitionGroup);
        applyImmediateState(m_Impl->m_RecentFilesGrid, m_Impl->m_RecentGridOpacityEffect, showRecentGrid,
                            m_Impl->m_RecentGridTransitionGroup);

        auto markTransitionActive = [transitionFlagDurationMs](QWidget* widget) {
            if (!widget)
            {
                return;
            }

            widget->setProperty("dashboardTransitionActive", true);
            QTimer::singleShot(transitionFlagDurationMs, widget,
                               [widget]() { widget->setProperty("dashboardTransitionActive", false); });
        };

        markTransitionActive(m_Impl->m_EmptyStateCard);
        markTransitionActive(m_Impl->m_RecentFilesGrid);

        if (showRecentGrid && m_Impl->m_RecentFilesGrid)
        {
            m_Impl->m_RecentFilesGrid->refreshLayout();
        }
        return;
    }

    if (showRecentGrid)
    {
        animateRecentSectionWidget(m_Impl->m_EmptyStateCard, m_Impl->m_EmptyStateOpacityEffect, false,
                                   m_Impl->m_EmptyStateTransitionGroup);
        animateRecentSectionWidget(m_Impl->m_RecentFilesGrid, m_Impl->m_RecentGridOpacityEffect, true,
                                   m_Impl->m_RecentGridTransitionGroup);
    }
    else
    {
        animateRecentSectionWidget(m_Impl->m_RecentFilesGrid, m_Impl->m_RecentGridOpacityEffect, false,
                                   m_Impl->m_RecentGridTransitionGroup);
        animateRecentSectionWidget(m_Impl->m_EmptyStateCard, m_Impl->m_EmptyStateOpacityEffect, true,
                                   m_Impl->m_EmptyStateTransitionGroup);
    }
}

void MainDashboard::animateRecentSectionWidget(QWidget* widget, QGraphicsOpacityEffect* opacityEffect, bool showWidget,
                                               QPointer<QParallelAnimationGroup>& transitionGroup)
{
    if (!widget || !opacityEffect)
    {
        if (widget)
        {
            widget->setVisible(showWidget);
        }
        return;
    }

    if (transitionGroup)
    {
        transitionGroup->stop();
        transitionGroup->deleteLater();
        transitionGroup = nullptr;
    }

    if (!an::ui::AnimationUtils::animationsEnabled())
    {
        widget->setProperty("dashboardTransitionActive", false);
        if (showWidget)
        {
            widget->show();
        }
        opacityEffect->setOpacity(showWidget ? 1.0 : 0.0);
        opacityEffect->setEnabled(false);
        if (!showWidget)
        {
            widget->hide();
        }
        else
        {
            if (m_Impl->m_RecentFilesGrid)
            {
                m_Impl->m_RecentFilesGrid->refreshLayout();
            }
        }
        return;
    }

    if (showWidget)
    {
        widget->show();
    }

    m_Impl->m_MainLayout->activate();

    const QPoint endPos = widget->pos();
    const int offset = scaledPixels(m_Impl->m_DpiScale, RECENT_SECTION_SLIDE_OFFSET_PX, 8);
    const QPoint startPos = showWidget ? QPoint(endPos.x(), endPos.y() + offset) : endPos;
    const QPoint finishPos = showWidget ? endPos : QPoint(endPos.x(), endPos.y() - offset);
    const int fadeDurationMs = an::ui::AnimationUtils::duration(RECENT_SECTION_FADE_DURATION_MS);
    const int slideDurationMs = an::ui::AnimationUtils::duration(RECENT_SECTION_SLIDE_DURATION_MS);

    widget->setProperty("dashboardTransitionActive", true);
    widget->move(startPos);
    opacityEffect->setEnabled(true);
    opacityEffect->setOpacity(showWidget ? 0.0 : 1.0);

    auto* animationGroup = new QParallelAnimationGroup(this);
    auto* opacityAnimation = new QPropertyAnimation(opacityEffect, "opacity", animationGroup);
    opacityAnimation->setDuration(fadeDurationMs);
    opacityAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    opacityAnimation->setStartValue(showWidget ? 0.0 : 1.0);
    opacityAnimation->setEndValue(showWidget ? 1.0 : 0.0);

    auto* slideAnimation = new QPropertyAnimation(widget, "pos", animationGroup);
    slideAnimation->setDuration(slideDurationMs);
    slideAnimation->setEasingCurve(showWidget ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    slideAnimation->setStartValue(startPos);
    slideAnimation->setEndValue(finishPos);

    transitionGroup = animationGroup;
    connect(animationGroup, &QParallelAnimationGroup::finished, this,
            [this, widget, opacityEffect, showWidget, endPos]() {
                widget->setProperty("dashboardTransitionActive", false);
                widget->move(endPos);
                opacityEffect->setOpacity(showWidget ? 1.0 : 0.0);
                opacityEffect->setEnabled(false);
                if (!showWidget)
                {
                    widget->hide();
                }
                else
                {
                    // After the grid container becomes visible, force a layout
                    // recalculation so thumbnails that loaded during the animation
                    // are placed correctly.
                    if (m_Impl->m_RecentFilesGrid)
                    {
                        m_Impl->m_RecentFilesGrid->refreshLayout();
                    }
                }
            });

    animationGroup->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainDashboard::initializeRecentFilesSection()
{
    m_Impl->m_RecentSectionHeader = new an::ui::AnSectionHeader(tr("Recent"), QString(), QString(), this);
    m_Impl->m_RecentSectionHeader->setObjectName("dashboardRecentSectionHeader");
    m_Impl->m_MainLayout->addWidget(m_Impl->m_RecentSectionHeader);

    auto* controlsRow = new QWidget(this);
    controlsRow->setObjectName("dashboardRecentControlsRow");
    m_Impl->m_RecentControlsRow = controlsRow;
    auto* controlsLayout = new QHBoxLayout(controlsRow);
    controlsLayout->setContentsMargins(0, 0, scaledPixels(m_Impl->m_DpiScale, 6, 4), 0);
    controlsLayout->setSpacing((std::max)(6, static_cast<int>(std::lround(8.0 * m_Impl->m_DpiScale))));

    m_Impl->m_RecentSearchInput = new QLineEdit(controlsRow);
    m_Impl->m_RecentSearchInput->setObjectName("dashboardRecentSearchInput");
    m_Impl->m_RecentSearchInput->setPlaceholderText(tr("Search by name, type, or path..."));
    m_Impl->m_RecentSearchInput->setToolTip(
        tr("Filter recent files by name, type, or path. Supports partial matches.\nSearch recent files (Ctrl+F)"));
    m_Impl->m_RecentSearchInput->setClearButtonEnabled(true);
    m_Impl->m_RecentSearchInput->setFont(an::ui::tokens().typography.body);

    m_Impl->m_SearchAction = m_Impl->m_RecentSearchInput->addAction(QIcon(), QLineEdit::LeadingPosition);
    m_Impl->m_SearchAction->setObjectName("dashboardSearchAction");

    auto* searchShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (m_Impl->m_RecentSearchInput && m_Impl->m_RecentSearchInput->isVisible())
        {
            m_Impl->m_RecentSearchInput->setFocus();
            m_Impl->m_RecentSearchInput->selectAll();
        }
    });

    controlsLayout->addWidget(m_Impl->m_RecentSearchInput, 1);

    m_Impl->m_RecentSortCombo = new QComboBox(controlsRow);
    m_Impl->m_RecentSortCombo->setObjectName("dashboardRecentSortCombo");
    m_Impl->m_RecentSortCombo->setToolTip(tr("Sort recent files by date, name, or file type"));
    m_Impl->m_RecentSortCombo->setFont(an::ui::tokens().typography.body);
    m_Impl->m_RecentSortCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_Impl->m_RecentSortCombo->setMinimumWidth(scaledPixels(m_Impl->m_DpiScale, 132, 118));
    m_Impl->m_RecentSortCombo->addItem(tr("Recent"), RECENT_SORT_RECENT);
    m_Impl->m_RecentSortCombo->addItem(tr("Name"), RECENT_SORT_NAME);
    m_Impl->m_RecentSortCombo->addItem(tr("Type"), RECENT_SORT_TYPE);
    controlsLayout->addWidget(m_Impl->m_RecentSortCombo, 0);
    loadRecentSortPreference();
    loadRecentSearchPreference();

    connect(m_Impl->m_RecentSearchInput, &QLineEdit::textChanged, this, [this](const QString&) {
        persistRecentSearchPreference();
        if (m_Impl->m_RecentFilesGrid)
        {
            m_Impl->m_RecentFilesGrid->setSearchFilter(m_Impl->m_RecentSearchInput->text());
        }
    });
    connect(m_Impl->m_RecentSortCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        persistRecentSortPreference();
        if (m_Impl->m_RecentFilesGrid)
        {
            m_Impl->m_RecentFilesGrid->setSortMode(m_Impl->m_RecentSortCombo->currentData().toInt());
        }
    });

    m_Impl->m_MainLayout->addWidget(controlsRow);

    m_Impl->m_RecentFilesHint = new QLabel(this);
    m_Impl->m_RecentFilesHint->setObjectName("dashboardRecentFilesHint");
    m_Impl->m_RecentFilesHint->setFont(an::ui::tokens().typography.caption);
    m_Impl->m_MainLayout->addWidget(m_Impl->m_RecentFilesHint);

    // ── Empty-state container ────────────────────────────────────────
    // A plain QWidget holds the onboarding panel and no-match card side
    // by side (only one visible at a time).  The container is the target
    // for the opacity/slide animations when transitioning to/from the
    // recent-files grid.
    m_Impl->m_EmptyStateCard = new QWidget(this);
    m_Impl->m_EmptyStateCard->setObjectName("dashboardEmptyStateCard");
    auto* emptyContainerLayout = new QVBoxLayout(m_Impl->m_EmptyStateCard);
    emptyContainerLayout->setContentsMargins(0, 0, 0, 0);
    emptyContainerLayout->setSpacing(0);

    // ── Onboarding Panel (Options A+B+C+D+E) ───────────────────────
    m_Impl->m_EmptyStateOnboardingPanel =
        new an::ui::DashboardOnboardingPanel(m_Impl->m_DpiScale, m_Impl->m_EmptyStateCard);
    connect(m_Impl->m_EmptyStateOnboardingPanel, &an::ui::DashboardOnboardingPanel::openFileRequested, this, [this]() {
        recordDashboardLastAction(tr("Open file"));
        emit openFileRequested();
    });
    connect(m_Impl->m_EmptyStateOnboardingPanel, &an::ui::DashboardOnboardingPanel::newSequenceRequested, this,
            [this]() {
                recordDashboardLastAction(tr("New sequence"));
                emit newSequenceRequested();
            });
    connect(m_Impl->m_EmptyStateOnboardingPanel, &an::ui::DashboardOnboardingPanel::imageIntegrationRequested, this,
            [this]() {
                recordDashboardLastAction(tr("Image integration"));
                emit imageIntegrationRequested();
            });
    emptyContainerLayout->addWidget(m_Impl->m_EmptyStateOnboardingPanel);

    // ── No-Match Card ───────────────────────────────────────────────
    m_Impl->m_EmptyStateNoMatchCard = new an::ui::AnCardWidget(QString(), m_Impl->m_EmptyStateCard);
    m_Impl->m_EmptyStateNoMatchCard->setObjectName("dashboardEmptyStateNoMatchPanel");
    auto* noMatchCardLayout = new QVBoxLayout();
    noMatchCardLayout->setContentsMargins(
        scaledPixels(m_Impl->m_DpiScale, 20, 14), scaledPixels(m_Impl->m_DpiScale, 20, 14),
        scaledPixels(m_Impl->m_DpiScale, 20, 14), scaledPixels(m_Impl->m_DpiScale, 20, 14));
    noMatchCardLayout->setSpacing(scaledPixels(m_Impl->m_DpiScale, 8, 6));
    noMatchCardLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_Impl->m_EmptyStateNoMatchCard->setContentLayout(noMatchCardLayout);

    m_Impl->m_EmptyStateNoMatchTitle =
        new QLabel(tr("No matches found"), m_Impl->m_EmptyStateNoMatchCard->contentWidget());
    m_Impl->m_EmptyStateNoMatchTitle->setObjectName("dashboardEmptyStateNoMatchTitle");
    m_Impl->m_EmptyStateNoMatchTitle->setAlignment(Qt::AlignCenter);
    m_Impl->m_EmptyStateNoMatchTitle->setWordWrap(true);
    noMatchCardLayout->addWidget(m_Impl->m_EmptyStateNoMatchTitle);

    m_Impl->m_EmptyStateNoMatchDescription = new QLabel(tr("Try a different search term to see your recent files."),
                                                        m_Impl->m_EmptyStateNoMatchCard->contentWidget());
    m_Impl->m_EmptyStateNoMatchDescription->setObjectName("dashboardEmptyStateNoMatchDescription");
    m_Impl->m_EmptyStateNoMatchDescription->setAlignment(Qt::AlignCenter);
    m_Impl->m_EmptyStateNoMatchDescription->setWordWrap(true);
    noMatchCardLayout->addWidget(m_Impl->m_EmptyStateNoMatchDescription);
    emptyContainerLayout->addWidget(m_Impl->m_EmptyStateNoMatchCard);

    m_Impl->m_MainLayout->addWidget(m_Impl->m_EmptyStateCard);
    m_Impl->m_EmptyStateOpacityEffect = new QGraphicsOpacityEffect(m_Impl->m_EmptyStateCard);
    m_Impl->m_EmptyStateOpacityEffect->setOpacity(1.0);
    m_Impl->m_EmptyStateOpacityEffect->setEnabled(false);
    m_Impl->m_EmptyStateCard->setGraphicsEffect(m_Impl->m_EmptyStateOpacityEffect);
    m_Impl->m_EmptyStateCard->setProperty("dashboardTransitionActive", false);

    m_Impl->m_RecentFilesGrid = new an::ui::RecentFilesGrid(this);
    m_Impl->m_RecentFilesGrid->setTileMetrics(m_Impl->m_TileSize, m_Impl->m_TileDistance);
    m_Impl->m_RecentFilesGrid->setDarkMode(isDarkMode());
    m_Impl->m_RecentFilesGrid->setPinnedFilePaths(m_Impl->m_PinnedRecentFiles);
    if (m_Impl->m_RecentSortCombo)
    {
        m_Impl->m_RecentFilesGrid->setSortMode(m_Impl->m_RecentSortCombo->currentData().toInt());
    }
    if (m_Impl->m_RecentSearchInput)
    {
        m_Impl->m_RecentFilesGrid->setSearchFilter(m_Impl->m_RecentSearchInput->text());
    }

    m_Impl->m_RecentFilesGrid->setThumbnailPathResolver(
        [](const QString& filePath) { return selectThumbnailPath(filePath); });

    connect(m_Impl->m_RecentFilesGrid, &an::ui::RecentFilesGrid::stateChanged, this,
            [this](const int visibleCount, const int totalCount, const bool hasActiveFilter) {
                RecentSectionState sectionState = RecentSectionState::HasResults;
                if (totalCount <= 0)
                {
                    sectionState = RecentSectionState::NoRecents;
                }
                else if (visibleCount <= 0)
                {
                    sectionState = RecentSectionState::FilteredEmpty;
                }

                updateRecentSectionState(sectionState, visibleCount, totalCount, hasActiveFilter);

                if (m_Impl->m_DashboardToolbar)
                {
                    m_Impl->m_DashboardToolbar->setActionEnabled(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),
                                                                 !m_Impl->m_AllRecentFilePaths.empty());
                }
            });

    connect(m_Impl->m_RecentFilesGrid, &an::ui::RecentFilesGrid::openFileRequested, this,
            [this](const QString& filePath) {
                recordDashboardLastAction(tr("Open recent file"));
                promoteRecentFileToFront(filePath);
                emit openRecentFileRequested(filePath);
            });

    // Update the hero card immediately when the user clicks a thumbnail, using
    // the image already stored in the grid's thumbnail cache.  This happens
    // synchronously before the file is opened, so the dashboard shows the
    // correct hero without waiting for an async presenter decode.
    // When a hero file is pinned, skip the instant preview update so the
    // pinned hero remains visible until the dashboard is hidden.
    connect(m_Impl->m_RecentFilesGrid, &an::ui::RecentFilesGrid::openFilePreviewReady, this,
            [this](const QString& filePath, const QImage& previewImage, const QSize& sourceSize) {
                if (!m_Impl->m_FeaturedFileCard || previewImage.isNull())
                {
                    return;
                }

                // A pinned hero must not be replaced even for a fraction of a
                // second — skip the instant preview so the pinned image stays.
                if (!m_Impl->m_PinnedHeroFilePath.isEmpty())
                {
                    return;
                }

                const QFileInfo fileInfo(filePath);
                m_Impl->m_FeaturedFileCard->setFeaturedFile(
                    filePath, fileInfo.fileName(),
                    m_Impl->m_FeaturedFilePresenter->metadataTextForFile(filePath, sourceSize),
                    QPixmap::fromImage(previewImage), false);
                m_Impl->m_FeaturedFileCard->show();
            });

    connect(m_Impl->m_RecentFilesGrid, &an::ui::RecentFilesGrid::removeFileRequested, this,
            [this](const QString& filePath) {
                recordDashboardLastAction(tr("Remove recent file"));
                setRecentFilePinned(filePath, false);
                emit removeRecentFileRequested(filePath);
                reloadRecentFilesThumbnails();
            });

    connect(m_Impl->m_RecentFilesGrid, &an::ui::RecentFilesGrid::pinToggled, this,
            [this](const QString& filePath, const bool pinned) { setRecentFilePinned(filePath, pinned); });

    if (m_Impl->m_FeaturedFilePresenter)
    {
        m_Impl->m_FeaturedFilePresenter->setThumbnailCache(m_Impl->m_RecentFilesGrid->thumbnailCache());
    }

    m_Impl->m_MainLayout->addWidget(m_Impl->m_RecentFilesGrid);

    m_Impl->m_RecentGridOpacityEffect = new QGraphicsOpacityEffect(m_Impl->m_RecentFilesGrid);
    m_Impl->m_RecentGridOpacityEffect->setOpacity(0.0);
    m_Impl->m_RecentGridOpacityEffect->setEnabled(false);
    m_Impl->m_RecentFilesGrid->setGraphicsEffect(m_Impl->m_RecentGridOpacityEffect);
    m_Impl->m_RecentFilesGrid->hide();

    // Determine initial state *before* the first paint.  When recent files
    // already exist we skip the NoRecents empty-state entirely so the
    // onboarding card is never briefly shown before the thumbnail grid
    // replaces it, eliminating the visual "jump" on startup.
    const auto recentFiles = currentRecentFiles();
    const bool hasRecentsAtInit = !recentFiles.empty();

    if (hasRecentsAtInit)
    {
        // Pre-set the section to HasResults so that the empty-state card is
        // never made visible.  The grid's stateChanged signal will refine
        // the counts once thumbnails are loaded.
        m_Impl->m_RecentSectionStateInitialized = false;
        m_Impl->m_RecentSectionState = RecentSectionState::HasResults;
        updateRecentSectionState(RecentSectionState::HasResults, 0, static_cast<int>(recentFiles.size()), false);
    }
    else
    {
        m_Impl->m_RecentSectionStateInitialized = false;
        m_Impl->m_RecentSectionState = RecentSectionState::NoRecents;
        updateRecentSectionState(RecentSectionState::NoRecents, 0, 0, false);
    }

    queueRecentThumbnails(recentFiles);
}

void MainDashboard::loadDashboardStyleSheet()
{
    // Keep dashboard styling minimal and theme-driven.
    // Component widgets and AnProxyStyle own their visual treatment.
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    // Recent files hint
    if (m_Impl->m_RecentFilesHint)
    {
        const auto themeVariant =
            isDarkMode() ? an::ui::styling::ThemeVariant::Dark : an::ui::styling::ThemeVariant::Light;
        const QColor hintColor =
            an::ui::styling::semanticColor(themeVariant, an::ui::styling::SemanticColorRole::TextMuted);
        QPalette pal = m_Impl->m_RecentFilesHint->palette();
        pal.setColor(QPalette::WindowText, hintColor);
        m_Impl->m_RecentFilesHint->setPalette(pal);
        m_Impl->m_RecentFilesHint->setFont(an::ui::tokens().typography.caption);
    }
}

void MainDashboard::updateThemeStyles()
{
    loadDashboardStyleSheet();

    const bool darkMode = isDarkMode();

    // Batch visual updates to avoid intermediate repaints while refreshing
    // shadow colors and thumbnail tints across all child widgets.
    setUpdatesEnabled(false);

    if (m_Impl->m_DashboardIdentityBar)
    {
        m_Impl->m_DashboardIdentityBar->refreshTheme();
    }

    if (m_Impl->m_FeaturedFileCard)
    {
        m_Impl->m_FeaturedFileCard->refreshTheme();
    }

    if (m_Impl->m_DashboardToolbar)
    {
        m_Impl->m_DashboardToolbar->style()->unpolish(m_Impl->m_DashboardToolbar);
        m_Impl->m_DashboardToolbar->style()->polish(m_Impl->m_DashboardToolbar);
        m_Impl->m_DashboardToolbar->update();
    }

    // Style the hero-recents divider with the subtle border color (E)
    if (m_Impl->m_HeroRecentDivider)
    {
        QPalette dividerPal = m_Impl->m_HeroRecentDivider->palette();
        dividerPal.setColor(QPalette::WindowText, an::ui::tokens().colors.borderSubtle);
        m_Impl->m_HeroRecentDivider->setPalette(dividerPal);
    }

    if (m_Impl->m_RecentSectionHeader)
    {
        m_Impl->m_RecentSectionHeader->refreshTheme();
    }

    if (m_Impl->m_EmptyStateOnboardingPanel)
    {
        m_Impl->m_EmptyStateOnboardingPanel->refreshTheme();
    }

    if (m_Impl->m_EmptyStateNoMatchCard)
    {
        m_Impl->m_EmptyStateNoMatchCard->refreshTheme();
    }

    const auto& tokenSet = an::ui::tokens();

    if (m_Impl->m_EmptyStateNoMatchTitle)
    {
        m_Impl->m_EmptyStateNoMatchTitle->setFont(tokenSet.typography.heading2);
        QPalette pal = m_Impl->m_EmptyStateNoMatchTitle->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textPrimary);
        m_Impl->m_EmptyStateNoMatchTitle->setPalette(pal);
    }
    if (m_Impl->m_EmptyStateNoMatchDescription)
    {
        m_Impl->m_EmptyStateNoMatchDescription->setFont(tokenSet.typography.body);
        QPalette pal = m_Impl->m_EmptyStateNoMatchDescription->palette();
        pal.setColor(QPalette::WindowText, tokenSet.colors.textSecondary);
        m_Impl->m_EmptyStateNoMatchDescription->setPalette(pal);
    }

    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setDarkMode(darkMode);
        m_Impl->m_RecentFilesGrid->refreshTheme();
    }

    setUpdatesEnabled(true);
}

void MainDashboard::ensureDashboardSettingsSchema()
{
    if (!globalSettings())
    {
        return;
    }

    const QString formatKey = QString::fromUtf8(DASHBOARD_SETTINGS_SCHEMA_VERSION_KEY);
    const QString current = QString::number(DASHBOARD_SETTINGS_SCHEMA_VERSION);
    const QString stated = selectGlobalSetting(formatKey, QString()).trimmed();
    if (stated == current)
    {
        return;
    }

    // Settings of another format - older, newer, or keys that state none - are never read: each one stored is set aside
    // under its own name with the format it states beside it, and the dashboard starts from its defaults.
    bool numbered = false;
    const int statedFormat = stated.toInt(&numbered);
    const QString suffix =
        numbered && statedFormat > 0 ? QStringLiteral(".format%1").arg(statedFormat) : QStringLiteral(".unversioned");
    QStringList setAside;
    for (const char* key : DASHBOARD_SETTINGS_KEYS)
    {
        const QString name = QString::fromUtf8(key);
        const QString value = selectGlobalSetting(name, QString());
        if (value.isEmpty())
        {
            continue;
        }
        upsertGlobalSetting(name + suffix, value);
        upsertGlobalSetting(name, QString());
        setAside << name;
    }
    if (!setAside.isEmpty())
    {
        LOG_WARNING("MainDashboard: the dashboard's settings {}; this version of AfterNight reads format {}. They were "
                    "set aside under their names with '{}' beside them, and the dashboard starts from its defaults.",
                    numbered && statedFormat > 0 ? fmt::format("were written in format {}", statedFormat)
                                                 : std::string("state no format version"),
                    DASHBOARD_SETTINGS_SCHEMA_VERSION, suffix.toStdString());
    }
    upsertGlobalSetting(formatKey, current);
}

void MainDashboard::loadActionLayoutPreferences(const QStringList& allActionIds)
{
    m_Impl->m_ActionOrderPreference = canonicalizeActionOrder(allActionIds, allActionIds);
    m_Impl->m_HiddenActionIds.clear();

    ensureDashboardSettingsSchema();
    if (!globalSettings())
    {
        return;
    }

    const QString serializedOrder = selectGlobalSetting(QString::fromUtf8(DASHBOARD_ACTION_ORDER_KEY), QString());
    const QString serializedHidden = selectGlobalSetting(QString::fromUtf8(DASHBOARD_HIDDEN_ACTIONS_KEY), QString());

    const QStringList canonicalOrder = canonicalizeActionOrder(allActionIds, deserializeActionIdList(serializedOrder));
    const QSet<QString> canonicalHidden =
        canonicalizeHiddenActionIds(allActionIds, deserializeActionIdList(serializedHidden));
    const QStringList canonicalHiddenOrder = hiddenActionIdsInCanonicalOrder(allActionIds, canonicalHidden);

    m_Impl->m_ActionOrderPreference = canonicalOrder;
    m_Impl->m_HiddenActionIds = canonicalHidden;

    const QString normalizedOrder = serializeActionIdList(canonicalOrder);
    const QString normalizedHidden = serializeActionIdList(canonicalHiddenOrder);
    if (serializedOrder != normalizedOrder || serializedHidden != normalizedHidden)
    {
        persistActionLayoutPreferences(allActionIds);
    }
}

void MainDashboard::persistActionLayoutPreferences(const QStringList& allActionIds) const
{
    if (!globalSettings())
    {
        return;
    }

    const QStringList canonicalOrder = canonicalizeActionOrder(allActionIds, m_Impl->m_ActionOrderPreference);
    const QStringList canonicalHiddenOrder = hiddenActionIdsInCanonicalOrder(allActionIds, m_Impl->m_HiddenActionIds);
    upsertGlobalSetting(QString::fromUtf8(DASHBOARD_ACTION_ORDER_KEY), serializeActionIdList(canonicalOrder));
    upsertGlobalSetting(QString::fromUtf8(DASHBOARD_HIDDEN_ACTIONS_KEY), serializeActionIdList(canonicalHiddenOrder));
}

void MainDashboard::loadRecentSortPreference()
{
    if (!m_Impl->m_RecentSortCombo)
    {
        return;
    }

    ensureDashboardSettingsSchema();
    if (!globalSettings())
    {
        return;
    }

    const QString persistedValue = selectGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SORT_MODE_KEY), QString());
    const int sortMode = deserializeRecentSortMode(persistedValue);
    const QString canonicalValue = serializeRecentSortMode(sortMode);
    if (persistedValue.trimmed().toLower() != canonicalValue)
    {
        upsertGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SORT_MODE_KEY), canonicalValue);
    }

    const int comboIndex = m_Impl->m_RecentSortCombo->findData(sortMode);
    if (comboIndex >= 0 && comboIndex != m_Impl->m_RecentSortCombo->currentIndex())
    {
        const QSignalBlocker blocker(m_Impl->m_RecentSortCombo);
        m_Impl->m_RecentSortCombo->setCurrentIndex(comboIndex);
    }
}

void MainDashboard::persistRecentSortPreference() const
{
    if (!m_Impl->m_RecentSortCombo || !globalSettings())
    {
        return;
    }

    const int sortMode = m_Impl->m_RecentSortCombo->currentData().toInt();
    upsertGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SORT_MODE_KEY), serializeRecentSortMode(sortMode));
}

void MainDashboard::loadRecentSearchPreference()
{
    if (!m_Impl->m_RecentSearchInput)
    {
        return;
    }

    ensureDashboardSettingsSchema();
    if (!globalSettings())
    {
        return;
    }

    const QString persistedSearch = selectGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SEARCH_TEXT_KEY), QString());
    const QString normalizedSearch = persistedSearch.trimmed();
    if (persistedSearch != normalizedSearch)
    {
        upsertGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SEARCH_TEXT_KEY), normalizedSearch);
    }

    if (m_Impl->m_RecentSearchInput->text() != normalizedSearch)
    {
        const QSignalBlocker blocker(m_Impl->m_RecentSearchInput);
        m_Impl->m_RecentSearchInput->setText(normalizedSearch);
    }
}

void MainDashboard::persistRecentSearchPreference() const
{
    if (!m_Impl->m_RecentSearchInput || !globalSettings())
    {
        return;
    }

    upsertGlobalSetting(QString::fromUtf8(DASHBOARD_RECENT_SEARCH_TEXT_KEY),
                        m_Impl->m_RecentSearchInput->text().trimmed());
}

void MainDashboard::recordDashboardLastAction(const QString& actionText)
{
    const QString trimmedAction = actionText.trimmed();
    if (trimmedAction.isEmpty())
    {
        return;
    }

    if (globalSettings())
    {
        upsertGlobalSetting(QString::fromUtf8(DASHBOARD_LAST_ACTION_KEY), trimmedAction);
    }
}

void MainDashboard::refreshFeaturedFileCard()
{
    if (!m_Impl->m_FeaturedFilePresenter || !m_Impl->m_FeaturedFileCard)
    {
        return;
    }

    if (m_Impl->m_AllRecentFilePaths.empty())
    {
        m_Impl->m_FeaturedFileCard->clearFeaturedFile();
        m_Impl->m_FeaturedFileCard->hide();
        return;
    }

    std::deque<QString> candidateRecentPaths = m_Impl->m_AllRecentFilePaths;
    const QString preferredHeroPath = normalizeRecentFilePath(m_Impl->m_PreferredHeroFilePath);

    // A pinned hero strictly overrides any preferred/promoted file.
    if (!preferredHeroPath.isEmpty() && m_Impl->m_PinnedHeroFilePath.isEmpty())
    {
        auto preferredIt = std::find(candidateRecentPaths.begin(), candidateRecentPaths.end(), preferredHeroPath);
        if (preferredIt != candidateRecentPaths.end())
        {
            if (preferredIt != candidateRecentPaths.begin())
            {
                candidateRecentPaths.erase(preferredIt);
                candidateRecentPaths.push_front(preferredHeroPath);
            }
            m_Impl->m_FeaturedFilePresenter->setPinnedHeroFilePath(QString());

            // Immediately update the hero card from the grid thumbnail cache
            // so the user sees the correct file without waiting for the async
            // presenter decode.  The async path will refine the image later
            // if a higher-resolution decode is available.
            if (m_Impl->m_RecentFilesGrid)
            {
                DashboardThumbnailCache* cache = m_Impl->m_RecentFilesGrid->thumbnailCache();
                if (cache)
                {
                    const QString key = DashboardThumbnailCache::cacheKey(preferredHeroPath, m_Impl->m_TileSize);
                    if (const auto* entry = cache->find(key); entry && !entry->displayImage.isNull())
                    {
                        const QFileInfo fi(preferredHeroPath);
                        m_Impl->m_FeaturedFileCard->setFeaturedFile(
                            preferredHeroPath, fi.fileName(),
                            m_Impl->m_FeaturedFilePresenter->metadataTextForFile(preferredHeroPath, entry->sourceSize),
                            QPixmap::fromImage(entry->displayImage), false);
                        m_Impl->m_FeaturedFileCard->show();
                    }
                    else
                    {
                        // Cache miss for preferred hero — fall through to async presenter.
                    }
                }
            }
        }
        else
        {
            m_Impl->m_PreferredHeroFilePath.clear();
            m_Impl->m_FeaturedFilePresenter->setPinnedHeroFilePath(m_Impl->m_PinnedHeroFilePath);
        }
    }
    else
    {
        m_Impl->m_FeaturedFilePresenter->setPinnedHeroFilePath(m_Impl->m_PinnedHeroFilePath);
    }

    m_Impl->m_FeaturedFilePresenter->setThumbnailCacheTileSize(m_Impl->m_FeaturedThumbnailCacheTileSize);
    m_Impl->m_FeaturedFilePresenter->setRecentFilePaths(candidateRecentPaths);
    m_Impl->m_FeaturedFilePresenter->refresh();
}

void MainDashboard::promoteRecentFileToFront(const QString& filePath)
{
    const QString normalizedPath = normalizeRecentFilePath(filePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    m_Impl->m_PendingPromotedRecentFilePath = normalizedPath;

    // Only update the preferred hero when no hero is pinned.  A pinned hero
    // must remain stable and must not flicker to the clicked file.
    if (m_Impl->m_PinnedHeroFilePath.isEmpty())
    {
        m_Impl->m_PreferredHeroFilePath = normalizedPath;
    }

    auto existingIt =
        std::find(m_Impl->m_AllRecentFilePaths.begin(), m_Impl->m_AllRecentFilePaths.end(), normalizedPath);
    if (existingIt == m_Impl->m_AllRecentFilePaths.begin())
    {
        m_Impl->m_PendingPromotedRecentFilePath.clear();
        m_Impl->m_PendingPromotionBaselineRecents.clear();
        refreshFeaturedFileCard();
        return;
    }

    m_Impl->m_PendingPromotedRecentFilePath = normalizedPath;
    m_Impl->m_PendingPromotionBaselineRecents = m_Impl->m_AllRecentFilePaths;

    if (existingIt != m_Impl->m_AllRecentFilePaths.end())
    {
        m_Impl->m_AllRecentFilePaths.erase(existingIt);
    }

    m_Impl->m_AllRecentFilePaths.push_front(normalizedPath);
    refreshFeaturedFileCard();
    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setRecentFiles(m_Impl->m_AllRecentFilePaths);
    }
}

void MainDashboard::loadPinnedRecentFilesPreference()
{
    m_Impl->m_PinnedRecentFiles.clear();

    if (!globalSettings())
    {
        return;
    }

    const QString serializedPinned =
        selectGlobalSetting(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS), QString());
    if (serializedPinned.isEmpty())
    {
        return;
    }

    const QStringList entries = serializedPinned.split('\n', Qt::SkipEmptyParts);
    for (const QString& entry : entries)
    {
        const QString normalizedPath = normalizeRecentFilePath(entry);
        if (!normalizedPath.isEmpty())
        {
            m_Impl->m_PinnedRecentFiles.insert(normalizedPath);
        }
    }
}

void MainDashboard::persistPinnedRecentFilesPreference() const
{
    if (!globalSettings())
    {
        return;
    }

    QStringList serializedEntries;
    serializedEntries.reserve(m_Impl->m_PinnedRecentFiles.size());
    for (const QString& path : m_Impl->m_PinnedRecentFiles)
    {
        serializedEntries.push_back(path);
    }

    std::sort(serializedEntries.begin(), serializedEntries.end(),
              [](const QString& lhs, const QString& rhs) { return QString::localeAwareCompare(lhs, rhs) < 0; });

    upsertGlobalSetting(QString::fromUtf8(SettingsKeys::INTERFACE_DASHBOARD_PINNED_RECENTS),
                        serializedEntries.join('\n'));
}

void MainDashboard::loadPinnedHeroFilePreference()
{
    m_Impl->m_PinnedHeroFilePath.clear();

    if (!globalSettings())
    {
        return;
    }

    // Only normalized: a file that is gone is shown as missing where the card is drawn, so no stat is made here on
    // every dashboard open.
    const QString persistedPinnedHeroPath =
        selectGlobalSetting(QString::fromUtf8(DASHBOARD_PINNED_HERO_FILE_KEY), QString());
    const QString normalizedPinnedHeroPath = normalizeRecentFilePath(persistedPinnedHeroPath);

    m_Impl->m_PinnedHeroFilePath = normalizedPinnedHeroPath;
    if (persistedPinnedHeroPath.trimmed() != normalizedPinnedHeroPath)
    {
        persistPinnedHeroFilePreference();
    }
}

void MainDashboard::persistPinnedHeroFilePreference() const
{
    if (!globalSettings())
    {
        return;
    }

    upsertGlobalSetting(QString::fromUtf8(DASHBOARD_PINNED_HERO_FILE_KEY), m_Impl->m_PinnedHeroFilePath.trimmed());
}

void MainDashboard::setPinnedHeroFile(const QString& filePath, const bool pinned)
{
    const QString normalizedPath = normalizeRecentFilePath(filePath);
    QString updatedPinnedPath = m_Impl->m_PinnedHeroFilePath;

    if (pinned)
    {
        if (normalizedPath.isEmpty())
        {
            return;
        }
        updatedPinnedPath = normalizedPath;
    }
    else
    {
        if (normalizeRecentFilePath(updatedPinnedPath) != normalizedPath)
        {
            return;
        }
        updatedPinnedPath.clear();
    }

    if (updatedPinnedPath == m_Impl->m_PinnedHeroFilePath)
    {
        return;
    }

    m_Impl->m_PinnedHeroFilePath = updatedPinnedPath;
    if (pinned)
    {
        m_Impl->m_PreferredHeroFilePath.clear();
    }
    persistPinnedHeroFilePreference();
    refreshFeaturedFileCard();
}

void MainDashboard::setRecentFilePinned(const QString& filePath, bool pinned)
{
    const QString normalizedPath = normalizeRecentFilePath(filePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    bool changed = false;
    if (pinned)
    {
        const int sizeBefore = m_Impl->m_PinnedRecentFiles.size();
        m_Impl->m_PinnedRecentFiles.insert(normalizedPath);
        changed = m_Impl->m_PinnedRecentFiles.size() != sizeBefore;
    }
    else
    {
        changed = m_Impl->m_PinnedRecentFiles.remove(normalizedPath);
    }

    if (changed)
    {
        persistPinnedRecentFilesPreference();
        if (m_Impl->m_RecentFilesGrid)
        {
            m_Impl->m_RecentFilesGrid->setPinnedFilePaths(m_Impl->m_PinnedRecentFiles);
        }
    }
}

bool MainDashboard::isRecentFilePinned(const QString& filePath) const
{
    const QString normalizedPath = normalizeRecentFilePath(filePath);
    return !normalizedPath.isEmpty() && m_Impl->m_PinnedRecentFiles.contains(normalizedPath);
}

void MainDashboard::queueRecentThumbnails(const std::deque<std::string>& recentFiles)
{
    const QString previousMostRecentPath =
        m_Impl->m_AllRecentFilePaths.empty() ? QString() : m_Impl->m_AllRecentFilePaths.front();

    std::deque<QString> normalizedRecentPaths;
    QSet<QString> normalizedRecentSet;
    for (const auto& file : recentFiles)
    {
        const QString normalizedPath = normalizeRecentFilePath(QString::fromUtf8(file));
        if (normalizedPath.isEmpty() || normalizedRecentSet.contains(normalizedPath))
        {
            continue;
        }

        normalizedRecentSet.insert(normalizedPath);
        normalizedRecentPaths.emplace_back(normalizedPath);
    }

    const QString pendingPromotedPath = normalizeRecentFilePath(m_Impl->m_PendingPromotedRecentFilePath);
    bool pendingPromotionApplied = false;
    if (!pendingPromotedPath.isEmpty())
    {
        auto pendingIt = std::find(normalizedRecentPaths.begin(), normalizedRecentPaths.end(), pendingPromotedPath);
        const bool shouldApplyPendingPromotion = pendingIt != normalizedRecentPaths.end() &&
                                                 normalizedRecentPaths == m_Impl->m_PendingPromotionBaselineRecents;
        if (shouldApplyPendingPromotion)
        {
            if (pendingIt != normalizedRecentPaths.begin())
            {
                normalizedRecentPaths.erase(pendingIt);
                normalizedRecentPaths.push_front(pendingPromotedPath);
            }
            pendingPromotionApplied = true;
        }

        m_Impl->m_PendingPromotedRecentFilePath.clear();
        m_Impl->m_PendingPromotionBaselineRecents.clear();
    }

    // Keep hero behavior consistent across all open paths (toolbar/menu,
    // drag-and-drop, save-as recents updates): when the recents head changes
    // externally and no hero is pinned, follow the newest recent file.
    // Explicit in-dashboard promotions (thumbnail clicks/continue button)
    // are handled separately via m_PendingPromotedRecentFilePath and must
    // retain precedence only when the baseline hasn't changed underneath.
    if (!pendingPromotionApplied && m_Impl->m_PinnedHeroFilePath.isEmpty())
    {
        const QString newMostRecentPath = normalizedRecentPaths.empty() ? QString() : normalizedRecentPaths.front();
        if (!newMostRecentPath.isEmpty() && newMostRecentPath != previousMostRecentPath)
        {
            m_Impl->m_PreferredHeroFilePath = newMostRecentPath;
        }
    }

    bool sameSnapshot = normalizedRecentPaths.size() == m_Impl->m_AllRecentFilePaths.size();
    if (sameSnapshot)
    {
        auto previousIt = m_Impl->m_AllRecentFilePaths.begin();
        for (const QString& path : normalizedRecentPaths)
        {
            if (previousIt == m_Impl->m_AllRecentFilePaths.end() || *previousIt != path)
            {
                sameSnapshot = false;
                break;
            }
            ++previousIt;
        }
    }

    if (!sameSnapshot)
    {
        m_Impl->m_AllRecentFilePaths = normalizedRecentPaths;
    }

    bool pinnedChanged = false;
    for (auto it = m_Impl->m_PinnedRecentFiles.begin(); it != m_Impl->m_PinnedRecentFiles.end();)
    {
        if (!normalizedRecentSet.contains(*it))
        {
            it = m_Impl->m_PinnedRecentFiles.erase(it);
            pinnedChanged = true;
        }
        else
        {
            ++it;
        }
    }

    if (pinnedChanged)
    {
        persistPinnedRecentFilesPreference();
    }

    bool pinnedHeroChanged = false;
    if (!m_Impl->m_PinnedHeroFilePath.isEmpty())
    {
        const QFileInfo pinnedHeroInfo(m_Impl->m_PinnedHeroFilePath);
        if (!normalizedRecentSet.contains(m_Impl->m_PinnedHeroFilePath) || !pinnedHeroInfo.exists() ||
            !pinnedHeroInfo.isFile())
        {
            m_Impl->m_PinnedHeroFilePath.clear();
            persistPinnedHeroFilePreference();
            pinnedHeroChanged = true;
        }
    }

    if (!m_Impl->m_PreferredHeroFilePath.isEmpty() && !normalizedRecentSet.contains(m_Impl->m_PreferredHeroFilePath))
    {
        m_Impl->m_PreferredHeroFilePath.clear();
    }

    refreshFeaturedFileCard();
    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setPinnedFilePaths(m_Impl->m_PinnedRecentFiles);
    }

    if (sameSnapshot && !pinnedChanged && !pinnedHeroChanged)
    {
        if (m_Impl->m_DashboardToolbar)
        {
            m_Impl->m_DashboardToolbar->setActionEnabled(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),
                                                         !m_Impl->m_AllRecentFilePaths.empty());
        }
        return;
    }

    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setRecentFiles(m_Impl->m_AllRecentFilePaths);
    }
}

void MainDashboard::reloadRecentFilesThumbnails()
{
    const auto recentFiles = currentRecentFiles();
    queueRecentThumbnails(recentFiles);
    synchronizeRecentSectionVisualState();
}

void MainDashboard::notifyRecentFilesListDeleted()
{
    const auto recentFiles = currentRecentFiles();
    if (!recentFiles.empty())
    {
        return;
    }

    m_Impl->m_AllRecentFilePaths.clear();
    m_Impl->m_PinnedRecentFiles.clear();
    m_Impl->m_PinnedHeroFilePath.clear();
    m_Impl->m_PreferredHeroFilePath.clear();
    persistPinnedRecentFilesPreference();
    persistPinnedHeroFilePreference();
    refreshFeaturedFileCard();
    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->setPinnedFilePaths(m_Impl->m_PinnedRecentFiles);
        m_Impl->m_RecentFilesGrid->clearAll();
    }
    if (m_Impl->m_DashboardToolbar)
    {
        m_Impl->m_DashboardToolbar->setActionEnabled(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS), false);
    }
    m_Impl->m_RecentSectionStateInitialized = false;
    m_Impl->m_RecentSectionState = RecentSectionState::NoRecents;
    updateRecentSectionState(RecentSectionState::NoRecents, 0, 0, false);
    update();
}

void MainDashboard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_Impl->m_RecentFilesGrid)
    {
        m_Impl->m_RecentFilesGrid->refreshLayout();
    }
}

void MainDashboard::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event || !event->mimeData())
    {
        return;
    }

    const auto urls = event->mimeData()->urls();
    for (const auto& url : urls)
    {
        const QString filePath = url.toLocalFile();
        if (filePath.isEmpty())
        {
            continue;
        }

        event->acceptProposedAction();

        // Highlight the onboarding drop zone (Option D)
        if (m_Impl->m_EmptyStateOnboardingPanel && m_Impl->m_EmptyStateOnboardingPanel->isVisible())
        {
            m_Impl->m_EmptyStateOnboardingPanel->setDragHighlighted(true);
        }
        return;
    }
}

void MainDashboard::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    if (m_Impl->m_EmptyStateOnboardingPanel)
    {
        m_Impl->m_EmptyStateOnboardingPanel->setDragHighlighted(false);
    }
}

void MainDashboard::dropEvent(QDropEvent* event)
{
    // Clear drop zone highlight regardless of outcome
    if (m_Impl->m_EmptyStateOnboardingPanel)
    {
        m_Impl->m_EmptyStateOnboardingPanel->setDragHighlighted(false);
    }

    if (!event || !event->mimeData())
    {
        return;
    }

    const auto urls = event->mimeData()->urls();
    QStringList files;

    for (const auto& url : urls)
    {
        const QString filePath = url.toLocalFile();
        if (filePath.isEmpty())
        {
            continue;
        }

        files.append(filePath);
    }

    if (files.isEmpty())
    {
        return;
    }

    recordDashboardLastAction(tr("Open dropped files"));
    openDroppedFilesViaContext(files);
    event->acceptProposedAction();
}

void MainDashboard::applyToolbarIcons()
{
    const auto provider = iconProvider();
    if (!provider)
    {
        return;
    }

    if (m_Impl->m_DashboardToolbar)
    {
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_OPEN_FILE),
                                                  provider->getIcon("folder_open"));
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_NEW_SEQUENCE),
                                                  provider->getIcon("movie"));
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_IMAGE_INTEGRATION),
                                                  provider->getIcon("stack"));
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_EXTENSION_MANAGER),
                                                  provider->getIcon("extension"));
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_CLEAR_RECENTS),
                                                  provider->getIcon("close"));
        m_Impl->m_DashboardToolbar->setActionIcon(QString::fromUtf8(ACTION_ID_SYSTEM_INFO),
                                                  provider->getIcon("computer"));
    }

    if (m_Impl->m_DashboardIdentityBar)
    {
        m_Impl->m_DashboardIdentityBar->setSettingsIcon(provider->getIcon("settings"));
        // Load help and info icons with the normal (mono) color instead of
        // their semantic tint so all three utility buttons look uniform.
        m_Impl->m_DashboardIdentityBar->setHelpIcon(
            monoSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/help.svg"), QSize(18, 18)));
        m_Impl->m_DashboardIdentityBar->setAboutIcon(
            monoSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/info.svg"), QSize(18, 18)));
    }

    if (m_Impl->m_FeaturedFileCard)
    {
        // Pin icon (unpinned state): semi-transparent white outline.
        // Unpin icon (pinned state): warm amber/gold filled star so the
        // two states are clearly distinguishable at a glance.
        const QColor pinColor(255, 255, 255, 180);
        const QColor unpinColor(255, 200, 60); // warm gold / amber
        const QIcon pinIcon(
            provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/star.svg"), QSize(18, 18), pinColor));
        const QIcon unpinIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/star_filled.svg"),
                                                            QSize(18, 18), unpinColor));
        m_Impl->m_FeaturedFileCard->setPinIcons(pinIcon, unpinIcon);
    }

    if (m_Impl->m_SearchAction)
    {
        m_Impl->m_SearchAction->setIcon(provider->getIcon("search"));
    }

    if (m_Impl->m_EmptyStateOnboardingPanel)
    {
        const QColor accentColor = an::ui::tokens().colors.accentPrimary;
        const QColor tertiaryColor = an::ui::tokens().colors.textTertiary;
        const QSize featureIconSize(28, 28);
        const QSize actionIconSize(36, 36);

        // Feature card icons (accent-tinted)
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            0, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/image_filled.svg"),
                                                   featureIconSize, accentColor)));
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            1, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/exposure.svg"),
                                                   featureIconSize, accentColor)));
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            2, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/star.svg"), featureIconSize,
                                                   accentColor)));
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            3, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/stack.svg"), featureIconSize,
                                                   accentColor)));
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            4, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/area_chart.svg"),
                                                   featureIconSize, accentColor)));
        m_Impl->m_EmptyStateOnboardingPanel->setFeatureIcon(
            5, QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/hq.svg"), featureIconSize,
                                                   accentColor)));

        // Action card icons (accent-tinted)
        m_Impl->m_EmptyStateOnboardingPanel->setActionIcons(
            QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/folder_open.svg"),
                                                actionIconSize, accentColor)),
            QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/movie.svg"), actionIconSize,
                                                accentColor)),
            QIcon(provider->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/stack.svg"), actionIconSize,
                                                accentColor)));

        // Drop zone icon (tertiary color)
        m_Impl->m_EmptyStateOnboardingPanel->setDropZoneIcon(QIcon(provider->loadSvgFileAsPixmap(
            QStringLiteral(":/AfterNight/icons/SVG/file_open.svg"), QSize(40, 40), tertiaryColor)));
    }
}

void MainDashboard::refreshIcons()
{
    applyToolbarIcons();
    updateThemeStyles();
}
