// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnIconRail.h"

#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"
#include "theme/Elevation.h"
#include "theme/PainterGuard.h"

#include <QAccessible>
#include <QAccessibleInterface>
#include <QAccessibleWidget>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnIconRail)

namespace
{

using std::chrono::milliseconds;

/// The overflow item's id in the rail's layout; never one an owner gives, which an id with a control character is not.
QString overflowId()
{
    return QStringLiteral("\x01overflow");
}

/// How long the pointer rests on an item before its tooltip shows, how long a press is held to ask for the item's
/// menu, how often a run's progress repaints at most, how long the turning arc takes to turn once and how often it
/// is drawn while it turns.
constexpr int kToolTipDelay = 400;
constexpr int kHoldDelay = 500;
constexpr int kProgressInterval = 100;
constexpr qint64 kTurnPeriod = 1200;
constexpr int kTurnInterval = 33;

/// How long after the menu key a context menu event the platform sends for it is taken as the same request.
constexpr milliseconds kMenuKeyEcho{500};

QFont badgeFont(const an::ui::DesignTokens& t, const QFont::Weight weight)
{
    return an::ui::parts::sizedFont(t.typography.monospace, an::ui::parts::kRailBadgeFont, weight);
}

QFont tipFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.body, t.metrics.fontSmall);
}

QFont overflowFont(const an::ui::DesignTokens& t)
{
    return an::ui::parts::sizedFont(t.typography.monospace, t.metrics.fontCaption, QFont::DemiBold);
}

QString overflowDescription(const int hidden)
{
    return hidden == 1 ? QStringLiteral("1 more task") : QStringLiteral("%1 more tasks").arg(hidden);
}

/// A raised window that follows nothing of its own: a tooltip or a ghost, shown without taking the focus and letting
/// the pointer through.
void makeRaisedWindow(QWidget& window)
{
    window.setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                          Qt::WindowTransparentForInput);
    window.setAttribute(Qt::WA_TranslucentBackground, true);
    window.setAttribute(Qt::WA_ShowWithoutActivating, true);
    window.setAttribute(Qt::WA_TransparentForMouseEvents, true);
    window.setContentsMargins(an::ui::elevationWindowMargins());
}

/// The rail's tooltip: 28 px of the fourth surface with a hairline and the small parts' corners, on its elevation, the
/// text in the primary text.
class RailToolTip final : public QWidget
{
public:
    explicit RailToolTip(QWidget* parent) : QWidget(parent)
    {
        makeRaisedWindow(*this);
    }

    void setText(const QString& text)
    {
        m_text = text;
        update();
    }

