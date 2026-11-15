// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The tasks of the shell core, over process windows and views of a real session: every registered process window is a
/// task named by the descriptor it was launched as, with its glyph and its target; the same process on two images is
/// two tasks; the tasks on a view and the one whose run is on its way there; a run that ends leaves its result until
/// the task is shown; a window that goes takes its task with it; the observers are told in order. And the thumbnails of
/// the views a shell follows: a square of 60 pixels presented as the view presents its image, admitted as a preview
/// that never waits, made again when the image changes and dropped when the view goes; the GUI thread makes no call of
/// the engine.

#include "ShellCoreTestSupport.h"
#include "ShellCoreViewSupport.h"

#include "actions/ActionCatalogue.h"
#include "actions/ActionIds.h"
#include "core/ApplicationContext.h"
#include "core/ApplicationCore.h"
#include "core/ApplicationEvents.h"
#include "services/OpenProcessRegistry.h"
#include "tasks/TaskModel.h"
#include "views/ViewService.h"
#include "views/ViewThumbnailService.h"

#include "app/ProcessRegistry.h"
#include "controls/IAppContext.h"
#include "controls/ImageProcessControl.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ProcessLaunchScope.h"
#include "controls/ProcessPresentation.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "session/ImageDocument.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

using an::tests::shellcore::pumpUntil;
using an::tests::shellcore::RecordingShell;
using an::tests::shellcore::ScopedStorageRoot;
using an::tests::shellcore::SessionProvider;
using an::ui::ProcessRunState;
using an::ui::process_registry::ProcessDescriptor;
using an::ui::process_registry::ProcessRegistry;
using an::ui::shell::ApplicationCore;
using an::ui::shell::Task;
using an::ui::shell::TaskId;
using Phase = ProcessRunState::Phase;
using Model = an::ui::shell::TaskModel;
using Thumbnails = an::ui::shell::ViewThumbnailService;

/// A grey Float32 image of `width` x `height`, each sample `value(x, y)`.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
greyAsset(const an::consumer::EngineSession& session, const std::uint32_t width, const std::uint32_t height,
          const std::function<float(std::uint32_t, std::uint32_t)>& value)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, 1u, hold->engine().limits());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::ImageDescriptor::create(an::ImageDescriptorRequest{.extent = *extent,
                                                               .sampleType = an::SampleType::Float32,
                                                               .channelSchema = an::ChannelSchema::Mono,
                                                               .colorSpace = an::ColorSpace::Unspecified,
                                                               .numericDomain = an::NumericDomain::NormalizedLinear,
                                                               .cfa = an::CfaDescriptor{},
                                                               .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                               .pixelAspectRatio = 1.0});
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image =
        an::Image<float>::createFilled(hold->engine(), *descriptor, an::StorageArrangement::Planar, 0.0f);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto row = access->view().row(0u, y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = value(x, y);
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage(std::move(*image)));
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

/// The registry as it was, put back when the case ends: the cases register descriptors of their own.
class RegistryKept
{
public:
    RegistryKept() : m_kept(ProcessRegistry::instance().all()) {}
    ~RegistryKept()
    {
        ProcessRegistry::instance().clear();
        for (const ProcessDescriptor& descriptor : m_kept)
        {
            (void)ProcessRegistry::instance().registerProcess(descriptor);
        }
    }
    RegistryKept(const RegistryKept&) = delete;
    RegistryKept& operator=(const RegistryKept&) = delete;

private:
    std::vector<ProcessDescriptor> m_kept;
};

/// A descriptor of the cases' own.
[[nodiscard]] ProcessDescriptor descriptorOf(const std::string& id, const std::string& name,
                                             const std::string& category, const std::string& action,
                                             const std::string& icon = {})
{
    ProcessDescriptor descriptor;
    descriptor.id = id;
    descriptor.display_name = name;
    descriptor.category = category;
    descriptor.action_object_name = action;
    descriptor.icon_key = icon;
    descriptor.process_name = name;
    descriptor.launch_mode = an::ui::process_registry::ProcessLaunchMode::SingleImage;
    descriptor.launch_single_image = [](ImageViewControl*) {};
    return descriptor;
}

class TasksFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_widgetsBefore = an::tests::shellcore::widgetsNow();
        ASSERT_TRUE(m_root.isValid());
        m_session = an::tests::shellcore::openSession();
        ASSERT_NE(m_session, nullptr);
        m_core.openStores();
        m_core.setShell(&m_shell);
        m_core.setEngineSessionProvider(m_session.get());
        m_target = std::make_unique<an::ui::shell::ServiceEventTarget>(
            m_core, an::ui::shell::ServiceEventTarget::Dependencies{});
        m_core.setEventTarget(m_target.get());
        m_context = std::make_unique<an::ui::shell::ApplicationContext>(m_core, m_shell);
        an::ui::clearAppContext();
        an::ui::setAppContext(m_context.get());
    }

    void TearDown() override
    {
        m_thumbnails.reset();
        m_model.reset();
        for (const QPointer<TopLevelProcessBaseControl>& process : m_processes)
        {
            delete process.data();
        }
        m_core.viewService().manager().clear();
        for (const QPointer<QWidget>& dock : m_docks)
        {
            delete dock.data();
        }
        for (const QPointer<ImageViewControl>& view : m_views)
        {
            delete view.data();
        }
        an::tests::shellcore::settle();
        m_core.discardPendingEvents();
        m_core.setEventTarget(nullptr);
        an::ui::clearAppContext();
        m_core.setEngineSessionProvider(nullptr);
        m_core.setShell(nullptr);
        m_core.closeStores();
        const QStringList left = an::tests::shellcore::widgetsLeftSince(m_widgetsBefore);
        EXPECT_TRUE(left.isEmpty()) << "the case left widgets behind: "
                                    << left.join(QStringLiteral(", ")).toStdString();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_session->engineSession();
    }

    /// A view of an image named `name`, made by the views' factory; the case deletes it.
    ImageViewControl* viewOf(
        const QString& name, const std::uint32_t width = 8u, const std::uint32_t height = 4u,
        const std::function<float(std::uint32_t, std::uint32_t)>& value = [](std::uint32_t, std::uint32_t) {
            return 0.25f;
        })
    {
        std::optional<an::consumer::ImageAsset> asset = greyAsset(session(), width, height, value);
        EXPECT_TRUE(asset.has_value());
        if (!asset.has_value())
        {
            return nullptr;
        }
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = name, .sourcePath = {}});
        EXPECT_TRUE(document.hasValue());
        if (!document.hasValue())
        {
            return nullptr;
        }
        an::ui::IViewFactory& factory = m_core.viewService();
        ImageViewControl* view = factory.createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "made", ViewControlMode::standard, nullptr, false, false);
        EXPECT_NE(view, nullptr);
        if (view != nullptr)
        {
            m_views.emplace_back(view);
            m_docks.emplace_back(view->getDockWidget());
        }
        return view;
    }

    /// A process window over a view, made inside a launch of `descriptor` when one is given; the case deletes it.
    ImageProcessControl* processOn(ImageViewControl* view, const std::string& name,
                                   const ProcessDescriptor* descriptor = nullptr)
    {
        std::optional<an::ui::ProcessLaunchScope> scope;
        if (descriptor != nullptr)
        {
            scope.emplace(*descriptor);
        }
        auto* process = new ImageProcessControl(view, name);
        process->setAttribute(Qt::WA_DeleteOnClose, false);
        m_processes.emplace_back(process);
        return process;
    }

    Model& model()
    {
        if (!m_model)
        {
            m_model = std::make_unique<Model>(m_core.openProcesses());
        }
        return *m_model;
    }

    Thumbnails& thumbnails()
    {
        if (!m_thumbnails)
        {
            m_thumbnails = std::make_unique<Thumbnails>(m_core);
        }
        return *m_thumbnails;
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    std::unique_ptr<SessionProvider> m_session;
    ApplicationCore m_core;
    RecordingShell m_shell;
    std::unique_ptr<an::ui::shell::ServiceEventTarget> m_target;
    std::unique_ptr<an::ui::shell::ApplicationContext> m_context;
    std::unique_ptr<Model> m_model;
    std::unique_ptr<Thumbnails> m_thumbnails;
    std::vector<QPointer<TopLevelProcessBaseControl>> m_processes;
    std::vector<QPointer<ImageViewControl>> m_views;
    std::vector<QPointer<QWidget>> m_docks;
    QSet<QWidget*> m_widgetsBefore;
};

class TaskModel : public TasksFixture
{
};

class ViewThumbnailService : public TasksFixture
{
};

