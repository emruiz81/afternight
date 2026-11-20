// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/AccessiblePage.h"

#include "components/AnCardWidget.h"
#include "components/AnCollapsibleSection.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QToolButton>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui
{

namespace
{

/// Whether a widget is a stop of Tab: it takes the focus from Tab itself, with no other widget taking it for it.
[[nodiscard]] bool takesTab(const QWidget* widget)
{
    return (widget->focusPolicy() & Qt::TabFocus) != 0 && widget->focusProxy() == nullptr;
}

/// The widget inside a widget that takes the focus for it - a tab widget's bar - which Qt reads as a stop at the
/// holder's place in the chain; null for any other widget.
[[nodiscard]] QWidget* innerFocusProxy(const QWidget* widget)
{
    QWidget* const proxy = widget->focusProxy();
    return proxy != nullptr && widget->isAncestorOf(proxy) && (proxy->focusPolicy() & Qt::TabFocus) != 0 ? proxy
                                                                                                         : nullptr;
}

/// Whether a widget is part of a control that lays it out inside itself - a spin box's, a combo box's or a field's
/// own parts, an item view's or a text area's - under `root`, and is reached and named with that control.
[[nodiscard]] bool isPartOfAControl(const QWidget* widget, const QWidget* root)
{
    for (const QWidget* parent = widget->parentWidget(); parent != nullptr && parent != root->parentWidget();
         parent = parent->parentWidget())
    {
        if (qobject_cast<const QAbstractSpinBox*>(parent) != nullptr ||
            qobject_cast<const QComboBox*>(parent) != nullptr || qobject_cast<const QLineEdit*>(parent) != nullptr ||
            qobject_cast<const QAbstractItemView*>(parent) != nullptr ||
            qobject_cast<const QTextEdit*>(parent) != nullptr || qobject_cast<const QPlainTextEdit*>(parent) != nullptr)
        {
            return true;
        }
    }
    return false;
}

/// Whether a widget is a control a person acts on: one that takes the focus from the keyboard, or a button.
[[nodiscard]] bool isInteractive(const QWidget* widget)
{
    return takesTab(widget) || qobject_cast<const QAbstractButton*>(widget) != nullptr;
}

// ---- The reading order

/// The widgets a layout places, in the order a person reads them.
void layoutOrder(const QLayout* layout, std::vector<QWidget*>& out);

void addItem(QLayoutItem* item, std::vector<QWidget*>& out)
{
    if (item == nullptr)
    {
        return;
    }
    if (QWidget* const widget = item->widget())
    {
        out.push_back(widget);
    }
    else if (const QLayout* const inner = item->layout())
    {
        layoutOrder(inner, out);
    }
}

void layoutOrder(const QLayout* layout, std::vector<QWidget*>& out)
{
    if (const auto* grid = qobject_cast<const QGridLayout*>(layout))
    {
        struct Cell
        {
            int row;
            int column;
            int index;
        };
        std::vector<Cell> cells;
        for (int index = 0; index < grid->count(); ++index)
        {
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
            cells.push_back(Cell{row, column, index});
        }
        std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
            return a.row != b.row ? a.row < b.row : a.column < b.column;
        });
        for (const Cell& cell : cells)
        {
            addItem(grid->itemAt(cell.index), out);
        }
        return;
    }
    if (const auto* form = qobject_cast<const QFormLayout*>(layout))
    {
        for (int row = 0; row < form->rowCount(); ++row)
        {
            addItem(form->itemAt(row, QFormLayout::LabelRole), out);
            addItem(form->itemAt(row, QFormLayout::FieldRole), out);
            addItem(form->itemAt(row, QFormLayout::SpanningRole), out);
        }
        return;
    }
    const auto* box = qobject_cast<const QBoxLayout*>(layout);
    const bool reversed =
        box != nullptr && (box->direction() == QBoxLayout::RightToLeft || box->direction() == QBoxLayout::BottomToTop);
    const int count = layout->count();
    for (int index = 0; index < count; ++index)
    {
        addItem(layout->itemAt(reversed ? count - 1 - index : index), out);
    }
}

