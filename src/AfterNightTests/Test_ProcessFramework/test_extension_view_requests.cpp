// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an extension asks of the view its session is bound to beyond its pixels - shapes over it, its selection area,
/// how it is shown, an Undo or a Redo: each request read with its fields; each limit refusing the whole request and
/// saying what was wrong; a host that did not state a request's capability refused; a session bound to no view
/// refused; and the shapes a session keeps held to their limits across its requests.

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionSdkService.h"
#include "app/ExtensionViewRequests.h"

#include <QCborArray>
#include <QCborMap>
#include <QString>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{

namespace ext = an::ui::extensions;

constexpr const char* kExtension = "an.test.view";
constexpr const char* kSession = "view-session";

/// Every capability of a request of the view, as a host states them.
[[nodiscard]] std::vector<std::string> everyViewCapability()
{
    return {std::string(ext::kViewOverlayCapability), std::string(ext::kViewSelectionCapability),
            std::string(ext::kViewDisplayCapability), std::string(ext::kViewHistoryCapability)};
}

/// A request a host sends, its session and request named.
[[nodiscard]] ext::ExtensionProtocolMessage requestOf(const std::string& type, QCborMap fields)
{
    ext::ExtensionProtocolMessage request;
    request.type = type;
    request.session_id = kSession;
    request.request_id = "request-1";
    request.fields = std::move(fields);
    return request;
}

/// An SDK service whose handler of a view's requests keeps what it was handed and answers as the case says.
struct ViewService
{
    ext::ExtensionSdkService service;
    std::vector<ext::ExtensionViewRequest> handed;
    std::vector<std::string> sessions;
    bool accept = true;
    std::string refusal = "the case refuses";

    ViewService()
    {
        service.setViewRequestHandler([this](const std::string_view session_id,
                                             const ext::ExtensionViewRequest& request, QCborMap* answer,
                                             std::string* reason) {
            handed.push_back(request);
            sessions.emplace_back(session_id);
            if (!accept)
            {
                *reason = refusal;
                return false;
            }
            (*answer)[QStringLiteral("handled")] = true;
            return true;
        });
    }

    /// The service's answer to a request from a host that stated `capabilities`.
    [[nodiscard]] ext::ExtensionProtocolMessage
    answer(const ext::ExtensionProtocolMessage& request,
           const std::vector<std::string>& capabilities = everyViewCapability())
    {
        ext::ExtensionProtocolMessage response;
        std::string error;
        EXPECT_TRUE(service.dispatchProtocolMessage(request, kExtension, ext::ExtensionLaunchMode::SingleImage,
                                                    capabilities, &response, &error))
            << error;
        return response;
    }
};

/// What a refusal said: its message and its details.
[[nodiscard]] std::string refusalOf(const ext::ExtensionProtocolMessage& response)
{
    ext::ExtensionProtocolError error;
    if (!ext::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")), &error))
    {
        return {};
    }
    return error.message + ": " + error.details;
}

/// A shape as a host states it.
[[nodiscard]] QCborMap shapeOf(const QString& kind, QCborArray points, const QString& color = QStringLiteral("#ff8000"))
{
    return QCborMap{{QStringLiteral("kind"), kind},
                    {QStringLiteral("points"), std::move(points)},
                    {QStringLiteral("color"), color}};
}

/// A shape of `count` points along a line.
[[nodiscard]] ext::ViewOverlayShape lineOf(const std::size_t count)
{
    ext::ViewOverlayShape shape;
    shape.points.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        shape.points.push_back(
            ext::ViewOverlayPoint{static_cast<double>(index % 1000u), static_cast<double>(index / 1000u)});
    }
    return shape;
}

} // namespace