    /// The surface's size for the text.
    [[nodiscard]] QSize surfaceSize() const
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        return QSize(2 * an::ui::parts::kRailTipPadding + an::ui::parts::textWidth(tipFont(t), m_text),
                     an::ui::parts::kRailTipHeight);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF surface(contentsRect());
        const qreal radius = t.radius.sm;
        an::ui::paintElevation(painter, surface, static_cast<int>(radius));
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface4);
        painter.drawRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
        painter.setPen(t.colors.textPrimary);
        painter.setFont(tipFont(t));
        painter.drawText(surface.adjusted(an::ui::parts::kRailTipPadding, 0, -an::ui::parts::kRailTipPadding, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
};

/// The ghost of a dragged task: its thumbnail at 70 %, on its elevation, centred on the cursor.
class RailGhost final : public QWidget
{
public:
    explicit RailGhost(QWidget* parent) : QWidget(parent)
    {
        makeRaisedWindow(*this);
    }

    void present(const an::ui::AnTaskThumbnail* thumbnail, const an::ui::RailItem& item,
                 const an::ui::AnTaskThumbnail::Look& look)
    {
        m_thumbnail = thumbnail;
        m_item = item;
        m_look = look;
        QWidget::show();
    }

    /// Puts the thumbnail's centre under a global point.
    void follow(const QPoint& globalPos)
    {
        QRect surface(QPoint(0, 0), QSize(an::ui::parts::kTaskBox, an::ui::parts::kTaskBox));
        surface.moveCenter(globalPos);
        setGeometry(surface.marginsAdded(contentsMargins()));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (m_thumbnail == nullptr)
        {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setOpacity(an::ui::parts::kRailGhostOpacity);
        const QRectF box(contentsRect());
        const qreal inset = (an::ui::parts::kTaskBox - an::ui::parts::kTaskPicture) / 2.0;
        an::ui::paintElevation(painter, box.adjusted(inset, inset, -inset, -inset), an::ui::parts::kTaskPictureRadius);
        m_thumbnail->paint(painter, box.topLeft(), m_item, m_look);
    }

private:
    const an::ui::AnTaskThumbnail* m_thumbnail = nullptr;
    an::ui::RailItem m_item;
    an::ui::AnTaskThumbnail::Look m_look;
};

} // namespace

namespace an::ui
{

/// The rail as assistive technology reads it: a list whose items are the items it shows, top to bottom, each named as
/// its tooltip; the overflow item among them when there is one.
class AnIconRailAccessible final : public QAccessibleWidget
{
public:
    explicit AnIconRailAccessible(AnIconRail* rail) : QAccessibleWidget(rail, QAccessible::List) {}

    ~AnIconRailAccessible() override
    {
        for (const auto& [id, child] : m_children)
        {
            QAccessible::deleteAccessibleInterface(child);
        }
    }

    AnIconRailAccessible(const AnIconRailAccessible&) = delete;
    AnIconRailAccessible& operator=(const AnIconRailAccessible&) = delete;

    [[nodiscard]] int childCount() const override
    {
        return static_cast<int>(rail()->m_layout.order.size());
    }

    [[nodiscard]] QAccessibleInterface* child(const int index) const override
    {
        const std::vector<QString>& order = rail()->m_layout.order;
        if (index < 0 || index >= static_cast<int>(order.size()))
        {
            return nullptr;
        }
        const QString& id = order[static_cast<std::size_t>(index)];
        const auto kept = m_children.find(id);
        if (kept != m_children.end())
        {
            return QAccessible::accessibleInterface(kept->second);
        }
        auto* const item = new Item(rail(), id);
        m_children.emplace(id, QAccessible::registerAccessibleInterface(item));
        return item;
    }

    [[nodiscard]] int indexOfChild(const QAccessibleInterface* child) const override
    {
        const std::vector<QString>& order = rail()->m_layout.order;
        for (std::size_t index = 0; index < order.size(); ++index)
        {
            const auto kept = m_children.find(order[index]);
            if (kept != m_children.end() && QAccessible::accessibleInterface(kept->second) == child)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    [[nodiscard]] QAccessibleInterface* childAt(const int x, const int y) const override
    {
        const QString id = rail()->itemAt(rail()->mapFromGlobal(QPoint(x, y)));
        const std::vector<QString>& order = rail()->m_layout.order;
        const auto found = std::find(order.begin(), order.end(), id);
        return found == order.end() ? nullptr : child(static_cast<int>(found - order.begin()));
    }

private:
    /// An item of the rail as assistive technology reads it.
    class Item final : public QAccessibleInterface
    {
    public:
        Item(AnIconRail* rail, QString id) : m_rail(rail), m_id(std::move(id)) {}

        [[nodiscard]] bool isValid() const override
        {
            return !m_rail.isNull();
        }

        [[nodiscard]] QObject* object() const override
        {
            return nullptr;
        }

        [[nodiscard]] QWindow* window() const override
        {
            return m_rail.isNull() ? nullptr : m_rail->window()->windowHandle();
        }

        [[nodiscard]] QAccessibleInterface* parent() const override
        {
            return m_rail.isNull() ? nullptr : QAccessible::queryAccessibleInterface(m_rail.data());
        }

        [[nodiscard]] QAccessibleInterface* child(int /*index*/) const override
        {
            return nullptr;
        }

        [[nodiscard]] int childCount() const override
        {
            return 0;
        }

        [[nodiscard]] int indexOfChild(const QAccessibleInterface* /*child*/) const override
        {
            return -1;
        }

        [[nodiscard]] QAccessibleInterface* childAt(int /*x*/, int /*y*/) const override
        {
            return nullptr;
        }

        [[nodiscard]] QString text(const QAccessible::Text kind) const override
        {
            if (m_rail.isNull() || kind != QAccessible::Name)
            {
                return {};
            }
            return m_id == overflowId() ? overflowDescription(m_rail->hiddenTaskCount()) : m_rail->itemToolTip(m_id);
        }

        void setText(QAccessible::Text /*kind*/, const QString& /*text*/) override {}

        [[nodiscard]] QRect rect() const override
        {
            if (m_rail.isNull())
            {
                return {};
            }
            const QRect local = m_id == overflowId() ? m_rail->overflowRect() : m_rail->itemRect(m_id);
            return local.isNull() ? QRect() : QRect(m_rail->mapToGlobal(local.topLeft()), local.size());
        }

        [[nodiscard]] QAccessible::Role role() const override
        {
            return QAccessible::ListItem;
        }

        [[nodiscard]] QAccessible::State state() const override
        {
            QAccessible::State state;
            if (m_rail.isNull())
            {
                state.invalid = true;
                return state;
            }
            state.selectable = true;
            state.focusable = true;
            state.focused = m_rail->hasFocus() && m_rail->focusedItemId() == m_id;
            for (const RailItem& item : m_rail->items())
            {
                if (item.id == m_id)
                {
                    state.selected = item.selected;
                    state.disabled = !item.enabled;
                }
            }
            state.invisible = rect().isNull();
            return state;
        }

    private:
        QPointer<AnIconRail> m_rail;
        QString m_id;
    };

    [[nodiscard]] AnIconRail* rail() const
    {
        return static_cast<AnIconRail*>(widget());
    }

    mutable std::map<QString, QAccessible::Id> m_children;
};

namespace
{

/// Makes the rail's accessible interface, for the rail's class alone.
QAccessibleInterface* railAccessibleFactory(const QString& key, QObject* object)
{
    if (key == QLatin1String(AnIconRail::staticMetaObject.className()))
    {
        if (auto* const rail = qobject_cast<AnIconRail*>(object))
        {
            return new AnIconRailAccessible(rail);
        }
    }
    return nullptr;
}

} // namespace

AnIconRail::AnIconRail(QWidget* parent)
    : QWidget(parent), m_clock([] {
          return std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
      })
{
    // One factory serves every rail: installed again it is not installed twice.
    QAccessible::removeFactory(&railAccessibleFactory);
    QAccessible::installFactory(&railAccessibleFactory);

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldDelay);
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        if (!m_pressed.isEmpty() && !m_dragging && m_pressed != overflowId())
        {
            m_holdFired = true;
            itemMenuRequested(m_pressed, m_pressGlobal);
        }
    });
    m_toolTipTimer.setSingleShot(true);
    m_toolTipTimer.setInterval(kToolTipDelay);
    connect(&m_toolTipTimer, &QTimer::timeout, this, [this] { showToolTip(); });
    m_progressTimer.setSingleShot(true);
    m_progressTimer.setInterval(kProgressInterval);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        for (const QString& id : m_progressDirty)
        {
            const QRect row = itemRect(id);
            if (!row.isNull())
            {
                update(row);
            }
        }
        m_progressDirty.clear();
    });
    m_turnTimer.setInterval(kTurnInterval);
    connect(&m_turnTimer, &QTimer::timeout, this, [this] {
        // Animations turned off while an arc turned: the arcs are drawn once more where arcs drawn with them off stand,
        // and stop.
        if (!AnimationUtils::animationsEnabled())
        {
            m_turnTimer.stop();
        }
        for (const RailItem& item : m_items)
        {
            if (item.section == RailSection::Task && item.runPhase == RailRunPhase::Running && !item.progress)
            {
                const QRect row = itemRect(item.id);
                if (!row.isNull())
                {
                    update(row);
                }
            }
        }
    });
}

