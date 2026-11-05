// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Contract tests for AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h.
//
// What is being tested is a lifetime, and the cases are shaped by the fact that the lifetime is
// process-wide whether this adapter likes it or not. CPython has one interpreter state per process,
// so there is exactly one of these objects at a time, and the interesting cases are the ones about
// the boundaries of that: a second adapter, somebody else's interpreter, a shutdown with a call
// still running, and a context that is asked to close underneath one.
//
// The cases that need a runtime say so. A build regenerated with AFTERNIGHT_PYTHON=0 contains this
// library with no interpreter in it, and the `RequirePython` helper skips there with a message
// instead of passing - a case that passed without running would be reporting that the refusal path
// works when it had not been taken. The refusal path has cases of its own, which run in that
// configuration and are the only ones that can.
//
// Finding a Python installation is the caller's job, not this adapter's, which is why these cases
// state a home rather than expecting one to be discovered: `AFTERNIGHT_TEST_PYTHON_HOME` names it,
// and the deployed `PythonLib` beside the test executable is the fallback, because that is where
// this repository's deploy step puts one.

#include <AfterNightEnginePythonAdapter/PythonRuntimeAdapter.h>

#include <AfterNightEnginePythonAdapter/PythonAdapterCapabilities.h>
#include <AfterNightEnginePythonAdapter/PythonBufferBridge.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Image/SampleType.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
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
///
/// One worker, because a call into Python runs on the calling thread: an execution that claimed the
/// whole budget would leave nothing for the next one, and several cases hold two.
[[nodiscard]] an::ExecutionContext makeExecution(const an::EngineContext& engine)
{
    an::ExecutionOptions options;
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> created = an::ExecutionContext::create(engine, options);
    EXPECT_TRUE(created.hasValue()) << created.status().message();
    return created.hasValue() ? std::move(*created) : an::ExecutionContext{};
}

/// Where a Python installation is on this host, or an empty string when the runtime resolves its own.
///
/// A deployment states its home, so these cases do too. The environment variable is how a host with
/// its Python somewhere else runs this suite without editing it.
[[nodiscard]] std::string pythonHome()
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t length = 0u;
    const auto copied = _dupenv_s(&value, &length, "AFTERNIGHT_TEST_PYTHON_HOME");
    const std::unique_ptr<char, decltype(&std::free)> stated(value, &std::free);
    if (copied == 0 && stated && *stated != '\0')
    {
        return stated.get();
    }
#else
    if (const char* stated = std::getenv("AFTERNIGHT_TEST_PYTHON_HOME"); stated != nullptr && *stated != '\0')
    {
        return stated;
    }
#endif
    std::error_code error;
    const std::filesystem::path deployed = std::filesystem::current_path(error) / "PythonLib";
    if (!error && std::filesystem::is_directory(deployed, error))
    {
        return deployed.string();
    }
    return std::string{};
}

/// A started adapter, or an empty optional with the case already skipped.
class PythonAdapterCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const an::PythonAdapterCapabilities capabilities = an::pythonAdapterCapabilities();
        if (!capabilities.runtimeCompiledIn)
        {
            GTEST_SKIP() << "this adapter was built with no Python runtime; the absent-path cases cover it";
        }
        if (capabilities.foreignInterpreter)
        {
            GTEST_SKIP() << "another interpreter is already running in this process";
        }
        // A host that has not been told where a Python installation is skips rather than reporting a
        // startup failure as though it were this adapter's: finding one is the caller's job, and a
        // suite run from a directory with no deployed PythonLib beside it has not been told.
        if (pythonHome().empty())
        {
            GTEST_SKIP() << "no Python installation is stated; set AFTERNIGHT_TEST_PYTHON_HOME or run "
                            "from a directory with a deployed PythonLib beside the executable";
        }
        m_engine = makeEngineContext();
        ASSERT_TRUE(m_engine.valid());
    }

    /// Starts an adapter with this host's Python home and the stated cancellation policy.
    [[nodiscard]] an::Result<an::PythonRuntimeAdapter>
    start(an::PythonCancellationPolicy policy = an::PythonCancellationPolicy::AtBoundary)
    {
        m_home = pythonHome();
        an::PythonRuntimeOptions options;
        options.pythonHome = m_home;
        options.cancellation = policy;
        return an::PythonRuntimeAdapter::create(m_engine, options);
    }

    /// Starts an adapter and fails the case when the runtime will not come up.
    [[nodiscard]] an::PythonRuntimeAdapter
    startOrFail(an::PythonCancellationPolicy policy = an::PythonCancellationPolicy::AtBoundary)
    {
        an::Result<an::PythonRuntimeAdapter> adapter = start(policy);
        EXPECT_TRUE(adapter.hasValue()) << adapter.status().message();
        return adapter.hasValue() ? std::move(*adapter) : an::PythonRuntimeAdapter{};
    }

    an::EngineContext m_engine;
    std::string m_home;
};

