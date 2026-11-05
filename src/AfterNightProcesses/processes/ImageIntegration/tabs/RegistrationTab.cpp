// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "RegistrationTab.h"
#include "RegistrationTabUiBuilder.h"
#include "TabStyling.h"

#include "controls/IAppContext.h"
#include "processes/ImageIntegration/controls/RegistrationSettingsDialog.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/RegistrationSettingsUtils.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/ImageIntegration/dialogs/CometTrackingDialogAutoTrackAdapter.h"
#include "processes/ImageIntegration/workflow/RegistrationWorkAnalyzer.h"
#include "processes/ImageIntegration/workflow/WorkflowExecutionController.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationMetricsSchema.h"
#include "processes/core/RegistrationMetricsWriter.h"
#include "RegistrationMetricsDataAccess.h"

#include <AfterNightPlatform/Logging.h>

#include <QBoxLayout>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QHash>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFutureWatcher>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QShortcut>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <unordered_map>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(RegistrationTab)

#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "processes/ImageIntegration/widgets/QualityDistributionWidget.h"
#include "theme/AnDesignTokens.h"

#include "RegistrationTabImpl.h"

namespace
{
// Persistent frame-quality sort state, shared across tab instances.
int g_qualitySortColumn = -1;
Qt::SortOrder g_qualitySortOrder = Qt::AscendingOrder;
} // namespace

void RegistrationTab::Impl::applyCardContentMargins()
{
    const QMargins margins = cardContentMargins();
    const int layoutSpacing = an::ui::tokens().spacing.sm;

    const auto applyToCard = [margins, layoutSpacing](an::ui::AnCardWidget* card) {
        if (!card)
        {
            return;
        }
        if (QLayout* layout = card->contentLayout())
        {
            layout->setContentsMargins(margins);
            layout->setSpacing(layoutSpacing);
        }
        card->setProperty("anCardBackdropColor", an::ui::tokens().colors.bgSurface);
        card->setProperty("anCardFlat", true);
        card->update();
    };

    applyToCard(ui.grpRegistrationGroups);
    applyToCard(ui.grpFrameQuality);
}

RegistrationTab::Impl::Impl(RegistrationTab* parent, IntegrationDataModel* m, QUndoStack* undo)
    : q(parent), model(m), undoStack(undo)
{
    ui = an::ui::imageintegration::buildRegistrationTabUi(q);
    if (ui.grpRegistrationGroups)
    {
        // AnCardWidget doesn't need setFlat
    }
    if (ui.grpFrameQuality)
    {
        // AnCardWidget doesn't need setFlat
    }
    applyCardContentMargins();
    applyBackground();

    if (ui.lblFrameStatusLegend)
    {
        // Theme-aware secondary text color
        const QColor sec = q->palette().color(QPalette::Disabled, QPalette::Text);
        QPalette p = ui.lblFrameStatusLegend->palette();
        p.setColor(QPalette::WindowText, sec);
        ui.lblFrameStatusLegend->setPalette(p);
        QFont f = an::ui::tokens().typography.caption;
        ui.lblFrameStatusLegend->setFont(f);
    }

    configureFrameQualityScrollArea();
    setupPerGroupButtons();
    configureLayoutSizing();
    setupTables();
    setupChartWidget();
    setupDebounceTimer();
    setupConnections();
    updateAll();
}

void RegistrationTab::Impl::applyBackground(QWidget* widget, bool useBase)
{
    QWidget* target = widget ? widget : q;
    if (!target)
    {
        return;
    }

    // Let the QTabWidget pane draw the page background so the surrounding "gutter"
    // (matching the tab header) remains visible. We only paint backgrounds for inner
    // containers (group boxes / scroll contents).
    if (target == q)
    {
        target->setAutoFillBackground(false);
        target->setAttribute(Qt::WA_StyledBackground, false);
    }
    else
    {
        // Ensure we always repaint cleared background regions when child widgets move/resize
        // (prevents "overlap trails" during interactive window resizing).
        target->setAutoFillBackground(true);
        target->setAttribute(Qt::WA_StyledBackground, true);
    }

    QWidget* palSource = target->parentWidget() ? target->parentWidget() : q;
    if (palSource && target != q)
    {
        QPalette pal = target->palette();
        const QPalette sourcePal = qApp ? qApp->palette() : palSource->palette();
        const QColor bgColor = useBase ? sourcePal.color(QPalette::Base) : sourcePal.color(QPalette::Window);
        pal.setColor(QPalette::Window, bgColor);
        target->setPalette(pal);
    }

    if (target == q)
    {
        if (ui.grpRegistrationGroups)
        {
            // AnCardWidget handles background
        }
        if (ui.grpFrameQuality)
        {
            // AnCardWidget handles background
        }
        if (ui.grpQualityThresholds)
        {
            // AnCardWidget handles background
        }
    }
}

