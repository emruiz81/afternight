// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "TreeDisplayModeWidget.h"

#include <QRadioButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QButtonGroup>
#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(TreeDisplayModeWidget)

TreeDisplayModeWidget::TreeDisplayModeWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addWidget(new QLabel(tr("Display:"), this));

    m_hierarchicalRadio = new QRadioButton(tr("Hierarchical"), this);
    m_flatRadio = new QRadioButton(tr("Flat"), this);

    m_group = new QButtonGroup(this);
    m_group->addButton(m_hierarchicalRadio, static_cast<int>(GroupTreeModel::DisplayMode::Hierarchical));
    m_group->addButton(m_flatRadio, static_cast<int>(GroupTreeModel::DisplayMode::Flat));

    layout->addWidget(m_hierarchicalRadio);
    layout->addWidget(m_flatRadio);

    layout->addStretch();

    // Default
    m_flatRadio->setChecked(true);

    connect(m_group, &QButtonGroup::idToggled, this, &TreeDisplayModeWidget::onToggled);
}

TreeDisplayModeWidget::~TreeDisplayModeWidget() = default;

void TreeDisplayModeWidget::setModel(GroupTreeModel* model)
{
    if (m_model == model)
        return;

    if (m_model)
    {
        disconnect(m_model, &GroupTreeModel::displayModeChanged, this, nullptr);
    }

    m_model = model;

    if (m_model)
    {
        updateFromModel();
        connect(m_model, &GroupTreeModel::displayModeChanged, this, &TreeDisplayModeWidget::updateFromModel);
    }
}

void TreeDisplayModeWidget::onToggled(int id, bool checked)
{
    if (!checked)
        return;

    auto mode = static_cast<GroupTreeModel::DisplayMode>(id);
    emit displayModeChanged(mode);

    if (m_model && m_model->displayMode() != mode)
    {
        m_model->setDisplayMode(mode);
    }
}

void TreeDisplayModeWidget::updateFromModel()
{
    if (!m_model)
        return;

    QSignalBlocker blocker(m_group);
    auto mode = m_model->displayMode();
    if (mode == GroupTreeModel::DisplayMode::Hierarchical)
        m_hierarchicalRadio->setChecked(true);
    else
        m_flatRadio->setChecked(true);
}
