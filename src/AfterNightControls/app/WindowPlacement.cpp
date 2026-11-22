// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/WindowPlacement.h"

#include "theme/AnDesignTokens.h"

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QList>
#include <QObject>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

#include <algorithm>

namespace an::ui
{

QRect geometryWithinArea(const QRect& geometry, const QMargins& frame, const QSize& least, const QRect& area)
{
    // The room the area leaves the window's own rectangle once its frame is taken out.
    const QRect room = area.marginsRemoved(frame);
    if (room.width() < least.width() || room.height() < least.height())
    {
        return geometry;
    }
    const QSize size = geometry.size().boundedTo(room.size()).expandedTo(least);
    // Moved in only where it stands out: the left and top edges kept where the window still fits.
    const int left = std::clamp(geometry.left(), room.left(), room.right() + 1 - size.width());
    const int top = std::clamp(geometry.top(), room.top(), room.bottom() + 1 - size.height());
    return QRect(QPoint(left, top), size);
}

void holdWithinAvailableArea(QWidget& window)
{
    const QScreen* screen = window.screen();
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr)
    {
        return;
    }
    // The frame the window system draws around the window: known once its native window exists.
    QMargins frame;
    if (const QWindow* const native = window.windowHandle())
    {
        frame = native->frameMargins();
    }
    const QRect held = geometryWithinArea(window.geometry(), frame, window.minimumSize(), screen->availableGeometry());
    if (held != window.geometry())
    {
        window.setGeometry(held);
    }
}

namespace
{

/// Lets a part take what it needs where a size stated for it is smaller: a least size under the need is dropped, so
/// that the part's own least size holds whatever the density; a greatest size under the need is lifted, and a part
/// that was held to one width keeps the width it asks for.
void letTakeWhatItNeeds(QWidget& part, const QSize& needs)
{
    const QSize stated = part.minimumSize();
    if (needs.width() > 0 && part.maximumWidth() < needs.width())
    {
        const bool oneWidth = stated.width() == part.maximumWidth();
        part.setMaximumWidth(QWIDGETSIZE_MAX);
        if (oneWidth)
        {
            QSizePolicy policy = part.sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Fixed);
            part.setSizePolicy(policy);
        }
    }
    if (needs.height() > 0 && part.maximumHeight() < needs.height())
    {
        part.setMaximumHeight(QWIDGETSIZE_MAX);
    }
    const bool dropWidth = needs.width() > 0 && stated.width() > 0 && stated.width() < needs.width();
    const bool dropHeight = needs.height() > 0 && stated.height() > 0 && stated.height() < needs.height();
    if (!dropWidth && !dropHeight)
    {
        return;
    }
    // A least size set to nothing in one direction alone still counts as stated in it, and a layout leaves a
    // stated size as it is - a window would then have no least height at all. Both are cleared, and the one that
    // stands is stated again: for a window, only where it is more than its layout asks, which is how a stated
    // one is told from the one its layout gave it.
    part.setMinimumSize(0, 0);
    if (!dropWidth && stated.width() > 0 && (!part.isWindow() || stated.width() > needs.width()))
    {
        part.setMinimumWidth(stated.width());
    }
    if (!dropHeight && stated.height() > 0 && (!part.isWindow() || stated.height() > needs.height()))
    {
        part.setMinimumHeight(stated.height());
    }
}

/// Applies the layouts of a window's parts now, the innermost first, and then the window's own. A part whose needs
/// changed tells the layout that holds it by a posted request, one level at a time: left to that, the window's
/// least size would be worked out from what its parts needed before.
void applyLayoutsFromTheInside(QWidget& window)
{
    const QList<QWidget*> parts = window.findChildren<QWidget*>();
    for (auto part = parts.crbegin(); part != parts.crend(); ++part)
    {
        if (QLayout* const layout = (*part)->isWindow() ? nullptr : (*part)->layout())
        {
            layout->activate();
        }
    }
    if (QLayout* const layout = window.layout())
    {
        layout->activate();
    }
}

