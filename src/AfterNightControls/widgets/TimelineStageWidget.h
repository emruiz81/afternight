// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QFrame>
#include <verdigris/wobjectdefs.h>

class AFTERNIGHTCONTROLS_API TimelineStageWidget : public QFrame
{
    W_OBJECT(TimelineStageWidget)

public:
    enum class StageState
    {
        Disabled, // Stage not included in workflow (grayed out)
        Pending, // Stage not yet started
        Active, // Currently processing
        Completed // Stage finished
    };

    explicit TimelineStageWidget(QWidget* parent = nullptr);
    ~TimelineStageWidget() override = default;

    void setStageName(const QString& name);
    void setGroupInfo(const QString& info);
    void setElapsedTime(const QString& time);
    void setState(StageState state);

    StageState state() const
    {
        return m_state;
    }
    QString stageName() const
    {
        return m_stageName;
    }

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void updateTooltip();
    QPixmap getIconForState() const;
    QString getIconPathForState() const;
    QColor getIconColor() const;
    QLinearGradient getBackgroundGradient() const;

    QString m_stageName;
    QString m_groupInfo;
    QString m_elapsedTime;
    StageState m_state;
};