// Each request reaches the application's handler with the fields the host stated, and its answer is the handler's,
// accepted, under the request's type.
TEST(ViewRequests, EachRequestIsReadWithItsFields)
{
    ViewService view;

    ext::ViewOverlayMessage overlay;
    overlay.session_id = kSession;
    overlay.request_id = "overlay";
    overlay.clear = false;
    overlay.remove = {3, 7};
    ext::ViewOverlayShape polygon;
    polygon.points = {{1.5, 2.0}, {10.0, 2.0}, {10.0, 8.25}};
    polygon.closed = true;
    polygon.filled = true;
    polygon.color = 0x80FF8000u;
    polygon.label = "NGC 2024";
    ext::ViewOverlayShape line;
    line.points = {{0.0, 0.0}, {-4.0, 1e6}};
    line.color = 0xFF00FFFFu;
    overlay.add = {polygon, line};
    ext::ExtensionProtocolMessage response = view.answer(ext::toProtocolMessage(overlay));
    EXPECT_EQ(response.type, "view.overlay.result");
    EXPECT_TRUE(response.fields.value(QStringLiteral("accepted")).toBool());
    EXPECT_TRUE(response.fields.value(QStringLiteral("handled")).toBool()) << "the answer is the handler's";
    ASSERT_EQ(view.handed.size(), 1u);
    EXPECT_EQ(view.sessions.back(), kSession);
    const auto* readOverlay = std::get_if<ext::ViewOverlayMessage>(&view.handed.back());
    ASSERT_NE(readOverlay, nullptr);
    EXPECT_FALSE(readOverlay->clear);
    EXPECT_EQ(readOverlay->remove, overlay.remove);
    ASSERT_EQ(readOverlay->add.size(), 2u);
    EXPECT_EQ(readOverlay->add[0], polygon);
    EXPECT_EQ(readOverlay->add[1], line);

    // A colour written without its alpha is opaque.
    QCborMap opaque{{QStringLiteral("add"),
                     QCborArray{shapeOf(QStringLiteral("line"), QCborArray{1, 2}, QStringLiteral("#12AB3f"))}}};
    response = view.answer(requestOf("view.overlay", opaque));
    EXPECT_EQ(response.type, "view.overlay.result") << refusalOf(response);
    readOverlay = std::get_if<ext::ViewOverlayMessage>(&view.handed.back());
    ASSERT_NE(readOverlay, nullptr);
    ASSERT_EQ(readOverlay->add.size(), 1u);
    EXPECT_EQ(readOverlay->add[0].color, 0xFF12AB3Fu);

    ext::ViewSelectionMessage selection;
    selection.session_id = kSession;
    selection.request_id = "selection";
    selection.enabled = true;
    selection.rect = ext::ViewSelectionRect{-5, 10, 64, 32};
    response = view.answer(ext::toProtocolMessage(selection));
    EXPECT_EQ(response.type, "view.selection.result");
    const auto* readSelection = std::get_if<ext::ViewSelectionMessage>(&view.handed.back());
    ASSERT_NE(readSelection, nullptr);
    EXPECT_TRUE(readSelection->enabled);
    EXPECT_EQ(readSelection->rect, selection.rect);
    selection.enabled = false;
    selection.rect.reset();
    response = view.answer(ext::toProtocolMessage(selection));
    readSelection = std::get_if<ext::ViewSelectionMessage>(&view.handed.back());
    ASSERT_NE(readSelection, nullptr);
    EXPECT_FALSE(readSelection->enabled);
    EXPECT_FALSE(readSelection->rect.has_value());

    ext::ViewDisplayMessage display;
    display.session_id = kSession;
    display.request_id = "display";
    display.zoom_percent = 250.0;
    display.center = ext::ViewOverlayPoint{120.5, 80.0};
    display.mode = "autostretch";
    response = view.answer(ext::toProtocolMessage(display));
    EXPECT_EQ(response.type, "view.display.result");
    const auto* readDisplay = std::get_if<ext::ViewDisplayMessage>(&view.handed.back());
    ASSERT_NE(readDisplay, nullptr);
    EXPECT_EQ(readDisplay->zoom_percent, display.zoom_percent);
    EXPECT_EQ(readDisplay->center, display.center);
    EXPECT_FALSE(readDisplay->fit);
    EXPECT_EQ(readDisplay->mode, "autostretch");
    ext::ViewDisplayMessage fit;
    fit.session_id = kSession;
    fit.request_id = "fit";
    fit.fit = true;
    response = view.answer(ext::toProtocolMessage(fit));
    readDisplay = std::get_if<ext::ViewDisplayMessage>(&view.handed.back());
    ASSERT_NE(readDisplay, nullptr);
    EXPECT_TRUE(readDisplay->fit);
    EXPECT_FALSE(readDisplay->zoom_percent.has_value());

    for (const ext::ViewHistoryAction action : {ext::ViewHistoryAction::Undo, ext::ViewHistoryAction::Redo})
    {
        ext::ViewHistoryMessage history;
        history.session_id = kSession;
        history.request_id = "history";
        history.action = action;
        response = view.answer(ext::toProtocolMessage(history));
        EXPECT_EQ(response.type, "view.history.result");
        const auto* readHistory = std::get_if<ext::ViewHistoryMessage>(&view.handed.back());
        ASSERT_NE(readHistory, nullptr);
        EXPECT_EQ(readHistory->action, action);
    }

    // What the handler refuses is answered refused, with its reason.
    view.accept = false;
    response = view.answer(ext::toProtocolMessage(fit));
    EXPECT_EQ(response.type, "view.display.error");
    EXPECT_NE(refusalOf(response).find("the case refuses"), std::string::npos) << refusalOf(response);
}

