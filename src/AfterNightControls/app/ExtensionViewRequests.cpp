// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ExtensionViewRequests.h"

#include "app/ExtensionHostManager.h"
#include "controls/ImageCropController.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ViewShapes.h"
#include "widgets/ImageViewWidget.h"

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

/// A rectangle stated in an image's pixels, kept inside an image of `size`: cut to it when larger, moved in when it
/// lies across an edge.
[[nodiscard]] QRect keptInside(const ViewSelectionRect& stated, const QSize& size)
{
    const int width = (std::min)(stated.width, size.width());
    const int height = (std::min)(stated.height, size.height());
    return QRect(std::clamp(stated.x, 0, size.width() - width), std::clamp(stated.y, 0, size.height() - height), width,
                 height);
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

    /// What the session's crop tool selects for: the host is told once a person lets the area go.
    class Selector final : public ImageAreaSelectorControl
    {
    public:
        explicit Selector(State& state) : m_state(state) {}

        void selectionAreaUpdated() override {}

        void selectionAreaChanged() override
        {
            m_state.tellReleased();
        }

        void newSelectionFinished() override
        {
            m_state.tellReleased();
        }

    private:
        State& m_state;
    };

    Selector selector{*this};
    std::function<void()> areaReleased;
    /// The tool that holds the selection area the session turned on, and the view it holds it on; null while it holds
    /// none.
    std::unique_ptr<ImageCropController> crop;
    QPointer<ImageViewControl> cropView;

    void tellReleased() const
    {
        if (areaReleased)
        {
            areaReleased();
        }
    }

    /// Turns off the area the session turned on, on a view still open, and lets its tool go.
    void releaseSelection()
    {
        if (!crop)
        {
            return;
        }
        if (cropView)
        {
            crop->destroy();
        }
        crop.reset();
        cropView = nullptr;
    }

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

bool ExtensionViewRequests::answerSelection(const ViewSelectionMessage& request, ImageViewControl* view,
                                            const std::vector<const ImageAreaSelectorControl*>& own, QCborMap* answer,
                                            std::string* reason)
{
    const auto refuse = [reason](std::string why) {
        if (reason)
        {
            *reason = std::move(why);
        }
        return false;
    };
    ImageViewWidget* const widget = view != nullptr ? view->getViewWidget() : nullptr;
    ImageViewScrollArea* const area = view != nullptr ? view->getScrollArea() : nullptr;
    const std::optional<an::ImageDescriptor> image = view != nullptr ? view->imageDescriptor() : std::nullopt;
    if (widget == nullptr || area == nullptr || !image.has_value())
    {
        return refuse("the view shows no image");
    }
    // A tool the session left on another view - the one it was bound to before - goes first.
    if (m_state->crop && m_state->cropView != view)
    {
        m_state->releaseSelection();
    }

    ImageCropController* const holder = ImageCropController::holding(area);
    const bool ours = holder != nullptr && holder == m_state->crop.get();
    const bool preview =
        holder != nullptr && !ours && std::find(own.begin(), own.end(), holder->selectorControl()) != own.end();
    if (holder != nullptr && !ours && !preview)
    {
        return refuse("another process holds the view's selection area");
    }

    if (!request.enabled)
    {
        if (preview)
        {
            return refuse("the selection area is the preview's part, which stays while the preview runs over a part");
        }
        m_state->releaseSelection();
    }
    else
    {
        const QSize size(static_cast<int>(image->extent().width()), static_cast<int>(image->extent().height()));
        const std::optional<QRect> rect =
            request.rect.has_value() ? std::optional<QRect>(keptInside(*request.rect, size)) : std::nullopt;
        if (preview)
        {
            // The preview's part is the same rectangle: moved where the host says, and the preview told it moved.
            if (rect.has_value())
            {
                holder->updateSelectionArea(*rect);
                holder->selectorControl()->selectionAreaChanged();
            }
        }
        else
        {
            if (!m_state->crop)
            {
                m_state->crop = std::make_unique<ImageCropController>(area, &m_state->selector);
                m_state->cropView = view;
                widget->clearSelectionAreaText();
                widget->setImageViewSelectionStyle(ImageViewSelectionStyle::AREA_SELECTION_STYLE);
                m_state->crop->setListenerEnabled(true);
            }
            if (rect.has_value())
            {
                m_state->crop->cancelNewSelection();
                m_state->crop->updateSelectionArea(*rect);
            }
            else
            {
                // As a new crop's: the area is drawn by the next drag on the image.
                m_state->crop->startNewSelection();
            }
        }
    }

    if (answer)
    {
        (*answer)[QStringLiteral("enabled")] = request.enabled;
        if (request.enabled && widget->isSelectionAreaEnabled())
        {
            const QRect shown = widget->getCurrentSelectionArea();
            (*answer)[QStringLiteral("rect")] = QCborArray{shown.x(), shown.y(), shown.width(), shown.height()};
        }
    }
    return true;
}

void ExtensionViewRequests::setAreaReleased(std::function<void()> released)
{
    m_state->areaReleased = std::move(released);
}

bool ExtensionViewRequests::holdsSelectionArea() const noexcept
{
    return m_state && m_state->crop != nullptr;
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
    m_state->releaseSelection();
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
    if (const auto* selection = std::get_if<ViewSelectionMessage>(&request))
    {
        // The area is the task's target's, as a page's image actions act on its target; an area the session's own
        // preview holds - its part, on the target or the hosted process's canvas - is the extension's own.
        std::vector<const ImageAreaSelectorControl*> own{&session};
        if (const auto* hosted = dynamic_cast<const ImageAreaSelectorControl*>(session.hosted_process.data()))
        {
            own.push_back(hosted);
        }
        return session.view_requests.answerSelection(*selection, session.target_control, own, answer, reason);
    }
    return refuse("the request is not answered for this session");
}

} // namespace an::ui::extensions
