// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/widgets/StageNavigator.h"

#include "processes/ImageIntegration/ImageIntegrationProcessShared.h"
#include "processes/ImageIntegration/widgets/TabIssueBadge.h"

#include <QLabel>
#include <QPalette>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::StageNavigator)
W_OBJECT_IMPL(ImageIntegration::TabStageNavigator)

namespace ImageIntegration
{

namespace
{

/// The first level's two tabs.
constexpr int kSourcesTab = 0;
constexpr int kWorkflowTab = 1;

constexpr int index(const StagePage page) noexcept
{
    return static_cast<int>(page);
}

[[nodiscard]] QLabel* makeBadge(QTabBar* bar, const QString& objectName)
{
    auto* badge = new TabIssueBadge(bar);
    badge->setObjectName(objectName);
    badge->setVisible(false);
    badge->setAlignment(Qt::AlignCenter);
    badge->setProperty("anTabIssueBadge", true);
    applyBadgeSeverityStyle(badge, ValidationIssue::Severity::Warning);
    return badge;
}

/// Qt does not always measure a tab again when the widget it carries changes its visibility or its text: applying
/// each tab's text again makes it.
void measureTabsAgain(QTabWidget& tabs)
{
    for (int tab = 0; tab < tabs.count(); ++tab)
    {
        tabs.setTabText(tab, tabs.tabText(tab));
    }
    if (QTabBar* const bar = tabs.tabBar())
    {
        bar->updateGeometry();
        bar->update();
    }
    tabs.updateGeometry();
    tabs.update();
}

[[nodiscard]] ValidationIssue::Severity severityOf(const StagePageIssues& issues) noexcept
{
    return issues.errors ? ValidationIssue::Severity::Error : ValidationIssue::Severity::Warning;
}

} // namespace

StageNavigator::StageNavigator(QObject* parent) : QObject(parent) {}

StageNavigator::~StageNavigator() = default;

StageNavigator* StageNavigator::of(const QWidget* part)
{
    for (const QWidget* at = part; at != nullptr; at = at->parentWidget())
    {
        for (QObject* child : at->children())
        {
            if (auto* const navigator = qobject_cast<StageNavigator*>(child))
            {
                return navigator;
            }
        }
    }
    return nullptr;
}

TabStageNavigator::TabStageNavigator(QTabWidget& root, QTabWidget& sources, QTabWidget& workflow, QObject* parent)
    : StageNavigator(parent), m_root(root), m_sources(sources), m_workflow(workflow)
{
    QTabBar* const sourceBar = m_sources.tabBar();
    for (int tab = 0; tab < kFrameSourcePageCount; ++tab)
    {
        QLabel* const badge = makeBadge(sourceBar, QStringLiteral("childBadge_%1").arg(tab));
        sourceBar->setTabButton(tab, QTabBar::RightSide, badge);
        m_badges[static_cast<std::size_t>(tab)] = badge;
    }
    QTabBar* const workflowBar = m_workflow.tabBar();
    for (int tab = 0; tab < kStagePageCount - kFrameSourcePageCount; ++tab)
    {
        const int page = tab + kFrameSourcePageCount;
        QLabel* const badge = makeBadge(workflowBar, QStringLiteral("childBadge_%1").arg(page));
        workflowBar->setTabButton(tab, QTabBar::RightSide, badge);
        m_badges[static_cast<std::size_t>(page)] = badge;
    }
    QTabBar* const rootBar = m_root.tabBar();
    for (int tab = 0; tab < static_cast<int>(m_parentBadges.size()); ++tab)
    {
        QLabel* const badge = makeBadge(rootBar, QStringLiteral("parentBadge_%1").arg(tab));
        rootBar->setTabButton(tab, QTabBar::RightSide, badge);
        m_parentBadges[static_cast<std::size_t>(tab)] = badge;
    }
    refreshTheme();
    m_told = currentPage();
    for (QTabWidget* const tabs : {&m_root, &m_sources, &m_workflow})
    {
        connect(tabs, &QTabWidget::currentChanged, this, [this](int) { tellCurrentPage(); });
    }
}

TabStageNavigator::~TabStageNavigator() = default;

QWidget* TabStageNavigator::widget() const
{
    return &m_root;
}

QWidget* TabStageNavigator::page(const StagePage page) const
{
    const int at = index(page);
    return at < kFrameSourcePageCount ? m_sources.widget(at) : m_workflow.widget(at - kFrameSourcePageCount);
}

StagePage TabStageNavigator::currentPage() const
{
    if (m_root.currentIndex() == kSourcesTab)
    {
        return static_cast<StagePage>((std::max)(0, m_sources.currentIndex()));
    }
    return static_cast<StagePage>(kFrameSourcePageCount + (std::max)(0, m_workflow.currentIndex()));
}

void TabStageNavigator::setCurrentPage(const StagePage page)
{
    const int at = index(page);
    if (at < 0 || at >= kStagePageCount)
    {
        return;
    }
    // Two tabs may move for one page: the page between them is not told.
    m_moving = true;
    if (at < kFrameSourcePageCount)
    {
        m_root.setCurrentIndex(kSourcesTab);
        m_sources.setCurrentIndex(at);
    }
    else
    {
        m_root.setCurrentIndex(kWorkflowTab);
        m_workflow.setCurrentIndex(at - kFrameSourcePageCount);
    }
    m_moving = false;
    tellCurrentPage();
}

void TabStageNavigator::setFrameCounts(const std::array<int, kFrameSourcePageCount>& counts)
{
    const std::array<QString, kFrameSourcePageCount> names = {tr("Bias"), tr("Darks"), tr("Flats"), tr("Lights")};
    for (int tab = 0; tab < kFrameSourcePageCount; ++tab)
    {
        QString text = names[static_cast<std::size_t>(tab)];
        if (counts[static_cast<std::size_t>(tab)] > 0)
        {
            text += QStringLiteral(" (%1)").arg(counts[static_cast<std::size_t>(tab)]);
        }
        m_sources.setTabText(tab, text);
    }
}

void TabStageNavigator::setIssues(const std::array<StagePageIssues, kStagePageCount>& issues)
{
    bool measureAgain = false;
    for (int page = 0; page < kStagePageCount; ++page)
    {
        QLabel* const badge = m_badges[static_cast<std::size_t>(page)];
        if (badge == nullptr)
        {
            continue;
        }
        const StagePageIssues& said = issues[static_cast<std::size_t>(page)];
        const bool wasVisible = badge->isVisible();
        const QString oldText = badge->text();
        const QColor oldColour = badge->palette().color(QPalette::Window);
        if (said.count <= 0)
        {
            badge->setVisible(false);
            badge->setToolTip(QString());
            measureAgain = measureAgain || wasVisible;
            continue;
        }
        badge->setText(said.text);
        badge->setToolTip(said.toolTip);
        applyBadgeSeverityStyle(badge, severityOf(said));
        badge->setVisible(true);
        measureAgain = measureAgain || !wasVisible || oldText != badge->text() ||
                       oldColour != badge->palette().color(QPalette::Window);
    }

    // A first-level tab says the sum of its tabs' issues, coloured by the gravest, each tab's share in its tooltip.
    for (int parent = 0; parent < static_cast<int>(m_parentBadges.size()); ++parent)
    {
        QLabel* const badge = m_parentBadges[static_cast<std::size_t>(parent)];
        if (badge == nullptr)
        {
            continue;
        }
        const bool wasVisible = badge->isVisible();
        const QString oldText = badge->text();
        const int first = parent == kSourcesTab ? 0 : kFrameSourcePageCount;
        const int end = parent == kSourcesTab ? kFrameSourcePageCount : kStagePageCount;
        int total = 0;
        bool errors = false;
        QStringList lines;
        for (int page = first; page < end; ++page)
        {
            const StagePageIssues& said = issues[static_cast<std::size_t>(page)];
            if (said.count <= 0)
            {
                continue;
            }
            total += said.count;
            errors = errors || said.errors;
            QString name = parent == kSourcesTab ? m_sources.tabText(page) : m_workflow.tabText(page - first);
            // A frame source's tab says its frames after its name.
            if (const qsizetype counted = name.lastIndexOf(QStringLiteral(" (")); counted > 0)
            {
                name = name.left(counted);
            }
            lines << tr("• %1 %2 (%3)")
                         .arg(name, said.errors ? tr("Errors") : tr("Warnings"), QString::number(said.count));
        }
        if (total <= 0)
        {
            badge->setVisible(false);
            measureAgain = measureAgain || wasVisible;
            continue;
        }
        badge->setVisible(true);
        badge->setText(total > 99 ? QStringLiteral("99+") : QString::number(total));
        applyBadgeSeverityStyle(badge, errors ? ValidationIssue::Severity::Error : ValidationIssue::Severity::Warning);
        lines.prepend(total == 1 ? tr("1 Issue Found:") : tr("%1 Issues Found:").arg(total));
        badge->setToolTip(lines.join(QStringLiteral("\n")));
        measureAgain = measureAgain || !wasVisible || oldText != badge->text();
    }

    if (measureAgain)
    {
        measureTabsAgain(m_sources);
        measureTabsAgain(m_workflow);
        measureTabsAgain(m_root);
    }
}

void TabStageNavigator::refreshTheme()
{
    using detail::integrationIcon;
    m_sources.setTabIcon(index(StagePage::Bias), integrationIcon(QStringLiteral("image_linear")));
    m_sources.setTabIcon(index(StagePage::Darks), integrationIcon(QStringLiteral("image_linear")));
    m_sources.setTabIcon(index(StagePage::Flats), integrationIcon(QStringLiteral("image_linear")));
    m_sources.setTabIcon(index(StagePage::Lights), integrationIcon(QStringLiteral("camera")));
    m_workflow.setTabIcon(index(StagePage::Calibration) - kFrameSourcePageCount,
                          integrationIcon(QStringLiteral("settings")));
    m_workflow.setTabIcon(index(StagePage::Registration) - kFrameSourcePageCount,
                          integrationIcon(QStringLiteral("grid")));
    m_workflow.setTabIcon(index(StagePage::Stacking) - kFrameSourcePageCount, integrationIcon(QStringLiteral("stack")));
    m_workflow.setTabIcon(index(StagePage::Summary) - kFrameSourcePageCount,
                          integrationIcon(QStringLiteral("summary")));
}

void TabStageNavigator::tellCurrentPage()
{
    if (m_moving)
    {
        return;
    }
    const StagePage now = currentPage();
    if (now != m_told)
    {
        m_told = now;
        emit currentPageChanged(now);
    }
}

} // namespace ImageIntegration