/// One image, its samples, and the description Python is handed for it.
class SharedImage
{
public:
    SharedImage(std::uint32_t width, std::uint32_t height, std::uint32_t planes, std::uint32_t samplesPerPixel,
                std::uint64_t rowStrideBytes)
        : m_samples(rowStrideBytes / sizeof(float) * height * planes, -1.0f)
    {
        const an::EngineLimits limits = an::defaultEngineLimits();
        const std::optional<an::ImageExtent> extent =
            an::ImageExtent::create(width, height, samplesPerPixel * planes, limits);
        EXPECT_TRUE(extent.has_value());
        const an::ImageLayoutRequest request{.extent = *extent,
                                             .elementSizeBytes = sizeof(float),
                                             .samplesPerPixel = samplesPerPixel,
                                             .planeCount = planes,
                                             .rowAlignmentBytes = 1u};
        const std::optional<an::ImageLayout> layout =
            an::ImageLayout::createWithStrides(request, rowStrideBytes, rowStrideBytes * height, limits);
        EXPECT_TRUE(layout.has_value());
        m_layout = *layout;
    }

    [[nodiscard]] an::PythonBufferDescription describe(an::BufferMutability mutability)
    {
        const an::ExternalBufferContract contract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                                  .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                  .ownership = an::BufferOwnership::EngineOwned,
                                                  .scope = an::BorrowScope::SingleCall,
                                                  .mutability = mutability};
        const std::optional<an::ImageView<float>> image =
            an::ImageView<float>::create(std::span<float>(m_samples), m_layout);
        EXPECT_TRUE(image.has_value());
        const std::optional<an::PythonBufferDescription> description = an::describeAsPythonBuffer(*image, contract);
        EXPECT_TRUE(description.has_value());
        return description.has_value() ? *description : an::PythonBufferDescription{};
    }

    [[nodiscard]] float& at(std::uint32_t plane, std::uint32_t y, std::uint32_t x, std::uint32_t sample = 0u)
    {
        const std::optional<std::uint64_t> offset = m_layout.sampleByteOffset(plane, y, x, sample);
        EXPECT_TRUE(offset.has_value());
        return m_samples[*offset / sizeof(float)];
    }

    [[nodiscard]] std::vector<float>& samples() noexcept
    {
        return m_samples;
    }

    [[nodiscard]] const an::ImageLayout& layout() const noexcept
    {
        return m_layout;
    }

private:
    std::vector<float> m_samples;
    an::ImageLayout m_layout{*an::ImageLayout::create(
        an::ImageLayoutRequest{.extent = *an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits()),
                               .elementSizeBytes = sizeof(float),
                               .samplesPerPixel = 1u,
                               .planeCount = 1u,
                               .rowAlignmentBytes = 1u},
        an::defaultEngineLimits())};
};

} // namespace

TEST(PythonRuntimeAdapterAbsentTest, ABuildWithNoRuntimeRefusesByNameRatherThanFailingToLoad)
{
    if (an::pythonAdapterCapabilities().runtimeCompiledIn)
    {
        GTEST_SKIP() << "this adapter was built with a Python runtime; the absent path is the other build";
    }

    an::EngineContext engine = makeEngineContext();
    const an::Result<an::PythonRuntimeAdapter> adapter =
        an::PythonRuntimeAdapter::create(engine, an::PythonRuntimeOptions{});

    ASSERT_FALSE(adapter.hasValue());
    EXPECT_EQ(adapter.status().code(), an::ErrorCode::Unsupported);
    EXPECT_EQ(an::pythonAdapterReason(adapter.status()), an::PythonAdapterReason::RuntimeNotCompiledIn);
    EXPECT_EQ(adapter.status().domain(), an::ErrorDomain::Interop);

    // And nothing was held: the engine context is quiescent and closes.
    EXPECT_TRUE(engine.quiescent());
    EXPECT_TRUE(engine.shutdown().ok());
}

