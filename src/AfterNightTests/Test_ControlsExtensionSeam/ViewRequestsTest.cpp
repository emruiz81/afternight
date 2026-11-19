// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What an extension asks of the view its session is bound to, over a stand-in for its host and a view: the selection
/// area turned on and shown, drawn by the next drag when no rectangle is stated, kept inside the image, the host told
/// once, on release, with the rectangle; refused for a view whose area another process holds; turned off by the
/// session's end when the session turned it on and left when it did not; and the same rectangle for an extension whose
/// preview runs over a part.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ControlsExtensionSeam/HostedPanelStandIn.h"
#include "Test_ControlsExtensionSeam/HostedProcessContext.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "app/AfterNightExtensionProtocol.h"
#include "app/ExtensionHostManager.h"
#include "app/ExtensionSdkService.h"
#include "app/ExtensionViewRequests.h"
#include "controls/ImageCropController.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewScrollArea.h"
#include "session/ImageDocument.h"
#include "widgets/ImageViewWidget.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::seam;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::settle;
using an::tests::viewer::ViewHandle;
using an::ui::extensions::ExtensionHostManager;
using an::ui::extensions::ExtensionProtocolMessage;
using an::ui::extensions::ExtensionSessionController;

constexpr const char* kSessionId = "view-session";
constexpr std::uint32_t kWidth = 64u;
constexpr std::uint32_t kHeight = 48u;

/// A selector another process's crop tool selects for.
class OtherProcess final : public ImageAreaSelectorControl
{
public:
    void selectionAreaUpdated() override {}
    void selectionAreaChanged() override {}
    void newSelectionFinished() override {}
};

/// A rectangle as a request or an answer states it.
[[nodiscard]] QCborArray rectOf(const int x, const int y, const int width, const int height)
{
    return QCborArray{x, y, width, height};
}

/// A rectangle an answer stated, or none.
[[nodiscard]] std::optional<QRect> answeredRect(const ExtensionProtocolMessage& answer)
{
    const QCborValue rect = answer.fields.value(QStringLiteral("rect"));
    if (!rect.isArray() || rect.toArray().size() != 4)
    {
        return std::nullopt;
    }
    const QCborArray sides = rect.toArray();
    return QRect(static_cast<int>(sides.at(0).toInteger()), static_cast<int>(sides.at(1).toInteger()),
                 static_cast<int>(sides.at(2).toInteger()), static_cast<int>(sides.at(3).toInteger()));
}

