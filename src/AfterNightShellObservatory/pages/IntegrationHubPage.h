// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The Integration hub: the place the rail's Integration shows - the ways to stack, the single steps and the
/// integration jobs.

#include "AfterNightShellObservatoryExport.h"

#include <QScrollArea>
#include <QString>
#include <verdigris/wobjectdefs.h>

#include <memory>
#include <vector>

class QAbstractButton;
class QComboBox;
class QLineEdit;
class QShowEvent;

namespace ImageIntegration
{
class IntegrationJobsModel;
} // namespace ImageIntegration

namespace an::ui
{
class AnTag;
class HomeText;
} // namespace an::ui

namespace an::ui::observatory
{

/// The page of the Integration place, which scrolls when the window is smaller than it: the heading and its line;
/// **three ways to stack** as large cards - the guided integration, shown as planned with its Start disabled, Image
/// Integration's New integration and Image Integration Lite's New quick integration; the **single steps** as four
/// compact cards - Image Calibration, Image Registration, Image Stacking and Image List Analyzer - each launching its
/// process; and the **integration jobs** of the model it is given, as a table - the job, its project, its frames, its
/// masters, its last run, how long it took and its status - the running one first with a thin bar of its progress, each
/// with Open (Show for a running one), searched by name or project and shown for every project, for none or for one.
/// Under the table, while the settings ask for it, a line says that a job run with no project open makes one in its
/// output folder (`setJobsGetAProject`).
///
/// What it asks for it asks in its signals, which are the window's to answer. Its sizes are the parts' (`kHub*`), its
/// colours the tokens' read where it paints.
class AFTERNIGHTSHELLOBSERVATORY_API IntegrationHubPage final : public QScrollArea
{
    W_OBJECT(IntegrationHubPage)

public:
    /// The processes the hub launches, by their registry's ids.
    static constexpr char kImageIntegration[] = "multi_image_stacking.actionImage_Integration";
    static constexpr char kImageIntegrationLite[] = "multi_image_stacking.actionImage_Integration_Lite";
    static constexpr char kImageCalibration[] = "sensor_calibration.actionImage_Calibration";
    static constexpr char kImageRegistration[] = "multi_image_stacking.actionImage_Registration";
    static constexpr char kImageStacking[] = "multi_image_stacking.actionImage_Stacking";
    static constexpr char kImageListAnalyzer[] = "multi_image_stacking.actionImage_List_Analyzer";

    /// Makes the page over the jobs, which outlive it.
    explicit IntegrationHubPage(ImageIntegration::IntegrationJobsModel& jobs, QWidget* parent = nullptr);
    ~IntegrationHubPage() override;

    /// Takes the icons the application's icon provider gives now.
    void refreshIcons();

    // ---- Its parts, for the window and the cases

    /// The three ways' buttons: the guided integration's Start, New integration and New quick integration.
    [[nodiscard]] QAbstractButton* guidedStart() const;
    [[nodiscard]] QAbstractButton* newIntegration() const;
    [[nodiscard]] QAbstractButton* newQuickIntegration() const;

    /// The guided integration's tag: "Planned".
    [[nodiscard]] an::ui::AnTag* guidedTag() const;

    /// The single steps' cards, in their order, each a button whose accessible name is its title.
    [[nodiscard]] std::vector<QAbstractButton*> steps() const;

    /// The jobs' search and the project they are shown for: All projects, No project, or one project by its name.
    [[nodiscard]] QLineEdit* jobSearch() const;
    [[nodiscard]] QComboBox* jobProjects() const;

    /// What the jobs' head counts: "5", or "5 · 1 running".
    [[nodiscard]] QString jobsCountText() const;

    /// The names of the jobs the table shows now, in its order.
    [[nodiscard]] QStringList shownJobs() const;

    /// The button of a job's row - Open, or Show for one that runs - while the table shows the row; null otherwise.
    [[nodiscard]] QAbstractButton* jobButton(const QString& databasePath, const QString& session) const;

    /// The status a job's row says, as its tag says it; empty while the table does not show the row.
    [[nodiscard]] QString jobStatusText(const QString& databasePath, const QString& session) const;

    /// Whether a job run with no project open is given one in its output folder: the line under the table says so
    /// while it is, and is not shown otherwise.
    void setJobsGetAProject(bool given);

    /// The line under the jobs' table; empty while it is not shown.
    [[nodiscard]] QString jobsProjectLine() const;

    /// A process is asked for: a way to stack or a single step, by its registry id.
    void processRequested(const QString& descriptorId) W_SIGNAL(processRequested, descriptorId);

    /// Open on a job: Image Integration on that job's session.
    void openJobRequested(const QString& databasePath, const QString& session)
        W_SIGNAL(openJobRequested, databasePath, session);

    /// Show on a job that runs: its task, brought forward.
    void showJobRequested(const QString& databasePath, const QString& session)
        W_SIGNAL(showJobRequested, databasePath, session);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void rebuildJobs();
    void refreshProjectChoices();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