TEST(PythonRuntimeAdapterAbsentTest, AnAdapterHoldingNothingRefusesEveryCall)
{
    an::EngineContext engine = makeEngineContext();
    const an::ExecutionContext execution = makeExecution(engine);
    an::PythonRuntimeAdapter adapter;

    EXPECT_FALSE(adapter.valid());
    EXPECT_EQ(adapter.engineContextId(), 0u);
    EXPECT_EQ(adapter.callsInFlight(), 0u);
    EXPECT_EQ(adapter.counters().calls, 0u);

    const an::Status executed = adapter.execute(execution, "value = 1\n");
    EXPECT_EQ(executed.code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::pythonAdapterReason(executed), an::PythonAdapterReason::AdapterShutDown);

    const an::Status called = adapter.callImageFunction(execution, an::PythonImageCall{});
    EXPECT_EQ(called.code(), an::ErrorCode::InvalidState);
    EXPECT_EQ(an::pythonAdapterReason(called), an::PythonAdapterReason::AdapterShutDown);

    // Shutting down what holds nothing is not an error to report.
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, StartingAnInterpreterReportsItLoadedWithItsOwnVersion)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    ASSERT_TRUE(adapter.valid());

    const an::PythonAdapterCapabilities capabilities = adapter.capabilities();
    EXPECT_EQ(capabilities.state, an::PythonAdapterState::Loaded);
    EXPECT_TRUE(capabilities.runtimeCompiledIn);
    EXPECT_EQ(capabilities.runtimeVersion.major, capabilities.buildVersion.major);
    EXPECT_EQ(capabilities.runtimeVersion.minor, capabilities.buildVersion.minor);
    EXPECT_EQ(capabilities.loadFailure, an::ErrorCode::Ok);

    // The free function agrees, because a consumer that has not been handed the adapter asks it.
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Loaded);
    EXPECT_EQ(adapter.engineContextId(), m_engine.id());

    EXPECT_TRUE(adapter.shutdown().ok());
    EXPECT_FALSE(adapter.valid());
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
}

TEST_F(PythonAdapterCase, OneProcessHoldsOneInterpreterAndTheSecondAdapterIsRefused)
{
    an::PythonRuntimeAdapter first = startOrFail();
    ASSERT_TRUE(first.valid());

    const an::Result<an::PythonRuntimeAdapter> second = start();
    ASSERT_FALSE(second.hasValue());
    EXPECT_EQ(second.status().code(), an::ErrorCode::AlreadyInitialized);
    EXPECT_EQ(an::pythonAdapterReason(second.status()), an::PythonAdapterReason::AdapterAlreadyActive);

    // The first adapter is untouched by the refusal, and after it goes the seat is free again.
    EXPECT_TRUE(first.valid());
    EXPECT_TRUE(first.shutdown().ok());
    an::Result<an::PythonRuntimeAdapter> third = start();
    ASSERT_TRUE(third.hasValue()) << third.status().message();
    EXPECT_TRUE(third->shutdown().ok());
}

TEST_F(PythonAdapterCase, AnEngineContextWithALiveAdapterRefusesToShutDown)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    ASSERT_TRUE(adapter.valid());

    EXPECT_FALSE(m_engine.quiescent());
    const an::Status refused = m_engine.shutdown();
    EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
    EXPECT_TRUE(m_engine.valid());

    EXPECT_TRUE(adapter.shutdown().ok());
    EXPECT_TRUE(m_engine.quiescent());
    EXPECT_TRUE(m_engine.shutdown().ok());
}

TEST_F(PythonAdapterCase, AnEngineContextHoldingNothingCannotStartAnAdapter)
{
    an::EngineContext empty;
    const std::string home = pythonHome();
    an::PythonRuntimeOptions options;
    options.pythonHome = home;

    const an::Result<an::PythonRuntimeAdapter> adapter = an::PythonRuntimeAdapter::create(empty, options);
    ASSERT_FALSE(adapter.hasValue());
    EXPECT_EQ(adapter.status().code(), an::ErrorCode::NotInitialized);
    EXPECT_TRUE(adapter.status().hasCause());
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
}

TEST_F(PythonAdapterCase, AnUnusableOptionIsRefusedBeforeAnythingIsStarted)
{
    const std::string home = pythonHome();
    an::PythonRuntimeOptions options;
    options.pythonHome = home;
    const std::string_view paths[] = {std::string_view{}};
    options.modulePaths = paths;

    const an::Result<an::PythonRuntimeAdapter> adapter = an::PythonRuntimeAdapter::create(m_engine, options);
    ASSERT_FALSE(adapter.hasValue());
    EXPECT_EQ(adapter.status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::pythonAdapterReason(adapter.status()), an::PythonAdapterReason::InterpreterConfigurationRejected);
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
    EXPECT_TRUE(m_engine.quiescent());
}