AnIconRail::~AnIconRail() = default;

void AnIconRail::setItems(const std::vector<RailItem>& items)
{
    m_items = items;
    refreshThumbnails();
    layOut();
    if (find(m_hovered) == nullptr)
    {
        setHovered(QString());
    }
    if (find(m_focused) == nullptr)
    {
        m_focused.clear();
    }
    updateGeometry();
    update();
    refreshTurning();
    if (QAccessible::isActive())
    {
        QAccessibleEvent event(this, QAccessible::ObjectReorder);
        QAccessible::updateAccessibility(&event);
    }
}

void AnIconRail::updateItem(const RailItem& item)
{
    const auto found =
        std::find_if(m_items.begin(), m_items.end(), [&item](const RailItem& kept) { return kept.id == item.id; });
    if (found == m_items.end())
    {
        return;
    }
    RailItem progressed = *found;
    progressed.progress = item.progress;
    const bool progressOnly = sameRailItem(progressed, item) && found->progress != item.progress;
    const bool sectionMoved = found->section != item.section;
    const bool phaseMoved = found->runPhase != item.runPhase;
    *found = item;
    if (progressOnly)
    {
        // A run's progress repaints its thumbnail at most every interval and lays nothing out.
        if (std::find(m_progressDirty.begin(), m_progressDirty.end(), item.id) == m_progressDirty.end())
        {
            m_progressDirty.push_back(item.id);
        }
        if (!m_progressTimer.isActive())
        {
            m_progressTimer.start();
        }
        return;
    }
    refreshThumbnails();
    if (sectionMoved)
    {
        layOut();
        updateGeometry();
        update();
    }
    else
    {
        const QRect row = itemRect(item.id);
        if (!row.isNull())
        {
            update(row);
        }
    }
    if (phaseMoved || progressed.progress != item.progress)
    {
        refreshTurning();
    }
}

const std::vector<RailItem>& AnIconRail::items() const noexcept
{
    return m_items;
}

void AnIconRail::setClock(Clock clock)
{
    if (clock)
    {
        m_clock = std::move(clock);
        update();
    }
}

void AnIconRail::setIconResolver(IconResolver resolver)
{
    m_iconResolver = std::move(resolver);
    m_icons.clear();
    update();
}

QIcon AnIconRail::icon(const QString& key) const
{
    if (key.isEmpty())
    {
        return {};
    }
    const auto kept = m_icons.find(key);
    if (kept != m_icons.end())
    {
        return kept->second;
    }
    // The product's icon files; the rail draws an icon in its own colours, so any icon of the key will do.
    QIcon resolved =
        m_iconResolver ? m_iconResolver(key) : QIcon(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(key));
    m_icons.emplace(key, resolved);
    return resolved;
}

const RailItem* AnIconRail::find(const QString& id) const
{
    if (id.isEmpty())
    {
        return nullptr;
    }
    const auto found =
        std::find_if(m_items.begin(), m_items.end(), [&id](const RailItem& item) { return item.id == id; });
    return found == m_items.end() ? nullptr : &*found;
}

int AnIconRail::taskCount() const
{
    return static_cast<int>(std::count_if(m_items.begin(), m_items.end(),
                                          [](const RailItem& item) { return item.section == RailSection::Task; }));
}

