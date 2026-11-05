// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// A control that recomputes a display for every value a slider passes through reads its widgets and updates the
// view on the GUI thread, and only for the newest value: requests from any thread are coalesced and delivered where
// the widgets live, and a request outliving its control is dropped.

#include <gtest/gtest.h>

#include "app/CoalescedGuiUpdate.h"

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <memory>
#include <thread>
#include <vector>

namespace
{

struct Delivery
{
    int value = 0;
    bool onGuiThread = false;
};

TEST(CoalescedGuiUpdate, DeliversOnlyTheNewestRequestOncePerEventLoopTurn)
{
    QObject context;
    std::vector<Delivery> deliveries;
    an::ui::CoalescedGuiUpdate<int> update(
        &context, [&](const int& value) { deliveries.push_back({value, QThread::currentThread() == qApp->thread()}); });

    update.request(1);
    update.request(2);
    update.request(3);
    EXPECT_TRUE(deliveries.empty());

    QCoreApplication::processEvents();

    ASSERT_EQ(deliveries.size(), 1u);
    EXPECT_EQ(deliveries[0].value, 3);
    EXPECT_TRUE(deliveries[0].onGuiThread);

    update.request(4);
    QCoreApplication::processEvents();
    ASSERT_EQ(deliveries.size(), 2u);
    EXPECT_EQ(deliveries[1].value, 4);
}

TEST(CoalescedGuiUpdate, ARequestFromAWorkerIsDeliveredOnTheGuiThread)
{
    QObject context;
    std::vector<Delivery> deliveries;
    an::ui::CoalescedGuiUpdate<int> update(
        &context, [&](const int& value) { deliveries.push_back({value, QThread::currentThread() == qApp->thread()}); });

    std::thread worker([&] { update.request(7); });
    worker.join();
    EXPECT_TRUE(deliveries.empty());

    QCoreApplication::processEvents();

    ASSERT_EQ(deliveries.size(), 1u);
    EXPECT_EQ(deliveries[0].value, 7);
    EXPECT_TRUE(deliveries[0].onGuiThread);
}

TEST(CoalescedGuiUpdate, NothingIsDeliveredOnceTheContextIsGone)
{
    auto context = std::make_unique<QObject>();
    std::vector<Delivery> deliveries;
    {
        an::ui::CoalescedGuiUpdate<int> update(context.get(), [&](const int& value) {
            deliveries.push_back({value, QThread::currentThread() == qApp->thread()});
        });
        update.request(1);
    }

    context.reset();
    QCoreApplication::processEvents();

    EXPECT_TRUE(deliveries.empty());
}

} // namespace
