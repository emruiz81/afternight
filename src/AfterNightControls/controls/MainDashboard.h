// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <verdigris/wobjectdefs.h>

#include <deque>
#include <memory>
#include <string>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QGraphicsOpacityEffect;
class QLayout;
class QMenu;
class QParallelAnimationGroup;
class QPoint;
class QResizeEvent;
class QShowEvent;

namespace an::ui
{
class AnCardWidget;
class AnSectionHeader;
class DashboardOnboardingPanel;
class DashboardToolbar;
class FeaturedFileCard;
class FeaturedFilePresenter;
class RecentFilesGrid;
} // namespace an::ui

class AFTERNIGHTCONTROLS_API MainDashboard : public QWidget
{
    W_OBJECT(MainDashboard)

public:
    explicit MainDashboard(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    void initialize();
    void initialize(QWidget* parent);

    void reloadRecentFilesThumbnails();

    void notifyRecentFilesListDeleted();

    /// Refresh all icons after theme change
    void refreshIcons();

    ~MainDashboard() override;

signals:
    void openFileRequested() W_SIGNAL(openFileRequested);
    void newSequenceRequested() W_SIGNAL(newSequenceRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);
    void openExtensionManagerRequested() W_SIGNAL(openExtensionManagerRequested);
    void clearRecentFilesRequested() W_SIGNAL(clearRecentFilesRequested);
    void openSettingsRequested() W_SIGNAL(openSettingsRequested);
    void systemInformationRequested() W_SIGNAL(systemInformationRequested);
    void aboutRequested() W_SIGNAL(aboutRequested);
    void openHelpRequested() W_SIGNAL(openHelpRequested);
    void openRecentFileRequested(const QString& filePath) W_SIGNAL(openRecentFileRequested, filePath);
    void removeRecentFileRequested(const QString& filePath) W_SIGNAL(removeRecentFileRequested, filePath);

private:
    enum class RecentSectionState
    {
        NoRecents,
        FilteredEmpty,
        HasResults
    };

    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void initializeDpiAwareSizing();
    void initializeIdentityBarSection();
    void initializeHeaderSection();
    void initializeActionButtonsSection();
    void initializeRecentFilesSection();
    void applyToolbarIcons();
    void populateQuickActionCustomizationMenu(QMenu& menu);
    void applyActionLayoutPreferences();
    QString actionTitleForId(const QString& actionId) const;
    void synchronizeRecentSectionVisualState();
    void updateRecentSectionState(RecentSectionState state, int visibleCount, int totalCount, bool hasActiveFilter);
    void animateRecentSectionWidget(QWidget* widget, QGraphicsOpacityEffect* opacityEffect, bool showWidget,
                                    QPointer<QParallelAnimationGroup>& transitionGroup);
    void loadDashboardStyleSheet();
    void updateThemeStyles();
    [[nodiscard]] bool isDarkMode() const;

    void queueRecentThumbnails(const std::deque<std::string>& recentFiles);

    void ensureDashboardSettingsSchema();
    void loadActionLayoutPreferences(const QStringList& allActionIds);
    void persistActionLayoutPreferences(const QStringList& allActionIds) const;
    void loadRecentSortPreference();
    void persistRecentSortPreference() const;
    void loadRecentSearchPreference();
    void persistRecentSearchPreference() const;
    void recordDashboardLastAction(const QString& actionText);
    void refreshFeaturedFileCard();
    void promoteRecentFileToFront(const QString& filePath);
    void loadPinnedRecentFilesPreference();
    void persistPinnedRecentFilesPreference() const;
    void loadPinnedHeroFilePreference();
    void persistPinnedHeroFilePreference() const;
    void setPinnedHeroFile(const QString& filePath, bool pinned);
    void setRecentFilePinned(const QString& filePath, bool pinned);
    [[nodiscard]] bool isRecentFilePinned(const QString& filePath) const;
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