AnIconRail::Layout AnIconRail::layoutAt(const int height) const
{
    using namespace parts;
    const int width = tokens().metrics.rail;
    std::vector<const RailItem*> places;
    std::vector<const RailItem*> tasks;
    std::vector<const RailItem*> foot;
    for (const RailItem& item : m_items)
    {
        (item.section == RailSection::Place  ? places
         : item.section == RailSection::Task ? tasks
                                             : foot)
            .push_back(&item);
    }

    Layout layout;
    int y = kRailPadding;
    for (const RailItem* item : places)
    {
        layout.rows.emplace(item->id, QRect(0, y, width, kRailPlaceRow));
        layout.order.push_back(item->id);
        y += kRailPlaceRow + kRailGap;
    }
    if (!tasks.empty())
    {
        layout.hairline = QRect((width - kRailHairlineWidth) / 2, y + kRailHairlineAbove, kRailHairlineWidth, 1);
        y = layout.hairline.bottom() + 1 + kRailHairlineBelow + kRailGap;
    }
    const int footCount = static_cast<int>(foot.size());
    const int footHeight = footCount > 0 ? footCount * kRailPlaceRow + (footCount - 1) * kRailGap : 0;
    const int footTop = height - kRailPadding - footHeight;
    const int tasksBottom = footCount > 0 ? footTop - kRailGap : height - kRailPadding;

    if (!tasks.empty())
    {
        const int total = static_cast<int>(tasks.size());
        const int fit = (std::max)(0, (tasksBottom - y + kRailGap) / (kRailTaskRow + kRailGap));
        layout.taskRows = fit;
        int first = 0;
        int shown = total;
        if (total > fit)
        {
            // The last row counts what is hidden; the wheel moves the ones shown.
            shown = (std::max)(0, fit - 1);
            first = std::clamp(m_taskOffset, 0, total - shown);
            layout.hiddenTasks = total - shown;
        }
        for (int index = first; index < first + shown; ++index)
        {
            const RailItem* item = tasks[static_cast<std::size_t>(index)];
            layout.rows.emplace(item->id, QRect(0, y, width, kRailTaskRow));
            layout.order.push_back(item->id);
            y += kRailTaskRow + kRailGap;
        }
        if (layout.hiddenTasks > 0 && fit > 0)
        {
            layout.overflow = QRect(0, y, width, kRailTaskRow);
            layout.order.push_back(overflowId());
        }
    }

    int footY = footTop;
    for (const RailItem* item : foot)
    {
        layout.rows.emplace(item->id, QRect(0, footY, width, kRailPlaceRow));
        layout.order.push_back(item->id);
        footY += kRailPlaceRow + kRailGap;
    }
    return layout;
}

void AnIconRail::layOut()
{
    m_layout = layoutAt(height());
    ++m_layouts;
}

QRect AnIconRail::itemRect(const QString& id) const
{
    const auto found = m_layout.rows.find(id);
    return found == m_layout.rows.end() ? QRect() : found->second;
}

QRect AnIconRail::hairlineRect() const
{
    return m_layout.hairline;
}

int AnIconRail::hiddenTaskCount() const
{
    return m_layout.hiddenTasks;
}

QRect AnIconRail::overflowRect() const
{
    return m_layout.overflow;
}

QMenu* AnIconRail::taskList() const noexcept
{
    return m_taskList;
}

QString AnIconRail::itemToolTip(const QString& id) const
{
    if (id == overflowId())
    {
        return overflowDescription(m_layout.hiddenTasks);
    }
    const RailItem* const item = find(id);
    return item == nullptr ? QString() : railItemDescription(*item);
}

QString AnIconRail::shownToolTipItem() const
{
    return m_toolTip != nullptr && m_toolTip->isVisible() ? m_toolTipItem : QString();
}

QWidget* AnIconRail::toolTipWindow() const noexcept
{
    return m_toolTip;
}

QString AnIconRail::focusedItemId() const
{
    return m_focused;
}

bool AnIconRail::isDragging() const noexcept
{
    return m_dragging;
}

QWidget* AnIconRail::dragGhost() const noexcept
{
    return m_ghost;
}

bool AnIconRail::isTurning() const
{
    return m_turnTimer.isActive();
}

std::uint64_t AnIconRail::paintCount() const noexcept
{
    return m_paints;
}

std::uint64_t AnIconRail::layoutCount() const noexcept
{
    return m_layouts;
}

QSize AnIconRail::sizeHint() const
{
    using namespace parts;
    int height = 2 * kRailPadding;
    int elements = 0;
    for (const RailItem& item : m_items)
    {
        height += item.section == RailSection::Task ? kRailTaskRow : kRailPlaceRow;
        ++elements;
    }
    if (taskCount() > 0)
    {
        height += kRailHairlineAbove + 1 + kRailHairlineBelow;
        ++elements;
    }
    height += (std::max)(0, elements - 1) * kRailGap;
    return QSize(tokens().metrics.rail, height);
}

QSize AnIconRail::minimumSizeHint() const
{
    using namespace parts;
    int height = 2 * kRailPadding;
    int elements = 0;
    for (const RailItem& item : m_items)
    {
        if (item.section != RailSection::Task)
        {
            height += kRailPlaceRow;
            ++elements;
        }
    }
    if (taskCount() > 0)
    {
        // The hairline and one row: a task, or the item counting them.
        height += kRailHairlineAbove + 1 + kRailHairlineBelow + kRailTaskRow;
        elements += 2;
    }
    height += (std::max)(0, elements - 1) * kRailGap;
    return QSize(tokens().metrics.rail, height);
}

qreal AnIconRail::turn() const
{
    if (!AnimationUtils::animationsEnabled())
    {
        return 0.0;
    }
    const qint64 now = m_clock().count();
    return static_cast<qreal>(((now % kTurnPeriod) + kTurnPeriod) % kTurnPeriod) / static_cast<qreal>(kTurnPeriod);
}

