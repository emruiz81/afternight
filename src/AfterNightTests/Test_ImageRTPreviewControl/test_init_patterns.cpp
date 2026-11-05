// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The real-time preview window around its preview, run against the Controls library: the main layout and the window's
/// size as the preview goes off and comes back on; the preview view - the window's own, fitted as it opens and again
/// for a first result of another size, comparing with and stretched through the images the process names, opened
/// without waiting for the target - and the preview area chosen on it; what a run on its way holds back; what a process
/// that states no options and no sizes gets; the plot resolutions its histogram offers; and Fit to Window in the views
/// the preview and the view-creation coordinator make. The window's type contracts are held at compile time.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "app/ViewCreationCoordinator.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/ImageViewControlManager.h"
#include "controls/ImageViewScrollArea.h"
#include "controls/ProcessPreviewLayout.h"
#include "widgets/HistogramDisplayData.h"
#include "widgets/HistogramWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/PresentationBridge.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::processes;

/// An image a process names through one of the framework's hooks.
using NamedImage = std::function<std::shared_ptr<const an::consumer::ImageAsset>()>;

/// What a hook process's builder states and which of the framework's hooks the process answers itself; whatever is
/// left unset is the framework's.
struct HookOptions
{
    /// Whether the window opens with its preview on.
    bool preview = false;

    /// The size constraint the builder gives the main layout, if it gives one.
    std::optional<QLayout::SizeConstraint> builderConstraint;

    /// Whether the preview needs the target view to itself among previews.
    bool exclusivePreview = true;

    /// Whether the process stretches its preview itself.
    bool supportStretch = false;

    /// Whether the window sets the histogram up beside its controls.
    bool histogram = false;

    /// The original the preview compares with.
    NamedImage alternate;

    /// The image whose statistics stretch the preview.
    NamedImage stretchSource;
};

/// A real-time preview process over the typed framework that states none of the framework's initialization options
/// and answers the framework's hooks as a case asks. Its body records every run, waits at a gate and scales the channel
/// it is handed.
class HookProcess final : public an::ui::TypedProcessControl<TestParams>
{
public:
    HookProcess(ImageViewControl* target, HookOptions options)
        : TypedProcessControl<TestParams>(target, "Hook Scale"), m_options(std::move(options))
    {
        auto* mainLayout = new QHBoxLayout(this);
        if (m_options.builderConstraint.has_value())
        {
            mainLayout->setSizeConstraint(*m_options.builderConstraint);
        }
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        auto* controlsPanel = new QWidget(splitter);
        auto* controlsLayout = new QVBoxLayout(controlsPanel);
        auto* previewPanel = new QWidget(splitter);
        auto* previewLayout = new QVBoxLayout(previewPanel);
        mainLayout->addWidget(splitter);

        previewCheckbox = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
        previewCheckbox->setChecked(m_options.preview);
        controlsLayout->addWidget(previewCheckbox);
        channelSelector = new QComboBox(controlsPanel);
        controlsLayout->addWidget(channelSelector);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, controlsPanel);
        controlsLayout->addWidget(buttons);

        initializeStandardProcess(mainLayout, previewLayout, previewCheckbox, buttons, nullptr, channelSelector,
                                  ProcessInitOptions{});
        if (m_options.histogram)
        {
            histogramArea = new QScrollArea(controlsPanel);
            controlsLayout->addWidget(histogramArea);
            plotResolution = new QComboBox(controlsPanel);
            controlsLayout->addWidget(plotResolution);
            std::array<QAbstractButton*, 8u> histogramButtons{};
            for (QAbstractButton*& button : histogramButtons)
            {
                button = new QToolButton(controlsPanel);
                controlsLayout->addWidget(button);
            }
            setupHistogramPreview(histogramArea, plotResolution, histogramButtons[0], histogramButtons[1],
                                  histogramButtons[2], histogramButtons[3], histogramButtons[4], histogramButtons[5],
                                  histogramButtons[6], histogramButtons[7]);
        }
        setExecutionAutostretchSupported(m_options.supportStretch);
        setupPreviewControl(false);
    }

    /// Changes the parameters the window holds, as a widget's change does, and asks for a preview.
    void setParams(const TestParams& params)
    {
        m_params = params;
        settingsUpdated();
    }

    /// The gate every body waits at.
    Gate gate;

    /// The runs every body saw, in the order they started.
    [[nodiscard]] std::vector<TestRun> runs() const
    {
        const std::lock_guard lock(m_runsLock);
        return m_runs;
    }

    /// The size the framework gives a window with its preview, which a process may change.
    [[nodiscard]] QSize sizeWithPreview() const
    {
        return m_DefaultSizePreview;
    }

    /// The size the framework gives a window without its preview, which a process may change.
    [[nodiscard]] QSize sizeWithoutPreview() const
    {
        return m_DefaultSizeNoPreview;
    }

    /// Whether the window shows a spinner over its preview while a preview runs.
    [[nodiscard]] bool showsWaitingSpinner() const
    {
        return m_ShowWaitingSpinner;
    }

    /// The initialization options of a process that states none.
    [[nodiscard]] static ProcessInitOptions optionsNobodyStates()
    {
        return ProcessInitOptions{};
    }

    [[nodiscard]] bool requiresExclusivePreviewAccess() const override
    {
        return m_options.exclusivePreview;
    }

    QCheckBox* previewCheckbox = nullptr;
    QComboBox* channelSelector = nullptr;
    QScrollArea* histogramArea = nullptr;
    QComboBox* plotResolution = nullptr;

protected:
    [[nodiscard]] bool isPreviewEnabled() const override
    {
        return previewCheckbox != nullptr && previewCheckbox->isChecked();
    }

    void enablePreview(bool enabled) override
    {
        if (previewCheckbox != nullptr)
        {
            const QSignalBlocker blocker(previewCheckbox);
            previewCheckbox->setChecked(enabled);
        }
    }

    void settingsUpdatedImpl() override {}

    [[nodiscard]] TestParams getParams() const override
    {
        return m_params;
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> getAlternateAsset() const override
    {
        return m_options.alternate ? m_options.alternate() : ImageRTPreviewControl::getAlternateAsset();
    }

    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> getExecutionAutostretchSource() const override
    {
        return m_options.stretchSource ? m_options.stretchSource()
                                       : ImageRTPreviewControl::getExecutionAutostretchSource();
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const TestParams& params,
                                       an::ui::ExecutionScope& scope) override
    {
        const std::optional<an::ImageDescriptor> described = source.descriptor();
        {
            const std::lock_guard lock(m_runsLock);
            m_runs.push_back(TestRun{.kind = run.kind,
                                     .channel = run.channel,
                                     .params = params,
                                     .frame = run.frame,
                                     .sourceWidth = described.has_value() ? described->extent().width() : 0u,
                                     .sourceHeight = described.has_value() ? described->extent().height() : 0u});
        }
        if (!gate.pass([&scope]() { return scope.stopRequested(); }))
        {
            return an::Status::failure(an::ErrorCode::Cancelled, "the hook body was stopped at its gate");
        }
        return executeTypedProcessUsingChannels(source, destination, run, params, scope);
    }

    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun&, const TestParams& params,
                                            an::ui::ExecutionScope&) override
    {
        return scalePlane(input, output, params.factor);
    }

