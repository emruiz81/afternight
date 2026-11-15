// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnCommandPalette.h"

#include "components/AnKeyHint.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/Elevation.h"
#include "theme/PainterGuard.h"

#include <QAbstractButton>
#include <QAbstractListModel>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnCommandPalette)

namespace
{

using an::ui::CommandRow;
using an::ui::CommandRowGroup;
using an::ui::DesignTokens;

/// What the body lists: a group's header, a row, or the line that says nothing matched.
struct PaletteItem
{
    enum class Kind
    {
        Header,
        Row,
        Nothing,
    };

    Kind kind = Kind::Row;
    QString groupId;
    QString title; ///< A header's title.
    int hidden = 0; ///< How many rows of a header's group are not shown.
    CommandRow row;
};

QString hiddenText(const int hidden)
{
    return QStringLiteral("%1 more").arg(hidden);
}

QString nothingText()
{
    return QStringLiteral("No command matches");
}

/// What assistive technology reads of an item.
QString describedItem(const PaletteItem& item)
{
    switch (item.kind)
    {
    case PaletteItem::Kind::Header:
        return item.hidden > 0 ? item.title + QStringLiteral(", ") + hiddenText(item.hidden) : item.title;
    case PaletteItem::Kind::Nothing:
        return nothingText();
    case PaletteItem::Kind::Row:
        break;
    }
    if (!item.row.enabled)
    {
        return item.row.title + QStringLiteral(", unavailable: ") + item.row.subtitle;
    }
    return item.row.subtitle.isEmpty() ? item.row.title : item.row.title + QStringLiteral(", ") + item.row.subtitle;
}

/// The items of the body, as a list view shows them.
class PaletteModel final : public QAbstractListModel
{
public:
    explicit PaletteModel(QObject* parent) : QAbstractListModel(parent) {}

    void setItems(std::vector<PaletteItem> items)
    {
        beginResetModel();
        m_items = std::move(items);
        endResetModel();
    }

    [[nodiscard]] const std::vector<PaletteItem>& items() const noexcept
    {
        return m_items;
    }

    [[nodiscard]] const PaletteItem* item(const QModelIndex& index) const
    {
        return index.isValid() && index.row() < static_cast<int>(m_items.size())
                   ? &m_items[static_cast<std::size_t>(index.row())]
                   : nullptr;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_items.size());
    }

    [[nodiscard]] QVariant data(const QModelIndex& index, const int role) const override
    {
        const PaletteItem* const shown = item(index);
        if (shown == nullptr)
        {
            return {};
        }
        switch (role)
        {
        case Qt::DisplayRole:
            return shown->kind == PaletteItem::Kind::Row ? shown->row.title : describedItem(*shown);
        case Qt::AccessibleTextRole:
            return describedItem(*shown);
        default:
            return {};
        }
    }

    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        const PaletteItem* const shown = item(index);
        if (shown == nullptr || shown->kind != PaletteItem::Kind::Row)
        {
            return Qt::ItemIsEnabled;
        }
        // A command that cannot run now is still selected, to be read; its name says it is unavailable.
        return shown->row.enabled ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::ItemIsSelectable;
    }

private:
    std::vector<PaletteItem> m_items;
};

QFont titleFont(const DesignTokens& t, const bool selected)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontBody, selected ? QFont::DemiBold : QFont::Normal);
}

QFont subtitleFont(const DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontSmall);
}

QFont headerFont(const DesignTokens& t)
{
    QFont font = an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption, QFont::DemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, an::ui::parts::kPaletteHeaderTracking * t.metrics.fontCaption);
    return font;
}

