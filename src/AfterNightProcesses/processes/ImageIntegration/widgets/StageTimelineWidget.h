// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <memory>

enum class StageTimelineStatus
{
    Pending,
    InProgress,
    Completed,
    Skipped,
    Failed
};

class AFTERNIGHTPROCESSES_API StageTimelineWidget : public QWidget
{
    W_OBJECT(StageTimelineWidget)

public:
    explicit StageTimelineWidget(QWidget* parent = nullptr);
    ~StageTimelineWidget() override;

    void setStages(const QStringList& stages);
    void setCurrentStage(int index);
    void setStageStatus(int index, StageTimelineStatus status);
    void setStageElapsedTime(int index, qint64 milliseconds);

    [[nodiscard]] int currentStage() const;
    [[nodiscard]] StageTimelineStatus stageStatus(int index) const;
    [[nodiscard]] QPoint stageCenter(int index) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