void AnIconRail::refreshThumbnails()
{
    std::map<QString, std::unique_ptr<AnTaskThumbnail>> kept;
    for (const RailItem& item : m_items)
    {
        if (item.section != RailSection::Task)
        {
            continue;
        }
        auto found = m_thumbnails.find(item.id);
        std::unique_ptr<AnTaskThumbnail> thumbnail =
            found != m_thumbnails.end() ? std::move(found->second) : std::make_unique<AnTaskThumbnail>();
        thumbnail->setPicture(item.thumbnail);
        kept.emplace(item.id, std::move(thumbnail));
    }
    m_thumbnails = std::move(kept);
}

void AnIconRail::refreshTurning()
{
    const bool turning = isVisible() && AnimationUtils::animationsEnabled() &&
                         std::any_of(m_items.begin(), m_items.end(), [this](const RailItem& item) {
                             return item.section == RailSection::Task && item.runPhase == RailRunPhase::Running &&
                                    !item.progress && m_layout.rows.contains(item.id);
                         });
    if (turning && !m_turnTimer.isActive())
    {
        m_turnTimer.start();
    }
    else if (!turning && m_turnTimer.isActive())
    {
        m_turnTimer.stop();
    }
}

QString AnIconRail::itemAt(const QPoint& position) const
{
    for (const auto& [id, row] : m_layout.rows)
    {
        if (row.contains(position))
        {
            return id;
        }
    }
    return m_layout.overflow.contains(position) ? overflowId() : QString();
}

void AnIconRail::paintEvent(QPaintEvent* /*event*/)
{
    using namespace parts;
    ++m_paints;
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), t.layers.surface1);
    painter.setPen(QPen(t.colors.borderSubtle, 1.0));
    painter.drawLine(QPointF(width() - 0.5, 0), QPointF(width() - 0.5, height()));
    if (!m_layout.hairline.isNull())
    {
        painter.fillRect(m_layout.hairline, atShare(t.colors.borderPrimary, kRailHairlineShare));
    }

    const qreal turnNow = turn();
    for (const RailItem& item : m_items)
    {
        const QRect row = itemRect(item.id);
        if (row.isNull())
        {
            continue;
        }
        const PainterGuard guard(&painter);
        if (!item.enabled)
        {
            painter.setOpacity(kDisabledOpacity);
        }
        if (item.selected)
        {
            // The bar at the rail's left edge, its right corners rounded.
            const qreal centre = QRectF(row).center().y();
            const PainterGuard bar(&painter);
            painter.setClipRect(
                QRectF(0, centre - kRailIndicatorHeight / 2.0, kRailIndicatorWidth, kRailIndicatorHeight));
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.colors.accentPrimary);
            painter.drawRoundedRect(QRectF(-kRailIndicatorWidth, centre - kRailIndicatorHeight / 2.0,
                                           2 * kRailIndicatorWidth, kRailIndicatorHeight),
                                    kRailIndicatorRadius, kRailIndicatorRadius);
        }
        if (item.section == RailSection::Task)
        {
            const auto thumbnail = m_thumbnails.find(item.id);
            if (thumbnail == m_thumbnails.end())
            {
                continue;
            }
            const QPointF centre = QRectF(row).center();
            AnTaskThumbnail::Look look;
            look.glyph = icon(item.glyphKey);
            look.windowGlyph = icon(QStringLiteral("window"));
            look.turn = turnNow;
            look.hovered = m_hovered == item.id && item.enabled;
            look.owner = this;
            look.repaint = [this] { update(); };
            const QPointF topLeft(centre.x() - kTaskBox / 2.0, centre.y() - kTaskBox / 2.0);
            thumbnail->second->paint(painter, topLeft, item, look);
            if (m_keyboardFocus && m_focused == item.id)
            {
                const qreal inset = (kTaskBox - kTaskPicture) / 2.0;
                paintFocusRing(painter,
                               QRectF(topLeft, QSizeF(kTaskBox, kTaskBox)).adjusted(inset, inset, -inset, -inset),
                               kTaskPictureRadius);
            }
        }
        else
        {
            paintPlace(painter, item, row);
        }
    }
    if (!m_layout.overflow.isNull())
    {
        paintOverflow(painter, m_layout.overflow);
    }
    if (const QRect line = insertionLineRect(); !line.isNull())
    {
        painter.fillRect(line, t.colors.accentPrimary);
    }
}