private:
    HookOptions m_options;
    TestParams m_params;
    mutable std::mutex m_runsLock;
    std::vector<TestRun> m_runs;
};

/// A hook process window, deleted with the case once its runs have landed.
struct HookProcessDeleter
{
    void operator()(HookProcess* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        process->gate.open();
        (void)pumpUntil([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};
using HookHandle = std::unique_ptr<HookProcess, HookProcessDeleter>;

/// A hook process window over a target, without the delete-on-close the framework sets so the case decides when it
/// goes, at the size it opens at.
[[nodiscard]] HookHandle openHook(ImageViewControl& target, HookOptions options)
{
    HookHandle process(new HookProcess(&target, std::move(options)));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    return process;
}

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] bool settleWindow(const ImageRTPreviewControl& window)
{
    if (!pumpUntil([&window]() { return window.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return window.isProcessIdle();
}

/// A grey Float32 document of a size, every sample a quarter.
[[nodiscard]] std::optional<an::ui::ImageDocument> flatDocument(const an::consumer::EngineSession& session,
                                                                std::uint32_t width, std::uint32_t height)
{
    return greyDocument(session, width, height, [](std::uint32_t, std::uint32_t) { return 0.25f; });
}

/// A grey 8 by 6 Float32 target whose samples are `value`; null when it cannot be made.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session, float value)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [value](std::uint32_t, std::uint32_t) { return value; });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("target", std::move(*document));
}

/// A grey 8 by 6 Float32 target whose sample at (x, y) is (x + 10 y) / 100; null when it cannot be made.
[[nodiscard]] ViewHandle rampTarget(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [](std::uint32_t x, std::uint32_t y) { return 0.01f * (x + 10.0f * y); });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("target", std::move(*document));
}

/// The main splitter of a process window: its controls pane, then its preview pane.
[[nodiscard]] QSplitter* mainSplitterOf(const QWidget& window)
{
    return window.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
}

/// The zoom Fit to Window gives an image of a size in a view as the view is laid out now: the viewport, any scroll bar
/// it shows given back, over the image, on whichever edge fits first.
[[nodiscard]] double fitFactor(const ImageViewControl& view, const QSize& image)
{
    const ImageViewScrollArea* area = view.getScrollArea();
    QRect viewport = area->viewport()->rect();
    if (area->horizontalScrollBar()->isVisible())
    {
        viewport.setHeight(viewport.height() + area->horizontalScrollBar()->rect().height());
    }
    if (area->verticalScrollBar()->isVisible())
    {
        viewport.setWidth(viewport.width() + area->verticalScrollBar()->rect().width());
    }
    return (std::min)(static_cast<double>(viewport.width()) / static_cast<double>(image.width()),
                      static_cast<double>(viewport.height()) / static_cast<double>(image.height()));
}

/// The bytes a view paints now, pixel by pixel without row padding; empty before anything is painted.
[[nodiscard]] std::vector<std::uint8_t> paintedBytes(const ImageViewControl& view)
{
    std::vector<std::uint8_t> bytes;
    const std::shared_ptr<const an::ui::PresentedImage> painted = view.getViewWidget()->presentedImage();
    if (!painted)
    {
        return bytes;
    }
    for (std::uint32_t y = 0u; y < painted->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < painted->width(); ++x)
        {
            const std::uint8_t* pixel = painted->pixel(x, y);
            if (pixel != nullptr)
            {
                bytes.insert(bytes.end(), pixel, pixel + painted->bytesPerPixel());
            }
        }
    }
    return bytes;
}

/// How many warnings the application was asked to show.
[[nodiscard]] qsizetype warningCount(const ProcessTestContext& context)
{
    return context.messages.filter(QStringLiteral("warning:")).size();
}

/// Holds a document's writer on a thread of its own, as a worker writing the image does, until it is let go or the
/// patience runs out.
class WriterElsewhere
{
public:
    explicit WriterElsewhere(an::ui::ImageDocument& document)
        : m_thread([this, &document]() {
              an::Result<an::ui::ImageDocumentWriter> writer = document.tryWrite();
              std::unique_lock lock(m_mutex);
              m_taken = writer.hasValue();
              m_answered = true;
              m_changed.notify_all();
              (void)m_changed.wait_for(lock, kPatience, [this]() { return m_letGo; });
          })
    {
        std::unique_lock lock(m_mutex);
        (void)m_changed.wait_for(lock, kPatience, [this]() { return m_answered; });
    }

    ~WriterElsewhere()
    {
        letGo();
        m_thread.join();
    }

    WriterElsewhere(const WriterElsewhere&) = delete;
    WriterElsewhere& operator=(const WriterElsewhere&) = delete;

    /// Whether the thread holds the writer.
    [[nodiscard]] bool taken()
    {
        const std::lock_guard lock(m_mutex);
        return m_taken;
    }

    /// Lets the writer go.
    void letGo()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_letGo = true;
        }
        m_changed.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_answered = false;
    bool m_taken = false;
    bool m_letGo = false;
    std::thread m_thread;
};

/// Grows a view the first time its image is scaled, as a dock's own layout pass does right after a new view is first
/// fitted, and counts every scaling. The listener subscribes to the view as it is made and unsubscribes as it goes.
class GrowOnFirstScale final : public ImageViewControlListener
{
public:
    explicit GrowOnFirstScale(ImageViewControl* view) : ImageViewControlListener(view) {}

    GrowOnFirstScale(const GrowOnFirstScale&) = delete;
    GrowOnFirstScale& operator=(const GrowOnFirstScale&) = delete;

    void onImageViewScaled(ImageViewControl* view) override
    {
        ++scalings;
        if (scalings == 1)
        {
            firstScale = view->getViewWidget()->getCurrentScaleFactor();
            view->resize(view->size() + QSize(200, 160));
        }
    }

    /// How many times the image was scaled.
    int scalings = 0;

