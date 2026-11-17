// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "workspaces/WorkspaceMiniature.h"

#include "controls/HomeParts.h"
#include "controls/ImageViewControl.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <kddockwidgets/DockWidget.h>
#include <kddockwidgets/MainWindow.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtwidgets/views/Group.h>

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace an::ui::observatory
{

namespace
{

/// The group of the dock library a view's dock widget is tabbed in; null for a view in no group of `area`.
QWidget* groupOf(ImageViewControl& view, const QWidget& area)
{
    KDDockWidgets::QtWidgets::DockWidget* const dock = view.getDockWidget();
    if (dock == nullptr || !area.isAncestorOf(dock))
    {
        return nullptr;
    }
    for (QWidget* parent = dock->parentWidget(); parent != nullptr && parent != &area; parent = parent->parentWidget())
    {
        if (qobject_cast<KDDockWidgets::QtWidgets::Group*>(parent) != nullptr)
        {
            return parent;
        }
    }
    return nullptr;
}

/// The distinct values of a list, within a pixel of each other.
int distinct(std::vector<int> values)
{
    std::sort(values.begin(), values.end());
    int count = 0;
    int last = 0;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index == 0 || std::abs(values[index] - last) > 1)
        {
            ++count;
        }
        last = values[index];
    }
    return count;
}

} // namespace

MiniatureLayout miniatureLayoutOf(KDDockWidgets::QtWidgets::MainWindow& area,
                                  const std::vector<ImageViewControl*>& views)
{
    // Each group with its tabs, in the order of their tabs, the one shown marked.
    struct Held
    {
        QRect place;
        std::vector<std::pair<int, QString>> tabs;
        QString shown;
    };
    std::map<QWidget*, Held> groups;
    std::vector<QWidget*> order;
    for (ImageViewControl* view : views)
    {
        if (view == nullptr)
        {
            continue;
        }
        QWidget* const group = groupOf(*view, area);
        if (group == nullptr)
        {
            continue;
        }
        auto [found, made] = groups.try_emplace(group);
        if (made)
        {
            found->second.place = QRect(group->mapTo(&area, QPoint(0, 0)), group->size());
            order.push_back(group);
        }
        KDDockWidgets::Core::DockWidget* const controller = view->getDockWidget()->dockWidget();
        const QString name = QString::fromUtf8(view->getUniqueName());
        found->second.tabs.emplace_back(controller != nullptr ? controller->tabIndex() : 0, name);
        if (controller != nullptr && controller->isCurrentTab())
        {
            found->second.shown = name;
        }
    }
    MiniatureLayout layout;
    if (groups.empty())
    {
        return layout;
    }
    // The places are shares of what the groups cover together.
    QRect covered;
    for (QWidget* group : order)
    {
        covered = covered.united(groups[group].place);
    }
    const double width = (std::max)(1, covered.width());
    const double height = (std::max)(1, covered.height());
    for (QWidget* group : order)
    {
        Held& held = groups[group];
        std::sort(held.tabs.begin(), held.tabs.end());
        MiniatureGroup made;
        made.place = QRectF((held.place.x() - covered.x()) / width, (held.place.y() - covered.y()) / height,
                            held.place.width() / width, held.place.height() / height);
        for (const auto& [index, name] : held.tabs)
        {
            made.tabs.append(name);
        }
        made.current = (std::max)(0, static_cast<int>(made.tabs.indexOf(held.shown)));
        layout.groups.push_back(std::move(made));
    }
    // In reading order: by row, then by column.
    std::stable_sort(layout.groups.begin(), layout.groups.end(),
                     [](const MiniatureGroup& lhs, const MiniatureGroup& rhs) {
                         if (std::abs(lhs.place.y() - rhs.place.y()) > 0.01)
                         {
                             return lhs.place.y() < rhs.place.y();
                         }
                         return lhs.place.x() < rhs.place.x();
                     });
    return layout;
}

QString miniatureArrangementText(const MiniatureLayout& layout)
{
    const std::size_t groups = layout.groups.size();
    if (groups == 0)
    {
        return QCoreApplication::translate("an::ui::observatory::WorkspaceMiniature", "empty");
    }
    if (groups == 1)
    {
        const int tabs = static_cast<int>(layout.groups.front().tabs.size());
        return tabs <= 1 ? QCoreApplication::translate("an::ui::observatory::WorkspaceMiniature", "1 image")
                         : QCoreApplication::translate("an::ui::observatory::WorkspaceMiniature", "%1 tabs").arg(tabs);
    }
    // Groups that make a grid: as many columns as distinct lefts, as many rows as distinct tops, each the same size.
    std::vector<int> lefts;
    std::vector<int> tops;
    for (const MiniatureGroup& group : layout.groups)
    {
        lefts.push_back(static_cast<int>(std::lround(group.place.x() * 1000.0)));
        tops.push_back(static_cast<int>(std::lround(group.place.y() * 1000.0)));
    }
    const int columns = distinct(lefts);
    const int rows = distinct(tops);
    if (static_cast<std::size_t>(columns * rows) == groups)
    {
        return QCoreApplication::translate("an::ui::observatory::WorkspaceMiniature", "%1 × %2 split")
            .arg(columns)
            .arg(rows);
    }
    return QCoreApplication::translate("an::ui::observatory::WorkspaceMiniature", "%1 groups").arg(groups);
}