TEST_F(PythonAdapterCase, AHomeWithNoStandardLibraryIsReportedAsAStartupFailure)
{
    an::PythonRuntimeOptions options;
    options.pythonHome = "/nonexistent/afternight-python-home";

    an::Result<an::PythonRuntimeAdapter> adapter = an::PythonRuntimeAdapter::create(m_engine, options);
    if (adapter.hasValue())
    {
        // A host whose runtime finds a usable installation anyway is not a failure of this adapter,
        // and pretending otherwise would make the case depend on the machine.
        EXPECT_TRUE(adapter->shutdown().ok());
        GTEST_SKIP() << "this host's runtime resolved an installation without the stated home";
    }
    EXPECT_EQ(adapter.status().domain(), an::ErrorDomain::Interop);
    EXPECT_EQ(an::pythonAdapterReason(adapter.status()), an::PythonAdapterReason::InterpreterStartupFailed);

    // A refused start leaves the process startable and the engine context free.
    const an::PythonAdapterCapabilities capabilities = an::pythonAdapterCapabilities();
    EXPECT_EQ(capabilities.state, an::PythonAdapterState::FailedToLoad);
    EXPECT_EQ(capabilities.loadReason, an::PythonAdapterReason::InterpreterStartupFailed);
    EXPECT_TRUE(m_engine.quiescent());
}

