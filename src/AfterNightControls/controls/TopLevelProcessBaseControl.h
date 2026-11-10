// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QPointer>
#include <QWidget>
#include <QString>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

class QBoxLayout;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QCloseEvent;

class AFTERNIGHTCONTROLS_API TopLevelProcessBaseControl : public QWidget
{
public:
    enum class ProcessMessageTone
    {
        Info = 0,
        Warning,
        Error
    };

    TopLevelProcessBaseControl(const std::string& processName);

    void initializeTopLevelProcess();

    // Called when the application theme changes to refresh icons, styles, etc.
    virtual void refreshTheme();
    void setModernShellEnabled(bool enabled);

    /// Mark this window as fixed-size: disables the maximise button and
    /// prevents the window manager from offering a resize handle.  Call
    /// this from the process constructor before the window is first shown.
    void setFixedSizeWindow(bool fixed);
    void setProcessDescription(const QString& description);
    void showProcessMessage(const QString& title, const QString& message,
                            ProcessMessageTone tone = ProcessMessageTone::Info,
                            std::optional<double> progressPercent = std::nullopt);
    void clearProcessMessage();

    /// The message the window's banner shows now, its title and text joined as `showProcessMessage` joins them; empty
    /// while the banner is hidden.
    [[nodiscard]] QString processMessage() const;

    /// The window's stop: requested when the window closes, when it is destroyed and when the application exits with
    /// it still open (`stopWorkInFlight`). Every run a window starts on the thread pool takes this token - beside a
    /// run's own stop, a progress window's Cancel - so it ends at its next stop point when the window goes, and no
    /// window has a stop of its own to request on each of those ways out. GUI thread.
    [[nodiscard]] std::stop_token windowStopToken() const noexcept;

    /// Stops what the window still runs on the thread pool, without waiting for it: the window's stop is requested,
    /// so a run's work ends at its next stop point and its result is not shown, and the window stops what it runs
    /// outside that token (`onStopWorkInFlight`). The application asks it of every window still open as it exits,
    /// before it drains the pool and deletes them (`an::ui::destroyProcessWindowsAtExit`). GUI thread.
    void stopWorkInFlight();

    /// States the user guide's page of the window's process, relative to the guide's root as the built guide names it
    /// (`processes/stretching/histogram-transformation.html`). The launcher states it on the window a launch shows,
    /// from the descriptor the launch is for.
    void setHelpPath(const QString& helpPath);

    /// The user guide's page of the window's process; empty when none was stated, and Help then opens the guide's
    /// home page.
    [[nodiscard]] QString helpPath() const;

    virtual ~TopLevelProcessBaseControl();

protected:
    /// Stops what the window runs that its stop token does not reach - a progress window, a request tracker - as
    /// closing the window stops it; `stopWorkInFlight` calls it after it requests the token. Nothing by default.
    virtual void onStopWorkInFlight() {}

    void closeEvent(QCloseEvent* evt) override;
    void keyPressEvent(QKeyEvent* evt) override;
    void showEvent(QShowEvent* evt) override;
    void paintEvent(QPaintEvent* evt) override;
    void resizeEvent(QResizeEvent* evt) override;

    virtual std::string serialize_json_settings()
    {
        return std::string();
    }

    virtual void deserialize_json_settings(const std::string& json_settings) {}

    /// Called after initializeModernProcessShell() replaces the top-level
    /// layout with the shell wrapper.  @p contentLayout is the new layout
    /// that now hosts the original layout items.
    virtual void onContentLayoutReplaced(QBoxLayout* contentLayout) {}

    // Scales fixed window-fit calculations independently by axis
    // (1.0 means no extra space).
    void setWindowSizeFitMultipliers(double widthMultiplier, double heightMultiplier);

    // Scales fixed window-fit calculations (1.0 means no extra space).
    void setWindowSizeFitMultiplier(double multiplier);

private:
    void initializeModernProcessShell();
    [[nodiscard]] double windowSizeFitMultiplierX() const;
    [[nodiscard]] double windowSizeFitMultiplierY() const;

    std::string m_tl_process_name;
    std::stop_source m_WindowStop;
    QString m_HelpPath;
};

namespace an::ui
{
/// The user guide's page Help opens from a widget: the page of the nearest process window at or above it that states
/// one, and nothing - the guide's home page - when there is none. GUI thread.
///
/// @param widget where Help was asked for - the widget with the keyboard focus; may be null.
/// @return the page, relative to the guide's root; empty for the home page.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString helpPathFor(const QWidget* widget);

/// Destroys the process windows still open as the application exits. A run's work calls its window's process body on
/// a worker, so no window is deleted under its work: each is asked to stop what it runs, the thread pool is drained
/// while every window is still there, and each is then deleted. The work their going leaves on the pool - a window's
/// read of its settings among it - is drained afterwards, so it finishes while the stores it reads are still there. A
/// window may close another as it goes, so each is held by a guard. GUI thread, once the event loop has ended.
AFTERNIGHTCONTROLS_API void
destroyProcessWindowsAtExit(const std::vector<QPointer<TopLevelProcessBaseControl>>& windows);
} // namespace an::ui
