// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "StackingTab.h"
#include "StackingTabUiBuilder.h"
#include "TabStyling.h"

#include "processes/ImageIntegration/core/FilenameUtils.h"
#include "processes/ImageIntegration/core/ParallelExecutionUtils.h"
#include "processes/ImageIntegration/core/StackingParamsUtils.h"
#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/UndoCommands.h"
#include "processes/ImageIntegration/model/IntegrationGroupEngine.h"
#include "processes/ImageIntegration/model/RegistrationGroupEngine.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"
#include "processes/ImageIntegration/widgets/FrameQualityDashboard.h"
#include "processes/ImageIntegration/workflow/ManifestBuilder.h"
#include "processes/ImageIntegration/workflow/WorkflowTimeEstimator.h"
#include "theme/AnDesignTokens.h"

#include "components/AnCardWidget.h"
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QPalette>
#include <QPointer>
#include <QFrame>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QPushButton>
#include <QShortcut>
#include <QCoreApplication>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(StackingTab)

namespace
{
class NumericSortItem final : public QTableWidgetItem
{
public:
    NumericSortItem(const QString& text, const QVariant& sortValue) : QTableWidgetItem(text)
    {
        setData(Qt::UserRole, sortValue);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
    }
};

QString outputFilenameFor(const GroupKey& key, const GroupingOptions& options, FrameType frameType)
{
    const QString label = key.toDisplayString(options, frameType);
    return QStringLiteral("stacked_%1.fit").arg(ImageIntegration::sanitizeFilename(label));
}

QString formatNormalizationSummary(const StackingSettings& settings)
{
    const auto equalsText = [](const QString& a, const QString& b) {
        return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
    };

    const auto globalNormalizationMethodLabel = [](GlobalNormalizationMethod method) {
        switch (method)
        {
        case GlobalNormalizationMethod::Additive:
            return StackingTab::tr("Additive");
        case GlobalNormalizationMethod::Multiplicative:
            return StackingTab::tr("Multiplicative");
        case GlobalNormalizationMethod::AdditiveScaling:
            return StackingTab::tr("Additive With Scaling");
        case GlobalNormalizationMethod::MultiplicativeScaling:
            return StackingTab::tr("Multiplicative With Scaling");
        }
        return StackingTab::tr("Additive With Scaling");
    };

    const auto localNormalizationPresetLabel = [&](const LocalNormalizationSettings& ln) {
        const QString singleBest = QCoreApplication::translate("LocalNormalizationSettingsDialog", "Single Best Frame");
        const QString integrateBest =
            QCoreApplication::translate("LocalNormalizationSettingsDialog", "Integrate Best Frames");

        if (ln.gridSize == 2 && equalsText(ln.referenceStrategy, singleBest))
        {
            return StackingTab::tr("Quick");
        }

        if (ln.gridSize == 4 && equalsText(ln.referenceStrategy, integrateBest))
        {
            return StackingTab::tr("Standard");
        }

        if (ln.gridSize == 8 && equalsText(ln.referenceStrategy, integrateBest))
        {
            return StackingTab::tr("Fine");
        }

        return StackingTab::tr("Custom");
    };

    if (settings.localNormalization.enabled || settings.normalizationMode == NormalizationMode::Local)
    {
        return StackingTab::tr("Local+Global Normalization: %1 (Additive With Scaling)")
            .arg(localNormalizationPresetLabel(settings.localNormalization));
    }

    if (settings.normalizationMode == NormalizationMode::None)
    {
        return StackingTab::tr("Off");
    }

    return StackingTab::tr("Global Normalization: %1")
        .arg(globalNormalizationMethodLabel(settings.globalNormalizationMethod));
}

QString formatRejectionSummary(const StackingSettings& settings)
{
    if (settings.stackMethod.trimmed().compare(QStringLiteral("Average"), Qt::CaseInsensitive) != 0)
    {
        return QStringLiteral("-");
    }

    const QString method = settings.rejectionMethod.trimmed();
    if (method.compare(QStringLiteral("None"), Qt::CaseInsensitive) == 0)
    {
        return StackingTab::tr("None");
    }

    // Keep it compact: show method + sigma if relevant.
    const bool isSigma = method.contains(QStringLiteral("Sigma"), Qt::CaseInsensitive) ||
                         method.contains(QStringLiteral("Clipping"), Qt::CaseInsensitive);
    if (isSigma)
    {
        const QString low = QString::number(settings.sigmaLow, 'f', 1);
        const QString high = QString::number(settings.sigmaHigh, 'f', 1);
        return StackingTab::tr("%1 (%2/%3)").arg(method, low, high);
    }

    return method;
}

QString formatDrizzleSummary(const DrizzleSettings& settings)
{
    if (!settings.enabled)
    {
        return StackingTab::tr("Off");
    }

    // Prefer compact, readable scale like "2x" or "2.5x".
    const double scale = settings.scale;
    const bool isInt = std::abs(scale - std::round(scale)) < 1e-6;
    return isInt ? StackingTab::tr("%1x").arg(static_cast<int>(std::round(scale)))
                 : StackingTab::tr("%1x").arg(scale, 0, 'f', 1);
}

QString formatStackMethodSummary(const StackingSettings& settings)
{
    if (settings.hdr.enabled)
    {
        return StackingTab::tr("HDR Merge (%1)").arg(settings.stackMethod);
    }

    return settings.stackMethod;
}

bool isWeightingEnabled(const QString& weighting)
{
    const QString normalized = weighting.trimmed().toLower();
    return !normalized.isEmpty() && normalized != QStringLiteral("none") && normalized != QStringLiteral("off") &&
           normalized != QStringLiteral("disabled");
}

QString formatWeightingSummary(const StackingSettings& settings)
{
    if (!isWeightingEnabled(settings.weighting))
    {
        return StackingTab::tr("Off");
    }

    const QString mode =
        settings.weighting.trimmed().isEmpty() ? StackingTab::tr("Custom") : settings.weighting.trimmed();
    const QString scaling = settings.starCountScaling.trimmed().isEmpty() ? QStringLiteral("Quadratic")
                                                                          : settings.starCountScaling.trimmed();
    const bool isStarCount = mode.compare(QStringLiteral("Number of stars"), Qt::CaseInsensitive) == 0 ||
                             mode.compare(QStringLiteral("Stars"), Qt::CaseInsensitive) == 0;
    const bool isPhotometric = mode.compare(QStringLiteral("Photometric"), Qt::CaseInsensitive) == 0;
    const bool isExponential = scaling.compare(QStringLiteral("Exponential"), Qt::CaseInsensitive) == 0;

    QStringList params;
    if (isStarCount)
    {
        params << StackingTab::tr("Scaling=%1").arg(scaling);
        if (isExponential)
        {
            params << StackingTab::tr("Alpha=%1").arg(settings.exponentialAlpha, 0, 'f', 2);
        }
    }
    params << StackingTab::tr("Floor=%1").arg(settings.minWeightFloor, 0, 'f', 3);
    if (isPhotometric)
    {
        params << StackingTab::tr("1.0=Avg");
    }

    return StackingTab::tr("On (%1; %2)").arg(mode, params.join(QStringLiteral(", ")));
}

QString formatWeightingTooltip(const StackingSettings& settings)
{
    if (!isWeightingEnabled(settings.weighting))
    {
        return StackingTab::tr("Weighting is disabled for this integration group.\n\n"
                               "Stacking Settings -> Weighting: None");
    }

    const QString mode =
        settings.weighting.trimmed().isEmpty() ? StackingTab::tr("Custom") : settings.weighting.trimmed();
    const QString scaling = settings.starCountScaling.trimmed().isEmpty() ? QStringLiteral("Quadratic")
                                                                          : settings.starCountScaling.trimmed();
    const bool isStarCount = mode.compare(QStringLiteral("Number of stars"), Qt::CaseInsensitive) == 0 ||
                             mode.compare(QStringLiteral("Stars"), Qt::CaseInsensitive) == 0;
    const bool isPhotometric = mode.compare(QStringLiteral("Photometric"), Qt::CaseInsensitive) == 0;
    const bool isExponential = scaling.compare(QStringLiteral("Exponential"), Qt::CaseInsensitive) == 0;

    QStringList lines;
    lines << StackingTab::tr("Weighting: On");
    lines << StackingTab::tr("Mode: %1").arg(mode);
    lines << StackingTab::tr("Min weight floor: %1").arg(settings.minWeightFloor, 0, 'f', 3);

    if (isStarCount)
    {
        lines << StackingTab::tr("Star count scaling: %1").arg(scaling);
        if (isExponential)
        {
            lines << StackingTab::tr("Exponential alpha: %1").arg(settings.exponentialAlpha, 0, 'f', 2);
        }
    }

    if (isPhotometric)
    {
        lines << StackingTab::tr("Photometric weights are mean-normalized; 1.0 represents an average frame.");
        lines << StackingTab::tr("The minimum weight floor is applied before normalization.");
    }

    return lines.join(QStringLiteral("\n"));
}

QString formatGroupLabel(const GroupKey& key, const GroupingOptions& options, FrameType frameType)
{
    const QString label = key.toDisplayString(options, frameType);
    if (label.trimmed().isEmpty())
    {
        return StackingTab::tr("[Ungrouped]");
    }
    return label;
}

QString formatIntegrationTimeShort(int totalSeconds)
{
    if (totalSeconds < 0)
    {
        totalSeconds = 0;
    }

    if (totalSeconds < 60)
    {
        return StackingTab::tr("%1s").arg(totalSeconds);
    }

    if (totalSeconds < 60 * 60)
    {
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        return StackingTab::tr("%1m %2s").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
    }

    const int hours = totalSeconds / (60 * 60);
    const int minutes = (totalSeconds % (60 * 60)) / 60;
    return StackingTab::tr("%1h %2m").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
}

struct ExposureBucket
{
    double exposureSeconds = 0.0;
    int frameCount = 0;
};

QString formatExposureSeconds(double seconds, int precision = 3)
{
    QString text = QString::number(seconds, 'f', precision);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.'))))
    {
        text.chop(1);
    }
    return text;
}

void addExposureBucket(std::vector<ExposureBucket>& buckets, double exposureSeconds, int frameCount = 1)
{
    constexpr double kExposureMergeToleranceSeconds = 1e-3;
    for (auto& bucket : buckets)
    {
        if (std::abs(bucket.exposureSeconds - exposureSeconds) <= kExposureMergeToleranceSeconds)
        {
            bucket.frameCount += frameCount;
            return;
        }
    }
    buckets.push_back(ExposureBucket{exposureSeconds, frameCount});
}

QString formatExposureBreakdown(const std::vector<ExposureBucket>& buckets, int unknownCount)
{
    QStringList parts;
    parts.reserve(static_cast<int>(buckets.size()) + (unknownCount > 0 ? 1 : 0));

    for (const auto& bucket : buckets)
    {
        parts << StackingTab::tr("%1 x %2s").arg(bucket.frameCount).arg(formatExposureSeconds(bucket.exposureSeconds));
    }
    if (unknownCount > 0)
    {
        parts << StackingTab::tr("%1 x ?s").arg(unknownCount);
    }

    if (parts.isEmpty())
    {
        return QStringLiteral("-");
    }
    return parts.join(QStringLiteral(", "));
}

int g_groupsSortColumn = -1;
Qt::SortOrder g_groupsSortOrder = Qt::AscendingOrder;
} // namespace