TEST_F(PythonAdapterCase, SourceRunsInTheMainModuleAndWhatItDefinesIsCallableAfterwards)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);

    const an::Status defined = adapter.execute(execution, "def double(source, destination):\n"
                                                          "    for y in range(source.shape[0]):\n"
                                                          "        for x in range(source.shape[1]):\n"
                                                          "            destination[y, x] = source[y, x] * 2.0\n");
    ASSERT_TRUE(defined.ok()) << defined.message();

    SharedImage source(3u, 2u, 1u, 1u, 32u);
    SharedImage destination(3u, 2u, 1u, 1u, 32u);
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            source.at(0u, y, x) = static_cast<float>(y * 10u + x);
        }
    }

    const an::PythonImageCall call{.moduleName = "__main__",
                                   .functionName = "double",
                                   .source = source.describe(an::BufferMutability::ReadOnly),
                                   .destination = destination.describe(an::BufferMutability::ReadWrite)};
    const an::Status applied = adapter.callImageFunction(execution, call);
    ASSERT_TRUE(applied.ok()) << applied.message();

    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            EXPECT_FLOAT_EQ(destination.at(0u, y, x), static_cast<float>(y * 10u + x) * 2.0f);
        }
    }

    // The padding between rows is not part of the image, and nothing wrote through it.
    EXPECT_FLOAT_EQ(destination.samples()[3u], -1.0f);
    EXPECT_FLOAT_EQ(destination.samples()[7u], -1.0f);

    const an::PythonCallCounters counters = adapter.counters();
    EXPECT_EQ(counters.calls, 2u);
    EXPECT_EQ(counters.copiedBytes, 0u);
    EXPECT_EQ(counters.sharedBytes, 2u * 3u * 2u * sizeof(float));
    EXPECT_EQ(counters.bridgeRefusals, 0u);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, TheAxisConventionReachesPythonAsTheArrangementStatesIt)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);

    const an::Status defined =
        adapter.execute(execution, "def report(source, destination):\n"
                                   "    destination[0, 0] = float(source.ndim)\n"
                                   "    destination[0, 1] = float(source.shape[0])\n"
                                   "    destination[0, 2] = float(source.shape[1])\n"
                                   "    destination[0, 3] = float(source.shape[2] if source.ndim > 2 else 0)\n"
                                   "    destination[0, 4] = float(source.strides[0])\n"
                                   "    destination[0, 5] = 1.0 if source.readonly else 0.0\n");
    ASSERT_TRUE(defined.ok()) << defined.message();

    SharedImage report(6u, 1u, 1u, 1u, 24u);
    const auto describe = [&adapter, &execution, &report](SharedImage& image) {
        const an::PythonImageCall call{.moduleName = "__main__",
                                       .functionName = "report",
                                       .source = image.describe(an::BufferMutability::ReadOnly),
                                       .destination = report.describe(an::BufferMutability::ReadWrite)};
        return adapter.callImageFunction(execution, call);
    };

    SharedImage planar(4u, 3u, 3u, 1u, 16u);
    ASSERT_TRUE(describe(planar).ok());
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 0u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 1u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 2u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 3u), 4.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 4u), static_cast<float>(planar.layout().planeStrideBytes()));
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 5u), 1.0f);

    SharedImage interleaved(4u, 3u, 1u, 3u, 64u);
    ASSERT_TRUE(describe(interleaved).ok());
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 0u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 1u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 2u), 4.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 3u), 3.0f);
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 4u), 64.0f);

    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, WritingThroughTheDestinationIsWritingIntoTheEnginesOwnSamples)
{
    // The property that makes this a bridge rather than a marshaller: no copy in either direction,
    // so what Python wrote is already in the buffer the caller owns when the call returns.
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);
    ASSERT_TRUE(adapter
                    .execute(execution, "def fill(source, destination):\n"
                                        "    destination[0, 0] = 7.5\n"
                                        "    if destination.readonly:\n"
                                        "        raise AssertionError('a writable borrow arrived read-only')\n")
                    .ok());

    SharedImage image(2u, 1u, 1u, 1u, 8u);
    const an::PythonImageCall call{.moduleName = "__main__",
                                   .functionName = "fill",
                                   .source = image.describe(an::BufferMutability::ReadOnly),
                                   .destination = image.describe(an::BufferMutability::ReadWrite)};
    ASSERT_TRUE(adapter.callImageFunction(execution, call).ok());
    EXPECT_FLOAT_EQ(image.samples()[0], 7.5f);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, LentMemoryViewsAreReleasedWhenTheCallReturns)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);
    ASSERT_TRUE(adapter
                    .execute(execution, "retained = []\n"
                                        "def retain(source, destination):\n"
                                        "    retained[:] = [source, destination]\n")
                    .ok());

    SharedImage image(2u, 1u, 1u, 1u, 8u);
    const an::PythonImageCall call{.moduleName = "__main__",
                                   .functionName = "retain",
                                   .source = image.describe(an::BufferMutability::ReadOnly),
                                   .destination = image.describe(an::BufferMutability::ReadWrite)};
    ASSERT_TRUE(adapter.callImageFunction(execution, call).ok());
    const an::Status checked = adapter.execute(execution, "released = 0\n"
                                                          "for view in retained:\n"
                                                          "    try:\n"
                                                          "        view.tobytes()\n"
                                                          "    except ValueError:\n"
                                                          "        released += 1\n"
                                                          "assert released == 2\n");
    EXPECT_TRUE(checked.ok()) << checked.message();
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, APythonExceptionBecomesAFailureCarryingItsClassAndMessage)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);

    const an::Status raised = adapter.execute(execution, "raise ValueError('the frame is not square')\n");
    ASSERT_TRUE(raised.failed());
    EXPECT_EQ(raised.code(), an::ErrorCode::ExternalFailure);
    EXPECT_EQ(an::pythonAdapterReason(raised), an::PythonAdapterReason::PythonExceptionRaised);
    EXPECT_EQ(raised.domain(), an::ErrorDomain::Interop);
    EXPECT_NE(std::string_view{raised.message()}.find("ValueError"), std::string_view::npos) << raised.message();
    EXPECT_NE(std::string_view{raised.message()}.find("the frame is not square"), std::string_view::npos)
        << raised.message();

    // A syntax error is the same kind of answer, and the interpreter is usable afterwards.
    const an::Status malformed = adapter.execute(execution, "def broken(:\n");
    EXPECT_EQ(an::pythonAdapterReason(malformed), an::PythonAdapterReason::PythonExceptionRaised);

    const an::Status unencodable = adapter.execute(execution, "raise ValueError('\\udc80')\n");
    EXPECT_EQ(an::pythonAdapterReason(unencodable), an::PythonAdapterReason::PythonExceptionRaised);
    EXPECT_NE(std::string_view{unencodable.message()}.find("ValueError"), std::string_view::npos);
    EXPECT_TRUE(adapter.execute(execution, "recovered = True\n").ok());
    EXPECT_EQ(adapter.counters().pythonExceptions, 3u);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, EveryWayACallCanNameSomethingUncallableIsItsOwnReason)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);
    ASSERT_TRUE(adapter
                    .execute(execution, "notAFunction = 12\n"
                                        "def returnsSomething(source, destination):\n"
                                        "    return 5\n")
                    .ok());

    SharedImage image(2u, 1u, 1u, 1u, 8u);
    const auto callNamed = [&](std::string_view module, std::string_view function) {
        const an::PythonImageCall call{.moduleName = module,
                                       .functionName = function,
                                       .source = image.describe(an::BufferMutability::ReadOnly),
                                       .destination = image.describe(an::BufferMutability::ReadWrite)};
        return adapter.callImageFunction(execution, call);
    };

    EXPECT_EQ(an::pythonAdapterReason(callNamed("afternight_not_a_module", "anything")),
              an::PythonAdapterReason::ModuleNotFound);
    EXPECT_EQ(an::pythonAdapterReason(callNamed("__main__", "notThere")), an::PythonAdapterReason::AttributeNotFound);
    EXPECT_EQ(an::pythonAdapterReason(callNamed("__main__", "notAFunction")), an::PythonAdapterReason::NotCallable);
    EXPECT_EQ(an::pythonAdapterReason(callNamed("__main__", "returnsSomething")),
              an::PythonAdapterReason::UnexpectedResult);

    ASSERT_TRUE(adapter
                    .execute(execution, "import sys\n"
                                        "class BrokenImport:\n"
                                        "    def find_spec(self, fullname, path, target=None):\n"
                                        "        if fullname == 'broken_import':\n"
                                        "            raise ImportError('dependency initialization failed')\n"
                                        "        return None\n"
                                        "sys.meta_path.insert(0, BrokenImport())\n")
                    .ok());
    const an::Status brokenImport = callNamed("broken_import", "anything");
    EXPECT_EQ(an::pythonAdapterReason(brokenImport), an::PythonAdapterReason::PythonExceptionRaised);
    EXPECT_NE(std::string_view{brokenImport.message()}.find("dependency initialization failed"),
              std::string_view::npos);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, ABridgeRefusalIsReportedBeforeTheInterpreterIsEntered)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext execution = makeExecution(m_engine);
    ASSERT_TRUE(adapter
                    .execute(execution, "calls = 0\n"
                                        "def counted(source, destination):\n"
                                        "    global calls\n"
                                        "    calls += 1\n")
                    .ok());

    SharedImage image(2u, 1u, 1u, 1u, 8u);
    an::PythonImageCall call{.moduleName = "__main__",
                             .functionName = "counted",
                             .source = image.describe(an::BufferMutability::ReadOnly),
                             .destination = image.describe(an::BufferMutability::ReadWrite)};
    call.destination.readOnly = true;

    const an::Status refused = adapter.callImageFunction(execution, call);
    EXPECT_EQ(refused.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::pythonAdapterReason(refused), an::PythonAdapterReason::BufferPermissionMismatch);
    EXPECT_EQ(adapter.counters().bridgeRefusals, 1u);

    // And the bridge's own alphabet answers the other kind: a description this adapter cannot lend at
    // all, rather than one whose permission is wrong.
    an::PythonImageCall strided = call;
    strided.destination.readOnly = false;
    strided.source.strides[1] = strided.source.itemSizeBytes * 2;
    const an::Status unshareable = adapter.callImageFunction(execution, strided);
    EXPECT_EQ(unshareable.code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::pythonAdapterReason(unshareable), an::PythonAdapterReason::BufferBridgeRefused);
    EXPECT_EQ(adapter.counters().bridgeRefusals, 2u);

    // Nothing reached Python: the call count it keeps for itself is still zero.
    ASSERT_TRUE(adapter
                    .execute(execution, "def reportCalls(source, destination):\n"
                                        "    destination[0, 0] = float(calls)\n")
                    .ok());
    SharedImage report(1u, 1u, 1u, 1u, 4u);
    const an::PythonImageCall readBack{.moduleName = "__main__",
                                       .functionName = "reportCalls",
                                       .source = image.describe(an::BufferMutability::ReadOnly),
                                       .destination = report.describe(an::BufferMutability::ReadWrite)};
    ASSERT_TRUE(adapter.callImageFunction(execution, readBack).ok());
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 0u), 0.0f);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, ACancellationAlreadyRequestedIsObservedBeforeTheInterpreterIsEntered)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const an::ExecutionContext plain = makeExecution(m_engine);
    ASSERT_TRUE(adapter.execute(plain, "ran = False\n").ok());

    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> stoppable = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(stoppable.hasValue()) << stoppable.status().message();
    source.request_stop();

    const an::Status cancelled = adapter.execute(*stoppable, "ran = True\n");
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_TRUE(cancelled.isCancellation());
    EXPECT_EQ(an::pythonAdapterReason(cancelled), an::PythonAdapterReason::PythonInterrupted);
    EXPECT_EQ(adapter.counters().cancellations, 1u);

    SharedImage report(1u, 1u, 1u, 1u, 4u);
    ASSERT_TRUE(adapter
                    .execute(plain, "def reportRan(source, destination):\n"
                                    "    destination[0, 0] = 1.0 if ran else 0.0\n")
                    .ok());
    SharedImage input(1u, 1u, 1u, 1u, 4u);
    const an::PythonImageCall call{.moduleName = "__main__",
                                   .functionName = "reportRan",
                                   .source = input.describe(an::BufferMutability::ReadOnly),
                                   .destination = report.describe(an::BufferMutability::ReadWrite)};
    ASSERT_TRUE(adapter.callImageFunction(plain, call).ok());
    EXPECT_FLOAT_EQ(report.at(0u, 0u, 0u), 0.0f);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, TheBoundaryPolicyLetsCodeFinishButReportsCancellationAtExit)
{
    // The boundary policy does not inject into Python, but a cancellation that arrived while Python
    // ran still decides the call's result at the exit boundary.
    an::PythonRuntimeAdapter adapter = startOrFail(an::PythonCancellationPolicy::AtBoundary);
    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::jthread stopper([&source] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source.request_stop();
    });

    const an::Status cancelled = adapter.execute(*execution, "import time\n"
                                                             "time.sleep(0.1)\n"
                                                             "finished = True\n");
    stopper.join();
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::pythonAdapterReason(cancelled), an::PythonAdapterReason::PythonInterrupted);
    EXPECT_EQ(adapter.counters().cancellations, 1u);
    const an::ExecutionContext plain = makeExecution(m_engine);
    EXPECT_TRUE(adapter.execute(plain, "assert finished\n").ok());
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, ThePerLinePolicyStopsALoopAndLeavesTheInterpreterUsable)
{
    an::PythonRuntimeAdapter adapter = startOrFail(an::PythonCancellationPolicy::PerLine);
    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::jthread stopper([&source] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source.request_stop();
    });

    const auto started = std::chrono::steady_clock::now();
    const an::Status cancelled = adapter.execute(*execution, "total = 0\n"
                                                             "for index in range(40000000):\n"
                                                             "    total += index\n");
    const auto elapsed = std::chrono::steady_clock::now() - started;
    stopper.join();

    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::pythonAdapterReason(cancelled), an::PythonAdapterReason::PythonInterrupted);
    EXPECT_TRUE(cancelled.isCancellation());
    EXPECT_LT(elapsed, std::chrono::seconds(20));
    EXPECT_EQ(adapter.counters().cancellations, 1u);

    // The exception the check raised was taken out of the interpreter, not left in it.
    const an::ExecutionContext plain = makeExecution(m_engine);
    EXPECT_TRUE(adapter.execute(plain, "recovered = True\n").ok());
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, CatchingTheInjectedKeyboardInterruptCannotTurnCancellationIntoSuccess)
{
    an::PythonRuntimeAdapter adapter = startOrFail(an::PythonCancellationPolicy::PerLine);
    std::stop_source source;
    an::ExecutionOptions options;
    options.stopToken = source.get_token();
    options.maxConcurrency = 1u;
    an::Result<an::ExecutionContext> execution = an::ExecutionContext::create(m_engine, options);
    ASSERT_TRUE(execution.hasValue()) << execution.status().message();

    std::jthread stopper([&source] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source.request_stop();
    });
    const an::Status cancelled = adapter.execute(*execution, "caught = False\n"
                                                             "try:\n"
                                                             "    for index in range(40000000):\n"
                                                             "        value = index\n"
                                                             "except KeyboardInterrupt:\n"
                                                             "    caught = True\n");
    stopper.join();
    EXPECT_EQ(cancelled.code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(an::pythonAdapterReason(cancelled), an::PythonAdapterReason::PythonInterrupted);
    EXPECT_EQ(adapter.counters().cancellations, 1u);
    const an::ExecutionContext plain = makeExecution(m_engine);
    EXPECT_TRUE(adapter.execute(plain, "assert caught\n").ok());
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, TwoThreadsMayCallOneAdapterAtOnce)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    constexpr int kCallsPerThread = 40;

    const auto body = [&adapter, this] {
        const an::ExecutionContext execution = makeExecution(m_engine);
        for (int index = 0; index < kCallsPerThread; ++index)
        {
            EXPECT_TRUE(adapter.execute(execution, "total = sum(range(50))\n").ok());
        }
    };

    std::thread first(body);
    std::thread second(body);
    first.join();
    second.join();

    EXPECT_EQ(adapter.counters().calls, static_cast<std::uint64_t>(2 * kCallsPerThread));
    EXPECT_EQ(adapter.callsInFlight(), 0u);
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, ShuttingDownWhileACallIsRunningIsRefusedRatherThanDone)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    std::atomic<bool> running{false};

    std::thread caller([&adapter, &running, this] {
        const an::ExecutionContext execution = makeExecution(m_engine);
        running.store(true);
        EXPECT_TRUE(adapter
                        .execute(execution, "total = 0\n"
                                            "for index in range(2000000):\n"
                                            "    total += index\n")
                        .ok());
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (adapter.callsInFlight() == 0u && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    if (adapter.callsInFlight() > 0u)
    {
        const an::Status refused = adapter.shutdown();
        EXPECT_EQ(refused.code(), an::ErrorCode::Busy);
        EXPECT_EQ(an::pythonAdapterReason(refused), an::PythonAdapterReason::CallInFlight);
        EXPECT_TRUE(adapter.valid());
    }
    caller.join();
    EXPECT_TRUE(running.load());
    EXPECT_TRUE(adapter.shutdown().ok());
}

TEST_F(PythonAdapterCase, MovingAnAdapterMovesTheInterpreterAndNotASecondHold)
{
    an::PythonRuntimeAdapter adapter = startOrFail();
    const std::uint64_t contextId = adapter.engineContextId();
    const std::uint64_t leases = m_engine.outstandingLeases();

    an::PythonRuntimeAdapter moved = std::move(adapter);
    EXPECT_FALSE(adapter.valid());
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.engineContextId(), contextId);
    EXPECT_EQ(m_engine.outstandingLeases(), leases);

    const an::ExecutionContext execution = makeExecution(m_engine);
    EXPECT_TRUE(moved.execute(execution, "afterMove = True\n").ok());
    EXPECT_TRUE(moved.shutdown().ok());
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
}

TEST_F(PythonAdapterCase, ADestroyedAdapterFinalisesTheInterpreterAndFreesTheSeat)
{
    {
        an::PythonRuntimeAdapter adapter = startOrFail();
        ASSERT_TRUE(adapter.valid());
        EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Loaded);
    }

    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
    EXPECT_TRUE(m_engine.quiescent());
    an::Result<an::PythonRuntimeAdapter> again = start();
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_TRUE(again->shutdown().ok());
}

