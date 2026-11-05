// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameQualityDashboard.h"
#include "app/UiStyleTokens.h"
#include <AfterNightPlatform/Logging.h>
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/FilterStatistics.h"
#include "processes/ImageIntegration/model/FrameQuality.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/core/BlinkComparatorExclusionUtils.h"
#include "processes/ImageIntegration/core/PhotometricWeightingUiUtils.h"
#include "processes/ImageIntegration/tabs/TabStyling.h"
#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"

#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QCoreApplication>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QTabWidget>
#include <QTimer>
#include <QStringList>
#include <QTableWidget>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(FrameQualityDashboard)

#include "processes/ImageIntegration/core/MathUtils.h"
#include "processes/ImageIntegration/ui_models/GroupTreeModel.h"
#include "processes/ImageIntegration/widgets/FrameTreeView.h"
#include "theme/AnDesignTokens.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboardColumns.h"
#include "processes/ImageIntegration/widgets/WeightHistogramWidget.h"

using ImageIntegration::clamp01;

namespace
{
// Persistent sort state for this dashboard type
static int g_dashboardSortColumn = -1;
static Qt::SortOrder g_dashboardSortOrder = Qt::AscendingOrder;

[[nodiscard]] QColor opaqueDashboardWindowColor()
{
    const auto& tokenSet = an::ui::tokens();
    if (tokenSet.materials.micaSurface.alpha() >= 255)
    {
        return tokenSet.materials.micaSurface;
    }

    return an::ui::compositeOver(tokenSet.materials.micaSurface, tokenSet.colors.bgPrimary);
}

void applyOpaqueDashboardPalette(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    const auto& tokenSet = an::ui::tokens();
    QPalette pal = widget->palette();
    pal.setColor(QPalette::Window, opaqueDashboardWindowColor());
    pal.setColor(QPalette::Base, tokenSet.colors.bgInput);
    widget->setPalette(pal);
    widget->setAutoFillBackground(true);
    widget->setAttribute(Qt::WA_StyledBackground, true);
}

void configureQualityTableSelection(QTableWidget* table)
{
    if (!table)
    {
        return;
    }

    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
}
} // namespace

