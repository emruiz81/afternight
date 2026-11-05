// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "fixtures/common/FixtureDigest.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace
{

using an::tests::fixtures::FixtureDigest;
using an::tests::fixtures::sha256Hex;

// The FIPS 180-4 example messages and their published digests.
TEST(FixtureDigestTest, TheStandardExamplesDigestToTheirPublishedValues)
{
    EXPECT_EQ(sha256Hex(std::string_view("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex(std::string_view("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex(std::string_view("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256Hex(std::string(1000000u, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(FixtureDigestTest, AMessageFedInPiecesDigestsAsItDoesWhole)
{
    std::string message;
    for (int index = 0; index < 1000; ++index)
    {
        message.push_back(static_cast<char>('A' + index % 53));
    }
    for (const std::size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 127u})
    {
        FixtureDigest digest;
        for (std::size_t offset = 0u; offset < message.size(); offset += piece)
        {
            digest.update(std::string_view(message).substr(offset, piece));
        }
        const std::string whole = sha256Hex(message);
        EXPECT_EQ(whole.size(), 64u);
        EXPECT_EQ(digest.finish(), whole) << "pieces of " << piece;
    }
}

TEST(FixtureDigestTest, BytesAndTextOfTheSameContentDigestAlike)
{
    const std::string text = "fixture";
    std::vector<std::byte> bytes;
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(character));
    }
    EXPECT_EQ(sha256Hex(text).size(), 64u);
    EXPECT_EQ(sha256Hex(std::span<const std::byte>(bytes)), sha256Hex(text));
}

} // namespace