void RegistrationTab::Impl::applyThemeStyling()
{
    if (applyingThemeStyling)
    {
        return;
    }
    applyingThemeStyling = true;

    applyBackground();
    applyCardContentMargins();

    if (auto* frameQualityScroll = q->findChild<QScrollArea*>(QStringLiteral("frameQualityScrollArea")))
    {
        applyBackground(frameQualityScroll);
        if (frameQualityScroll->viewport())
        {
            applyBackground(frameQualityScroll->viewport());
        }
        if (auto* content = frameQualityScroll->widget())
        {
            applyBackground(content);
        }
    }

    if (ui.lblFrameStatusLegend)
    {
        const QColor sec = q->palette().color(QPalette::Disabled, QPalette::Text);
        QPalette p = ui.lblFrameStatusLegend->palette();
        p.setColor(QPalette::WindowText, sec);
        ui.lblFrameStatusLegend->setPalette(p);
    }

    if (ui.tblRegistrationGroups)
    {
        ImageIntegration::Style::applyTableStyle(ui.tblRegistrationGroups);
        ui.tblRegistrationGroups->horizontalHeader()->setStretchLastSection(false);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableGroupCol, QHeaderView::Stretch);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableReferenceCol,
                                                                           QHeaderView::Stretch);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableStatusCol, QHeaderView::Fixed);
        ui.tblRegistrationGroups->setColumnWidth(kGroupTableStatusCol, 90);
    }

    if (ui.tblFrameQuality)
    {
        ImageIntegration::Style::applyTableStyle(ui.tblFrameQuality);
        ui.tblFrameQuality->horizontalHeader()->setStretchLastSection(false);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kFrameCol, QHeaderView::Stretch);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kStatusCol, QHeaderView::Fixed);
        ui.tblFrameQuality->setColumnWidth(kStatusCol, 40);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kQualityCol, QHeaderView::Fixed);
        ui.tblFrameQuality->setColumnWidth(kQualityCol, 60);
    }

    if (chartWidget)
    {
        chartWidget->update();
    }

    if (ui.btnRunRegistration)
    {
        ui.btnRunRegistration->setIcon(integrationIcon(QStringLiteral("play")));
    }
    if (btnRunRegistrationGroup)
    {
        btnRunRegistrationGroup->setIcon(integrationIcon(QStringLiteral("play")));
    }
    if (btnClearRegistrationGroup)
    {
        btnClearRegistrationGroup->setIcon(integrationIcon(QStringLiteral("delete")));
    }

    q->update();
    applyingThemeStyling = false;
}