FrameQualityDashboard::FrameQualityDashboard(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_model(model), m_undoStack(undoStack)
{
    // Ensure the background is painted. Without this, the space between child widgets can show
    // "stale" pixels from previously painted widgets (appearing as overlap artifacts) when the
    // dashboard is resized or re-laid out.
    applyOpaqueDashboardPalette(this);
    setAttribute(Qt::WA_StaticContents, false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    auto* layout = new QVBoxLayout(this);

    m_helpLabel = new QLabel(
        tr("Frame Quality (per frame): use this to understand which frames are outliers and to preview weights. "
           "This is a preview tool and does not change stacking settings by itself."),
        this);
    m_helpLabel->setWordWrap(true);
    layout->addWidget(m_helpLabel);

    // Banner warning for missing metrics (kept as a normal layout widget so it behaves consistently
    // with style + accessibility and doesn't overlay table content).
    m_banner = new QLabel(this);
    m_banner->setVisible(false);
    m_banner->setWordWrap(true);

    // Theme-aware warning colors
    const QColor warnBg =
        an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::WarningBackground);
    const QColor warnFg = an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::WarningText);
    const QColor warnBorder =
        an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::WarningBorder);

    m_banner->setFrameShape(QFrame::StyledPanel);
    {
        QPalette p = m_banner->palette();
        p.setColor(QPalette::Window, warnBg);
        p.setColor(QPalette::WindowText, warnFg);
        p.setColor(QPalette::Mid, warnBorder);
        m_banner->setPalette(p);
    }
    m_banner->setContentsMargins(6, 6, 6, 6);
    m_banner->setProperty("anFrameRadius", 4);
    m_banner->setText(tr("Frame quality metrics are not available. Run Registration first to compute them."));
    layout->addWidget(m_banner);

    m_weightingBanner = new QLabel(this);
    m_weightingBanner->setObjectName("weightingPreviewBanner");
    m_weightingBanner->setVisible(false);
    m_weightingBanner->setWordWrap(true);

    // Theme-aware info colors
    const QColor infoBg = an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::InfoBackground);
    const QColor infoFg = an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::InfoText);
    const QColor infoBorder = an::ui::styling::semanticColor(palette(), an::ui::styling::SemanticColorRole::InfoBorder);

    m_weightingBanner->setFrameShape(QFrame::StyledPanel);
    {
        QPalette p = m_weightingBanner->palette();
        p.setColor(QPalette::Window, infoBg);
        p.setColor(QPalette::WindowText, infoFg);
        p.setColor(QPalette::Mid, infoBorder);
        m_weightingBanner->setPalette(p);
    }
    m_weightingBanner->setContentsMargins(6, 6, 6, 6);
    m_weightingBanner->setProperty("anFrameRadius", 4);
    m_weightingBanner->setText(
        tr("Weighting is disabled for this integration group (Stacking Settings → Weighting: None)."));

    m_table = new QTableWidget(this);
    m_table->setObjectName("qualityTable");
    // Allow the parent tab/window to shrink smaller than the table's default minimumSizeHint()
    // (QAbstractScrollArea defaults can otherwise force a large min-height).
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    // Keep this low; a scroll area around the dashboard handles very small window sizes.
    m_table->setMinimumHeight(0);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({"Frame", "Score", "Stars", "FWHM", "SNR", "Eccentricity", "Weight"});

    // Apply standard styling first
    ImageIntegration::Style::applyTableStyle(m_table);
    configureQualityTableSelection(m_table);

    // Customize column sizing: stretch Frame (col 0), fit Weight (col 6) to contents
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(kFrameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kWeightColumn, QHeaderView::ResizeToContents);

    // Re-enable editing triggers (applyTableStyle disables them)
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    // Help avoid rendering trails during interactive resizing by ensuring the scroll area
    // and its viewport are opaque and paint their backgrounds.
    m_table->setAutoFillBackground(true);
    m_table->setAttribute(Qt::WA_StyledBackground, true);
    m_table->setAttribute(Qt::WA_StaticContents, false);
    m_table->installEventFilter(this);
    if (auto* vp = m_table->viewport())
    {
        vp->setAutoFillBackground(true);
        vp->setAttribute(Qt::WA_StyledBackground, true);
        vp->setAttribute(Qt::WA_StaticContents, false);
        vp->installEventFilter(this);
    }

    for (int i = 0; i < m_table->columnCount(); ++i)
    {
        if (auto* header = m_table->horizontalHeaderItem(i))
        {
            switch (i)
            {
            case 0:
                header->setToolTip(tr("Frame file name / path."));
                break;
            case 1:
                header->setToolTip(tr("Overall quality score (0-100) computed from all metrics.\n\n"
                                      "When filter statistics are available (≥2 frames per filter),\n"
                                      "the score is computed using z-scores normalized to each filter's\n"
                                      "baseline, making scores comparable across different filters.\n\n"
                                      "Higher scores indicate better quality frames."));
                break;
            case 2:
                header->setToolTip(tr("Number of detected stars in the frame.\n\n"
                                      "More stars generally indicate better quality and\n"
                                      "enable more accurate registration.\n\n"
                                      "Hover over individual cells to see filter-relative statistics."));
                break;
            case 3:
                header->setToolTip(tr("Full-Width Half-Maximum of stellar profiles (pixels).\n\n"
                                      "Lower values indicate sharper, better-focused frames.\n"
                                      "Values vary significantly between filters due to atmospheric\n"
                                      "dispersion and optical characteristics.\n\n"
                                      "Hover over individual cells to see filter-relative statistics."));
                break;
            case 4:
                header->setToolTip(tr("Signal-to-Noise Ratio computed from star profiles.\n\n"
                                      "Higher values indicate better quality with less noise.\n"
                                      "SNR varies between filters due to different exposure levels\n"
                                      "and target brightness in each wavelength.\n\n"
                                      "Hover over individual cells to see filter-relative statistics."));
                break;
            case 5:
                header->setToolTip(tr("Star eccentricity (0 = perfectly round, 1 = elongated).\n\n"
                                      "Lower values indicate rounder stars, which typically means\n"
                                      "better tracking and guiding. Values above ~0.4 may indicate\n"
                                      "tracking issues or wind shake.\n\n"
                                      "Hover over individual cells to see filter-relative statistics."));
                break;
            case 6:
                header->setToolTip(tr("Per-frame contribution factor (0..1) used for weighting preview."));
                break;
            }
        }
    }

    // Enable sorting
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSortIndicatorShown(true);
    if (g_dashboardSortColumn >= 0)
    {
        m_table->horizontalHeader()->setSortIndicator(g_dashboardSortColumn, g_dashboardSortOrder);
    }
    connect(m_table->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [](int column, Qt::SortOrder order) {
        g_dashboardSortColumn = column;
        g_dashboardSortOrder = order;
    });

    // Ensure a few rows are visible by default (without forcing the overall window
    // to grow, since the parent tab wraps this dashboard in a scroll area).
    if (m_table->verticalHeader() && m_table->horizontalHeader())
    {
        const int row_h = m_table->verticalHeader()->defaultSectionSize();
        const int header_h = m_table->horizontalHeader()->sizeHint().height();
        constexpr int min_rows = 4;
        m_table->setMinimumHeight(header_h + (row_h * min_rows) + 8);
    }

    layout->addWidget(m_table);
    layout->setStretchFactor(m_table, 1);

    // Outlier analysis controls (explicitly grouped so "sigma" doesn't look like a weighting setting)
    m_outlierHelpLabel = new QLabel(
        tr("Outlier analysis: Adjust sensitivity, then click \"Detect Outliers\" to mark unusually low-scoring frames. "
           "This does not change weights by itself."),
        this);
    m_outlierHelpLabel->setObjectName("outlierHelpLabel");
    m_outlierHelpLabel->setWordWrap(true);

    QColor secondaryText = palette().color(QPalette::Disabled, QPalette::Text);
    {
        QPalette p = m_outlierHelpLabel->palette();
        p.setColor(QPalette::WindowText, secondaryText);
        m_outlierHelpLabel->setPalette(p);
    }

    auto* outlierControlsLayout = new QHBoxLayout();
    auto* sigmaLabel = new QLabel(tr("Outlier sensitivity (σ):"), this);
    m_sigmaSpinBox = new QDoubleSpinBox(this);
    m_sigmaSpinBox->setObjectName("sigmaSpinBox");
    m_sigmaSpinBox->setRange(1.0, 5.0);
    m_sigmaSpinBox->setValue(3.0);
    m_sigmaSpinBox->setSingleStep(0.5);
    m_sigmaSpinBox->setDecimals(1);
    m_sigmaSpinBox->setToolTip(tr("Used only for Detect Outliers (preview).\n\n"
                                  "A frame is flagged when: score < mean - (σ × stddev).\n\n"
                                  "Examples:\n"
                                  "• 2.0σ = Aggressive (catches more outliers, may include good frames)\n"
                                  "• 3.0σ = Balanced (recommended default)\n"
                                  "• 4.0σ = Conservative (only extreme outliers)"));

    m_detectButton = new QPushButton(tr("Analyze → Detect Outliers"), this);
    m_detectButton->setObjectName("detectOutliersButton");
    m_detectButton->setToolTip(tr("Analyze the frame quality scores and flag statistical outliers (preview)."));

    m_excludeButton = new QPushButton(tr("Exclude Selected Frames"), this);
    m_excludeButton->setObjectName("excludeSelectedButton");
    m_excludeButton->setToolTip(
        tr("Mark the selected frame(s) as excluded. Excluded frames are ignored by registration and stacking."));

    m_includeButton = new QPushButton(tr("Include Selected in Stack"), this);
    m_includeButton->setObjectName("includeSelectedInStackButton");
    m_includeButton->setToolTip(tr("Restore selected frame(s) to the current stack."));
    m_includeButton->setVisible(false);

    outlierControlsLayout->addWidget(sigmaLabel);
    outlierControlsLayout->addWidget(m_sigmaSpinBox);
    outlierControlsLayout->addSpacing(12);
    outlierControlsLayout->addWidget(m_detectButton);
    outlierControlsLayout->addWidget(m_excludeButton);
    outlierControlsLayout->addWidget(m_includeButton);
    outlierControlsLayout->addStretch();
    layout->addLayout(outlierControlsLayout);
    layout->addWidget(m_outlierHelpLabel);
    layout->addSpacing(8);

    auto* histogramTitle = new QLabel(tr("Weights Histogram"), this);
    histogramTitle->setObjectName("histogramTitleLabel");
    histogramTitle->setAlignment(Qt::AlignCenter);
    {
        QPalette p = histogramTitle->palette();
        p.setColor(QPalette::WindowText, secondaryText);
        histogramTitle->setPalette(p);
        histogramTitle->setFont(an::ui::tokens().typography.heading3);
    }
    layout->addWidget(histogramTitle);

    m_weightHistogram = new WeightHistogramWidget(this);
    m_weightHistogram->setObjectName("weightHistogram");
    m_weightHistogram->setVisible(true);
    // Keep this preview visible by default (otherwise it can collapse to 0px when the table takes
    // most of the available height), while still being small enough to not force a tall window.
    m_weightHistogram->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_weightHistogram->setMinimumHeight(120);
    layout->addWidget(m_weightHistogram);

    // Keep the disabled-weighting hint close to the histogram preview.
    layout->addWidget(m_weightingBanner);

    m_effectiveExposureLabel = new QLabel(tr("Effective exposure: -"), this);
    m_effectiveExposureLabel->setObjectName("effectiveExposureLabel");
    m_effectiveExposureLabel->setToolTip(tr("Effective exposure based on preview weights (for comparison only)."));
    layout->addWidget(m_effectiveExposureLabel);

    connect(m_model, &IntegrationDataModel::framesAdded, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_model, &IntegrationDataModel::framesRemoved, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_model, &IntegrationDataModel::modelReset, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_model, &IntegrationDataModel::selectionChanged, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_model, &IntegrationDataModel::stackExclusionsChanged, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_model, &IntegrationDataModel::settingsChanged, this, &FrameQualityDashboard::onFramesAdded);
    connect(m_detectButton, &QPushButton::clicked, this, &FrameQualityDashboard::onDetectOutliersClicked);
    connect(m_excludeButton, &QPushButton::clicked, this, &FrameQualityDashboard::onExcludeSelectedClicked);
    connect(m_includeButton, &QPushButton::clicked, this, &FrameQualityDashboard::onIncludeSelectedClicked);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (!m_weightHistogram)
            return;
        auto selected = m_table->selectionModel()->selectedRows();
        double weight = std::numeric_limits<double>::quiet_NaN();
        if (selected.size() == 1)
        {
            weight = weightFor(selected.first().row());
        }
        static_cast<WeightHistogramWidget*>(m_weightHistogram)->setSelectedFrameWeight(weight);
    });

    // Context menu for quick actions.
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (!m_table || !m_model)
        {
            return;
        }

        // If the user right-clicked a row that isn't selected, select it first.
        const QModelIndex clicked = m_table->indexAt(pos);
        if (clicked.isValid())
        {
            const int row = clicked.row();
            bool rowAlreadySelected = false;
            for (const auto& idx : m_table->selectionModel()->selectedRows())
            {
                if (idx.row() == row)
                {
                    rowAlreadySelected = true;
                    break;
                }
            }
            if (!rowAlreadySelected)
            {
                m_table->selectRow(row);
            }
        }

        const auto selectedRows = m_table->selectionModel()->selectedRows();
        const bool hasSelection = !selectedRows.isEmpty();
        const bool hasSingleSelection = selectedRows.size() == 1;

        QMenu menu(m_table);

        QAction* showInLightsTreeAction = menu.addAction(tr("Show in Lights Tree"));
        showInLightsTreeAction->setEnabled(hasSingleSelection);
        connect(showInLightsTreeAction, &QAction::triggered, this, [this]() {
            const auto selected = m_table->selectionModel()->selectedRows();
            if (selected.size() != 1)
            {
                return;
            }
            const int row = selected.first().row();
            auto* item = m_table->item(row, kFrameColumn);
            if (!item)
            {
                return;
            }
            const QString path = item->data(Qt::UserRole).toString();
            if (path.trimmed().isEmpty())
            {
                return;
            }

            showInLightsTree(path);
        });

        QAction* excludeSelectedAction =
            menu.addAction(isStackExclusionMode() ? tr("Exclude from This Stack") : tr("Exclude Selected Frames"));
        excludeSelectedAction->setEnabled(hasSelection);
        connect(excludeSelectedAction, &QAction::triggered, this, &FrameQualityDashboard::onExcludeSelectedClicked);

        QAction* includeSelectedAction = nullptr;
        if (isStackExclusionMode())
        {
            includeSelectedAction = menu.addAction(tr("Include in This Stack"));
            includeSelectedAction->setEnabled(hasSelection);
            connect(includeSelectedAction, &QAction::triggered, this, &FrameQualityDashboard::onIncludeSelectedClicked);
        }

        menu.addSeparator();

        QAction* previewBlinkAction = menu.addAction(tr("Preview in Blink Comparator..."));
        previewBlinkAction->setEnabled(hasSelection);
        connect(previewBlinkAction, &QAction::triggered, this, [this]() {
            if (!m_table)
            {
                return;
            }

            const auto selectedRows = m_table->selectionModel()->selectedRows();
            if (selectedRows.isEmpty())
            {
                return;
            }

            QStringList paths;
            paths.reserve(selectedRows.size());
            for (const auto& idx : selectedRows)
            {
                auto* item = m_table->item(idx.row(), kFrameColumn);
                if (!item)
                {
                    continue;
                }
                const QString canonical = item->data(Qt::UserRole).toString().trimmed();
                if (!canonical.isEmpty())
                {
                    paths.append(canonical);
                }
            }

            paths.removeDuplicates();
            if (paths.isEmpty())
            {
                return;
            }

            auto* dialog = new BlinkComparatorDialog(paths, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            const QPointer<BlinkComparatorDialog> dialogPtr(dialog);
            connect(dialog, &QDialog::finished, this, [this, dialogPtr]() {
                if (!dialogPtr)
                {
                    return;
                }
                // false = no exclusions to apply (not an error); changes are undo-aware.
                (void)ImageIntegration::blink::applyExclusions(m_model, m_undoStack, FrameType::Light,
                                                               dialogPtr->comparisonResult().excludedPaths);
            });
            dialog->open();
        });

        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });

    connect(m_table, &QTableWidget::itemChanged, this, &FrameQualityDashboard::onWeightItemChanged);

    updateExclusionUiText();
    applyThemeStyling();
}

