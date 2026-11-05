// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEngineGmicAdapter/GmicRuntimeAdapter.h.
//
// What is being tested is a boundary: who owns the memory on each side of it, who may write through
// it, what happens when the thing on the far side changes the picture into something this engine
// cannot describe, and what happens when the caller stops it halfway.
//
// The cases that need the library say so. A build regenerated with AFTERNIGHT_GMIC=0 contains this
// adapter with no library in it, and the `GmicSessionCase` fixture skips there with a message instead
// of passing - a case that passed without running would be reporting that the absent path works when
// it had not been taken. The absent path has cases of its own, which run in that configuration and
// are the only ones that can.
//
// Three of the cases are about somebody else's defects rather than this adapter's behaviour, and
// they are here because containing them is what this adapter is for. The library's C bridge writes
// one structure per image a command produced into an array sized for the images it was given, and it
// copies an image's name back with no bound at all - so a command that splits an image, or renames
// it past the field's length, corrupts the caller's memory in every program that calls the bridge
// the obvious way. This adapter reserves room for both and refuses rather than reporting a success
// over memory the library overran.

#include <AfterNightEngineGmicAdapter/GmicRuntimeAdapter.h>

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>
#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/RawFrame.h>
#include <AfterNightEngine/Image/SampleBuffer.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// An engine context every case starts from.
///
/// Four workers rather than one, because a case that holds two executions at once - a stoppable one
/// and a plain one to read the result back with - needs a budget that admits both.
[[nodiscard]] an::EngineContext makeEngineContext()
{
    an::EngineContextOptions options;
    options.maxConcurrency = 4u;

    an::Result<an::EngineContext> created = an::EngineContext::create(options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::EngineContext{};
}

/// An execution context with no stop token, for the cases that are not about cancellation.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
}

/// A tightly packed planar float layout, which is the one arrangement the library addresses directly.
[[nodiscard]] an::ImageLayout packedFloatLayout(std::uint32_t width, std::uint32_t height, std::uint32_t planes)
{
    const an::EngineLimits limits = an::defaultEngineLimits();
    const std::optional<an::ImageExtent> extent = an::ImageExtent::create(width, height, planes, limits);
    EXPECT_TRUE(extent.has_value());
    const an::ImageLayoutRequest request{.extent = *extent,
                                         .elementSizeBytes = 4u,
                                         .samplesPerPixel = 1u,
                                         .planeCount = planes,
                                         .rowAlignmentBytes = 1u};
    const std::uint64_t row = static_cast<std::uint64_t>(width) * 4u;
    const std::optional<an::ImageLayout> layout =
        an::ImageLayout::createWithStrides(request, row, row * height, limits);
    EXPECT_TRUE(layout.has_value());
    return *layout;
}

[[nodiscard]] constexpr an::ExternalBufferContract sourceContract(an::BufferMutability mutability) noexcept
{
    return an::ExternalBufferContract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                      .ownership = an::BufferOwnership::EngineOwned,
                                      .scope = an::BorrowScope::SingleCall,
                                      .mutability = mutability};
}

/// Samples and the description over them, kept together so a case cannot outlive one with the other.
struct Picture
{
    std::vector<float> samples;
    an::ImageLayout layout;
    an::GmicImageDescription description{};
};

[[nodiscard]] Picture makePicture(std::uint32_t width, std::uint32_t height, std::uint32_t planes, float value,
                                  an::BufferMutability mutability)
{
    Picture picture{std::vector<float>(static_cast<std::size_t>(width) * height * planes, value),
                    packedFloatLayout(width, height, planes)};
    const std::optional<an::ImageView<float>> image =
        an::ImageView<float>::create(std::span<float>(picture.samples), picture.layout);
    EXPECT_TRUE(image.has_value());
    const std::optional<an::GmicImageDescription> described =
        an::describeAsGmicImage(*image, sourceContract(mutability));
    EXPECT_TRUE(described.has_value());
    if (described.has_value())
    {
        picture.description = *described;
    }
    return picture;
}

/// What a result means when a case does not care - one plain plane of normalized values.
[[nodiscard]] constexpr an::GmicResultIntent monoIntent() noexcept
{
    return an::GmicResultIntent{.channelSchema = an::ChannelSchema::Mono,
                                .colorSpace = an::ColorSpace::Unspecified,
                                .numericDomain = an::NumericDomain::NormalizedLinear,
                                .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                .pixelAspectRatio = 1.0};
}

/// The samples a frame holds, read back as floats.
[[nodiscard]] std::vector<float> resultSamples(const an::RawFrame& frame)
{
    const std::span<const std::byte> bytes = frame.bytes();
    std::vector<float> values(bytes.size() / sizeof(float));
    std::memcpy(values.data(), bytes.data(), values.size() * sizeof(float));
    return values;
}

