// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The header of a task's page: which process works on which image, where its run stands, and the ways out.

#include "AfterNightControlsExport.h"

#include <QImage>
#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <cstdint>
#include <optional>

class QEvent;
class QPaintEvent;
class QResizeEvent;

namespace an::ui
{

class AnIconButton;

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