/// Whether a rectangle is read before another: it lies wholly above it, or on its row at its left.
[[nodiscard]] bool readsBefore(const QRect& a, const QRect& b)
{
    if (a.bottom() < b.top())
    {
        return true;
    }
    if (b.bottom() < a.top())
    {
        return false;
    }
    return a.left() < b.left();
}

/// Where a widget lies in `root`'s coordinates.
[[nodiscard]] QRect rectIn(const QWidget* widget, const QWidget* root)
{
    return QRect(widget->mapTo(root, QPoint(0, 0)), widget->size());
}

/// Every widget under `widget`, `widget` first, in reading order; the children that float over a sibling are left in
/// `overlays`, to be read after what they float over.
void readingOrder(QWidget* widget, std::vector<QWidget*>& order, std::vector<QWidget*>& overlays)
{
    order.push_back(widget);
    std::vector<QWidget*> laid;
    if (const QLayout* const layout = widget->layout())
    {
        layoutOrder(layout, laid);
    }
    laid.erase(
        std::remove_if(laid.begin(), laid.end(),
                       [widget](const QWidget* child) { return child->parentWidget() != widget || child->isWindow(); }),
        laid.end());
    std::vector<QWidget*> loose;
    for (QObject* const object : widget->children())
    {
        auto* const child = qobject_cast<QWidget*>(object);
        if (child == nullptr || child->isWindow() || std::find(laid.begin(), laid.end(), child) != laid.end())
        {
            continue;
        }
        // A child no layout places that lies over a placed sibling floats over it.
        const bool floats =
            child->isVisibleTo(widget) && std::any_of(laid.begin(), laid.end(), [child](const QWidget* sibling) {
                return sibling->isVisibleTo(child->parentWidget()) && sibling->geometry().intersects(child->geometry());
            });
        (floats ? overlays : loose).push_back(child);
    }
    // The children no layout places are read where they lie among the placed ones: before the first that lies below
    // them, or at their right on their row.
    std::stable_sort(loose.begin(), loose.end(), [](const QWidget* a, const QWidget* b) {
        return a->y() != b->y() ? a->y() < b->y() : a->x() < b->x();
    });
    std::vector<QWidget*> children = laid;
    for (QWidget* const child : loose)
    {
        const auto after = std::find_if(children.begin(), children.end(), [child](const QWidget* placed) {
            return readsBefore(child->geometry(), placed->geometry());
        });
        children.insert(after, child);
    }
    for (QWidget* const child : children)
    {
        readingOrder(child, order, overlays);
    }
}

// ---- The names

/// A label's or a title's text as it is read: no markup, no accelerator mark, no colon at its end.
[[nodiscard]] QString readable(const QString& text)
{
    QString plain = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    QString stripped;
    stripped.reserve(plain.size());
    for (qsizetype index = 0; index < plain.size(); ++index)
    {
        if (plain.at(index) == QLatin1Char('&') && index + 1 < plain.size())
        {
            ++index;
        }
        stripped.append(plain.at(index));
    }
    stripped = stripped.simplified();
    while (stripped.endsWith(QLatin1Char(':')))
    {
        stripped.chop(1);
    }
    return stripped.trimmed();
}

/// The text of a label item; empty for any other item.
[[nodiscard]] QString labelText(const QLayoutItem* item)
{
    const auto* label = item != nullptr ? qobject_cast<const QLabel*>(item->widget()) : nullptr;
    return label != nullptr && label->pixmap().isNull() ? readable(label->text()) : QString();
}

/// The layout that places a widget directly, and the widget's index in it.
struct Placement
{
    const QLayout* layout = nullptr;
    int index = -1;
};