class ViewRequestsSeam : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::optional<an::tests::controls::TestSessionProvider> provider =
            an::tests::viewer::openFileProvider(2u, an::consumer::MemoryAllowances{});
        ASSERT_TRUE(provider.has_value());
        m_context = std::make_unique<HostedProcessContext>(std::move(provider->owned()));
        m_installed.emplace(*m_context);
        m_target = makeView("target");
        std::optional<an::ui::ImageDocument> document =
            makeDocument<float>(m_context->engineSession(), kWidth, kHeight, Channels::Rgb,
                                [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                    return static_cast<float>(x + y) / static_cast<float>(kWidth + kHeight);
                                });
        ASSERT_TRUE(document.has_value());
        m_target->setDocument(std::move(*document));
        m_target->resize(400, 300);
        m_target->show();
        m_target->getViewWidget()->scaleImageView(1.0, true);
        ASSERT_TRUE(settle(*m_target));
        m_target->initializeNewImageHistory("Image opened");

        m_controller = std::make_unique<ExtensionSessionController>();
        m_controller->descriptor_id = "seam.extension.view";
        m_controller->display_name = "View Requests";
        m_controller->target_control = m_target.get();
        m_host.emplace();
        m_host->install(*m_controller, kSessionId);
        auto& running = m_controller->running_session;
        running.hosted_panel = false;
        running.handshake.hello.capabilities = {std::string(an::ui::extensions::kViewOverlayCapability),
                                                std::string(an::ui::extensions::kViewSelectionCapability),
                                                std::string(an::ui::extensions::kViewDisplayCapability),
                                                std::string(an::ui::extensions::kViewHistoryCapability)};
        m_sdk.setViewRequestHandler([this](const std::string_view,
                                           const an::ui::extensions::ExtensionViewRequest& request, QCborMap* answer,
                                           std::string* reason) {
            return an::ui::extensions::answerViewRequest(*m_controller, request, answer, reason);
        });
        m_manager.setSdkService(&m_sdk);
        m_controller->view_requests.setAreaReleased([this]() {
            ++m_released;
            m_releasedArea = widget()->getCurrentSelectionArea();
        });
    }

    void TearDown() override
    {
        QThreadPool::globalInstance()->waitForDone();
        pumpQuietly();
        m_controller.reset();
        m_host.reset();
        m_target.reset();
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] ImageViewWidget* widget() const
    {
        return m_target->getViewWidget();
    }

    void pump()
    {
        std::string error;
        static_cast<void>(m_manager.pumpSession(&m_controller->running_session, 0, &error));
    }

    /// Sends a request as the host does and waits for its answer, accepted or refused.
    [[nodiscard]] ExtensionProtocolMessage ask(const std::string& type, const QCborMap& fields)
    {
        ExtensionProtocolMessage request;
        request.type = type;
        request.request_id = "request-" + std::to_string(++m_requests);
        request.fields = fields;
        m_host->send(request);
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 5000)
        {
            pump();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            for (const std::string& answered : {type + ".result", type + ".error"})
            {
                if (m_host->waiting(answered) > 0u)
                {
                    std::optional<ExtensionProtocolMessage> taken = m_host->next(answered, [this]() { pump(); });
                    if (taken.has_value())
                    {
                        return *taken;
                    }
                }
            }
        }
        ADD_FAILURE() << "no answer to " << type;
        return {};
    }

    /// What a refusal said.
    [[nodiscard]] static std::string refusalOf(const ExtensionProtocolMessage& response)
    {
        an::ui::extensions::ExtensionProtocolError error;
        if (!an::ui::extensions::protocolErrorFromCborValue(response.fields.value(QStringLiteral("error")), &error))
        {
            return {};
        }
        return error.message + ": " + error.details;
    }

    /// A drag on the view's image, as a person makes one: over `from`, pressed there, moved through the steps, released
    /// at `to`.
    void drag(const QPointF from, const QPointF to, const int steps = 4,
              const std::function<void()>& afterEachMove = {})
    {
        ImageViewWidget* const view = widget();
        const auto send = [view](const QEvent::Type type, const QPointF at, const Qt::MouseButton button,
                                 const Qt::MouseButtons buttons) {
            QMouseEvent event(type, at, view->mapToGlobal(at), button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &event);
        };
        // The pointer comes over the image first, as a mouse does: the tool learns which part of the area it is over.
        send(QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= steps; ++step)
        {
            send(QEvent::MouseMove, from + (to - from) * (static_cast<double>(step) / steps), Qt::NoButton,
                 Qt::LeftButton);
            if (afterEachMove)
            {
                afterEachMove();
            }
        }
        send(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }

    std::unique_ptr<HostedProcessContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_target;
    std::unique_ptr<ExtensionSessionController> m_controller;
    std::optional<HostedPanelStandIn> m_host;
    ExtensionHostManager m_manager;
    an::ui::extensions::ExtensionSdkService m_sdk;
    int m_requests = 0;
    int m_released = 0;
    QRect m_releasedArea;
};

/// The selection area a session asks for.
class ViewSelection : public ViewRequestsSeam
{
};

} // namespace

// Turned on with a rectangle, the view shows its selection area there, and the answer says where it is.
TEST_F(ViewSelection, TurningItOnShowsTheViewsSelectionArea)
{
    const ExtensionProtocolMessage answer = ask(
        "view.selection", QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(8, 6, 20, 10)}});
    ASSERT_EQ(answer.type, "view.selection.result") << refusalOf(answer);
    EXPECT_TRUE(widget()->isSelectionAreaEnabled());
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(8, 6, 20, 10));
    EXPECT_EQ(answeredRect(answer), QRect(8, 6, 20, 10));
    EXPECT_TRUE(answer.fields.value(QStringLiteral("enabled")).toBool());
    EXPECT_TRUE(m_controller->view_requests.holdsSelectionArea());

    const ExtensionProtocolMessage off = ask("view.selection", QCborMap{{QStringLiteral("enabled"), false}});
    ASSERT_EQ(off.type, "view.selection.result") << refusalOf(off);
    EXPECT_FALSE(widget()->isSelectionAreaEnabled()) << "turned off, the area the session turned on stays";
    EXPECT_FALSE(m_controller->view_requests.holdsSelectionArea());
}