/// The mean of a picture's red, green and blue over every pixel.
[[nodiscard]] double meanLevel(const QImage& picture)
{
    double sum = 0.0;
    for (int y = 0; y < picture.height(); ++y)
    {
        for (int x = 0; x < picture.width(); ++x)
        {
            const QColor colour = picture.pixelColor(x, y);
            sum += (colour.red() + colour.green() + colour.blue()) / 3.0;
        }
    }
    return sum / (static_cast<double>(picture.width()) * picture.height());
}

} // namespace

// A process window registered is a task: the descriptor it was launched as names it, with its category and its glyph -
// the descriptor's icon, else its action's, else the generic one - and the image it targets; one made outside a launch
// is named by its own name. A model made after the windows are open takes them in, their targets with them.
TEST_F(TaskModel, ARegisteredProcessIsATaskWithItsDescriptorsNamesAndItsTarget)
{
    const RegistryKept kept;
    const ProcessDescriptor curves =
        descriptorOf("tests.curves", "Curves Transformation", "transforms", "actionCurves_Transformation", "curves");
    const ProcessDescriptor open =
        descriptorOf("tests.open", "Opener", "files", std::string(an::ui::shell::ids::kOpen));
    const ProcessDescriptor plain = descriptorOf("tests.plain", "Plain", "filters", "actionNoSuchAction");
    ASSERT_TRUE(ProcessRegistry::instance().registerProcess(curves));
    ASSERT_TRUE(ProcessRegistry::instance().registerProcess(open));
    ASSERT_TRUE(ProcessRegistry::instance().registerProcess(plain));
    ImageViewControl* view = viewOf(QStringLiteral("M31"));
    ASSERT_NE(view, nullptr);

    Model& tasks = model();
    ImageProcessControl* first = processOn(view, "CurvesTransformationProcess", &curves);
    // The shell's presenter says which image the process targets.
    tasks.setTarget(*first, view);
    ASSERT_EQ(tasks.tasks().size(), 1u);
    const Task& task = tasks.tasks().front();
    EXPECT_NE(task.id, 0u);
    EXPECT_EQ(task.descriptorId, "tests.curves");
    EXPECT_EQ(task.displayName, QStringLiteral("Curves Transformation"));
    EXPECT_EQ(task.processName, QStringLiteral("CurvesTransformationProcess"));
    EXPECT_EQ(task.category, QStringLiteral("transforms"));
    EXPECT_EQ(task.iconKey, QStringLiteral("curves"));
    EXPECT_EQ(task.process.data(), first);
    EXPECT_EQ(task.target.data(), view);
    EXPECT_EQ(task.targetName, QString::fromStdString(view->getUniqueName()));
    EXPECT_EQ(task.surface, an::ui::ProcessSurface::Window);
    EXPECT_EQ(task.presentation, an::ui::shell::TaskPresentation::Window);
    EXPECT_EQ(task.run.phase, Phase::Idle);
    EXPECT_EQ(tasks.taskFor(first), &task);

    (void)processOn(view, "OpenerProcess", &open);
    (void)processOn(view, "PlainProcess", &plain);
    auto* unlaunched = processOn(view, "Unlaunched Process");
    ASSERT_EQ(tasks.tasks().size(), 4u);
    const an::ui::shell::ActionSpec* opener = an::ui::shell::ActionCatalogue::specOf(an::ui::shell::ids::kOpen);
    ASSERT_NE(opener, nullptr);
    EXPECT_EQ(tasks.tasks()[1].iconKey, QString::fromLatin1(opener->iconKey)) << "the action's icon";
    EXPECT_EQ(tasks.tasks()[2].iconKey, QString::fromLatin1(Model::kGenericGlyph));
    EXPECT_EQ(tasks.taskFor(unlaunched)->displayName, QStringLiteral("Unlaunched Process"));
    EXPECT_TRUE(tasks.taskFor(unlaunched)->descriptorId.empty());
    EXPECT_TRUE(tasks.taskFor(unlaunched)->category.isEmpty());

    // A model made now takes the open windows in, each with the image it targets.
    const Model later(m_core.openProcesses());
    EXPECT_EQ(later.tasks().size(), 4u);
    ASSERT_NE(later.taskFor(unlaunched), nullptr);
    EXPECT_EQ(later.taskFor(unlaunched)->target.data(), view);
}