[[nodiscard]] std::optional<Placement> placementIn(const QLayout* layout, const QWidget* widget)
{
    for (int index = 0; index < layout->count(); ++index)
    {
        QLayoutItem* const item = layout->itemAt(index);
        if (item == nullptr)
        {
            continue;
        }
        if (item->widget() == widget)
        {
            return Placement{layout, index};
        }
        if (const QLayout* const inner = item->layout())
        {
            if (const std::optional<Placement> found = placementIn(inner, widget))
            {
                return found;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Placement> placementOf(const QWidget& widget)
{
    const QWidget* const parent = widget.parentWidget();
    const QLayout* const layout = parent != nullptr ? parent->layout() : nullptr;
    return layout != nullptr ? placementIn(layout, &widget) : std::nullopt;
}

/// What the layout that places a widget says beside it: its form row's label, its grid row's label and column's
/// heading, the label or the check box before it in a row, the label over it in a column.
[[nodiscard]] QString besideIn(const QWidget& widget)
{
    const std::optional<Placement> placement = placementOf(widget);
    if (!placement.has_value())
    {
        return {};
    }
    const QLayout* const layout = placement->layout;
    if (const auto* form = qobject_cast<const QFormLayout*>(layout))
    {
        int row = -1;
        QFormLayout::ItemRole role = QFormLayout::LabelRole;
        form->getWidgetPosition(const_cast<QWidget*>(&widget), &row, &role);
        if (row >= 0 && role == QFormLayout::FieldRole)
        {
            const QLayoutItem* const label = form->itemAt(row, QFormLayout::LabelRole);
            if (const auto* button = label != nullptr ? qobject_cast<const QAbstractButton*>(label->widget()) : nullptr)
            {
                return readable(button->text());
            }
            return labelText(label);
        }
        return {};
    }
    if (const auto* grid = qobject_cast<const QGridLayout*>(layout))
    {
        int row = 0;
        int column = 0;
        int rowSpan = 0;
        int columnSpan = 0;
        grid->getItemPosition(placement->index, &row, &column, &rowSpan, &columnSpan);
        // The nearest label at the left of its row.
        QString rowLabel;
        for (int left = column - 1; left >= 0 && rowLabel.isEmpty(); --left)
        {
            rowLabel = labelText(grid->itemAtPosition(row, left));
        }
        // The heading over its column: a label in a row above that holds labels alone.
        QString heading;
        for (int above = row - 1; above >= 0 && heading.isEmpty(); --above)
        {
            bool labelsAlone = true;
            for (int cell = 0; cell < grid->columnCount(); ++cell)
            {
                const QLayoutItem* const item = grid->itemAtPosition(above, cell);
                if (item != nullptr && item->widget() != nullptr && isInteractive(item->widget()))
                {
                    labelsAlone = false;
                }
            }
            if (labelsAlone)
            {
                heading = labelText(grid->itemAtPosition(above, column));
            }
        }
        if (!rowLabel.isEmpty() && !heading.isEmpty())
        {
            return rowLabel + QStringLiteral(", ") + heading;
        }
        return rowLabel.isEmpty() ? heading : rowLabel;
    }
    if (const auto* box = qobject_cast<const QBoxLayout*>(layout))
    {
        const bool reversed =
            box->direction() == QBoxLayout::RightToLeft || box->direction() == QBoxLayout::BottomToTop;
        const int before = placement->index + (reversed ? 1 : -1);
        if (before < 0 || before >= box->count())
        {
            return {};
        }
        const QLayoutItem* const item = box->itemAt(before);
        const QString label = labelText(item);
        if (!label.isEmpty())
        {
            return label;
        }
        // In a row, the check box or the radio button before a field governs it.
        const bool row = box->direction() == QBoxLayout::LeftToRight || box->direction() == QBoxLayout::RightToLeft;
        const QWidget* const previous = item != nullptr ? item->widget() : nullptr;
        if (row && previous != nullptr &&
            (qobject_cast<const QCheckBox*>(previous) != nullptr ||
             qobject_cast<const QRadioButton*>(previous) != nullptr))
        {
            return readable(static_cast<const QAbstractButton*>(previous)->text());
        }
    }
    return {};
}

/// The title of the card, the section or the group a widget fills.
[[nodiscard]] QString titleAround(const QWidget& widget)
{
    for (const QWidget* parent = widget.parentWidget(); parent != nullptr && !parent->isWindow();
         parent = parent->parentWidget())
    {
        if (const auto* group = qobject_cast<const QGroupBox*>(parent); group != nullptr && !group->title().isEmpty())
        {
            return readable(group->title());
        }
        if (const auto* card = dynamic_cast<const AnCardWidget*>(parent); card != nullptr && !card->title().isEmpty())
        {
            return readable(card->title());
        }
        if (const auto* section = dynamic_cast<const AnCollapsibleSection*>(parent);
            section != nullptr && !section->title().isEmpty())
        {
            return readable(section->title());
        }
    }
    return {};
}

/// Whether a widget is the first control its parent lays out, so that what is said beside its parent says it.
[[nodiscard]] bool isFirstControlOfItsParent(const QWidget& widget)
{
    const QWidget* const parent = widget.parentWidget();
    if (parent == nullptr || parent->layout() == nullptr)
    {
        return false;
    }
    std::vector<QWidget*> placed;
    layoutOrder(parent->layout(), placed);
    for (const QWidget* const child : placed)
    {
        if (child == &widget)
        {
            return true;
        }
        if (isInteractive(child) || qobject_cast<const QLabel*>(child) != nullptr)
        {
            return false;
        }
    }
    return false;
}

[[nodiscard]] QString accessibleNameOf(QWidget& widget)
{
    QAccessibleInterface* const accessible = QAccessible::queryAccessibleInterface(&widget);
    return accessible != nullptr ? accessible->text(QAccessible::Name).trimmed() : QString();
}

} // namespace

void orderFocusChain(QWidget& root)
{
    std::vector<QWidget*> order;
    std::vector<QWidget*> overlays;
    readingOrder(&root, order, overlays);
    // What floats is read after the deepest widget that holds all of it - a toolbar after the canvas it floats over,
    // or after the whole area of views it floats under - else after its parent.
    for (std::size_t next = 0; next < overlays.size(); ++next)
    {
        QWidget* const overlay = overlays[next];
        std::vector<QWidget*> held;
        readingOrder(overlay, held, overlays);
        const QRect area = rectIn(overlay, &root);
        std::size_t under = order.size();
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            const QWidget* const candidate = order[index];
            if (candidate != &root && candidate->isVisibleTo(&root) && rectIn(candidate, &root).contains(area))
            {
                under = index;
            }
        }
        if (under == order.size())
        {
            const auto parent = std::find(order.begin(), order.end(), overlay->parentWidget());
            under = parent != order.end() ? static_cast<std::size_t>(parent - order.begin()) : order.size();
        }
        std::size_t at = order.size();
        if (under < order.size())
        {
            const QWidget* const host = order[under];
            at = under + 1;
            while (at < order.size() && host->isAncestorOf(order[at]))
            {
                ++at;
            }
        }
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), held.begin(), held.end());
    }
    // The stops, and the widgets that hand the focus to a widget inside them: Qt reads one of those as a stop at its
    // own place, so it is kept just before the widget it hands the focus to, which Qt then moves with it.
    std::vector<QWidget*> stops;
    for (QWidget* const widget : order)
    {
        if (widget->window() == root.window() && !isPartOfAControl(widget, &root) &&
            (takesTab(widget) || innerFocusProxy(widget) != nullptr))
        {
            stops.push_back(widget);
        }
    }
    // A holder that takes no focus of its own is still read as a stop through its proxy, but Qt moves no widget that
    // takes no focus: it takes Tab's while the chain is put in order.
    std::vector<std::pair<QWidget*, Qt::FocusPolicy>> lent;
    for (QWidget* const holder : stops)
    {
        if (innerFocusProxy(holder) != nullptr && holder->focusPolicy() == Qt::NoFocus)
        {
            lent.emplace_back(holder, holder->focusPolicy());
            holder->setFocusPolicy(Qt::TabFocus);
        }
    }
    for (QWidget* const holder : stops)
    {
        if (QWidget* const proxy = innerFocusProxy(holder))
        {
            QWidget::setTabOrder(holder, proxy);
        }
    }
    for (std::size_t index = 1; index < stops.size(); ++index)
    {
        QWidget::setTabOrder(stops[index - 1], stops[index]);
    }
    for (const auto& [holder, policy] : lent)
    {
        holder->setFocusPolicy(policy);
    }
}

