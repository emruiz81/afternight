// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "ProgressDialogControlUiBuilder.h"
#include "session/ExecutionScope.h"

#include <QtWidgets/qdialog.h>

#include <stop_token>

/// The window a process shows while it runs: the run's progress, a second card for a sequence's overall progress, and
/// a Cancel button that stops the run.
///
/// The dialog is fed through `ExecutionObserver`s it makes, whose callbacks run on the GUI thread only while the dialog
/// lives, so a run that outlives it reports to nothing. Cancel stops the token the dialog lends; a run that takes the
/// token as its stop is stopped at its next checkpoint and publishes nothing.
class AFTERNIGHTCONTROLS_API ProgressDialogControl : public QDialog
{
public:
    ProgressDialogControl(QWidget* parent, const QString& title, bool twoStepProgress = false);

    void setValue(int value);
    void setValueAsync(int value);

    void setText(const QString& text);
    void setTextAsync(const QString& text);

    void setSeqValue(int value);
    void setSeqValueAsync(int value);

    void setSeqText(const QString& text);
    void setSeqTextAsync(const QString& text);

    /// Closes the dialog from any thread, through the event loop; nothing happens once it is gone.
    void closeAsync();

    void setValueCoeff(const float val);

    float getValueCoeff() const;

    void setValueOffset(const float val);

    float getValueOffset() const;

    void setSeqValueCoeff(const float val);

    float getSeqValueCoeff() const;

    bool isTwoStepProgress() const;

    void setProgressBarRange(int min, int max);

    /// Shows the run as work whose share done is not known - a busy bar - until a report states a share, which the
    /// dialog then shows as it shows every other.
    ///
    /// @param busy whether the share done is not known yet.
    void setBusy(bool busy);

    /// Whether the main card shows a busy bar.
    [[nodiscard]] bool isBusy() const noexcept;

    /// Stops the token the dialog lends, as Cancel does. Requesting twice requests once.
    void requestCancel() noexcept;

    /// Whether the run was asked to stop.
    [[nodiscard]] bool cancelRequested() const noexcept;

    /// The token Cancel stops; a run takes it as its stop.
    [[nodiscard]] std::stop_token stopToken() const noexcept;

    /// Where a run reports to show its progress in the main card.
    [[nodiscard]] an::ui::ExecutionObserver observer();

    /// Where a run reports to show its progress in the overall card of a two-step dialog.
    [[nodiscard]] an::ui::ExecutionObserver sequenceObserver();

    /// Where one step of several reports to show the whole's progress in the overall card: the steps before it complete
    /// and this one as far as it has come.
    ///
    /// @param step the zero-based step.
    /// @param steps how many steps there are; at least one.
    [[nodiscard]] an::ui::ExecutionObserver overallObserver(int step, int steps);

    /// Shows one progress report in the main card: its fraction, scaled and offset, and its stage as the card's title.
    void showProgress(const an::ui::ExecutionProgress& progress);

    /// Shows one progress report in the overall card.
    void showSequenceProgress(const an::ui::ExecutionProgress& progress);

    ~ProgressDialogControl() override;

private:
    void on_cancelButtonPushed();

    an::ui::controls::ProgressDialogControlUiElements m_ui;

    float m_value_coeff = 100.0f;

    float m_value_offset = 0.0f;

    float m_seq_value_coeff = 100.0f;

    bool m_TwoStepProgress = false;

    std::stop_source m_Stop;
};