void RegistrationTab::Impl::configureFrameQualityScrollArea()
{
    if (!ui.grpFrameQuality)
    {
        return;
    }

    auto* frameContentOuter = ui.grpFrameQuality->contentWidget();
    if (!frameContentOuter)
    {
        return;
    }

    auto* frameLayout = qobject_cast<QVBoxLayout*>(frameContentOuter->layout());
    if (!frameLayout)
    {
        return;
    }

    const QMargins margins = cardContentMargins();
    frameLayout->setContentsMargins(margins);

    // Move existing Frame Quality widgets into a scroll area so the table + histogram
    // can grow without forcing the whole window to be tall.
    auto* frameContent = new QWidget(frameContentOuter);
    frameContent->setObjectName(QStringLiteral("frameQualityContent"));
    auto* contentLayout = new QVBoxLayout(frameContent);
    contentLayout->setContentsMargins(margins);
    contentLayout->setSpacing(frameLayout->spacing());

    QVector<QLayoutItem*> items;
    while (frameLayout->count() > 0)
    {
        if (auto* item = frameLayout->takeAt(0))
        {
            items.push_back(item);
        }
    }

    for (auto* item : items)
    {
        if (auto* widget = item->widget())
        {
            contentLayout->addWidget(widget);
            // The new layout made an item of its own for the widget; the one taken from the old is nobody's.
            delete item;
        }
        else if (auto* layout = item->layout())
        {
            contentLayout->addLayout(layout);
        }
        else
        {
            contentLayout->addItem(item);
        }
    }

    auto* scroll = new QScrollArea(frameContentOuter);
    scroll->setObjectName(QStringLiteral("frameQualityScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    scroll->setMinimumHeight(0);
    scroll->setWidget(frameContent);

    applyBackground(scroll);
    if (scroll->viewport())
    {
        applyBackground(scroll->viewport());
    }

    frameLayout->addWidget(scroll);
}

bool RegistrationTab::Impl::executeMetricsDb(const std::function<void(QSqlDatabase&)>& fn)
{
    const QString outputDir = model ? model->outputDirectory().trimmed() : QString();
    const bool ok = metricsDataAccess.execute(outputDir, fn);
    if (!ok)
    {
        const QString accessError = metricsDataAccess.lastError().trimmed();
        if (!accessError.isEmpty())
        {
            lastDbError = RegistrationTab::tr("Failed to open metrics database: %1").arg(accessError);
        }
    }
    return ok;
}

void RegistrationTab::Impl::setupPerGroupButtons()
{
    const auto applyButtonVariant = [](QPushButton* button, const char* variant) {
        if (!button || !variant)
        {
            return;
        }
        button->setProperty("anButtonVariant", QString::fromUtf8(variant));
    };

    applyButtonVariant(ui.btnRunRegistration, "primary");
    applyButtonVariant(ui.btnGroupSettings, "secondary");
    applyButtonVariant(ui.btnSetReference, "secondary");
    applyButtonVariant(ui.btnExcludeSelected, "secondary");
    applyButtonVariant(ui.btnIncludeSelected, "secondary");
    applyButtonVariant(ui.btnApplyThresholds, "secondary");
    applyButtonVariant(ui.btnDetectOutliers, "secondary");
    applyButtonVariant(ui.btnResetExcluded, "danger");

    if (!ui.btnGroupSettings)
    {
        return;
    }

    btnRunRegistrationGroup =
        new QPushButton(integrationIcon(QStringLiteral("play")), RegistrationTab::tr("Run Registration"), q);
    btnRunRegistrationGroup->setObjectName(QStringLiteral("btnRunRegistrationGroup"));
    applyButtonVariant(btnRunRegistrationGroup, "primary");
    btnRunRegistrationGroup->setToolTip(
        RegistrationTab::tr("Run the registration workflow for the selected registration group only.\n"
                            "Only new/changed frames are processed when possible."));

    btnClearRegistrationGroup =
        new QPushButton(integrationIcon(QStringLiteral("delete")), RegistrationTab::tr("Clear Registration..."), q);
    btnClearRegistrationGroup->setObjectName(QStringLiteral("btnClearRegistrationGroup"));
    applyButtonVariant(btnClearRegistrationGroup, "danger");
    btnClearRegistrationGroup->setToolTip(
        RegistrationTab::tr("Remove all registration data for the selected group (star detection cache and metrics)."));

    // Place buttons to the right of the spacer (same row as settings).
    QLayout* layout = ui.registrationGroupActionsLayout;
    if (!layout)
    {
        QWidget* container = ui.btnGroupSettings->parentWidget();
        layout = container ? container->layout() : nullptr;
    }

    if (auto* box = qobject_cast<QBoxLayout*>(layout))
    {
        int insertIdx = -1;
        for (int i = 0; i < box->count(); ++i)
        {
            QLayoutItem* item = box->itemAt(i);
            if (item && item->spacerItem())
            {
                insertIdx = i;
                break;
            }
        }

        if (insertIdx >= 0)
        {
            box->insertWidget(insertIdx + 1, btnRunRegistrationGroup);
            box->insertWidget(insertIdx + 2, btnClearRegistrationGroup);
        }
        else
        {
            const int idx = box->indexOf(ui.btnGroupSettings);
            if (idx >= 0)
            {
                box->insertWidget(idx + 1, btnRunRegistrationGroup);
                box->insertWidget(idx + 2, btnClearRegistrationGroup);
            }
            else
            {
                box->addWidget(btnRunRegistrationGroup);
                box->addWidget(btnClearRegistrationGroup);
            }
        }
    }
    else if (layout)
    {
        layout->addWidget(btnRunRegistrationGroup);
        layout->addWidget(btnClearRegistrationGroup);
    }

    if (ui.btnRunRegistration)
    {
        ui.btnRunRegistration->setIcon(integrationIcon(QStringLiteral("play")));
        ui.btnRunRegistration->setText(RegistrationTab::tr("Run Registration (All Groups)"));
        ui.btnRunRegistration->setToolTip(
            RegistrationTab::tr("Run the registration workflow for all registration groups.\n"
                                "Only new/changed frames are processed when possible.\n"
                                "If nothing needs updating, the workflow will not start."));
    }
}

void RegistrationTab::Impl::setupTables()
{
    if (ui.tblRegistrationGroups)
    {
        // Allow the window to shrink smaller than the table's default minimumSizeHint()
        // (QAbstractScrollArea defaults can otherwise force a large min-height for the entire tab).
        ui.tblRegistrationGroups->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        ui.tblRegistrationGroups->setMinimumHeight(110);

        ui.tblRegistrationGroups->setColumnCount(8);
        ui.tblRegistrationGroups->setHorizontalHeaderLabels(
            {RegistrationTab::tr("Group"), RegistrationTab::tr("Frames"), RegistrationTab::tr("Filters"),
             RegistrationTab::tr("Exposures"), RegistrationTab::tr("Alignment"), RegistrationTab::tr("Algorithm"),
             RegistrationTab::tr("Reference"), RegistrationTab::tr("Status")});

        // Header tooltips (explain each column).
        const auto setHeaderTip = [](QTableWidget* table, int col, const QString& tip) {
            if (!table)
                return;
            if (auto* item = table->horizontalHeaderItem(col))
            {
                item->setToolTip(tip);
            }
        };

        setHeaderTip(ui.tblRegistrationGroups, 0,
                     RegistrationTab::tr(
                         "Registration group label (derived from grouping rules like resolution/object/filter)."));
        setHeaderTip(ui.tblRegistrationGroups, 1,
                     RegistrationTab::tr(
                         "Frames in this group shown as included/total (non-excluded / all frames in the group)."));
        setHeaderTip(ui.tblRegistrationGroups, 2, RegistrationTab::tr("Unique filter names present in this group."));
        setHeaderTip(ui.tblRegistrationGroups, 3,
                     RegistrationTab::tr("Exposure summary for frames in this group (single value, list, or range)."));
        setHeaderTip(ui.tblRegistrationGroups, 4,
                     RegistrationTab::tr("Alignment mode used for this group (e.g. Standard/Mosaic/Intersection)."));
        setHeaderTip(ui.tblRegistrationGroups, 5,
                     RegistrationTab::tr("Transformation algorithm used during registration for this group."));
        setHeaderTip(ui.tblRegistrationGroups, 6,
                     RegistrationTab::tr("Reference frame used for registration (user-selected or auto-suggested)."));
        setHeaderTip(ui.tblRegistrationGroups, 7,
                     RegistrationTab::tr("Registration metrics status for the group (Pending/Partial/Complete)."));

        ui.tblRegistrationGroups->setSelectionMode(QAbstractItemView::SingleSelection);
        ImageIntegration::Style::applyTableStyle(ui.tblRegistrationGroups);

        // Keep selection highlight readable even when the table is not focused (e.g. clicking the chart).
        // The proxy style and applyTableStyle() handle all visual properties.

        // applyTableStyle() enables stretchLastSection(true) globally; override it for this table.
        // We want Group + Reference to consume remaining space, not Status.
        ui.tblRegistrationGroups->horizontalHeader()->setStretchLastSection(false);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableGroupCol, QHeaderView::Stretch);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableReferenceCol,
                                                                           QHeaderView::Stretch);
        ui.tblRegistrationGroups->horizontalHeader()->setSectionResizeMode(kGroupTableStatusCol, QHeaderView::Fixed);
        ui.tblRegistrationGroups->setColumnWidth(kGroupTableStatusCol, 90);

        ui.tblRegistrationGroups->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(ui.tblRegistrationGroups, &QWidget::customContextMenuRequested,
                         [this](const QPoint& pos) { showGroupContextMenu(pos); });

        if (auto* boxLayout = qobject_cast<QBoxLayout*>(ui.tblRegistrationGroups->parentWidget()->layout()))
        {
            boxLayout->setStretchFactor(ui.tblRegistrationGroups, 1);
        }
    }

    if (ui.tblFrameQuality)
    {
        // Allow the window to shrink smaller than the table's default minimumSizeHint()
        // (QAbstractScrollArea defaults can otherwise force a large min-height for the entire tab).
        ui.tblFrameQuality->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        ui.tblFrameQuality->setMinimumHeight(160);

        ui.tblFrameQuality->setColumnCount(9);
        ui.tblFrameQuality->setHorizontalHeaderLabels(
            {RegistrationTab::tr("Status"), RegistrationTab::tr("Frame"), RegistrationTab::tr("Filter"),
             RegistrationTab::tr("Stars"), RegistrationTab::tr("FWHM"), RegistrationTab::tr("SNR"),
             RegistrationTab::tr("Ecc"), RegistrationTab::tr("Score"), RegistrationTab::tr("Quality")});

        // Header tooltips (explain each column).
        const auto setHeaderTip = [](QTableWidget* table, int col, const QString& tip) {
            if (!table)
                return;
            if (auto* item = table->horizontalHeaderItem(col))
            {
                item->setToolTip(tip);
            }
        };

        setHeaderTip(
            ui.tblFrameQuality, kStatusCol,
            RegistrationTab::tr("Per-frame status icon.\n"
                                "★ reference | ✗ excluded | ✓ included | ⚠ metrics missing | ❌ registration failed"));
        setHeaderTip(ui.tblFrameQuality, kFrameCol,
                     RegistrationTab::tr("Frame file name (hover the cell for full path and filter)."));
        setHeaderTip(
            ui.tblFrameQuality, kFilterCol,
            RegistrationTab::tr("Filter metadata for the frame (or (unknown)/(no filter) when not available)."));
        setHeaderTip(ui.tblFrameQuality, kStarsCol,
                     RegistrationTab::tr("Detected star count from registration metrics (higher is better)."));
        setHeaderTip(ui.tblFrameQuality, kFwhmCol,
                     RegistrationTab::tr("FWHM from registration metrics in pixels (lower is better)."));
        setHeaderTip(ui.tblFrameQuality, kSnrCol,
                     RegistrationTab::tr("Signal-to-noise ratio from registration metrics (higher is better)."));
        setHeaderTip(ui.tblFrameQuality, kEccCol,
                     RegistrationTab::tr("Star eccentricity from registration metrics (lower is better)."));
        setHeaderTip(ui.tblFrameQuality, kScoreCol,
                     RegistrationTab::tr(
                         "Combined quality score computed from the metrics (used for sorting/outlier detection)."));
        setHeaderTip(
            ui.tblFrameQuality, kQualityCol,
            RegistrationTab::tr("Overall quality bucket derived from per-metric quartiles (emoji indicator)."));

        ui.tblFrameQuality->setSelectionMode(QAbstractItemView::ExtendedSelection);
        ImageIntegration::Style::applyTableStyle(ui.tblFrameQuality);

        // applyTableStyle() enables stretchLastSection(true) globally; override it for this table.
        // We only want the Frame column to stretch, not the last (Quality) column.
        ui.tblFrameQuality->horizontalHeader()->setStretchLastSection(false);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kFrameCol, QHeaderView::Stretch);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kStatusCol, QHeaderView::Fixed);
        ui.tblFrameQuality->setColumnWidth(kStatusCol, 40);
        ui.tblFrameQuality->horizontalHeader()->setSectionResizeMode(kQualityCol, QHeaderView::Fixed);
        ui.tblFrameQuality->setColumnWidth(kQualityCol, 60);

        // Keep selection highlight readable even when the table is not focused (e.g. clicking the chart).
        // The proxy style and applyTableStyle() handle all visual properties.

        ui.tblFrameQuality->setSortingEnabled(true);
        ui.tblFrameQuality->horizontalHeader()->setSortIndicatorShown(true);
        if (g_qualitySortColumn >= 0)
        {
            ui.tblFrameQuality->horizontalHeader()->setSortIndicator(g_qualitySortColumn, g_qualitySortOrder);
        }
        QObject::connect(ui.tblFrameQuality->horizontalHeader(), &QHeaderView::sortIndicatorChanged, q,
                         [](int column, Qt::SortOrder order) {
                             g_qualitySortColumn = column;
                             g_qualitySortOrder = order;
                         });

        ui.tblFrameQuality->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(ui.tblFrameQuality, &QWidget::customContextMenuRequested,
                         [this](const QPoint& pos) { showFrameContextMenu(pos); });

        if (auto* boxLayout = qobject_cast<QBoxLayout*>(ui.tblFrameQuality->parentWidget()->layout()))
        {
            boxLayout->setStretchFactor(ui.tblFrameQuality, 1);
        }
    }
}