QFont captionFont(const DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

int headerHeight()
{
    using namespace an::ui::parts;
    return kPaletteHeaderPaddingTop + kPaletteHeaderLine + kPaletteHeaderPaddingBottom;
}

/// Where a row's star is, in the row's rectangle.
QRect starIn(const QRect& row)
{
    using namespace an::ui::parts;
    return QRect(row.right() - kPaletteRowPadding - kPaletteStar + 1, row.center().y() - kPaletteStar / 2 + 1,
                 kPaletteStar, kPaletteStar);
}

/// A text of the footer, in the caption's size and the secondary text.
class PaletteCaption final : public QWidget
{
public:
    PaletteCaption(QString text, QWidget* parent) : QWidget(parent), m_text(std::move(text))
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const DesignTokens& t = an::ui::tokens();
        return QSize(an::ui::parts::textWidth(captionFont(t), m_text), t.metrics.tag);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setPen(t.colors.textSecondary);
        painter.setFont(captionFont(t));
        painter.drawText(QRectF(rect()), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
};

/// The footer's button that asks for every menu: an icon and its words, the state colour laid over it under the
/// pointer.
class PaletteMenusButton final : public QAbstractButton
{
public:
    PaletteMenusButton(std::function<QIcon(const QString&)> icon, QWidget* parent)
        : QAbstractButton(parent), m_icon(std::move(icon))
    {
        setText(QStringLiteral("all menus"));
        setAccessibleName(QStringLiteral("All menus"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        using namespace an::ui::parts;
        const DesignTokens& t = an::ui::tokens();
        return QSize(2 * kKeyHintPadding + kPaletteFooterIcon + kPaletteFooterHintGap +
                         textWidth(captionFont(t), text()),
                     t.metrics.tag);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        using namespace an::ui::parts;
        const DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        paintStateLayer(painter, QRectF(rect()), t.radius.sm, underMouse(), isDown());
        const QRectF square(kKeyHintPadding, (height() - kPaletteFooterIcon) / 2.0, kPaletteFooterIcon,
                            kPaletteFooterIcon);
        paintIcon(painter, m_icon(QStringLiteral("more_horiz")), square, t.colors.textSecondary);
        painter.setPen(t.colors.textSecondary);
        painter.setFont(captionFont(t));
        painter.drawText(QRectF(square.right() + kPaletteFooterHintGap, 0, width(), height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    std::function<QIcon(const QString&)> m_icon;
};

/// The palette as assistive technology reads it: a combo box - the editable query and the list of rows it holds.
QAccessibleInterface* paletteAccessibleFactory(const QString& key, QObject* object)
{
    if (key == QLatin1String(an::ui::AnCommandPalette::staticMetaObject.className()))
    {
        if (auto* const palette = qobject_cast<an::ui::AnCommandPalette*>(object))
        {
            return new QAccessibleWidget(palette, QAccessible::ComboBox);
        }
    }
    return nullptr;
}

} // namespace

namespace an::ui
{

struct AnCommandPalette::Impl
{
    class Delegate;

    explicit Impl(AnCommandPalette& owner) : palette(owner) {}

    AnCommandPalette& palette;
    ICommandSource* source = nullptr;
    IconResolver iconResolver;
    mutable std::map<QString, QIcon> icons;

    QLineEdit* query = nullptr;
    AnKeyHint* escape = nullptr;
    QListView* list = nullptr;
    PaletteModel* model = nullptr;
    QWidget* footer = nullptr;
    QAbstractButton* allMenus = nullptr;

    std::vector<CommandRowGroup> groups;
    std::set<QString> expanded;
    QString hovered;
    QPoint anchor;
    int windowHeight = 0;

    [[nodiscard]] QIcon icon(const QString& key) const
    {
        if (key.isEmpty())
        {
            return {};
        }
        const auto kept = icons.find(key);
        if (kept != icons.end())
        {
            return kept->second;
        }
        QIcon made = iconResolver ? iconResolver(key) : QIcon(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(key));
        icons.emplace(key, made);
        return made;
    }

    [[nodiscard]] const PaletteItem* itemAt(const QModelIndex& index) const
    {
        return model->item(index);
    }

    [[nodiscard]] QModelIndex indexOfRow(const QString& id) const
    {
        const std::vector<PaletteItem>& items = model->items();
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (items[index].kind == PaletteItem::Kind::Row && items[index].row.id == id)
            {
                return model->index(static_cast<int>(index));
            }
        }
        return {};
    }

    [[nodiscard]] QModelIndex indexOfHeader(const QString& groupId) const
    {
        const std::vector<PaletteItem>& items = model->items();
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (items[index].kind == PaletteItem::Kind::Header && items[index].groupId == groupId)
            {
                return model->index(static_cast<int>(index));
            }
        }
        return {};
    }

    /// The rows, in the order the body shows them.
    [[nodiscard]] std::vector<int> rowPositions() const
    {
        std::vector<int> rows;
        const std::vector<PaletteItem>& items = model->items();
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            if (items[index].kind == PaletteItem::Kind::Row)
            {
                rows.push_back(static_cast<int>(index));
            }
        }
        return rows;
    }

    [[nodiscard]] QString selectedId() const
    {
        const PaletteItem* const selected = itemAt(list->currentIndex());
        return selected != nullptr && selected->kind == PaletteItem::Kind::Row ? selected->row.id : QString();
    }

    /// Whether a row shows its star: a favoritable one while it is hovered or selected, a favourite always.
    [[nodiscard]] bool showsStar(const CommandRow& row) const
    {
        return row.favoritable && (row.favorite || row.id == hovered || row.id == selectedId());
    }

    void select(const QModelIndex& index)
    {
        if (!index.isValid())
        {
            list->selectionModel()->clearCurrentIndex();
            list->selectionModel()->clearSelection();
            list->viewport()->update();
            return;
        }
        list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        list->scrollTo(index);
        list->viewport()->update();
    }

    /// Lists what the source answered: each group under its header, its rows up to its limit unless it was expanded.
    void rebuild()
    {
        std::vector<PaletteItem> items;
        for (const CommandRowGroup& group : groups)
        {
            if (group.rows.empty())
            {
                continue;
            }
            const bool limited =
                group.limit > 0 && static_cast<int>(group.rows.size()) > group.limit && expanded.count(group.id) == 0u;
            const int shown = limited ? group.limit : static_cast<int>(group.rows.size());
            PaletteItem header;
            header.kind = PaletteItem::Kind::Header;
            header.groupId = group.id;
            header.title = group.title;
            header.hidden = static_cast<int>(group.rows.size()) - shown;
            items.push_back(header);
            for (int index = 0; index < shown; ++index)
            {
                PaletteItem row;
                row.kind = PaletteItem::Kind::Row;
                row.groupId = group.id;
                row.row = group.rows[static_cast<std::size_t>(index)];
                items.push_back(std::move(row));
            }
        }
        if (items.empty())
        {
            PaletteItem nothing;
            nothing.kind = PaletteItem::Kind::Nothing;
            items.push_back(nothing);
        }
        model->setItems(std::move(items));
    }

    /// Asks the source what the query answers now and lists it, selecting `keep` when it is still shown and the first
    /// row otherwise.
    void requery(const QString& keep = {})
    {
        groups = source != nullptr ? source->query(query->text()) : std::vector<CommandRowGroup>{};
        rebuild();
        QModelIndex selected = keep.isEmpty() ? QModelIndex() : indexOfRow(keep);
        if (!selected.isValid())
        {
            const std::vector<int> rows = rowPositions();
            selected = rows.empty() ? QModelIndex() : model->index(rows.front());
        }
        select(selected);
        layOut();
    }

    /// How tall the body's list is with every item shown.
    [[nodiscard]] int contentHeight() const
    {
        const DesignTokens& t = tokens();
        int height = 0;
        for (const PaletteItem& item : model->items())
        {
            height += item.kind == PaletteItem::Kind::Header ? headerHeight() : t.metrics.rowPalette;
        }
        return height;
    }

    /// Sizes the surface to what the body shows - no taller than its share of the window - and places it under the
    /// anchor.
    void layOut()
    {
        using namespace parts;
        const int maximumBody = static_cast<int>(std::floor(windowHeight * kPaletteBodyShare));
        const int listHeight = (std::max)(0, (std::min)(contentHeight(), maximumBody - kPaletteBodyPadding));
        list->setFixedHeight(listHeight);
        const QMargins margins = palette.contentsMargins();
        const QSize size(margins.left() + kPaletteWidth + margins.right(), margins.top() + kPaletteQueryRow +
                                                                               listHeight + kPaletteBodyPadding +
                                                                               kPaletteFooter + margins.bottom());
        palette.setFixedSize(size);
        palette.move(anchor.x() - margins.left() - kPaletteWidth / 2, anchor.y() + kPaletteOffset - margins.top());
        if (QLayout* const layout = palette.layout())
        {
            layout->activate();
        }
    }

    /// Moves the selection by a number of rows, stopping at the ends.
    void move(const int step)
    {
        const std::vector<int> rows = rowPositions();
        if (rows.empty())
        {
            return;
        }
        const auto current = std::find(rows.begin(), rows.end(), list->currentIndex().row());
        const int at = current == rows.end() ? 0 : static_cast<int>(current - rows.begin());
        const int next = std::clamp(at + step, 0, static_cast<int>(rows.size()) - 1);
        select(model->index(rows[static_cast<std::size_t>(next)]));
    }

    /// How many rows a page is: the rows the body shows at once.
    [[nodiscard]] int page() const
    {
        return (std::max)(1, list->viewport()->height() / (std::max)(1, tokens().metrics.rowPalette));
    }

    /// Selects the first row of the next group, or of the previous one, going round.
    void moveToGroup(const int step)
    {
        std::vector<QString> shownGroups;
        for (const PaletteItem& item : model->items())
        {
            if (item.kind == PaletteItem::Kind::Header)
            {
                shownGroups.push_back(item.groupId);
            }
        }
        if (shownGroups.empty())
        {
            return;
        }
        const PaletteItem* const current = itemAt(list->currentIndex());
        const auto at = current == nullptr ? shownGroups.end()
                                           : std::find(shownGroups.begin(), shownGroups.end(), current->groupId);
        const int count = static_cast<int>(shownGroups.size());
        const int from = at == shownGroups.end() ? (step > 0 ? -1 : 0) : static_cast<int>(at - shownGroups.begin());
        const QString target = shownGroups[static_cast<std::size_t>(((from + step) % count + count) % count)];
        for (const int position : rowPositions())
        {
            if (model->items()[static_cast<std::size_t>(position)].groupId == target)
            {
                select(model->index(position));
                return;
            }
        }
    }

    /// Runs the selected command, closing the palette first; a command that cannot run now is not run.
    void runSelected()
    {
        const PaletteItem* const selected = itemAt(list->currentIndex());
        if (selected == nullptr || selected->kind != PaletteItem::Kind::Row || !selected->row.enabled ||
            source == nullptr)
        {
            return;
        }
        const QString id = selected->row.id;
        palette.close();
        if (source->run(id))
        {
            palette.commandRun(id);
        }
    }

    void toggleFavorite(const QString& id)
    {
        if (source == nullptr)
        {
            return;
        }
        const QString keep = selectedId().isEmpty() ? id : selectedId();
        source->toggleFavorite(id);
        requery(keep);
    }

    void expand(const QString& groupId)
    {
        const QString keep = selectedId();
        expanded.insert(groupId);
        rebuild();
        select(indexOfRow(keep));
        layOut();
    }

    /// Takes a press in the body: a header expands its group, a star toggles its row, a row is selected and run.
    void press(const QPoint& position)
    {
        const QModelIndex index = list->indexAt(position);
        const PaletteItem* const item = itemAt(index);
        if (item == nullptr)
        {
            return;
        }
        if (item->kind == PaletteItem::Kind::Header)
        {
            if (item->hidden > 0)
            {
                expand(item->groupId);
            }
            return;
        }
        if (item->kind != PaletteItem::Kind::Row)
        {
            return;
        }
        if (item->row.favoritable && starIn(list->visualRect(index)).contains(position))
        {
            toggleFavorite(item->row.id);
            return;
        }
        select(index);
        runSelected();
    }

    void setHovered(const QString& id)
    {
        if (hovered != id)
        {
            hovered = id;
            list->viewport()->update();
        }
    }

    /// The fonts that are no part's own: the query's, at its size.
    void applyLook() const
    {
        query->setFont(parts::sizedFont(tokens().typography.body, parts::kPaletteQueryFont));
    }
};

/// Paints the body's items: a header's caps title and its hidden count, a row's selection or hover, icon, title,
/// subtitle, keys and star, the line that says nothing matched.
class AnCommandPalette::Impl::Delegate final : public QStyledItemDelegate
{
public:
    Delegate(const Impl& impl, QObject* parent) : QStyledItemDelegate(parent), m_impl(impl) {}

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const PaletteItem* const item = m_impl.itemAt(index);
        const int height =
            item != nullptr && item->kind == PaletteItem::Kind::Header ? headerHeight() : tokens().metrics.rowPalette;
        return QSize(option.rect.width(), height);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const PaletteItem* const item = m_impl.itemAt(index);
        if (item == nullptr)
        {
            return;
        }
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        switch (item->kind)
        {
        case PaletteItem::Kind::Header:
            paintHeader(*painter, option.rect, *item);
            break;
        case PaletteItem::Kind::Nothing:
            paintNothing(*painter, option.rect);
            break;
        case PaletteItem::Kind::Row:
            paintRow(*painter, option.rect, item->row);
            break;
        }
    }

private:
    static void paintHeader(QPainter& painter, const QRect& rect, const PaletteItem& item)
    {
        using namespace parts;
        const DesignTokens& t = tokens();
        const QRectF line(rect.left() + kPaletteHeaderPaddingSide, rect.top() + kPaletteHeaderPaddingTop,
                          rect.width() - 2 * kPaletteHeaderPaddingSide, kPaletteHeaderLine);
        painter.setPen(t.colors.textTertiary);
        painter.setFont(headerFont(t));
        painter.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, item.title);
        if (item.hidden > 0)
        {
            painter.setFont(captionFont(t));
            painter.drawText(line, Qt::AlignRight | Qt::AlignVCenter, hiddenText(item.hidden));
        }
    }

    static void paintNothing(QPainter& painter, const QRect& rect)
    {
        using namespace parts;
        const DesignTokens& t = tokens();
        painter.setPen(t.colors.textTertiary);
        painter.setFont(titleFont(t, false));
        painter.drawText(QRectF(rect).adjusted(kPaletteRowPadding, 0, -kPaletteRowPadding, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, nothingText());
    }

    void paintRow(QPainter& painter, const QRect& rect, const CommandRow& row) const
    {
        using namespace parts;
        const DesignTokens& t = tokens();
        const bool selected = row.id == m_impl.selectedId();
        const bool hovered = row.id == m_impl.hovered;
        const QRectF area(rect);
        if (!row.enabled)
        {
            painter.setOpacity(kDisabledOpacity);
        }
        if (selected)
        {
            QPainterPath shape;
            shape.addRoundedRect(area, kPaletteRowRadius, kPaletteRowRadius);
            painter.fillPath(shape, t.colors.accentContainer);
            // The accent's bar at the row's left edge, inside its corners.
            const PainterGuard bar(&painter);
            painter.setClipPath(shape);
            painter.fillRect(QRectF(area.left(), area.top(), kPaletteSelectedBar, area.height()),
                             t.colors.accentPrimary);
        }
        else if (hovered)
        {
            paintStateLayer(painter, area, kPaletteRowRadius, true, false);
        }
        const QColor text = selected ? t.colors.textOnAccentContainer : t.colors.textPrimary;
        const QColor secondary = selected ? t.colors.textOnAccentContainer : t.colors.textSecondary;

        qreal left = area.left() + kPaletteRowPadding;
        const qreal middle = area.center().y();
        if (!row.iconKey.isEmpty())
        {
            paintIcon(painter, m_impl.icon(row.iconKey),
                      QRectF(left, middle - kPaletteRowIcon / 2.0, kPaletteRowIcon, kPaletteRowIcon), secondary);
        }
        left += kPaletteRowIcon + kPaletteRowGap;

        qreal right = area.right() + 1 - kPaletteRowPadding;
        if (row.favoritable)
        {
            if (m_impl.showsStar(row))
            {
                const QColor star = selected ? t.colors.textOnAccentContainer
                                             : (row.favorite ? t.colors.textAccent : t.colors.textSecondary);
                paintIcon(painter, m_impl.icon(row.favorite ? QStringLiteral("star_filled") : QStringLiteral("star")),
                          QRectF(starIn(rect)), star);
            }
            right -= kPaletteStar + kPaletteHintGap;
        }
        if (!row.shortcut.isEmpty())
        {
            const QString keys = AnKeyHint::keysText(row.shortcut);
            const QSize hint = AnKeyHint::hintSize(keys);
            const QRectF box(right - hint.width(), middle - hint.height() / 2.0, hint.width(), hint.height());
            AnKeyHint::paintHint(painter, box, keys,
                                 selected ? AnKeyHint::Surface::OnColour : AnKeyHint::Surface::Raised,
                                 t.colors.textOnAccentContainer);
            right -= hint.width() + kPaletteHintGap;
        }

        const qreal room = (std::max)(0.0, right - left);
        const QFont subtitle = subtitleFont(t);
        const qreal subtitleWidth = row.subtitle.isEmpty()
                                        ? 0.0
                                        : (std::min)(static_cast<qreal>(textWidth(subtitle, row.subtitle)),
                                                     std::floor(room * kPaletteSubtitleShare));
        const qreal titleWidth = room - subtitleWidth - (row.subtitle.isEmpty() ? 0.0 : kPaletteRowGap);

        const QFont title = titleFont(t, selected);
        painter.setFont(title);
        painter.setPen(text);
        const QRectF titleArea(left, area.top(), (std::max)(0.0, titleWidth), area.height());
        painter.drawText(titleArea, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(title).elidedText(row.title, Qt::ElideRight, titleArea.width()));
        if (!row.subtitle.isEmpty())
        {
            painter.setFont(subtitle);
            painter.setPen(secondary);
            const QRectF subtitleArea(right - subtitleWidth, area.top(), subtitleWidth, area.height());
            painter.drawText(subtitleArea, Qt::AlignRight | Qt::AlignVCenter,
                             QFontMetricsF(subtitle).elidedText(row.subtitle, Qt::ElideMiddle, subtitleArea.width()));
        }
    }

    const Impl& m_impl;
};

namespace
{

/// The footer: what the keys do, the prefixes and the menus' button, on the palette's foot.
QWidget* makeFooter(AnCommandPalette& palette, QAbstractButton*& allMenus,
                    const std::function<QIcon(const QString&)>& icon)
{
    using namespace parts;
    auto* const footer = new QWidget(&palette);
    footer->setObjectName(QStringLiteral("commandPaletteFooter"));
    footer->setFixedHeight(kPaletteFooter);
    auto* const layout = new QHBoxLayout(footer);
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
    // The hairline above the footer is the palette's.
    layout->setContentsMargins(kPaletteFooterPadding, 1, kPaletteFooterPadding, 0);
    layout->setSpacing(kPaletteFooterGap);

    const auto hint = [footer, layout](std::initializer_list<int> keys, const QString& text) {
        auto* const pair = new QHBoxLayout();
        pair->setProperty("anKeepLayoutMargins", true);
        pair->setProperty("anKeepLayoutSpacing", true);
        pair->setContentsMargins(0, 0, 0, 0);
        pair->setSpacing(kPaletteFooterHintGap);
        for (const int key : keys)
        {
            pair->addWidget(new AnKeyHint(QKeySequence(key), footer));
        }
        pair->addWidget(new PaletteCaption(text, footer));
        layout->addLayout(pair);
    };
    hint({Qt::Key_Up, Qt::Key_Down}, QStringLiteral("navigate"));
    hint({Qt::Key_Enter}, QStringLiteral("run"));
    hint({Qt::Key_Escape}, QStringLiteral("close"));
    layout->addStretch(1);
    hint({Qt::Key_Greater}, QStringLiteral("processes"));
    hint({Qt::Key_At}, QStringLiteral("images"));
    hint({Qt::Key_Colon}, QStringLiteral("settings"));
    allMenus = new PaletteMenusButton(icon, footer);
    layout->addWidget(allMenus);
    return footer;
}

} // namespace

AnCommandPalette::AnCommandPalette(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>(*this))
{
    using namespace parts;
    // One factory serves every palette: installed again it is not installed twice.
    QAccessible::removeFactory(&paletteAccessibleFactory);
    QAccessible::installFactory(&paletteAccessibleFactory);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setContentsMargins(elevationWindowMargins());
    setAccessibleName(QStringLiteral("Command palette"));
    setFocusPolicy(Qt::NoFocus);

    Impl& impl = *m_impl;
    auto* const layout = new QVBoxLayout(this);
    layout->setProperty("anKeepLayoutMargins", true);
    layout->setProperty("anKeepLayoutSpacing", true);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The query row: the search icon the palette paints, the field, the key hint; its hairline at its foot.
    auto* const queryRow = new QHBoxLayout();
    queryRow->setProperty("anKeepLayoutMargins", true);
    queryRow->setProperty("anKeepLayoutSpacing", true);
    queryRow->setContentsMargins(kPaletteQueryPaddingStart + kPaletteQueryIcon + kPaletteQueryGap, 0,
                                 kPaletteQueryPaddingEnd, 1);
    queryRow->setSpacing(kPaletteQueryGap);
    queryRow->addStrut(kPaletteQueryRow - 1);
    impl.query = new QLineEdit(this);
    impl.query->setObjectName(QStringLiteral("commandPaletteQuery"));
    impl.query->setFrame(false);
    impl.query->setPlaceholderText(QStringLiteral("Type a command, process or file..."));
    impl.query->setAccessibleName(QStringLiteral("Search commands"));
    impl.query->installEventFilter(this);
    queryRow->addWidget(impl.query, 1);
    impl.escape = new AnKeyHint(QKeySequence(Qt::Key_Escape), this);
    queryRow->addWidget(impl.escape);
    layout->addLayout(queryRow);

    // The body: the list, inset at its sides and its foot.
    auto* const body = new QHBoxLayout();
    body->setProperty("anKeepLayoutMargins", true);
    body->setProperty("anKeepLayoutSpacing", true);
    body->setContentsMargins(kPaletteBodyPadding, 0, kPaletteBodyPadding, kPaletteBodyPadding);
    body->setSpacing(0);
    impl.list = new QListView(this);
    impl.list->setObjectName(QStringLiteral("commandPaletteList"));
    impl.list->setAccessibleName(QStringLiteral("Commands"));
    impl.list->setFrameShape(QFrame::NoFrame);
    impl.list->setFocusPolicy(Qt::NoFocus);
    impl.list->setSelectionMode(QAbstractItemView::SingleSelection);
    impl.list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    impl.list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    impl.list->setAutoFillBackground(false);
    impl.list->viewport()->setAutoFillBackground(false);
    impl.list->setMouseTracking(true);
    impl.list->viewport()->installEventFilter(this);
    impl.model = new PaletteModel(this);
    impl.list->setModel(impl.model);
    impl.list->setItemDelegate(new Impl::Delegate(impl, this));
    body->addWidget(impl.list);
    layout->addLayout(body);

    impl.footer = makeFooter(*this, impl.allMenus, [&impl](const QString& key) { return impl.icon(key); });
    layout->addWidget(impl.footer);
    connect(impl.allMenus, &QAbstractButton::clicked, this, [this]() {
        close();
        allMenusRequested();
    });

    connect(impl.query, &QLineEdit::textChanged, this, [&impl]() {
        impl.expanded.clear();
        impl.requery();
    });
}

AnCommandPalette::~AnCommandPalette() = default;

void AnCommandPalette::setSource(ICommandSource* const source)
{
    m_impl->source = source;
}

ICommandSource* AnCommandPalette::source() const noexcept
{
    return m_impl->source;
}

void AnCommandPalette::setIconResolver(IconResolver resolver)
{
    m_impl->iconResolver = std::move(resolver);
    m_impl->icons.clear();
    update();
}

void AnCommandPalette::open(const QPoint& anchor, const int windowHeight)
{
    Impl& impl = *m_impl;
    impl.anchor = anchor;
    impl.windowHeight = windowHeight;
    impl.expanded.clear();
    impl.hovered.clear();
    impl.applyLook();
    if (impl.source != nullptr)
    {
        impl.source->refresh();
    }
    {
        const QSignalBlocker quiet(impl.query);
        impl.query->clear();
    }
    impl.requery();
    show();
    raise();
    activateWindow();
    impl.query->setFocus(Qt::PopupFocusReason);
}

QLineEdit* AnCommandPalette::queryField() const noexcept
{
    return m_impl->query;
}

QListView* AnCommandPalette::list() const noexcept
{
    return m_impl->list;
}

QAbstractButton* AnCommandPalette::allMenusButton() const noexcept
{
    return m_impl->allMenus;
}

QRect AnCommandPalette::surfaceRect() const
{
    return contentsRect();
}

QRect AnCommandPalette::queryRowRect() const
{
    const QRect surface = surfaceRect();
    return QRect(surface.left(), surface.top(), surface.width(), parts::kPaletteQueryRow);
}

QRect AnCommandPalette::footerRect() const
{
    const QRect surface = surfaceRect();
    return QRect(surface.left(), surface.bottom() - parts::kPaletteFooter + 1, surface.width(), parts::kPaletteFooter);
}

QRect AnCommandPalette::bodyRect() const
{
    const QRect query = queryRowRect();
    const QRect footer = footerRect();
    return QRect(query.left(), query.bottom() + 1, query.width(), footer.top() - query.bottom() - 1);
}

const std::vector<CommandRowGroup>& AnCommandPalette::groups() const noexcept
{
    return m_impl->groups;
}

QStringList AnCommandPalette::shownRowIds() const
{
    QStringList ids;
    for (const PaletteItem& item : m_impl->model->items())
    {
        if (item.kind == PaletteItem::Kind::Row)
        {
            ids.append(item.row.id);
        }
    }
    return ids;
}

QString AnCommandPalette::selectedId() const
{
    return m_impl->selectedId();
}

void AnCommandPalette::setSelectedId(const QString& id)
{
    const QModelIndex index = m_impl->indexOfRow(id);
    if (index.isValid())
    {
        m_impl->select(index);
    }
}

QRect AnCommandPalette::rowRect(const QString& id) const
{
    const QModelIndex index = m_impl->indexOfRow(id);
    if (!index.isValid())
    {
        return {};
    }
    const QRect shown = m_impl->list->visualRect(index);
    return QRect(m_impl->list->viewport()->mapTo(this, shown.topLeft()), shown.size());
}

QRect AnCommandPalette::headerRect(const QString& groupId) const
{
    const QModelIndex index = m_impl->indexOfHeader(groupId);
    if (!index.isValid())
    {
        return {};
    }
    const QRect shown = m_impl->list->visualRect(index);
    return QRect(m_impl->list->viewport()->mapTo(this, shown.topLeft()), shown.size());
}

QRect AnCommandPalette::starRect(const QString& id) const
{
    const QModelIndex index = m_impl->indexOfRow(id);
    const PaletteItem* const item = m_impl->itemAt(index);
    if (item == nullptr || !m_impl->showsStar(item->row))
    {
        return {};
    }
    const QRect star = starIn(m_impl->list->visualRect(index));
    return QRect(m_impl->list->viewport()->mapTo(this, star.topLeft()), star.size());
}

QSize AnCommandPalette::sizeHint() const
{
    return size();
}

bool AnCommandPalette::event(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::StyleChange:
    case QEvent::PaletteChange: {
        const bool handled = QWidget::event(event);
        m_impl->applyLook();
        if (isVisible())
        {
            m_impl->layOut();
        }
        return handled;
    }
    default:
        return QWidget::event(event);
    }
}

bool AnCommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    Impl& impl = *m_impl;
    if (watched == impl.query && event->type() == QEvent::KeyPress)
    {
        const auto* const key = static_cast<QKeyEvent*>(event);
        switch (key->key())
        {
        case Qt::Key_Up:
            impl.move(-1);
            return true;
        case Qt::Key_Down:
            impl.move(1);
            return true;
        case Qt::Key_PageUp:
            impl.move(-impl.page());
            return true;
        case Qt::Key_PageDown:
            impl.move(impl.page());
            return true;
        case Qt::Key_Home:
            impl.move(-static_cast<int>(impl.model->items().size()));
            return true;
        case Qt::Key_End:
            impl.move(static_cast<int>(impl.model->items().size()));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            impl.runSelected();
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        case Qt::Key_Tab:
            impl.moveToGroup(1);
            return true;
        case Qt::Key_Backtab:
            impl.moveToGroup(-1);
            return true;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == impl.list->viewport())
    {
        switch (event->type())
        {
        case QEvent::MouseMove: {
            const PaletteItem* const item =
                impl.itemAt(impl.list->indexAt(static_cast<QMouseEvent*>(event)->position().toPoint()));
            impl.setHovered(item != nullptr && item->kind == PaletteItem::Kind::Row ? item->row.id : QString());
            return false;
        }
        case QEvent::Leave:
            impl.setHovered(QString());
            return false;
        case QEvent::MouseButtonPress: {
            const auto* const mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                impl.press(mouse->position().toPoint());
            }
            return true;
        }
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AnCommandPalette::paintEvent(QPaintEvent* /*event*/)
{
    using namespace parts;
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF surface(surfaceRect());
    const qreal radius = t.radius.lg;
    paintElevation(painter, surface, static_cast<int>(radius));

    QPainterPath shape;
    shape.addRoundedRect(surface, radius, radius);
    painter.fillPath(shape, t.layers.surface3);
    {
        // The footer on the second surface, inside the surface's corners.
        const PainterGuard footer(&painter);
        painter.setClipPath(shape);
        painter.fillRect(QRectF(footerRect()), t.layers.surface2);
    }
    // The hairlines under the query row and over the footer.
    const QRect query = queryRowRect();
    painter.fillRect(QRectF(query.left(), query.bottom(), query.width(), 1), t.colors.borderSubtle);
    const QRect footer = footerRect();
    painter.fillRect(QRectF(footer.left(), footer.top(), footer.width(), 1), t.colors.borderSubtle);
    painter.setPen(QPen(t.colors.borderSubtle, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);

    const QRectF search(surface.left() + kPaletteQueryPaddingStart,
                        query.top() + (kPaletteQueryRow - 1 - kPaletteQueryIcon) / 2.0, kPaletteQueryIcon,
                        kPaletteQueryIcon);
    paintIcon(painter, m_impl->icon(QStringLiteral("search")), search, t.colors.textSecondary);
}

void AnCommandPalette::mousePressEvent(QMouseEvent* event)
{
    // A press on the shadow is outside the surface: it closes the palette, as a press on another window does.
    if (!surfaceRect().contains(event->position().toPoint()))
    {
        close();
        event->accept();
        return;
    }
    event->accept();
}

void AnCommandPalette::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_impl->setHovered(QString());
    closed();
}

} // namespace an::ui
