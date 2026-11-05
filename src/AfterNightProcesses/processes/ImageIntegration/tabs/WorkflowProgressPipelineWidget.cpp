// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "WorkflowProgressPipelineWidget.h"

#include "processes/ProcessCommonUtils.h"
#include "theme/AnDesignTokens.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QStyle>

#include <verdigris/wobjectimpl.h>

W_REGISTER_ARGTYPE(WorkflowProgressPipelineWidget::Stage)

W_OBJECT_IMPL(WorkflowProgressPipelineWidget)

WorkflowProgressPipelineWidget::WorkflowProgressPipelineWidget(QWidget* parent) : QWidget(parent)
{
    const auto& spacing = an::ui::tokens().spacing;
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(spacing.md, spacing.md, spacing.md, spacing.md);
    layout->setSpacing(spacing.md);

    auto createButton = [this](const QString& text) {
        auto* button = new QToolButton(this);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setText(text);
        button->setAutoRaise(true);
        return button;
    };

    auto* btnFrames = createButton(tr("Frames"));
    auto* btnCalibration = createButton(tr("Calibration"));
    auto* btnRegistration = createButton(tr("Registration"));
    auto* btnStacking = createButton(tr("Stacking"));

    m_buttons = {btnFrames, btnCalibration, btnRegistration, btnStacking};

    auto addSeparator = [this, layout]() {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        line->setMinimumWidth(an::ui::tokens().spacing.xl);
        layout->addWidget(line);
    };

    layout->addWidget(btnFrames);
    addSeparator();
    layout->addWidget(btnCalibration);
    addSeparator();
    layout->addWidget(btnRegistration);
    addSeparator();
    layout->addWidget(btnStacking);
    layout->addStretch(1);

    QObject::connect(btnFrames, &QToolButton::clicked, [this]() { emit stageClicked(Stage::Frames); });
    QObject::connect(btnCalibration, &QToolButton::clicked, [this]() { emit stageClicked(Stage::Calibration); });
    QObject::connect(btnRegistration, &QToolButton::clicked, [this]() { emit stageClicked(Stage::Registration); });
    QObject::connect(btnStacking, &QToolButton::clicked, [this]() { emit stageClicked(Stage::Stacking); });
}

QToolButton* WorkflowProgressPipelineWidget::buttonForStage(Stage stage) const
{
    switch (stage)
    {
    case Stage::Frames:
        return m_buttons.value(0);
    case Stage::Calibration:
        return m_buttons.value(1);
    case Stage::Registration:
        return m_buttons.value(2);
    case Stage::Stacking:
        return m_buttons.value(3);
    }
    return nullptr;
}

void WorkflowProgressPipelineWidget::updateButton(QToolButton* button, StageState state, const QString& label) const
{
    if (!button)
    {
        return;
    }

    if (!label.isEmpty())
    {
        button->setText(label);
    }

    QIcon icon;
    const auto iconProvider = process_utils::currentIconProvider();

    switch (state)
    {
    case StageState::Ready:
        icon = iconProvider ? iconProvider->getIcon(QStringLiteral("check")) : QIcon();
        break;
    case StageState::Warning:
        icon = iconProvider ? iconProvider->getIcon(QStringLiteral("warning")) : QIcon();
        break;
    case StageState::Error:
        icon = iconProvider ? iconProvider->getIcon(QStringLiteral("priority_high")) : QIcon();
        break;
    case StageState::Pending:
    default:
        icon = iconProvider ? iconProvider->getIcon(QStringLiteral("info")) : QIcon();
        break;
    }

    button->setIcon(icon);
}

void WorkflowProgressPipelineWidget::setStageState(Stage stage, StageState state, const QString& label)
{
    updateButton(buttonForStage(stage), state, label);
}