class StackingTab::Impl
{
public:
    enum Column
    {
        ColGroup = 0,
        ColFrames,
        ColCalibrationGroups,
        ColTotalIntegrationTime,
        ColStack,
        ColReject,
        ColNormalize,
        ColWeighting,
        ColDrizzle,
        ColOutput,
        ColumnCount
    };

    StackingTab* q;
    an::ui::imageintegration::StackingTabUiElements ui;
    IntegrationDataModel* model;
    QUndoStack* undoStack = nullptr;
    QTimer* debounceTimer = nullptr;
    FrameQualityDashboard* dashboard = nullptr;
    an::ui::AnCardWidget* frameQualityPreviewCard = nullptr;
    std::vector<GroupKey> cachedGroups;
    bool blockSettings = false;
    bool rebuildingTable = false;
    bool applyingThemeStyling = false;

    [[nodiscard]] static QMargins cardContentMargins()
    {
        const auto& spacing = an::ui::tokens().spacing;
        return QMargins(spacing.sm, spacing.md, spacing.sm, spacing.sm);
    }

    void applyCardContentMargins()
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

        applyToCard(ui.grpIntegrationGroups);
        applyToCard(ui.grpExecutionOptions);
        applyToCard(frameQualityPreviewCard);
    }

    void applyGroupBoxTitleStyling()
    {
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
    }

    QStringList frameIdsForIntegrationGroup(const GroupKey& key) const
    {
        if (key.toString().trimmed().isEmpty())
        {
            return {};
        }

        IntegrationGroupEngine engine;
        const auto groups =
            engine.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());
        auto it = groups.find(IntegrationGroupKey{key});
        if (it == groups.end())
        {
            return {};
        }

        QStringList ids;
        ids.reserve(static_cast<int>(it->second.size()));
        for (const auto& id : it->second)
        {
            ids.push_back(id.canonicalPath());
        }
        return ids;
    }

    std::optional<GroupKey> integrationGroupKeyForFrame(const FrameId& id) const
    {
        if (!id.isValid())
        {
            return std::nullopt;
        }

        IntegrationGroupEngine engine;
        const auto groups =
            engine.computeGroups(model->lightFrames().frames(), model->lightIntegrationGroupingOptions());

        for (const auto& [key, frames] : groups)
        {
            const auto it = std::find(frames.begin(), frames.end(), id);
            if (it != frames.end())
            {
                return key.key;
            }
        }

        return std::nullopt;
    }

    void selectIntegrationGroupInTable(const GroupKey& key)
    {
        if (!ui.tblIntegrationGroups || key.toString().trimmed().isEmpty())
        {
            return;
        }

        blockSettings = true;
        for (int i = 0; i < ui.tblIntegrationGroups->rowCount(); ++i)
        {
            auto* item = ui.tblIntegrationGroups->item(i, 0);
            if (item && item->data(Qt::UserRole).toString() == key.toString())
            {
                ui.tblIntegrationGroups->setFocus(Qt::OtherFocusReason);
                ui.tblIntegrationGroups->selectRow(i);
                ui.tblIntegrationGroups->scrollToItem(item);
                break;
            }
        }
        blockSettings = false;

        model->setSelectedGroupKeys({key});
        updateDashboardForSelection();
    }

    void updateDashboardForSelection()
    {
        if (!dashboard)
        {
            return;
        }

        const GroupKey key = selectedGroupKey();
        if (key.toString().trimmed().isEmpty())
        {
            dashboard->setStackExclusionGroupKey(GroupKey{});
            dashboard->setFrameIdFilter({});
            dashboard->setWeightingEnabled(false);
            return;
        }

        dashboard->setStackExclusionGroupKey(key);
        // The settings the group's stack runs: a weighting it lets go of - beside a combination that takes no weights,
        // an exposure merge or a comet stack - is not previewed, whatever the group stores.
        const StackingSettings stored = settingsForGroup(key);
        const StackingSettings settings = ImageIntegration::sanitizeStackingSettings(stored);
        FrameQualityDashboard::WeightingPreviewConfig config;
        config.weightingMode = settings.weighting;
        config.starCountScaling = settings.starCountScaling;
        config.exponentialAlpha = settings.exponentialAlpha;
        config.minWeightFloor = settings.minWeightFloor;
        // The light the group's stack weighs against: the width preview penalizes a frame for the stars it lacks
        // against it, as the stack does.
        if (const std::optional<FrameId> reference = stackingReferenceFrame(*model, IntegrationGroupKey{key}))
        {
            config.referenceFrame = reference->canonicalPath();
        }
        dashboard->setWeightingPreviewConfig(config);

        const bool weighting_enabled =
            !settings.weighting.trimmed().isEmpty() &&
            settings.weighting.trimmed().compare(QStringLiteral("None"), Qt::CaseInsensitive) != 0;
        dashboard->setWeightingEnabled(weighting_enabled, ImageIntegration::weightingNotAppliedNote(stored));
        dashboard->setFrameIdFilter(frameIdsForIntegrationGroup(key));
    }

    explicit Impl(StackingTab* parent, IntegrationDataModel* m, QUndoStack* undo) : q(parent), model(m), undoStack(undo)
    {
        ui = an::ui::imageintegration::buildStackingTabUi(q);
        if (ui.grpIntegrationGroups)
        {
            // AnCardWidget doesn't need setFlat
        }
        if (ui.grpExecutionOptions)
        {
            // AnCardWidget doesn't need setFlat
        }
        applyCardContentMargins();
        applyBackground();

        // Insert a clearly-labeled Frame Quality preview panel below Integration Groups.
        auto* dashboardGroup = new an::ui::AnCardWidget(StackingTab::tr("Frame Quality"), q);
        frameQualityPreviewCard = dashboardGroup;
        dashboardGroup->setObjectName(QStringLiteral("grpFrameQualityPreview"));
        auto* dashboardLayout = new QVBoxLayout();
        dashboardGroup->setContentLayout(dashboardLayout);
        applyCardContentMargins();
        // Keep the container itself normally participating in layout; internal widgets are already
        // shrinkable (tables/histogram use vertical QSizePolicy::Ignored).
        dashboardGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        dashboardGroup->setMinimumHeight(0);
        // AnCardWidget handles its own background internally through anCardBgColor property
        // applyBackground(dashboardGroup);

        // Keep Frame Quality usable when the top-level window is restored to a small height:
        // use a scroll area for the whole dashboard instead of forcing the window to grow.
        auto* dashboardScroll = new QScrollArea(dashboardGroup);
        dashboardScroll->setObjectName(QStringLiteral("frameQualityScrollArea"));
        dashboardScroll->setWidgetResizable(true);
        dashboardScroll->setFrameShape(QFrame::NoFrame);
        dashboardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dashboardScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        dashboardScroll->setMinimumHeight(0);
        applyBackground(dashboardScroll);
        if (dashboardScroll->viewport())
        {
            applyBackground(dashboardScroll->viewport());
        }

        dashboard = new FrameQualityDashboard(model, dashboardScroll, undoStack);
        dashboard->setExclusionMode(FrameQualityDashboard::ExclusionMode::StackOnly);
        dashboardScroll->setWidget(dashboard);
        dashboardLayout->addWidget(dashboardScroll);
        ui.verticalLayout->insertWidget(1, dashboardGroup);

        if (ui.verticalLayout)
        {
            // Prefer a stable, widget-based stretch configuration. Index-based stretch becomes
            // brittle as .ui changes and can lead to odd sizing behavior.
            if (ui.grpIntegrationGroups)
            {
                ui.verticalLayout->setStretchFactor(ui.grpIntegrationGroups, 2);
            }
            ui.verticalLayout->setStretchFactor(dashboardGroup, 3);
            if (ui.grpExecutionOptions)
            {
                ui.verticalLayout->setStretchFactor(ui.grpExecutionOptions, 0);
            }
        }

        applyGroupBoxTitleStyling();

        setupTable();
        setupControls();
        setupDebounceTimer();
        setupConnections();
        updateGroupTable();
        if (dashboard)
        {
            updateDashboardForSelection();
        }
    }

    void applyBackground(QWidget* widget = nullptr, bool useBase = false)
    {
        QWidget* target = widget ? widget : q;
        if (!target)
        {
            return;
        }

        // Let the QTabWidget pane draw the page background so the surrounding "gutter"
        // (matching the tab header) remains visible. We only paint backgrounds for inner
        // containers (group boxes / frames / scroll contents).
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
            if (ui.grpIntegrationGroups)
            {
                // AnCardWidget handles background
            }
            if (ui.grpExecutionOptions)
            {
                // AnCardWidget handles background
            }
        }
    }

    void applyThemeStyling()
    {
        if (applyingThemeStyling)
        {
            return;
        }
        applyingThemeStyling = true;

        applyBackground();
        applyCardContentMargins();
        applyGroupBoxTitleStyling();

        if (auto* frameQualityGroup = q->findChild<QWidget*>(QStringLiteral("grpFrameQualityPreview")))
        {
            // an::ui::AnCardWidget handles its own background rendering
            // applyBackground(frameQualityGroup); // No longer needed for AnCardWidget
        }
        if (auto* frameQualityScroll = q->findChild<QScrollArea*>(QStringLiteral("frameQualityScrollArea")))
        {
            applyBackground(frameQualityScroll);
            if (frameQualityScroll->viewport())
            {
                applyBackground(frameQualityScroll->viewport());
            }
        }

        if (ui.tblIntegrationGroups)
        {
            ImageIntegration::Style::applyTableStyle(ui.tblIntegrationGroups);
            ui.tblIntegrationGroups->horizontalHeader()->setSectionResizeMode(ColOutput, QHeaderView::Stretch);
            if (g_groupsSortColumn >= 0)
            {
                ui.tblIntegrationGroups->horizontalHeader()->setSortIndicator(g_groupsSortColumn, g_groupsSortOrder);
            }
        }

        if (dashboard)
        {
            dashboard->refresh();
            dashboard->update();
        }

        q->update();
        applyingThemeStyling = false;
    }

    void setupTable()
    {
        if (!ui.tblIntegrationGroups)
        {
            return;
        }

        // Allow the window to shrink smaller than the table's default minimumSizeHint()
        // (QAbstractScrollArea defaults can otherwise force a large min-height for the entire tab).
        ui.tblIntegrationGroups->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        ui.tblIntegrationGroups->setMinimumHeight(110);
        if (auto* boxLayout = qobject_cast<QBoxLayout*>(ui.tblIntegrationGroups->parentWidget()->layout()))
        {
            boxLayout->setStretchFactor(ui.tblIntegrationGroups, 1);
        }

        ui.tblIntegrationGroups->setColumnCount(ColumnCount);
        ui.tblIntegrationGroups->setHorizontalHeaderLabels(
            {StackingTab::tr("Group"), StackingTab::tr("Frames"), StackingTab::tr("Calib Groups"),
             StackingTab::tr("Total Time"), StackingTab::tr("Stack"), StackingTab::tr("Reject"),
             StackingTab::tr("Normalize"), StackingTab::tr("Weighting"), StackingTab::tr("Drizzle"),
             StackingTab::tr("Output")});

        ui.tblIntegrationGroups->setSelectionMode(QAbstractItemView::SingleSelection);
        ImageIntegration::Style::applyTableStyle(ui.tblIntegrationGroups);

        ui.tblIntegrationGroups->setSortingEnabled(true);
        ui.tblIntegrationGroups->horizontalHeader()->setSortIndicatorShown(true);
        if (g_groupsSortColumn >= 0)
        {
            ui.tblIntegrationGroups->horizontalHeader()->setSortIndicator(g_groupsSortColumn, g_groupsSortOrder);
        }
        QObject::connect(ui.tblIntegrationGroups->horizontalHeader(), &QHeaderView::sortIndicatorChanged, q,
                         [](int column, Qt::SortOrder order) {
                             g_groupsSortColumn = column;
                             g_groupsSortOrder = order;
                         });

        ui.tblIntegrationGroups->horizontalHeader()->setSectionResizeMode(ColOutput, QHeaderView::Stretch);
    }

    void setupControls()
    {
        const auto applyButtonVariant = [](QPushButton* button, const char* variant) {
            if (!button || !variant)
            {
                return;
            }
            button->setProperty("anButtonVariant", QString::fromUtf8(variant));
        };

        if (ui.btnStackingSettings)
        {
            applyButtonVariant(ui.btnStackingSettings, "secondary");
            ui.btnStackingSettings->setEnabled(false);
            ui.btnStackingSettings->setToolTip(StackingTab::tr("Open stacking settings dialog for the selected group"));
        }

        if (ui.btnRunIntegrationGroup)
        {
            applyButtonVariant(ui.btnRunIntegrationGroup, "primary");
            ui.btnRunIntegrationGroup->setEnabled(false);
            ui.btnRunIntegrationGroup->setToolTip(
                StackingTab::tr("Execute full integration ONLY for the selected integration group.\n"
                                "This will stack the selected group, register its frames (if needed), "
                                "and calibrate only the frames required by this group."));
        }

        if (ui.btnRunIntegration)
        {
            applyButtonVariant(ui.btnRunIntegration, "primary");
            ui.btnRunIntegration->setMinimumHeight(32);
            ui.btnRunIntegration->setText(StackingTab::tr("Run Integration (All Groups)"));
            ui.btnRunIntegration->setToolTip(StackingTab::tr("Run the integration workflow for all stacking groups.\n"
                                                             "Includes calibration, registration, and stacking."));
        }
    }

    void setupDebounceTimer()
    {
        debounceTimer = new QTimer(q);
        debounceTimer->setSingleShot(true);
        debounceTimer->setInterval(200);
        QObject::connect(debounceTimer, &QTimer::timeout, q, [this]() {
            updateGroupTable();
            if (dashboard)
            {
                updateDashboardForSelection();
            }
        });
    }

    void setupConnections()
    {
        // Model signals
        QObject::connect(model, &IntegrationDataModel::framesAdded, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::framesRemoved, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::frameUpdated, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::groupingChanged, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::integrationGroupsChanged, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::settingsChanged, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::modelReset, q, &StackingTab::refreshAll);
        QObject::connect(model, &IntegrationDataModel::selectionChanged, q, [this]() { onModelSelectionChanged(); });

        // Table signals
        if (ui.tblIntegrationGroups)
        {
            QObject::connect(ui.tblIntegrationGroups, &QTableWidget::itemSelectionChanged, q,
                             [this]() { onTableSelectionChanged(); });

            QObject::connect(ui.tblIntegrationGroups, &QTableWidget::cellDoubleClicked, q,
                             [this](int row, int /*col*/) {
                                 if (row < 0 || row >= static_cast<int>(cachedGroups.size()))
                                 {
                                     return;
                                 }
                                 openSettingsDialogForGroup(cachedGroups[static_cast<size_t>(row)]);
                             });

            // Enter key opens settings dialog
            auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), ui.tblIntegrationGroups, nullptr, nullptr,
                                        Qt::WidgetWithChildrenShortcut);
            auto* returnKey = new QShortcut(QKeySequence(Qt::Key_Enter), ui.tblIntegrationGroups, nullptr, nullptr,
                                            Qt::WidgetWithChildrenShortcut);

            const auto openSettings = [this]() {
                if (!ui.tblIntegrationGroups->hasFocus())
                    return;
                const GroupKey key = selectedGroupKey();
                if (!key.toString().isEmpty())
                    openSettingsDialogForGroup(key);
            };
            QObject::connect(enter, &QShortcut::activated, q, openSettings);
            QObject::connect(returnKey, &QShortcut::activated, q, openSettings);
        }

        // Stacking Grouping Options
        if (ui.chkGroupBySession)
        {
            QObject::connect(ui.chkGroupBySession, &QCheckBox::toggled, q, [this](bool checked) {
                auto opts = model->lightIntegrationGroupingOptions();
                const auto oldOpts = opts;
                opts.bySession = checked;
                if (oldOpts == opts)
                {
                    return;
                }
                if (undoStack)
                {
                    undoStack->push(new SetLightIntegrationGroupingOptionsCommand(model, oldOpts, opts));
                }
                else
                {
                    model->setLightIntegrationGroupingOptions(opts);
                }
            });
        }
        if (ui.chkGroupByCustomTag)
        {
            QObject::connect(ui.chkGroupByCustomTag, &QCheckBox::toggled, q, [this](bool checked) {
                auto opts = model->lightIntegrationGroupingOptions();
                const auto oldOpts = opts;
                opts.byCustomTag = checked;
                if (oldOpts == opts)
                {
                    return;
                }
                if (undoStack)
                {
                    undoStack->push(new SetLightIntegrationGroupingOptionsCommand(model, oldOpts, opts));
                }
                else
                {
                    model->setLightIntegrationGroupingOptions(opts);
                }
            });
        }
        if (ui.chkGroupByExposure)
        {
            QObject::connect(ui.chkGroupByExposure, &QCheckBox::toggled, q, [this](bool checked) {
                auto opts = model->lightIntegrationGroupingOptions();
                const auto oldOpts = opts;
                opts.byExposure = checked;
                if (oldOpts == opts)
                {
                    return;
                }
                if (undoStack)
                {
                    undoStack->push(new SetLightIntegrationGroupingOptionsCommand(model, oldOpts, opts));
                }
                else
                {
                    model->setLightIntegrationGroupingOptions(opts);
                }
                // Enabled state updated in updateOptions via model signal loopback
            });
        }
        if (ui.spinExposureTolerance)
        {
            QObject::connect(ui.spinExposureTolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged), q,
                             [this](double value) {
                                 auto opts = model->lightIntegrationGroupingOptions();
                                 const auto oldOpts = opts;
                                 opts.exposureToleranceSeconds = value;
                                 if (oldOpts == opts)
                                 {
                                     return;
                                 }
                                 if (undoStack)
                                 {
                                     undoStack->push(
                                         new SetLightIntegrationGroupingOptionsCommand(model, oldOpts, opts));
                                 }
                                 else
                                 {
                                     model->setLightIntegrationGroupingOptions(opts);
                                 }
                             });
        }

        // Button signals
        if (ui.btnStackingSettings)
        {
            QObject::connect(ui.btnStackingSettings, &QPushButton::clicked, q, [this]() {
                const GroupKey key = selectedGroupKey();
                if (!key.toString().isEmpty())
                {
                    openSettingsDialogForGroup(key);
                }
            });
        }

        if (ui.btnRunIntegrationGroup)
        {
            QObject::connect(ui.btnRunIntegrationGroup, &QPushButton::clicked, q, [this]() {
                const GroupKey key = selectedGroupKey();
                if (!key.toString().isEmpty())
                {
                    emit q->runIntegrationRequested(IntegrationGroupKey{key}.toString());
                }
            });
        }

        if (ui.btnRunIntegration)
        {
            QObject::connect(ui.btnRunIntegration, &QPushButton::clicked, q,
                             [this]() { emit q->runIntegrationAllRequested(); });
        }

        if (ui.chkSaveCalibratedFrames)
        {
            QObject::connect(ui.chkSaveCalibratedFrames, &QCheckBox::toggled, q, [this](bool checked) {
                const auto oldMode = model->lightCalibrationCacheMode();
                const auto newMode = checked ? LightCalibrationCacheMode::Disk : LightCalibrationCacheMode::InMemory;
                if (undoStack)
                {
                    undoStack->push(new SetLightCalibrationCacheModeCommand(model, oldMode, newMode));
                }
                else
                {
                    model->setLightCalibrationCacheMode(newMode);
                }
            });
        }

        if (ui.chkLimitMemory)
        {
            QObject::connect(ui.chkLimitMemory, &QCheckBox::toggled, q, [this](bool checked) {
                ui.spinMaxMemory->setEnabled(checked);
                // Apply to all light stacking settings
                const auto& currentSettings = model->stackingSettings(FrameType::Light);
                if (undoStack)
                {
                    model->beginBatchUpdate();
                    undoStack->beginMacro(StackingTab::tr("Update memory limit setting"));
                }
                for (auto it = currentSettings.begin(); it != currentSettings.end(); ++it)
                {
                    const StackingSettings oldSettings = it->second;
                    StackingSettings newSettings = oldSettings;
                    newSettings.limitMemory = checked;
                    if (oldSettings.limitMemory == newSettings.limitMemory)
                    {
                        continue;
                    }
                    if (undoStack)
                    {
                        undoStack->push(new SetStackingSettingsCommand(model, FrameType::Light, it->first, oldSettings,
                                                                       newSettings));
                    }
                    else
                    {
                        model->setStackingSettings(FrameType::Light, it->first, newSettings);
                    }
                }
                if (undoStack)
                {
                    undoStack->endMacro();
                    model->endBatchUpdate();
                }
            });
        }

        if (ui.spinMaxMemory)
        {
            QObject::connect(ui.spinMaxMemory, QOverload<int>::of(&QSpinBox::valueChanged), q, [this](int value) {
                // Apply to all light stacking settings
                const auto& currentSettings = model->stackingSettings(FrameType::Light);
                if (undoStack)
                {
                    model->beginBatchUpdate();
                    undoStack->beginMacro(StackingTab::tr("Update max memory setting"));
                }
                for (auto it = currentSettings.begin(); it != currentSettings.end(); ++it)
                {
                    const StackingSettings oldSettings = it->second;
                    StackingSettings newSettings = oldSettings;
                    newSettings.maxMemoryMb = value;
                    if (oldSettings.maxMemoryMb == newSettings.maxMemoryMb)
                    {
                        continue;
                    }
                    if (undoStack)
                    {
                        undoStack->push(new SetStackingSettingsCommand(model, FrameType::Light, it->first, oldSettings,
                                                                       newSettings));
                    }
                    else
                    {
                        model->setStackingSettings(FrameType::Light, it->first, newSettings);
                    }
                }
                if (undoStack)
                {
                    undoStack->endMacro();
                    model->endBatchUpdate();
                }
            });
        }
    }

    void openSettingsDialogForGroup(const GroupKey& key)
    {
        if (key.toString().isEmpty())
        {
            return;
        }

        // Reuse the calibration helper; it handles load + save to model, and refreshes the table once the settings
        // were saved.
        const QPointer<StackingTab> tabGuard(q);
        openStackingSettingsDialog(model, FrameType::Light, key,
                                   formatGroupLabel(key, model->lightIntegrationGroupingOptions(), FrameType::Light), q,
                                   undoStack, [this, tabGuard](const bool accepted) {
                                       if (accepted && tabGuard)
                                       {
                                           updateGroupTable();
                                       }
                                   });
    }

    void scheduleRefresh()
    {
        debounceTimer->start();
    }

    void setEmptyMessage(QTableWidget* table, const QString& message)
    {
        if (!table)
        {
            return;
        }

        table->clearContents();
        table->clearSpans();
        table->setRowCount(1);
        table->setSpan(0, 0, 1, table->columnCount());
        auto* item = new QTableWidgetItem(message);
        item->setTextAlignment(Qt::AlignCenter);
        item->setForeground(QBrush(ImageIntegration::Style::mutedTextColor()));
        table->setItem(0, 0, item);
    }

    GroupKey selectedGroupKey() const
    {
        if (!ui.tblIntegrationGroups || cachedGroups.empty())
        {
            return GroupKey{};
        }

        auto* selection = ui.tblIntegrationGroups->selectionModel();
        if (!selection)
        {
            return GroupKey{};
        }

        const auto selected = selection->selectedRows();
        if (selected.isEmpty())
        {
            return GroupKey{};
        }

        const int row = selected.front().row();
        if (row < 0 || row >= static_cast<int>(cachedGroups.size()))
        {
            return GroupKey{};
        }

        return cachedGroups[static_cast<size_t>(row)];
    }

    StackingSettings settingsForGroup(const GroupKey& key) const
    {
        const auto& map = model->stackingSettings(FrameType::Light);
        auto it = map.find(key);
        if (it != map.end())
        {
            return it->second;
        }
        return StackingSettings{};
    }

    std::vector<ImageIntegration::RgbIntegrationGroupCandidate> rgbCandidatesForCurrentGroups() const
    {
        std::vector<ImageIntegration::RgbIntegrationGroupCandidate> candidates;
        if (!model)
        {
            return candidates;
        }

        const auto light_frames = model->lightFrames().frames();
        if (light_frames.empty())
        {
            return candidates;
        }

        std::unordered_map<FrameId, const FrameInfo*> frames_by_id;
        frames_by_id.reserve(light_frames.size());
        for (const auto& frame : light_frames)
        {
            frames_by_id.emplace(frame.id, &frame);
        }

        IntegrationGroupEngine engine;
        const auto groups = engine.computeGroups(light_frames, model->lightIntegrationGroupingOptions());
        candidates.reserve(groups.size());

        for (const auto& [group_key, frame_ids] : groups)
        {
            if (frame_ids.empty())
            {
                continue;
            }

            const FrameInfo* representative = nullptr;
            for (const auto& id : frame_ids)
            {
                const auto it = frames_by_id.find(id);
                if (it != frames_by_id.end())
                {
                    representative = it->second;
                    break;
                }
            }
            if (!representative)
            {
                continue;
            }

            const GroupKey registration_group =
                RegistrationGroupEngine::toMachineGroupKey(RegistrationGroupKey::fromFrame(*representative));
            candidates.push_back(ImageIntegration::RgbIntegrationGroupCandidate{
                group_key.toString(), registration_group.toString(), representative->metadata.filter(),
                outputFilenameFor(group_key.key, model->lightIntegrationGroupingOptions(), FrameType::Light),
                static_cast<int>(frame_ids.size())});
        }

        return candidates;
    }

    void updateOptions()
    {
        if (ui.chkSaveCalibratedFrames)
        {
            QSignalBlocker blocker(ui.chkSaveCalibratedFrames);
            ui.chkSaveCalibratedFrames->setChecked(model->lightCalibrationCacheMode() ==
                                                   LightCalibrationCacheMode::Disk);
        }

        // Load memory settings from the first available light group (assuming global consistency)
        // or defaults if no groups exist.
        bool memLimitEnabled = false;
        int memLimitMb = 2048;
        const auto& lightSettings = model->stackingSettings(FrameType::Light);
        if (!lightSettings.empty())
        {
            const auto& first = lightSettings.begin()->second;
            memLimitEnabled = first.limitMemory;
            memLimitMb = first.maxMemoryMb;
        }

        if (ui.chkLimitMemory)
        {
            QSignalBlocker blocker(ui.chkLimitMemory);
            ui.chkLimitMemory->setChecked(memLimitEnabled);
        }
        if (ui.spinMaxMemory)
        {
            QSignalBlocker blocker(ui.spinMaxMemory);
            ui.spinMaxMemory->setValue(memLimitMb);
            ui.spinMaxMemory->setEnabled(memLimitEnabled);
        }

        const auto& opts = model->lightIntegrationGroupingOptions();
        if (ui.chkGroupBySession)
        {
            QSignalBlocker blocker(ui.chkGroupBySession);
            ui.chkGroupBySession->setChecked(opts.bySession);
        }
        if (ui.chkGroupByCustomTag)
        {
            QSignalBlocker blocker(ui.chkGroupByCustomTag);
            ui.chkGroupByCustomTag->setChecked(opts.byCustomTag);
        }
        if (ui.chkGroupByExposure)
        {
            QSignalBlocker blocker(ui.chkGroupByExposure);
            ui.chkGroupByExposure->setChecked(opts.byExposure);
        }
        if (ui.spinExposureTolerance)
        {
            QSignalBlocker blocker(ui.spinExposureTolerance);
            ui.spinExposureTolerance->setValue(opts.exposureToleranceSeconds);
            ui.spinExposureTolerance->setEnabled(opts.byExposure);
        }

        if (ui.chkCombineRgbImages)
        {
            const bool can_combine_rgb =
                ImageIntegration::hasEligibleRgbIntegrationGroup(rgbCandidatesForCurrentGroups());
            QSignalBlocker blocker(ui.chkCombineRgbImages);
            ui.chkCombineRgbImages->setEnabled(can_combine_rgb);
            if (!can_combine_rgb)
            {
                ui.chkCombineRgbImages->setChecked(false);
            }
        }
    }

    void updateGroupTable()
    {
        updateOptions();

        if (!ui.tblIntegrationGroups)
        {
            return;
        }

        if (rebuildingTable)
        {
            return;
        }
        rebuildingTable = true;

        GroupKey currentSelection = selectedGroupKey();
        cachedGroups = model->integrationGroups();
        IntegrationGroupEngine::GroupMap group_map;

        const auto light_frames = model->lightFrames().frames();

        {
            IntegrationGroupEngine engine;
            group_map = engine.computeGroups(light_frames, model->lightIntegrationGroupingOptions());
        }

        std::unordered_map<FrameId, const FrameInfo*> framesById;
        framesById.reserve(light_frames.size());
        for (const auto& frame : light_frames)
        {
            framesById.emplace(frame.id, &frame);
        }

        const auto& calibrationGroupingOptions = model->groupingOptions(FrameType::Light);
        const std::vector<FrameId> emptyFrames;

        const bool wasSorting = ui.tblIntegrationGroups->isSortingEnabled();
        ui.tblIntegrationGroups->setSortingEnabled(false);

        ui.tblIntegrationGroups->setRowCount(0);
        ui.tblIntegrationGroups->clearContents();
        ui.tblIntegrationGroups->clearSpans();

        if (cachedGroups.empty())
        {
            setEmptyMessage(ui.tblIntegrationGroups, StackingTab::tr("No integration groups available."));
            if (ui.btnStackingSettings)
                ui.btnStackingSettings->setEnabled(false);
            if (ui.btnRunIntegration)
                ui.btnRunIntegration->setEnabled(false);
            ui.tblIntegrationGroups->setSortingEnabled(wasSorting);
            rebuildingTable = false;
            return;
        }

        ui.tblIntegrationGroups->setRowCount(static_cast<int>(cachedGroups.size()));

        int row = 0;
        for (const auto& key : cachedGroups)
        {
            const auto groupIt = group_map.find(IntegrationGroupKey{key});
            const std::vector<FrameId>& groupFrames = (groupIt != group_map.end()) ? groupIt->second : emptyFrames;

            std::unordered_set<GroupKey> contributingCalibrationGroups;
            contributingCalibrationGroups.reserve(32);

            double totalExposureSeconds = 0.0;
            double minExposureSeconds = std::numeric_limits<double>::infinity();
            double maxExposureSeconds = 0.0;
            int knownExposureFrameCount = 0;
            int unknownExposureFrameCount = 0;
            std::vector<ExposureBucket> exposureBuckets;
            exposureBuckets.reserve(8);
            int stackExcludedFrameCount = 0;

            for (const auto& id : groupFrames)
            {
                const auto it = framesById.find(id);
                if (it == framesById.end() || !it->second)
                {
                    continue;
                }

                const FrameInfo& frame = *it->second;
                if (model->isFrameStackExcluded(key, frame.id))
                {
                    ++stackExcludedFrameCount;
                    continue;
                }

                const double exposure = frame.metadata.exposureSeconds();
                if (exposure > 0.0)
                {
                    totalExposureSeconds += exposure;
                    minExposureSeconds = (std::min)(minExposureSeconds, exposure);
                    maxExposureSeconds = (std::max)(maxExposureSeconds, exposure);
                    ++knownExposureFrameCount;
                    addExposureBucket(exposureBuckets, exposure, 1);
                }
                else
                {
                    ++unknownExposureFrameCount;
                }

                const LightGroupKey lightKey = LightGroupKey::fromFrame(frame);
                contributingCalibrationGroups.insert(lightKey.toGroupKey(calibrationGroupingOptions));
            }

            const int totalFrameCount = static_cast<int>(groupFrames.size());
            const int frameCount = (std::max)(0, totalFrameCount - stackExcludedFrameCount);
            std::sort(exposureBuckets.begin(), exposureBuckets.end(),
                      [](const ExposureBucket& lhs, const ExposureBucket& rhs) {
                          return lhs.exposureSeconds < rhs.exposureSeconds;
                      });
            const QString exposureBreakdown = formatExposureBreakdown(exposureBuckets, unknownExposureFrameCount);

            const QString groupLabel =
                formatGroupLabel(key, model->lightIntegrationGroupingOptions(), FrameType::Light);
            auto* groupItem = new QTableWidgetItem(groupLabel);
            groupItem->setData(Qt::UserRole, key.toString());
            groupItem->setToolTip(
                StackingTab::tr("Integration group:\n%1\n\nStack frames: %2 of %3\nExcluded from this stack: "
                                "%4\nCalibration groups: %5\nTotal integration time: %6\nExposure breakdown: %7")
                    .arg(groupLabel)
                    .arg(frameCount)
                    .arg(totalFrameCount)
                    .arg(stackExcludedFrameCount)
                    .arg(static_cast<int>(contributingCalibrationGroups.size()))
                    .arg(WorkflowTimeEstimator::formatDuration(static_cast<int>(std::round(totalExposureSeconds))))
                    .arg(exposureBreakdown));
            ui.tblIntegrationGroups->setItem(row, ColGroup, groupItem);

            auto* framesItem = new NumericSortItem(QString::number(frameCount), frameCount);
            framesItem->setData(Qt::EditRole, frameCount);
            framesItem->setTextAlignment(Qt::AlignCenter);
            if (frameCount <= 0)
            {
                framesItem->setToolTip(
                    stackExcludedFrameCount > 0
                        ? StackingTab::tr("All %1 frame(s) in this integration group are excluded from this stack.")
                              .arg(totalFrameCount)
                        : StackingTab::tr("No frames in this integration group."));
            }
            else
            {
                QStringList tip;
                tip << StackingTab::tr("Stack frames: %1 of %2").arg(frameCount).arg(totalFrameCount);
                if (stackExcludedFrameCount > 0)
                {
                    tip << StackingTab::tr("Excluded from this stack: %1").arg(stackExcludedFrameCount);
                }
                tip << StackingTab::tr("Total integration time: %1")
                           .arg(WorkflowTimeEstimator::formatDuration(
                               static_cast<int>(std::round(totalExposureSeconds))));
                tip << StackingTab::tr("Exposure breakdown: %1").arg(exposureBreakdown);
                if (knownExposureFrameCount > 0)
                {
                    const double avgExposure = totalExposureSeconds / static_cast<double>(knownExposureFrameCount);
                    tip << StackingTab::tr("Average exposure: %1s").arg(avgExposure, 0, 'f', 1);
                    if (std::isfinite(minExposureSeconds) && maxExposureSeconds > 0.0)
                    {
                        tip << StackingTab::tr("Exposure range: %1s .. %2s")
                                   .arg(minExposureSeconds, 0, 'f', 1)
                                   .arg(maxExposureSeconds, 0, 'f', 1);
                    }
                }
                framesItem->setToolTip(tip.join(QStringLiteral("\n")));
            }
            ui.tblIntegrationGroups->setItem(row, ColFrames, framesItem);

            const int calibGroupCount = static_cast<int>(contributingCalibrationGroups.size());
            auto* calibGroupsItem = new NumericSortItem(QString::number(calibGroupCount), calibGroupCount);
            calibGroupsItem->setData(Qt::EditRole, calibGroupCount);
            calibGroupsItem->setTextAlignment(Qt::AlignCenter);
            if (calibGroupCount <= 0)
            {
                calibGroupsItem->setToolTip(StackingTab::tr("No calibration groups found for this integration group."));
            }
            else
            {
                std::vector<QString> labels;
                labels.reserve(contributingCalibrationGroups.size());
                for (const auto& calibKey : contributingCalibrationGroups)
                {
                    labels.push_back(calibKey.toDisplayString(calibrationGroupingOptions, FrameType::Light));
                }
                std::sort(labels.begin(), labels.end());

                QStringList tip;
                tip << StackingTab::tr("Calibration groups contributing frames:");
                const int maxLines = 12;
                int shown = 0;
                for (const auto& label : labels)
                {
                    if (shown >= maxLines)
                    {
                        tip << StackingTab::tr("... (%1 more)").arg(static_cast<int>(labels.size()) - shown);
                        break;
                    }
                    tip << QStringLiteral("  %1").arg(label.trimmed().isEmpty() ? StackingTab::tr("[Ungrouped]")
                                                                                : label);
                    ++shown;
                }
                calibGroupsItem->setToolTip(tip.join(QStringLiteral("\n")));
            }
            ui.tblIntegrationGroups->setItem(row, ColCalibrationGroups, calibGroupsItem);

            const int totalSeconds = static_cast<int>(std::round(totalExposureSeconds));
            auto* totalTimeItem = new NumericSortItem(formatIntegrationTimeShort(totalSeconds), totalSeconds);
            totalTimeItem->setTextAlignment(Qt::AlignCenter);
            totalTimeItem->setToolTip(
                StackingTab::tr("Sum of exposure times for all frames in this group:\n%1\nExposure breakdown: %2")
                    .arg(WorkflowTimeEstimator::formatDuration(totalSeconds))
                    .arg(exposureBreakdown));
            ui.tblIntegrationGroups->setItem(row, ColTotalIntegrationTime, totalTimeItem);

            const StackingSettings settings = settingsForGroup(key);
            const QString settingsTooltip =
                stackingSettingsTooltip(model, FrameType::Light, key, model->stackingSettings(FrameType::Light));
            QString stackText = formatStackMethodSummary(settings);
            QString stackToolTip = settingsTooltip;
            if (settings.hdr.enabled)
            {
                stackText =
                    StackingTab::tr("HDR Merge (%1, %2 buckets)").arg(settings.stackMethod).arg(exposureBuckets.size());
                stackToolTip +=
                    QStringLiteral("\n\n") + StackingTab::tr("HDR exposure buckets: %1").arg(exposureBreakdown);
            }

            auto* stackItem = new QTableWidgetItem(stackText);
            stackItem->setToolTip(stackToolTip);
            ui.tblIntegrationGroups->setItem(row, ColStack, stackItem);

            auto* rejectItem = new QTableWidgetItem(formatRejectionSummary(settings));
            rejectItem->setToolTip(settingsTooltip);
            ui.tblIntegrationGroups->setItem(row, ColReject, rejectItem);

            auto* normItem = new QTableWidgetItem(formatNormalizationSummary(settings));
            normItem->setToolTip(settingsTooltip);
            ui.tblIntegrationGroups->setItem(row, ColNormalize, normItem);

            auto* weightingItem = new QTableWidgetItem(formatWeightingSummary(settings));
            weightingItem->setToolTip(formatWeightingTooltip(settings));
            ui.tblIntegrationGroups->setItem(row, ColWeighting, weightingItem);

            auto* drizzleItem = new QTableWidgetItem(formatDrizzleSummary(settings.drizzle));
            drizzleItem->setToolTip(settingsTooltip);
            ui.tblIntegrationGroups->setItem(row, ColDrizzle, drizzleItem);

            const QString outputName =
                outputFilenameFor(key, model->lightIntegrationGroupingOptions(), FrameType::Light);
            auto* outputItem = new QTableWidgetItem(outputName);
            outputItem->setToolTip(StackingTab::tr("Output filename for this integration group:\n%1").arg(outputName));
            ui.tblIntegrationGroups->setItem(row, ColOutput, outputItem);

            ++row;
        }

        ui.tblIntegrationGroups->setSortingEnabled(wasSorting);
        if (wasSorting)
        {
            if (g_groupsSortColumn >= 0)
            {
                ui.tblIntegrationGroups->sortItems(g_groupsSortColumn, g_groupsSortOrder);
                ui.tblIntegrationGroups->horizontalHeader()->setSortIndicator(g_groupsSortColumn, g_groupsSortOrder);
            }
            else
            {
                ui.tblIntegrationGroups->sortItems(ColGroup, Qt::AscendingOrder);
            }
        }

        ui.tblIntegrationGroups->resizeColumnsToContents();

        // Restore selection or select first row
        int selectRow = 0;
        if (!currentSelection.toString().isEmpty())
        {
            for (int i = 0; i < ui.tblIntegrationGroups->rowCount(); ++i)
            {
                const auto* item = ui.tblIntegrationGroups->item(i, 0);
                if (item && item->data(Qt::UserRole).toString() == currentSelection.toString())
                {
                    selectRow = i;
                    break;
                }
            }
        }
        ui.tblIntegrationGroups->selectRow(selectRow);

        rebuildingTable = false;
        onTableSelectionChanged();
    }

    void onTableSelectionChanged()
    {
        if (blockSettings || rebuildingTable)
            return;

        GroupKey key = selectedGroupKey();
        const bool hasSelection = !key.toString().isEmpty();

        if (ui.btnStackingSettings)
            ui.btnStackingSettings->setEnabled(hasSelection);

        if (ui.btnRunIntegrationGroup)
            ui.btnRunIntegrationGroup->setEnabled(hasSelection);

        if (ui.btnRunIntegration)
            ui.btnRunIntegration->setEnabled(hasSelection);

        if (hasSelection)
        {
            model->setSelectedGroupKeys({key});
        }
        else
        {
            model->setSelectedGroupKeys({});
        }

        updateDashboardForSelection();
    }

    void onModelSelectionChanged()
    {
        const auto& keys = model->selectedGroupKeys();
        if (keys.empty())
        {
            if (ui.tblIntegrationGroups)
            {
                blockSettings = true;
                ui.tblIntegrationGroups->clearSelection();
                blockSettings = false;
            }
            updateDashboardForSelection();
            return;
        }

        GroupKey key = keys.front(); // Single selection for now

        if (ui.tblIntegrationGroups)
        {
            auto* selection = ui.tblIntegrationGroups->selectionModel();
            if (!selection)
            {
                return;
            }
            blockSettings = true;
            for (int i = 0; i < ui.tblIntegrationGroups->rowCount(); ++i)
            {
                auto* item = ui.tblIntegrationGroups->item(i, 0);
                if (item && item->data(Qt::UserRole).toString() == key.toString())
                {
                    ui.tblIntegrationGroups->selectRow(i);
                    break;
                }
            }
            blockSettings = false;
        }
        updateDashboardForSelection();
    }
};

