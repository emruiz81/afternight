// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "FrameGroupingIssuesWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/GroupingKeys.h"
#include "processes/ImageIntegration/model/ValidationResult.h"
#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <map>
#include <set>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::FrameGroupingIssuesWidget)

namespace ImageIntegration
{

namespace
{

enum class IssueSeverity
{
    Error, // Blocking issues
    Warning, // Quality concerns
    Info // Informational
};

struct GroupingIssue
{
    IssueSeverity severity;
    QString message;
    QString tooltip;
    std::vector<GroupKey> affectedGroups;
};

QString severityIcon(IssueSeverity severity)
{
    switch (severity)
    {
    case IssueSeverity::Error:
        return QStringLiteral("❌");
    case IssueSeverity::Warning:
        return QStringLiteral("⚠️");
    case IssueSeverity::Info:
        return QStringLiteral("ℹ️");
    }
    return QStringLiteral("•");
}

QColor severityColor(IssueSeverity severity)
{
    switch (severity)
    {
    case IssueSeverity::Error:
        return an::ui::tokens().colors.statusError;
    case IssueSeverity::Warning:
        return an::ui::tokens().colors.statusWarning;
    case IssueSeverity::Info:
        return an::ui::tokens().colors.statusInfo;
    }
    return an::ui::tokens().colors.textPrimary;
}

} // anonymous namespace

class FrameGroupingIssuesWidget::Impl
{
public:
    FrameGroupingIssuesWidget* q;
    IntegrationDataModel* model;
    FrameType frameType;

    static constexpr int maxDisplayedIssues = 5;

    QLabel* header = nullptr;
    QLabel* emptyLabel = nullptr;
    std::array<QLabel*, maxDisplayedIssues> issueLabels{};
    QLabel* moreLabel = nullptr;

    std::vector<GroupingIssue> currentIssues;

    explicit Impl(FrameGroupingIssuesWidget* parent, FrameType type, IntegrationDataModel* m)
        : q(parent), model(m), frameType(type)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        header = new QLabel(QObject::tr("Issues (0)"), q);
        header->setObjectName(QStringLiteral("lblGroupingIssuesHeader"));
        header->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(header);

        emptyLabel = new QLabel(QObject::tr("No issues detected."), q);
        emptyLabel->setObjectName(QStringLiteral("lblGroupingIssuesEmpty"));
        layout->addWidget(emptyLabel);

        for (size_t i = 0; i < issueLabels.size(); ++i)
        {
            auto* label = new QLabel(QObject::tr("-"), q);
            label->setObjectName(QStringLiteral("lblGroupingIssue_%1").arg(static_cast<int>(i)));
            label->setWordWrap(true);
            label->hide();
            issueLabels[i] = label;
            layout->addWidget(label);
        }

        moreLabel = new QLabel(q);
        moreLabel->setObjectName(QStringLiteral("lblGroupingIssuesMore"));
        moreLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        moreLabel->setFont(an::ui::tokens().typography.caption);
        moreLabel->hide();
        layout->addWidget(moreLabel);

        layout->addStretch(1);
        q->setObjectName(QStringLiteral("frameGroupingIssuesWidget"));
    }