void RegistrationTab::Impl::configureLayoutSizing()
{
    // The tab uses a top-level QVBoxLayout with two group boxes:
    // - Registration Groups
    // - Frame Quality
    // We want the groups area to remain relatively compact, and the frame quality area
    // (table + chart) to take most of the available vertical space.
    if (auto* layout = qobject_cast<QVBoxLayout*>(q->layout()))
    {
        // Item 0 = grpRegistrationGroups, item 1 = grpFrameQuality
        layout->setStretch(0, 0);
        layout->setStretch(1, 1);
    }

    if (ui.grpRegistrationGroups)
    {
        ui.grpRegistrationGroups->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    }
    if (ui.grpFrameQuality)
    {
        ui.grpFrameQuality->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }

    QFont base_font = an::ui::tokens().typography.body;
    // Explicitly set weight and size into the resolve mask.  Without this,
    // child widgets inherit the QGroupBox's enlarged bold title font because
    // Qt's font cascade only overrides properties that are explicitly marked
    // in the resolve mask — and q->font() leaves them unresolved when no
    // explicit setFont() was ever called on the tab widget itself.
    base_font.setWeight(base_font.weight());
    if (base_font.pointSizeF() > 0.0)
    {
        base_font.setPointSizeF(base_font.pointSizeF());
    }
    QFont title_font = an::ui::tokens().typography.heading3;
    title_font.setWeight(QFont::DemiBold);

    const auto apply_groupbox_title_font = [&base_font, &title_font](QGroupBox* box) {
        if (!box)
        {
            return;
        }

        box->setFont(title_font);
        const auto direct_children = box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : direct_children)
        {
            if (child)
            {
                child->setFont(base_font);
            }
        }
    };

    // apply_groupbox_title_font is not needed for AnCardWidget
    // apply_groupbox_title_font is not needed for AnCardWidget
    // apply_groupbox_title_font is not needed for AnCardWidget

    if (ui.btnRunRegistration)
    {
        ui.btnRunRegistration->setMinimumHeight(32);
    }
}