/// An open session, or an empty optional with the case already skipped.
class GmicSessionCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!an::gmicAdapterCapabilities().libraryCompiledIn)
        {
            GTEST_SKIP() << "this adapter was built with no G'MIC library; the absent-path cases cover it";
        }
        m_engine = makeEngineContext();
        ASSERT_TRUE(m_engine.valid());
        an::Result<an::GmicRuntimeAdapter> created = an::GmicRuntimeAdapter::create(m_engine, options());
        ASSERT_TRUE(created.hasValue()) << created.status().message();
        m_session = std::move(*created);
    }

    void TearDown() override
    {
        if (m_session.valid())
        {
            EXPECT_TRUE(m_session.shutdown().ok());
        }
    }

    /// What this fixture opens its session with. Overridden by the cancellation cases.
    [[nodiscard]] virtual an::GmicRuntimeOptions options() const
    {
        return an::GmicRuntimeOptions{};
    }

    an::EngineContext m_engine;
    an::GmicRuntimeAdapter m_session;
};

} // namespace

// --- both configurations ------------------------------------------------------------------------

TEST(GmicRuntimeAdapterTest, ASessionThatHoldsNothingRefusesEveryCallByName)
{
    an::GmicRuntimeAdapter empty;
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);

    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.engineContextId(), 0u);
    EXPECT_EQ(empty.callsInFlight(), 0u);
    EXPECT_EQ(empty.counters().calls, 0u);

    const an::Status executed = empty.execute(execution, "fill 0");
    EXPECT_EQ(executed.code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::gmicAdapterReason(executed), an::GmicAdapterReason::AdapterShutDown);

    Picture picture = makePicture(2u, 2u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    const an::Result<an::RawFrame> ran =
        empty.run(execution, an::GmicImageCall{.command = "fill 0", .source = picture.description});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::AdapterShutDown);

    // Shutting down what holds nothing is not an error, so cleanup that does it twice is correct.
    EXPECT_TRUE(empty.shutdown().ok());
    EXPECT_TRUE(empty.shutdown().ok());
}

TEST(GmicRuntimeAdapterTest, AnEngineContextThatHoldsNothingCannotCarryASession)
{
    const an::EngineContext empty;
    an::Result<an::GmicRuntimeAdapter> created = an::GmicRuntimeAdapter::create(empty, an::GmicRuntimeOptions{});

    ASSERT_FALSE(created.hasValue());
    if (an::gmicAdapterCapabilities().libraryCompiledIn)
    {
        EXPECT_EQ(created.status().code(), an::ErrorCode::NotInitialized);
    }
    else
    {
        // A build with no library answers before the other rules are reached, which is the order the
        // header states: there is nothing for them to decide.
        EXPECT_EQ(an::gmicAdapterReason(created.status()), an::GmicAdapterReason::LibraryNotCompiledIn);
    }
}

TEST(GmicRuntimeAdapterTest, AnOptionOutsideItsEnumerationIsRefusedBeforeAnythingIsAsked)
{
    if (!an::gmicAdapterCapabilities().libraryCompiledIn)
    {
        // A build with no library answers before the option rules are reached, which is the order
        // the header states: there is nothing for them to decide.
        GTEST_SKIP() << "this adapter was built with no G'MIC library";
    }
    an::EngineContext engine = makeEngineContext();
    an::GmicRuntimeOptions options;
    options.cancellation = static_cast<an::GmicCancellationPolicy>(9);

    an::Result<an::GmicRuntimeAdapter> created = an::GmicRuntimeAdapter::create(engine, options);
    ASSERT_FALSE(created.hasValue());
    EXPECT_EQ(created.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::gmicAdapterReason(created.status()), an::GmicAdapterReason::OptionsRejected);
    // A rejected option is not a load failure: nothing was loaded, so the report must not say one was.
    EXPECT_NE(an::gmicAdapterCapabilities().state, an::GmicAdapterState::FailedToLoad);
}

TEST(GmicRuntimeAdapterTest, ACustomCommandBlockWithAnEmbeddedNullIsRefused)
{
    if (!an::gmicAdapterCapabilities().libraryCompiledIn)
    {
        GTEST_SKIP() << "this adapter was built with no G'MIC library";
    }
    an::EngineContext engine = makeEngineContext();
    const std::string commands("first :\n  fill 0\0second :\n  fill 1", 34);
    an::GmicRuntimeOptions options;
    options.customCommands = std::string_view(commands.data(), commands.size());

    an::Result<an::GmicRuntimeAdapter> created = an::GmicRuntimeAdapter::create(engine, options);
    ASSERT_FALSE(created.hasValue());
    EXPECT_EQ(created.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::gmicAdapterReason(created.status()), an::GmicAdapterReason::OptionsRejected);
}

TEST(GmicRuntimeAdapterTest, TheTwoCancellationPoliciesAndTwoSharingDecisionsAreClosedSets)
{
    EXPECT_TRUE(an::isGmicCancellationPolicy(an::GmicCancellationPolicy::AtBoundary));
    EXPECT_TRUE(an::isGmicCancellationPolicy(an::GmicCancellationPolicy::DuringCall));
    EXPECT_FALSE(an::isGmicCancellationPolicy(static_cast<an::GmicCancellationPolicy>(2)));
    EXPECT_TRUE(an::isGmicSourceSharing(an::GmicSourceSharing::Copy));
    EXPECT_TRUE(an::isGmicSourceSharing(an::GmicSourceSharing::Share));
    EXPECT_FALSE(an::isGmicSourceSharing(static_cast<an::GmicSourceSharing>(2)));
    // Copying is the default, because it is the decision that leaves the caller's image alone.
    EXPECT_EQ(an::GmicImageCall{}.sharing, an::GmicSourceSharing::Copy);
}

