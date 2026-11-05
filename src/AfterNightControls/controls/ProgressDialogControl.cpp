// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "ProgressDialogControl.h"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <functional>
#include <optional>

namespace
{

void invokeProgressDialogAsync(ProgressDialogControl* instance,
                               const std::function<void(ProgressDialogControl*)>& callback)
{
    if (!instance || !callback)
    {
        return;
    }

    QMetaObject::invokeMethod(
        instance,
        [guard = QPointer<ProgressDialogControl>(instance), callback]() {
            if (guard.isNull())
            {
                return;
            }

            callback(guard.data());
        },
        Qt::QueuedConnection);
}

/// A report's fraction of its work, or nothing while its total is unknown.
[[nodiscard]] std::optional<double> fractionOf(const an::ui::ExecutionProgress& progress) noexcept
{
    if (progress.total == 0u)
    {
        return std::nullopt;
    }
    return (std::min)(1.0, static_cast<double>(progress.completed) / static_cast<double>(progress.total));
}

} // anonymous namespace

ProgressDialogControl::ProgressDialogControl(QWidget* parent, const QString& title, bool twoStepProgress)
    : QDialog(parent)
{
    m_ui = an::ui::controls::buildProgressDialogControlUi(this);

    this->setAttribute(Qt::WA_DeleteOnClose);

    Qt::WindowFlags flags = windowFlags();
    Qt::WindowFlags closeFlag = Qt::WindowCloseButtonHint;
    flags = flags & (~closeFlag);
    setWindowFlags(flags);

    m_TwoStepProgress = twoStepProgress;
    if (!m_TwoStepProgress)
    {
        m_ui.sequenceProgressCard->hide();
        setFixedSize(420, 180);
    }
    else
    {
        setFixedSize(420, 260);
    }

    QObject::connect(m_ui.cancelButton, &QPushButton::released, this, &ProgressDialogControl::on_cancelButtonPushed);

    setValue(0);
    setSeqValue(0);

    setWindowTitle(title);
}

void ProgressDialogControl::setValue(int value)
{
    m_ui.progressCard->setProgressValue(value);
}

void ProgressDialogControl::setValueAsync(int value)
{
    invokeProgressDialogAsync(this, [value](ProgressDialogControl* dialog) { dialog->setValue(value); });
}

void ProgressDialogControl::setText(const QString& text)
{
    m_ui.progressCard->setTitle(text);
}

void ProgressDialogControl::setTextAsync(const QString& text)
{
    invokeProgressDialogAsync(this, [text](ProgressDialogControl* dialog) { dialog->setText(text); });
}

void ProgressDialogControl::setSeqValue(int value)
{
    m_ui.sequenceProgressCard->setProgressValue(value);
}

void ProgressDialogControl::setSeqValueAsync(int value)
{
    invokeProgressDialogAsync(this, [value](ProgressDialogControl* dialog) { dialog->setSeqValue(value); });
}

void ProgressDialogControl::setSeqText(const QString& text)
{
    m_ui.sequenceProgressCard->setTitle(text);
}

void ProgressDialogControl::setSeqTextAsync(const QString& text)
{
    invokeProgressDialogAsync(this, [text](ProgressDialogControl* dialog) { dialog->setSeqText(text); });
}

void ProgressDialogControl::closeAsync()
{
    invokeProgressDialogAsync(this, [](ProgressDialogControl* dialog) { dialog->accept(); });
}

void ProgressDialogControl::setProgressBarRange(int min, int max)
{
    m_ui.progressCard->setProgressRange(min, max);
}

void ProgressDialogControl::setBusy(const bool busy)
{
    m_ui.progressCard->setBusy(busy);
}

bool ProgressDialogControl::isBusy() const noexcept
{
    return m_ui.progressCard->isBusy();
}

void ProgressDialogControl::setValueCoeff(const float val)
{
    m_value_coeff = val;
}

float ProgressDialogControl::getValueCoeff() const
{
    return m_value_coeff;
}

void ProgressDialogControl::setValueOffset(const float val)
{
    m_value_offset = val;
}

float ProgressDialogControl::getValueOffset() const
{
    return m_value_offset;
}

void ProgressDialogControl::setSeqValueCoeff(const float val)
{
    m_seq_value_coeff = val;
}

float ProgressDialogControl::getSeqValueCoeff() const
{
    return m_seq_value_coeff;
}

bool ProgressDialogControl::isTwoStepProgress() const
{
    return m_TwoStepProgress;
}

void ProgressDialogControl::requestCancel() noexcept
{
    m_Stop.request_stop();
}

bool ProgressDialogControl::cancelRequested() const noexcept
{
    return m_Stop.stop_requested();
}

std::stop_token ProgressDialogControl::stopToken() const noexcept
{
    return m_Stop.get_token();
}

an::ui::ExecutionObserver ProgressDialogControl::observer()
{
    an::ui::ExecutionObserver observer;
    observer.receiver = this;
    QPointer<ProgressDialogControl> guard(this);
    observer.progress = [guard](const an::ui::ExecutionProgress& progress) {
        if (guard)
        {
            guard->showProgress(progress);
        }
    };
    return observer;
}

an::ui::ExecutionObserver ProgressDialogControl::sequenceObserver()
{
    an::ui::ExecutionObserver observer;
    observer.receiver = this;
    QPointer<ProgressDialogControl> guard(this);
    observer.progress = [guard](const an::ui::ExecutionProgress& progress) {
        if (guard)
        {
            guard->showSequenceProgress(progress);
        }
    };
    return observer;
}

an::ui::ExecutionObserver ProgressDialogControl::overallObserver(const int step, const int steps)
{
    an::ui::ExecutionObserver observer;
    observer.receiver = this;
    QPointer<ProgressDialogControl> guard(this);
    const int stepCount = (std::max)(1, steps);
    const int stepIndex = std::clamp(step, 0, stepCount - 1);
    observer.progress = [guard, stepIndex, stepCount](const an::ui::ExecutionProgress& progress) {
        if (!guard)
        {
            return;
        }
        const double fraction = fractionOf(progress).value_or(0.0);
        const double overall = (static_cast<double>(stepIndex) + fraction) / static_cast<double>(stepCount);
        guard->setSeqValue(static_cast<int>(overall * guard->getSeqValueCoeff()));
        if (!progress.stage.empty())
        {
            guard->setSeqText(QString::fromStdString(progress.stage));
        }
    };
    return observer;
}

void ProgressDialogControl::showProgress(const an::ui::ExecutionProgress& progress)
{
    if (const std::optional<double> fraction = fractionOf(progress); fraction.has_value())
    {
        // A share the run states ends a busy bar: from here on the dialog shows it.
        setBusy(false);
        setValue(static_cast<int>(*fraction * m_value_coeff + m_value_offset));
    }
    if (!progress.stage.empty())
    {
        setText(QString::fromStdString(progress.stage));
    }
}

void ProgressDialogControl::showSequenceProgress(const an::ui::ExecutionProgress& progress)
{
    if (const std::optional<double> fraction = fractionOf(progress); fraction.has_value())
    {
        setSeqValue(static_cast<int>(*fraction * m_seq_value_coeff));
    }
    if (!progress.stage.empty())
    {
        setSeqText(QString::fromStdString(progress.stage));
    }
}

ProgressDialogControl::~ProgressDialogControl()
{
    // A run that still holds the token stops when the dialog that lent it goes.
    m_Stop.request_stop();
}

void ProgressDialogControl::on_cancelButtonPushed()
{
    m_ui.progressCard->setTitle(tr("Canceling execution..."));
    requestCancel();
    m_ui.cancelButton->setEnabled(false);
}
