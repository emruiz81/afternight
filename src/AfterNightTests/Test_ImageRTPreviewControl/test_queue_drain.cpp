// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What a real-time preview window refuses when it goes, run against the Controls library: a window deleted while its
/// preview's body runs is gone at once, waiting for nothing; the body sees its stop, nothing it would have shown is
/// published, and the window's claim on the session's preview share is let go.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSplitter>
#include <QThreadPool>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

namespace
{

using namespace an::tests::processes;

/// How long a detached body holds its run when nothing lets it go.
constexpr std::chrono::seconds kBodyHoldLimit{5};

/// What a detached body tells the case, shared so that it outlives the window that runs it.
struct BodyProbe
{
    /// The body has started.
    std::atomic<bool> arrived{false};

    /// The body saw its run's stop.
    std::atomic<bool> sawStop{false};

    /// The case lets the body go.
    std::atomic<bool> release{false};

    /// The body has returned.
    std::atomic<bool> left{false};
};

/// A real-time preview process whose body touches nothing of the window once it has started: it holds its run until the
/// case lets it go or a time limit passes, notes whether the run was stopped meanwhile, and always ends stopped, so the
/// framework never hands the window anything back.
class DetachedBodyProcess final : public an::ui::TypedProcessControl<TestParams>
{
public:
    DetachedBodyProcess(ImageViewControl* target, std::shared_ptr<BodyProbe> probe)
        : TypedProcessControl<TestParams>(target, "Detached Scale"), m_probe(std::move(probe))
    {
        auto* mainLayout = new QHBoxLayout(this);
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setObjectName(QStringLiteral("mainSplitter"));
        auto* controlsPanel = new QWidget(splitter);
        auto* controlsLayout = new QVBoxLayout(controlsPanel);
        auto* previewPanel = new QWidget(splitter);
        auto* previewLayout = new QVBoxLayout(previewPanel);
        mainLayout->addWidget(splitter);
        previewCheckbox = new QCheckBox(QStringLiteral("Preview"), controlsPanel);
        controlsLayout->addWidget(previewCheckbox);
        initializeStandardProcess(mainLayout, previewLayout, previewCheckbox, nullptr, nullptr, nullptr,
                                  ProcessInitOptions{});
        setupPreviewControl(false);
    }

    QCheckBox* previewCheckbox = nullptr;

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
        return TestParams{};
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource&, an::ui::ProcessDestination&,
                                       const an::ui::ProcessRun&, const TestParams&,
                                       an::ui::ExecutionScope& scope) override
    {
        // Copied before the case hears the body started: from then on the window may be gone.
        const std::shared_ptr<BodyProbe> probe = m_probe;
        probe->arrived = true;
        const auto until = std::chrono::steady_clock::now() + kBodyHoldLimit;
        while (!probe->release.load() && std::chrono::steady_clock::now() < until)
        {
            if (scope.stopRequested())
            {
                probe->sawStop = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (scope.stopRequested())
        {
            probe->sawStop = true;
        }
        probe->left = true;
        return an::Status::failure(an::ErrorCode::Cancelled, "the detached body never hands a result back");
    }

private:
    std::shared_ptr<BodyProbe> m_probe;
};

/// Lets a detached body go when the case ends, passing or failing, so nothing is left holding the pool.
class ReleasedAtTheEnd
{
public:
    explicit ReleasedAtTheEnd(std::shared_ptr<BodyProbe> probe) : m_probe(std::move(probe)) {}

    ~ReleasedAtTheEnd()
    {
        m_probe->release = true;
    }

    ReleasedAtTheEnd(const ReleasedAtTheEnd&) = delete;
    ReleasedAtTheEnd& operator=(const ReleasedAtTheEnd&) = delete;

private:
    std::shared_ptr<BodyProbe> m_probe;
};

/// Waits, without turning the event loop, until a condition holds or the patience runs out.
[[nodiscard]] bool waitWithoutEvents(const std::function<bool()>& condition)
{
    const auto until = std::chrono::steady_clock::now() + kPatience;
    while (std::chrono::steady_clock::now() < until)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

} // namespace

// A window deleted while its preview runs is gone at once: the body sees its stop and nothing of the run is published.
TEST(ImageRTPreviewControlQueueDrain, DeletingAWindowWhileItsPreviewRunsWaitsForNothingAndStopsThePreview)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    ASSERT_TRUE(document.has_value());
    ViewHandle target = makeTargetView("target", std::move(*document));
    ASSERT_TRUE(settleView(*target));
    const auto probe = std::make_shared<BodyProbe>();
    const ReleasedAtTheEnd released(probe);
    auto window = std::make_unique<DetachedBodyProcess>(target.get(), probe);
    window->setAttribute(Qt::WA_DeleteOnClose, false);

    window->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&probe]() { return probe->arrived.load(); }));
    EXPECT_FALSE(probe->sawStop.load());

    const auto deleting = std::chrono::steady_clock::now();
    window.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - deleting, std::chrono::seconds(1)) << "deleting waits for nothing";
    EXPECT_TRUE(waitWithoutEvents([&probe]() { return probe->sawStop.load(); })) << "the body sees its stop";

    probe->release = true;
    ASSERT_TRUE(waitWithoutEvents([&probe]() { return probe->left.load(); }));
    ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(kPatience).count())));
    pumpQuietly();
    EXPECT_EQ(context->errorCount(), 0) << "nothing was told of a run whose window is gone";
    EXPECT_NEAR(documentSample(target->document(), 0u, 0u), 0.25, 1e-6);
    const an::consumer::SessionOutstanding outstanding = context->engineSession().outstanding();
    EXPECT_EQ(outstanding.operations, 0u);
    EXPECT_EQ(outstanding.previewReservations, 0u) << "the window's claim on the preview share went with it";
}