TEST_F(PythonAdapterCase, ALateReleaseFinalisesOnItsOwnerWhileCapabilitiesAreReadConcurrently)
{
    auto adapter = std::make_unique<an::PythonRuntimeAdapter>(startOrFail());
    an::PythonRuntimeAdapter* const borrowed = adapter.get();
    std::atomic<bool> finished{false};
    an::Status call;
    std::thread caller([&] {
        const an::ExecutionContext execution = makeExecution(m_engine);
        call = borrowed->execute(execution, "import time\ntime.sleep(0.1)\n");
        finished.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (borrowed->callsInFlight() == 0u && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    if (borrowed->callsInFlight() == 0u)
    {
        caller.join();
        FAIL() << "the Python call finished before its lifetime could be tested";
    }

    std::atomic<bool> capabilityMismatch{false};
    std::thread observer([&] {
        while (!finished.load(std::memory_order_acquire))
        {
            const an::PythonAdapterCapabilities capabilities = an::pythonAdapterCapabilities();
            if (capabilities.state == an::PythonAdapterState::Loaded)
                capabilityMismatch.store(capabilities.runtimeVersion.major != capabilities.buildVersion.major,
                                         std::memory_order_relaxed);
        }
    });
    adapter.reset();
    caller.join();
    observer.join();

    EXPECT_TRUE(call.ok()) << call.message();
    EXPECT_FALSE(capabilityMismatch.load(std::memory_order_relaxed));
    EXPECT_EQ(an::pythonAdapterCapabilities().state, an::PythonAdapterState::Available);
    an::Result<an::PythonRuntimeAdapter> again = start();
    ASSERT_TRUE(again.hasValue()) << again.status().message();
    EXPECT_TRUE(again->shutdown().ok());
}

TEST_F(PythonAdapterCase, ModulePathsAreAppendedToTheSearchPathInOrder)
{
    const std::string_view paths[] = {"/afternight/first", "/afternight/second"};
    an::PythonRuntimeOptions options;
    m_home = pythonHome();
    options.pythonHome = m_home;
    options.modulePaths = paths;

    an::Result<an::PythonRuntimeAdapter> adapter = an::PythonRuntimeAdapter::create(m_engine, options);
    ASSERT_TRUE(adapter.hasValue()) << adapter.status().message();

    const an::ExecutionContext execution = makeExecution(m_engine);
    ASSERT_TRUE(adapter
                    ->execute(execution, "import sys\n"
                                         "first = sys.path.index('/afternight/first')\n"
                                         "second = sys.path.index('/afternight/second')\n"
                                         "if second != first + 1:\n"
                                         "    raise AssertionError('the stated order was not kept')\n")
                    .ok());
    EXPECT_TRUE(adapter->shutdown().ok());
}
