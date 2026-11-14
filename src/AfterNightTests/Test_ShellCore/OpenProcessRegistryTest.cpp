// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The process windows the application knows are open: each held once, forgotten when it goes, listed and visited,
/// from any thread.

#include "services/OpenProcessRegistry.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <set>
#include <thread>
#include <vector>

class TopLevelProcessBaseControl;

namespace
{

using an::ui::shell::OpenProcessRegistry;

/// Distinct window addresses the registry is handed and never follows: it holds windows, it does not use them.
class Windows
{
public:
    [[nodiscard]] TopLevelProcessBaseControl* operator[](std::size_t index)
    {
        return reinterpret_cast<TopLevelProcessBaseControl*>(&m_storage.at(index));
    }

private:
    std::array<int, 64> m_storage{};
};

TEST(OpenProcessRegistry, HoldsEachRegisteredWindowOnce)
{
    Windows windows;
    OpenProcessRegistry registry;
    registry.add(windows[0]);
    registry.add(windows[1]);
    registry.add(windows[0]);
    registry.add(nullptr);

    EXPECT_EQ(registry.size(), 2u);
    EXPECT_TRUE(registry.contains(windows[0]));
    EXPECT_TRUE(registry.contains(windows[1]));
}

TEST(OpenProcessRegistry, ForgetsAWindowThatWent)
{
    Windows windows;
    OpenProcessRegistry registry;
    registry.add(windows[0]);
    registry.add(windows[1]);

    registry.remove(windows[0]);
    // A window it never held, or one it forgot already, is no error.
    registry.remove(windows[0]);
    registry.remove(windows[2]);

    EXPECT_EQ(registry.size(), 1u);
    EXPECT_FALSE(registry.contains(windows[0]));
    EXPECT_TRUE(registry.contains(windows[1]));
}

TEST(OpenProcessRegistry, ListsAndVisitsTheWindowsItHolds)
{
    Windows windows;
    OpenProcessRegistry registry;
    for (std::size_t index = 0; index < 3u; ++index)
    {
        registry.add(windows[index]);
    }

    const std::vector<TopLevelProcessBaseControl*> listed = registry.snapshot();
    const std::set<TopLevelProcessBaseControl*> expected{windows[0], windows[1], windows[2]};
    EXPECT_EQ(std::set<TopLevelProcessBaseControl*>(listed.begin(), listed.end()), expected);
    EXPECT_EQ(listed.size(), 3u);

    std::set<TopLevelProcessBaseControl*> visited;
    registry.forEach([&visited](TopLevelProcessBaseControl* window) { visited.insert(window); });
    EXPECT_EQ(visited, expected);
}

// A process window registers itself as it is made and forgets itself as it goes, and the close of the application
// counts the open ones: each from whatever thread it runs on.
TEST(OpenProcessRegistry, TakesWindowsFromSeveralThreadsAtOnce)
{
    Windows windows;
    OpenProcessRegistry registry;
    constexpr std::size_t kThreads = 8u;
    constexpr std::size_t kPerThread = 8u;
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < kThreads; ++thread)
    {
        threads.emplace_back([&registry, &windows, thread]() {
            for (int round = 0; round < 200; ++round)
            {
                for (std::size_t index = 0; index < kPerThread; ++index)
                {
                    registry.add(windows[thread * kPerThread + index]);
                }
                (void)registry.size();
                for (std::size_t index = 0; index < kPerThread; index += 2u)
                {
                    registry.remove(windows[thread * kPerThread + index]);
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(registry.size(), kThreads * kPerThread / 2u);
}

} // namespace
