// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QWidget>

#include <functional>

/// Lightweight event filter for dashboard action card micro-interactions.
/// Separates hover/press/keyboard/context-menu logic from MainDashboard.
class ActionCardEventFilter final : public QObject
{
public:
    using InteractionCallback = std::function<void(QWidget* card, bool hoverActive, bool pressActive)>;
    using ClickCallback = std::function<void(QWidget* card)>;
    using FocusCallback = std::function<void(QWidget* card, int delta)>;
    using ContextMenuCallback = std::function<void(QWidget* card, const QPoint& globalPos)>;

    explicit ActionCardEventFilter(InteractionCallback interactionCb, ClickCallback clickCb, FocusCallback focusCb,
                                   ContextMenuCallback contextMenuCb, QObject* parent = nullptr)
        : QObject(parent), m_interactionCallback(std::move(interactionCb)), m_clickCallback(std::move(clickCb)),
          m_focusCallback(std::move(focusCb)), m_contextMenuCallback(std::move(contextMenuCb))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* cardWidget = qobject_cast<QWidget*>(watched);
        if (!cardWidget || !event)
        {
            return QObject::eventFilter(watched, event);
        }

        if (!cardWidget->property("dashboardActionId").isValid())
        {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type())
        {
        case QEvent::Enter: {
            m_interactionCallback(cardWidget, true, cardWidget->property("dashboardPressActive").toBool());
            return false;
        }
        case QEvent::Leave: {
            m_interactionCallback(cardWidget, false, false);
            return false;
        }
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_interactionCallback(cardWidget, true, true);
            }
            return false;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                const bool hoverActive = cardWidget->rect().contains(mouseEvent->pos());
                m_interactionCallback(cardWidget, hoverActive, false);
                if (hoverActive)
                {
                    m_clickCallback(cardWidget);
                }
            }
            return false;
        }
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            switch (keyEvent->key())
            {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                m_clickCallback(cardWidget);
                return true;
            case Qt::Key_Right:
            case Qt::Key_Down:
                m_focusCallback(cardWidget, 1);
                return true;
            case Qt::Key_Left:
            case Qt::Key_Up:
                m_focusCallback(cardWidget, -1);
                return true;
            default:
                return false;
            }
        }
        case QEvent::ContextMenu: {
            auto* contextEvent = static_cast<QContextMenuEvent*>(event);
            m_contextMenuCallback(cardWidget, contextEvent->globalPos());
            return true;
        }
        default:
            return QObject::eventFilter(watched, event);
        }
    }

private:
    InteractionCallback m_interactionCallback;
    ClickCallback m_clickCallback;
    FocusCallback m_focusCallback;
    ContextMenuCallback m_contextMenuCallback;
};
