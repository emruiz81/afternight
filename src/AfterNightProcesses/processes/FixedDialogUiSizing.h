// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QLayout>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <algorithm>

namespace an::ui::processes::fixed_dialog_ui
{

inline constexpr int kFieldControlMinHeight = 28;
inline constexpr int kSliderControlMinHeight = 24;
inline constexpr int kDialogButtonRowMinHeight = 34;

template <typename T> inline void ensureMinimumHeight(T* widget, int minimumHeight)
{
    if (!widget)
    {
        return;
    }

    if (widget->minimumHeight() < minimumHeight)
    {
        widget->setMinimumHeight(minimumHeight);
    }
}

inline void applyStandardControlHeights(QWidget* root)
{
    if (!root)
    {
        return;
    }

    for (auto* comboBox : root->findChildren<QComboBox*>())
    {
        ensureMinimumHeight(comboBox, kFieldControlMinHeight);
    }

    for (auto* spinBox : root->findChildren<QAbstractSpinBox*>())
    {
        ensureMinimumHeight(spinBox, kFieldControlMinHeight);
    }

    for (auto* slider : root->findChildren<QSlider*>())
    {
        ensureMinimumHeight(slider, kSliderControlMinHeight);
    }

    for (auto* buttonBox : root->findChildren<QDialogButtonBox*>())
    {
        ensureMinimumHeight(buttonBox, kDialogButtonRowMinHeight);
    }

    // Second pass: for every QWidget that uses absolute geometry (i.e. its
    // direct parent has no layout manager) and hosts an inner QLayout, expand
    // the widget's geometry so it is at least as tall/wide as the inner
    // layout's minimumSize().  This is style-agnostic — it accounts for
    // whatever spacing the active QStyle reports, so we never clip.
    for (auto* widget : root->findChildren<QWidget*>())
    {
        if (!widget || widget == root)
        {
            continue;
        }

        // Only geometry-managed widgets need fixing; skip layout-managed ones.
        const QWidget* parentWidget = widget->parentWidget();
        if (parentWidget && parentWidget->layout())
        {
            continue;
        }

        QLayout* innerLayout = widget->layout();
        if (!innerLayout)
        {
            continue;
        }

        innerLayout->invalidate();
        const QSize minSize = innerLayout->minimumSize();
        if (!minSize.isValid())
        {
            continue;
        }

        QRect geo = widget->geometry();
        bool changed = false;
        if (geo.width() < minSize.width())
        {
            geo.setWidth(minSize.width());
            changed = true;
        }
        if (geo.height() < minSize.height())
        {
            geo.setHeight(minSize.height());
            changed = true;
        }
        if (changed)
        {
            widget->setGeometry(geo);
        }
    }
}

inline void scheduleDialogResize(QDialog* dialog, const int delayMs = 0)
{
    if (!dialog)
    {
        return;
    }

    const QPointer<QDialog> guard(dialog);
    auto resizeFn = [guard]() {
        if (!guard)
        {
            return;
        }

        const int currentWidth =
            (std::max)(guard->width(), (std::max)(guard->sizeHint().width(), guard->minimumSizeHint().width()));
        const int oldMinimumWidth = guard->minimumWidth();
        const int oldMaximumWidth = guard->maximumWidth();

        guard->setMinimumWidth(currentWidth);
        guard->setMaximumWidth(currentWidth);

        for (auto* childLayout : guard->findChildren<QLayout*>())
        {
            childLayout->invalidate();
        }

        for (auto* childWidget : guard->findChildren<QWidget*>())
        {
            childWidget->updateGeometry();
        }

        int targetHeight = (std::max)(guard->sizeHint().height(), guard->minimumSizeHint().height());
        if (auto* layout = guard->layout())
        {
            layout->invalidate();
            layout->activate();
            targetHeight = (std::max)(layout->minimumSize().height(), layout->sizeHint().height());

            for (auto* scrollArea : guard->findChildren<QScrollArea*>(QString(), Qt::FindDirectChildrenOnly))
            {
                if (!scrollArea || !scrollArea->isVisible() || !scrollArea->widget())
                {
                    continue;
                }

                QWidget* content = scrollArea->widget();
                const int contentHeight = (std::max)(content->sizeHint().height(), content->minimumSizeHint().height());
                const int desiredScrollHeight = contentHeight + scrollArea->frameWidth() * 2;
                const int nonScrollHeight = guard->height() - scrollArea->height();
                if (nonScrollHeight > 0)
                {
                    targetHeight = (std::max)(layout->minimumSize().height(), nonScrollHeight + desiredScrollHeight);
                }
            }
        }

        guard->setMinimumWidth(oldMinimumWidth);
        guard->setMaximumWidth(oldMaximumWidth);
        guard->resize(currentWidth, targetHeight);
        guard->updateGeometry();
    };

    if (delayMs <= 0)
    {
        QTimer::singleShot(0, dialog, resizeFn);
    }
    else
    {
        QTimer::singleShot(delayMs, dialog, resizeFn);
    }
}

inline void connectSectionResizeBehavior(QToolButton* toggleButton, QDialog* dialog, const int animationDurationMs)
{
    if (!toggleButton || !dialog)
    {
        return;
    }

    QObject::connect(toggleButton, &QToolButton::toggled, dialog, [dialog, animationDurationMs](bool) {
        scheduleDialogResize(dialog, 0);
        scheduleDialogResize(dialog, animationDurationMs + 40);
    });
}

} // namespace an::ui::processes::fixed_dialog_ui