    void detectIssues()
    {
        currentIssues.clear();
        if (!model)
        {
            return;
        }

        // --- Calibration issues (Lights only) ---
        // Light groups may be marked with calibration warnings (e.g. missing dark/flat matches).
        // Those are modeled as workflow validation warnings; surface a summary here so the user
        // sees them without switching to the Calibration tab.
        if (frameType == FrameType::Light)
        {
            const WorkflowValidationResult validation = model->validate();

            int missingDarkOrBias = 0;
            int missingFlat = 0;
            int missingBias = 0;
            int missingDark = 0;

            int fallbackDark = 0;
            int fallbackFlat = 0;
            int fallbackBias = 0;

            int manuallyUnassigned = 0;

            for (const auto& issue : validation.issues)
            {
                if (issue.category() != QStringLiteral("Calibration"))
                {
                    continue;
                }

                switch (issue.code)
                {
                case ValidationErrorCode::MissingCalibrationMatch:
                    if (issue.missingCalibrationType.has_value())
                    {
                        switch (*issue.missingCalibrationType)
                        {
                        case CalibrationFrameType::DarkOrBias:
                            ++missingDarkOrBias;
                            break;
                        case CalibrationFrameType::Flat:
                            ++missingFlat;
                            break;
                        case CalibrationFrameType::Bias:
                            ++missingBias;
                            break;
                        case CalibrationFrameType::Dark:
                            ++missingDark;
                            break;
                        }
                    }
                    else
                    {
                        ++missingDarkOrBias;
                    }
                    break;

                case ValidationErrorCode::FallbackCalibrationMatch:
                    if (issue.missingCalibrationType.has_value())
                    {
                        switch (*issue.missingCalibrationType)
                        {
                        case CalibrationFrameType::Flat:
                            ++fallbackFlat;
                            break;
                        case CalibrationFrameType::Dark:
                            ++fallbackDark;
                            break;
                        case CalibrationFrameType::Bias:
                            ++fallbackBias;
                            break;
                        case CalibrationFrameType::DarkOrBias:
                            ++fallbackDark;
                            break;
                        }
                    }
                    else
                    {
                        ++fallbackDark;
                    }
                    break;

                case ValidationErrorCode::ManuallyUnassignedCalibration:
                    ++manuallyUnassigned;
                    break;

                case ValidationErrorCode::NoCalibrationFramesProvided:
                case ValidationErrorCode::RedundantCalibration:
                case ValidationErrorCode::Unknown:
                default:
                    break;
                }
            }

            if (missingDarkOrBias > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Warning;
                issue.message = QObject::tr("%n light group(s) missing dark/bias calibration", "", missingDarkOrBias);
                issue.tooltip = QObject::tr("Some light groups have no usable dark or bias calibration match.\n"
                                            "Check the Calibration tab to assign matches or add calibration frames.");
                currentIssues.push_back(std::move(issue));
            }
            if (missingFlat > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Warning;
                issue.message = QObject::tr("%n light group(s) missing flat calibration", "", missingFlat);
                issue.tooltip = QObject::tr("Some light groups have no usable flat calibration match.\n"
                                            "Check the Calibration tab to assign matches or add flat frames.");
                currentIssues.push_back(std::move(issue));
            }
            if (missingDark > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Warning;
                issue.message = QObject::tr("%n light group(s) missing dark calibration", "", missingDark);
                issue.tooltip = QObject::tr("Some light groups have no usable dark calibration match.\n"
                                            "Check the Calibration tab to assign matches or add dark frames.");
                currentIssues.push_back(std::move(issue));
            }
            if (missingBias > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Warning;
                issue.message = QObject::tr("%n light group(s) missing bias calibration", "", missingBias);
                issue.tooltip = QObject::tr("Some light groups have no usable bias calibration match.\n"
                                            "Check the Calibration tab to assign matches or add bias frames.");
                currentIssues.push_back(std::move(issue));
            }
            if ((fallbackDark + fallbackFlat + fallbackBias) > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Info;
                issue.message = QObject::tr("%n light group(s) using fallback calibration", "",
                                            (fallbackDark + fallbackFlat + fallbackBias));
                issue.tooltip =
                    QObject::tr("Some groups are using fallback calibration matches from a nearby session.\n"
                                "Review the Calibration tab if you want stricter matching.");
                currentIssues.push_back(std::move(issue));
            }
            if (manuallyUnassigned > 0)
            {
                GroupingIssue issue;
                issue.severity = IssueSeverity::Info;
                issue.message =
                    QObject::tr("%n light group(s) have manually disabled calibration", "", manuallyUnassigned);
                issue.tooltip = QObject::tr("Calibration was manually disabled for one or more groups.\n"
                                            "Check the Calibration tab to re-enable if needed.");
                currentIssues.push_back(std::move(issue));
            }
        }

        GroupedFrameView groups;
        switch (frameType)
        {
        case FrameType::Bias:
            groups = model->biasGroups();
            break;
        case FrameType::Dark:
            groups = model->darkGroups();
            break;
        case FrameType::Flat:
            groups = model->flatGroups();
            break;
        case FrameType::Light:
            groups = model->lightGroups();
            break;
        }

        // --- Grouping issues ---
        std::vector<GroupKey> singleFrameGroups;
        std::vector<GroupKey> mixedDimensionGroups;
        std::vector<GroupKey> highExclusionGroups;
        int totalExcluded = 0;
        int totalFrames = 0;

        for (const auto& group : groups)
        {
            if (group.frames.empty())
            {
                continue;
            }

            // Count included/excluded in this group
            int includedCount = 0;
            int excludedCount = 0;
            std::set<QString> dimensions;
            for (const auto& frame : group.frames)
            {
                if (frame.excluded)
                {
                    ++excludedCount;
                }
                else
                {
                    ++includedCount;
                }
                dimensions.insert(frame.metadata.resolutionString());
            }

            totalExcluded += excludedCount;
            totalFrames += static_cast<int>(group.frames.size());

            // Check for single-frame groups (insufficient for stacking)
            if (includedCount == 1)
            {
                singleFrameGroups.push_back(group.key);
            }

            // Check for mixed dimensions within group
            if (dimensions.size() > 1)
            {
                mixedDimensionGroups.push_back(group.key);
            }

            // High exclusion ratio (>50% excluded)
            const int groupTotal = includedCount + excludedCount;
            if (groupTotal > 2 && excludedCount > includedCount)
            {
                highExclusionGroups.push_back(group.key);
            }
        }

        // Build issue list
        if (!singleFrameGroups.empty())
        {
            GroupingIssue issue;
            issue.severity = IssueSeverity::Warning;
            issue.message =
                QObject::tr("%n group(s) have only 1 frame", "", static_cast<int>(singleFrameGroups.size()));
            issue.tooltip = QObject::tr("Groups with a single included frame cannot be stacked. "
                                        "Add more frames or these will be processed as-is.");
            issue.affectedGroups = singleFrameGroups;
            currentIssues.push_back(std::move(issue));
        }

        if (!mixedDimensionGroups.empty())
        {
            GroupingIssue issue;
            issue.severity = IssueSeverity::Error;
            issue.message =
                QObject::tr("%n group(s) have mixed dimensions", "", static_cast<int>(mixedDimensionGroups.size()));
            issue.tooltip = QObject::tr("Frames with different dimensions cannot be stacked together. "
                                        "Check your grouping settings or remove mismatched frames.");
            issue.affectedGroups = mixedDimensionGroups;
            currentIssues.push_back(std::move(issue));
        }

        if (!highExclusionGroups.empty())
        {
            GroupingIssue issue;
            issue.severity = IssueSeverity::Info;
            issue.message =
                QObject::tr("%n group(s) have >50%% excluded", "", static_cast<int>(highExclusionGroups.size()));
            issue.tooltip = QObject::tr("These groups have more excluded frames than included. "
                                        "Review exclusions to ensure important data isn't being skipped.");
            issue.affectedGroups = highExclusionGroups;
            currentIssues.push_back(std::move(issue));
        }

        // Overall exclusion info
        if (totalExcluded > 0 && totalFrames > 0)
        {
            GroupingIssue issue;
            issue.severity = IssueSeverity::Info;
            issue.message = QObject::tr("%1 of %2 frames excluded").arg(totalExcluded).arg(totalFrames);
            issue.tooltip = QObject::tr("Excluded frames are kept for reference but not used in processing.");
            currentIssues.push_back(std::move(issue));
        }
    }

