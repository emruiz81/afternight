// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The pieces every AfterNightEngine fuzz target shares: what it tells the driver about itself,
// how it reads structure out of a flat byte string, and how it says an invariant broke.
//
// A target is one translation unit that defines three things - `targetInfo`, `seedCorpus` and
// `LLVMFuzzerTestOneInput` - and nothing else. Which driver runs it is a property of the build, not
// of the target: a build with libFuzzer supplies `main` and drives the entry point with coverage
// feedback, and a build without one links `FuzzDriver.cpp`, which replays the seeds and then mutates
// them deterministically. The same target source is the subject of both, so a reproducer found by
// either is an input the other also accepts.
//
// **A broken invariant aborts.** There is no reporting channel back to the driver and no return code
// that means "this input was wrong", because libFuzzer recognises exactly one failure signal and it
// is a crashing process. `AN_FUZZ_CHECK` therefore prints what broke and calls `std::abort`, which
// the standalone driver turns into a written reproducer and libFuzzer turns into a `crash-` artifact.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

/// The entry point libFuzzer calls, and the one the standalone driver calls in its place.
///
/// @param data the input bytes. Never null when `size` is positive; may be null when it is zero.
/// @param size how many bytes there are.
/// @return zero. libFuzzer reserves every other value, so a target that wants to reject an input
///         returns zero after doing nothing with it.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace an::fuzz
{

/// What a target tells the driver about itself before the first input.
struct TargetInfo
{
    /// The target's own name, as the registry and the reports spell it.
    const char* name = "";

    /// The largest input this target wants to be given. The driver never grows a mutated input past
    /// it, so a target that writes its input to a file can bound what it writes.
    std::size_t maxInputBytes = 65536u;
};

/// What this target is.
///
/// Defined by each target's own translation unit.
///
/// @return the target's name and input bound.
[[nodiscard]] TargetInfo targetInfo();

/// The inputs this target wants replayed before anything is mutated.
///
/// Defined by each target's own translation unit. These are the corpus: the standalone driver
/// replays every one of them, then mutates them for the rest of its budget, and `--dump-corpus`
/// writes them out so a coverage-guided run can start from the same place.
///
/// A target whose subject is a file format builds its seeds by encoding one, so that a valid
/// container is a fact about the engine rather than a binary checked into the repository.
///
/// @return the seeds, which may be empty for a target whose input is unstructured text.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> seedCorpus();

/// Says an invariant broke and ends the process.
///
/// @param file the source file of the check.
/// @param line its line.
/// @param expression the condition as it was written.
/// @param message what the condition means.
[[noreturn]] void reportBrokenInvariant(const char* file, int line, const char* expression, const char* message);

/// Records which input is being evaluated, so a crash handler can write it out.
///
/// The standalone driver calls this before each input and nothing calls it in a libFuzzer build,
/// where libFuzzer already writes the input that crashed.
///
/// @param data the input bytes, borrowed until the next call.
/// @param size how many bytes there are.
/// @param iteration which iteration of the sweep this is.
void noteCurrentInput(const std::uint8_t* data, std::size_t size, std::uint64_t iteration);

/// Writes the input recorded by `noteCurrentInput` next to a description of why.
///
/// Called from the driver's crash handlers, so it uses no allocation it can avoid and reports
/// through `std::fputs` rather than through a stream.
///
/// @param reason what stopped the process, as one word for the file name.
void writeCurrentInputArtifact(const char* reason);

/// Where a reproducer is written. Empty until the driver sets one, which is the case in a libFuzzer
/// build, and then nothing is written.
///
/// @param directory the directory to write into. Created by the driver, not by this.
void setArtifactDirectory(std::string directory);

/// Counts how often a target reached something worth knowing it reached.
///
/// A fuzz target passes by not crashing, which means a target that refused every input at its first
/// branch reports exactly what one that exercised every path reports. That is the failure mode of a
/// campaign rather than of the code under test, and it is invisible: a mode byte read from the wrong
/// end, a layout that never builds, an engine that never starts, and the sweep still prints a clean
/// line with a large number in it.
///
/// So each target counts the handful of places that being reached is the whole point of, and the
/// driver prints the counts beside the iteration total. A count of zero is then a finding about the
/// target rather than a silence.
///
/// Counted on the calling thread only, which is the one the driver evaluates inputs on.
///
/// @param what the name of the thing reached. Must be a literal that outlives the process.
void countReached(const char* what) noexcept;

/// Prints every counter `countReached` was given, in the order they were first named.
///
/// @param prefix what to begin the line with, so a report is recognisable in a campaign's log.
void reportReached(const char* prefix);

/// Opens a file, spelled the way each toolchain wants it spelled.
///
/// A stream would be the ordinary choice, and it is the wrong one here: the crash handler that writes
/// a reproducer runs after the process has already lost something, and a `std::ofstream` allocates
/// and throws where a `FILE*` does neither.
///
/// @param path what to open.
/// @param mode the `fopen` mode string.
/// @return the file, or null.
[[nodiscard]] inline std::FILE* openFile(const char* path, const char* mode) noexcept
{
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return fopen_s(&file, path, mode) == 0 ? file : nullptr;
#else
    return std::fopen(path, mode);
#endif
}

/// What an environment variable holds, spelled the way each toolchain wants it spelled.
///
/// @param name the variable.
/// @return its value, or an empty string when it is unset or empty.
[[nodiscard]] inline std::string environmentValue(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0u;
    std::string copied;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
    {
        copied.assign(value);
    }
    std::free(value);
    return copied;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

/// A flat byte string read as the structure a target wants.
///
/// Two cursors rather than one, and the reason is what each end is for. A target whose subject is a
/// parser wants the whole input to be the text it parses, and still needs a byte or two to choose a
/// mode; taking those from the back leaves the front intact, so a mutation that changes the payload
/// does not also change which code path the payload reaches. A target whose subject is a set of
/// parameters takes everything from the front and never touches the back.
///
/// Reading past what is left yields zero rather than failing, so a target is total: every byte
/// string is a valid input, which is the property that lets a mutator generate them blindly.
class Input
{
public:
    /// Reads a byte string.
    ///
    /// @param data the bytes. May be null when `size` is zero.
    /// @param size how many there are.
    Input(const std::uint8_t* data, std::size_t size) noexcept : m_data(data), m_front(0u), m_back(size) {}

    /// How many bytes neither cursor has taken.
    ///
    /// @return the count.
    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return m_back - m_front;
    }

    /// What neither cursor has taken, in order.
    ///
    /// @return the bytes, borrowed for as long as the input is.
    [[nodiscard]] std::span<const std::uint8_t> rest() const noexcept
    {
        return {m_data + m_front, remaining()};
    }

    /// What neither cursor has taken, as text.
    ///
    /// @return a copy of the remaining bytes, which may hold a NUL and need not be valid UTF-8.
    [[nodiscard]] std::string restAsText() const noexcept
    {
        return std::string(reinterpret_cast<const char*>(m_data + m_front), remaining());
    }

    /// The next byte from the front.
    ///
    /// @return the byte, or zero when nothing is left.
    [[nodiscard]] std::uint8_t take() noexcept
    {
        if (remaining() == 0u)
        {
            return 0u;
        }
        return m_data[m_front++];
    }

    /// The next byte from the back.
    ///
    /// @return the byte, or zero when nothing is left.
    [[nodiscard]] std::uint8_t control() noexcept
    {
        if (remaining() == 0u)
        {
            return 0u;
        }
        return m_data[--m_back];
    }

    /// An unsigned integer of a stated width from the front, little-endian.
    ///
    /// @tparam ValueT the integer type to fill.
    /// @return the value, with missing bytes read as zero.
    template <typename ValueT> [[nodiscard]] ValueT takeInteger() noexcept
    {
        static_assert(std::is_trivially_copyable_v<ValueT>, "an integer is copied byte by byte");
        std::uint8_t bytes[sizeof(ValueT)] = {};
        for (std::size_t index = 0u; index < sizeof(ValueT); ++index)
        {
            bytes[index] = take();
        }
        ValueT value{};
        std::memcpy(&value, bytes, sizeof(ValueT));
        return value;
    }

    /// A `double` built from eight bytes, which is as likely to be infinite or not-a-number as any
    /// other bit pattern is.
    ///
    /// @return the value.
    [[nodiscard]] double takeDouble() noexcept
    {
        return takeInteger<double>();
    }

    /// A number below a bound, from the front.
    ///
    /// Reads as few bytes as the bound needs rather than a fixed four. A target chooses a dozen small
    /// things - a mode, an enumerator, a width - before it reads its payload, and at four bytes each
    /// that consumed every byte a short input had, leaving the payload a run of zeros. Which is worse
    /// than it sounds: a sweep in that state still evaluates its whole budget and still reports a
    /// clean line, because what it lost is coverage rather than correctness.
    ///
    /// @param bound one past the largest value wanted. Zero yields zero.
    /// @return the value.
    [[nodiscard]] std::uint32_t takeBelow(std::uint32_t bound) noexcept
    {
        if (bound == 0u)
        {
            return 0u;
        }
        if (bound <= 0x100u)
        {
            return static_cast<std::uint32_t>(take() % bound);
        }
        if (bound <= 0x10000u)
        {
            return static_cast<std::uint32_t>(takeInteger<std::uint16_t>() % bound);
        }
        return static_cast<std::uint32_t>(takeInteger<std::uint32_t>() % bound);
    }

    /// A number below a bound, from the back.
    ///
    /// @param bound one past the largest value wanted. Zero yields zero.
    /// @return the value.
    [[nodiscard]] std::uint32_t controlBelow(std::uint32_t bound) noexcept
    {
        if (bound == 0u)
        {
            return 0u;
        }
        return static_cast<std::uint32_t>(control() % bound);
    }

    /// Whether the next byte from the front is odd.
    ///
    /// @return the flag.
    [[nodiscard]] bool takeFlag() noexcept
    {
        return (take() & 1u) != 0u;
    }

    /// An enumerator, sometimes one this build does not know.
    ///
    /// The point of a fuzzed enumeration is the value just past the last enumerator, so the range is
    /// deliberately wider than the enumeration: a target that only ever saw valid values would never
    /// reach the membership check it exists to exercise. Two past and no further, because the margin
    /// is not free - a request of six enumerated fields with a four-value margin is refused by a
    /// membership check nine times in ten, and every one of those refusals is the same check, taken
    /// out of the budget that should have been spent on the rules behind it.
    ///
    /// @tparam EnumT the enumeration.
    /// @param lastKnown the last enumerator this build has.
    /// @return a value at most two past that one.
    template <typename EnumT> [[nodiscard]] EnumT takeEnum(EnumT lastKnown) noexcept
    {
        using Underlying = std::underlying_type_t<EnumT>;
        const std::uint32_t span = static_cast<std::uint32_t>(static_cast<Underlying>(lastKnown)) + 3u;
        return static_cast<EnumT>(static_cast<Underlying>(takeBelow(span)));
    }

    /// A run of bytes from the front.
    ///
    /// @param count how many are wanted.
    /// @return that many bytes, short only when the input ran out.
    [[nodiscard]] std::vector<std::uint8_t> takeBytes(std::size_t count) noexcept
    {
        const std::size_t taken = count < remaining() ? count : remaining();
        std::vector<std::uint8_t> bytes(m_data + m_front, m_data + m_front + taken);
        m_front += taken;
        return bytes;
    }

    /// Text of at most a stated length from the front.
    ///
    /// @param maxBytes the bound.
    /// @return the text, which may hold a NUL and need not be valid UTF-8.
    [[nodiscard]] std::string takeText(std::size_t maxBytes) noexcept
    {
        const std::size_t wanted = static_cast<std::size_t>(takeInteger<std::uint16_t>()) % (maxBytes + 1u);
        const std::vector<std::uint8_t> bytes = takeBytes(wanted);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

private:
    const std::uint8_t* m_data;
    std::size_t m_front;
    std::size_t m_back;
};

} // namespace an::fuzz

/// Says that something must hold of every input, and ends the process when it does not.
///
/// @param condition what must hold.
/// @param message what it means, in a few words, for the report.
#define AN_FUZZ_CHECK(condition, message)                                                 \
    do                                                                                    \
    {                                                                                     \
        if (!(condition))                                                                 \
        {                                                                                 \
            ::an::fuzz::reportBrokenInvariant(__FILE__, __LINE__, #condition, (message)); \
        }                                                                                 \
    } while (false)