// The same process opened over two images is two tasks, each on its own view.
TEST_F(TaskModel, TheSameProcessOnTwoImagesIsTwoTasks)
{
    ImageViewControl* left = viewOf(QStringLiteral("left"));
    ImageViewControl* right = viewOf(QStringLiteral("right"));
    ASSERT_TRUE(left != nullptr && right != nullptr);
    Model& tasks = model();
    ImageProcessControl* onLeft = processOn(left, "Curves Transformation");
    ImageProcessControl* onRight = processOn(right, "Curves Transformation");
    tasks.setTarget(*onLeft, left);
    tasks.setTarget(*onRight, right);

    ASSERT_EQ(tasks.tasks().size(), 2u);
    const TaskId leftId = tasks.taskFor(onLeft)->id;
    const TaskId rightId = tasks.taskFor(onRight)->id;
    EXPECT_NE(leftId, rightId);
    EXPECT_EQ(tasks.tasksOn(left), std::vector<TaskId>{leftId});
    EXPECT_EQ(tasks.tasksOn(right), std::vector<TaskId>{rightId});
    EXPECT_TRUE(tasks.tasksOn(nullptr).empty());

    // A process pointed at the other image is a task on that one.
    tasks.setTarget(*onLeft, right);
    EXPECT_TRUE(tasks.tasksOn(left).empty());
    EXPECT_EQ(tasks.tasksOn(right), (std::vector<TaskId>{leftId, rightId}));
}

// The task whose run is on its way over a view is the one the view is blocked by; none once the run has ended.
TEST_F(TaskModel, TheRunningTaskOnAView)
{
    ImageViewControl* view = viewOf(QStringLiteral("target"));
    ImageViewControl* other = viewOf(QStringLiteral("other"));
    ASSERT_TRUE(view != nullptr && other != nullptr);
    Model& tasks = model();
    ImageProcessControl* idle = processOn(view, "Idle Process");
    ImageProcessControl* running = processOn(view, "Running Process");
    tasks.setTarget(*idle, view);
    tasks.setTarget(*running, view);
    EXPECT_EQ(tasks.runningTaskOn(view), nullptr);

    tasks.setRunState(*running, an::ui::runningState({}, 0.42));
    ASSERT_NE(tasks.runningTaskOn(view), nullptr);
    EXPECT_EQ(tasks.runningTaskOn(view)->process.data(), running);
    EXPECT_EQ(tasks.runningTaskOn(view)->run.progress, std::optional<double>(0.42));
    EXPECT_EQ(tasks.runningTaskOn(other), nullptr);

    tasks.setRunState(*running, ProcessRunState{.phase = Phase::Succeeded});
    EXPECT_EQ(tasks.runningTaskOn(view), nullptr);
}

// A run that ends while its task is not the current one leaves its result - succeeded or failed - until the task is
// shown, and then it is let go; one that ends while its task is current is seen; a stop leaves no result to see.
TEST_F(TaskModel, AnEndedRunLeavesItsResultUntilTheTaskIsShown)
{
    ImageViewControl* view = viewOf(QStringLiteral("target"));
    ASSERT_NE(view, nullptr);
    Model& tasks = model();
    ImageProcessControl* process = processOn(view, "Curves Transformation");
    const TaskId id = tasks.taskFor(process)->id;
    ASSERT_EQ(tasks.current(), 0u);

    tasks.setRunState(*process, an::ui::runningState());
    tasks.setRunState(*process, ProcessRunState{.phase = Phase::Succeeded});
    EXPECT_EQ(tasks.task(id)->run.phase, Phase::Succeeded);
    EXPECT_TRUE(tasks.task(id)->unseenResult);
    ASSERT_TRUE(tasks.setCurrent(id));
    EXPECT_EQ(tasks.current(), id);
    EXPECT_FALSE(tasks.task(id)->unseenResult);
    EXPECT_EQ(tasks.task(id)->run.phase, Phase::Idle);

    tasks.setRunState(*process, an::ui::runningState());
    tasks.setRunState(*process, ProcessRunState{.phase = Phase::Failed, .text = QStringLiteral("out of memory")});
    EXPECT_FALSE(tasks.task(id)->unseenResult) << "it ended while it was shown";
    EXPECT_EQ(tasks.task(id)->run.text, QStringLiteral("out of memory"));

    ASSERT_TRUE(tasks.setCurrent(0u));
    tasks.setRunState(*process, an::ui::runningState());
    EXPECT_FALSE(tasks.task(id)->unseenResult) << "a new run lets the last result go";
    tasks.setRunState(*process, ProcessRunState{.phase = Phase::Failed, .text = QStringLiteral("out of memory")});
    EXPECT_TRUE(tasks.task(id)->unseenResult);
    tasks.setRunState(*process, an::ui::runningState());
    tasks.setRunState(*process, ProcessRunState{.phase = Phase::Cancelled});
    EXPECT_FALSE(tasks.task(id)->unseenResult) << "a stop says nothing";
    EXPECT_FALSE(tasks.setCurrent(id + 1000u)) << "an id the model does not hold";
}

