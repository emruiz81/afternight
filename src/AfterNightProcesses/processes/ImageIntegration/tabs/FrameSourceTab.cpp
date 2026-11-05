// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameSourceTab.h"
#include "app/ApplicationStorage.h"

#include "controls/IAppContext.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/tabs/TabStyling.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/model/FrameTreeFilter.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/FrameGroupingIssuesWidget.h"
#include "processes/ImageIntegration/widgets/FrameQuickActionsWidget.h"
#include "processes/ImageIntegration/widgets/FrameStatisticsDashboard.h"
#include "processes/ImageIntegration/widgets/GroupingOptionsWidget.h"
#include "controls/ImageViewControl.h"
#include "processes/ImageIntegration/widgets/TreeDisplayModeWidget.h"

#include <QCheckBox>
#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QResizeEvent>
#include <QShortcut>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QTimer>
#include <QSettings>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QPalette>
#include <QStyle>
#include <cmath>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(FrameSourceTab)

namespace
{
QString settingsGroupFor(FrameType type)
{
    switch (type)
    {
    case FrameType::Bias:
        return QStringLiteral("ImageIntegration/FrameSourceTab/Bias");
    case FrameType::Dark:
        return QStringLiteral("ImageIntegration/FrameSourceTab/Dark");
    case FrameType::Flat:
        return QStringLiteral("ImageIntegration/FrameSourceTab/Flat");
    case FrameType::Light:
    default:
        return QStringLiteral("ImageIntegration/FrameSourceTab/Light");
    }
}

bool shouldDefaultPreviewToAutostretch(FrameType type)
{
    return type == FrameType::Flat || type == FrameType::Light;
}
} // namespace