void AnIconRail::paintPlace(QPainter& painter, const RailItem& item, const QRect& row) const
{
    using namespace parts;
    const DesignTokens& t = tokens();
    const QPointF centre = QRectF(row).center();
    const QRectF square(centre.x() - kRailSquare / 2.0, centre.y() - kRailSquare / 2.0, kRailSquare, kRailSquare);
    const qreal radius = t.radius.md;
    QColor iconColour = t.colors.textSecondary;
    if (item.selected)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.colors.accentContainer);
        painter.drawRoundedRect(square, radius, radius);
        iconColour = t.colors.textOnAccentContainer;
    }
    else if (m_hovered == item.id && item.enabled)
    {
        paintStateLayer(painter, square, radius, true, m_pressed == item.id);
        iconColour = t.colors.textPrimary;
    }
    if (m_keyboardFocus && m_focused == item.id)
    {
        paintFocusRing(painter, square, radius);
    }
    paintIcon(painter, icon(item.iconKey),
              QRectF(centre.x() - kRailIcon / 2.0, centre.y() - kRailIcon / 2.0, kRailIcon, kRailIcon), iconColour);

    const bool digit = item.section == RailSection::Place && (!item.badgeText.isEmpty() || item.floating);
    const bool count = item.section == RailSection::Foot && !item.badgeText.isEmpty();
    if (!digit && !count)
    {
        return;
    }
    const QFont font = badgeFont(t, digit ? QFont::DemiBold : QFont::Bold);
    const int padding = digit ? kRailBadgePadding : kRailPillPadding;
    const qreal width = (std::max)(kRailBadge, 2 * padding + textWidth(font, item.badgeText));
    QRectF badge;
    if (digit)
    {
        // A place's digit at its square's lower right.
        badge = QRectF(square.right() + kRailBadgeOffset - width, square.bottom() + kRailBadgeOffset - kRailBadge,
                       width, kRailBadge);
    }
    else
    {
        // A foot item's count at its square's upper right.
        badge = QRectF(square.right() + kRailPillOffset - width, square.top() - kRailPillOffset, width, kRailBadge);
    }
    const qreal badgeRadius = digit ? kRailBadgeRadius : kRailBadge / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.layers.surface1);
    painter.drawRoundedRect(badge.adjusted(-kRailBadgeRing, -kRailBadgeRing, kRailBadgeRing, kRailBadgeRing),
                            badgeRadius + kRailBadgeRing, badgeRadius + kRailBadgeRing);
    painter.setBrush(digit ? t.layers.surface4 : t.colors.statusWarning);
    painter.drawRoundedRect(badge, badgeRadius, badgeRadius);
    if (digit && item.floating)
    {
        // A workspace in a window of its own carries the window glyph.
        const QPointF middle = badge.center();
        paintIcon(painter, icon(QStringLiteral("window")),
                  QRectF(middle.x() - kTaskResultGlyph / 2.0, middle.y() - kTaskResultGlyph / 2.0, kTaskResultGlyph,
                         kTaskResultGlyph),
                  t.colors.textPrimary);
        return;
    }
    painter.setPen(digit ? t.colors.textPrimary : t.layers.surface0);
    painter.setFont(font);
    painter.drawText(badge, Qt::AlignCenter, item.badgeText);
}

void AnIconRail::paintOverflow(QPainter& painter, const QRect& row) const
{
    using namespace parts;
    const DesignTokens& t = tokens();
    const QPointF centre = QRectF(row).center();
    const QRectF square(centre.x() - kRailSquare / 2.0, centre.y() - kRailSquare / 2.0, kRailSquare, kRailSquare);
    if (m_hovered == overflowId())
    {
        paintStateLayer(painter, square, t.radius.md, true, m_pressed == overflowId());
    }
    if (m_keyboardFocus && m_focused == overflowId())
    {
        paintFocusRing(painter, square, t.radius.md);
    }
    painter.setPen(t.colors.textSecondary);
    painter.setFont(overflowFont(t));
    painter.drawText(square, Qt::AlignCenter, QStringLiteral("+%1").arg(m_layout.hiddenTasks));
}

bool AnIconRail::event(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::ToolTip:
        // The rail shows its own tooltip, at the item's right, after its own delay.
        event->accept();
        return true;
    case QEvent::Show:
    case QEvent::Hide: {
        const bool handled = QWidget::event(event);
        refreshTurning();
        return handled;
    }
    case QEvent::StyleChange: {
        const bool handled = QWidget::event(event);
        layOut();
        return handled;
    }
    default:
        return QWidget::event(event);
    }
}

void AnIconRail::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layOut();
    refreshTurning();
}

void AnIconRail::setHovered(const QString& id)
{
    if (m_hovered == id)
    {
        return;
    }
    const QRect before = id.isEmpty() || m_hovered.isEmpty() ? rect() : (itemRect(m_hovered) | itemRect(id));
    m_hovered = id;
    hideToolTip();
    if (!id.isEmpty() && !m_dragging && m_pressed.isEmpty())
    {
        m_toolTipTimer.start();
    }
    update(before);
}

void AnIconRail::showToolTip()
{
    if (m_hovered.isEmpty() || m_dragging)
    {
        return;
    }
    const QRect row = m_hovered == overflowId() ? m_layout.overflow : itemRect(m_hovered);
    if (row.isNull())
    {
        return;
    }
    if (m_toolTip == nullptr)
    {
        m_toolTip = new RailToolTip(this);
    }
    auto* const tip = static_cast<RailToolTip*>(m_toolTip);
    tip->setText(itemToolTip(m_hovered));
    QRect surface(QPoint(0, 0), tip->surfaceSize());
    surface.moveCenter(mapToGlobal(row.center()));
    surface.moveLeft(mapToGlobal(QPoint(width(), 0)).x() + parts::kRailTipGap);
    tip->setGeometry(surface.marginsAdded(tip->contentsMargins()));
    m_toolTipItem = m_hovered;
    tip->show();
}