// Each limit refuses the whole request before the application acts on it, and the refusal says what was wrong.
TEST(ViewRequests, EachLimitRefusesTheWholeRequestAndSaysWhatWasWrong)
{
    QCborArray tooManyShapes;
    for (std::size_t index = 0; index <= ext::kViewOverlayShapeLimit; ++index)
    {
        tooManyShapes.push_back(shapeOf(QStringLiteral("line"), QCborArray{1, 1}));
    }
    QCborArray tooManyPoints;
    for (std::size_t index = 0; index <= ext::kViewOverlayPointLimit; ++index)
    {
        tooManyPoints.push_back(static_cast<double>(index % 4000u));
        tooManyPoints.push_back(1.0);
    }
    QCborArray tooManyRemoved;
    for (std::size_t index = 1; index <= ext::kViewOverlayShapeLimit + 1u; ++index)
    {
        tooManyRemoved.push_back(static_cast<qint64>(index));
    }
    const QString longLabel(static_cast<qsizetype>(ext::kViewOverlayLabelLimit + 1u), QLatin1Char('x'));
    QCborMap labelled = shapeOf(QStringLiteral("line"), QCborArray{1, 1});
    labelled.insert(QStringLiteral("label"), longLabel);
    QCborMap brokenLabel = shapeOf(QStringLiteral("line"), QCborArray{1, 1});
    brokenLabel.insert(QStringLiteral("label"), QStringLiteral("two\nlines"));
    QCborMap filledLine = shapeOf(QStringLiteral("line"), QCborArray{1, 1, 4, 4});
    filledLine.insert(QStringLiteral("filled"), true);
    QCborMap strayShapeField = shapeOf(QStringLiteral("polygon"), QCborArray{1, 1});
    strayShapeField.insert(QStringLiteral("width"), 3);

    struct Refused
    {
        const char* what;
        std::string type;
        QCborMap fields;
        const char* says;
    };
    const auto addOne = [](QCborMap shape) { return QCborMap{{QStringLiteral("add"), QCborArray{std::move(shape)}}}; };
    const std::vector<Refused> refused = {
        {"too many shapes", "view.overlay", QCborMap{{QStringLiteral("add"), tooManyShapes}}, "4097 shapes"},
        {"too many points", "view.overlay", addOne(shapeOf(QStringLiteral("line"), tooManyPoints)),
         "more than 262144 points"},
        {"too many removed", "view.overlay", QCborMap{{QStringLiteral("remove"), tooManyRemoved}}, "removes 4097"},
        {"a label too long", "view.overlay", addOne(labelled), "label is 121 characters long"},
        {"a label not plain", "view.overlay", addOne(brokenLabel), "label is not plain text"},
        {"a point not finite", "view.overlay",
         addOne(shapeOf(QStringLiteral("line"), QCborArray{1.0, std::numeric_limits<double>::quiet_NaN()})),
         "point 1 is not finite"},
        {"an infinite point", "view.overlay",
         addOne(shapeOf(QStringLiteral("polygon"), QCborArray{1, 2, std::numeric_limits<double>::infinity(), 3})),
         "point 2 is not finite"},
        {"no point", "view.overlay", addOne(shapeOf(QStringLiteral("line"), QCborArray{})), "at least one"},
        {"half a point", "view.overlay", addOne(shapeOf(QStringLiteral("line"), QCborArray{1, 2, 3})),
         "pairs of an x and a y"},
        {"a kind not known", "view.overlay", addOne(shapeOf(QStringLiteral("circle"), QCborArray{1, 2})),
         "neither 'polygon' nor 'line'"},
        {"a colour not written so", "view.overlay",
         addOne(shapeOf(QStringLiteral("line"), QCborArray{1, 2}, QStringLiteral("orange"))), "color is not written"},
        {"a filled line", "view.overlay", addOne(filledLine), "never filled"},
        {"a field a shape has not", "view.overlay", addOne(strayShapeField), "a field a shape has not: width"},
        {"a field the request has not", "view.overlay", QCborMap{{QStringLiteral("draw"), true}},
         "a field it has not: draw"},
        {"an id removed twice", "view.overlay", QCborMap{{QStringLiteral("remove"), QCborArray{2, 2}}},
         "removes shape 2 twice"},
        {"an id that is no id", "view.overlay", QCborMap{{QStringLiteral("remove"), QCborArray{0}}},
         "is not a shape's id"},
        {"a removal with a clear", "view.overlay",
         QCborMap{{QStringLiteral("clear"), true}, {QStringLiteral("remove"), QCborArray{1}}}, "clears every shape"},
        {"an area with no state", "view.selection", QCborMap{}, "enabled is not a bool"},
        {"a rectangle for an area turned off", "view.selection",
         QCborMap{{QStringLiteral("enabled"), false}, {QStringLiteral("rect"), QCborArray{0, 0, 4, 4}}},
         "an area it turns off"},
        {"a rectangle of no width", "view.selection",
         QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), QCborArray{0, 0, 0, 4}}}, "not positive"},
        {"a rectangle of three sides", "view.selection",
         QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), QCborArray{0, 0, 4}}}, "four integers"},
        {"a display of nothing", "view.display", QCborMap{}, "states none of"},
        {"a zoom of none", "view.display", QCborMap{{QStringLiteral("zoom"), 0}}, "zoom is not a finite positive"},
        {"fit with a zoom", "view.display", QCborMap{{QStringLiteral("fit"), true}, {QStringLiteral("zoom"), 100}},
         "fit to window with a zoom"},
        {"a centre of one number", "view.display", QCborMap{{QStringLiteral("center"), QCborArray{3}}},
         "center is not two finite numbers"},
        {"a mode that is no name", "view.display", QCborMap{{QStringLiteral("mode"), QStringLiteral("auto stretch")}},
         "mode is not a display mode's name"},
        {"an action not known", "view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("clear")}},
         "neither 'undo' nor 'redo'"},
        {"a history with a stray field", "view.history",
         QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}, {QStringLiteral("steps"), 2}},
         "a field it has not: steps"},
    };

    ViewService view;
    for (const Refused& refusal : refused)
    {
        SCOPED_TRACE(refusal.what);
        const ext::ExtensionProtocolMessage response = view.answer(requestOf(refusal.type, refusal.fields));
        EXPECT_EQ(response.type, refusal.type + ".error");
        const std::string said = refusalOf(response);
        EXPECT_NE(said.find("Invalid " + refusal.type + " request"), std::string::npos) << said;
        EXPECT_NE(said.find(refusal.says), std::string::npos) << said;
    }
    EXPECT_TRUE(view.handed.empty()) << "a request refused reached the application";
}