// --- the absent configuration -------------------------------------------------------------------

TEST(GmicAbsentLibraryTest, ABuildWithNoLibraryRefusesEverySessionByName)
{
    if (an::gmicAdapterCapabilities().libraryCompiledIn)
    {
        GTEST_SKIP() << "this adapter was built with a G'MIC library; the session cases cover it";
    }
    an::EngineContext engine = makeEngineContext();

    an::Result<an::GmicRuntimeAdapter> created = an::GmicRuntimeAdapter::create(engine, an::GmicRuntimeOptions{});
    ASSERT_FALSE(created.hasValue());
    EXPECT_EQ(created.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::gmicAdapterReason(created.status()), an::GmicAdapterReason::LibraryNotCompiledIn);

    // The refusal is the whole of the absent path: nothing is held, nothing is probed, and the state
    // stays what it was rather than becoming a failure to load something that is not there.
    const an::GmicAdapterCapabilities capabilities = an::gmicAdapterCapabilities();
    EXPECT_EQ(capabilities.state, an::GmicAdapterState::Absent);
    EXPECT_FALSE(capabilities.probed);
    EXPECT_EQ(capabilities.openSessions, 0u);
}

// --- the library's own behaviour ----------------------------------------------------------------

TEST_F(GmicSessionCase, AnOpenSessionReportsItselfLoadedAndHoldsItsContext)
{
    const an::GmicAdapterCapabilities capabilities = m_session.capabilities();
    EXPECT_EQ(capabilities.state, an::GmicAdapterState::Loaded);
    EXPECT_TRUE(capabilities.libraryCompiledIn);
    EXPECT_TRUE(capabilities.probed);
    EXPECT_GE(capabilities.openSessions, 1u);
    const an::ExecutionContext execution = makeExecution(m_engine);
    EXPECT_EQ(m_session.engineContextId(), execution.engineContextId());
    EXPECT_EQ(m_session.callsInFlight(), 0u);

    const an::GmicCallCounters counters = m_session.counters();
    EXPECT_EQ(counters.calls, 0u);
    EXPECT_EQ(counters.sharedBytes, 0u);
    EXPECT_EQ(counters.copiedIntoLibraryBytes, 0u);
    EXPECT_EQ(counters.libraryResultBytes, 0u);
}

TEST_F(GmicSessionCase, AContextWithALiveSessionRefusesToShutDown)
{
    const an::Status refused = m_engine.shutdown();
    EXPECT_TRUE(refused.failed()) << "a session holds a lease, so the context it belongs to is in use";

    ASSERT_TRUE(m_session.shutdown().ok());
    EXPECT_TRUE(m_engine.shutdown().ok());
}

TEST_F(GmicSessionCase, TwoSessionsCoexistBecauseTheLibraryHasNoSeatToTake)
{
    // The difference from the Python adapter worth pinning: CPython has one interpreter state per
    // process and refuses the second adapter. This library builds an interpreter per call and keeps
    // nothing between them, so a second session is ordinary - what the two share is the lock.
    an::Result<an::GmicRuntimeAdapter> second = an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{});
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    EXPECT_GE(an::gmicAdapterCapabilities().openSessions, 2u);
    EXPECT_TRUE(second->shutdown().ok());
    EXPECT_GE(an::gmicAdapterCapabilities().openSessions, 1u);
}

TEST_F(GmicSessionCase, AnEmptyCommandIsRefusedWithoutEnteringTheLibrary)
{
    const an::ExecutionContext execution = makeExecution(m_engine);

    const an::Status executed = m_session.execute(execution, "");
    EXPECT_EQ(executed.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::gmicAdapterReason(executed), an::GmicAdapterReason::CommandEmpty);
    EXPECT_EQ(m_session.counters().calls, 0u) << "a refusal before the library is not a call into it";
}

TEST_F(GmicSessionCase, ACommandWithNoImageRunsAndAFailingOneCarriesTheLibrarysOwnSentence)
{
    const an::ExecutionContext execution = makeExecution(m_engine);

    EXPECT_TRUE(m_session.execute(execution, "v 0").ok())
        << "a command that does nothing observable still proves the interpreter ran";

    const an::Status failed = m_session.execute(execution, "this_is_not_a_gmic_command");
    EXPECT_EQ(failed.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::gmicAdapterReason(failed), an::GmicAdapterReason::CommandFailed);
    EXPECT_STRNE(failed.message(), "") << "the library's own text is what a person needs";
    EXPECT_EQ(m_session.counters().commandFailures, 1u);
}