FrameSourceTab::FrameSourceTab(FrameType type, IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_type(type), m_model(model), m_undoStack(undoStack)
{
    // Let the tab widget pane draw the page background so the surrounding "gutter"
    // matches the tab header/pane styling. The inner widgets provide their own fill.
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(4);

    auto* action_strip = new QWidget(this);
    action_strip->setObjectName(QStringLiteral("frameSourceActionStrip"));
    action_strip->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    action_strip->setFixedHeight(26);
    auto* action_layout = new QHBoxLayout(action_strip);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setSpacing(4);

    auto* appContext = an::ui::appContext();
    const auto iconProvider = appContext ? appContext->getIconProvider() : an::ui::IIconProviderPtr{};
    const auto iconOrFallback = [this, iconProvider](const QString& name, const QStyle::StandardPixmap fallback) {
        QIcon icon;
        if (iconProvider)
        {
            icon = iconProvider->getIcon(name);
        }
        if (!icon.isNull())
        {
            return icon;
        }
        return style() ? style()->standardIcon(fallback) : QIcon();
    };

    const auto setActionTooltip = [](QAction* action, const QString& tooltip) {
        if (!action)
        {
            return;
        }
        action->setToolTip(tooltip);
        action->setStatusTip(tooltip);
    };

    const auto configureButton = [](QToolButton* button) {
        if (!button)
        {
            return;
        }
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setMinimumHeight(22);
    };

    auto* add_files_button = new QToolButton(action_strip);
    add_files_button->setObjectName(QStringLiteral("actionAddFiles"));
    add_files_button->setText(tr("Add Files..."));
    add_files_button->setIcon(iconOrFallback(QStringLiteral("file_open"), QStyle::SP_DialogOpenButton));
    add_files_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add_files_button->setToolTip(tr("Add files to this tab's frame list."));
    configureButton(add_files_button);
    connect(add_files_button, &QToolButton::clicked, this, [this]() { emit addFilesRequested(m_type); });
    action_layout->addWidget(add_files_button);

    auto* add_folder_button = new QToolButton(action_strip);
    add_folder_button->setObjectName(QStringLiteral("actionAddFolder"));
    add_folder_button->setIcon(iconOrFallback(QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon));
    add_folder_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add_folder_button->setToolTip(tr("Add files from a folder to this tab's frame list."));
    configureButton(add_folder_button);
    auto* add_folder_default = new QAction(tr("Add Folder..."), add_folder_button);
    add_folder_default->setObjectName(QStringLiteral("actionAddFolderDefault"));
    add_folder_default->setIcon(iconOrFallback(QStringLiteral("folder_open"), QStyle::SP_DirOpenIcon));
    setActionTooltip(add_folder_default, tr("Pick a folder and add supported files to this tab."));
    connect(add_folder_default, &QAction::triggered, this,
            [this]() { emit addFolderRequested(m_type, /*recursive=*/false); });
    add_folder_button->setDefaultAction(add_folder_default);
    add_folder_button->setPopupMode(QToolButton::MenuButtonPopup);

    auto* add_folder_menu = new QMenu(add_folder_button);
    auto* add_folder_this = add_folder_menu->addAction(tr("This Folder Only..."));
    add_folder_this->setObjectName(QStringLiteral("actionAddFolderThis"));
    setActionTooltip(add_folder_this, tr("Add supported files from a folder (non-recursive)."));
    connect(add_folder_this, &QAction::triggered, this,
            [this]() { emit addFolderRequested(m_type, /*recursive=*/false); });
    auto* add_folder_recursive = add_folder_menu->addAction(tr("Recursive..."));
    add_folder_recursive->setObjectName(QStringLiteral("actionAddFolderRecursive"));
    setActionTooltip(add_folder_recursive, tr("Add supported files from a folder and all subfolders."));
    connect(add_folder_recursive, &QAction::triggered, this,
            [this]() { emit addFolderRequested(m_type, /*recursive=*/true); });
    add_folder_button->setMenu(add_folder_menu);
    action_layout->addWidget(add_folder_button);

    auto* remove_selected_button = new QToolButton(action_strip);
    remove_selected_button->setObjectName(QStringLiteral("actionRemoveSelected"));
    remove_selected_button->setText(tr("Remove Selected"));
    remove_selected_button->setIcon(iconOrFallback(QStringLiteral("delete"), QStyle::SP_TrashIcon));
    remove_selected_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    remove_selected_button->setToolTip(tr("Remove the selected frames from this tab."));
    configureButton(remove_selected_button);
    connect(remove_selected_button, &QToolButton::clicked, this, [this]() { emit removeSelectedRequested(); });
    action_layout->addWidget(remove_selected_button);
    m_removeSelectedButton = remove_selected_button;

    auto* remove_all_button = new QToolButton(action_strip);
    remove_all_button->setObjectName(QStringLiteral("actionRemoveAll"));
    remove_all_button->setText(tr("Remove All"));
    remove_all_button->setIcon(iconOrFallback(QStringLiteral("close"), QStyle::SP_DialogCloseButton));
    remove_all_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    remove_all_button->setToolTip(tr("Remove all frames from this tab."));
    configureButton(remove_all_button);
    connect(remove_all_button, &QToolButton::clicked, this, [this]() { emit removeAllRequested(m_type); });
    action_layout->addWidget(remove_all_button);
    m_removeAllButton = remove_all_button;

    action_layout->addStretch(1);

    root->addWidget(action_strip);

    m_previewClampDebounce = new QTimer(this);
    m_previewClampDebounce->setSingleShot(true);
    m_previewClampDebounce->setInterval(0);
    connect(m_previewClampDebounce, &QTimer::timeout, this, [this]() { updatePreviewPanelAspectClamp(); });

    m_splitter = new QSplitter(Qt::Horizontal, this);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this]() {
        m_splitterUserSized = true;
        schedulePreviewPanelAspectClamp();
        schedulePersistUiState();
    });
    root->addWidget(m_splitter);

    auto* left = new QWidget(m_splitter);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(0);

    auto* rightColumn = new QWidget(m_splitter);
    auto* rightColumnLayout = new QVBoxLayout(rightColumn);
    rightColumnLayout->setContentsMargins(0, 0, 0, 0);
    rightColumnLayout->setSpacing(0);

    m_treeModel = new GroupTreeModel(type, model, this, m_undoStack);

    // Search/filter row.
    auto* search_row = new QWidget(left);
    auto* search_layout = new QHBoxLayout(search_row);
    search_layout->setContentsMargins(0, 6, 6, 6);
    search_layout->setSpacing(6);

    m_searchBar = new QLineEdit(search_row);
    m_searchBar->setObjectName(QStringLiteral("frameTreeSearch"));
    m_searchBar->setPlaceholderText(tr("Search frames..."));
    m_searchBar->setToolTip(tr("<b>Search / Filter</b><br/>"
                               "Filters the frame list as you type (case-insensitive). Only matching frames remain "
                               "visible; nothing is deleted or changed.<br/>"
                               "<br/>"
                               "<b>What it searches</b><br/>"
                               "- File name and full path<br/>"
                               "- Filter name (e.g. L / R / G / B / Ha / OIII)<br/>"
                               "- Object name and session name/date<br/>"
                               "- Resolution (e.g. 3008x3004)<br/>"
                               "- Custom tag<br/>"
                               "- Exposure (e.g. 60s), ISO/Gain, Offset, Temperature<br/>"
                               "<br/>"
                               "<b>Tips</b><br/>"
                               "- Use partial text (e.g. \"m83\", \"ha\", \"-10\", \"3008x\", \"60s\")<br/>"
                               "- Press <b>Ctrl+F</b> to focus this box quickly<br/>"
                               "- Clear the text to show all frames again"));
    search_layout->addWidget(m_searchBar, /*stretch=*/1);

    m_filterStatsLabel = new QLabel(search_row);
    m_filterStatsLabel->setObjectName(QStringLiteral("frameTreeMatchCount"));
    m_filterStatsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    search_layout->addWidget(m_filterStatsLabel, /*stretch=*/0);

    if (type == FrameType::Light)
    {
        m_coverageToggle = new QCheckBox(tr("Coverage"), search_row);
        m_coverageToggle->setObjectName(QStringLiteral("coverageToggle"));
        m_coverageToggle->setToolTip(tr("Show only frames that appear to be missing calibration matches."));
        search_layout->addWidget(m_coverageToggle, /*stretch=*/0);
        connect(m_coverageToggle, &QCheckBox::toggled, this, [this](bool checked) {
            if (m_treeModel)
            {
                m_treeModel->setShowOnlyCalibrationWarnings(checked);
                refreshFilterStats();
                schedulePersistUiState();
            }
        });
    }

    left_layout->addWidget(search_row, /*stretch=*/0);

    m_tree = new FrameTreeView(this);
    m_tree->setFrameType(type);
    m_tree->setGroupTreeModel(m_treeModel);
    if (m_tree && m_tree->header())
    {
        connect(m_tree->header(), &QHeaderView::sectionResized, this, [this]() { schedulePersistUiState(); });
        connect(m_tree->header(), &QHeaderView::sectionMoved, this, [this]() { schedulePersistUiState(); });
    }

    if (m_tree && m_tree->selectionModel())
    {
        connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                [this](const QItemSelection&, const QItemSelection&) { updateActionStripButtonStates(); });
    }

    // Update summary when checkbox state changes or when the model is rebuilt.
    if (m_treeModel)
    {
        connect(m_treeModel, &QAbstractItemModel::dataChanged, this, [this]() { refreshQuickStats(); });
        connect(m_treeModel, &QAbstractItemModel::modelReset, this, [this]() { refreshQuickStats(); });
    }

    if (m_searchBar && m_treeModel)
    {
        m_searchDebounce = new QTimer(this);
        m_searchDebounce->setSingleShot(true);
        m_searchDebounce->setInterval(200);
        connect(m_searchDebounce, &QTimer::timeout, this, &FrameSourceTab::applySearchFilter);

        connect(m_searchBar, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_pendingSearchText = text;
            if (m_searchDebounce)
            {
                m_searchDebounce->start();
            }
            else
            {
                applySearchFilter();
            }
        });

        connect(m_treeModel, &GroupTreeModel::filterChanged, this, &FrameSourceTab::refreshFilterStats);

        auto* focusSearch = new QShortcut(QKeySequence::Find, this);
        focusSearch->setContext(Qt::WidgetWithChildrenShortcut);
        connect(focusSearch, &QShortcut::activated, this, [this]() {
            if (m_searchBar)
            {
                m_searchBar->setFocus();
                m_searchBar->selectAll();
            }
        });
    }

    m_displayMode = new TreeDisplayModeWidget(this);
    m_displayMode->setModel(m_treeModel);
    if (m_treeModel)
    {
        connect(m_treeModel, &GroupTreeModel::displayModeChanged, this, [this]() { schedulePersistUiState(); });
    }

    // Display mode bar at the bottom (V1-style).
    left_layout->addWidget(m_tree, /*stretch=*/1);
    left_layout->addWidget(m_displayMode, /*stretch=*/0);

    auto* context_scroll = new QScrollArea(rightColumn);
    context_scroll->setObjectName(QStringLiteral("frameSourceRightScrollArea"));
    context_scroll->setWidgetResizable(true);
    context_scroll->setFrameShape(QFrame::NoFrame);
    context_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* context = new QWidget(context_scroll);
    context->setObjectName(QStringLiteral("frameSourceRightPanelContents"));
    context->setAutoFillBackground(true);
    // Match CalibrationTab's right panel feel: a panel-like background (darker in dark mode)
    // rather than an input-field background.
    context->setBackgroundRole(QPalette::Window);
    context->setAttribute(Qt::WA_StyledBackground, true);

    auto* context_layout = new QVBoxLayout(context);
    // Keep consistent padding with CalibrationTab right panel.
    context_layout->setContentsMargins(14, 14, 14, 14);
    context_layout->setSpacing(10);
    context_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);

    context_scroll->setWidget(context);

    if (appContext)
    {
        const std::string previewName = std::string("IntegrationPreview_") + std::to_string(static_cast<int>(type));
        m_preview = new ImageViewControl(rightColumn);
        m_preview->initialize(previewName, ViewControlMode::st_preview);
        m_preview->setAutoDebayerCfaImagePreviews(true);
    }
    if (m_preview)
    {
        // Every type states its preview's mode, so the mode shown never depends on what the view last did.
        m_preview->setViewMode(shouldDefaultPreviewToAutostretch(type) ? ImageViewMode::AUTOSTRETCH_MODE
                                                                       : ImageViewMode::LINEAR_MODE);
        m_preview->setCalibrationPreviewVisible(type == FrameType::Light);
        rightColumnLayout->addWidget(m_preview, /*stretch=*/100);
    }

    rightColumnLayout->addWidget(context_scroll, /*stretch=*/0);

    m_groupingOptions = new GroupingOptionsWidget(context);
    m_groupingOptions->setFrameType(type);
    m_groupingOptions->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (model)
    {
        m_groupingDebounce = new QTimer(this);
        m_groupingDebounce->setSingleShot(true);
        m_groupingDebounce->setInterval(150);
        connect(m_groupingDebounce, &QTimer::timeout, this, &FrameSourceTab::applyPendingGroupingOptions);

        const GroupingOptions initial_opts = model->groupingOptions(type);
        m_groupingOptions->setOptions(initial_opts);
        if (m_tree)
        {
            m_tree->setGroupingOptions(initial_opts);
        }
        connect(m_groupingOptions, &GroupingOptionsWidget::optionsChanged, this, [this](const GroupingOptions& opts) {
            m_pendingGroupingOptions = opts;
            m_hasPendingGroupingOptions = true;

            const int count = m_model ? static_cast<int>(m_model->frames(m_type).size()) : 0;
            const bool showProgress = (count > 500) && m_regroupWarningLabel;

            if (showProgress)
            {
                ImageIntegration::Style::applyBannerStyle(m_regroupWarningLabel,
                                                          ImageIntegration::Style::BannerTone::Info);
                m_regroupWarningLabel->setText(tr("Regrouping..."));
                m_regroupWarningLabel->setVisible(true);
            }

            if (m_groupingDebounce)
            {
                m_groupingDebounce->start();
            }
        });
    }
    context_layout->addWidget(m_groupingOptions, /*stretch=*/0);

    m_regroupWarningLabel = new QLabel(context);
    m_regroupWarningLabel->setObjectName(QStringLiteral("regroupWarningLabel"));
    m_regroupWarningLabel->setVisible(false);
    m_regroupWarningLabel->setWordWrap(true);
    ImageIntegration::Style::applyBannerStyle(m_regroupWarningLabel, ImageIntegration::Style::BannerTone::Warning);
    context_layout->addWidget(m_regroupWarningLabel, /*stretch=*/0);

    // Lights-only widgets.
    if (type == FrameType::Light)
    {
        const auto addSeparator = [&]() {
            auto* line = new QFrame(context);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            context_layout->addWidget(line, /*stretch=*/0);
        };

        // Visual separation from the Group By section.
        addSeparator();

        m_statisticsDashboard = new ImageIntegration::FrameStatisticsDashboard(type, model, context);
        context_layout->addWidget(m_statisticsDashboard, /*stretch=*/0);

        addSeparator();

        m_issuesWidget = new ImageIntegration::FrameGroupingIssuesWidget(type, model, context);
        context_layout->addWidget(m_issuesWidget, /*stretch=*/0);
        connect(m_issuesWidget, &ImageIntegration::FrameGroupingIssuesWidget::showGroupRequested, this,
                [this](const GroupKey& key) {
                    if (!m_treeModel || !m_tree)
                    {
                        return;
                    }

                    const int topLevel = m_treeModel->rowCount(QModelIndex{});
                    for (int row = 0; row < topLevel; ++row)
                    {
                        const QModelIndex idx = m_treeModel->index(row, 0, QModelIndex{});
                        if (!idx.isValid() || !m_treeModel->isGroup(idx))
                        {
                            continue;
                        }

                        if (m_treeModel->getGroupKey(idx) == key)
                        {
                            m_tree->expand(idx);
                            m_tree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                            if (auto* sel = m_tree->selectionModel())
                            {
                                sel->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            }
                            return;
                        }
                    }
                });

        addSeparator();

        m_quickActionsWidget = new ImageIntegration::FrameQuickActionsWidget(type, model, context, m_undoStack);
        context_layout->addWidget(m_quickActionsWidget, /*stretch=*/0);
    }

    // A hidden quick-stats label for code that expects statsLabel().
    m_statsLabel = new QLabel(context);
    m_statsLabel->setObjectName(QStringLiteral("quickStatsLabel"));
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setVisible(false);
    context_layout->addWidget(m_statsLabel, /*stretch=*/0);

    // Absorb any leftover vertical space at the bottom (e.g., when preview hits max-height).
    context_layout->addStretch(1);

    // Add widgets to splitter and set initial sizes
    m_splitter->addWidget(left);
    m_splitter->addWidget(rightColumn);

    // Keep the right side as a narrow column (user can still drag the splitter wider).
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);

    // Set size constraints for preview panel
    if (m_preview)
    {
        m_preview->setMinimumWidth(200);
        m_preview->setMinimumHeight(120);
    }
    context_scroll->setMinimumWidth(450);
    // Keep restored splitter state from hiding the preview/context column entirely.
    m_splitter->setCollapsible(1, false);

    refreshQuickStats();
    refreshFilterStats();

    restoreUiState();
    schedulePreviewPanelAspectClamp();

    if (m_model)
    {
        connect(m_model, &IntegrationDataModel::framesAdded, this, [this](FrameType changedType, const QStringList&) {
            if (changedType == m_type)
            {
                refreshQuickStats();
                updateActionStripButtonStates();
            }
        });
        connect(m_model, &IntegrationDataModel::framesRemoved, this, [this](FrameType changedType, const QStringList&) {
            if (changedType == m_type)
            {
                refreshQuickStats();
                updateActionStripButtonStates();
            }
        });
        connect(m_model, &IntegrationDataModel::groupingChanged, this, [this](FrameType changedType) {
            if (changedType == m_type)
            {
                refreshQuickStats();
                if (m_tree)
                {
                    m_tree->setGroupingOptions(m_model->groupingOptions(m_type));
                }
                updateActionStripButtonStates();
            }
        });
        connect(m_model, &IntegrationDataModel::calibrationChanged, this, [this]() {
            // Calibration impacts light-group warnings and right-panel diagnostics.
            refreshQuickStats();
            updateActionStripButtonStates();
        });
        connect(m_model, &IntegrationDataModel::settingsChanged, this, [this]() {
            refreshQuickStats();
            updateActionStripButtonStates();
        });
        connect(m_model, &IntegrationDataModel::modelReset, this, [this]() {
            refreshQuickStats();
            updateActionStripButtonStates();
        });
    }

    updateActionStripButtonStates();
}