    /// The zoom the first scaling gave the image.
    double firstScale = 0.0;
};

/// Whether a toolbar offers a checkbox enabled: on the toolbar while it has room for it, or as the entry of its
/// overflow menu once it has none - a window never shown has no room, so its toolbar offers everything there.
[[nodiscard]] bool offersEnabled(const QCheckBox& checkBox)
{
    const auto* toolbar = qobject_cast<const QToolBar*>(checkBox.parentWidget());
    if (toolbar == nullptr)
    {
        return false;
    }
    for (QAction* entry : toolbar->actions())
    {
        if (toolbar->widgetForAction(entry) == &checkBox && entry->isVisible())
        {
            return checkBox.isEnabled();
        }
    }
    const auto* button = toolbar->findChild<QToolButton*>(QStringLiteral("imageViewToolbarOverflowButton"));
    if (button == nullptr || button->menu() == nullptr)
    {
        return false;
    }
    for (QAction* entry : button->menu()->actions())
    {
        if (!entry->isSeparator() && entry->text() == checkBox.text())
        {
            return entry->isEnabled();
        }
    }
    return false;
}

} // namespace

// =======================================================================
// The layout and the window's size
// =======================================================================

// The panels are 24 pixels apart; the main layout enforces its minimum unless its builder stated a constraint.
TEST(RTPreviewLayoutTest, TheMainLayoutSpacesThePanelsAndKeepsTheConstraintItsBuilderStates)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);

    ProcessHandle unstated = openProcess(*target, TestProcessOptions{});
    ASSERT_NE(unstated->layout(), nullptr);
    EXPECT_EQ(unstated->layout()->spacing(), an::ui::kProcessMainPanelsSpacing);
    EXPECT_EQ(unstated->layout()->spacing(), 24);
    EXPECT_EQ(unstated->layout()->sizeConstraint(), QLayout::SetMinimumSize);

    HookHandle stated = openHook(*target, HookOptions{.builderConstraint = QLayout::SetFixedSize});
    ASSERT_NE(stated->layout(), nullptr);
    EXPECT_EQ(stated->layout()->spacing(), 24);
    EXPECT_EQ(stated->layout()->sizeConstraint(), QLayout::SetFixedSize);
}

// Turning the preview off, from its checkbox or by the framework, shrinks the window at once to its controls' width.
TEST(RTPreviewLayoutTest, TurningThePreviewOffShrinksTheWindowToItsControlsAndLeavesItResizable)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
    process->show();
    ASSERT_TRUE(settleProcess(*process));
    QSplitter* splitter = mainSplitterOf(*process);
    ASSERT_NE(splitter, nullptr);
    ASSERT_EQ(splitter->count(), 2);
    QWidget* previewPane = splitter->widget(1);

    for (const bool fromTheCheckbox : {true, false})
    {
        SCOPED_TRACE(fromTheCheckbox ? "turned off from its checkbox" : "turned off by the framework");
        ASSERT_NE(process->previewView(), nullptr);
        const QList<int> sizes = splitter->sizes();
        ASSERT_GT(sizes.value(0), 0);
        const int withPreview = process->width();
        const int controlsOnly = sizes.value(0) + (process->width() - splitter->width());

        if (fromTheCheckbox)
        {
            process->previewCheckbox->setChecked(false);
        }
        else
        {
            process->disableRTPreview();
        }
        EXPECT_EQ(process->previewView(), nullptr);
        EXPECT_FALSE(process->previewCheckbox->isChecked());
        EXPECT_EQ(process->width(), controlsOnly) << "the window is as wide as its controls pane was";
        EXPECT_FALSE(previewPane->isVisible());
        EXPECT_EQ(previewPane->maximumWidth(), 0);

        ASSERT_TRUE(settleProcess(*process));
        EXPECT_EQ(process->maximumSize(), QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)) << "the window can be resized again";
        EXPECT_LT(process->width(), withPreview);

        process->previewCheckbox->setChecked(true);
        ASSERT_TRUE(settleProcess(*process));
        EXPECT_TRUE(previewPane->isVisible());
    }
}

// Turning the preview back on gives the window the limits it had, and shows the preview pane at least 200 pixels wide.
TEST(RTPreviewLayoutTest, TurningThePreviewBackOnRestoresTheWindowsLimitsAndShowsThePaneAtItsMinimumWidth)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    {
        ProcessHandle process = openProcess(*target, TestProcessOptions{.preview = true});
        process->show();
        ASSERT_TRUE(settleProcess(*process));
        QSplitter* splitter = mainSplitterOf(*process);
        ASSERT_NE(splitter, nullptr);
        QWidget* previewPane = splitter->widget(1);
        ASSERT_NE(previewPane, nullptr);
        process->setMaximumSize(1500, 1100);
        const QLayout::SizeConstraint constraint = process->layout()->sizeConstraint();

        process->disableRTPreview();
        ASSERT_TRUE(settleProcess(*process));
        EXPECT_EQ(process->maximumSize(), QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        EXPECT_EQ(process->layout()->sizeConstraint(), QLayout::SetDefaultConstraint);

        process->previewCheckbox->setChecked(true);
        ASSERT_TRUE(settleProcess(*process));
        ASSERT_NE(process->previewView(), nullptr);
        EXPECT_EQ(process->maximumSize(), QSize(1500, 1100));
        EXPECT_EQ(process->layout()->sizeConstraint(), constraint);
        EXPECT_TRUE(previewPane->isVisible());
        EXPECT_EQ(previewPane->minimumWidth(), an::ui::kProcessMinPreviewPaneWidth);
        EXPECT_EQ(previewPane->minimumWidth(), 200);
        EXPECT_GE(previewPane->width(), 200);
    }
    // A window opened with its preview off saved no layout constraint: its preview turned on enforces the minimum.
    {
        ProcessHandle process = openProcess(*target, TestProcessOptions{});
        process->layout()->setSizeConstraint(QLayout::SetNoConstraint);
        process->previewCheckbox->setChecked(true);
        ASSERT_TRUE(settleProcess(*process));
        ASSERT_NE(process->previewView(), nullptr);
        EXPECT_EQ(process->layout()->sizeConstraint(), QLayout::SetMinimumSize);
    }
}

// =======================================================================
// What a process that states nothing gets
// =======================================================================

// A window opened with its preview on starts 1000 by 700, and the framework sizes a window without one 400 by 200.
TEST(RTPreviewDefaultsTest, AWindowOpenedWithItsPreviewStartsAtThePreviewSize)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);

    HookHandle process = openHook(*target, HookOptions{.preview = true});
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_EQ(process->size(), QSize(1000, 700));
    EXPECT_EQ(process->sizeWithPreview(), QSize(1000, 700));
    EXPECT_EQ(process->sizeWithoutPreview(), QSize(400, 200));
    ASSERT_TRUE(settleWindow(*process));
}

