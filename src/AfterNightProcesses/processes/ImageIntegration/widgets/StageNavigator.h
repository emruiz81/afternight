// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// How a person moves between the pages of Image Integration: the seam between its pages and what shows them.
///
/// Image Integration has eight pages - the four frame sources and calibration, registration, stacking and the
/// summary - and they are the same widgets whatever moves between them. In a window of its own they are held by two
/// levels of tabs (`TabStageNavigator`); as a page of a main window by a list of the job's stages with the stage's
/// page beside it (`StageListNavigator`, `widgets/StageListNavigator.h`). The process asks its navigator for a page,
/// for the page shown and to show another, and tells it what it has to say of each - how many frames a source holds,
/// what the validation found - and each navigator says it its own way: the tabs in their labels and their badges, the
/// list in its rows.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/ui_models/IntegrationStageStates.h"

#include <QObject>

#include <verdigris/wobjectdefs.h>

#include <array>

class QLabel;
class QTabWidget;
class QWidget;

W_REGISTER_ARGTYPE(ImageIntegration::StagePage)

namespace ImageIntegration
{

/// What moves between the pages of an Image Integration. A child of the process's widget; used on the GUI thread.
class AFTERNIGHTPROCESSES_API StageNavigator : public QObject
{
    W_OBJECT(StageNavigator)

public:
    explicit StageNavigator(QObject* parent = nullptr);
    ~StageNavigator() override;

    StageNavigator(const StageNavigator&) = delete;
    StageNavigator& operator=(const StageNavigator&) = delete;

    /// The navigator of the Image Integration a widget is part of - a page, or anything a page holds; null for a
    /// widget of no Image Integration.
    [[nodiscard]] static StageNavigator* of(const QWidget* part);

    /// What holds the pages and what moves between them.
    [[nodiscard]] virtual QWidget* widget() const = 0;

    /// A page: the same widget under any navigator.
    [[nodiscard]] virtual QWidget* page(StagePage page) const = 0;

    [[nodiscard]] virtual StagePage currentPage() const = 0;

    /// Shows a page, and tells it (`currentPageChanged`) when another was shown.
    virtual void setCurrentPage(StagePage page) = 0;

    /// How many frames each frame source holds, in the pages' order.
    virtual void setFrameCounts(const std::array<int, kFrameSourcePageCount>& counts) = 0;

    /// What the validation says of each page.
    virtual void setIssues(const std::array<StagePageIssues, kStagePageCount>& issues) = 0;

    /// Draws again what follows the look: icons, badges.
    virtual void refreshTheme() = 0;

    /// Told when another page is shown.
    void currentPageChanged(ImageIntegration::StagePage page) W_SIGNAL(currentPageChanged, page)
};

/// The two levels of tabs of Image Integration's window: the frame sources' tabs and the workflow's, under the two
/// tabs that hold them. A frame source's tab says how many frames it holds; a tab whose page has issues carries a badge
/// with their count, coloured by the gravest and spelled out in its tooltip, and the two first-level tabs the sum of
/// theirs. It moves none of the tabs' widgets: it is how the window's own tabs are asked.
class AFTERNIGHTPROCESSES_API TabStageNavigator final : public StageNavigator
{
    W_OBJECT(TabStageNavigator)

public:
    /// @param root the first level: the frame sources' tab, then the workflow's.
    /// @param sources the frame sources' tabs: bias, darks, flats, lights.
    /// @param workflow the workflow's tabs: calibration, registration, stacking, summary.
    TabStageNavigator(QTabWidget& root, QTabWidget& sources, QTabWidget& workflow, QObject* parent = nullptr);
    ~TabStageNavigator() override;

    [[nodiscard]] QWidget* widget() const override;
    [[nodiscard]] QWidget* page(StagePage page) const override;
    [[nodiscard]] StagePage currentPage() const override;
    void setCurrentPage(StagePage page) override;
    void setFrameCounts(const std::array<int, kFrameSourcePageCount>& counts) override;
    void setIssues(const std::array<StagePageIssues, kStagePageCount>& issues) override;
    void refreshTheme() override;

private:
    void tellCurrentPage();

    QTabWidget& m_root;
    QTabWidget& m_sources;
    QTabWidget& m_workflow;
    std::array<QLabel*, kStagePageCount> m_badges{};
    std::array<QLabel*, 2> m_parentBadges{};
    StagePage m_told = StagePage::Bias;
    bool m_moving = false;
};

} // namespace ImageIntegration
