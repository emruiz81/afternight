// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an extension's session asks of the view it is bound to beyond its pixels, and what it leaves there: the shapes
/// it draws over the view (`view.overlay`). A request names no view: it acts on the view the session is bound to, or,
/// for an extension hosted in a page, on the page's canvas, where the shapes are drawn too. A request is answered on
/// the GUI thread when the session's messages are pumped and waits for nothing. Nothing a session leaves outlives it:
/// its shapes go when it ends, when its host dies and when the view closes.
#pragma once

#include "AfterNightControlsExport.h"

#include "app/ExtensionSdkService.h"

#include <QCborMap>
#include <QPointer>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ImageViewControl;

namespace an::ui
{
struct ViewShape;
struct ViewShapes;
} // namespace an::ui

namespace an::ui::extensions
{

struct ExtensionSessionController;

/// One session's requests of its view, and what they left on it. GUI thread.
class AFTERNIGHTCONTROLS_API ExtensionViewRequests
{
public:
    ExtensionViewRequests();
    ~ExtensionViewRequests();
    ExtensionViewRequests(const ExtensionViewRequests&) = delete;
    ExtensionViewRequests& operator=(const ExtensionViewRequests&) = delete;
    ExtensionViewRequests(ExtensionViewRequests&&) noexcept;
    ExtensionViewRequests& operator=(ExtensionViewRequests&&) noexcept;

    /// Answers a `view.overlay`: every shape cleared, the shapes of `remove` taken away, the shapes of `add` added and
    /// given their ids, in that order. Refused whole, and nothing changed, when a shape to remove is not one the
    /// session keeps, or when the session would keep more than `kViewOverlayShapeLimit` shapes or
    /// `kViewOverlayPointLimit` points; the answer gives each added shape's id, in order, and how many shapes and
    /// points the session keeps.
    [[nodiscard]] bool answerOverlay(const ViewOverlayMessage& request, QCborMap* answer, std::string* reason);

    /// Draws the session's shapes over `view` under `owner`, taking them from the view they were drawn on; null draws
    /// them nowhere. Nothing is done when they are drawn on `view` already.
    void drawOn(const std::string& owner, ImageViewControl* view);

    /// Takes away everything the session left on its views: its shapes.
    void release();

    /// How many shapes the session keeps.
    [[nodiscard]] std::size_t shapeCount() const noexcept;

    /// How many points the shapes the session keeps hold together.
    [[nodiscard]] std::size_t pointCount() const noexcept;

    /// The view the session's shapes are drawn on, or null.
    [[nodiscard]] ImageViewControl* drawnOn() const noexcept;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// Answers what an extension asked of its view, for its session: refused, with why, for a session bound to no view, and
/// otherwise as the request's kind is answered. The view is the one `ExtensionSessionController::requestView` names.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool answerViewRequest(ExtensionSessionController& session,
                                                            const ExtensionViewRequest& request, QCborMap* answer,
                                                            std::string* reason);

} // namespace an::ui::extensions