    void updateDisplay()
    {
        header->setText(QObject::tr("Issues (%1)").arg(static_cast<int>(currentIssues.size())));

        if (currentIssues.empty())
        {
            const QString noIssuesTooltip = QObject::tr("No issues detected for the current frames/groups.");
            header->setToolTip(noIssuesTooltip);
            emptyLabel->setToolTip(noIssuesTooltip);

            emptyLabel->show();
            moreLabel->hide();
            for (auto* label : issueLabels)
            {
                if (label)
                {
                    label->hide();
                }
            }
            return;
        }

        // Clear the "no issues" tooltips once we have content.
        header->setToolTip(QString());
        emptyLabel->setToolTip(QString());

        emptyLabel->hide();

        // Display issues up to max
        for (size_t i = 0; i < issueLabels.size(); ++i)
        {
            auto* label = issueLabels[i];
            if (!label)
            {
                continue;
            }

            if (i < currentIssues.size())
            {
                const auto& issue = currentIssues[i];
                const QString text = QStringLiteral("%1 %2").arg(severityIcon(issue.severity), issue.message);
                label->setText(text);
                label->setToolTip(issue.tooltip);

                // Apply severity color to text
                const QColor color = severityColor(issue.severity);
                QPalette pal = label->palette();
                pal.setColor(QPalette::WindowText, color);
                label->setPalette(pal);

                label->show();
            }
            else
            {
                label->hide();
            }
        }

        // Show "and X more..." if needed
        if (currentIssues.size() > maxDisplayedIssues)
        {
            const int remaining = static_cast<int>(currentIssues.size()) - maxDisplayedIssues;
            moreLabel->setText(QObject::tr("...and %n more", "", remaining));
            moreLabel->show();
        }
        else
        {
            moreLabel->hide();
        }
    }
};

FrameGroupingIssuesWidget::FrameGroupingIssuesWidget(FrameType frameType, IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, frameType, model))
{
}

FrameGroupingIssuesWidget::~FrameGroupingIssuesWidget() = default;

void FrameGroupingIssuesWidget::setFrameType(FrameType type)
{
    m_impl->frameType = type;
    refresh();
}

FrameType FrameGroupingIssuesWidget::frameType() const
{
    return m_impl->frameType;
}

void FrameGroupingIssuesWidget::refresh()
{
    m_impl->detectIssues();
    m_impl->updateDisplay();
}

} // namespace ImageIntegration
