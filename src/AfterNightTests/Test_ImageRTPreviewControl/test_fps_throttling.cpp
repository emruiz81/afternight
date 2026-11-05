// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The real-time preview's frame budget, run against the Controls library: previews start at most thirty times a
/// second unless the Processing settings state another rate, a preview asked for inside the budget waits out the rest
/// of it, and requests that arrive while it waits collapse into the one that starts, with the latest parameters.

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "controls/settings/SettingsKeys.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace
{

using namespace an::tests::processes;

/// A real-time preview process whose body returns at once with its source as it is, keeping the factor of every run and
/// the moment every preview started on the GUI thread; its frame budget is the one the settings state.
class RateProcess final : public an::ui::TypedProcessControl<TestParams>
{
public:
    explicit RateProcess(ImageViewControl* target) : TypedProcessControl<TestParams>(target, "Rate Scale")
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

    /// Changes the parameters the window holds, as a widget's change does, and asks for a preview.
    void setParams(const TestParams& params)
    {
        m_params = params;
        settingsUpdated();
    }

    /// When each preview started, on the GUI thread, in order.
    [[nodiscard]] const std::vector<std::chrono::steady_clock::time_point>& previewStarts() const
    {
        return m_starts;
    }

    /// The factor of every run the body saw, in order.
    [[nodiscard]] std::vector<double> factors() const
    {
        const std::lock_guard lock(m_factorsLock);
        return m_factors;
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
        return m_params;
    }

    void executeProcessStarted(const RTPreviewMode previewMode, const bool) override
    {
        if (previewMode == RTPreviewMode::AsyncPreview)
        {
            m_starts.push_back(std::chrono::steady_clock::now());
        }
    }

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource&, an::ui::ProcessDestination&,
                                       const an::ui::ProcessRun&, const TestParams& params,
                                       an::ui::ExecutionScope&) override
    {
        const std::lock_guard lock(m_factorsLock);
        m_factors.push_back(params.factor);
        return an::Status::success();
    }

private:
    TestParams m_params;
    std::vector<std::chrono::steady_clock::time_point> m_starts;
    mutable std::mutex m_factorsLock;
    std::vector<double> m_factors;
};

/// A rate process window, deleted with the case once its runs have landed.
struct RateProcessDeleter
{
    void operator()(RateProcess* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        (void)pumpUntil([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};
using RateHandle = std::unique_ptr<RateProcess, RateProcessDeleter>;

/// A rate process window over a target, without the delete-on-close the framework sets so the case decides when it
/// goes.
[[nodiscard]] RateHandle openRateProcess(ImageViewControl& target)
{
    RateHandle process(new RateProcess(&target));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    return process;
}

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] bool settleRate(const RateProcess& process)
{
    if (!pumpUntil([&process]() { return process.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return process.isProcessIdle();
}

/// A grey 8 by 6 Float32 target, every sample a quarter; null when it cannot be made.
[[nodiscard]] ViewHandle flatTarget(const an::consumer::EngineSession& session)
{
    std::optional<an::ui::ImageDocument> document =
        greyDocument(session, 8u, 6u, [](std::uint32_t, std::uint32_t) { return 0.25f; });
    if (!document.has_value())
    {
        return ViewHandle();
    }
    return makeTargetView("target", std::move(*document));
}

} // namespace

// Previews start at most thirty times a second unless the settings state another rate: one asked for just after
// another started waits out the thirtieth.
TEST(ImageRTPreviewControlFpsThrottling, PreviewsStartAtMostThirtyTimesASecondByDefault)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ViewHandle target = flatTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    RateHandle process = openRateProcess(*target);
    EXPECT_FALSE(context->selectOptional(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE)).has_value());

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->previewStarts().size() == 1u; }));
    process->setParams(TestParams{.factor = 3.0});
    ASSERT_TRUE(pumpUntil([&process]() { return process->previewStarts().size() == 2u; }));
    const std::chrono::steady_clock::duration spacing = process->previewStarts()[1] - process->previewStarts()[0];
    // A thirtieth of a second, less the grain of the clocks that read it.
    EXPECT_GE(spacing, std::chrono::microseconds(32500));

    ASSERT_TRUE(settleRate(*process));
    EXPECT_EQ(process->previewStarts().size(), 2u);
    const std::vector<double> factors = process->factors();
    ASSERT_EQ(factors.size(), 2u);
    EXPECT_DOUBLE_EQ(factors.back(), 3.0);
}

// At five previews a second, the fewest the settings allow, a burst of changes while the next preview waits starts one
// more, later, with the latest.
TEST(ImageRTPreviewControlFpsThrottling, RequestsWhileAPreviewWaitsOutItsBudgetCollapseIntoOneWithTheLatestParameters)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    (void)context->upsert(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE), QStringLiteral("5"));
    ViewHandle target = flatTarget(context->engineSession());
    ASSERT_NE(target, nullptr);
    RateHandle process = openRateProcess(*target);

    process->previewCheckbox->setChecked(true);
    ASSERT_TRUE(pumpUntil([&process]() { return process->runTelemetry().previewsCommitted == 1u; }));
    for (const double factor : {3.0, 4.0, 5.0})
    {
        process->setParams(TestParams{.factor = factor});
    }
    EXPECT_EQ(process->previewStarts().size(), 1u) << "the burst waits out the budget";
    ASSERT_TRUE(pumpUntil([&process]() { return process->previewStarts().size() == 2u; }));
    ASSERT_TRUE(settleRate(*process));
    pumpQuietly();

    const std::vector<std::chrono::steady_clock::time_point>& starts = process->previewStarts();
    ASSERT_EQ(starts.size(), 2u);
    EXPECT_GE(starts[1] - starts[0], std::chrono::milliseconds(199));
    const std::vector<double> factors = process->factors();
    ASSERT_EQ(factors.size(), 2u);
    EXPECT_DOUBLE_EQ(factors[0], 2.0);
    EXPECT_DOUBLE_EQ(factors[1], 5.0);
    const ProcessRunTelemetry telemetry = process->runTelemetry();
    EXPECT_EQ(telemetry.previewsRequested, 4u);
    EXPECT_EQ(telemetry.previewsStarted, 2u);
    EXPECT_EQ(telemetry.previewsCommitted, 2u);
}