struct WorkspaceMiniature::Impl
{
    MiniatureLayout layout;
    // The pictures by their views' names, each shown in Night's form under Night.
    std::map<QString, std::unique_ptr<an::ui::NightPicture>> pictures;
};

WorkspaceMiniature::WorkspaceMiniature(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("workspaceMiniature"));
    setFixedSize(an::ui::parts::kWorkspaceMiniatureWidth, an::ui::parts::kWorkspaceMiniatureHeight);
}

WorkspaceMiniature::~WorkspaceMiniature() = default;

void WorkspaceMiniature::setMiniatureLayout(MiniatureLayout layout)
{
    m_impl->layout = std::move(layout);
    update();
}

const MiniatureLayout& WorkspaceMiniature::miniatureLayout() const noexcept
{
    return m_impl->layout;
}

void WorkspaceMiniature::setPicture(const QString& view, const QImage& picture)
{
    if (picture.isNull())
    {
        m_impl->pictures.erase(view);
    }
    else
    {
        auto& held = m_impl->pictures[view];
        if (!held)
        {
            held = std::make_unique<an::ui::NightPicture>();
        }
        held->setPicture(picture);
    }
    update();
}

int WorkspaceMiniature::picturesShown() const
{
    int shown = 0;
    for (const MiniatureGroup& group : m_impl->layout.groups)
    {
        if (group.current >= 0 && group.current < group.tabs.size() &&
            m_impl->pictures.count(group.tabs.at(group.current)) > 0)
        {
            ++shown;
        }
    }
    return shown;
}

bool WorkspaceMiniature::showsTabStrip(const int group) const
{
    return group >= 0 && group < static_cast<int>(m_impl->layout.groups.size()) &&
           m_impl->layout.groups[static_cast<std::size_t>(group)].tabs.size() > 1;
}

QSize WorkspaceMiniature::sizeHint() const
{
    return {an::ui::parts::kWorkspaceMiniatureWidth, an::ui::parts::kWorkspaceMiniatureHeight};
}

QSize WorkspaceMiniature::minimumSizeHint() const
{
    return sizeHint();
}

void WorkspaceMiniature::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = an::ui::parts::kWorkspaceMiniatureRadius;
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setClipPath(clip);
    painter.fillRect(rect(), t.colors.bgCanvas);
    const qreal gap = an::ui::parts::kWorkspaceMiniatureGap;
    const QRectF area = QRectF(rect());
    for (const MiniatureGroup& group : m_impl->layout.groups)
    {
        QRectF place(area.x() + group.place.x() * area.width(), area.y() + group.place.y() * area.height(),
                     group.place.width() * area.width(), group.place.height() * area.height());
        // Two pixels between neighbours: half of it at each inner side.
        place = place.adjusted(group.place.x() > 0.001 ? gap / 2 : 0, group.place.y() > 0.001 ? gap / 2 : 0,
                               group.place.right() < 0.999 ? -gap / 2 : 0, group.place.bottom() < 0.999 ? -gap / 2 : 0);
        QRectF image = place;
        if (group.tabs.size() > 1)
        {
            // The strip of tabs: the one shown underlined in the accent.
            const QRectF strip(place.x(), place.y(), place.width(), an::ui::parts::kWorkspaceMiniatureTabStrip);
            painter.fillRect(strip, t.layers.surface3);
            qreal x = strip.x() + an::ui::parts::kWorkspaceMiniatureTabGap + 1;
            const qreal top = strip.y() + (strip.height() - an::ui::parts::kWorkspaceMiniatureTabHeight) / 2.0;
            for (int index = 0; index < group.tabs.size(); ++index)
            {
                const QRectF tab(x, top, an::ui::parts::kWorkspaceMiniatureTabWidth,
                                 an::ui::parts::kWorkspaceMiniatureTabHeight);
                if (tab.right() > strip.right())
                {
                    break;
                }
                const auto found = m_impl->pictures.find(group.tabs.at(index));
                const QPixmap shown =
                    found != m_impl->pictures.end() ? found->second->shown(this, [this]() { update(); }) : QPixmap();
                painter.save();
                painter.setOpacity(index == group.current ? 1.0 : an::ui::parts::kDisabledOpacity);
                an::ui::paintHomePicture(painter, tab, shown, an::ui::parts::kWorkspaceMiniatureGap);
                painter.restore();
                if (index == group.current)
                {
                    // The tab shown is underlined in the accent, as thick as the gap between the groups.
                    painter.fillRect(QRectF(tab.x(), tab.bottom(), tab.width(), an::ui::parts::kWorkspaceMiniatureGap),
                                     t.colors.accentPrimary);
                }
                x += an::ui::parts::kWorkspaceMiniatureTabWidth + an::ui::parts::kWorkspaceMiniatureTabGap;
            }
            image.setTop(strip.bottom());
        }
        const QString current =
            group.current >= 0 && group.current < group.tabs.size() ? group.tabs.at(group.current) : QString();
        const auto found = m_impl->pictures.find(current);
        const QPixmap shown =
            found != m_impl->pictures.end() ? found->second->shown(this, [this]() { update(); }) : QPixmap();
        an::ui::paintHomePicture(painter, image, shown, 0.0);
    }
}

} // namespace an::ui::observatory
