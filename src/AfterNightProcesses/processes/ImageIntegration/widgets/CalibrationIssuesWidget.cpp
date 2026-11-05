// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "CalibrationIssuesWidget.h"

#include "processes/ImageIntegration/model/IntegrationDataModel.h"
#include "processes/ImageIntegration/model/ValidationResult.h"
#include "theme/AnDesignTokens.h"

#include <QLabel>
#include <QVBoxLayout>

#include <array>
#include <map>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::CalibrationIssuesWidget)

namespace ImageIntegration
{

namespace
{

// Group similar issues by error code, calibration type, and message
struct IssueKey
{
    ValidationErrorCode code;
    std::optional<CalibrationFrameType> calibrationType;
    QString message;

    bool operator<(const IssueKey& other) const
    {
        if (code != other.code)
        {
            return static_cast<int>(code) < static_cast<int>(other.code);
        }
        // Compare optional calibration types
        if (calibrationType.has_value() != other.calibrationType.has_value())
        {
            return calibrationType.has_value() < other.calibrationType.has_value();
        }
        if (calibrationType.has_value() && *calibrationType != *other.calibrationType)
        {
            return static_cast<int>(*calibrationType) < static_cast<int>(*other.calibrationType);
        }
        return message < other.message;
    }
};

struct GroupedIssue
{
    ValidationIssue representative;
    int count = 0;
    QStringList sampleGroups;
};

} // anonymous namespace

class CalibrationIssuesWidget::Impl
{
public:
    CalibrationIssuesWidget* q;
    IntegrationDataModel* model;

    static constexpr int maxDisplayedIssues = 5;
    static constexpr int maxTooltipGroups = 5;

    QLabel* header = nullptr;
    QLabel* emptyLabel = nullptr;
    std::array<QLabel*, maxDisplayedIssues> issueLabels{};
    QLabel* moreLabel = nullptr;

    explicit Impl(CalibrationIssuesWidget* parent, IntegrationDataModel* m) : q(parent), model(m)
    {
        auto* layout = new QVBoxLayout(q);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        header = new QLabel(tr("Issues (0)"), q);
        header->setObjectName(QStringLiteral("lblIssuesHeader"));
        header->setFont(an::ui::tokens().typography.heading3);
        layout->addWidget(header);

        emptyLabel = new QLabel(tr("No calibration issues detected."), q);
        emptyLabel->setObjectName(QStringLiteral("lblIssuesEmpty"));
        layout->addWidget(emptyLabel);

        for (int i = 0; i < static_cast<int>(issueLabels.size()); ++i)
        {
            auto* label = new QLabel(tr("-"), q);
            label->setObjectName(QStringLiteral("lblIssue_%1").arg(i));
            label->setWordWrap(true);
            label->hide();
            issueLabels[static_cast<size_t>(i)] = label;
            layout->addWidget(label);
        }

        moreLabel = new QLabel(q);
        moreLabel->setObjectName(QStringLiteral("lblIssuesMore"));
        moreLabel->setProperty("anStatusTone", QStringLiteral("secondary"));
        QFont moreFont = an::ui::tokens().typography.caption;
        moreFont.setItalic(true);
        moreLabel->setFont(moreFont);
        moreLabel->hide();
        layout->addWidget(moreLabel);

        layout->addStretch(1);
        q->setObjectName(QStringLiteral("calibrationIssuesWidget"));
    }

    void updateIssues()
    {
        if (!model)
        {
            return;
        }

        const auto result = model->validate();
        std::vector<ValidationIssue> calibrationIssues;
        calibrationIssues.reserve(result.issues.size());

        for (const auto& issue : result.issues)
        {
            if (issue.category() == QStringLiteral("Calibration"))
            {
                calibrationIssues.push_back(issue);
            }
        }

        header->setText(tr("Issues (%1)").arg(static_cast<int>(calibrationIssues.size())));

        if (calibrationIssues.empty())
        {
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

        emptyLabel->hide();

        // Group issues by type to avoid repetitive messages
        std::map<IssueKey, GroupedIssue> groupedIssues;
        for (const auto& issue : calibrationIssues)
        {
            IssueKey key{issue.code, issue.missingCalibrationType, issue.message};
            auto& group = groupedIssues[key];
            if (group.count == 0)
            {
                group.representative = issue;
            }
            group.count++;

            const QString groupId = issue.filePath.trimmed();
            if (!groupId.isEmpty() && group.sampleGroups.size() < maxTooltipGroups)
            {
                // Avoid duplicates so the tooltip stays useful.
                if (!group.sampleGroups.contains(groupId))
                {
                    group.sampleGroups.push_back(groupId);
                }
            }
        }

        // Convert to vector for display
        std::vector<GroupedIssue> displayIssues;
        displayIssues.reserve(groupedIssues.size());
        for (const auto& [key, group] : groupedIssues)
        {
            displayIssues.push_back(group);
        }

        // Display grouped issues
        int displayedCount = 0;
        for (int i = 0; i < static_cast<int>(issueLabels.size()); ++i)
        {
            auto* label = issueLabels[static_cast<size_t>(i)];
            if (!label)
            {
                continue;
            }

            if (i < static_cast<int>(displayIssues.size()))
            {
                const auto& group = displayIssues[static_cast<size_t>(i)];
                const auto& issue = group.representative;

                QString text = QStringLiteral("%1 %2").arg(issue.severityIcon(), issue.message);

                QString toolTip = QStringLiteral("%1\n\n").arg(issue.message);

                if (group.count <= 1)
                {
                    const QString groupId = issue.filePath.trimmed();
                    if (!groupId.isEmpty())
                    {
                        toolTip += QStringLiteral("Group: %1\n").arg(groupId);
                    }
                }

                // Add count if multiple occurrences
                if (group.count > 1)
                {
                    text += QStringLiteral(" (%1 groups)").arg(group.count);

                    toolTip += QStringLiteral("Applies to %1 groups.\n").arg(group.count);

                    if (!group.sampleGroups.isEmpty())
                    {
                        toolTip += QStringLiteral("Examples:\n");
                        for (const auto& groupId : group.sampleGroups)
                        {
                            toolTip += QStringLiteral("  • %1\n").arg(groupId);
                        }
                        const int remaining = group.count - group.sampleGroups.size();
                        if (remaining > 0)
                        {
                            toolTip += QStringLiteral("  • …and %1 more\n").arg(remaining);
                        }
                    }
                }

                label->setText(text);
                label->setToolTip(toolTip);
                label->show();
                displayedCount++;
            }
            else
            {
                label->hide();
            }
        }

        // Show "more" label if there are more issue types than we can display
        if (static_cast<int>(displayIssues.size()) > maxDisplayedIssues)
        {
            int remaining = static_cast<int>(displayIssues.size()) - maxDisplayedIssues;
            moreLabel->setText(tr("...and %1 more issue type(s)").arg(remaining));
            moreLabel->show();
        }
        else
        {
            moreLabel->hide();
        }
    }
};

CalibrationIssuesWidget::CalibrationIssuesWidget(IntegrationDataModel* model, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>(this, model))
{
}

CalibrationIssuesWidget::~CalibrationIssuesWidget() = default;

void CalibrationIssuesWidget::refresh()
{
    m_impl->updateIssues();
}

} // namespace ImageIntegration