// A process window that goes takes its task with it, the current task among them.
TEST_F(TaskModel, AWindowThatGoesTakesItsTask)
{
    ImageViewControl* view = viewOf(QStringLiteral("target"));
    ASSERT_NE(view, nullptr);
    Model& tasks = model();
    ImageProcessControl* kept = processOn(view, "Kept");
    ImageProcessControl* gone = processOn(view, "Gone");
    const TaskId goneId = tasks.taskFor(gone)->id;
    ASSERT_TRUE(tasks.setCurrent(goneId));

    delete gone;
    ASSERT_EQ(tasks.tasks().size(), 1u);
    EXPECT_EQ(tasks.tasks().front().process.data(), kept);
    EXPECT_EQ(tasks.task(goneId), nullptr);
    EXPECT_EQ(tasks.current(), 0u);
}

// Every change is told to the observers, in the order it happened, and the observers in the order they came; a change
// that changes nothing is told to none, and an observer that left is told nothing.
TEST_F(TaskModel, ObserversAreToldInOrder)
{
    ImageViewControl* view = viewOf(QStringLiteral("target"));
    ASSERT_NE(view, nullptr);
    Model& tasks = model();
    std::vector<std::string> told;
    const auto record = [&told](const char* who) {
        return [&told, who](const Model::Change change, const TaskId id) {
            const char* names[] = {"added", "removed", "changed", "current"};
            told.push_back(std::string(who) + " " + names[static_cast<int>(change)] + " " + std::to_string(id));
        };
    };
    const std::size_t first = tasks.subscribe(record("a"));
    const std::size_t second = tasks.subscribe(record("b"));

    ImageProcessControl* process = processOn(view, "Curves Transformation");
    const TaskId id = tasks.taskFor(process)->id;
    tasks.setTarget(*process, view);
    tasks.setTarget(*process, view);
    tasks.setRunState(*process, an::ui::runningState());
    tasks.setCurrent(id);
    tasks.unsubscribe(second);
    delete process;
    const std::string n = std::to_string(id);
    EXPECT_EQ(told, (std::vector<std::string>{"a added " + n, "b added " + n, "a changed " + n, "b changed " + n,
                                              "a changed " + n, "b changed " + n, "a current " + n, "b current " + n,
                                              "a current 0", "a removed " + n}));
    tasks.unsubscribe(first);
}

// A thumbnail is a square of 60 pixels: the whole image, presented as the view presents it - linear, its samples read
// as they are; stretched, brighter - with no call of the engine on the GUI thread.
TEST_F(ViewThumbnailService, ASixtyPixelSquareThroughTheViewsDisplay)
{
    const auto dark = [](std::uint32_t x, std::uint32_t y) { return 0.01f + 0.0004f * static_cast<float>(x + y); };
    ImageViewControl* linear = viewOf(QStringLiteral("linear"), 160u, 96u, dark);
    ImageViewControl* stretched = viewOf(QStringLiteral("stretched"), 160u, 96u, dark);
    ASSERT_TRUE(linear != nullptr && stretched != nullptr);
    linear->setViewMode(ImageViewMode::LINEAR_MODE);
    stretched->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    an::tests::shellcore::settle();

    Thumbnails& service = thumbnails();
    std::vector<ImageViewControl*> told;
    const std::size_t token =
        service.subscribe([&told](ImageViewControl* view, const QImage&) { told.push_back(view); });
    service.follow(linear);
    service.follow(stretched);
    ASSERT_TRUE(pumpUntil([&service]() { return service.isIdle() && service.telemetry().published >= 2u; }));

    const QImage plain = service.thumbnail(linear);
    const QImage bright = service.thumbnail(stretched);
    ASSERT_FALSE(plain.isNull());
    EXPECT_EQ(plain.size(), QSize(Thumbnails::kSide, Thumbnails::kSide));
    EXPECT_EQ(bright.size(), QSize(Thumbnails::kSide, Thumbnails::kSide));
    // The linear picture holds the samples as they are, a few hundredths of full scale.
    EXPECT_LT(meanLevel(plain), 0.25 * 255.0);
    EXPECT_GT(meanLevel(bright), meanLevel(plain) + 20.0) << "the stretched view's picture is stretched";
    EXPECT_EQ(told.size(), 2u);
    EXPECT_EQ(service.telemetry().engineCallsOnGuiThread, 0u);
    EXPECT_GT(service.telemetry().engineCalls, 0u);
    service.unsubscribe(token);
}