QString readableNameOf(const QWidget& control)
{
    if (QString beside = besideIn(control); !beside.isEmpty())
    {
        return beside;
    }
    // The first control of a row or a cell is named by what names the row or the cell.
    const QWidget* holder = &control;
    for (int level = 0; level < 2 && isFirstControlOfItsParent(*holder); ++level)
    {
        holder = holder->parentWidget();
        if (QString beside = besideIn(*holder); !beside.isEmpty())
        {
            return beside;
        }
    }
    if (const auto* field = qobject_cast<const QLineEdit*>(&control); field != nullptr)
    {
        if (QString placeholder = readable(field->placeholderText()); !placeholder.isEmpty())
        {
            return placeholder;
        }
    }
    if (const auto* button = qobject_cast<const QAbstractButton*>(&control); button != nullptr)
    {
        const auto* tool = qobject_cast<const QToolButton*>(button);
        const bool iconAlone =
            button->text().isEmpty() || (tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonIconOnly);
        if (iconAlone && !button->toolTip().isEmpty())
        {
            return readable(button->toolTip().section(QLatin1Char('\n'), 0, 0));
        }
    }
    if (qobject_cast<const QAbstractItemView*>(&control) != nullptr ||
        qobject_cast<const QTextEdit*>(&control) != nullptr || qobject_cast<const QPlainTextEdit*>(&control) != nullptr)
    {
        return titleAround(control);
    }
    return {};
}

