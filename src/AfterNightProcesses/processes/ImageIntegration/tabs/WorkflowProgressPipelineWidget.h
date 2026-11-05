// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QWidget>
#include <QToolButton>
#include <QVector>
#include <verdigris/wobjectdefs.h>

class WorkflowProgressPipelineWidget : public QWidget
{
    W_OBJECT(WorkflowProgressPipelineWidget)

public:
    enum class Stage
    {
        Frames,
        Calibration,
        Registration,
        Stacking
    };

    enum class StageState
    {
        Pending,
        Warning,
        Error,
        Ready
    };

    explicit WorkflowProgressPipelineWidget(QWidget* parent = nullptr);

    void setStageState(Stage stage, StageState state, const QString& label = QString());

    void stageClicked(Stage stage) W_SIGNAL(stageClicked, stage);

private:
    QToolButton* buttonForStage(Stage stage) const;
    void updateButton(QToolButton* button, StageState state, const QString& label) const;

    QVector<QToolButton*> m_buttons;
};