void FrameSourceTab::updateActionStripButtonStates()
{
    const bool hasFrames = (m_model != nullptr) && (m_model->frames(m_type).size() > 0);

    if (m_removeAllButton)
    {
        m_removeAllButton->setEnabled(hasFrames);
    }

    bool hasSelection = false;
    if (m_tree)
    {
        hasSelection = !m_tree->selectedFilePaths().isEmpty();
    }
    if (m_removeSelectedButton)
    {
        m_removeSelectedButton->setEnabled(hasSelection);
    }
}

FrameType FrameSourceTab::frameType() const
{
    return m_type;
}

FrameTreeView* FrameSourceTab::tree() const
{
    return m_tree;
}

GroupTreeModel* FrameSourceTab::treeModel() const
{
    return m_treeModel;
}

TreeDisplayModeWidget* FrameSourceTab::displayModeWidget() const
{
    return m_displayMode;
}

ImageViewControl* FrameSourceTab::previewPanel() const
{
    return m_preview;
}

GroupingOptionsWidget* FrameSourceTab::groupingOptions() const
{
    return m_groupingOptions;
}

QLabel* FrameSourceTab::regroupWarningLabel() const
{
    return m_regroupWarningLabel;
}

QLabel* FrameSourceTab::statsLabel() const
{
    return m_statsLabel;
}

