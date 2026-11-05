// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The requests for a read one worker serves: the first starts the worker, those made while it reads make it read once
/// more, and a request made between the worker's last read and its end is not lost to it.

#include "app/ReadRequests.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

using an::ui::ReadRequests;

// The first request starts a worker and a second finds one on its way; the worker, finding a request made while it
// read, reads again, and ends once none was; the next request then starts a worker of its own.
TEST(ReadRequestsTest, TheFirstRequestStartsAWorkerAndOneDuringTheReadMakesItReadOnceMore)
{
    ReadRequests requests;
    EXPECT_TRUE(requests.request());
    const std::uint32_t first = requests.begin();
    EXPECT_FALSE(requests.request()) << "a second request started a second worker";
    EXPECT_TRUE(requests.end(first)) << "the request made during the read was lost";
    const std::uint32_t second = requests.begin();
    EXPECT_FALSE(requests.end(second)) << "the worker read again with nothing asked";
    EXPECT_TRUE(requests.request()) << "a request after the worker ended did not start a worker";
}

// Requests made during one read are answered by one more read, not one each.
TEST(ReadRequestsTest, RequestsDuringOneReadAreAnsweredByOneMore)
{
    ReadRequests requests;
    ASSERT_TRUE(requests.request());
    const std::uint32_t first = requests.begin();
    EXPECT_FALSE(requests.request());
    EXPECT_FALSE(requests.request());
    EXPECT_FALSE(requests.request());
    ASSERT_TRUE(requests.end(first));
    const std::uint32_t second = requests.begin();
    EXPECT_FALSE(requests.end(second)) << "the requests made during one read were read more than once more";
}

// A request made after the worker counted what it answers and before it took that count off - after its read, before
// its end - is the one a pair of flags lost: the worker's end cleared the request while another read, started before
// the request, had already read. Here the end finds the request and reads for it, and no later worker exists to clear
// it: a worker is started only once the count is at zero.
TEST(ReadRequestsTest, ARequestBetweenTheWorkersReadAndItsEndIsReadOnceMore)
{
    ReadRequests requests;
    ASSERT_TRUE(requests.request());
    const std::uint32_t answered = requests.begin();
    // The read is over; the request lands before the worker's end.
    EXPECT_FALSE(requests.request()) << "a worker was started beside the one still ending";
    EXPECT_TRUE(requests.end(answered)) << "the request made after the read was lost";
    const std::uint32_t again = requests.begin();
    EXPECT_EQ(again, 1u);
    EXPECT_FALSE(requests.end(again));
}

} // namespace