/// Keeps a window's least height as high as its rows need at its least width, as what it shows changes. Qt works a
/// window's least size out without the height its layout needs for a width, so where text wraps - a heading's
/// description, a card of values - the rows were squeezed under their lines at the window's least size, by a few
/// pixels each in a font whose lines are taller (Noto Sans). Owned by the window.
class LeastHeightOfTheLines final : public QObject
{
public:
    LeastHeightOfTheLines(QWidget& window, const int stated) : QObject(&window), m_window(window), m_stated(stated)
    {
        window.installEventFilter(this);
        apply();
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // A part's needs changed: the layout is worked out again as the window handles the request, after this.
        if (watched == &m_window && event->type() == QEvent::LayoutRequest)
        {
            apply();
        }
        return false;
    }

private:
    void apply()
    {
        QLayout* const layout = m_window.layout();
        if (layout == nullptr)
        {
            return;
        }
        const QSize least = layout->totalMinimumSize();
        int needs = least.height();
        if (layout->hasHeightForWidth())
        {
            // The least height for the width: a part whose height follows its width at that height, any other at its
            // least size - a scroll area at its own, not at what it holds.
            const QMargins margins = m_window.contentsMargins();
            const int width = (std::max)(m_window.minimumWidth(), least.width()) - margins.left() - margins.right();
            needs = (std::max)(needs, layout->minimumHeightForWidth(width) + margins.top() + margins.bottom());
        }
        const int height = (std::max)(m_stated, needs);
        if (height != m_window.minimumHeight())
        {
            m_window.setMinimumHeight(height);
        }
    }

    QWidget& m_window;
    int m_stated = 0;
};

/// Sizes a window's parts for the look in force. A window drawn for AfterNight 1.1 states sizes in pixels - a field
/// 60 wide, a row 28 high, a window no smaller than 500 by 380 - that the Observatory profile's font and its
/// densities outgrow: a field, a button with a text, a part that lays out others and the window itself each take
/// what they need where what was stated is less. The innermost parts come first, so that a part that lays out
/// others is asked what it needs once they have taken what they need.
void fitToTheLook(QWidget& window)
{
    const QList<QWidget*> parts = window.findChildren<QWidget*>();
    for (auto each = parts.crbegin(); each != parts.crend(); ++each)
    {
        QWidget* const part = *each;
        const QWidget* const holder = part->parentWidget();
        // What a field lays out inside itself is the field's own.
        if (part->isWindow() || qobject_cast<const QLineEdit*>(holder) != nullptr ||
            qobject_cast<const QAbstractSpinBox*>(holder) != nullptr)
        {
            continue;
        }
        if (const auto* const spin = qobject_cast<const QAbstractSpinBox*>(part))
        {
            letTakeWhatItNeeds(*part, spin->minimumSizeHint());
        }
        else if (const auto* const combo = qobject_cast<const QComboBox*>(part))
        {
            // A combo box keeps the least width it worked out when it was first asked, which for one filled since
            // is the width of an empty box: a layout then squeezes it under its longest item. Told its style
            // changed, it works both out again.
            if (combo->minimumContentsLength() == 0 && combo->minimumSizeHint().width() < combo->sizeHint().width())
            {
                QEvent styleChange(QEvent::StyleChange);
                QCoreApplication::sendEvent(part, &styleChange);
                // The layout that holds it keeps what it was told before, until the box says it changed.
                part->updateGeometry();
            }
            letTakeWhatItNeeds(*part, QSize(combo->sizeHint().width(), combo->minimumSizeHint().height()));
        }
        else if (const auto* const button = qobject_cast<const QAbstractButton*>(part))
        {
            // A button that shows an icon alone is the size it was drawn at.
            const auto* const tool = qobject_cast<const QToolButton*>(part);
            const bool iconAlone =
                button->text().isEmpty() || (tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonIconOnly);
            if (!iconAlone)
            {
                letTakeWhatItNeeds(*part, button->minimumSizeHint());
            }
        }
        else if (qobject_cast<const QLineEdit*>(part) != nullptr)
        {
            letTakeWhatItNeeds(*part, QSize(0, part->minimumSizeHint().height()));
        }
        else if (const auto* const label = qobject_cast<const QLabel*>(part))
        {
            // A label that does not wrap shows its text whole only as wide as the text: a width stated under it
            // (1.1's "Output File Name", 100 px) cut the text short. A label that elides by design says so in its
            // own least size.
            if (!label->wordWrap() && !label->text().isEmpty())
            {
                letTakeWhatItNeeds(*part, QSize(label->minimumSizeHint().width(), 0));
            }
        }
        else if (QLayout* const layout = part->layout())
        {
            // Its layout's least size and what the part draws round it - a group box, its title - now that what
            // it lays out has changed.
            layout->activate();
            letTakeWhatItNeeds(*part, part->minimumSizeHint());
        }
    }
    applyLayoutsFromTheInside(window);
    if (QLayout* const layout = window.layout())
    {
        letTakeWhatItNeeds(window, layout->totalMinimumSize());
        // The window's least size is its layout's where none is stated: worked out again now, and the window brought
        // up to it.
        layout->invalidate();
        layout->activate();
        // A least height still stated is more than the layout asks; one the layout gave follows the layout.
        const int stated = window.minimumHeight() > layout->totalMinimumSize().height() ? window.minimumHeight() : 0;
        new LeastHeightOfTheLines(window, stated);
    }
}

