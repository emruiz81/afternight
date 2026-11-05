// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationCoverageWidget.h"

#include "theme/AnDesignTokens.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/CalibrationMatcher.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/tabs/CalibrationPanelUtils.h"

#include <QPainter>
#include <QToolTip>
#include <QHelpEvent>
#include <QPalette>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::CalibrationCoverageWidget)

namespace ImageIntegration
{

struct CoverageStats
{
    int totalGroups = 0;
    int completeGroups = 0; // Meets requirements; all relevant matches are exact
    int warningGroups = 0; // All matched, but some are fallback
    int incompleteGroups = 0; // Missing at least one required calibration match
    int overrideGroups = 0; // Has manual overrides
};

class CalibrationCoverageWidget::Impl
{
public:
    CalibrationCoverageWidget* q;
    IntegrationDataModel* model;
    CoverageStats stats;
    bool hasCalibrationInputs = false;

    [[nodiscard]] QColor completeColor() const
    {
        return an::ui::tokens().colors.statusSuccess;
    }
    [[nodiscard]] QColor warningColor() const
    {
        return an::ui::tokens().colors.statusWarning;
    }
    [[nodiscard]] QColor missingColor() const
    {
        return an::ui::tokens().colors.statusError;
    }
    [[nodiscard]] QColor overrideColor() const
    {
        return an::ui::tokens().colors.statusInfo;
    }

    explicit Impl(CalibrationCoverageWidget* parent, IntegrationDataModel* m) : q(parent), model(m) {}

    [[nodiscard]] QString tooltipHtml() const
    {
        if (!hasCalibrationInputs)
        {
            return q
                ->tr("<b>Calibration Coverage Summary</b><br/>"
                     "Total Light Groups: %1<br/><br/>"
                     "No calibration frames are loaded (Bias/Dark/Flat), so coverage is not computed.")
                .arg(stats.totalGroups);
        }

        return q
            ->tr("<b>Calibration Coverage Summary</b><br/>"
                 "Total Light Groups: %1<br/>"
                 "<span style='color:%2'>●</span> Complete: %3<br/>"
                 "<span style='color:%4'>●</span> Warning: %5<br/>"
                 "<span style='color:%6'>●</span> Override: %7<br/>"
                 "<span style='color:%8'>●</span> Incomplete: %9<br/><br/>"
                 "<i>Incomplete</i> means the group is missing at least one required calibration match "
                 "and the user did not explicitly select a manual override or <i>Do not use</i>.<br/>"
                 "Required matches: Flat and either Bias or Dark; in CCD mode, both Bias and Dark.<br/>"
                 "<i>Do not use</i> is counted as a Warning (intentional), not Incomplete.")
            .arg(stats.totalGroups)
            .arg(completeColor().name())
            .arg(stats.completeGroups)
            .arg(warningColor().name())
            .arg(stats.warningGroups)
            .arg(overrideColor().name())
            .arg(stats.overrideGroups)
            .arg(missingColor().name())
            .arg(stats.incompleteGroups);
    }