FrameQualityDashboard::~FrameQualityDashboard()
{
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
}

void FrameQualityDashboard::applyThemeStyling()
{
    if (m_applyingThemeStyling)
    {
        return;
    }
    m_applyingThemeStyling = true;

    applyOpaqueDashboardPalette(this);

    if (m_table)
    {
        ImageIntegration::Style::applyTableStyle(m_table);
        configureQualityTableSelection(m_table);
        m_table->horizontalHeader()->setStretchLastSection(false);
        m_table->horizontalHeader()->setSectionResizeMode(kFrameColumn, QHeaderView::Stretch);
        m_table->horizontalHeader()->setSectionResizeMode(kWeightColumn, QHeaderView::ResizeToContents);
        m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    }

    const QColor secondaryText = palette().color(QPalette::Disabled, QPalette::Text);
    if (auto* outlierHelp = findChild<QLabel*>(QStringLiteral("outlierHelpLabel")))
    {
        QPalette p = outlierHelp->palette();
        p.setColor(QPalette::WindowText, secondaryText);
        outlierHelp->setPalette(p);
    }
    if (auto* histogramTitle = findChild<QLabel*>(QStringLiteral("histogramTitleLabel")))
    {
        QPalette p = histogramTitle->palette();
        p.setColor(QPalette::WindowText, secondaryText);
        histogramTitle->setPalette(p);
    }

    if (m_banner)
    {
        QColor warnBg = palette().color(QPalette::Highlight);
        warnBg.setAlpha(52);
        QColor warnBorder = palette().color(QPalette::Highlight);
        warnBorder.setAlpha(146);

        QPalette p = m_banner->palette();
        p.setColor(QPalette::Window, warnBg);
        p.setColor(QPalette::WindowText, palette().color(QPalette::WindowText));
        p.setColor(QPalette::Mid, warnBorder);
        m_banner->setPalette(p);
    }

    if (m_weightingBanner)
    {
        QColor infoBg = palette().color(QPalette::Base);
        infoBg.setAlpha(230);
        QColor infoBorder = palette().color(QPalette::Mid);

        QPalette p = m_weightingBanner->palette();
        p.setColor(QPalette::Window, infoBg);
        p.setColor(QPalette::WindowText, palette().color(QPalette::WindowText));
        p.setColor(QPalette::Mid, infoBorder);
        m_weightingBanner->setPalette(p);
    }

    m_applyingThemeStyling = false;
}