void RegistrationTab::Impl::setupChartWidget()
{
    chartWidget = new QualityDistributionWidget(q);
    chartWidget->setObjectName("qualityDistributionChart");
    chartWidget->setMinimumHeight(260);
    chartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Add to layout below frame quality table.
    // We try to find the layout of the container holding tblFrameQuality.
    if (ui.tblFrameQuality && ui.tblFrameQuality->parentWidget() && ui.tblFrameQuality->parentWidget()->layout())
    {
        if (auto* boxLayout = qobject_cast<QBoxLayout*>(ui.tblFrameQuality->parentWidget()->layout()))
        {
            boxLayout->addSpacing(10);
            boxLayout->addWidget(chartWidget);
        }
        else
        {
            ui.tblFrameQuality->parentWidget()->layout()->addWidget(chartWidget);
        }
    }

    if (ui.tblFrameQuality && ui.tblFrameQuality->parentWidget())
    {
        if (auto* boxLayout = qobject_cast<QBoxLayout*>(ui.tblFrameQuality->parentWidget()->layout()))
        {
            boxLayout->setStretchFactor(ui.tblFrameQuality, 1);
            boxLayout->setStretchFactor(chartWidget, 0);
        }
    }

    // Connect signals
    QObject::connect(chartWidget, &QualityDistributionWidget::frameClicked, [this](const QString& frameId) {
        if (!ui.tblFrameQuality)
            return;

        // Find row with this frameId
        for (int i = 0; i < ui.tblFrameQuality->rowCount(); ++i)
        {
            auto* item = ui.tblFrameQuality->item(i, kFrameCol);
            if (item && item->data(Qt::UserRole).toString() == frameId)
            {
                ui.tblFrameQuality->selectRow(i);
                ui.tblFrameQuality->scrollToItem(item);
                break;
            }
        }
    });

    QObject::connect(chartWidget, &QualityDistributionWidget::frameDoubleClicked, [this](const QString& frameId) {
        auto* dialog = new BlinkComparatorDialog({frameId}, q);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        const QPointer<BlinkComparatorDialog> dialogPtr(dialog);
        QObject::connect(dialog, &QDialog::finished, q, [this, dialogPtr]() {
            if (!dialogPtr)
            {
                return;
            }
            // false = no exclusions to apply (not an error); changes are undo-aware.
            (void)ImageIntegration::blink::applyExclusions(model, undoStack, FrameType::Light,
                                                           dialogPtr->comparisonResult().excludedPaths);
        });
        dialog->open();
    });

    QObject::connect(chartWidget, &QualityDistributionWidget::frameContextMenuRequested,
                     [this](const QString& frameId, const QPoint& globalPos) {
                         // Find row and select it
                         if (ui.tblFrameQuality)
                         {
                             for (int i = 0; i < ui.tblFrameQuality->rowCount(); ++i)
                             {
                                 auto* item = ui.tblFrameQuality->item(i, kFrameCol);
                                 if (item && item->data(Qt::UserRole).toString() == frameId)
                                 {
                                     ui.tblFrameQuality->selectRow(i);
                                     break;
                                 }
                             }
                             showFrameContextMenu(ui.tblFrameQuality->mapFromGlobal(globalPos));
                         }
                     });

    // Sync selection from table to chart
    if (ui.tblFrameQuality)
    {
        QObject::connect(ui.tblFrameQuality->selectionModel(), &QItemSelectionModel::selectionChanged, [this]() {
            const auto selected = ui.tblFrameQuality->selectionModel()->selectedRows();
            if (selected.size() == 1)
            {
                int row = selected.first().row();
                auto* item = ui.tblFrameQuality->item(row, kFrameCol);
                if (item)
                {
                    chartWidget->setSelectedFrameId(item->data(Qt::UserRole).toString());
                }
            }
            else
            {
                chartWidget->clearSelection();
            }
        });
    }

    updateChartThresholds();
}