// A request comes only from a host that stated its capability in its hello; from another it is refused, naming the
// capability, and the application is not asked. A request with no request id is refused too.
TEST(ViewRequests, AHostThatDidNotStateTheCapabilityIsRefused)
{
    const std::vector<std::pair<std::string, QCborMap>> requests = {
        {"view.overlay", QCborMap{{QStringLiteral("clear"), true}}},
        {"view.selection", QCborMap{{QStringLiteral("enabled"), true}}},
        {"view.display", QCborMap{{QStringLiteral("fit"), true}}},
        {"view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}}},
    };
    ViewService view;
    for (const auto& [type, fields] : requests)
    {
        SCOPED_TRACE(type);
        const std::string capability(ext::viewRequestCapability(type));
        ASSERT_FALSE(capability.empty());
        std::vector<std::string> others = everyViewCapability();
        std::erase(others, capability);
        others.emplace_back(ext::kContributedProcessesCapability);
        ext::ExtensionProtocolMessage response = view.answer(requestOf(type, fields), others);
        EXPECT_EQ(response.type, type + ".error");
        EXPECT_NE(refusalOf(response).find("did not state the " + capability + " capability"), std::string::npos)
            << refusalOf(response);
        EXPECT_TRUE(view.handed.empty());

        response = view.answer(requestOf(type, fields), {capability});
        EXPECT_EQ(response.type, type + ".result") << refusalOf(response);
        EXPECT_EQ(view.handed.size(), 1u);
        view.handed.clear();
    }

    // With no one to answer them, the requests are refused.
    ext::ExtensionSdkService bare;
    ext::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(bare.dispatchProtocolMessage(requestOf("view.display", QCborMap{{QStringLiteral("fit"), true}}),
                                             kExtension, ext::ExtensionLaunchMode::SingleImage, everyViewCapability(),
                                             &response, &error))
        << error;
    EXPECT_EQ(response.type, "view.display.error");
    EXPECT_NE(refusalOf(response).find("answers no request of a view"), std::string::npos) << refusalOf(response);
}

// A session bound to no view is refused whatever it asks; the coordinator refuses a session it does not run.
TEST(ViewRequests, ASessionBoundToNoViewIsRefused)
{
    ext::ExtensionSessionController session;
    session.running_session.prepared_session.session_id = kSession;
    ext::ViewOverlayMessage overlay;
    overlay.add = {lineOf(2u)};
    const std::vector<ext::ExtensionViewRequest> requests = {overlay, ext::ViewSelectionMessage{},
                                                             ext::ViewDisplayMessage{}, ext::ViewHistoryMessage{}};
    for (const ext::ExtensionViewRequest& request : requests)
    {
        QCborMap answer;
        std::string reason;
        EXPECT_FALSE(ext::answerViewRequest(session, request, &answer, &reason));
        EXPECT_EQ(reason, "the session is bound to no view");
        EXPECT_TRUE(answer.isEmpty());
    }
    EXPECT_EQ(session.view_requests.shapeCount(), 0u) << "a refused overlay kept its shapes";

    ext::ExtensionHostLaunchCoordinator coordinator;
    ext::ExtensionProtocolMessage response;
    std::string error;
    ASSERT_TRUE(coordinator.sdkService().dispatchProtocolMessage(
        requestOf("view.display", QCborMap{{QStringLiteral("fit"), true}}), kExtension,
        ext::ExtensionLaunchMode::SingleImage, everyViewCapability(), &response, &error))
        << error;
    EXPECT_EQ(response.type, "view.display.error");
    EXPECT_NE(refusalOf(response).find("not one the application runs"), std::string::npos) << refusalOf(response);
}

// The shapes a session keeps are held to their limits across its requests: a request that would keep more shapes or
// more points than the limits, or remove a shape the session does not keep, is refused whole and changes nothing; each
// added shape is answered its id, and an id is never given twice.
TEST(ViewRequests, TheSessionsShapesAreHeldToTheirLimitsAcrossRequests)
{
    ext::ExtensionViewRequests requests;
    const auto overlayOf = [](std::vector<ext::ViewOverlayShape> add, std::vector<std::int64_t> remove = {},
                              const bool clear = false) {
        ext::ViewOverlayMessage message;
        message.add = std::move(add);
        message.remove = std::move(remove);
        message.clear = clear;
        return message;
    };
    const auto idsOf = [](const QCborMap& answer) {
        std::vector<std::int64_t> ids;
        for (const QCborValue id : answer.value(QStringLiteral("ids")).toArray())
        {
            ids.push_back(id.toInteger());
        }
        return ids;
    };

    std::vector<ext::ViewOverlayShape> many(ext::kViewOverlayShapeLimit - 1u, lineOf(1u));
    QCborMap answer;
    std::string reason;
    ASSERT_TRUE(requests.answerOverlay(overlayOf(many), &answer, &reason)) << reason;
    std::vector<std::int64_t> ids = idsOf(answer);
    ASSERT_EQ(ids.size(), many.size());
    EXPECT_EQ(ids.front(), 1);
    EXPECT_EQ(ids.back(), static_cast<std::int64_t>(many.size()));
    EXPECT_EQ(answer.value(QStringLiteral("shape_count")).toInteger(), static_cast<qint64>(many.size()));

    answer = {};
    EXPECT_FALSE(requests.answerOverlay(overlayOf({lineOf(1u), lineOf(1u)}), &answer, &reason));
    EXPECT_NE(reason.find("would keep 4097 shapes, more than its limit of 4096"), std::string::npos) << reason;
    EXPECT_TRUE(answer.isEmpty());
    EXPECT_EQ(requests.shapeCount(), many.size()) << "a refused request changed the shapes";

    EXPECT_FALSE(requests.answerOverlay(overlayOf({}, {1, 99999}), &answer, &reason));
    EXPECT_NE(reason.find("shape 99999 is not one the session keeps"), std::string::npos) << reason;
    EXPECT_EQ(requests.shapeCount(), many.size()) << "the first removal of a refused request was kept";

    // Removing one makes room for the one more the limit allows, and ids are never given again.
    ASSERT_TRUE(requests.answerOverlay(overlayOf({lineOf(1u), lineOf(1u)}, {1}), &answer, &reason)) << reason;
    ids = idsOf(answer);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], static_cast<std::int64_t>(many.size()) + 1);
    EXPECT_EQ(requests.shapeCount(), ext::kViewOverlayShapeLimit);

    // The points are held across the shapes: a clear and the most points, then one point more.
    ASSERT_TRUE(requests.answerOverlay(overlayOf({lineOf(ext::kViewOverlayPointLimit)}, {}, true), &answer, &reason))
        << reason;
    EXPECT_EQ(requests.shapeCount(), 1u);
    EXPECT_EQ(requests.pointCount(), ext::kViewOverlayPointLimit);
    EXPECT_EQ(idsOf(answer).front(), static_cast<std::int64_t>(many.size()) + 3) << "an id was given again";
    EXPECT_FALSE(requests.answerOverlay(overlayOf({lineOf(1u)}), &answer, &reason));
    EXPECT_NE(reason.find("would hold 262145 points, more than their limit of 262144"), std::string::npos) << reason;
    EXPECT_EQ(requests.pointCount(), ext::kViewOverlayPointLimit);

    ASSERT_TRUE(requests.answerOverlay(overlayOf({}, {}, true), &answer, &reason)) << reason;
    EXPECT_EQ(requests.shapeCount(), 0u);
    EXPECT_EQ(requests.pointCount(), 0u);
    EXPECT_EQ(answer.value(QStringLiteral("shape_count")).toInteger(), 0);
}