void FrameQualityDashboard::paintEvent(QPaintEvent* event)
{
    // Ensure the widget background is always painted.
    // This prevents "stale" pixels when large child widgets (e.g. tables) shrink and other
    // controls move into the newly exposed space during interactive resizing.
    QStyleOption opt;
    opt.initFrom(this);

    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(QPalette::Window));
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);

    QWidget::paintEvent(event);
}

void FrameQualityDashboard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_table && m_table->viewport())
    {
        m_table->viewport()->update();
    }
}

void FrameQualityDashboard::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (auto* l = layout())
    {
        l->activate();
    }
    update();
    if (m_table && m_table->viewport())
    {
        m_table->viewport()->update();
    }

    // Some widget trees (notably with large QAbstractScrollArea children) can show an initial
    // "overlap" until the first interactive resize. Force a second activation after the widget
    // is fully visible so geometry is correct immediately.
    QTimer::singleShot(0, this, [this]() {
        if (auto* l = layout())
        {
            l->activate();
        }
        update();
        if (m_table && m_table->viewport())
        {
            m_table->viewport()->update();
        }
    });
}

bool FrameQualityDashboard::eventFilter(QObject* watched, QEvent* event)
{
    // When a large QAbstractScrollArea child (the table) is moved/resized by the layout (especially
    // during tab show or interactive resizing), Qt may repaint the child widgets but not repaint
    // the newly exposed background of the parent. That leaves old table pixels in the backing
    // store, which looks like widgets "overlapping" until the next full-window repaint.
    if ((watched == m_table || (m_table && watched == m_table->viewport())) && event)
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
            update();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void FrameQualityDashboard::refresh()
{
    // A refresh reads the lights' headers again, in case a file was replaced.
    m_combinedFrameCounts.clear();
    m_combinedFrameCountsFailure.clear();
    ++m_combinedFrameCountsGeneration;
    applyThemeStyling();
    onFramesAdded();
}

void FrameQualityDashboard::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!event)
    {
        return;
    }

    const auto type = event->type();
    if (type == QEvent::ThemeChange)
    {
        applyThemeStyling();
        onFramesAdded();
    }
}

