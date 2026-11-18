// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Image Integration's pages under a list of the job's stages, as a page of a main window shows them.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/widgets/StageNavigator.h"

#include <QList>
#include <QPointer>
#include <QString>

#include <array>
#include <vector>

class QAbstractButton;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;

namespace an::ui
{
class AnStageList;
} // namespace an::ui

namespace ImageIntegration
{

/// The pages of Image Integration in three parts, left to right:
///
/// ```
/// body
/// |- column (216 px)   the eyebrow, the stage list, and at its foot the output folder and the project file's acts
/// '- pages (the rest)  the page of the stage selected; a page with a side panel keeps it at its right as an aside
/// ```
///
/// **The list** (`components/AnStageList.h`) holds a row for each frame source and each stage of the workflow, in the
/// order a run reaches them, and the summary last (`kStageRows`). Selecting a row shows its page; normalization and
/// drizzle, which have no page of their own, show the stacking page, where their settings are. A page asked for
/// (`setCurrentPage`) selects the row named as the page. The rows' marks and lines are given whole (`setRows`): the
/// navigator shows them and decides none.
///
/// **The foot** holds the job's output folder - the label, the field and the button the window's foot holds, moved
/// here - and the acts of the project file, each a button for an act or a menu of the process's own: import, export,
/// the recent projects, the presets and Reset Settings. A button is enabled as its act is, and says the act's tooltip.
///
/// **A run** on its way is shown where the stage's page is (`showRun`): its view takes the pages' place, and no row is
/// selected while it does. A row selected meanwhile shows its page, held so that it is looked at and not changed
/// (`setPagesHeld`), and the view is shown again when it is asked for (`showRunAgain`) or put away for good
/// (`dismissRun`).
///
/// The pages are the widgets it is given and it holds them for as long as it lives; nothing of them is changed.
class AFTERNIGHTPROCESSES_API StageListNavigator final : public StageNavigator
{
    W_OBJECT(StageListNavigator)

public:
    /// What the foot is made of: widgets and acts of the process's own.
    struct Foot
    {
        QLabel* outputLabel = nullptr;
        QLineEdit* outputEdit = nullptr;
        QAbstractButton* outputBrowse = nullptr;
        QAction* importProject = nullptr;
        QAction* exportProject = nullptr;
        QMenu* recentProjects = nullptr;
        QMenu* presets = nullptr;
        QAction* resetSettings = nullptr;
    };

    /// @param pages the eight pages, in the pages' order; each is taken into the navigator's own widget.
    /// @param current the page shown first.
    /// @param foot what the foot holds.
    /// @param parent the widget the navigator's own is made in.
    /// @param owner the process's widget, whose child the navigator is.
    StageListNavigator(const std::array<QWidget*, kStagePageCount>& pages, StagePage current, const Foot& foot,
                       QWidget* parent, QObject* owner);
    ~StageListNavigator() override;

    [[nodiscard]] QWidget* widget() const override;
    [[nodiscard]] QWidget* page(StagePage page) const override;
    [[nodiscard]] StagePage currentPage() const override;
    void setCurrentPage(StagePage page) override;

    /// The rows say the frames and the issues: these change nothing by themselves.
    void setFrameCounts(const std::array<int, kFrameSourcePageCount>& counts) override;
    void setIssues(const std::array<StagePageIssues, kStagePageCount>& issues) override;
    void refreshTheme() override;

    /// The rows' marks and lines, by their ids; the selection stays.
    void setRows(const std::vector<an::ui::StageRow>& rows);

    [[nodiscard]] an::ui::AnStageList* list() const noexcept;
    [[nodiscard]] QWidget* column() const noexcept;
    [[nodiscard]] QWidget* foot() const noexcept;

    /// The foot's buttons, in their order: import, export, the recent projects, the presets, Reset Settings.
    [[nodiscard]] QList<QAbstractButton*> footButtons() const;

    /// The row selected.
    [[nodiscard]] QString currentRow() const;

    /// Shows a run's view where the stage's page is, the view the navigator's from then on; no row is selected while
    /// it is shown, so that any row a person selects shows its page. A view shown before is put away first.
    void showRun(QWidget* runView);

    /// Shows the run's view again after a stage's page was looked at; nothing when there is none.
    void showRunAgain();

    /// Puts the run's view away and deletes it, and shows the page that was shown, its row selected again.
    void dismissRun();

    /// Whether the run's view is what the pages' place shows.
    [[nodiscard]] bool isRunShown() const;

    /// Makes the stages' pages and the foot wait - disabled, to be looked at and not changed - or gives them back. The
    /// list stays live either way: it moves between the pages.
    void setPagesHeld(bool held);

private:
    void rowSelected(const QString& rowId);

    QPointer<QWidget> m_body;
    QWidget* m_column = nullptr;
    an::ui::AnStageList* m_list = nullptr;
    QWidget* m_foot = nullptr;
    QStackedWidget* m_pages = nullptr;
    QList<QAbstractButton*> m_footButtons;
    std::array<QWidget*, kStagePageCount> m_page{};
    StagePage m_current = StagePage::Bias;
    QPointer<QWidget> m_runView;
};

} // namespace ImageIntegration