TEST_F(GmicSessionCase, CopyingLeavesTheSourceAloneAndHandsBackTheLibrarysOwnMemory)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(6u, 4u, 1u, 0.25f, an::BufferMutability::ReadOnly);

    an::Result<an::RawFrame> ran = m_session.run(execution, an::GmicImageCall{.command = "add 0.5",
                                                                              .imageName = "source",
                                                                              .source = picture.description,
                                                                              .sharing = an::GmicSourceSharing::Copy,
                                                                              .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_TRUE(ran->valid());

    for (const float sample : picture.samples)
    {
        EXPECT_FLOAT_EQ(sample, 0.25f) << "a copied source is exactly as it was";
    }
    const std::vector<float> produced = resultSamples(*ran);
    ASSERT_EQ(produced.size(), picture.samples.size());
    for (const float sample : produced)
    {
        EXPECT_FLOAT_EQ(sample, 0.75f);
    }

    // The result is the library's memory, released through the token the frame holds. It is
    // deliberately not adoptable: an image built on it would have storage this engine does not own.
    EXPECT_EQ(ran->buffer().ownership(), an::SampleBufferOwnership::DecoderOwned);
    EXPECT_FALSE(ran->buffer().adoptable());
    EXPECT_TRUE(ran->checkAdoptable().failed());

    const an::GmicCallCounters counters = m_session.counters();
    EXPECT_EQ(counters.calls, 1u);
    EXPECT_EQ(counters.sharedBytes, 0u);
    EXPECT_EQ(counters.copiedIntoLibraryBytes, picture.samples.size() * sizeof(float));
    EXPECT_EQ(counters.libraryResultBytes, picture.samples.size() * sizeof(float));
    EXPECT_EQ(counters.peakLibraryResultBytes, picture.samples.size() * sizeof(float));
}

TEST_F(GmicSessionCase, SharingGivesTheLibraryTheEnginesOwnSamplesAndCopiesNothing)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(6u, 4u, 1u, 0.25f, an::BufferMutability::ReadWrite);
    const void* before = picture.description.data;

    an::Result<an::RawFrame> ran = m_session.run(execution, an::GmicImageCall{.command = "add 0.5",
                                                                              .source = picture.description,
                                                                              .sharing = an::GmicSourceSharing::Share,
                                                                              .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    for (const float sample : picture.samples)
    {
        EXPECT_FLOAT_EQ(sample, 0.75f) << "sharing consumes the source; that is what it costs";
    }
    EXPECT_EQ(static_cast<const void*>(ran->bytes().data()), before)
        << "an in-place command leaves the result where the source was";

    // Borrowed rather than owned: nothing here may be freed by whoever receives it, and the
    // ownership vocabulary is what says so instead of a convention in a comment.
    EXPECT_EQ(ran->buffer().ownership(), an::SampleBufferOwnership::Borrowed);
    EXPECT_FALSE(ran->buffer().adoptable());

    const an::GmicCallCounters counters = m_session.counters();
    EXPECT_EQ(counters.sharedBytes, picture.samples.size() * sizeof(float));
    EXPECT_EQ(counters.copiedIntoLibraryBytes, 0u);
    EXPECT_EQ(counters.libraryResultBytes, 0u) << "nothing was allocated on the far side";
}

TEST_F(GmicSessionCase, SharingReadOnlySamplesIsRefusedRatherThanNarrowed)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);

    const an::Result<an::RawFrame> ran =
        m_session.run(execution, an::GmicImageCall{.command = "add 1",
                                                   .source = picture.description,
                                                   .sharing = an::GmicSourceSharing::Share,
                                                   .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::SourceNotShareable);
    EXPECT_EQ(m_session.counters().bridgeRefusals, 1u);
}

TEST_F(GmicSessionCase, ASourceTheBridgeWouldNotDescribeIsRefusedBeforeTheLibrary)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    an::GmicImageDescription broken = picture.description;
    broken.data = nullptr;

    const an::Result<an::RawFrame> ran =
        m_session.run(execution, an::GmicImageCall{.command = "add 1", .source = broken, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::BufferBridgeRefused);
    EXPECT_EQ(m_session.counters().calls, 0u);
}

TEST_F(GmicSessionCase, ACommandThatChangesTheChannelCountMustAgreeWithTheStatedSchema)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 3u, 0.5f, an::BufferMutability::ReadOnly);

    // Three planes in, one plane out, and the caller still claiming three. The library says nothing
    // about what its channels mean, so a disagreement here is a failure rather than a relabelling.
    an::GmicResultIntent rgb = monoIntent();
    rgb.channelSchema = an::ChannelSchema::Rgb;
    const an::Result<an::RawFrame> mismatched = m_session.run(
        execution, an::GmicImageCall{.command = "channels 0", .source = picture.description, .intent = rgb});
    ASSERT_FALSE(mismatched.hasValue());
    EXPECT_EQ(an::gmicAdapterReason(mismatched.status()), an::GmicAdapterReason::ResultGeometryRejected);

    // The same command with the schema the caller actually gets back is accepted.
    an::Result<an::RawFrame> stated = m_session.run(
        execution, an::GmicImageCall{.command = "channels 0", .source = picture.description, .intent = monoIntent()});
    ASSERT_TRUE(stated.hasValue()) << stated.status().message();
    const std::optional<an::RawFrameDescriptor> descriptor = stated->descriptor();
    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->channelSchema(), an::ChannelSchema::Mono);
    EXPECT_EQ(descriptor->extent().channelCount(), 1u);
    EXPECT_EQ(descriptor->sampleFormat().sizeBytes, 4u);
    EXPECT_EQ(descriptor->numericDomain(), an::NumericDomain::NormalizedLinear);
    EXPECT_EQ(descriptor->coordinateOrigin(), an::CoordinateOrigin::TopLeft);
}