// A process that states no options previews the whole image with no channel choice or spinner and offers an area.
TEST(RTPreviewDefaultsTest, AProcessThatStatesNoOptionsOffersThePreviewAreaAndNoChannelChoice)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = makeDocument<float>(
        context->engineSession(), 8u, 6u, Channels::Rgb,
        [](std::uint32_t, std::uint32_t, std::uint32_t c) { return 0.1f * static_cast<float>(c + 1u); });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("colour", std::move(*document));

    HookHandle process = openHook(*target, HookOptions{.preview = true});
    ASSERT_TRUE(settleWindow(*process));
    const auto stated = HookProcess::optionsNobodyStates();
    EXPECT_FALSE(stated.supportPerChannel);
    EXPECT_FALSE(stated.showWaitingSpinner);
    EXPECT_TRUE(stated.previewAreaSelectionEnabled);

    EXPECT_EQ(process->channelSelector->count(), 0) << "the selector the window was handed is left unfilled";
    EXPECT_EQ(process->selectedChannelScope(), an::ChannelScope::Whole);
    const std::vector<TestRun> runs = process->runs();
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs.front().channel, an::ChannelScope::Whole);
    EXPECT_FALSE(process->showsWaitingSpinner());

    ASSERT_NE(process->previewView(), nullptr);
    QCheckBox* area = process->previewView()->getRTPreviewAreaCheckBox();
    ASSERT_NE(area, nullptr);
    EXPECT_TRUE(offersEnabled(*area)) << "the preview area is offered";
    EXPECT_FALSE(area->isChecked());
    EXPECT_TRUE(process->previewArea().isEmpty());
}

// =======================================================================
// The preview view
// =======================================================================

// The preview is the window's own view in its preview pane, never the factory's, and starts showing the target's own
// image, shared rather than copied.
TEST(RTPreviewViewTest, ThePreviewIsAViewOfTheWindowsOwnInItsPaneStartingWithTheTargetsOwnImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    ASSERT_TRUE(settleProcess(*process));
    process->gate.close();

    process->previewCheckbox->setChecked(true);
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->getViewControlMode(), ViewControlMode::rt_preview);
    QSplitter* splitter = mainSplitterOf(*process);
    ASSERT_NE(splitter, nullptr);
    EXPECT_EQ(preview->parentWidget(), splitter->widget(1)) << "laid out in the preview pane";
    EXPECT_FALSE(preview->isWindow());
    EXPECT_TRUE(preview->isVisible());
    EXPECT_TRUE(context->madeViews.empty()) << "the application's view factory made no view";
    // Before the first preview lands the view shows the target's own image, in a document of its own that shares it.
    EXPECT_NE(preview->document().identity(), target->document().identity());
    EXPECT_TRUE(preview->document().isShared());
    EXPECT_EQ(preview->document().asset(), target->document().asset()) << "the preview copied the target";
    EXPECT_NEAR(documentSample(preview->document(), 3u, 2u), 0.25, 1e-6);

    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->previewView(), preview);
    EXPECT_FALSE(preview->document().isShared()) << "the result is shown in a document of the preview's own";
    EXPECT_NEAR(documentSample(preview->document(), 3u, 2u), 0.5, 1e-6);
    EXPECT_NEAR(documentSample(target->document(), 3u, 2u), 0.25, 1e-6);
    EXPECT_TRUE(context->madeViews.empty());
}

// Opening the preview while another thread writes the target waits for nothing: the preview shares the target, shows
// nothing until the writer lets go, and a preview fills it.
TEST(RTPreviewViewTest, OpeningThePreviewWhileTheTargetIsWrittenWaitsForNothingAndShowsNothingMeanwhile)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    ASSERT_TRUE(settleView(*target));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    WriterElsewhere writer(target->document());
    ASSERT_TRUE(writer.taken());

    const auto opening = std::chrono::steady_clock::now();
    process->previewCheckbox->setChecked(true);
    EXPECT_LT(std::chrono::steady_clock::now() - opening, std::chrono::seconds(2)) << "the GUI thread did not wait";
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    EXPECT_TRUE(preview->document().isShared()) << "the preview takes no copy of a target being written";
    EXPECT_EQ(preview->document().asset(), target->document().asset());
    pumpQuietly();
    EXPECT_EQ(preview->getViewWidget()->presentedImage(), nullptr) << "nothing is shown while the target is written";

    writer.letGo();
    ASSERT_TRUE(settleProcess(*process));
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_TRUE(preview->hasImage());
    EXPECT_NEAR(documentSample(preview->document(), 0u, 0u), 0.75, 1e-6);
    EXPECT_EQ(context->errorCount(), 0);
}

// The first preview to land is fitted to the preview pane as laid out; a later one keeps the zoom the person chose.
TEST(RTPreviewViewTest, TheFirstPreviewIsFittedToItsPaneAndALaterOneKeepsTheZoomChosen)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = flatDocument(context->engineSession(), 64u, 48u);
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    ASSERT_TRUE(settleProcess(*process));

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    const double fitted = preview->getViewWidget()->getCurrentScaleFactor();
    EXPECT_NEAR(fitted, fitFactor(*preview, QSize(64, 48)), 1e-9);
    ASSERT_GT(std::abs(fitted - 1.0), 0.01) << "the fit is a zoom of its own";

    preview->zoom100();
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 2);
    EXPECT_DOUBLE_EQ(preview->getViewWidget()->getCurrentScaleFactor(), 1.0);
}

// The preview is fitted to its pane as it opens, showing the target's image while its first result is on its way, and
// a first result of the same size keeps the zoom the person chose meanwhile rather than jumping to the fit.
TEST(RTPreviewViewTest, ThePreviewIsFittedAsItOpensAndItsFirstResultOfTheSameSizeKeepsTheZoomChosenMeanwhile)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = flatDocument(context->engineSession(), 64u, 48u);
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    ASSERT_TRUE(settleProcess(*process));
    process->gate.close();

    process->previewCheckbox->setChecked(true);
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    pumpQuietly();
    ASSERT_TRUE(preview->document().isShared()) << "no result has landed yet";
    const double fitted = preview->getViewWidget()->getCurrentScaleFactor();
    EXPECT_NEAR(fitted, fitFactor(*preview, QSize(64, 48)), 1e-9) << "fitted before the first result lands";
    ASSERT_GT(std::abs(fitted - 1.0), 0.01) << "the fit is a zoom of its own";

    preview->zoom100();
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    ASSERT_FALSE(preview->document().isShared()) << "the first result has landed";
    EXPECT_DOUBLE_EQ(preview->getViewWidget()->getCurrentScaleFactor(), 1.0);
}

