// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The decode scenarios: what reading a real container through a real external library does when an
// allocation is refused at each of the points it has, and when a stop arrives at each of them - read
// through the engine context's own registry, and through a registry the read names while the engine
// context has none, which has to reach every point the ordinary read reaches.
//
// The file is written once, by an ordinary engine context with nothing injected, and every run of
// the sweep reads that same file. That is deliberate: a scenario that wrote its own input would
// spend its first injection points on the writing, and a decode that failed because its input was
// never produced proves nothing about decoding.

#include "FaultFixtures.h"
#include "FaultHarness.h"

#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace
{
using an::faults::FaultKind;
using an::faults::RunContext;
using an::faults::ScopedDirectory;
using an::faults::Sweep;

/// Writes the frame every decode scenario reads, through an ordinary engine context.
///
/// @param registry the codecs to write through.
/// @param destination where to write it.
/// @return success, or the failure that stopped it.
an::Status writeSourceFile(const std::shared_ptr<const an::CodecRegistry>& registry, const std::string& destination)
{
    an::Result<an::EngineContext> engine = an::EngineContext::create({.codecRegistry = registry});
    if (!engine.hasValue())
    {
        return engine.status();
    }
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(*engine, {});
    if (!execution.hasValue())
    {
        return execution.status();
    }
    an::Result<an::RawFrame> frame = an::faults::makeFrame(*engine);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const an::ImageMetadata metadata;
    return an::writeImage(*engine, *execution, {.destinationPath = destination}, *frame, metadata).status();
}

/// Decodes the file once, and destroys everything it built.
///
/// @param run what this run was given.
/// @param source the file to read.
/// @return the decode's own answer.
an::Status decodeOnce(RunContext& run, const std::string& source)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }
    an::Result<an::CodecDecodeResult> decoded = an::decodeImage(run.engine, *execution, {source});
    return an::faults::settle(decoded);
}

/// Decodes the file once through a registry the read names, and destroys everything it built.
///
/// @param run what this run was given; its engine context has no codecs of its own.
/// @param registry the codecs to read through.
/// @param source the file to read.
/// @return the decode's own answer.
an::Status decodeThroughOnce(RunContext& run, const an::CodecRegistry& registry, const std::string& source)
{
    an::ExecutionOptions policy;
    policy.stopToken = run.stop.get_token();
    policy.progressSink = &run.progress;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(run.engine, policy);
    if (!execution.hasValue())
    {
        return an::faults::settle(execution);
    }
    an::Result<an::CodecDecodeResult> decoded = an::decodeImage(run.engine, *execution, registry, {source});
    return an::faults::settle(decoded);
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfADecode)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    ScopedDirectory directory("an-fault-decode-allocation");
    const std::string source = directory.file("frame.fits");
    ASSERT_TRUE(writeSourceFile(*registry, source).ok());

    Sweep sweep("alloc.decode", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source](RunContext& run) { return decodeOnce(run, source); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfADecode)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    ScopedDirectory directory("an-fault-decode-cancellation");
    const std::string source = directory.file("frame.fits");
    ASSERT_TRUE(writeSourceFile(*registry, source).ok());

    Sweep sweep("cancel.decode", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u, .codecRegistry = *registry});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source](RunContext& run) { return decodeOnce(run, source); });
}

TEST(FaultCampaign, AnAllocationIsRefusedAtEveryPointOfADecodeThroughANamedRegistry)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    ScopedDirectory directory("an-fault-decode-registry-allocation");
    const std::string source = directory.file("frame.fits");
    ASSERT_TRUE(writeSourceFile(*registry, source).ok());

    Sweep sweep("alloc.decode.registry", FaultKind::Allocation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::AllocationFailed);
    sweep.accept(an::ErrorCode::MemoryBudgetExceeded);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source, &registry](RunContext& run) { return decodeThroughOnce(run, **registry, source); });
}

TEST(FaultCampaign, AStopIsRequestedAtEveryPointOfADecodeThroughANamedRegistry)
{
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::faults::fitsRegistry();
    ASSERT_TRUE(registry.hasValue()) << registry.status().message();
    ScopedDirectory directory("an-fault-decode-registry-cancellation");
    const std::string source = directory.file("frame.fits");
    ASSERT_TRUE(writeSourceFile(*registry, source).ok());

    Sweep sweep("cancel.decode.registry", FaultKind::Cancellation);
    sweep.engineOptions({.maxConcurrency = 2u});
    sweep.accept(an::ErrorCode::Cancelled);
    sweep.runBound(std::chrono::milliseconds{5000});
    sweep.run([&source, &registry](RunContext& run) { return decodeThroughOnce(run, **registry, source); });
}

} // namespace