StackingTab::StackingTab(IntegrationDataModel* model, QWidget* parent, QUndoStack* undoStack)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model, undoStack))
{
}

StackingTab::~StackingTab()
{
    // Ensure queued/in-flight callbacks cannot reach this object while it is tearing down.
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);

    if (m_impl && m_impl->debounceTimer)
    {
        m_impl->debounceTimer->stop();
    }
}

void StackingTab::refreshAll()
{
    if (!m_impl)
    {
        return;
    }
    m_impl->scheduleRefresh();
}

void StackingTab::changeEvent(QEvent* event)
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

void StackingTab::showFrameInTable(const QString& framePath)
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
    const FrameId canonicalId(canonical);
    if (!canonicalId.isValid())
    {
        return;
    }

    const auto groupKeyOpt = m_impl->integrationGroupKeyForFrame(canonicalId);
    if (!groupKeyOpt.has_value() || groupKeyOpt->toString().trimmed().isEmpty())
    {
        return;
    }

    m_impl->model->setSelectedGroupKeys({*groupKeyOpt});
    m_impl->model->setSelectedFrameIds({canonicalId});

    // Update now (avoid debounce) so the row and dashboard content exist.
    m_impl->updateGroupTable();
    m_impl->selectIntegrationGroupInTable(*groupKeyOpt);

    QTimer::singleShot(0, this, [this, canonical]() {
        if (m_impl && m_impl->dashboard)
        {
            m_impl->dashboard->highlightFrame(canonical);
        }
    });
}