// A preview that shares the view leaves another window's preview on, and is left on by one that needs the view alone.
TEST(RTPreviewViewTest, APreviewThatSharesTheViewNeitherTurnsOffNorIsTurnedOffByAnother)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    ProcessHandle exclusive = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*exclusive));
    ASSERT_NE(exclusive->previewView(), nullptr);

    HookHandle sharing = openHook(*target, HookOptions{.preview = true, .exclusivePreview = false});
    ASSERT_TRUE(settleWindow(*sharing));
    ASSERT_TRUE(settleProcess(*exclusive));
    EXPECT_NE(sharing->previewView(), nullptr);
    EXPECT_NE(exclusive->previewView(), nullptr) << "a preview that shares the view turns no other off";

    ProcessHandle another(new TestProcess(target.get(), TestProcessOptions{.preview = true}, "Another Scale"));
    another->setAttribute(Qt::WA_DeleteOnClose, false);
    ASSERT_TRUE(settleProcess(*another));
    ASSERT_TRUE(settleProcess(*exclusive));
    ASSERT_TRUE(settleWindow(*sharing));
    EXPECT_NE(another->previewView(), nullptr);
    EXPECT_EQ(exclusive->previewView(), nullptr) << "a preview that needs the view alone turns the other such one off";
    EXPECT_NE(sharing->previewView(), nullptr) << "and leaves the one that shares the view on";
}

// The original the preview compares with is the image the process names, asked for again whenever the target changes.
TEST(RTPreviewViewTest, TheOriginalThePreviewComparesWithIsTheImageTheProcessNames)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    ViewHandle target = flatTarget(session, 0.25f);
    ASSERT_NE(target, nullptr);
    const auto flatAsset = [&session](float value) -> std::shared_ptr<const an::consumer::ImageAsset> {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session, 8u, 6u, Channels::Grey, [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
        return asset.has_value() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
    };
    const std::shared_ptr<const an::consumer::ImageAsset> first = flatAsset(0.75f);
    const std::shared_ptr<const an::consumer::ImageAsset> second = flatAsset(0.9f);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    std::shared_ptr<const an::consumer::ImageAsset> named = first;

    HookHandle process = openHook(*target, HookOptions{.preview = true, .alternate = [&named]() { return named; }});
    ASSERT_TRUE(settleWindow(*process));
    ASSERT_NE(process->previewView(), nullptr);
    EXPECT_EQ(process->previewView()->alternateAsset(), first);
    EXPECT_NE(process->previewView()->alternateAsset(), target->document().asset());

    named = second;
    target->notifyImageUpdated(true);
    ASSERT_TRUE(settleWindow(*process));
    EXPECT_EQ(process->previewView()->alternateAsset(), second);
}

// A process with a stretch of its own stretches its preview through the image it names, the target when it names none.
TEST(RTPreviewViewTest, AProcessThatStretchesItsPreviewStretchesItThroughTheImageItNames)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    const auto ramp = [](std::uint32_t x, std::uint32_t y) { return 0.01f * static_cast<float>(x + 8u * y); };
    std::optional<an::ui::ImageDocument> document = greyDocument(session, 8u, 6u, ramp);
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    target->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(target->isAutoStretchEnabled());
    ASSERT_TRUE(settleView(*target));

    const auto rampTimes = [&session, &ramp](float scale) -> std::shared_ptr<const an::consumer::ImageAsset> {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session, 8u, 6u, Channels::Grey,
            [&ramp, scale](std::uint32_t x, std::uint32_t y, std::uint32_t) { return ramp(x, y) * scale; });
        return asset.has_value() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
    };
    const std::shared_ptr<const an::consumer::ImageAsset> sameSamples = rampTimes(1.0f);
    const std::shared_ptr<const an::consumer::ImageAsset> dimmer = rampTimes(0.25f);
    ASSERT_NE(sameSamples, nullptr);
    ASSERT_NE(dimmer, nullptr);

    // What the preview paints once a second result has replaced the first while nothing read the preview's image, so
    // its writer was taken at once.
    const auto paintedThrough = [&target](const NamedImage& named) -> std::vector<std::uint8_t> {
        HookHandle process = openHook(*target, HookOptions{.supportStretch = true, .stretchSource = named});
        process->previewCheckbox->setChecked(true);
        ImageViewControl* preview = process->previewView();
        if (preview == nullptr)
        {
            ADD_FAILURE() << "the preview did not open";
            return {};
        }
        if (!settleWindow(*process))
        {
            ADD_FAILURE() << "the first preview did not land";
            return {};
        }
        const auto published = process->runTelemetry().previewsCommitted;
        process->setParams(TestParams{.factor = 3.0});
        if (!settleWindow(*process))
        {
            ADD_FAILURE() << "the second preview did not land";
            return {};
        }
        EXPECT_GT(process->runTelemetry().previewsCommitted, published) << "the second result was published";
        return paintedBytes(*preview);
    };
    const std::vector<std::uint8_t> namedNone = paintedThrough(NamedImage());
    const std::vector<std::uint8_t> namedSameSamples = paintedThrough([&sameSamples]() { return sameSamples; });
    const std::vector<std::uint8_t> namedDimmer = paintedThrough([&dimmer]() { return dimmer; });
    ASSERT_FALSE(namedNone.empty());
    EXPECT_EQ(namedSameSamples, namedNone) << "the target's statistics stretch the preview of a process naming none";
    EXPECT_NE(namedDimmer, namedNone) << "the statistics of the image named stretch the preview";
}