void RegistrationTab::Impl::setupDebounceTimer()
{
    debounceTimer = new QTimer(q);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(200);
    QObject::connect(debounceTimer, &QTimer::timeout, [this]() {
        if (pendingMetricsDbReset)
        {
            pendingMetricsDbReset = false;
            closeMetricsDb();
        }
        updateAll();
    });
}

void RegistrationTab::Impl::setupConnections()
{
    QObject::connect(model, &IntegrationDataModel::framesAdded, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::framesRemoved, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::frameUpdated, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::groupingChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::calibrationChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::registrationChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::integrationGroupsChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::settingsChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::outputDirectoryChanged, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::modelReset, q, &RegistrationTab::refreshAll);
    QObject::connect(model, &IntegrationDataModel::selectionChanged, q, [this]() { onModelSelectionChanged(); });

    if (ui.tblRegistrationGroups)
    {
        QObject::connect(ui.tblRegistrationGroups, &QTableWidget::itemSelectionChanged, [this]() {
            if (ignoreSelectionChange)
                return;

            const RegistrationGroupEntry* group = selectedGroup();
            if (group)
            {
                GroupKey machineKey = RegistrationGroupEngine::toMachineGroupKey(group->key);
                model->setSelectedRegistrationGroupKey(machineKey);
            }
            else
            {
                model->setSelectedRegistrationGroupKey(std::nullopt);
            }

            updateFrameTable();
            updateActionButtons();
        });

        // Double-click on row opens settings dialog
        QObject::connect(ui.tblRegistrationGroups, &QTableWidget::itemDoubleClicked,
                         [this](QTableWidgetItem*) { openSettingsDialogForSelectedGroup(); });

        // Enter key opens settings dialog
        auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), ui.tblRegistrationGroups, nullptr, nullptr,
                                    Qt::WidgetWithChildrenShortcut);
        auto* returnKey = new QShortcut(QKeySequence(Qt::Key_Enter), ui.tblRegistrationGroups, nullptr, nullptr,
                                        Qt::WidgetWithChildrenShortcut);

        const auto openSettings = [this]() {
            if (ui.tblRegistrationGroups->hasFocus())
                openSettingsDialogForSelectedGroup();
        };
        QObject::connect(enter, &QShortcut::activated, openSettings);
        QObject::connect(returnKey, &QShortcut::activated, openSettings);
    }

    if (ui.tblFrameQuality)
    {
        QObject::connect(ui.tblFrameQuality, &QTableWidget::itemSelectionChanged, [this]() { updateActionButtons(); });

        // Delete key excludes selected frames
        auto* del =
            new QShortcut(QKeySequence::Delete, ui.tblFrameQuality, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
        QObject::connect(del, &QShortcut::activated, [this]() {
            if (ui.tblFrameQuality->hasFocus() && ui.btnExcludeSelected->isEnabled())
            {
                ui.btnExcludeSelected->click();
            }
        });
    }

    if (ui.btnSetReference)
    {
        QObject::connect(ui.btnSetReference, &QPushButton::clicked, [this]() {
            const RegistrationGroupEntry* group = selectedGroup();
            const auto selected_frame = selectedSingleFrameId();
            if (!group || !selected_frame.has_value())
            {
                return;
            }
            const auto oldReference = model->registrationReferenceFrame(group->key);
            if (undoStack)
            {
                undoStack->push(new SetRegistrationReferenceCommand(model, group->key, oldReference, selected_frame));
            }
            else
            {
                model->setRegistrationReferenceFrame(group->key, selected_frame);
            }
            updateGroupTable();
            updateActionButtons();
        });
    }

    if (ui.btnExcludeSelected)
    {
        QObject::connect(ui.btnExcludeSelected, &QPushButton::clicked, [this]() { excludeSelectedFrames(); });
    }

    if (ui.btnIncludeSelected)
    {
        QObject::connect(ui.btnIncludeSelected, &QPushButton::clicked, [this]() { includeSelectedFrames(); });
    }

    if (btnRunRegistrationGroup)
    {
        QObject::connect(btnRunRegistrationGroup, &QPushButton::clicked, [this]() {
            const RegistrationGroupEntry* group = selectedGroup();
            if (!group)
            {
                return;
            }

            const QString machine_key = RegistrationGroupEngine::toMachineGroupKey(group->key).toString();
            emit q->runRegistrationRequested(machine_key);
            LOG_INFO("RegistrationTab: runRegistrationRequested emitted (group='{}')", machine_key.toStdString());
        });
    }

    if (ui.btnRunRegistration)
    {
        QObject::connect(ui.btnRunRegistration, &QPushButton::clicked, [this]() {
            emit q->runRegistrationAllRequested();
            LOG_INFO("RegistrationTab: runRegistrationAllRequested emitted");
        });
    }

    if (btnClearRegistrationGroup)
    {
        QObject::connect(btnClearRegistrationGroup, &QPushButton::clicked,
                         [this]() { clearRegistrationForSelectedGroup(); });
    }

    if (ui.btnGroupSettings)
    {
        QObject::connect(ui.btnGroupSettings, &QPushButton::clicked,
                         [this]() { openSettingsDialogForSelectedGroup(); });
    }

    if (ui.btnApplyThresholds)
    {
        QObject::connect(ui.btnApplyThresholds, &QPushButton::clicked, [this]() { applyQualityThresholds(); });
    }

    if (ui.btnDetectOutliers)
    {
        QObject::connect(ui.btnDetectOutliers, &QPushButton::clicked, [this]() { detectOutliers(); });
    }

    if (ui.btnResetExcluded)
    {
        QObject::connect(ui.btnResetExcluded, &QPushButton::clicked, [this]() { resetExcludedFrames(); });
    }

    // Live quality filter preview and chart threshold overlays
    auto refreshQualityFilters = [this]() {
        syncThresholdEditorStates();
        updateThresholdPreview();
        updateChartThresholds();
    };

    const auto connectCheckBox = [refreshQualityFilters](QCheckBox* checkBox) {
        if (checkBox)
        {
            QObject::connect(checkBox, &QCheckBox::toggled, refreshQualityFilters);
        }
    };
    connectCheckBox(ui.chkMinScore);
    connectCheckBox(ui.chkMinStars);
    connectCheckBox(ui.chkMinSnr);
    connectCheckBox(ui.chkMaxFwhm);
    connectCheckBox(ui.chkMaxEccentricity);

    if (ui.spinMinScore)
    {
        QObject::connect(ui.spinMinScore, QOverload<double>::of(&QDoubleSpinBox::valueChanged), refreshQualityFilters);
    }
    if (ui.spinMinStars)
    {
        QObject::connect(ui.spinMinStars, QOverload<int>::of(&QSpinBox::valueChanged), refreshQualityFilters);
    }
    if (ui.spinMinSnr)
    {
        QObject::connect(ui.spinMinSnr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), refreshQualityFilters);
    }
    if (ui.spinMaxFwhm)
    {
        QObject::connect(ui.spinMaxFwhm, QOverload<double>::of(&QDoubleSpinBox::valueChanged), refreshQualityFilters);
    }
    if (ui.spinMaxEccentricity)
    {
        QObject::connect(ui.spinMaxEccentricity, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         refreshQualityFilters);
    }

    refreshQualityFilters();
}