void StackingTab::sortIntegrationGroupsByColumn(int column, Qt::SortOrder order)
{
    if (!m_impl || !m_impl->ui.tblIntegrationGroups)
    {
        return;
    }

    g_groupsSortColumn = column;
    g_groupsSortOrder = order;
    m_impl->ui.tblIntegrationGroups->sortItems(column, order);
    m_impl->ui.tblIntegrationGroups->horizontalHeader()->setSortIndicator(column, order);
}

int StackingTab::integrationGroupsSortIndicatorColumn() const
{
    if (!m_impl || !m_impl->ui.tblIntegrationGroups)
    {
        return -1;
    }
    return m_impl->ui.tblIntegrationGroups->horizontalHeader()->sortIndicatorSection();
}

Qt::SortOrder StackingTab::integrationGroupsSortIndicatorOrder() const
{
    if (!m_impl || !m_impl->ui.tblIntegrationGroups)
    {
        return Qt::AscendingOrder;
    }
    return m_impl->ui.tblIntegrationGroups->horizontalHeader()->sortIndicatorOrder();
}

bool StackingTab::isReprocessExistingEnabled() const
{
    return m_impl->ui.chkReprocessExisting && m_impl->ui.chkReprocessExisting->isChecked();
}

bool StackingTab::isOpenIntegratedFilesEnabled() const
{
    return !m_impl->ui.chkOpenIntegratedFiles || m_impl->ui.chkOpenIntegratedFiles->isChecked();
}

bool StackingTab::isCombineRgbImagesEnabled() const
{
    return m_impl->ui.chkCombineRgbImages && m_impl->ui.chkCombineRgbImages->isEnabled() &&
           m_impl->ui.chkCombineRgbImages->isChecked();
}

int StackingTab::maxParallelCores() const
{
    if (!m_impl || !m_impl->ui.spinMaxParallelCores)
    {
        return ImageIntegration::core::detectedParallelCoreCount();
    }

    return ImageIntegration::core::clampParallelCoreLimit(m_impl->ui.spinMaxParallelCores->value());
}