// A result that has to wait for the preview's readers before it replaces the image they read is stretched through the
// image the process names all the same, not by statistics the preview measures of the result itself.
TEST(RTPreviewViewTest, APreviewThatWaitsForItsReadersIsStillStretchedThroughTheImageItNames)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    const auto ramp = [](std::uint32_t x, std::uint32_t y) { return 0.01f * static_cast<float>(x + 8u * y); };
    std::optional<an::ui::ImageDocument> document = greyDocument(session, 8u, 6u, ramp);
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    target->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(target->isAutoStretchEnabled());
    ASSERT_TRUE(settleView(*target));

    const auto rampTimes = [&session, &ramp](float scale) -> std::shared_ptr<const an::consumer::ImageAsset> {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session, 8u, 6u, Channels::Grey,
            [&ramp, scale](std::uint32_t x, std::uint32_t y, std::uint32_t) { return ramp(x, y) * scale; });
        return asset.has_value() ? std::make_shared<const an::consumer::ImageAsset>(std::move(*asset)) : nullptr;
    };
    const std::shared_ptr<const an::consumer::ImageAsset> sameSamples = rampTimes(1.0f);
    const std::shared_ptr<const an::consumer::ImageAsset> dimmer = rampTimes(0.25f);
    ASSERT_NE(sameSamples, nullptr);
    ASSERT_NE(dimmer, nullptr);

    // What the preview paints once a result published while something read the preview's image has landed: the window
    // settles first, so that result is the last one, and its run waits at the gate until the image is read.
    const auto paintedThrough = [&target](const NamedImage& named) -> std::vector<std::uint8_t> {
        HookHandle process = openHook(*target, HookOptions{.supportStretch = true, .stretchSource = named});
        process->previewCheckbox->setChecked(true);
        ImageViewControl* preview = process->previewView();
        if (preview == nullptr)
        {
            ADD_FAILURE() << "the preview did not open";
            return {};
        }
        if (!settleWindow(*process))
        {
            ADD_FAILURE() << "the first preview did not land";
            return {};
        }
        const auto published = process->runTelemetry().previewsCommitted;
        const int started = process->gate.arrived();
        process->gate.close();
        process->setParams(TestParams{.factor = 3.0});
        if (!pumpUntil([&process, started]() { return process->gate.arrived() == started + 1; }))
        {
            ADD_FAILURE() << "the preview did not start";
            return {};
        }
        std::optional<an::consumer::AssetReadLease> reading;
        if (an::Result<an::consumer::AssetReadLease> lease = preview->document().tryRead(); lease.hasValue())
        {
            reading.emplace(std::move(*lease));
        }
        if (!reading.has_value())
        {
            ADD_FAILURE() << "the preview's image could not be read";
            return {};
        }
        process->gate.open();
        if (!pumpUntil([&process, published]() { return process->runTelemetry().previewsCommitted > published; }))
        {
            ADD_FAILURE() << "the result was not published";
            return {};
        }
        reading.reset();
        if (!settleWindow(*process))
        {
            ADD_FAILURE() << "the result did not land";
            return {};
        }
        EXPECT_EQ(process->runTelemetry().previewsCommitted, published + 1u) << "no later result replaced it";
        return paintedBytes(*preview);
    };
    const std::vector<std::uint8_t> namedNone = paintedThrough(NamedImage());
    const std::vector<std::uint8_t> namedSameSamples = paintedThrough([&sameSamples]() { return sameSamples; });
    const std::vector<std::uint8_t> namedDimmer = paintedThrough([&dimmer]() { return dimmer; });
    ASSERT_FALSE(namedNone.empty());
    EXPECT_EQ(namedSameSamples, namedNone) << "the target's statistics stretch the preview of a process naming none";
    EXPECT_NE(namedDimmer, namedNone) << "the statistics of the image named stretch the preview";
}

// =======================================================================
// The preview area
// =======================================================================

// The preview's "Preview area" checkbox runs the next preview over the whole image when off, over the area when on.
TEST(RTPreviewAreaTest, ThePreviewToolbarsCheckboxRunsThePreviewOverTheAreaOrTheWholeImage)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = rampTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    {
        ProcessHandle process = openProcess(
            *target, TestProcessOptions{.preview = true, .previewArea = true, .previewAreaSize = QSize(4, 2)});
        ASSERT_TRUE(settleProcess(*process));
        ImageViewControl* preview = process->previewView();
        ASSERT_NE(preview, nullptr);
        QCheckBox* areaBox = preview->getRTPreviewAreaCheckBox();
        ASSERT_NE(areaBox, nullptr);
        EXPECT_EQ(areaBox->text(), QStringLiteral("Preview area"));
        EXPECT_NE(qobject_cast<QToolBar*>(areaBox->parentWidget()), nullptr) << "it sits on the preview's toolbar";
        EXPECT_TRUE(offersEnabled(*areaBox));
        EXPECT_TRUE(areaBox->isChecked());
        EXPECT_EQ(process->previewArea(), QRect(2, 2, 4, 2));

        areaBox->setChecked(false);
        ASSERT_TRUE(settleProcess(*process));
        EXPECT_TRUE(process->previewArea().isEmpty());
        std::vector<TestRun> runs = process->runs();
        ASSERT_FALSE(runs.empty());
        EXPECT_EQ(runs.back().sourceWidth, 8u);
        EXPECT_EQ(runs.back().sourceHeight, 6u);
        EXPECT_NEAR(documentSample(preview->document(), 1u, 2u), 2.0 * 0.21, 1e-6);

        areaBox->setChecked(true);
        ASSERT_TRUE(settleProcess(*process));
        EXPECT_EQ(process->previewArea(), QRect(2, 2, 4, 2));
        runs = process->runs();
        ASSERT_FALSE(runs.empty());
        EXPECT_EQ(runs.back().sourceWidth, 4u);
        EXPECT_EQ(runs.back().sourceHeight, 2u);
        EXPECT_NEAR(documentSample(preview->document(), 1u, 2u), 0.21, 1e-6);
        EXPECT_NEAR(documentSample(preview->document(), 2u, 2u), 2.0 * 0.22, 1e-6);
    }
    // A window that offers no preview area keeps the checkbox off its preview's toolbar.
    ProcessHandle plain = openProcess(*target, TestProcessOptions{.preview = true});
    ASSERT_TRUE(settleProcess(*plain));
    ASSERT_NE(plain->previewView(), nullptr);
    QCheckBox* withheld = plain->previewView()->getRTPreviewAreaCheckBox();
    ASSERT_NE(withheld, nullptr);
    EXPECT_FALSE(withheld->isEnabled());
    EXPECT_TRUE(withheld->isHidden());
}

// The preview area is drawn and chosen on the preview view, never on the target; the next preview runs where it moves.
TEST(RTPreviewAreaTest, TheAreaIsChosenOnThePreviewViewAndTheNextPreviewRunsOverIt)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = rampTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.preview = true, .previewArea = true, .previewAreaSize = QSize(4, 2)});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    ImageViewWidget* shown = preview->getViewWidget();
    EXPECT_TRUE(shown->isSelectionAreaEnabled());
    EXPECT_EQ(shown->getCurrentSelectionArea(), QRect(2, 2, 4, 2));
    EXPECT_EQ(shown->getSelectionAreaText(), QStringLiteral("Preview area"));
    EXPECT_EQ(shown->getImageViewSelectionStyle(), ImageViewSelectionStyle::PREVIEW_STYLE);
    EXPECT_FALSE(target->getViewWidget()->isSelectionAreaEnabled()) << "the target shows no area";

    shown->updateSelectionArea(QRect(0, 0, 4, 2));
    process->selectionAreaChanged();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_EQ(process->previewArea(), QRect(0, 0, 4, 2));
    EXPECT_EQ(shown->getCurrentSelectionArea(), QRect(0, 0, 4, 2));
    const std::vector<TestRun> runs = process->runs();
    ASSERT_FALSE(runs.empty());
    EXPECT_EQ(runs.back().sourceWidth, 4u);
    EXPECT_EQ(runs.back().sourceHeight, 2u);
    EXPECT_NEAR(documentSample(preview->document(), 3u, 1u), 2.0 * 0.13, 1e-6);
    EXPECT_NEAR(documentSample(preview->document(), 2u, 2u), 0.22, 1e-6) << "where the area was is the image again";
}