// Turned on with no rectangle, the view shows none until the next drag on the image draws one, as a new crop's does;
// the host is told once, when the drag ends.
TEST_F(ViewSelection, WithNoRectangleTheNextDragDrawsOne)
{
    const ExtensionProtocolMessage answer = ask("view.selection", QCborMap{{QStringLiteral("enabled"), true}});
    ASSERT_EQ(answer.type, "view.selection.result") << refusalOf(answer);
    EXPECT_FALSE(answeredRect(answer).has_value()) << "no area is drawn yet";
    EXPECT_FALSE(widget()->isSelectionAreaEnabled());

    drag(QPointF(10.0, 8.0), QPointF(40.0, 30.0), 4, [this]() { EXPECT_EQ(m_released, 0) << "told while it moved"; });
    EXPECT_TRUE(widget()->isSelectionAreaEnabled());
    const QRect drawn = widget()->getCurrentSelectionArea();
    EXPECT_NEAR(drawn.x(), 10, 1);
    EXPECT_NEAR(drawn.y(), 8, 1);
    EXPECT_NEAR(drawn.width(), 30, 2);
    EXPECT_NEAR(drawn.height(), 22, 2);
    EXPECT_EQ(m_released, 1);
    EXPECT_EQ(m_releasedArea, drawn);
}

// A rectangle the host sets is kept inside the image: moved in when it lies across an edge, cut to the image when it is
// larger.
TEST_F(ViewSelection, ASetRectangleIsKeptInsideTheImage)
{
    ExtensionProtocolMessage answer = ask("view.selection", QCborMap{{QStringLiteral("enabled"), true},
                                                                     {QStringLiteral("rect"), rectOf(50, 40, 30, 20)}});
    ASSERT_EQ(answer.type, "view.selection.result") << refusalOf(answer);
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(34, 28, 30, 20));
    EXPECT_EQ(answeredRect(answer), QRect(34, 28, 30, 20));

    answer = ask("view.selection",
                 QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(-10, -5, 100, 100)}});
    ASSERT_EQ(answer.type, "view.selection.result") << refusalOf(answer);
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight)));
}

// A person moves the area as today; the host is told once, when the mouse is released, with the rectangle in the view's
// state - never for each move.
TEST_F(ViewSelection, TheHostIsToldOnceOnReleaseWithTheRectangle)
{
    ASSERT_EQ(ask("view.selection",
                  QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(8, 6, 20, 10)}})
                  .type,
              "view.selection.result");
    EXPECT_EQ(m_released, 0) << "setting the area is not a person's release";

    drag(QPointF(18.0, 11.0), QPointF(28.0, 21.0), 5, [this]() { EXPECT_EQ(m_released, 0) << "told while it moved"; });
    EXPECT_EQ(m_released, 1);
    EXPECT_EQ(m_releasedArea, QRect(18, 16, 20, 10));
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(18, 16, 20, 10));
}

// A view whose area another process holds - a crop, a preview's part - refuses, and its area is left as it was; the
// session's end leaves it on, since the session did not turn it on.
TEST_F(ViewSelection, AViewWhoseAreaAnotherProcessHoldsRefuses)
{
    OtherProcess other;
    auto crop = std::make_unique<ImageCropController>(m_target->getScrollArea(), &other);
    crop->setListenerEnabled(true);
    crop->updateSelectionArea(QRect(2, 2, 10, 10));

    const ExtensionProtocolMessage answer = ask(
        "view.selection", QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(20, 20, 8, 8)}});
    EXPECT_EQ(answer.type, "view.selection.error");
    EXPECT_NE(refusalOf(answer).find("another process holds the view's selection area"), std::string::npos)
        << refusalOf(answer);
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(2, 2, 10, 10));
    EXPECT_FALSE(m_controller->view_requests.holdsSelectionArea());

    m_controller->detachUiHandles();
    EXPECT_TRUE(widget()->isSelectionAreaEnabled()) << "the session's end turned off an area it did not turn on";
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(2, 2, 10, 10));
    crop->destroy();
}

// The session's end turns off the area the session turned on, and lets go of the view's mouse.
TEST_F(ViewSelection, TheSessionsEndTurnsOffWhatItTurnedOn)
{
    ASSERT_EQ(ask("view.selection",
                  QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(8, 6, 20, 10)}})
                  .type,
              "view.selection.result");
    ASSERT_TRUE(widget()->isSelectionAreaEnabled());

    m_controller->detachUiHandles();
    EXPECT_FALSE(widget()->isSelectionAreaEnabled());
    EXPECT_FALSE(m_controller->view_requests.holdsSelectionArea());
    EXPECT_EQ(ImageCropController::holding(m_target->getScrollArea()), nullptr)
        << "the session's tool stayed on the view";

    // Another process may take the area now.
    OtherProcess other;
    auto crop = std::make_unique<ImageCropController>(m_target->getScrollArea(), &other);
    crop->setListenerEnabled(true);
    EXPECT_EQ(ImageCropController::holding(m_target->getScrollArea()), crop.get());
    crop->destroy();
}