TEST_F(GmicSessionCase, ACommandThatResizesTheImageIsCarriedWithItsNewGeometry)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(8u, 8u, 1u, 0.5f, an::BufferMutability::ReadOnly);

    an::Result<an::RawFrame> ran = m_session.run(
        execution,
        an::GmicImageCall{.command = "resize 4,4,1,1,1", .source = picture.description, .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    const std::optional<an::ImageLayout> layout = ran->layout();
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->extent().width(), 4u);
    EXPECT_EQ(layout->extent().height(), 4u);
    EXPECT_EQ(layout->rowStrideBytes(), 4u * 4u) << "the library's buffer is packed, so the layout is";
    EXPECT_EQ(m_session.counters().libraryResultBytes, 4u * 4u * 4u);
}

TEST_F(GmicSessionCase, ACommandThatLeavesNoImageIsNamedRatherThanReturningAnEmptyFrame)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);

    const an::Result<an::RawFrame> ran = m_session.run(
        execution, an::GmicImageCall{.command = "rm", .source = picture.description, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::ResultMissing);
}

TEST_F(GmicSessionCase, ACommandThatLeavesSeveralImagesIsRefusedAndEveryBufferReleased)
{
    // The library's C bridge writes one structure per image a command produced into the array it was
    // given, which is sized for the images it was handed. Every caller that passes a one-element
    // array has a heap overflow here. This adapter reserves room for the overrun, refuses the call,
    // and frees what the library allocated.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 3u, 0.5f, an::BufferMutability::ReadOnly);

    const an::Result<an::RawFrame> ran = m_session.run(
        execution, an::GmicImageCall{.command = "split c", .source = picture.description, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::ResultImageCountUnexpected);

    Picture many = makePicture(65u, 1u, 1u, 0.5f, an::BufferMutability::ReadOnly);
    const an::Result<an::RawFrame> overCapacity = m_session.run(
        execution, an::GmicImageCall{.command = "split x", .source = many.description, .intent = monoIntent()});
    ASSERT_FALSE(overCapacity.hasValue());
    EXPECT_EQ(an::gmicAdapterReason(overCapacity.status()), an::GmicAdapterReason::ResultImageCountUnexpected);
}

TEST_F(GmicSessionCase, ANameTheLibraryWritesPastItsOwnFieldIsDetectedRatherThanTrusted)
{
    // The bridge copies an image's name back with `strcpy` and no bound, so a command that renames an
    // image past the 255 characters its structure declares writes into whatever follows. This adapter
    // puts its own slack there, so the write lands in memory it owns and the call is refused.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    const std::string longName(600, 'n');

    const an::Result<an::RawFrame> ran = m_session.run(
        execution,
        an::GmicImageCall{.command = "name " + longName, .source = picture.description, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::ResultNameOverflowed);
}

TEST_F(GmicSessionCase, ANameLongerThanAllTheRoomThisAdapterReservesIsRefusedBeforeItIsCopied)
{
    // Room behind the slots contains an overrun only as long as the overrun fits in it, and a command
    // can name an image at any length. Sixty-four kibibytes is longer than every slot and all the
    // slack behind them together, so a bridge that copied it would write past the memory this adapter
    // owns. The bridge is asked instead to refuse a name its field cannot hold before it copies back
    // any image at all, which bounds the write whatever the command names.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    const std::string longName(64u * 1024u, 'n');

    const an::Result<an::RawFrame> ran = m_session.run(
        execution,
        an::GmicImageCall{.command = "name " + longName, .source = picture.description, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::ResultNameOverflowed);
}

TEST_F(GmicSessionCase, AnImageInputFromValuesLongerThanItsTitleIsNamedInsideTheTitle)
{
    // The interpreter names an image it makes from values after those values, formatted into a 256-byte title. On
    // Microsoft's runtime the library's formatting bound the runtime's own '_vsnprintf', which leaves a truncated
    // string unterminated, so values longer than the title made a name that ran on into whatever followed it - which
    // the bridge then refused as a name its field cannot hold. The vendored copy formats through the terminating one.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    std::string values = "x";
    while (values.size() < 300u)
    {
        values += "+x*" + std::to_string(values.size() % 7u) + "+y/" + std::to_string(1u + values.size() % 5u);
    }

    const an::Result<an::RawFrame> ran =
        m_session.run(execution, an::GmicImageCall{.command = "rm 4,4,1,1,\"" + values + "\"",
                                                   .source = picture.description,
                                                   .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(ran->layout()->extent().width(), 4u);
}

TEST_F(GmicSessionCase, AnImagePassedIntoAForeachOverTheImageThatOwnsItsBufferIsACopy)
{
    // 'pass' in its default state shared the parent's buffer, and a 'foreach' that moved the image owning that buffer
    // into its own list freed it when the iteration ended, so the next iteration copied freed memory - the path of the
    // standard library's alpha splitting, which Multiscale Alpha Sharpen runs. The vendored copy shares only when the
    // shared state is asked for. A copy of freed memory usually reads as the old samples, so it is the address
    // configuration that sees this case fail without the change; here it holds the answer.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(16u, 16u, 1u, 0.25f, an::BufferMutability::ReadOnly);

    const an::Result<an::RawFrame> passed =
        m_session.run(execution, an::GmicImageCall{.command = "+b. 0.5 pass[0] foreach[^-1] { pass. } keep[-1]",
                                                   .source = picture.description,
                                                   .intent = monoIntent()});
    ASSERT_TRUE(passed.hasValue()) << passed.status().message();
    const std::span<const std::byte> bytes = passed->bytes();
    ASSERT_GE(bytes.size(), sizeof(float));
    float first = 0.0f;
    std::memcpy(&first, bytes.data(), sizeof(float));
    EXPECT_FLOAT_EQ(first, 0.25f) << "the image passed last is the source's copy";

    const an::Result<an::RawFrame> sharpened = m_session.run(
        execution,
        an::GmicImageCall{.command = "sharpen_alpha 2.5,5,0", .source = picture.description, .intent = monoIntent()});
    EXPECT_TRUE(sharpened.hasValue()) << sharpened.status().message();
}

TEST_F(GmicSessionCase, AStopRequestThatArrivedFirstIsObservedBeforeTheLibraryIsEntered)
{
    std::stop_source stopSource;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopSource.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    stopSource.request_stop();

    ASSERT_TRUE(execution->stopRequested());

    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    const an::Result<an::RawFrame> ran = m_session.run(
        *execution, an::GmicImageCall{.command = "add 1", .source = picture.description, .intent = monoIntent()});
    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::Interrupted);
    EXPECT_EQ(m_session.counters().cancellations, 1u);
    EXPECT_EQ(m_session.counters().calls, 0u) << "an operation already stopped does not need a library";
}

TEST_F(GmicSessionCase, ADeadlineThatHasAlreadyPassedIsObservedAtTheBoundary)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    const an::Status executed = m_session.execute(*execution, "v 0");
    // A deadline that ran out is told apart from a stop that was requested, because a caller does
    // something different about each; the adapter's own reason is the same for both.
    EXPECT_EQ(executed.code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(an::gmicAdapterReason(executed), an::GmicAdapterReason::Interrupted);
}

namespace
{

/// The fixture for the cases about a command that is already running.
class GmicBoundaryOnlyCase : public GmicSessionCase
{
protected:
    [[nodiscard]] an::GmicRuntimeOptions options() const override
    {
        an::GmicRuntimeOptions stated;
        stated.cancellation = an::GmicCancellationPolicy::AtBoundary;
        return stated;
    }
};

/// A command that keeps the interpreter busy long enough to be stopped, and is cheap per iteration.
///
/// The loop body sets the verbosity it already has, which is the least this interpreter can be asked
/// to do per iteration: the library has no command that does nothing at all. Roughly one and a half
/// three seconds on the host this was written on, so a stop that arrives after sixty milliseconds
/// arrives while it is running rather than after it - and a build that observed it only on the way
/// out would take those three seconds, which is what the cases below bound.
constexpr std::string_view kBusyCommand = "repeat 8000000 { v 0 }";

} // namespace

TEST_F(GmicSessionCase, AStopRequestReachesACommandThatHasAlreadyStarted)
{
    std::stop_source stopSource;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopSource.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::thread stopper([&stopSource] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        stopSource.request_stop();
    });
    const auto started = std::chrono::steady_clock::now();
    const an::Status executed = m_session.execute(*execution, kBusyCommand);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    stopper.join();

    EXPECT_EQ(executed.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::gmicAdapterReason(executed), an::GmicAdapterReason::Interrupted);
    EXPECT_GE(m_session.counters().cancellations, 1u);
    // The bound is what tells "the stop reached the running command" apart from "the command
    // finished and the stop was noticed on the way out", which report the same failure and are not
    // the same behaviour.
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000))
        << "the abort flag is what carries a stop into a command that has already started";
}

TEST_F(GmicBoundaryOnlyCase, TheBoundaryPolicyLetsARunningCommandFinishAndSaysSo)
{
    // The limit rather than a defect: nothing is armed, so a command that has started runs to the
    // end and the stop is observed when it returns. A caller that needs the other behaviour states
    // the other policy and pays for the watch.
    std::stop_source stopSource;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopSource.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::thread stopper([&stopSource] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        stopSource.request_stop();
    });
    const an::Status executed = m_session.execute(*execution, "repeat 200000 { v 0 }");
    stopper.join();

    EXPECT_EQ(executed.code(), an::ErrorCode::Cancelled) << "the boundary check on the way out";
    EXPECT_EQ(m_session.counters().calls, 1u) << "the command ran; only the report is a cancellation";
}

