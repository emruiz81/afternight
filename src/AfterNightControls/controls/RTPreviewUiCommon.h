// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QHBoxLayout>
#include <QLayout>
#include <QMargins>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "controls/RTPreviewHistogramToolbar.h"

#include <algorithm>

namespace an::ui::processes::rt_preview_ui
{

inline constexpr QMargins kDefaultPaneMargins(12, 8, 12, 8);
inline constexpr int kMinPreviewPaneWidth = 200;

struct SplitterPaneElements
{
    QSplitter* mainSplitter = nullptr;
    QWidget* controlsPanel = nullptr;
    QWidget* previewPanel = nullptr;
    QVBoxLayout* controlsLayout = nullptr;
    QVBoxLayout* previewLayout = nullptr;
};

inline SplitterPaneElements buildMainProcessSplitter(QHBoxLayout* rootLayout, QWidget* parent,
                                                     const QMargins& paneMargins = kDefaultPaneMargins)
{
    SplitterPaneElements elements;
    const bool keepExplicitPaneMargins = paneMargins != kDefaultPaneMargins;

    if (!rootLayout || !parent)
    {
        return elements;
    }

    elements.mainSplitter = new QSplitter(Qt::Horizontal, parent);
    elements.mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    elements.mainSplitter->setProperty("anSplitterVariant", QStringLiteral("process"));
    elements.mainSplitter->setChildrenCollapsible(false);

    elements.controlsPanel = new QWidget(elements.mainSplitter);
    elements.controlsPanel->setObjectName(QStringLiteral("controlsPanel"));

    elements.controlsLayout = new QVBoxLayout(elements.controlsPanel);
    elements.controlsLayout->setObjectName(QStringLiteral("verticalLayout"));
    elements.controlsLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    elements.controlsLayout->setContentsMargins(paneMargins);
    if (keepExplicitPaneMargins)
    {
        elements.controlsLayout->setProperty("anKeepLayoutMargins", true);
    }
    elements.controlsLayout->setSpacing(10);

    elements.previewPanel = new QWidget(elements.mainSplitter);
    elements.previewPanel->setObjectName(QStringLiteral("previewPanel"));

    elements.previewLayout = new QVBoxLayout(elements.previewPanel);
    elements.previewLayout->setObjectName(QStringLiteral("verticalLayout_2"));
    elements.previewLayout->setContentsMargins(paneMargins);
    elements.previewLayout->setProperty("anKeepPreviewLayoutMargins", true);

    auto* previewHost = new QWidget(elements.previewPanel);
    previewHost->setObjectName(QStringLiteral("previewHost"));
    elements.previewLayout->addWidget(previewHost);

    elements.previewPanel->setMinimumWidth(kMinPreviewPaneWidth);

    elements.mainSplitter->setStretchFactor(0, 0);
    elements.mainSplitter->setStretchFactor(1, 1);

    const int controlsWidth = elements.controlsPanel->sizeHint().width();
    const int previewWidth = (std::max)(1, parent->width() - controlsWidth);
    elements.mainSplitter->setSizes({controlsWidth, previewWidth});

    rootLayout->addWidget(elements.mainSplitter);

    return elements;
}

} // namespace an::ui::processes::rt_preview_ui