QLineEdit* FrameSourceTab::searchBar() const
{
    return m_searchBar;
}

void FrameSourceTab::refreshGroupingOptionsFromModel()
{
    if (!m_model || !m_groupingOptions)
    {
        return;
    }

    m_groupingOptions->setOptions(m_model->groupingOptions(m_type));
}

void FrameSourceTab::refreshQuickStats()
{
    if (!m_model)
    {
        return;
    }

    if (m_statisticsDashboard)
    {
        m_statisticsDashboard->refresh();
    }
    if (m_issuesWidget)
    {
        m_issuesWidget->refresh();
    }
    if (m_quickActionsWidget)
    {
        m_quickActionsWidget->refresh();
    }

    const int frame_count = static_cast<int>(m_model->frames(m_type).size());
    const int master_count = m_model->masterCount(m_type);
    const int checked_count = checkedFrameCount();

    int group_count = 0;
    switch (m_type)
    {
    case FrameType::Bias:
        group_count = static_cast<int>(m_model->biasGroups().size());
        break;
    case FrameType::Dark:
        group_count = static_cast<int>(m_model->darkGroups().size());
        break;
    case FrameType::Flat:
        group_count = static_cast<int>(m_model->flatGroups().size());
        break;
    case FrameType::Light:
        group_count = static_cast<int>(m_model->lightGroups().size());
        break;
    }

    if (m_statsLabel)
    {
        m_statsLabel->setText(tr("Included: %1/%2\nGroups: %3\nMasters: %4")
                                  .arg(checked_count)
                                  .arg(frame_count)
                                  .arg(group_count)
                                  .arg(master_count));
    }
}