void FrameQualityDashboard::setRegistrationGroupFilteringEnabled(bool enabled)
{
    m_filterByRegistrationGroup = enabled;
    onFramesAdded();
}

void FrameQualityDashboard::setFrameIdFilter(const QStringList& canonicalFrameIds)
{
    m_frameIdFilter.clear();
    m_frameIdFilterActive = true;
    for (const auto& id : canonicalFrameIds)
    {
        const QString trimmed = id.trimmed();
        if (!trimmed.isEmpty())
        {
            m_frameIdFilter.insert(trimmed);
        }
    }
    onFramesAdded();
}

void FrameQualityDashboard::clearFrameIdFilter()
{
    if (!m_frameIdFilterActive && m_frameIdFilter.isEmpty())
    {
        return;
    }
    m_frameIdFilter.clear();
    m_frameIdFilterActive = false;
    onFramesAdded();
}

void FrameQualityDashboard::setExclusionMode(ExclusionMode mode)
{
    if (m_exclusionMode == mode)
    {
        return;
    }

    m_exclusionMode = mode;
    updateExclusionUiText();
    onFramesAdded();
}

void FrameQualityDashboard::setStackExclusionGroupKey(const GroupKey& key)
{
    if (m_stackExclusionGroupKey == key)
    {
        return;
    }

    m_stackExclusionGroupKey = key;
    onFramesAdded();
}

