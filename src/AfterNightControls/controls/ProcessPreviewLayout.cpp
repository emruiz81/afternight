// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The layout helpers of a process dialog with a real-time preview.

#include "controls/ProcessPreviewLayout.h"

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QSlider>
#include <QSplitter>
#include <QWidget>

#include <algorithm>
#include <vector>

namespace an::ui
{

namespace
{

/// Collects the first label of every slider row under a layout: a horizontal row holding a label, a spin box and a
/// slider.
void collectSliderRowLabels(QLayout* layout, std::vector<QLabel*>& labels)
{
    if (layout == nullptr)
    {
        return;
    }

    if (auto* rowLayout = qobject_cast<QHBoxLayout*>(layout))
    {
        QLabel* firstLabel = nullptr;
        bool hasSpinBox = false;
        bool hasSlider = false;

        for (int i = 0; i < rowLayout->count(); ++i)
        {
            auto* item = rowLayout->itemAt(i);
            if (item == nullptr)
            {
                continue;
            }

            if (QWidget* widget = item->widget())
            {
                if (firstLabel == nullptr)
                {
                    firstLabel = qobject_cast<QLabel*>(widget);
                }

                hasSpinBox = hasSpinBox || qobject_cast<QAbstractSpinBox*>(widget) != nullptr;
                hasSlider = hasSlider || qobject_cast<QSlider*>(widget) != nullptr;
            }
        }

        if (firstLabel != nullptr && hasSpinBox && hasSlider)
        {
            labels.push_back(firstLabel);
        }
    }

    for (int i = 0; i < layout->count(); ++i)
    {
        auto* item = layout->itemAt(i);
        if (item == nullptr)
        {
            continue;
        }

        if (QLayout* childLayout = item->layout())
        {
            collectSliderRowLabels(childLayout, labels);
        }

        if (QWidget* widget = item->widget())
        {
            if (QLayout* childWidgetLayout = widget->layout())
            {
                collectSliderRowLabels(childWidgetLayout, labels);
            }
        }
    }
}

} // namespace

QSplitter* findMainProcessSplitter(QBoxLayout* mainLayout)
{
    if (mainLayout == nullptr)
    {
        return nullptr;
    }

    for (int i = 0; i < mainLayout->count(); ++i)
    {
        if (auto* item = mainLayout->itemAt(i))
        {
            if (auto* widget = item->widget())
            {
                if (auto* splitter = qobject_cast<QSplitter*>(widget))
                {
                    return splitter;
                }

                if (auto* nested = widget->findChild<QSplitter*>(QStringLiteral("mainSplitter")))
                {
                    return nested;
                }
            }
        }
    }

    if (auto* host = mainLayout->parentWidget())
    {
        return host->findChild<QSplitter*>(QStringLiteral("mainSplitter"));
    }

    return nullptr;
}

void setPreviewPaneVisible(QSplitter* splitter, const bool visible)
{
    if (splitter == nullptr || splitter->count() < 2)
    {
        return;
    }

    if (auto* previewPane = splitter->widget(1))
    {
        if (visible)
        {
            previewPane->setMinimumWidth(kProcessMinPreviewPaneWidth);
            previewPane->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        else
        {
            previewPane->setMaximumWidth(0);
            previewPane->setMinimumWidth(0);
        }

        previewPane->setVisible(visible);
    }

    if (auto* handle = splitter->handle(1))
    {
        handle->setVisible(visible);
    }
}

void collapsePreviewPane(QSplitter* splitter)
{
    if (splitter == nullptr || splitter->count() < 2)
    {
        return;
    }

    if (auto* previewPane = splitter->widget(1))
    {
        // The splitter may give the pane no width even when it was set up with the shown pane's minimum.
        previewPane->setMinimumWidth(0);
    }

    // Dragging stays constrained; only this explicit collapse takes the pane to nothing.
    splitter->setCollapsible(1, true);

    const QList<int> currentSizes = splitter->sizes();
    const int left = currentSizes.size() > 0 ? currentSizes[0] : splitter->widget(0)->sizeHint().width();
    const int right = currentSizes.size() > 1 ? currentSizes[1] : 0;
    const int total = (std::max)(1, left + right);

    splitter->setSizes({total, 0});
}

QWidget* findPreviewPanelWidget(QLayout* previewLayout)
{
    return previewLayout != nullptr ? previewLayout->parentWidget() : nullptr;
}

void alignSliderRowLabels(QWidget* controlsPanel)
{
    if (controlsPanel == nullptr || controlsPanel->layout() == nullptr)
    {
        return;
    }

    std::vector<QLabel*> labels;
    collectSliderRowLabels(controlsPanel->layout(), labels);
    if (labels.empty())
    {
        return;
    }

    int maxLabelWidth = 0;
    for (QLabel* label : labels)
    {
        maxLabelWidth = (std::max)(maxLabelWidth, label->sizeHint().width());
    }
    if (maxLabelWidth <= 0)
    {
        return;
    }

    for (QLabel* label : labels)
    {
        label->setMinimumWidth(maxLabelWidth);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

QSize sanitizeMaxSize(const QSize& size)
{
    int width = size.width();
    int height = size.height();

    if (width <= 0 || width > QWIDGETSIZE_MAX)
    {
        width = QWIDGETSIZE_MAX;
    }

    if (height <= 0 || height > QWIDGETSIZE_MAX)
    {
        height = QWIDGETSIZE_MAX;
    }

    return QSize(width, height);
}

QRect centeredPreviewArea(const QRect& imageBounds, const QSize& requestedSize)
{
    if (imageBounds.isEmpty())
    {
        return {};
    }

    const int width = std::clamp(requestedSize.width() > 0 ? requestedSize.width() : kProcessDefaultPreviewAreaEdge, 1,
                                 imageBounds.width());
    const int height = std::clamp(requestedSize.height() > 0 ? requestedSize.height() : kProcessDefaultPreviewAreaEdge,
                                  1, imageBounds.height());

    QRect area(QPoint(0, 0), QSize(width, height));
    area.moveCenter(imageBounds.center());
    return area.intersected(imageBounds);
}

} // namespace an::ui