/// The scroll area a window's body scrolls in; null for a window with none.
[[nodiscard]] const QScrollArea* bodyOf(const QWidget& window)
{
    const auto* const body = window.findChild<QScrollArea*>(QString::fromLatin1(kWindowBodyName));
    return body != nullptr && body->widget() != nullptr && window.layout() != nullptr ? body : nullptr;
}

/// How much higher a window whose body scrolls has to be to show the body whole - less than nothing where it shows
/// more than the body - and nothing for a window with no such body.
[[nodiscard]] int heightTheBodyLacks(QWidget& window)
{
    const QScrollArea* const body = bodyOf(window);
    return body != nullptr ? windowBodyHeightNeeded(window) - body->viewport()->height() : 0;
}

/// Gives a window whose body scrolls the height that shows the body whole: a scroll area asks for a few lines'
/// height whatever it holds, so the window would open with a bar where its screen has room for all of it. The hold
/// that follows bounds the height to the screen, and the body scrolls there.
void showBodyWhole(QWidget& window)
{
    const int lacks = heightTheBodyLacks(window);
    if (lacks > 0)
    {
        window.resize(window.width(), window.height() + lacks);
    }
}

/// Holds its window within its screen at the window's first show, and goes.
class FirstShowHold final : public QObject
{
public:
    explicit FirstShowHold(QWidget& window) : QObject(&window)
    {
        window.installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && !event->spontaneous())
        {
            // The show event comes before the window is put on the screen: its size and its place are decided, and
            // nothing of it has been seen where it will not be.
            auto* const window = qobject_cast<QWidget*>(watched);
            if (window != nullptr && window->isWindow())
            {
                if (styleProfile() == StyleProfile::Observatory)
                {
                    fitToTheLook(*window);
                    showBodyWhole(*window);
                }
                holdWithinAvailableArea(*window);
            }
            watched->removeEventFilter(this);
            deleteLater();
        }
        return false;
    }
};

} // namespace

int windowBodyHeightNeeded(QWidget& window)
{
    const QScrollArea* const body = bodyOf(window);
    if (body == nullptr)
    {
        return 0;
    }
    applyLayoutsFromTheInside(window);
    const QWidget* const content = body->widget();
    // At the width the body has with no bar beside it: shown whole, it has none.
    const int width = body->width() - 2 * body->frameWidth();
    const int needs = content->hasHeightForWidth() ? content->heightForWidth(width) : content->sizeHint().height();
    return (std::max)(needs, content->minimumSizeHint().height());
}

void fitHeightToBodyWithinAvailableArea(QWidget& window)
{
    const int lacks = heightTheBodyLacks(window);
    if (lacks != 0)
    {
        window.resize(window.width(), (std::max)(window.minimumHeight(), window.height() + lacks));
    }
    holdWithinAvailableArea(window);
}

void holdWithinAvailableAreaWhenFirstShown(QWidget& window)
{
    // Owned by the window, and gone at its first show.
    new FirstShowHold(window);
}

} // namespace an::ui