int FrameSourceTab::checkedFrameCount() const
{
    if (!m_model)
    {
        return 0;
    }

    int includedFrames = 0;
    for (const auto& frame : m_model->frames(m_type).frames())
    {
        if (!frame.excluded)
        {
            ++includedFrames;
        }
    }

    return includedFrames;
}

void FrameSourceTab::refreshFilterStats()
{
    if (!m_filterStatsLabel || !m_treeModel)
    {
        return;
    }

    const int total = m_treeModel->totalCount();
    const int match = m_treeModel->matchCount();
    if (total <= 0)
    {
        m_filterStatsLabel->clear();
        return;
    }

    m_filterStatsLabel->setText(tr("Showing %1 of %2 frames").arg(match).arg(total));
}

void FrameSourceTab::keyPressEvent(QKeyEvent* event)
{
    if (event && event->matches(QKeySequence::Find))
    {
        if (m_searchBar)
        {
            m_searchBar->setFocus();
            m_searchBar->selectAll();
        }
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void FrameSourceTab::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreviewPanelSize();
    schedulePreviewPanelAspectClamp();
}

void FrameSourceTab::schedulePreviewPanelAspectClamp()
{
    // When resizing the splitter, Qt may emit splitterMoved before child widgets have their new widths.
    // Deferring to the next event loop tick ensures we clamp based on the final, post-layout width.
    if (m_previewClampDebounce)
    {
        m_previewClampDebounce->start();
        return;
    }

    updatePreviewPanelAspectClamp();
}

void FrameSourceTab::updatePreviewPanelAspectClamp()
{
    if (!m_preview || !m_splitter)
    {
        return;
    }

    // Use the right pane's width from the splitter sizes (more reliable than m_preview->width()
    // which may not yet reflect the new splitter position during a drag).
    const QList<int> sizes = m_splitter->sizes();
    const int rightColumnWidth = (sizes.size() >= 2) ? sizes.at(1) : m_preview->width();
    if (rightColumnWidth <= 0)
    {
        return;
    }

    int horizontalMargins = 0;
    if (const QWidget* parent = m_preview->parentWidget())
    {
        if (const QLayout* layout = parent->layout())
        {
            const QMargins m = layout->contentsMargins();
            horizontalMargins = m.left() + m.right();
        }
    }

    // Account for the preview container padding so we clamp based on the actual usable width.
    const int effectiveWidth = (std::max)(0, rightColumnWidth - horizontalMargins);

    // Keep the preview from becoming overly tall when the window height grows.
    // The max height is derived from its width (so height only increases when width increases).
    constexpr double kAspectHeightOverWidth = 9.0 / 16.0; // 16:9-ish default preview shape
    const int minH = (std::max)(0, m_preview->minimumHeight());
    const int maxH = (std::max)(minH, static_cast<int>(std::lround(effectiveWidth * kAspectHeightOverWidth)));
    m_preview->setMaximumHeight(maxH);
    m_preview->updateGeometry();
}

void FrameSourceTab::updatePreviewPanelSize()
{
    if (!m_splitter || !m_preview)
    {
        return;
    }

    if (m_splitterUserSized || m_splitterAutoSized)
    {
        return;
    }

    // Dynamically adjust the context panel width based on total width
    // Use approximately 25-30% of total width for context panel, with min/max constraints
    const int totalWidth = m_splitter->width();
    if (totalWidth <= 0)
    {
        return;
    }

    // Calculate target context width: narrow column feel, between 480 and 690 pixels.
    // (Right panel keeps a column-like width when resizing the window, but remains user-resizable.)
    int targetContextWidth = static_cast<int>(totalWidth * 0.30);
    targetContextWidth = (std::max)(480, (std::min)(690, targetContextWidth));

    const int leftWidth = totalWidth - targetContextWidth - m_splitter->handleWidth();

    if (leftWidth > 0 && targetContextWidth > 0)
    {
        m_splitter->setSizes({leftWidth, targetContextWidth});
        m_splitterAutoSized = true;
    }
}

void FrameSourceTab::applySearchFilter()
{
    if (!m_treeModel)
    {
        return;
    }

    FrameTreeFilter filter;
    filter.setSearchText(m_pendingSearchText);
    if (filter.isEmpty())
    {
        m_treeModel->clearFilter();
    }
    else
    {
        m_treeModel->setFilter(filter);
    }
    refreshFilterStats();
}

void FrameSourceTab::applyPendingGroupingOptions()
{
    if (!m_model || !m_hasPendingGroupingOptions)
    {
        return;
    }

    const int count = static_cast<int>(m_model->frames(m_type).size());
    const bool showProgress = (count > 500) && m_regroupWarningLabel;

    if (m_undoStack)
    {
        const GroupingOptions oldOptions = m_model->groupingOptions(m_type);
        if (!(oldOptions == m_pendingGroupingOptions))
        {
            m_undoStack->push(new SetGroupingOptionsCommand(m_model, m_type, oldOptions, m_pendingGroupingOptions));
        }
    }
    else
    {
        m_model->setGroupingOptions(m_type, m_pendingGroupingOptions);
    }
    m_hasPendingGroupingOptions = false;

    // Tree grouping options are updated via IntegrationDataModel::groupingChanged.

    if (showProgress && m_regroupWarningLabel)
    {
        if (m_regroupWarningLabel->text() == tr("Regrouping..."))
        {
            m_regroupWarningLabel->setVisible(false);
        }

        if (!m_regroupWarningOriginalStyle.isEmpty())
        {
            ImageIntegration::Style::applyBannerStyle(m_regroupWarningLabel,
                                                      ImageIntegration::Style::BannerTone::Warning);
        }
    }
}

void FrameSourceTab::restoreUiState()
{
    if (!m_splitter)
    {
        return;
    }

    QSettings settings = an::ui::applicationSettings();
    settings.beginGroup(settingsGroupFor(m_type));
    m_savedSplitterState = settings.value(QStringLiteral("splitterState")).toByteArray();
    m_savedHeaderState = settings.value(QStringLiteral("treeHeaderState")).toByteArray();
    const int displayMode =
        settings.value(QStringLiteral("displayMode"), static_cast<int>(GroupTreeModel::DisplayMode::Flat)).toInt();
    const bool coverageOnly = settings.value(QStringLiteral("coverageOnly"), false).toBool();
    settings.endGroup();

    if (m_treeModel)
    {
        const auto mode = static_cast<GroupTreeModel::DisplayMode>(displayMode);
        if (m_treeModel->displayMode() != mode)
        {
            m_treeModel->setDisplayMode(mode);
        }

        if (m_type == FrameType::Light)
        {
            m_treeModel->setShowOnlyCalibrationWarnings(coverageOnly);
        }
    }

    if (m_coverageToggle)
    {
        QSignalBlocker blocker(m_coverageToggle);
        m_coverageToggle->setChecked(coverageOnly);
    }

    if (!m_savedSplitterState.isEmpty())
    {
        const bool restored = m_splitter->restoreState(m_savedSplitterState);
        const auto sizes = m_splitter->sizes();
        const int minimumPreviewWidth = m_preview ? m_preview->minimumWidth() : 1;
        m_splitterUserSized = restored && sizes.size() >= 2 && sizes.at(1) >= minimumPreviewWidth;
        m_splitterAutoSized = false;
    }

    if (!m_savedHeaderState.isEmpty() && m_tree && m_tree->header())
    {
        // Restore after the view has applied its own default sizing.
        QTimer::singleShot(0, this, [this]() {
            if (!m_tree || !m_tree->header() || m_savedHeaderState.isEmpty())
            {
                return;
            }
            m_tree->header()->restoreState(m_savedHeaderState);
        });

        if (m_treeModel)
        {
            connect(
                m_treeModel, &QAbstractItemModel::modelReset, this,
                [this]() {
                    QTimer::singleShot(0, this, [this]() {
                        if (m_tree && m_tree->header() && !m_savedHeaderState.isEmpty())
                        {
                            m_tree->header()->restoreState(m_savedHeaderState);
                        }
                    });
                },
                Qt::QueuedConnection);
        }
    }
}

void FrameSourceTab::schedulePersistUiState()
{
    if (!m_persistDebounce)
    {
        m_persistDebounce = new QTimer(this);
        m_persistDebounce->setSingleShot(true);
        m_persistDebounce->setInterval(250);
        connect(m_persistDebounce, &QTimer::timeout, this, &FrameSourceTab::persistUiStateNow);
    }
    m_persistDebounce->start();
}

void FrameSourceTab::persistUiStateNow()
{
    if (!m_splitter)
    {
        return;
    }

    QSettings settings = an::ui::applicationSettings();
    settings.beginGroup(settingsGroupFor(m_type));
    settings.setValue(QStringLiteral("splitterState"), m_splitter->saveState());
    if (m_tree && m_tree->header())
    {
        settings.setValue(QStringLiteral("treeHeaderState"), m_tree->header()->saveState());
        m_savedHeaderState = m_tree->header()->saveState();
    }
    if (m_treeModel)
    {
        settings.setValue(QStringLiteral("displayMode"), static_cast<int>(m_treeModel->displayMode()));
    }
    if (m_coverageToggle)
    {
        settings.setValue(QStringLiteral("coverageOnly"), m_coverageToggle->isChecked());
    }
    settings.endGroup();
}
