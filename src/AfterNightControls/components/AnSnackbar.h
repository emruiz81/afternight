// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A transient message: what just happened, with one thing to do about it, for a few seconds.

#include "AfterNightControlsExport.h"

#include <QIcon>
#include <QMargins>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

#include <chrono>
#include <functional>

class QAbstractButton;
class QEnterEvent;
class QEvent;
class QKeyEvent;
class QPaintEvent;
class QSpacerItem;
class QTimer;

namespace an::ui
{

/// What just happened - "Curves Transformation applied", "stars_only opened in Workspace 1" - as a bar of the message
/// colour, dark in both themes, a large control high with the controls' corners, standing on its elevation: an
/// optional icon and the text in the message's text, a detail in the monospace at the caption's size ("2.8 s"), and one
/// action ("Undo", "Show") in the accent drawn over an image, semibold. The widget is the bar and the room its shadow
/// is drawn in (`shadowMargins`); the part that holds it places it.
///
/// `present()` shows it for its lifetime, six seconds unless set. The lifetime pauses while the pointer is over the
/// message and runs on from what was left when it goes; when it has passed the message hides itself and says so
/// (`dismissed`). The action tells its press (`actionTriggered`) and dismisses the message; Esc dismisses it too. The
/// time is read from a clock, the steady clock unless a suite sets one. Assistive technology reads the message by its
/// text and its detail, and the action as a button named by its text.
class AFTERNIGHTCONTROLS_API AnSnackbar : public QWidget
{
    W_OBJECT(AnSnackbar)

public:
    /// What the lifetime is read from: the time since some fixed start.
    using Clock = std::function<std::chrono::milliseconds()>;

    /// How long a message stays when no other lifetime is set.
    static constexpr std::chrono::milliseconds kDefaultLifetime{6000};

    explicit AnSnackbar(QWidget* parent = nullptr);
    ~AnSnackbar() override;

    AnSnackbar(const AnSnackbar&) = delete;
    AnSnackbar& operator=(const AnSnackbar&) = delete;

    void setText(const QString& text);
    [[nodiscard]] QString text() const;

    /// A detail after the text in the monospace - a duration, a count - or none when empty.
    void setDetail(const QString& detail);
    [[nodiscard]] QString detail() const;

    /// An icon before the text, drawn in the message's text colour; none when null.
    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    /// The action's text; an empty text shows no action.
    void setActionText(const QString& text);
    [[nodiscard]] QString actionText() const;

    void setLifetime(std::chrono::milliseconds lifetime);
    [[nodiscard]] std::chrono::milliseconds lifetime() const noexcept;

    /// The clock the lifetime is read from. For the suites.
    void setClock(Clock clock);

    /// Shows the message and starts a whole lifetime.
    void present();

    /// Hides the message and says so; nothing when it is hidden.
    void dismiss();

    /// Whether the lifetime is held by the pointer over the message.
    [[nodiscard]] bool isPaused() const noexcept;

    /// What is left of the lifetime: all of it before the message is presented, none once it is dismissed.
    [[nodiscard]] std::chrono::milliseconds remainingLifetime() const;

    /// The action, a button.
    [[nodiscard]] QAbstractButton* actionButton() const noexcept;

    /// The room around the bar its shadow is drawn in.
    [[nodiscard]] QMargins shadowMargins() const;

    /// Where the bar lies in the widget.
    [[nodiscard]] QRect bodyRect() const;

    /// The bar and the room of its shadow.
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    /// Told when the action is pressed, before the message leaves.
void actionTriggered() W_SIGNAL(actionTriggered)

    /// Told when the message leaves: its lifetime passed, its action was pressed, or it was dismissed.
    void dismissed() W_SIGNAL(dismissed)

        protected : bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] std::chrono::milliseconds now() const;
    void schedule();
    void checkLifetime();
    /// Sizes the room of the painted parts and the margins around them from the tokens in force.
    void applyMetrics();
    void refreshAccessibleName();

    QString m_text;
    QString m_detail;
    QIcon m_icon;
    QAbstractButton* m_action = nullptr;
    QTimer* m_timer = nullptr;
    Clock m_clock;
    /// The room of the icon, the text and the detail, and the gap before the action.
    QSpacerItem* m_content = nullptr;
    std::chrono::milliseconds m_lifetime = kDefaultLifetime;
    /// What was left of the lifetime when it last started to run, and when that was.
    std::chrono::milliseconds m_remaining = kDefaultLifetime;
    std::chrono::milliseconds m_runningSince{0};
    bool m_running = false;
    bool m_paused = false;
};

} // namespace an::ui
