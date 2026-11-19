// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionViewRequests.h"

#include "app/ExtensionHostManager.h"
#include "controls/ImageViewControl.h"
#include "controls/ViewShapes.h"

#include <QCborArray>

#include <algorithm>
#include <utility>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace an::ui::extensions
{

namespace
{

/// A shape as the protocol states it, as the view draws it.
[[nodiscard]] an::ui::ViewShape viewShapeOf(const ViewOverlayShape& stated)
{
    an::ui::ViewShape shape;
    shape.points.reserve(stated.points.size());
    for (const ViewOverlayPoint& point : stated.points)
    {
        shape.points.emplace_back(point.x, point.y);
    }
    shape.closed = stated.closed;
    shape.filled = stated.filled;
    shape.color = QColor::fromRgba(stated.color);
    shape.label = QString::fromStdString(stated.label);
    shape.bounds = an::ui::boundsOfPoints(shape.points);
    return shape;
}

} // namespace

struct ExtensionViewRequests::State
{
    /// A shape the session keeps, by the id it was answered.
    struct Kept
    {
        std::int64_t id = 0;
        an::ui::ViewShape shape;
    };

    std::vector<Kept> shapes;
    std::size_t points = 0u;
    std::int64_t nextId = 1;
    /// What the view is handed to draw, made again each time the shapes change; null for none.
    std::shared_ptr<const an::ui::ViewShapes> drawn;
    QPointer<ImageViewControl> drawnOn;
    std::string owner;

    void publish()
    {
        if (shapes.empty())
        {
            drawn.reset();
        }
        else
        {
            auto made = std::make_shared<an::ui::ViewShapes>();
            made->shapes.reserve(shapes.size());
            for (const Kept& kept : shapes)
            {
                made->shapes.push_back(kept.shape);
            }
            drawn = std::move(made);
        }
        if (drawnOn)
        {
            drawnOn->setOwnedShapes(owner, drawn);
        }
    }
};

ExtensionViewRequests::ExtensionViewRequests() : m_state(std::make_unique<State>()) {}

ExtensionViewRequests::~ExtensionViewRequests()
{
    release();
}

ExtensionViewRequests::ExtensionViewRequests(ExtensionViewRequests&&) noexcept = default;
ExtensionViewRequests& ExtensionViewRequests::operator=(ExtensionViewRequests&&) noexcept = default;

bool ExtensionViewRequests::answerOverlay(const ViewOverlayMessage& request, QCborMap* answer, std::string* reason)
{
    const auto refuse = [reason](std::string why) {
        if (reason)
        {
            *reason = std::move(why);
        }
        return false;
    };

    // The request is applied to a copy, so a refused one leaves the session's shapes as they were.
    std::vector<State::Kept> shapes = request.clear ? std::vector<State::Kept>() : m_state->shapes;
    for (const std::int64_t id : request.remove)
    {
        const auto found =
            std::find_if(shapes.begin(), shapes.end(), [id](const State::Kept& kept) { return kept.id == id; });
        if (found == shapes.end())
        {
            return refuse(fmt::format("shape {} is not one the session keeps", id));
        }
        shapes.erase(found);
    }
    std::size_t points = 0u;
    for (const State::Kept& kept : shapes)
    {
        points += kept.shape.points.size();
    }
    for (const ViewOverlayShape& added : request.add)
    {
        points += added.points.size();
    }
    if (shapes.size() + request.add.size() > kViewOverlayShapeLimit)
    {
        return refuse(fmt::format("the session would keep {} shapes, more than its limit of {}",
                                  shapes.size() + request.add.size(), kViewOverlayShapeLimit));
    }
    if (points > kViewOverlayPointLimit)
    {
        return refuse(fmt::format("the session's shapes would hold {} points, more than their limit of {}", points,
                                  kViewOverlayPointLimit));
    }

    QCborArray ids;
    std::int64_t next = m_state->nextId;
    for (const ViewOverlayShape& added : request.add)
    {
        const std::int64_t id = next++;
        shapes.push_back(State::Kept{id, viewShapeOf(added)});
        ids.push_back(static_cast<qint64>(id));
    }
    m_state->shapes = std::move(shapes);
    m_state->points = points;
    m_state->nextId = next;
    m_state->publish();

    if (answer)
    {
        (*answer)[QStringLiteral("ids")] = ids;
        (*answer)[QStringLiteral("shape_count")] = static_cast<qint64>(m_state->shapes.size());
        (*answer)[QStringLiteral("point_count")] = static_cast<qint64>(m_state->points);
    }
    return true;
}

void ExtensionViewRequests::drawOn(const std::string& owner, ImageViewControl* view)
{
    m_state->owner = owner;
    if (m_state->drawnOn == view)
    {
        return;
    }
    if (m_state->drawnOn)
    {
        m_state->drawnOn->setOwnedShapes(m_state->owner, nullptr);
    }
    m_state->drawnOn = view;
    if (view != nullptr && m_state->drawn)
    {
        view->setOwnedShapes(m_state->owner, m_state->drawn);
    }
}

void ExtensionViewRequests::release()
{
    if (!m_state)
    {
        return;
    }
    m_state->shapes.clear();
    m_state->points = 0u;
    m_state->drawn.reset();
    if (m_state->drawnOn)
    {
        m_state->drawnOn->setOwnedShapes(m_state->owner, nullptr);
    }
    m_state->drawnOn = nullptr;
}

std::size_t ExtensionViewRequests::shapeCount() const noexcept
{
    return m_state ? m_state->shapes.size() : 0u;
}

std::size_t ExtensionViewRequests::pointCount() const noexcept
{
    return m_state ? m_state->points : 0u;
}

ImageViewControl* ExtensionViewRequests::drawnOn() const noexcept
{
    return m_state ? m_state->drawnOn.data() : nullptr;
}

bool answerViewRequest(ExtensionSessionController& session, const ExtensionViewRequest& request, QCborMap* answer,
                       std::string* reason)
{
    const auto refuse = [reason](std::string why) {
        if (reason)
        {
            *reason = std::move(why);
        }
        return false;
    };
    ImageViewControl* const view = session.requestView();
    if (session.target_control == nullptr || view == nullptr)
    {
        return refuse("the session is bound to no view");
    }

    if (const auto* overlay = std::get_if<ViewOverlayMessage>(&request))
    {
        session.view_requests.drawOn(session.sessionId(), view);
        return session.view_requests.answerOverlay(*overlay, answer, reason);
    }
    return refuse("the request is not answered for this session");
}

} // namespace an::ui::extensions