void AnIconRail::hideToolTip()
{
    m_toolTipTimer.stop();
    m_toolTipItem.clear();
    if (m_toolTip != nullptr)
    {
        m_toolTip->hide();
    }
}

void AnIconRail::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    const QString id = itemAt(event->position().toPoint());
    const RailItem* const item = find(id);
    if (id.isEmpty() || (item != nullptr && !item->enabled))
    {
        return;
    }
    hideToolTip();
    m_pressed = id;
    m_pressGlobal = event->globalPosition().toPoint();
    m_holdFired = false;
    if (id != overflowId())
    {
        m_holdTimer.start();
    }
    update(id == overflowId() ? m_layout.overflow : itemRect(id));
    event->accept();
}

void AnIconRail::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint globalPos = event->globalPosition().toPoint();
    if (!m_pressed.isEmpty() && (event->buttons() & Qt::LeftButton))
    {
        if (m_dragging)
        {
            static_cast<RailGhost*>(m_ghost)->follow(globalPos);
            itemDragMoved(m_pressed, globalPos);
            return;
        }
        const RailItem* const item = find(m_pressed);
        if (item != nullptr && item->section == RailSection::Task &&
            (globalPos - m_pressGlobal).manhattanLength() >= QApplication::startDragDistance())
        {
            startDrag(globalPos);
        }
        return;
    }
    setHovered(itemAt(event->position().toPoint()));
}

void AnIconRail::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_holdTimer.stop();
    const QString pressed = m_pressed;
    m_pressed.clear();
    if (m_dragging)
    {
        finishDrag();
        itemDragReleased(pressed, event->globalPosition().toPoint());
        return;
    }
    if (!pressed.isEmpty() && !m_holdFired && itemAt(event->position().toPoint()) == pressed)
    {
        activate(pressed);
    }
    m_holdFired = false;
    update();
}

void AnIconRail::startDrag(const QPoint& globalPos)
{
    m_dragging = true;
    m_holdTimer.stop();
    hideToolTip();
    const RailItem* const item = find(m_pressed);
    if (m_ghost == nullptr)
    {
        m_ghost = new RailGhost(this);
    }
    auto* const ghost = static_cast<RailGhost*>(m_ghost);
    AnTaskThumbnail::Look look;
    look.glyph = icon(item->glyphKey);
    look.windowGlyph = icon(QStringLiteral("window"));
    look.owner = ghost;
    look.repaint = [ghost] { ghost->update(); };
    ghost->follow(globalPos);
    ghost->present(m_thumbnails.at(item->id).get(), *item, look);
    // Esc cancels the drag wherever the focus is.
    qApp->installEventFilter(this);
    itemDragStarted(m_pressed);
    itemDragMoved(m_pressed, globalPos);
}

void AnIconRail::finishDrag()
{
    m_dragging = false;
    qApp->removeEventFilter(this);
    if (m_ghost != nullptr)
    {
        m_ghost->hide();
    }
    update();
}

void AnIconRail::endDrag()
{
    m_holdTimer.stop();
    if (m_dragging)
    {
        finishDrag();
    }
    m_pressed.clear();
    m_holdFired = false;
}

void AnIconRail::setInsertionMark(const QString& id)
{
    if (id == m_insertionMark)
    {
        return;
    }
    const QRect before = insertionLineRect();
    m_insertionMark = id;
    update(before);
    update(insertionLineRect());
}

QString AnIconRail::insertionMark() const
{
    return m_insertionMark;
}

QRect AnIconRail::insertionLineRect() const
{
    const RailItem* const item = find(m_insertionMark);
    if (item == nullptr || item->section != RailSection::Task)
    {
        return {};
    }
    const QRect row = itemRect(item->id);
    if (row.isNull())
    {
        return {};
    }
    // In the gap above the row, centred as the task's box is.
    return QRect(row.left() + (row.width() - parts::kTaskBox) / 2, row.top() - parts::kRailInsertionLine,
                 parts::kTaskBox, parts::kRailInsertionLine);
}