// A view loading an image with no plate solution turns its hover tracking off, and the preview view loads one at its
// first frame: the area's resize cursors still follow the mouse after it.
TEST(RTPreviewAreaTest, TheAreasResizeCursorsFollowTheMouseAfterThePreviewsFirstFrame)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = rampTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    ProcessHandle process =
        openProcess(*target, TestProcessOptions{.preview = true, .previewArea = true, .previewAreaSize = QSize(4, 2)});
    ASSERT_TRUE(settleProcess(*process));
    ImageViewControl* preview = process->previewView();
    ASSERT_NE(preview, nullptr);
    ASSERT_FALSE(process->runs().empty()) << "the first frame landed";
    ASSERT_FALSE(preview->document().isShared()) << "the preview shows a document of its own";
    EXPECT_TRUE(preview->getViewWidget()->hasMouseTracking());
}

// =======================================================================
// What a run on its way holds back
// =======================================================================

// While a run is on its way no preview starts and a second Apply is ignored; once it lands the preview runs again.
TEST(RTPreviewRunTest, WhileARunIsOnItsWayNoPreviewStartsAndASecondApplyIsIgnored)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const bool toANewImage : {false, true})
    {
        SCOPED_TRACE(toANewImage ? "a run to a new image" : "an Apply in place");
        ViewHandle target = flatTarget(context->engineSession(), 0.25f);
        ASSERT_NE(target, nullptr);
        ProcessHandle process =
            openProcess(*target, TestProcessOptions{.preview = true, .createNewImage = toANewImage});
        ASSERT_TRUE(settleProcess(*process));
        ASSERT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1);
        const an::ui::ProcessRunKind kind =
            toANewImage ? an::ui::ProcessRunKind::NewImage : an::ui::ProcessRunKind::Apply;
        const qsizetype warnings = warningCount(*context);

        process->gate.close();
        process->applyProcess();
        ASSERT_TRUE(pumpUntil([&process, kind]() { return process->runsOf(kind) == 1; }));
        process->setParams(TestParams{.factor = 3.0});
        process->applyProcess();
        pumpQuietly();
        EXPECT_FALSE(process->isProcessIdle());
        EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1) << "no preview starts beside the run";
        EXPECT_EQ(process->runsOf(kind), 1) << "the second Apply is ignored";
        EXPECT_EQ(warningCount(*context), warnings) << "without telling anyone the view is busy";

        process->gate.open();
        ASSERT_TRUE(settleProcess(*process));
        ASSERT_TRUE(settleView(*target));
        EXPECT_EQ(process->runsOf(kind), 1);
        const std::vector<TestRun> runs = process->runs();
        ASSERT_FALSE(runs.empty());
        EXPECT_EQ(runs.back().kind, an::ui::ProcessRunKind::Preview);
        EXPECT_DOUBLE_EQ(runs.back().params.factor, 3.0);
        ASSERT_NE(process->previewView(), nullptr);
        const double source = toANewImage ? 0.25 : 0.5;
        EXPECT_NEAR(documentSample(process->previewView()->document(), 0u, 0u), source * 3.0, 1e-6);
        EXPECT_TRUE(process->isProcessIdle());
    }
}

// A change made after the window was closed, while its last preview still runs, asks for no preview.
TEST(RTPreviewRunTest, AChangeAfterTheWindowIsClosedAsksForNoPreview)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    ProcessHandle process = openProcess(*target, TestProcessOptions{});
    process->show();
    process->gate.close();
    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->gate.arrived() == 1; }));
    const std::uint64_t asked = process->runTelemetry().previewsRequested;

    EXPECT_FALSE(process->close());
    process->setParams(TestParams{.factor = 7.0});
    EXPECT_EQ(process->runTelemetry().previewsRequested, asked);
    process->gate.open();
    ASSERT_TRUE(settleProcess(*process));
    EXPECT_TRUE(process->isHidden());
    EXPECT_EQ(process->runsOf(an::ui::ProcessRunKind::Preview), 1);
    EXPECT_EQ(process->runTelemetry().previewsRequested, asked);
}

// =======================================================================
// The histogram's plot resolutions
// =======================================================================

// The plot resolutions run from six bits to ten thousand levels, eight bits chosen; the histogram counts at the choice.
TEST(RTPreviewHistogramTest, ThePlotResolutionsOfferedAreTheHistogramsAndTheOneChosenIsCounted)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession(), 0.25f);
    ASSERT_NE(target, nullptr);
    HookHandle process = openHook(*target, HookOptions{.histogram = true});
    ASSERT_NE(process->plotResolution, nullptr);
    ASSERT_NE(process->histogramArea, nullptr);
    auto* histogram = dynamic_cast<HistogramWidget*>(process->histogramArea->widget());
    ASSERT_NE(histogram, nullptr);

    using Resolution = an::ui::HistogramResolution;
    const std::vector<std::pair<QString, Resolution>> offered{
        {QStringLiteral("6-bit (64)"), Resolution::Bits6},
        {QStringLiteral("7-bit (128)"), Resolution::Bits7},
        {QStringLiteral("8-bit (256)"), Resolution::Bits8},
        {QStringLiteral("9-bit (512)"), Resolution::Bits9},
        {QStringLiteral("10-bit (1024)"), Resolution::Bits10},
        {QStringLiteral("12-bit (4096)"), Resolution::Bits12},
        {QStringLiteral("14-bit (16K)"), Resolution::Bits14},
        {QStringLiteral("16-bit (64K)"), Resolution::Bits16},
        {QStringLiteral("100 levels"), Resolution::Levels100},
        {QStringLiteral("1000 levels"), Resolution::Levels1000},
        {QStringLiteral("10000 levels"), Resolution::Levels10000}};
    QComboBox& plot = *process->plotResolution;
    ASSERT_EQ(plot.count(), static_cast<int>(offered.size()));
    for (int index = 0; index < plot.count(); ++index)
    {
        EXPECT_EQ(plot.itemText(index), offered[static_cast<std::size_t>(index)].first) << index;
    }
    EXPECT_EQ(plot.currentText(), QStringLiteral("8-bit (256)"));
    EXPECT_EQ(histogram->getHistogramResolution(), Resolution::Bits8);

    for (const auto& [label, resolution] : offered)
    {
        plot.setCurrentText(label);
        EXPECT_EQ(histogram->getHistogramResolution(), resolution) << label.toStdString();
    }
}

// =======================================================================
// Fit to Window
// =======================================================================

// Fit to Window measures the viewport as laid out at once, a layout change still waiting for the event loop included.
TEST(ViewFitTest, FitToWindowFitsTheViewportAsLaidOutAtOnceAPendingChangeIncluded)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = flatDocument(context->engineSession(), 48u, 64u);
    ASSERT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("fitted", std::move(*document));
    view->show();
    ASSERT_TRUE(settleView(*view));
    auto* toolbar = view->findChild<QToolBar*>(QStringLiteral("anImageViewToolbar"));
    ASSERT_NE(toolbar, nullptr);
    ASSERT_TRUE(toolbar->isVisible());
    const QSize image(48, 64);
    const double underTheToolbar = fitFactor(*view, image);

    // Hiding the toolbar leaves the view's layout for the event loop to redo; the fit is asked for before it runs.
    toolbar->hide();
    view->fitToWindow();
    const double fitted = view->getViewWidget()->getCurrentScaleFactor();
    pumpQuietly();
    const double laidOut = fitFactor(*view, image);
    ASSERT_GT(laidOut, underTheToolbar) << "the image is fitted by its height, which grows by the toolbar's";
    EXPECT_NEAR(fitted, laidOut, 1e-9);
}

