// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SHA-256 digest: the standard's known answers on both implementations, and the two implementations agreeing
/// over every message length and every way of handing a message over in pieces.

#include <AfterNightPlatform/Sha256.h>

#include "PlatformTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace platform = an::platform;
using platform::Sha256;
using platform::Sha256Implementation;

/// The digest of a message handed over whole, as lowercase hexadecimal.
std::string digestOf(std::string_view message, Sha256Implementation implementation)
{
    Sha256 hash(implementation);
    hash.update(std::as_bytes(std::span(message.data(), message.size())));
    return platform::sha256Hex(hash.finish());
}

/// Both implementations, the portable one first; the instructions only where the processor has them.
std::vector<Sha256Implementation> implementations()
{
    std::vector<Sha256Implementation> all{Sha256Implementation::Portable};
    if (platform::sha256InstructionsAvailable())
    {
        all.push_back(Sha256Implementation::Instructions);
    }
    return all;
}

/// A deterministic message of `length` bytes that no block of it repeats.
std::vector<std::byte> patterned(std::size_t length)
{
    std::vector<std::byte> bytes(length);
    std::uint32_t state = 0x2545f491u;
    for (std::byte& value : bytes)
    {
        state = state * 1664525u + 1013904223u;
        value = static_cast<std::byte>(state >> 24u);
    }
    return bytes;
}

// FIPS 180-4's examples and NIST's long messages, from the standard's own example values.
TEST(Sha256Test, EveryImplementationGivesTheStandardsAnswers)
{
    for (const Sha256Implementation implementation : implementations())
    {
        SCOPED_TRACE(implementation == Sha256Implementation::Portable ? "portable" : "instructions");
        EXPECT_EQ(digestOf("", implementation), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        EXPECT_EQ(digestOf("abc", implementation), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        EXPECT_EQ(digestOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", implementation),
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        EXPECT_EQ(
            digestOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopq"
                     "rsmnopqrstnopqrstu",
                     implementation),
            "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
        const std::string million(1000000u, 'a');
        EXPECT_EQ(digestOf(million, implementation),
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
}

// A message is the same message however it is handed over: whole, a byte at a time, or in pieces that straddle the
// 64-byte blocks, at every length from nothing to past two hundred blocks.
TEST(Sha256Test, TheImplementationsAgreeAtEveryLengthAndEveryChunking)
{
    const std::vector<std::byte> message = patterned(13000u);
    for (std::size_t length = 0u; length <= 13000u; length += length < 300u ? 1u : 97u)
    {
        const std::span<const std::byte> whole(message.data(), length);
        Sha256 reference(Sha256Implementation::Portable);
        reference.update(whole);
        const std::array<std::uint8_t, 32> expected = reference.finish();
        for (const Sha256Implementation implementation : implementations())
        {
            for (const std::size_t piece : {std::size_t{1u}, std::size_t{7u}, std::size_t{63u}, std::size_t{64u},
                                            std::size_t{65u}, std::size_t{1000u}, length + 1u})
            {
                Sha256 hash(implementation);
                for (std::size_t offset = 0u; offset < length; offset += piece)
                {
                    hash.update(whole.subspan(offset, (std::min)(piece, length - offset)));
                }
                ASSERT_EQ(hash.finish(), expected) << "length " << length << ", pieces of " << piece;
            }
        }
    }
}

// The automatic choice is the instructions where the processor has them and the portable path elsewhere, and it is
// the digest either gives.
TEST(Sha256Test, TheAutomaticChoiceIsTheFastestThatRunsHere)
{
    const Sha256 automatic;
    EXPECT_EQ(automatic.implementation(), platform::sha256InstructionsAvailable() ? Sha256Implementation::Instructions
                                                                                  : Sha256Implementation::Portable);
    EXPECT_EQ(digestOf("abc", Sha256Implementation::Automatic),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// Asking for the instructions on a processor without them is answered by the portable path rather than a fault.
TEST(Sha256Test, InstructionsAskedForWhereTheyAreAbsentFallBackToThePortablePath)
{
    const Sha256 asked(Sha256Implementation::Instructions);
    if (!platform::sha256InstructionsAvailable())
    {
        EXPECT_EQ(asked.implementation(), Sha256Implementation::Portable);
    }
    else
    {
        EXPECT_EQ(asked.implementation(), Sha256Implementation::Instructions);
    }
}

// A finished hash starts again: the next message is digested from the beginning.
TEST(Sha256Test, FinishingStartsANewMessage)
{
    for (const Sha256Implementation implementation : implementations())
    {
        Sha256 hash(implementation);
        const std::string_view first = "a message that is thrown away";
        hash.update(std::as_bytes(std::span(first.data(), first.size())));
        static_cast<void>(hash.finish());
        const std::string_view second = "abc";
        hash.update(std::as_bytes(std::span(second.data(), second.size())));
        EXPECT_EQ(platform::sha256Hex(hash.finish()),
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
}

// A file's digest is the digest of its bytes, across the mebibyte pieces it is read in; a file that is not there
// has none.
TEST(Sha256Test, AFilesDigestIsTheDigestOfItsBytes)
{
    const std::filesystem::path directory = an::platform::test::caseDirectory();
    const std::filesystem::path small = directory / "abc.txt";
    {
        std::ofstream stream(small, std::ios::binary);
        stream << "abc";
    }
    const auto smallDigest = platform::sha256OfFile(small);
    ASSERT_TRUE(smallDigest.has_value());
    EXPECT_EQ(platform::sha256Hex(*smallDigest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    const std::vector<std::byte> bytes = patterned((std::size_t{3u} << 20u) + 4321u);
    const std::filesystem::path large = directory / "large.bin";
    {
        std::ofstream stream(large, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Sha256 reference(Sha256Implementation::Portable);
    reference.update(bytes);
    const std::array<std::uint8_t, 32> expected = reference.finish();
    for (const Sha256Implementation implementation : implementations())
    {
        const auto digest = platform::sha256OfFile(large, implementation);
        ASSERT_TRUE(digest.has_value());
        EXPECT_EQ(*digest, expected);
    }

    EXPECT_FALSE(platform::sha256OfFile(directory / "absent.bin").has_value());
}

} // namespace
