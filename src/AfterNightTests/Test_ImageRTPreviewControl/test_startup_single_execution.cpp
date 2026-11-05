// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file test_startup_single_execution.cpp
/// @brief Behavioral tests for the startup preview-scheduling contract.
///
/// The production ImageRTPreviewControl startup path now relies on a small
/// sequencing contract:
///   1. Constructor-time settings changes may call settingsUpdated().
///   2. Those calls must not enqueue preview work before the preview exists.
///   3. createPreviewControl() marks startup as pending, then defers exactly
///      one initial enqueue onto the next event-loop turn.
///
/// These tests exercise that contract as a lightweight Qt harness; the real
/// window's half of it is held by the process framework suite's
/// PreviewRunTest.ChangesBeforeThePreviewExistsRunNothingAndOpeningItRunsOnce.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <QTest>

#include <functional>

namespace
{

class StartupPreviewSchedulerHarness : public QObject
{
public:
    void settingsUpdated()
    {
        if (m_PreviewEnabled)
        {
            schedulePreviewProcessExecution();
        }
    }

    void setupPreviewControl(const bool useSubArea, const QSize& subAreaSize = QSize(0, 0))
    {
        m_UseSubArea = useSubArea;
        if (m_UseSubArea)
        {
            initializeSubArea(subAreaSize);
        }

        if (m_PreviewEnabled)
        {
            createPreviewControl();
        }
    }

    int scheduledExecutionCount() const
    {
        return m_ScheduledExecutionCount;
    }

    [[nodiscard]] QRect subArea() const
    {
        return m_SubArea;
    }

private:
    void initializeSubArea(const QSize& subAreaSize)
    {
        const QPoint centerPos = m_ImageArea.center();
        m_SubArea.setTopLeft(QPoint(centerPos.x() - subAreaSize.width() / 2, centerPos.y() - subAreaSize.height() / 2));
        m_SubArea.setSize(subAreaSize);

        if (m_SubArea.x() < 0)
        {
            m_SubArea.setX(0);
        }

        if (m_SubArea.y() < 0)
        {
            m_SubArea.setY(0);
        }

        if (m_SubArea.x() + m_SubArea.width() > m_ImageArea.width())
        {
            m_SubArea.setWidth(m_ImageArea.width() - m_SubArea.x());
        }

        if (m_SubArea.y() + m_SubArea.height() > m_ImageArea.height())
        {
            m_SubArea.setHeight(m_ImageArea.height() - m_SubArea.y());
        }
    }

    void schedulePreviewProcessExecution()
    {
        if (!m_PreviewExists || m_ProcessCloseRequested || m_StartupPreviewPending)
        {
            return;
        }

        ++m_ScheduledExecutionCount;
    }

    void createPreviewControl()
    {
        if (m_PreviewExists)
        {
            return;
        }

        m_PreviewExists = true;
        m_StartupPreviewPending = true;

        QTimer::singleShot(0, this, [this]() {
            m_StartupPreviewPending = false;

            if (!m_PreviewExists || !m_PreviewEnabled || m_ProcessCloseRequested)
            {
                return;
            }

            schedulePreviewProcessExecution();
        });
    }

    bool m_PreviewEnabled = true;
    bool m_PreviewExists = false;
    bool m_ProcessCloseRequested = false;
    bool m_StartupPreviewPending = false;
    bool m_UseSubArea = false;
    int m_ScheduledExecutionCount = 0;
    QRect m_ImageArea = QRect(0, 0, 256, 256);
    QRect m_SubArea;
};

bool waitForCondition(const std::function<bool()>& condition, const int timeoutMs = 1000)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs)
    {
        if (condition())
        {
            return true;
        }

        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }

    return condition();
}

void expectSingleStartupExecution(const bool useSubArea)
{
    StartupPreviewSchedulerHarness harness;

    // Mirror the constructor-time churn that previously caused duplicate
    // startup preview executions before the preview widget existed.
    harness.settingsUpdated();
    harness.settingsUpdated();

    if (useSubArea)
    {
        harness.setupPreviewControl(true, QSize(96, 96));
    }
    else
    {
        harness.setupPreviewControl(false);
    }

    ASSERT_TRUE(waitForCondition([&harness]() { return harness.scheduledExecutionCount() == 1; }))
        << "The deferred startup callback should enqueue exactly one initial preview execution";

    QTest::qWait(100);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    EXPECT_EQ(harness.scheduledExecutionCount(), 1)
        << "Startup scheduling should remain coalesced to a single initial execution";

    if (useSubArea)
    {
        EXPECT_TRUE(harness.subArea().isValid());
        EXPECT_EQ(harness.subArea().size(), QSize(96, 96));
    }
}

} // anonymous namespace

TEST(StartupSingleExecution, CoalescesStartupUpdatesWithoutSubArea)
{
    expectSingleStartupExecution(false);
}

TEST(StartupSingleExecution, CoalescesStartupUpdatesWithSubArea)
{
    expectSingleStartupExecution(true);
}