void RegistrationTab::Impl::onModelSelectionChanged()
{
    const auto key_opt = model->selectedRegistrationGroupKey();
    if (!key_opt.has_value())
    {
        if (ui.tblRegistrationGroups)
        {
            ignoreSelectionChange = true;
            ui.tblRegistrationGroups->clearSelection();
            ignoreSelectionChange = false;
            updateFrameTable();
            updateActionButtons();
        }
        return;
    }

    const GroupKey targetKey = *key_opt;
    if (ui.tblRegistrationGroups)
    {
        ignoreSelectionChange = true;
        for (int i = 0; i < ui.tblRegistrationGroups->rowCount(); ++i)
        {
            if (ui.tblRegistrationGroups->item(i, 0)->data(Qt::UserRole).toString() == targetKey.toString())
            {
                ui.tblRegistrationGroups->selectRow(i);
                break;
            }
        }
        ignoreSelectionChange = false;
        updateFrameTable();
        updateActionButtons();
    }
}

RegistrationTab::RegistrationTab(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model, undoStack))
{
}

RegistrationTab::~RegistrationTab()
{
    // Ensure queued/in-flight callbacks cannot reach this object while it is tearing down.
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->debounceTimer)
    {
        m_impl->debounceTimer->stop();
    }
}

void RegistrationTab::refreshAll()
{
    if (!m_impl)
    {
        return;
    }

    // Close the cached database connection on the next refresh to ensure fresh data is read.
    // This avoids repeated close/open cycles when many frame updates arrive in a burst.
    m_impl->invalidateRegistrationAnalysisManifest();
    m_impl->pendingMetricsDbReset = true;
    m_impl->scheduleRefresh();
}