// An extension whose preview runs over a part of the image holds the area already: asked for, it is the same rectangle,
// set where the host says, and the preview is told it moved.
TEST_F(ViewSelection, AnExtensionThatPreviewsInAPartGetsTheSameRectangle)
{
    int previewTold = 0;
    m_controller->managed_preview_sub_area_enabled = true;
    m_controller->managed_preview_sub_area_size = QSize(16, 12);
    m_controller->managed_preview_sub_area_changed = [&previewTold]() { ++previewTold; };
    m_controller->configureManagedPreviewSubArea();
    ASSERT_TRUE(widget()->isSelectionAreaEnabled());

    const ExtensionProtocolMessage answer = ask(
        "view.selection", QCborMap{{QStringLiteral("enabled"), true}, {QStringLiteral("rect"), rectOf(4, 4, 16, 12)}});
    ASSERT_EQ(answer.type, "view.selection.result") << refusalOf(answer);
    EXPECT_EQ(widget()->getCurrentSelectionArea(), QRect(4, 4, 16, 12));
    EXPECT_EQ(answeredRect(answer), QRect(4, 4, 16, 12));
    EXPECT_EQ(previewTold, 1) << "the preview's part moved and the preview was not told";
    EXPECT_FALSE(m_controller->view_requests.holdsSelectionArea()) << "the area is the preview's, not the request's";

    const ExtensionProtocolMessage off = ask("view.selection", QCborMap{{QStringLiteral("enabled"), false}});
    EXPECT_EQ(off.type, "view.selection.error");
    EXPECT_NE(refusalOf(off).find("the preview's part"), std::string::npos) << refusalOf(off);
    EXPECT_TRUE(widget()->isSelectionAreaEnabled());
    m_controller->clearManagedPreviewSubArea();
}

// ---- How the view is shown, and its history

namespace
{

/// How a session asks the view to be shown.
class ViewDisplay : public ViewRequestsSeam
{
protected:
    /// Where a point of the image is shown in the view's viewport now.
    [[nodiscard]] QPointF shownAt(const QPointF image) const
    {
        const double scale = widget()->getCurrentScaleFactor();
        return QPointF(widget()->mapTo(m_target->getScrollArea()->viewport(), image * scale));
    }
};

/// The Undo and the Redo a session asks of the view.
class ViewHistory : public ViewRequestsSeam
{
protected:
    /// Makes one step of the target's history, as an edit of the view does, and waits until it is shown.
    void makeAStep()
    {
        m_target->horizontalMirror();
        ASSERT_TRUE(settle(*m_target));
        ASSERT_EQ(m_target->viewHistory()->previousStepName(), "Image Horizontal Mirror");
    }

    [[nodiscard]] bool pumpUntilHistory(const std::string& previous, const std::string& next)
    {
        an::ui::IViewHistory* const history = m_target->viewHistory();
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 5000)
        {
            pump();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (history->previousStepName() == previous && history->nextStepName() == next)
            {
                return true;
            }
        }
        return false;
    }
};

} // namespace

// A zoom with a centre lands there: the view takes the zoom and shows the point at the middle of its viewport.
TEST_F(ViewDisplay, AZoomWithACentreLandsThere)
{
    // At 1600 % the image is wider and taller than the view, so the view scrolls to centre a point.
    const ExtensionProtocolMessage answer =
        ask("view.display", QCborMap{{QStringLiteral("zoom"), 1600}, {QStringLiteral("center"), QCborArray{40, 30}}});
    ASSERT_EQ(answer.type, "view.display.result") << refusalOf(answer);
    EXPECT_NEAR(widget()->getCurrentZoom(), 1600.0, 0.01);
    EXPECT_NEAR(answer.fields.value(QStringLiteral("zoom")).toDouble(), 1600.0, 0.01);
    const QPointF middle = QRectF(m_target->getScrollArea()->viewport()->rect()).center();
    const QPointF shown = shownAt(QPointF(40.0, 30.0));
    EXPECT_NEAR(shown.x(), middle.x(), 2.0);
    EXPECT_NEAR(shown.y(), middle.y(), 2.0);

    // A centre alone moves the view and keeps its zoom.
    ASSERT_EQ(ask("view.display", QCborMap{{QStringLiteral("center"), QCborArray{28, 24}}}).type,
              "view.display.result");
    EXPECT_NEAR(widget()->getCurrentZoom(), 1600.0, 0.01);
    EXPECT_NEAR(shownAt(QPointF(28.0, 24.0)).x(), middle.x(), 2.0);
}