// Fit to Window puts the image back at its fitted size when the zoom already fits, repairing geometry left stale.
TEST(ViewFitTest, FitToWindowRepairsTheImagesGeometryWhenTheZoomAlreadyFits)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document = flatDocument(context->engineSession(), 48u, 64u);
    ASSERT_TRUE(document.has_value());
    ViewHandle view = makeTargetView("fitted", std::move(*document));
    view->show();
    ASSERT_TRUE(settleView(*view));
    view->fitToWindow();
    pumpQuietly();
    ImageViewWidget* image = view->getViewWidget();
    const double fitted = image->getCurrentScaleFactor();
    const QSize fittedSize = image->size();
    ASSERT_FALSE(fittedSize.isEmpty());

    image->setFixedSize(fittedSize / 2);
    pumpQuietly();
    ASSERT_EQ(image->size(), fittedSize / 2);
    view->fitToWindow();
    pumpQuietly();
    EXPECT_DOUBLE_EQ(image->getCurrentScaleFactor(), fitted);
    EXPECT_EQ(image->size(), fittedSize);
}

// The coordinator shows a view it makes and fits its image, but leaves a real-time preview view hidden and unfitted.
TEST(ViewCreationFitTest, TheCoordinatorShowsAndFitsAViewItMakesButLeavesAPreviewViewToItsWindow)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ImageViewControlManager manager;
    an::ui::ViewCreationCoordinator::Dependencies dependencies;
    dependencies.imageViewControlManager = &manager;
    const an::ui::ViewCreationCoordinator coordinator(dependencies);
    std::optional<an::ui::ImageDocument> shownDocument = flatDocument(context->engineSession(), 48u, 64u);
    std::optional<an::ui::ImageDocument> previewDocument = flatDocument(context->engineSession(), 48u, 64u);
    ASSERT_TRUE(shownDocument.has_value() && previewDocument.has_value());

    ViewHandle shown(coordinator.createNewImageViewControlFromImage(
        nullptr, std::move(*shownDocument), nullptr, "made here", ViewControlMode::st_preview, nullptr, false, false));
    ViewHandle preview(coordinator.createNewImageViewControlFromImage(nullptr, std::move(*previewDocument), nullptr,
                                                                      "made here", ViewControlMode::rt_preview, nullptr,
                                                                      false, false));
    ASSERT_NE(shown, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(settleView(*shown));
    ASSERT_TRUE(settleView(*preview));
    // Past the fit the coordinator asks for once a dock's layout has settled.
    pumpQuietly();
    pumpQuietly();

    EXPECT_TRUE(shown->isVisible());
    const double fitted = shown->getViewWidget()->getCurrentScaleFactor();
    EXPECT_NEAR(fitted, fitFactor(*shown, QSize(48, 64)), 1e-9);
    EXPECT_GT(std::abs(fitted - 1.0), 0.01) << "the view was fitted";
    EXPECT_FALSE(preview->isVisible());
    EXPECT_DOUBLE_EQ(preview->getViewWidget()->getCurrentScaleFactor(), 1.0);
    EXPECT_TRUE(preview->windowTitle().startsWith(QStringLiteral("** RT PREVIEW **")));
}

// A view the coordinator shows is fitted again once its layout settles, taking up a resize right after its first fit.
TEST(ViewCreationFitTest, AViewTheCoordinatorShowsIsFittedAgainOnceItsLayoutHasSettled)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ImageViewControlManager manager;
    an::ui::ViewCreationCoordinator::Dependencies dependencies;
    dependencies.imageViewControlManager = &manager;
    const an::ui::ViewCreationCoordinator coordinator(dependencies);
    std::optional<an::ui::ImageDocument> document = flatDocument(context->engineSession(), 48u, 64u);
    ASSERT_TRUE(document.has_value());

    ViewHandle view(coordinator.createNewImageViewControlFromImage(nullptr, std::move(*document), nullptr, "made here",
                                                                   ViewControlMode::st_preview, nullptr, false, false));
    ASSERT_NE(view, nullptr);
    // The view is laid out again right after its first fit, as a dock's own pass does.
    GrowOnFirstScale dock(view.get());
    ASSERT_TRUE(pumpUntil([&dock]() { return dock.scalings >= 2; }));
    ASSERT_TRUE(settleView(*view));
    const double refitted = view->getViewWidget()->getCurrentScaleFactor();
    EXPECT_NE(refitted, dock.firstScale);
    EXPECT_NEAR(refitted, fitFactor(*view, QSize(48, 64)), 1e-9);
}

// =======================================================================
// Type contracts
// =======================================================================

TEST(InitPatterns, RTPreviewModeEnumValues)
{
    // Verify the enum values at compile time.
    EXPECT_EQ(static_cast<int>(RTPreviewMode::NoPreview), 0);
    EXPECT_EQ(static_cast<int>(RTPreviewMode::SyncPreview), 1);
    EXPECT_EQ(static_cast<int>(RTPreviewMode::AsyncPreview), 2);
    EXPECT_EQ(static_cast<int>(RTPreviewMode::TakeSnapshot), 3);
}

TEST(InitPatterns, RTPreviewModeIsEnumClass)
{
    static_assert(std::is_enum_v<RTPreviewMode>, "RTPreviewMode should be an enum");
    static_assert(!std::is_convertible_v<RTPreviewMode, int>,
                  "RTPreviewMode should be an enum class (not implicitly convertible)");
    SUCCEED();
}

TEST(InitPatterns, ControlInheritsFromImageProcessControl)
{
    static_assert(std::is_base_of_v<ImageProcessControl, ImageRTPreviewControl>,
                  "ImageRTPreviewControl should inherit ImageProcessControl");
    SUCCEED();
}

TEST(InitPatterns, ControlIsAbstract)
{
    static_assert(std::is_abstract_v<ImageRTPreviewControl>, "ImageRTPreviewControl should remain abstract");
    SUCCEED();
}

TEST(InitPatterns, ControlIsNotCopyable)
{
    static_assert(!std::is_copy_constructible_v<ImageRTPreviewControl>, "ImageRTPreviewControl should not be copyable");
    static_assert(!std::is_copy_assignable_v<ImageRTPreviewControl>,
                  "ImageRTPreviewControl should not be copy-assignable");
    SUCCEED();
}
