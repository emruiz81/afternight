// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/ImageIntegration/widgets/StageNavigator.h"

#include <QWidget>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(ImageIntegration::StageNavigator)

namespace ImageIntegration
{

StageNavigator::StageNavigator(QObject* parent) : QObject(parent) {}

StageNavigator::~StageNavigator() = default;

StageNavigator* StageNavigator::of(const QWidget* part)
{
    for (const QWidget* at = part; at != nullptr; at = at->parentWidget())
    {
        for (QObject* child : at->children())
        {
            if (auto* const navigator = qobject_cast<StageNavigator*>(child))
            {
                return navigator;
            }
        }
    }
    return nullptr;
}

} // namespace ImageIntegration
