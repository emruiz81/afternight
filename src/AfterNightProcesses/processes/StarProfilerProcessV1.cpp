// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The first star profiler's window over the one detector.

#include "stdafx.hpp"

#include "StarProfilerProcessV1.h"

#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "controls/StarsFilterControlV1.h"
#include "processes/ProcessCommonUtils.h"

#include <QTableWidgetItem>

#include <utility>

namespace
{

using an::ui::processes::FirstProfilerColumn;

/// How many decimals a column shows: two for a position, six for every figure.
[[nodiscard]] int decimalsOf(const FirstProfilerColumn column) noexcept
{
    return column == FirstProfilerColumn::X || column == FirstProfilerColumn::Y ? 2 : 6;
}

} // namespace

StarProfilerProcessV1::StarProfilerProcessV1(ImageViewControl* control)
    : StarProfilerWindow(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildStarProfilerProcessV1Ui(this);
    setProcessDescription(
        QStringLiteral("Detect, inspect, and curate star profiles with filtering and mask generation tools."));

    INIT_D_SPINBOX_CTRL(detectionThresholdControl, m_ui.detectionThresholdValue, m_ui.detectionThresholdSlider,
                        []() {});
    INIT_D_SPINBOX_CTRL(roundnessToleranceControl, m_ui.roundnessToleranceValue, m_ui.roundnessToleranceSlider,
                        []() {});
    INIT_SPINBOX_CTRL(minSizeControl, m_ui.minSizeValue, m_ui.minSizeSlider, []() {});
    INIT_SPINBOX_CTRL(maxSizeControl, m_ui.maxSizeValue, m_ui.maxSizeSlider, []() {});

    attachWidgets(an::ui::processes::StarProfilerWidgets{.table = m_ui.tableWidget,
                                                         .detect = m_ui.detectStarsButton,
                                                         .reset = m_ui.resetButton,
                                                         .load = m_ui.loadProfileButton,
                                                         .save = m_ui.saveProfileButton,
                                                         .clear = m_ui.clearProfileButton,
                                                         .filter = m_ui.starsFilterButton,
                                                         .mask = m_ui.createMaskButton,
                                                         .deleteStar = m_ui.deleteStarButton,
                                                         .detectedStars = m_ui.detectedStarsLabel,
                                                         .centerSelected = m_ui.centerSelectedStar,
                                                         .zoom = m_ui.zoomDetectedStars,
                                                         .buttons = m_ui.buttonBox});
    resetSettings();
    updateWindowTitle();
    refreshTheme();
}

StarProfilerProcessV1::~StarProfilerProcessV1() = default;

std::string StarProfilerProcessV1::getProcessNameStatic()
{
    return "StarProfilerV1";
}

bool StarProfilerProcessV1::requiresExclusiveAccessStatic()
{
    return true;
}

an::ui::processes::FirstProfilerSettings StarProfilerProcessV1::settings() const
{
    an::ui::processes::FirstProfilerSettings settings;
    settings.detectionThreshold = detectionThresholdControl->getValue();
    settings.roundnessTolerance = roundnessToleranceControl->getValue();
    settings.minimumSize = minSizeControl->getValue();
    settings.maximumSize = maxSizeControl->getValue();
    return settings;
}

const an::ui::processes::StarProfilerProcessV1UiElements& StarProfilerProcessV1::ui() const
{
    return m_ui;
}

void StarProfilerProcessV1::resetSettings()
{
    detectionThresholdControl->setValue(0.1);
    roundnessToleranceControl->setValue(2.0);
    minSizeControl->setValue(2);
    maxSizeControl->setValue(25);
}

void StarProfilerProcessV1::refreshTheme()
{
    StarProfilerWindow::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.deleteStarButton->setIcon(iconProvider->getIcon(QStringLiteral("close")));
    }
}

an::StarProfileGeneration StarProfilerProcessV1::profileGeneration() const
{
    return an::StarProfileGeneration::Version1;
}

QString StarProfilerProcessV1::profileFileFilter() const
{
    return QStringLiteral("Files (*.anspv1)");
}

QString StarProfilerProcessV1::profileFolderContext() const
{
    return QStringLiteral("__STAR_PROFILER_V1_FOLDER__");
}

std::string StarProfilerProcessV1::logName() const
{
    return "StarProfilerV1";
}

an::ui::processes::StarProfileWork StarProfilerProcessV1::detectionWork() const
{
    const an::ui::processes::FirstProfilerSettings asked = settings();
    return [asked](const an::ui::ExecutionScope& scope,
                   const an::ui::ProcessSource& source) -> an::Result<an::ui::processes::StarProfileResult> {
        an::Result<an::ui::processes::ProfileDetection> detection = an::ui::processes::detectProfileStars(
            scope, source, an::ui::processes::firstProfilerDetection(asked), false);
        if (!detection.hasValue())
        {
            return detection.status();
        }
        an::ui::StarList kept;
        kept.reserve(detection->stars.size());
        for (const an::DetectedStar& star : detection->stars)
        {
            if (an::ui::processes::firstProfilerKeeps(star, asked))
            {
                kept.push_back(star);
            }
        }
        an::ui::processes::StarProfileResult result;
        // The first layout shows its stars brightest first.
        result.stars =
            an::ui::processes::orderedForFirstProfiler(std::move(kept), FirstProfilerColumn::Intensity, true);
        result.header = an::ui::processes::firstProfileHeader(asked, detection->summary);
        return result;
    };
}

QStringList StarProfilerProcessV1::columnTitles() const
{
    return {QStringLiteral("x"),
            QStringLiteral("y"),
            QStringLiteral("intensity"),
            QStringLiteral("percentage"),
            QStringLiteral("deltaRadius"),
            QStringLiteral("quality"),
            QStringLiteral("radius"),
            QStringLiteral("meanRadius"),
            QStringLiteral("largeMajorAxis"),
            QStringLiteral("smallMajorAxis"),
            QStringLiteral("largeMinorAxis"),
            QStringLiteral("smallMinorAxis"),
            QStringLiteral("majorAxisAngle")};
}

void StarProfilerProcessV1::fillRow(TableWidget& table, const int row, const an::DetectedStar& star) const
{
    for (int column = 0; column < an::ui::processes::kFirstProfilerColumnCount; ++column)
    {
        const auto which = static_cast<FirstProfilerColumn>(column);
        table.setItem(row, column,
                      new QTableWidgetItem(an::ui::processes::figureText(
                          an::ui::processes::firstProfilerFigure(star, which), decimalsOf(which))));
    }
}

an::ui::StarList StarProfilerProcessV1::orderedBy(an::ui::StarList stars, const int column, const bool descending) const
{
    if (column < 0 || column >= an::ui::processes::kFirstProfilerColumnCount)
    {
        return stars;
    }
    return an::ui::processes::orderedForFirstProfiler(std::move(stars), static_cast<FirstProfilerColumn>(column),
                                                      descending);
}

StarsFilterControlBase* StarProfilerProcessV1::makeStarsFilter()
{
    return new StarsFilterControlV1(this);
}