void RegistrationTab::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event || !m_impl)
    {
        return;
    }

    const auto type = event->type();
    if (type == QEvent::ThemeChange)
    {
        m_impl->applyThemeStyling();
        m_impl->scheduleRefresh();
    }
}

void RegistrationTab::showFrameInTable(const QString& framePath)
{
    if (!m_impl || !m_impl->model)
    {
        return;
    }

    const FrameId id(framePath);
    if (!id.isValid())
    {
        return;
    }

    const QString canonical = id.canonicalPath();
    const auto frameOpt = m_impl->model->frames(FrameType::Light).findById(FrameId(canonical));
    if (!frameOpt.has_value())
    {
        return;
    }

    const RegistrationGroupKey regKey = RegistrationGroupKey::fromFrame(*frameOpt);
    const GroupKey machineKey = RegistrationGroupEngine::toMachineGroupKey(regKey);

    m_impl->model->setSelectedRegistrationGroupKey(machineKey);
    m_impl->model->setSelectedFrameIds({FrameId(canonical)});

    // Update now (avoid debounce) so the row exists.
    m_impl->updateAll();

    QTimer::singleShot(0, this, [this, canonical]() {
        if (m_impl)
        {
            m_impl->selectFrameInQualityTable(canonical);
        }
    });
}

void RegistrationTab::sortFrameQualityByColumn(int column, Qt::SortOrder order)
{
    if (!m_impl || !m_impl->ui.tblFrameQuality)
    {
        return;
    }

    g_qualitySortColumn = column;
    g_qualitySortOrder = order;
    m_impl->ui.tblFrameQuality->sortItems(column, order);
    m_impl->ui.tblFrameQuality->horizontalHeader()->setSortIndicator(column, order);
}

int RegistrationTab::frameQualitySortIndicatorColumn() const
{
    if (!m_impl || !m_impl->ui.tblFrameQuality)
    {
        return -1;
    }
    return m_impl->ui.tblFrameQuality->horizontalHeader()->sortIndicatorSection();
}

Qt::SortOrder RegistrationTab::frameQualitySortIndicatorOrder() const
{
    if (!m_impl || !m_impl->ui.tblFrameQuality)
    {
        return Qt::AscendingOrder;
    }
    return m_impl->ui.tblFrameQuality->horizontalHeader()->sortIndicatorOrder();
}