// Fit to window shows the whole image, as the view's own Fit To Window does.
TEST_F(ViewDisplay, FitToWindowShowsTheWholeImage)
{
    ASSERT_EQ(ask("view.display", QCborMap{{QStringLiteral("zoom"), 1600}}).type, "view.display.result");
    ASSERT_GT(widget()->width(), m_target->getScrollArea()->viewport()->width());

    const ExtensionProtocolMessage answer = ask("view.display", QCborMap{{QStringLiteral("fit"), true}});
    ASSERT_EQ(answer.type, "view.display.result") << refusalOf(answer);
    ASSERT_TRUE(settle(*m_target));
    const QSize viewport = m_target->getScrollArea()->viewport()->size();
    EXPECT_LE(widget()->width(), viewport.width());
    EXPECT_LE(widget()->height(), viewport.height());
    EXPECT_LT(widget()->getCurrentZoom(), 1600.0);
}

// A display mode is asked for by the name the view's state reports it under.
TEST_F(ViewDisplay, ADisplayModeByName)
{
    ExtensionProtocolMessage answer =
        ask("view.display", QCborMap{{QStringLiteral("mode"), QStringLiteral("autostretch")}});
    ASSERT_EQ(answer.type, "view.display.result") << refusalOf(answer);
    EXPECT_EQ(widget()->getViewMode(), ImageViewMode::AUTOSTRETCH_MODE);
    EXPECT_EQ(answer.fields.value(QStringLiteral("mode")).toString(), QStringLiteral("autostretch"));

    answer = ask("view.display", QCborMap{{QStringLiteral("mode"), QStringLiteral("linear")}});
    ASSERT_EQ(answer.type, "view.display.result") << refusalOf(answer);
    EXPECT_EQ(widget()->getViewMode(), ImageViewMode::LINEAR_MODE);
    EXPECT_EQ(answer.fields.value(QStringLiteral("mode")).toString(), QStringLiteral("linear"));
}

// A mode the view has not is refused by name, and a zoom outside the view's range is refused; a refused request changes
// nothing of what it also asked.
TEST_F(ViewDisplay, AModeTheViewHasNotIsRefusedByName)
{
    const double zoom = widget()->getCurrentZoom();
    const ImageViewMode mode = widget()->getViewMode();
    ExtensionProtocolMessage answer =
        ask("view.display",
            QCborMap{{QStringLiteral("zoom"), 300}, {QStringLiteral("mode"), QStringLiteral("false_color")}});
    EXPECT_EQ(answer.type, "view.display.error");
    EXPECT_NE(refusalOf(answer).find("the view has no display mode 'false_color'"), std::string::npos)
        << refusalOf(answer);
    EXPECT_NEAR(widget()->getCurrentZoom(), zoom, 0.01) << "the zoom of a refused request was taken";
    EXPECT_EQ(widget()->getViewMode(), mode);

    answer = ask("view.display", QCborMap{{QStringLiteral("zoom"), 10000}});
    EXPECT_EQ(answer.type, "view.display.error");
    EXPECT_NE(refusalOf(answer).find("outside the view's range"), std::string::npos) << refusalOf(answer);
    EXPECT_NEAR(widget()->getCurrentZoom(), zoom, 0.01);
}

// An Undo the session asks for is the view's own, run once: the view shows the step before and its history moves by
// one; a Redo shows the step again. No request takes a step away.
TEST_F(ViewHistory, UndoRunsTheViewsUndoOnceAndRedoItsRedo)
{
    makeAStep();
    an::ui::IViewHistory* const history = m_target->viewHistory();
    const an::ui::ViewHistorySteps before = history->steps();
    ASSERT_GT(before.current, 0u);

    ExtensionProtocolMessage answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}});
    ASSERT_EQ(answer.type, "view.history.result") << refusalOf(answer);
    ASSERT_TRUE(pumpUntilHistory(std::string(), "Image Horizontal Mirror")) << "the view did not undo its step";
    ASSERT_TRUE(settle(*m_target));
    EXPECT_EQ(history->steps().current, before.current - 1u) << "the Undo ran other than once";
    EXPECT_EQ(history->steps().steps.size(), before.steps.size()) << "an Undo took a step away";

    answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("redo")}});
    ASSERT_EQ(answer.type, "view.history.result") << refusalOf(answer);
    ASSERT_TRUE(pumpUntilHistory("Image Horizontal Mirror", std::string())) << "the view did not redo its step";
    ASSERT_TRUE(settle(*m_target));
    EXPECT_EQ(history->steps(), before) << "the Redo did not show the step again, once";
}