TEST_F(GmicSessionCase, ADeadlineIsWatchedWhileACommandRuns)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    const auto started = std::chrono::steady_clock::now();
    const an::Status executed = m_session.execute(*execution, kBusyCommand);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(executed.code(), an::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(an::gmicAdapterReason(executed), an::GmicAdapterReason::Interrupted);
    // The bound is what makes the watch load-bearing rather than decorative: the same command runs
    // for well over a second when nothing stops it, so a build whose watch never fires would report
    // the deadline on the way out instead of while the command was running, and take that long to.
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000)) << "the watch is what makes a deadline reach the library";
}

TEST_F(GmicSessionCase, ACommandStoppedWhileItRunsReleasesWhatTheLibraryLeftBehind)
{
    // The library reports success for a command it abandoned, and hands back the images it has -
    // which for a copied source is memory it allocated. This adapter reports the cancellation over
    // them and releases them, so what decides whether there is anything to free is the library's own
    // result code rather than the failure this adapter is about to return. Leak-freedom itself is the
    // sanitizer campaign's to prove; what this case pins is that the path runs and reports.
    std::stop_source stopSource;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopSource.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();
    Picture picture = makePicture(64u, 64u, 1u, 0.25f, an::BufferMutability::ReadOnly);

    std::thread stopper([&stopSource] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        stopSource.request_stop();
    });
    const an::Result<an::RawFrame> ran =
        m_session.run(*execution, an::GmicImageCall{.command = kBusyCommand,
                                                    .source = picture.description,
                                                    .sharing = an::GmicSourceSharing::Copy,
                                                    .intent = monoIntent()});
    stopper.join();

    ASSERT_FALSE(ran.hasValue());
    EXPECT_EQ(ran.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::gmicAdapterReason(ran.status()), an::GmicAdapterReason::Interrupted);
    const an::GmicCallCounters counters = m_session.counters();
    EXPECT_EQ(counters.calls, 1u);
    EXPECT_GE(counters.cancellations, 1u);
    EXPECT_EQ(counters.copiedIntoLibraryBytes, 0u) << "a call that produced nothing transferred nothing";
    EXPECT_EQ(counters.libraryResultBytes, 0u);
    for (const float sample : picture.samples)
    {
        EXPECT_FLOAT_EQ(sample, 0.25f) << "a copied source is untouched however the call ended";
    }
}

