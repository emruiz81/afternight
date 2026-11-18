// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The header of a task's page: which process works on which image, where its run stands, and the ways out; or, for a
/// task that holds a job, the job.

#include "AfterNightControlsExport.h"

#include <QAction>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <optional>
#include <vector>

class QAbstractButton;
class QEvent;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

class AnIconButton;

/// What the header of a task says when the task holds a job - a body of work with a name of its own, which a process
/// that fills its page whole keeps: an integration - in place of a process and its image. The process states it, and
/// states it again when it changes.
struct JobHeading
{
    /// The line over the name, drawn in capitals in the accent's text: what holds the job.
    QString eyebrow;

    /// The job's name.
    QString title;

    /// Its facts, on one line in the monospace: "718 frames - 11 groups - 20h 2m".
    QString facts;

    /// A measure of the job drawn at the header's right with a bar under it: the disk its runs write to.
    struct Meter
    {
        /// What is measured: "Disk space".
        QString label;

        /// The measure, in the monospace: "253.75 GB free - need ~62.56 GB".
        QString text;

        /// How much of the bar is filled, from 0 to 1: the share of what there is that the job needs.
        double share = 0.0;

        /// Whether the job needs more than there is: the bar is drawn in the error colour.
        bool exceeded = false;

        /// What the measure is made of, as the tooltip of its place.
        QString toolTip;

        [[nodiscard]] bool operator==(const Meter&) const = default;
    };
    std::optional<Meter> meter;

    /// An act of the job's own - Undo, Redo - shown as an icon button that follows the process's action.
    struct Act
    {
        QPointer<QAction> action;

        /// The key of the product's icon the button shows.
        QString iconKey;

        [[nodiscard]] bool operator==(const Act&) const = default;
    };
    std::vector<Act> acts;

    /// The job's primary act - Execute - shown as the header's primary button, which follows the process's action.
    QPointer<QAction> primary;

    [[nodiscard]] bool operator==(const JobHeading&) const = default;
};

/// A task's header: a strip high (34 / 40 px), the first surface, a hairline under it, a padding of 8 before and 6
/// after, its parts 8 apart. The target's thumbnail (22 px, radius 5); the process's name (the body's size and one up
/// in the comfortable density, semibold); "on" in the tertiary text; the target's full name (semibold, elided in the
/// middle) - or the process's name alone for a task with no single target; the state with a 13 px icon: "preview up to
/// date" with a check in the success colour, "applying - 42 %" in the accent's text and semibold, "applied 2.8 s ago",
/// "failed" in the error colour. Then a stretch, `Float as window` (`Attach` in a floating task) and `Close`; a full
/// page adds a menu button for the process's own menus.
///
/// During a run a 2 px bar of the accent lies along its foot: as long as the progress, or sliding across when the run
/// cannot tell how far it has come. Assistive technology reads it as one line: the process, the image and the state.
///
/// **A task that holds a job** (`setJob`) is said on three lines instead, in a header as high as they need with 8 px
/// above and below: the eyebrow in capitals in the accent's text, the job's name at 22 px in semibold, its facts in
/// the monospace in the secondary text, each elided at its end where it does not fit. At the right, in this order: the
/// job's measure - its label and its text over a bar of 180 x 4 px, filled by its share in the success colour, or in
/// the error colour once the job needs more than there is; the job's acts as icon buttons; the menu button; the
/// primary act as a primary button; a hairline; `Float as window` and `Close`. Each button of the job is its
/// process's action: enabled while it is, saying what it says, triggering it. Where the header is too narrow to leave
/// the name its least width beside the measure, the measure is left out. The run's bar lies along its foot as in a
/// process's header; the state's text is not drawn.
class AFTERNIGHTCONTROLS_API AnTaskHeader : public QWidget
{
    W_OBJECT(AnTaskHeader)

public:
    /// Where the task's run stands, as the header tells it.
    enum class State : std::uint8_t
    {
        /// Nothing to say.
        None,
        /// The preview shows the parameters: a check in the success colour.
        UpToDate,
        /// A run is on its way: the accent's text, and the bar.
        Running,
        /// The last run succeeded: the secondary text.
        Applied,
        /// The last run failed: the error colour.
        Failed
    };

    explicit AnTaskHeader(QWidget* parent = nullptr);
    ~AnTaskHeader() override;

    AnTaskHeader(const AnTaskHeader&) = delete;
    AnTaskHeader& operator=(const AnTaskHeader&) = delete;

    void setThumbnail(const QImage& thumbnail);
    [[nodiscard]] QImage thumbnail() const;

    void setProcessName(const QString& name);
    [[nodiscard]] QString processName() const;

    /// The target's full name; empty for a task with no single target, whose header names the process alone.
    void setTargetName(const QString& name);
    [[nodiscard]] QString targetName() const;

    /// The state and its text - "preview up to date", "applying - 42 %" - and, during a run, how far it has come.
    void setState(State state, const QString& text, std::optional<double> progress = std::nullopt);
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] std::optional<double> progress() const;

    /// Whether the task floats in a window of its own: its button attaches it then, and floats it otherwise.
    void setFloating(bool floating);
    [[nodiscard]] bool isFloating() const noexcept;

    /// Shows the menu button of a full page's own menus.
    void setMenuButtonVisible(bool visible);

    // ---- A job

    /// Says a job in place of a process and its image, or - with none - the process and its image again.
    void setJob(const std::optional<JobHeading>& job);
    [[nodiscard]] const std::optional<JobHeading>& job() const noexcept;

    /// The buttons of the job's acts, in their order; none without a job.
    [[nodiscard]] QList<QAbstractButton*> jobActButtons() const;

    /// The button of the job's primary act; null without one.
    [[nodiscard]] QAbstractButton* jobPrimaryButton() const noexcept;

    /// Where the bar of the job's measure is drawn; empty with no measure, and where it is left out for want of room.
    [[nodiscard]] QRect jobMeterRect() const;

    /// The job's name as it is drawn: elided at its end when it does not fit.
    [[nodiscard]] QString shownJobTitle() const;

    [[nodiscard]] AnIconButton* floatButton() const noexcept;
    [[nodiscard]] AnIconButton* closeButton() const noexcept;
    [[nodiscard]] AnIconButton* menuButton() const noexcept;

    /// Where the target's name is drawn, and the text drawn there: elided in the middle when it does not fit.
    [[nodiscard]] QString shownTargetName() const;

    /// Where the run's bar is drawn now; empty while no run is on its way.
    [[nodiscard]] QRect runBarRect() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Told when the person asks to float the task, or to attach it in a floating one.
void floatRequested() W_SIGNAL(floatRequested)

    /// Told when the person asks to close the task.
    void closeRequested() W_SIGNAL(closeRequested)

    /// Told when the person opens a full page's menus.
    void menuRequested() W_SIGNAL(menuRequested)

        protected : bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace an::ui
