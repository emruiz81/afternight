// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "SettingsDialog.h"
#include "settings/SettingsPage.h"
#include "settings/PathPickerWidget.h"
#include "settings/SettingsSearchTable.h"
#include "app/WindowPlacement.h"
#include "components/AnSectionHeader.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <verdigris/wobjectimpl.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMargins>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSizePolicy>

#include <algorithm>
#include <vector>

W_OBJECT_IMPL(SettingsDialog)

namespace
{
constexpr int NAV_ICON_SIZE = 20;
constexpr int NAV_ITEM_HEIGHT = 36;
constexpr int MIN_NAV_WIDTH = 200;
constexpr int MIN_CONTENT_WIDTH = 550;
constexpr int MIN_DIALOG_HEIGHT = 600;
constexpr int DEFAULT_DIALOG_WIDTH = 900;
constexpr int DEFAULT_DIALOG_HEIGHT = 700;

// Role for storing page pointer in list items
constexpr int PagePointerRole = Qt::UserRole + 1;

/// The navigation column's width: the Observatory profile's column, or 1.1's.
int navigationWidth()
{
    return an::ui::styleProfile() == an::ui::StyleProfile::Observatory ? an::ui::parts::kSettingsNavigationWidth
                                                                       : MIN_NAV_WIDTH;
}

/// The page's least width: the Observatory profile's, whose Comfortable density draws larger controls, or 1.1's.
int pageLeastWidth()
{
    return an::ui::styleProfile() == an::ui::StyleProfile::Observatory ? an::ui::parts::kSettingsPageLeastWidth
                                                                       : MIN_CONTENT_WIDTH;
}

/// The pages' stack, as tall as the page it shows. A stack is as tall as its tallest page, and in the dialog's scroll
/// area every page would then scroll as far as that one does, into empty room under a shorter page's rows.
class ShownPageStack final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;

    [[nodiscard]] QSize sizeHint() const override
    {
        const QWidget* const page = currentWidget();
        return page != nullptr ? page->sizeHint() + marginsSize() : QStackedWidget::sizeHint();
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        const QWidget* const page = currentWidget();
        return page != nullptr ? page->minimumSizeHint() + marginsSize() : QStackedWidget::minimumSizeHint();
    }

    [[nodiscard]] bool hasHeightForWidth() const override
    {
        const QWidget* const page = currentWidget();
        return page != nullptr ? page->hasHeightForWidth() : QStackedWidget::hasHeightForWidth();
    }

    /// The height the page shown needs at a width: the scroll area asks for it whenever one of the pages' heights
    /// follows its width, as its stack's layout answers for all of them.
    [[nodiscard]] int heightForWidth(const int width) const override
    {
        const QWidget* const page = currentWidget();
        if (page == nullptr)
        {
            return QStackedWidget::heightForWidth(width);
        }
        const QMargins margins = contentsMargins();
        const int least = page->minimumSizeHint().height();
        const int rows =
            page->hasHeightForWidth() ? page->heightForWidth(width - margins.left() - margins.right()) : least;
        return (std::max)(rows, least) + margins.top() + margins.bottom();
    }

private:
    [[nodiscard]] QSize marginsSize() const
    {
        const QMargins margins = contentsMargins();
        return {margins.left() + margins.right(), margins.top() + margins.bottom()};
    }
};