TEST_F(GmicSessionCase, ShuttingDownWhileACallIsRunningIsRefusedRatherThanRacedWith)
{
    std::stop_source stopSource;
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    options.stopToken = stopSource.get_token();
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    an::Status refused = an::Status::success();
    std::thread observer([this, &refused, &stopSource] {
        while (m_session.callsInFlight() == 0u)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        refused = m_session.shutdown();
        stopSource.request_stop();
    });
    const an::Status executed = m_session.execute(*execution, kBusyCommand);
    observer.join();

    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_EQ(an::gmicAdapterReason(refused), an::GmicAdapterReason::CallInFlight);
    EXPECT_TRUE(executed.failed()) << "the call this shutdown refused to interrupt is the one that reports";
    EXPECT_TRUE(m_session.valid()) << "a refused shutdown changes nothing";
}

TEST_F(GmicSessionCase, TheStandardLibraryIsAvailableOrNotAsTheSessionStated)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.5f, an::BufferMutability::ReadOnly);

    // A native command runs whatever the session said, because it is built into the interpreter.
    an::Result<an::GmicRuntimeAdapter> bare =
        an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{.useStandardLibrary = false});
    ASSERT_TRUE(bare.hasValue()) << bare.status().message();
    an::Result<an::RawFrame> native = bare->run(
        execution, an::GmicImageCall{.command = "add 1", .source = picture.description, .intent = monoIntent()});
    EXPECT_TRUE(native.hasValue()) << native.status().message();

    // One defined in the library's own command file does not, which is what the flag is for.
    const an::Result<an::RawFrame> defined = bare->run(
        execution,
        an::GmicImageCall{.command = "normalize_local 2,6", .source = picture.description, .intent = monoIntent()});
    EXPECT_FALSE(defined.hasValue()) << "a standard-library command needs the standard library";
    EXPECT_TRUE(bare->shutdown().ok());
}