    void calculateStats()
    {
        stats = CoverageStats();
        hasCalibrationInputs = false;
        if (!model)
            return;

        CalibrationMatcher matcher(model->groupingOptions(FrameType::Bias), model->groupingOptions(FrameType::Dark),
                                   model->groupingOptions(FrameType::Flat), model->calibrationMatchingPolicy());

        // Collect groups for matcher
        matcher.setBiasGroups(::collectBiasGroups(model));
        matcher.setDarkGroups(::collectDarkGroups(model));
        matcher.setFlatGroups(::collectFlatGroups(model));
        matcher.setOverrides(model->calibrationOverrides());

        const bool has_bias = !model->biasFrames().empty();
        const bool has_dark = !model->darkFrames().empty();
        const bool has_flat = !model->flatFrames().empty();
        hasCalibrationInputs = (has_bias || has_dark || has_flat || !model->calibrationOverrides().empty());

        const auto& lightGroups = model->lightGroups();
        const auto policy = model->calibrationMatchingPolicy();
        const bool warnOnFallback = policy.warnOnFallback;
        const auto& light_settings = model->lightCalibrationSettings();

        auto isUsableMatch = [](const CalibrationMatch& m) {
            return m.type != CalibrationMatch::MatchType::None &&
                   m.type != CalibrationMatch::MatchType::ManuallyUnassigned;
        };

        bool explicitDisableSeen = false;
        auto combine = [&](CalibrationMatch::MatchType& worstType, CalibrationMatch::MatchType t) {
            if (t == CalibrationMatch::MatchType::None)
            {
                worstType = CalibrationMatch::MatchType::None;
                return;
            }

            if (t == CalibrationMatch::MatchType::ManuallyUnassigned)
            {
                explicitDisableSeen = true;
                return;
            }

            if (worstType == CalibrationMatch::MatchType::None)
            {
                return;
            }

            if (t == CalibrationMatch::MatchType::ManualOverride)
            {
                worstType = CalibrationMatch::MatchType::ManualOverride;
                return;
            }

            if (worstType == CalibrationMatch::MatchType::ManualOverride)
            {
                return;
            }

            if (warnOnFallback &&
                (t == CalibrationMatch::MatchType::ClosestDate || t == CalibrationMatch::MatchType::ClosestExposure))
            {
                if (worstType == CalibrationMatch::MatchType::Exact)
                {
                    worstType = t;
                }
            }
        };

        for (const auto& group : lightGroups)
        {
            if (group.frames.empty())
                continue;

            // Skip coverage for groups that are explicitly marked as pre-calibrated (calibration is skipped).
            const auto itSettings = light_settings.find(group.key);
            if (itSettings != light_settings.end() && itSettings->second.preCalibrated)
            {
                continue;
            }

            stats.totalGroups++;

            if (!hasCalibrationInputs)
            {
                continue;
            }

            const LightGroupKey lightKey = LightGroupKey::fromFrame(group.frames.front());

            CalibrationMatch biasMatch = matcher.findBias(lightKey);
            CalibrationMatch darkMatch = matcher.findDark(lightKey);
            CalibrationMatch flatMatch = matcher.findFlat(lightKey);

            const bool bias_ok = has_bias ? isUsableMatch(biasMatch) : false;
            const bool dark_ok = has_dark ? isUsableMatch(darkMatch) : false;
            const bool flat_ok = has_flat ? isUsableMatch(flatMatch) : false;

            const bool bias_disabled = (biasMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned);
            const bool dark_disabled = (darkMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned);
            const bool flat_disabled = (flatMatch.type == CalibrationMatch::MatchType::ManuallyUnassigned);

            bool requirementsOk = true;

            // When calibration is intended (any calibration frames exist), a light group is considered
            // "complete" only when it has a usable Flat match and either Bias or Dark.
            if (hasCalibrationInputs)
            {
                if (!flat_ok && !flat_disabled)
                {
                    requirementsOk = false;
                }

                const bool bias_or_dark_ok = bias_ok || dark_ok;
                const bool bias_or_dark_disabled = bias_disabled || dark_disabled;
                if (!bias_or_dark_ok && !bias_or_dark_disabled)
                {
                    requirementsOk = false;
                }

                if (policy.subtractBiasFromDark)
                {
                    // CCD mode expects both Bias and Dark for (Dark - Bias) subtraction.
                    const bool bias_ok_or_disabled = bias_ok || bias_disabled;
                    const bool dark_ok_or_disabled = dark_ok || dark_disabled;
                    if (!(bias_ok_or_disabled && dark_ok_or_disabled))
                    {
                        requirementsOk = false;
                    }
                }
            }

            if (!requirementsOk)
            {
                stats.incompleteGroups++;
                continue;
            }

            CalibrationMatch::MatchType worstType = CalibrationMatch::MatchType::Exact;
            explicitDisableSeen = false;

            // Relevant matches only:
            // - Flat (if present)
            // - Standard mode: Dark preferred over Bias
            // - CCD mode: both Bias and Dark
            if (has_flat)
            {
                combine(worstType, flatMatch.type);
            }

            if (policy.subtractBiasFromDark)
            {
                if (has_dark)
                {
                    combine(worstType, darkMatch.type);
                }
                if (has_bias)
                {
                    combine(worstType, biasMatch.type);
                }
            }
            else
            {
                const bool consider_dark = has_dark && (dark_ok || dark_disabled);
                if (consider_dark)
                {
                    combine(worstType, darkMatch.type);
                }
                const bool consider_bias = has_bias && (!consider_dark || dark_disabled || !dark_ok);
                if (consider_bias)
                {
                    combine(worstType, biasMatch.type);
                }
            }

            if (worstType == CalibrationMatch::MatchType::ManualOverride)
            {
                stats.overrideGroups++;
            }
            else if (explicitDisableSeen || bias_disabled || dark_disabled || flat_disabled ||
                     (warnOnFallback && (worstType == CalibrationMatch::MatchType::ClosestDate ||
                                         worstType == CalibrationMatch::MatchType::ClosestExposure)))
            {
                stats.warningGroups++;
            }
            else
            {
                stats.completeGroups++;
            }
        }
    }
};

CalibrationCoverageWidget::CalibrationCoverageWidget(IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model))
{
    setObjectName(QStringLiteral("calibrationCoverageWidget"));
    setMinimumWidth(180);
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

CalibrationCoverageWidget::~CalibrationCoverageWidget() = default;

void CalibrationCoverageWidget::refresh()
{
    m_impl->calculateStats();
    setToolTip(m_impl->tooltipHtml());
    update();
}

QSize CalibrationCoverageWidget::sizeHint() const
{
    return QSize(220, 260);
}

void CalibrationCoverageWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();

    if (m_impl->stats.totalGroups == 0)
    {
        p.setPen(palette().placeholderText().color());
        p.drawText(rect(), Qt::AlignCenter, tr("No Light groups\nloaded."));
        return;
    }

    // --- Title ---
    p.setPen(palette().text().color());
    QFont titleFont = font();
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(0, 8, w, 20), Qt::AlignHCenter, tr("Calibration Coverage"));

    if (!m_impl->hasCalibrationInputs)
    {
        p.setPen(palette().placeholderText().color());
        p.setFont(font());
        p.drawText(QRect(0, 48, w, 120), Qt::AlignCenter,
                   tr("No calibration frames loaded.\nAdd Bias/Dark/Flat to compute coverage."));
        return;
    }

    // --- Pie Chart (centered) ---
    const int pieSize = qMin(w - 40, 120);
    const int pieX = (w - pieSize) / 2;
    const int pieY = 36;
    const QRect pieRect(pieX, pieY, pieSize, pieSize);

    auto drawSegment = [&](int count, const QColor& color, int& startAngle) {
        if (count <= 0)
            return;
        int spanAngle = (360 * 16 * count) / m_impl->stats.totalGroups;
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPie(pieRect, startAngle, spanAngle);
        startAngle += spanAngle;
    };

    int startAngle = 90 * 16; // Start at top
    drawSegment(m_impl->stats.completeGroups, m_impl->completeColor(), startAngle);
    drawSegment(m_impl->stats.warningGroups, m_impl->warningColor(), startAngle);
    drawSegment(m_impl->stats.overrideGroups, m_impl->overrideColor(), startAngle);
    drawSegment(m_impl->stats.incompleteGroups, m_impl->missingColor(), startAngle);

    // --- Percentage in center of pie ---
    const int covered = m_impl->stats.completeGroups + m_impl->stats.warningGroups + m_impl->stats.overrideGroups;
    int percent = covered * 100 / m_impl->stats.totalGroups;
    QFont percentFont = an::ui::tokens().typography.heading1;
    p.setFont(percentFont);
    p.setPen(palette().text().color());
    p.drawText(pieRect, Qt::AlignCenter, tr("%1%").arg(percent));

    // --- Legend (below pie) ---
    int legendY = pieY + pieSize + 16;
    const int legendX = 20;
    const int itemHeight = 20;

    p.setFont(font());
    auto drawLegendItem = [&](const QString& label, int count, const QColor& color) {
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(legendX, legendY + 3, 12, 12, an::ui::tokens().radius.sm, an::ui::tokens().radius.sm);

        p.setPen(palette().text().color());
        p.drawText(legendX + 18, legendY, w - legendX - 24, itemHeight, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1: %2").arg(label).arg(count));
        legendY += itemHeight;
    };

    drawLegendItem(tr("Complete"), m_impl->stats.completeGroups, m_impl->completeColor());
    drawLegendItem(tr("Warning"), m_impl->stats.warningGroups, m_impl->warningColor());
    drawLegendItem(tr("Override"), m_impl->stats.overrideGroups, m_impl->overrideColor());
    drawLegendItem(tr("Incomplete"), m_impl->stats.incompleteGroups, m_impl->missingColor());
}

bool CalibrationCoverageWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto* helpEvent = static_cast<QHelpEvent*>(event);
        const QString tooltip = m_impl->tooltipHtml();

        QToolTip::showText(helpEvent->globalPos(), tooltip);
        return true;
    }
    return QWidget::event(event);
}

} // namespace ImageIntegration