void nameUnnamedControls(QWidget& root)
{
    QList<QWidget*> widgets = root.findChildren<QWidget*>();
    widgets.prepend(&root);
    for (QWidget* const widget : widgets)
    {
        if (widget->window() != root.window() || !isInteractive(widget) || isPartOfAControl(widget, &root) ||
            !accessibleNameOf(*widget).isEmpty())
        {
            continue;
        }
        if (const QString name = readableNameOf(*widget); !name.isEmpty())
        {
            widget->setAccessibleName(name);
        }
    }
}

void prepareForKeyboard(QWidget& root)
{
    QList<QWidget*> widgets = root.findChildren<QWidget*>();
    widgets.prepend(&root);
    for (QWidget* const widget : widgets)
    {
        if (auto* const area = qobject_cast<QScrollArea*>(widget); area != nullptr && area->widget() != nullptr)
        {
            // The controls it holds are the stops; it scrolls to the one the focus is on.
            area->setFocusPolicy(Qt::NoFocus);
        }
        else if (auto* const text = qobject_cast<QTextEdit*>(widget))
        {
            text->setTabChangesFocus(true);
        }
        else if (auto* const plain = qobject_cast<QPlainTextEdit*>(widget))
        {
            plain->setTabChangesFocus(true);
        }
        else if (auto* const view = qobject_cast<QAbstractItemView*>(widget))
        {
            view->setTabKeyNavigation(false);
        }
    }
    nameUnnamedControls(root);
}

} // namespace an::ui