bool AnIconRail::eventFilter(QObject* watched, QEvent* event)
{
    if (m_dragging && event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
    {
        const QString id = m_pressed;
        finishDrag();
        m_pressed.clear();
        itemDragCancelled(id);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void AnIconRail::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (m_layout.hiddenTasks == 0 || steps == 0)
    {
        QWidget::wheelEvent(event);
        return;
    }
    scrollTasksBy(-steps);
    event->accept();
}

void AnIconRail::scrollTasksBy(const int rows)
{
    const int shown = (std::max)(0, m_layout.taskRows - 1);
    const int offset = std::clamp(m_taskOffset + rows, 0, (std::max)(0, taskCount() - shown));
    if (offset == m_taskOffset)
    {
        return;
    }
    m_taskOffset = offset;
    layOut();
    refreshTurning();
    update();
}

void AnIconRail::ensureShown(const QString& id)
{
    const RailItem* const item = find(id);
    if (item == nullptr || item->section != RailSection::Task || !itemRect(id).isNull())
    {
        return;
    }
    int index = 0;
    for (const RailItem& other : m_items)
    {
        if (other.section != RailSection::Task)
        {
            continue;
        }
        if (other.id == id)
        {
            break;
        }
        ++index;
    }
    const int shown = (std::max)(1, m_layout.taskRows - 1);
    m_taskOffset = index < m_taskOffset ? index : index - shown + 1;
    layOut();
    refreshTurning();
}

void AnIconRail::contextMenuEvent(QContextMenuEvent* event)
{
    if (event->reason() == QContextMenuEvent::Keyboard)
    {
        // The menu key asked already, when the platform sends its context menu event after the key itself.
        if (std::chrono::steady_clock::now() - m_menuKeyHandled < kMenuKeyEcho)
        {
            event->accept();
            return;
        }
        const RailItem* const item = find(m_focused);
        if (item != nullptr && item->enabled)
        {
            itemMenuRequested(item->id, mapToGlobal(itemRect(item->id).center()));
        }
        event->accept();
        return;
    }
    const QString id = itemAt(event->pos());
    const RailItem* const item = find(id);
    if (item != nullptr && item->enabled)
    {
        itemMenuRequested(id, event->globalPos());
    }
    event->accept();
}

void AnIconRail::moveFocus(const int step)
{
    // Every item in the rail's order, the hidden tasks among them: moving onto one scrolls it into view.
    std::vector<const RailItem*> order;
    for (const RailSection section : {RailSection::Place, RailSection::Task, RailSection::Foot})
    {
        for (const RailItem& item : m_items)
        {
            if (item.section == section && item.enabled)
            {
                order.push_back(&item);
            }
        }
    }
    if (order.empty())
    {
        return;
    }
    const auto current =
        std::find_if(order.begin(), order.end(), [this](const RailItem* item) { return item->id == m_focused; });
    int index = 0;
    if (step == std::numeric_limits<int>::min())
    {
        index = 0;
    }
    else if (step == std::numeric_limits<int>::max())
    {
        index = static_cast<int>(order.size()) - 1;
    }
    else if (current != order.end())
    {
        index = std::clamp(static_cast<int>(current - order.begin()) + step, 0, static_cast<int>(order.size()) - 1);
    }
    m_focused = order[static_cast<std::size_t>(index)]->id;
    ensureShown(m_focused);
    update();
    if (QAccessible::isActive())
    {
        const auto shown = std::find(m_layout.order.begin(), m_layout.order.end(), m_focused);
        if (shown != m_layout.order.end())
        {
            QAccessibleEvent focus(this, QAccessible::Focus);
            focus.setChild(static_cast<int>(shown - m_layout.order.begin()));
            QAccessible::updateAccessibility(&focus);
        }
    }
}

void AnIconRail::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
    case Qt::Key_Up:
        moveFocus(-1);
        break;
    case Qt::Key_Down:
        moveFocus(1);
        break;
    case Qt::Key_Home:
        moveFocus(std::numeric_limits<int>::min());
        break;
    case Qt::Key_End:
        moveFocus(std::numeric_limits<int>::max());
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (!event->isAutoRepeat() && !m_focused.isEmpty())
        {
            activate(m_focused);
        }
        break;
    case Qt::Key_Menu:
        if (const RailItem* const item = find(m_focused); item != nullptr && item->enabled)
        {
            m_menuKeyHandled = std::chrono::steady_clock::now();
            itemMenuRequested(item->id, mapToGlobal(itemRect(item->id).center()));
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void AnIconRail::focusInEvent(QFocusEvent* event)
{
    m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    if (find(m_focused) == nullptr || itemRect(m_focused).isNull())
    {
        // The selected item that is shown, else the first.
        const auto selected = std::find_if(m_items.begin(), m_items.end(), [this](const RailItem& item) {
            return item.selected && item.enabled && !itemRect(item.id).isNull();
        });
        if (selected != m_items.end())
        {
            m_focused = selected->id;
        }
        else
        {
            m_focused.clear();
            moveFocus(std::numeric_limits<int>::min());
        }
    }
    QWidget::focusInEvent(event);
    update();
}

void AnIconRail::focusOutEvent(QFocusEvent* event)
{
    m_keyboardFocus = false;
    QWidget::focusOutEvent(event);
    update();
}

void AnIconRail::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    setHovered(QString());
}

void AnIconRail::activate(const QString& id)
{
    if (id == overflowId())
    {
        openTaskList();
        return;
    }
    const RailItem* const item = find(id);
    if (item != nullptr && item->enabled)
    {
        itemActivated(id);
    }
}

void AnIconRail::openTaskList()
{
    if (m_taskList == nullptr)
    {
        m_taskList = new QMenu(this);
    }
    m_taskList->clear();
    for (const RailItem& item : m_items)
    {
        if (item.section != RailSection::Task)
        {
            continue;
        }
        QString text = railItemDescription(item);
        text.replace(QLatin1Char('&'), QStringLiteral("&&"));
        QAction* const action = m_taskList->addAction(text);
        const auto thumbnail = m_thumbnails.find(item.id);
        if (thumbnail != m_thumbnails.end())
        {
            const QPixmap shown = thumbnail->second->shownPicture(m_taskList, [] {});
            if (!shown.isNull())
            {
                action->setIcon(QIcon(shown));
            }
        }
        const QString id = item.id;
        connect(action, &QAction::triggered, this, [this, id] { itemActivated(id); });
    }
    m_taskList->popup(mapToGlobal(QPoint(width() + parts::kRailTipGap, m_layout.overflow.top())));
}

} // namespace an::ui