// With nothing to undo, or to redo, the request is refused and the history left as it was.
TEST_F(ViewHistory, IsRefusedWithNothingToUndoOrToRedo)
{
    ExtensionProtocolMessage answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}});
    EXPECT_EQ(answer.type, "view.history.error");
    EXPECT_NE(refusalOf(answer).find("there is nothing to undo"), std::string::npos) << refusalOf(answer);

    makeAStep();
    answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("redo")}});
    EXPECT_EQ(answer.type, "view.history.error");
    EXPECT_NE(refusalOf(answer).find("there is nothing to redo"), std::string::npos) << refusalOf(answer);
    EXPECT_EQ(m_target->viewHistory()->previousStepName(), "Image Horizontal Mirror");
}

// While a run holds the view, and while the session's own commit is still to land, an Undo is refused: the history
// must not move under a result on its way.
TEST_F(ViewHistory, IsRefusedWhileARunHoldsTheViewAndWhileTheSessionsCommitLands)
{
    makeAStep();
    ASSERT_TRUE(m_target->tryRequestLockForProcessing());
    ExtensionProtocolMessage answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}});
    EXPECT_EQ(answer.type, "view.history.error");
    EXPECT_NE(refusalOf(answer).find("a run holds the view"), std::string::npos) << refusalOf(answer);
    m_target->releaseLockForProcessing();
    pumpQuietly();

    an::ui::extensions::ViewCommitMessage commit;
    commit.session_id = kSessionId;
    commit.request_id = "commit-1";
    commit.result_buffers.emplace_back();
    m_controller->running_session.last_view_commit = commit;
    ASSERT_TRUE(m_controller->owesExecuteCommit());
    answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}});
    EXPECT_EQ(answer.type, "view.history.error");
    EXPECT_NE(refusalOf(answer).find("the session's own commit has not landed"), std::string::npos)
        << refusalOf(answer);
    EXPECT_EQ(m_target->viewHistory()->previousStepName(), "Image Horizontal Mirror")
        << "a refused Undo moved the history";

    m_controller->running_session.last_view_commit.reset();
    answer = ask("view.history", QCborMap{{QStringLiteral("action"), QStringLiteral("undo")}});
    EXPECT_EQ(answer.type, "view.history.result") << refusalOf(answer);
    ASSERT_TRUE(pumpUntilHistory(std::string(), "Image Horizontal Mirror"));
}

// A host that dies ends its session, and the session's end takes away what it left on the view: its shapes, and the
// area it turned on.
TEST_F(ViewSelection, AHostThatDiesTakesItsShapesAndItsAreaWithIt)
{
    ASSERT_EQ(ask("view.selection", QCborMap{{QStringLiteral("enabled"), true}}).type, "view.selection.result");
    const QCborMap shape{{QStringLiteral("kind"), QStringLiteral("line")},
                         {QStringLiteral("points"), QCborArray{1, 1, 20, 20}},
                         {QStringLiteral("color"), QStringLiteral("#ff0000")}};
    ASSERT_EQ(ask("view.overlay", QCborMap{{QStringLiteral("add"), QCborArray{shape}}}).type, "view.overlay.result");
    ASSERT_NE(m_target->ownedShapes(kSessionId), nullptr);
    ASSERT_NE(ImageCropController::holding(m_target->getScrollArea()), nullptr);

    m_host->die(*m_controller);
    QElapsedTimer clock;
    clock.start();
    while (m_controller->isActive() && clock.elapsed() < 5000)
    {
        pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    ASSERT_FALSE(m_controller->isActive()) << "the host's death did not end its session";
    // What the coordinator does with a session that ended: it stops it.
    m_controller->detachUiHandles();
    EXPECT_EQ(m_target->ownedShapes(kSessionId), nullptr) << "the shapes outlived their host";
    EXPECT_EQ(ImageCropController::holding(m_target->getScrollArea()), nullptr) << "the area outlived its host";
    EXPECT_FALSE(widget()->isSelectionAreaEnabled());
}