void FrameQualityDashboard::setWeightingPreviewConfig(const WeightingPreviewConfig& config)
{
    m_weightingConfig = config;
    if (m_weightingEnabled)
    {
        calculateWeights();
    }
}

void FrameQualityDashboard::setWeightingEnabled(bool enabled, const QString& disabledReason)
{
    m_weightingEnabled = enabled;
    m_weightingDisabledReason = enabled ? QString() : disabledReason;
    onWeightingToggled(enabled);
}

bool FrameQualityDashboard::weightingEnabled() const
{
    return m_weightingEnabled;
}

bool FrameQualityDashboard::hasColumn(const QString& name) const
{
    if (!m_table)
    {
        return false;
    }
    for (int i = 0; i < m_table->columnCount(); ++i)
    {
        if (m_table->horizontalHeaderItem(i) &&
            m_table->horizontalHeaderItem(i)->text().compare(name, Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

bool FrameQualityDashboard::isStackExclusionMode() const
{
    return m_exclusionMode == ExclusionMode::StackOnly;
}

bool FrameQualityDashboard::isFrameExcludedFromCurrentStack(const FrameInfo& frame) const
{
    if (frame.excluded)
    {
        return true;
    }

    return isStackExclusionMode() && m_model && m_model->isFrameStackExcluded(m_stackExclusionGroupKey, frame.id);
}

QString FrameQualityDashboard::exclusionStateText(const FrameInfo& frame) const
{
    if (frame.excluded)
    {
        return tr("Excluded globally: ignored by registration and stacking.");
    }

    if (isStackExclusionMode() && m_model && m_model->isFrameStackExcluded(m_stackExclusionGroupKey, frame.id))
    {
        return tr("Excluded from this stack: registration and other stacks are unchanged.");
    }

    return {};
}

void FrameQualityDashboard::updateExclusionUiText()
{
    if (m_helpLabel)
    {
        m_helpLabel->setText(
            isStackExclusionMode()
                ? tr("Frame Quality (per frame): use this to review stack contributors, identify outliers, and "
                     "preview weights for the selected integration group.")
                : tr("Frame Quality (per frame): use this to understand which frames are outliers and to preview "
                     "weights. This is a preview tool and does not change stacking settings by itself."));
    }

    if (m_outlierHelpLabel)
    {
        m_outlierHelpLabel->setText(
            isStackExclusionMode()
                ? tr("Outlier analysis: Adjust sensitivity, detect low-scoring frames, then exclude selected frames "
                     "only from this stack.")
                : tr("Outlier analysis: Adjust sensitivity, then click \"Detect Outliers\" to mark unusually "
                     "low-scoring frames. This does not change weights by itself."));
    }

    if (m_excludeButton)
    {
        m_excludeButton->setText(isStackExclusionMode() ? tr("Exclude from This Stack")
                                                        : tr("Exclude Selected Frames"));
        m_excludeButton->setToolTip(
            isStackExclusionMode()
                ? tr("Omit selected frame(s) only from the current integration stack. Registration and other stacks "
                     "keep using them.")
                : tr("Mark the selected frame(s) as excluded. Excluded frames are ignored by registration and "
                     "stacking."));
    }

    if (m_includeButton)
    {
        m_includeButton->setVisible(isStackExclusionMode());
        m_includeButton->setToolTip(
            tr("Restore selected frame(s) to the current stack without changing global exclusion state."));
    }
}

void FrameQualityDashboard::highlightFrame(const QString& canonicalPath)
{
    if (!m_table)
    {
        return;
    }

    const QString canonical = canonicalPath.trimmed();
    if (canonical.isEmpty())
    {
        return;
    }

    const auto flashRow = [this](int row) {
        if (!m_table || row < 0 || row >= m_table->rowCount())
        {
            return;
        }

        auto* keyItem = m_table->item(row, 0);
        if (!keyItem)
        {
            return;
        }

        const QString key = keyItem->data(Qt::UserRole).toString().trimmed();
        if (key.isEmpty())
        {
            return;
        }

        const QColor flashColor =
            an::ui::styling::semanticColor(m_table->palette(), an::ui::styling::SemanticColorRole::HighlightBackground);
        const QColor flashTextColor = an::ui::bestContrastTextColor(
            an::ui::compositeOver(flashColor, m_table->palette().color(QPalette::Base)),
            an::ui::tokens().colors.textOnAccent, QColor(255, 255, 255), QColor(0, 0, 0), 4.5);

        struct CellStyle
        {
            QBrush background;
            QBrush foreground;
            QFont font;
            bool valid = false;
        };

        std::vector<CellStyle> before;
        before.resize(static_cast<size_t>(m_table->columnCount()));

        for (int col = 0; col < m_table->columnCount(); ++col)
        {
            if (auto* item = m_table->item(row, col))
            {
                before[static_cast<size_t>(col)] =
                    CellStyle{item->background(), item->foreground(), item->font(), true};
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
                item->setBackground(QBrush(flashColor));
                item->setForeground(QBrush(flashTextColor));
            }
        }

        QTimer::singleShot(900, this, [this, row, key, before]() {
            if (!m_table || row < 0 || row >= m_table->rowCount())
            {
                return;
            }
            auto* keyItem2 = m_table->item(row, 0);
            if (!keyItem2 || keyItem2->data(Qt::UserRole).toString().trimmed() != key)
            {
                return;
            }
            for (int col = 0; col < m_table->columnCount(); ++col)
            {
                if (auto* item = m_table->item(row, col))
                {
                    const auto& prev = before[static_cast<size_t>(col)];
                    if (!prev.valid)
                    {
                        continue;
                    }
                    item->setBackground(prev.background);
                    item->setForeground(prev.foreground);
                    item->setFont(prev.font);
                }
            }
        });
    };

    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto* item = m_table->item(row, 0);
        if (!item)
        {
            continue;
        }

        if (item->data(Qt::UserRole).toString().trimmed() == canonical)
        {
            m_table->setFocus(Qt::OtherFocusReason);
            m_table->selectRow(row);
            m_table->scrollToItem(item);
            flashRow(row);
            break;
        }
    }
}

void FrameQualityDashboard::showInLightsTree(const QString& canonicalPath)
{
    const QString path = canonicalPath.trimmed();
    if (path.isEmpty())
    {
        return;
    }

    // Navigate to the Lights tab so the selection is visible in the tree.
    // sourceTabWidget indices: Bias=0, Darks=1, Flats=2, Lights=3, ...
    QWidget* top = window();
    auto* rootTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("rootTabWidget")) : nullptr;
    auto* sourceTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("sourceTabWidget")) : nullptr;
    auto* mainTabs = top ? top->findChild<QTabWidget*>(QStringLiteral("mainTabWidget")) : nullptr;
    if (rootTabs)
    {
        rootTabs->setCurrentIndex(0); // Frame Sources
    }

    auto* tabs = sourceTabs ? sourceTabs : mainTabs;
    if (tabs)
    {
        tabs->setCurrentIndex(static_cast<int>(FrameType::Light));
    }

    // Trigger normal selection sync (preview, model state).
    if (m_model)
    {
        m_model->setSelectedFrameIds({FrameId(path)});
    }

    // Ensure the tree actually expands and scrolls to the frame.
    // Selecting an index that is under a collapsed group doesn't make it visible.
    QTimer::singleShot(0, this, [tabs, path]() {
        if (!tabs)
        {
            return;
        }

        QWidget* lightTabWidget = tabs->widget(static_cast<int>(FrameType::Light));
        if (!lightTabWidget)
        {
            return;
        }

        auto* tree = lightTabWidget->findChild<FrameTreeView*>();
        if (!tree || !tree->selectionModel())
        {
            return;
        }

        auto* treeModel = qobject_cast<GroupTreeModel*>(tree->model());
        if (!treeModel)
        {
            return;
        }

        const QModelIndex idx = treeModel->findIndexForPath(path);
        if (!idx.isValid())
        {
            return;
        }

        // Expand ancestors so the row becomes visible.
        for (QModelIndex parent = idx.parent(); parent.isValid(); parent = parent.parent())
        {
            tree->expand(parent);
        }

        tree->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        tree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    });
}