/// Whether a page's rows, as the Settings' search table states them, hold a text: a row's label or its section.
bool rowsHold(const std::vector<an::ui::SettingsSearchPage>& table, const QString& title, const QString& search)
{
    for (const an::ui::SettingsSearchPage& entry : table)
    {
        if (entry.title != title)
        {
            continue;
        }
        for (const an::ui::SettingsSearchRow& row : entry.rows)
        {
            if (row.label.toLower().contains(search) || row.section.toLower().contains(search))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setupUi();
    setupConnections();

    // Set dialog properties
    setWindowTitle(tr("Settings"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumSize(navigationWidth() + pageLeastWidth() + 50, MIN_DIALOG_HEIGHT);
    resize(DEFAULT_DIALOG_WIDTH, DEFAULT_DIALOG_HEIGHT);

    // Center on parent or primary screen
    const auto centerTo = [this](const QRect& area) {
        const QSize s = size();
        const int x = area.x() + (area.width() - s.width()) / 2;
        const int y = area.y() + (area.height() - s.height()) / 2;
        move(x, y);
    };

    if (parent)
    {
        centerTo(parent->window()->screen()->availableGeometry());
    }
    else if (const QScreen* scr = QApplication::primaryScreen())
    {
        centerTo(scr->availableGeometry());
    }
}

SettingsDialog::~SettingsDialog()
{
    // Pages are owned by the stacked widget (Qt parent-child relationship)
}

void SettingsDialog::setupUi()
{
    const auto& tokenSet = an::ui::tokens();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Create a splitter for navigation and content
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // === Left Panel (Navigation) ===
    auto* navPanel = new QWidget(splitter);
    auto* navLayout = new QVBoxLayout(navPanel);
    navLayout->setSpacing(8);
    navLayout->setContentsMargins(12, 12, 8, 12);

    // Search box
    m_SearchEdit = new QLineEdit(navPanel);
    m_SearchEdit->setObjectName("settingsSearchEdit");
    m_SearchEdit->setPlaceholderText(tr("Search settings..."));
    m_SearchEdit->setClearButtonEnabled(true);
    navLayout->addWidget(m_SearchEdit);

    // Navigation list
    m_NavigationList = new QListWidget(navPanel);
    m_NavigationList->setObjectName("settingsNavigationList");
    m_NavigationList->setIconSize(QSize(NAV_ICON_SIZE, NAV_ICON_SIZE));
    m_NavigationList->setSpacing(2);
    m_NavigationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_NavigationList->setFocusPolicy(Qt::StrongFocus);
    navLayout->addWidget(m_NavigationList, 1);

    navPanel->setMinimumWidth(navigationWidth());
    splitter->addWidget(navPanel);

    // === Right Panel (Content) ===
    auto* contentPanel = new QWidget(splitter);
    auto* contentLayout = new QVBoxLayout(contentPanel);
    contentLayout->setSpacing(12);
    contentLayout->setContentsMargins(12, 12, 12, 12);

    // Page header (AnSectionHeader with inline Reset Page action)
    m_PageSectionHeader = new an::ui::AnSectionHeader(QString(), QString(), tr("Reset Page"), contentPanel);
    m_PageSectionHeader->setObjectName(QStringLiteral("settingsPageSectionHeader"));
    m_PageSectionHeader->actionButton()->setObjectName(QStringLiteral("settingsResetPageButton"));
    m_PageSectionHeader->actionButton()->setToolTip(tr("Reset all settings on this page to defaults"));
    contentLayout->addWidget(m_PageSectionHeader);

    // Scrollable page area
    m_PageScrollArea = new QScrollArea(contentPanel);
    m_PageScrollArea->setFrameShape(QFrame::NoFrame);
    m_PageScrollArea->setWidgetResizable(true);
    m_PageScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Stacked widget for pages
    m_PageStack = new ShownPageStack(m_PageScrollArea);
    m_PageStack->setContentsMargins(0, 0, tokenSet.spacing.sm, 0);
    m_PageStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_PageScrollArea->setWidget(m_PageStack);

    contentLayout->addWidget(m_PageScrollArea, 1);

    contentPanel->setMinimumWidth(pageLeastWidth());
    splitter->addWidget(contentPanel);

    // Set initial splitter sizes (nav:content = 1:3)
    splitter->setSizes({navigationWidth(), pageLeastWidth() + 100});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(splitter, 1);

    // === Bottom Button Bar ===
    auto* buttonBar = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(12, 8, 12, 12);
    buttonLayout->setSpacing(8);

    m_ResetAllButton = new QPushButton(tr("Reset All to Defaults"), buttonBar);
    m_ResetAllButton->setObjectName(QStringLiteral("settingsResetAllButton"));
    m_ResetAllButton->setToolTip(tr("Reset all settings in all categories to their default values"));
    buttonLayout->addWidget(m_ResetAllButton);

    buttonLayout->addStretch();

    m_ApplyButton = new QPushButton(tr("Apply"), buttonBar);
    m_ApplyButton->setObjectName(QStringLiteral("settingsApplyButton"));
    m_ApplyButton->setToolTip(tr("Apply changes without closing the dialog"));
    m_ApplyButton->setProperty("buttonVariant", "primary");
    buttonLayout->addWidget(m_ApplyButton);

    m_CancelButton = new QPushButton(tr("Cancel"), buttonBar);
    m_CancelButton->setObjectName(QStringLiteral("settingsCancelButton"));
    buttonLayout->addWidget(m_CancelButton);

    m_OkButton = new QPushButton(tr("OK"), buttonBar);
    m_OkButton->setObjectName(QStringLiteral("settingsOkButton"));
    m_OkButton->setProperty("buttonVariant", "primary");
    m_OkButton->setDefault(true);
    buttonLayout->addWidget(m_OkButton);

    m_CloseButton = new QPushButton(tr("Close"), buttonBar);
    m_CloseButton->setObjectName(QStringLiteral("settingsCloseButton"));
    m_CloseButton->setToolTip(tr("Close this dialog"));
    m_CloseButton->setProperty("buttonVariant", "primary");
    buttonLayout->addWidget(m_CloseButton);

    mainLayout->addWidget(buttonBar);

    // Add an empty placeholder label for when no pages are added
    auto* emptyLabel = new QLabel(tr("No settings pages available"), m_PageStack);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setEnabled(false);
    m_PageStack->addWidget(emptyLabel);

    updateUiState();
}

void SettingsDialog::setupConnections()
{
    // Search filtering
    connect(m_SearchEdit, &QLineEdit::textChanged, this, &SettingsDialog::filterPages);

    // Navigation selection
    connect(m_NavigationList, &QListWidget::currentItemChanged, this, &SettingsDialog::onNavigationItemChanged);

    // Button actions
    connect(m_OkButton, &QPushButton::clicked, this, &SettingsDialog::accept);
    connect(m_CancelButton, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(m_CloseButton, &QPushButton::clicked, this, &SettingsDialog::reject);
    connect(m_ApplyButton, &QPushButton::clicked, this, [this]() {
        saveAllSettings();
        refreshIcons(); // Refresh icons in case theme changed
        emit settingsApplied();
        updateUiState();
    });
    connect(m_PageSectionHeader->actionButton(), &QPushButton::clicked, this,
            &SettingsDialog::resetCurrentPageToDefaults);
    connect(m_ResetAllButton, &QPushButton::clicked, this, &SettingsDialog::resetAllToDefaults);
}

void SettingsDialog::addPage(SettingsPage* page)
{
    if (!page || m_Pages.contains(page))
    {
        return;
    }

    // Add to our list
    m_Pages.append(page);

    // Add to the stacked widget
    m_PageStack->addWidget(page);

    // Create navigation item
    auto* navItem = createNavItem(page);
    m_NavigationList->addItem(navItem);

    // Connect to page's modified signal
    connect(page, &SettingsPage::settingsModified, this, &SettingsDialog::updateUiState);

    // Note: Settings will be loaded lazily when page is first activated

    // If this is the first page, select it
    if (m_Pages.size() == 1)
    {
        m_NavigationList->setCurrentRow(0);
    }

    updateUiState();
}

bool SettingsDialog::removePage(SettingsPage* page)
{
    const int index = m_Pages.indexOf(page);
    if (index < 0)
    {
        return false;
    }

    // Remove from navigation list
    delete m_NavigationList->takeItem(index);

    // Remove from stacked widget
    m_PageStack->removeWidget(page);

    // Remove from our list
    m_Pages.removeAt(index);

    // Disconnect signals
    disconnect(page, nullptr, this, nullptr);

    updateUiState();
    return true;
}

SettingsPage* SettingsDialog::currentPage() const
{
    const int index = m_NavigationList->currentRow();
    if (index >= 0 && index < m_Pages.size())
    {
        return m_Pages.at(index);
    }
    return nullptr;
}

void SettingsDialog::setCurrentPage(int index)
{
    if (index >= 0 && index < m_NavigationList->count())
    {
        m_NavigationList->setCurrentRow(index);
    }
}

void SettingsDialog::setCurrentPage(const QString& title)
{
    for (int i = 0; i < m_Pages.size(); ++i)
    {
        if (m_Pages.at(i)->pageTitle() == title)
        {
            setCurrentPage(i);
            return;
        }
    }
}

int SettingsDialog::pageCount() const
{
    return m_Pages.size();
}

SettingsPage* SettingsDialog::page(const int index) const
{
    return index >= 0 && index < m_Pages.size() ? m_Pages.at(index) : nullptr;
}

bool SettingsDialog::hasUnsavedChanges() const
{
    for (const auto* page : m_Pages)
    {
        if (page->hasUnsavedChanges())
        {
            return true;
        }
    }
    return false;
}

void SettingsDialog::accept()
{
    // Validate all loaded pages
    for (auto* page : m_Pages)
    {
        // Skip unloaded pages (can't have invalid data if never loaded)
        if (!page->isSettingsLoaded())
        {
            continue;
        }

        const ValidationResult result = page->validateSettings();
        if (!result.isValid)
        {
            // Switch to the page with the error
            setCurrentPage(page->pageTitle());
            QMessageBox::warning(this, tr("Validation Error"), result.errorMessage);
            return;
        }
    }

    saveAllSettings();
    emit settingsApplied();
    QDialog::accept();
}

void SettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // A window that stays a window opens within its screen: on a small display its default size would put its title bar
    // above the screen's top.
    if (!m_Shown && !event->spontaneous())
    {
        m_Shown = true;
        widenForThePages();
        an::ui::holdWithinAvailableArea(*this);
    }
}

void SettingsDialog::widenForThePages()
{
    if (an::ui::styleProfile() != an::ui::StyleProfile::Observatory)
    {
        return;
    }
    QWidget* const column = m_PageScrollArea->parentWidget();
    int widest = 0;
    for (SettingsPage* const page : m_Pages)
    {
        page->ensurePolished();
        widest = (std::max)(widest, page->minimumSizeHint().width());
    }
    // The page within the stack's margins, in the scroll area beside the bar a page taller than the area shows, within
    // the column's margins.
    const QMargins stack = m_PageStack->contentsMargins();
    const QMargins around = column->layout() != nullptr ? column->layout()->contentsMargins() : QMargins();
    const int needs = widest + stack.left() + stack.right() + 2 * m_PageScrollArea->frameWidth() +
                      m_PageScrollArea->verticalScrollBar()->sizeHint().width() + around.left() + around.right();
    const int least = (std::max)(pageLeastWidth(), needs);
    if (least != column->minimumWidth())
    {
        column->setMinimumWidth(least);
        setMinimumWidth(navigationWidth() + least + 50);
    }
}

void SettingsDialog::reject()
{
    if (hasUnsavedChanges())
    {
        if (!confirmDiscardChanges())
        {
            return;
        }
        // What a page showed at once goes with its unsaved changes.
        for (auto* page : m_Pages)
        {
            if (page->hasUnsavedChanges())
            {
                page->discardChanges();
            }
        }
    }
    QDialog::reject();
}

void SettingsDialog::filterPages(const QString& searchText)
{
    const QString search = searchText.trimmed().toLower();
    const std::vector<an::ui::SettingsSearchPage> table =
        search.isEmpty() ? std::vector<an::ui::SettingsSearchPage>{} : an::ui::settingsSearchPages();

    for (int i = 0; i < m_NavigationList->count(); ++i)
    {
        auto* item = m_NavigationList->item(i);
        if (!item)
            continue;

        bool visible = true;
        if (!search.isEmpty())
        {
            // Check page title and keywords
            if (i < m_Pages.size())
            {
                auto* page = m_Pages.at(i);
                visible = page->pageTitle().toLower().contains(search);
                if (!visible)
                {
                    // Also check keywords
                    const auto keywords = page->searchKeywords();
                    for (const auto& keyword : keywords)
                    {
                        if (keyword.toLower().contains(search))
                        {
                            visible = true;
                            break;
                        }
                    }
                }
                // And the rows the page shows.
                visible = visible || rowsHold(table, page->pageTitle(), search);
            }
        }

        item->setHidden(!visible);
    }

    // If current item is hidden, select the first visible one
    if (m_NavigationList->currentItem() && m_NavigationList->currentItem()->isHidden())
    {
        for (int i = 0; i < m_NavigationList->count(); ++i)
        {
            if (!m_NavigationList->item(i)->isHidden())
            {
                m_NavigationList->setCurrentRow(i);
                break;
            }
        }
    }
}

void SettingsDialog::onNavigationItemChanged(QListWidgetItem* current, QListWidgetItem* previous)
{
    // Notify previous page it's being deactivated
    if (previous)
    {
        const int prevIndex = m_NavigationList->row(previous);
        if (prevIndex >= 0 && prevIndex < m_Pages.size())
        {
            m_Pages.at(prevIndex)->onPageDeactivated();
        }
    }

    if (!current)
    {
        m_PageSectionHeader->setTitle(QString());
        m_PageSectionHeader->actionButton()->setEnabled(false);
        m_PageStack->setCurrentIndex(0); // Show empty placeholder
        m_PageStack->updateGeometry();
        updateUiState();
        return;
    }

    const int index = m_NavigationList->row(current);
    if (index < 0 || index >= m_Pages.size())
    {
        return;
    }

    auto* page = m_Pages.at(index);

    // Keep pages in sync with persisted settings whenever they are activated,
    // unless the page currently has unsaved edits the user is working on.
    const bool shouldReloadPageSettings = !page->isSettingsLoaded() || !page->hasUnsavedChanges();
    if (shouldReloadPageSettings)
    {
        page->loadSettings();
        page->setSettingsLoaded(true);
    }
    // A page loaded first now may need more width than it did before it was filled.
    if (m_Shown)
    {
        widenForThePages();
    }

    // Update title
    m_PageSectionHeader->setTitle(page->pageTitle());

    // Switch stacked widget to the page (add 1 to skip empty placeholder at index 0)
    m_PageStack->setCurrentWidget(page);
    // The page's height is the stack's now: the scroll area scrolls as far as this page needs.
    m_PageStack->updateGeometry();

    // Notify page it's being activated
    page->onPageActivated();

    emit pageChanged(page);
    updateUiState();
}

void SettingsDialog::saveAllSettings()
{
    for (auto* page : m_Pages)
    {
        // Save all enabled pages that have been loaded. Relying only on
        // "modified" tracking can miss edge cases and lead to settings not
        // being persisted even though the UI changed.
        if (page->isEnabled() && page->isSettingsLoaded())
        {
            page->saveSettings();
            page->setSettingsLoaded(true);
        }
    }
}

void SettingsDialog::loadAllSettings()
{
    for (auto* page : m_Pages)
    {
        page->loadSettings();
    }
}

void SettingsDialog::resetCurrentPageToDefaults()
{
    auto* page = currentPage();
    if (!page)
        return;

    const int result = QMessageBox::question(
        this, tr("Reset Settings"),
        tr("Are you sure you want to reset all settings on the \"%1\" page to their default values?")
            .arg(page->pageTitle()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result == QMessageBox::Yes)
    {
        page->resetToDefaults();
        page->setSettingsLoaded(true);
        updateUiState();
    }
}

void SettingsDialog::resetAllToDefaults()
{
    const int result =
        QMessageBox::question(this, tr("Reset All Settings"),
                              tr("Are you sure you want to reset ALL settings to their default values?\n\n"
                                 "This action cannot be undone."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result == QMessageBox::Yes)
    {
        for (auto* page : m_Pages)
        {
            if (page->isEnabled())
            {
                page->resetToDefaults();
                page->setSettingsLoaded(true);
            }
        }
        emit settingsReset();
        updateUiState();
    }
}

void SettingsDialog::updateFooterMode()
{
    const auto* page = currentPage();
    const bool use_commit_buttons = !page || page->usesSettingsDialogCommitButtons();

    m_ApplyButton->setVisible(use_commit_buttons);
    m_CancelButton->setVisible(use_commit_buttons);
    m_OkButton->setVisible(use_commit_buttons);
    m_CloseButton->setVisible(!use_commit_buttons);

    m_OkButton->setDefault(use_commit_buttons);
    m_CloseButton->setDefault(!use_commit_buttons);
}

void SettingsDialog::updateUiState()
{
    const bool hasPages = !m_Pages.isEmpty();
    const bool hasChanges = hasUnsavedChanges();
    auto* page = currentPage();
    const bool pageEnabled = page && page->isEnabled();
    const bool use_commit_buttons = !page || page->usesSettingsDialogCommitButtons();

    updateFooterMode();

    m_ApplyButton->setEnabled(use_commit_buttons && hasChanges);
    m_PageSectionHeader->actionButton()->setEnabled(pageEnabled);
    m_ResetAllButton->setEnabled(hasPages);
    m_OkButton->setEnabled(use_commit_buttons && hasPages);
    m_CloseButton->setEnabled(!use_commit_buttons && hasPages);

    // Update window title to show modified state
    const QString baseTitle = tr("Settings");
    setWindowTitle(hasChanges ? baseTitle + " *" : baseTitle);
}

bool SettingsDialog::confirmDiscardChanges()
{
    const int result =
        QMessageBox::question(this, tr("Unsaved Changes"), tr("You have unsaved changes. Do you want to discard them?"),
                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return result == QMessageBox::Discard;
}

QListWidgetItem* SettingsDialog::createNavItem(SettingsPage* page)
{
    auto* item = new QListWidgetItem();
    item->setText(page->pageTitle());
    item->setIcon(page->pageIcon());
    item->setSizeHint(QSize(0, NAV_ITEM_HEIGHT));
    item->setData(PagePointerRole, QVariant::fromValue(static_cast<void*>(page)));

    // Disable item if page is not enabled
    if (!page->isEnabled())
    {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }

    return item;
}

void SettingsDialog::refreshIcons()
{
    // Refresh navigation list icons
    for (int i = 0; i < m_NavigationList->count(); ++i)
    {
        QListWidgetItem* item = m_NavigationList->item(i);
        if (item)
        {
            auto* page = static_cast<SettingsPage*>(item->data(PagePointerRole).value<void*>());
            if (page)
            {
                item->setIcon(page->pageIcon());
            }
        }
    }

    // Refresh styles on all pages
    for (auto* page : m_Pages)
    {
        if (page)
        {
            page->refreshStyles();

            // Some settings controls cache icons at construction time.
            // Refresh them explicitly so theme Apply updates immediately.
            const auto pickers = page->findChildren<PathPickerWidget*>();
            for (auto* picker : pickers)
            {
                if (picker)
                {
                    picker->refreshBrowseIcon();
                }
            }
        }
    }
}