// A rendering is admitted as a preview that never waits: while every thread of the session is held it is refused - no
// request of the session waits - and it is asked for again a little later, once the threads are free.
TEST_F(ViewThumbnailService, AdmittedWithTryAdmitAsAPreview)
{
    ImageViewControl* view = viewOf(QStringLiteral("refused"));
    ASSERT_NE(view, nullptr);
    an::tests::shellcore::settle();
    std::optional<an::consumer::OperationScope> held;
    {
        an::Result<an::consumer::OperationScope> admitted = session().tryAdmit(
            an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Apply, .maxConcurrency = 0u});
        ASSERT_TRUE(admitted.hasValue()) << admitted.status().message();
        held.emplace(std::move(*admitted));
    }
    const an::consumer::AdmissionSnapshot before = session().admission();

    Thumbnails& service = thumbnails();
    service.follow(view);
    ASSERT_TRUE(pumpUntil([&service]() { return service.telemetry().refused >= 1u; }));
    EXPECT_TRUE(service.thumbnail(view).isNull());
    EXPECT_GT(session().admission().refusedBusy, before.refusedBusy);
    EXPECT_EQ(session().admission().queued, 0u) << "a rendering never waits for a thread";

    held.reset();
    ASSERT_TRUE(pumpUntil([&service]() { return service.isIdle() && service.telemetry().published >= 1u; }));
    EXPECT_FALSE(service.thumbnail(view).isNull());
}

// The image updated, the thumbnail is made again; the view gone, its thumbnail is dropped and its listeners told.
TEST_F(ViewThumbnailService, RedoneOnAnImageUpdateAndDroppedWhenTheViewGoes)
{
    ImageViewControl* view = viewOf(QStringLiteral("updated"));
    ASSERT_NE(view, nullptr);
    view->setViewMode(ImageViewMode::LINEAR_MODE);
    Thumbnails& service = thumbnails();
    QImage last;
    int dropped = 0;
    const std::size_t token = service.subscribe([&last, &dropped](ImageViewControl*, const QImage& thumbnail) {
        last = thumbnail;
        dropped += thumbnail.isNull() ? 1 : 0;
    });
    service.follow(view);
    ASSERT_TRUE(pumpUntil([&service]() { return service.isIdle() && service.telemetry().published >= 1u; }));
    const double before = meanLevel(service.thumbnail(view));

    std::optional<an::consumer::ImageAsset> brighter =
        greyAsset(session(), 8u, 4u, [](std::uint32_t, std::uint32_t) { return 0.75f; });
    ASSERT_TRUE(brighter.has_value());
    view->replaceImage(std::move(*brighter));
    ASSERT_TRUE(pumpUntil([&service]() { return service.isIdle() && service.telemetry().published >= 2u; }));
    EXPECT_GT(meanLevel(service.thumbnail(view)), before + 100.0);
    EXPECT_EQ(last.cacheKey(), service.thumbnail(view).cacheKey());

    // The view goes as the application lets it go: its dock, which holds it, deleted.
    const QPointer<ImageViewControl> going(view);
    m_core.viewService().manager().clear();
    for (QPointer<QWidget>& dock : m_docks)
    {
        delete dock.data();
    }
    delete going.data();
    an::tests::shellcore::settle();
    EXPECT_FALSE(service.follows(view));
    EXPECT_TRUE(service.thumbnail(view).isNull());
    EXPECT_EQ(dropped, 1);
    EXPECT_TRUE(last.isNull());
    service.unsubscribe(token);
}