TEST_F(GmicSessionCase, ACustomCommandBlockIsParsedBeforeEveryCommandThisSessionRuns)
{
    // A session's own command block, parsed once, rather than a `command <file>` prefixed to each
    // command.
    an::Result<an::GmicRuntimeAdapter> defined = an::GmicRuntimeAdapter::create(
        m_engine, an::GmicRuntimeOptions{.customCommands = "afternight_test_fill :\n  fill 0.125\n"});
    ASSERT_TRUE(defined.hasValue()) << defined.status().message();

    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);
    an::Result<an::RawFrame> ran = defined->run(
        execution,
        an::GmicImageCall{.command = "afternight_test_fill", .source = picture.description, .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (const float sample : resultSamples(*ran))
    {
        EXPECT_FLOAT_EQ(sample, 0.125f);
    }
    EXPECT_TRUE(defined->shutdown().ok());
}

TEST_F(GmicSessionCase, NothingIsClampedOnTheWayBack)
{
    // Clamping every result into [0, 1] before converting it would destroy whatever a filter
    // produced outside that range. This adapter returns what the library produced;
    // a range is one of the five decisions `Image/ImageConversion.h` makes, and it belongs to the
    // caller.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.75f, an::BufferMutability::ReadOnly);

    an::Result<an::RawFrame> ran = m_session.run(
        execution, an::GmicImageCall{.command = "add 1.5", .source = picture.description, .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    for (const float sample : resultSamples(*ran))
    {
        EXPECT_FLOAT_EQ(sample, 2.25f);
    }
}

TEST_F(GmicSessionCase, TheCountersAreThisSessionsAlone)
{
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(4u, 4u, 1u, 0.0f, an::BufferMutability::ReadOnly);

    an::Result<an::GmicRuntimeAdapter> second = an::GmicRuntimeAdapter::create(m_engine, an::GmicRuntimeOptions{});
    ASSERT_TRUE(second.hasValue()) << second.status().message();
    an::Result<an::RawFrame> ran = m_session.run(
        execution, an::GmicImageCall{.command = "add 1", .source = picture.description, .intent = monoIntent()});
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();

    EXPECT_EQ(m_session.counters().calls, 1u);
    EXPECT_EQ(second->counters().calls, 0u);
    EXPECT_TRUE(second->shutdown().ok());
}

TEST_F(GmicSessionCase, TwoThreadsCallingOneSessionAreSerialisedRatherThanRacing)
{
    // What is thread-safe here is the call, not the library: the library is entered one call at a
    // time across the whole process, and that is a cost stated rather than a race discovered.
    const an::ExecutionContext first = makeExecution(m_engine);
    const an::ExecutionContext second = makeExecution(m_engine);
    Picture one = makePicture(16u, 16u, 1u, 0.25f, an::BufferMutability::ReadOnly);
    Picture two = makePicture(16u, 16u, 1u, 0.5f, an::BufferMutability::ReadOnly);

    an::Status firstStatus = an::Status::success();
    std::thread worker([this, &first, &one, &firstStatus] {
        an::Result<an::RawFrame> ran = m_session.run(
            first, an::GmicImageCall{.command = "add 1", .source = one.description, .intent = monoIntent()});
        firstStatus = ran.status();
    });
    an::Result<an::RawFrame> ran =
        m_session.run(second, an::GmicImageCall{.command = "add 1", .source = two.description, .intent = monoIntent()});
    worker.join();

    EXPECT_TRUE(firstStatus.ok()) << firstStatus.message();
    ASSERT_TRUE(ran.hasValue()) << ran.status().message();
    EXPECT_EQ(m_session.counters().calls, 2u);
}

namespace
{

/// How much stack the caller in the case below is given: a quarter of a megabyte, which is a
/// quarter of what a Windows thread gets by default and far less than any filter in the library's
/// standard command file needs.
constexpr std::size_t kSmallCallerStackBytes = 256u * 1024u;

#if defined(_WIN32)
template <typename BodyT> unsigned __stdcall smallStackEntry(void* context)
{
    (*static_cast<BodyT*>(context))();
    return 0u;
}
#else
template <typename BodyT> void* smallStackEntry(void* context)
{
    (*static_cast<BodyT*>(context))();
    return nullptr;
}
#endif

/// Runs `body` on a thread whose stack is `kSmallCallerStackBytes`, and waits for it.
///
/// `std::thread` cannot state a stack size, so the case states one through the platform: what a
/// consumer's own worker thread, or a Windows executable linked with the default reserve, gives
/// the thread that calls this adapter.
///
/// @return false when the thread could not be started, in which case `body` did not run.
template <typename BodyT> [[nodiscard]] bool runOnSmallStack(BodyT& body)
{
#if defined(_WIN32)
    const std::uintptr_t handle =
        _beginthreadex(nullptr, static_cast<unsigned>(kSmallCallerStackBytes), &smallStackEntry<BodyT>, &body,
                       STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (handle == 0u)
    {
        return false;
    }
    const auto thread = reinterpret_cast<HANDLE>(handle);
    (void)WaitForSingleObject(thread, INFINITE);
    (void)CloseHandle(thread);
    return true;
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
    {
        return false;
    }
    pthread_t thread{};
    const bool started = pthread_attr_setstacksize(&attributes, kSmallCallerStackBytes) == 0 &&
                         pthread_create(&thread, &attributes, &smallStackEntry<BodyT>, &body) == 0;
    (void)pthread_attr_destroy(&attributes);
    if (started)
    {
        (void)pthread_join(thread, nullptr);
    }
    return started;
#endif
}

} // namespace

TEST_F(GmicSessionCase, ACallerOnASmallStackCanRunAFilterThatNeedsADeepOne)
{
    // The library's interpreter recurses once per nested command, up to 128 levels, and a filter
    // from its standard command file nests deeply: on MSVC this one overflows a two-megabyte stack.
    // A stack overflow is not a failure a caller can be told about - it ends the process - and a
    // Windows thread has one megabyte unless its executable says otherwise. So the stack a command
    // runs on is this adapter's to provide rather than every caller's to know about, and the case
    // calls from a thread with a quarter of a megabyte to prove it does not borrow the caller's.
    const an::ExecutionContext execution = makeExecution(m_engine);
    Picture picture = makePicture(64u, 64u, 1u, 0.25f, an::BufferMutability::ReadOnly);

    an::Status outcome = an::Status::failure(an::ErrorCode::Internal);
    auto call = [this, &execution, &picture, &outcome]() {
        an::Result<an::RawFrame> ran = m_session.run(execution, an::GmicImageCall{.command = "denoise_haar 0.1,3,1",
                                                                                  .source = picture.description,
                                                                                  .intent = monoIntent()});
        outcome = ran.status();
    };
    ASSERT_TRUE(runOnSmallStack(call)) << "the platform refused a thread the case needs";

    EXPECT_TRUE(outcome.ok()) << outcome.message();
    EXPECT_EQ(m_session.counters().calls, 1u);
}
