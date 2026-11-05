// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The runtime backend's loading contract, the full host's half of it: the module beside the host and found by name,
/// its five C exports at interface version 2, the probe, initialization and shutdown semantics in processes of their
/// own, the private session capsule the host borrows the session through, and what loading the module brings into a
/// process - the engine, and not Qt.

#include "RuntimeBackendTestSupport.h"
#include "RuntimeProcessModules.h"
#include "RuntimeSessionCapsule.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Version.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace an::tests::runtime_backend
{
namespace
{

void expectScenario(const std::string& name)
{
    const ScenarioRun run = runScenario(name);
    EXPECT_EQ(run.exitCode, 0) << "scenario '" << name << "' failed:\n" << run.output;
}

/// The capsule the embedded module answers, read as the full host reads it.
struct BorrowedSession
{
    py::object capsule;
    an::Result<std::shared_ptr<an::consumer::EngineSession>> session =
        an::Status::failure(an::ErrorCode::NotInitialized, "not borrowed");
};

[[nodiscard]] BorrowedSession borrowSession()
{
    BorrowedSession borrowed;
    borrowed.capsule = py::module_::import("_afternight_runtime").attr("_session_capsule")();
    const char* name = PyCapsule_GetName(borrowed.capsule.ptr());
    void* pointer = PyCapsule_GetPointer(borrowed.capsule.ptr(), name);
    borrowed.session = an::sdk::acceptRuntimeSessionCapsule(name, pointer);
    return borrowed;
}

TEST(RuntimeBackendExportsTest, TheModuleSitsBesideTheSuiteAndLoadsByName)
{
    ASSERT_TRUE(std::filesystem::exists(backendModulePath())) << backendModulePath().string();
    std::string error;
    EXPECT_NE(loadBackend(&error), nullptr) << error;
}

TEST(RuntimeBackendExportsTest, TheExportedInterfaceIsVersionTwoAndASuccessfulProbeLeavesNoError)
{
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    ASSERT_NE(exports, nullptr) << error;
    EXPECT_EQ(exports->abiVersion(), 2u);
    EXPECT_EQ(exports->probe(), 1) << exports->lastErrorText();
    ASSERT_NE(exports->lastError(), nullptr);
    EXPECT_STREQ(exports->lastError(), "");
}

TEST(RuntimeBackendExportsTest, TheModuleHasOriginRunpath)
{
#ifdef _WIN32
    GTEST_SKIP() << "a run path is an ELF property";
#else
    const std::string command = "readelf -d \"" + backendModulePath().string() + "\" 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        output += buffer;
    }
    pclose(pipe);
    EXPECT_NE(output.find("$ORIGIN"), std::string::npos) << output;
#endif
}

TEST(RuntimeBackendLifecycleTest, TheProbeBeforeInitializationLeavesNothingAndStartsNoInterpreter)
{
    expectScenario("probe-before-initialize");
}

TEST(RuntimeBackendLifecycleTest, InitializationIsIdempotentAndRepeatsAfterAShutdown)
{
    expectScenario("initialize-is-idempotent-and-repeatable");
}

TEST(RuntimeBackendLifecycleTest, AShutdownIsRefusedVisiblyWhilePythonHoldsTheSession)
{
    expectScenario("shutdown-is-refused-while-python-holds-the-session");
}

TEST(RuntimeBackendLifecycleTest, FinalizingTheInterpreterReleasesRetainedArraysAndCapsules)
{
    expectScenario("finalizing-the-interpreter-releases-retained-arrays-and-capsules");
}

TEST(RuntimeBackendLifecycleTest, AHostThatRefusesTheHandshakeStillShutsTheBackendDown)
{
    expectScenario("a-refused-handshake-leaves-nothing-behind");
}

TEST(RuntimeBackendLifecycleTest, AnUnknownScenarioIsRefused)
{
    EXPECT_EQ(runScenario("no-such-scenario").exitCode, 2);
}

TEST(RuntimeSessionCapsuleTest, TheCapsuleLendsTheSessionPythonImagesLiveIn)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    py::gil_scoped_acquire gil;
    BorrowedSession borrowed = borrowSession();
    ASSERT_TRUE(borrowed.session.hasValue()) << borrowed.session.status().message();
    const std::shared_ptr<an::consumer::EngineSession>& session = *borrowed.session;
    EXPECT_EQ(std::string(PyCapsule_GetName(borrowed.capsule.ptr())), an::sdk::kRuntimeSessionCapsuleName);
    EXPECT_EQ(session->state(), an::consumer::SessionState::Available);

    // What earlier cases left in reference cycles - a function holds the scope it was defined in - goes first, so
    // the count is of the images that are alive.
    py::module_::import("gc").attr("collect")();
    const std::uint64_t before = session->outstanding().assets;
    py::dict locals;
    runPython(R"PY(
import numpy as np
import _afternight_runtime as rt
held = rt.image_from_numpy(np.zeros((3, 5), np.float32))
)PY",
              locals);
    EXPECT_EQ(session->outstanding().assets, before + 1u) << "a Python image is an asset of the borrowed session";
    locals.clear();
    py::module_::import("gc").attr("collect")();
    EXPECT_EQ(session->outstanding().assets, before);
}

TEST(RuntimeSessionCapsuleTest, ACapsuleThatIsNotTheBackendsRecordIsRefused)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    py::gil_scoped_acquire gil;
    BorrowedSession borrowed = borrowSession();
    ASSERT_TRUE(borrowed.session.hasValue()) << borrowed.session.status().message();
    const auto* record = static_cast<const an::sdk::RuntimeSessionCapsule*>(
        PyCapsule_GetPointer(borrowed.capsule.ptr(), an::sdk::kRuntimeSessionCapsuleName));
    ASSERT_NE(record, nullptr);

    const auto otherName = an::sdk::acceptRuntimeSessionCapsule("afternight.other", record);
    ASSERT_FALSE(otherName.hasValue());
    EXPECT_EQ(otherName.status().code(), an::ErrorCode::IncompatibleArguments);
    EXPECT_FALSE(an::sdk::acceptRuntimeSessionCapsule(nullptr, record).hasValue());
    EXPECT_EQ(an::sdk::acceptRuntimeSessionCapsule(an::sdk::kRuntimeSessionCapsuleName, nullptr).status().code(),
              an::ErrorCode::NotInitialized);

    an::sdk::RuntimeSessionCapsule otherSize = *record;
    otherSize.recordBytes += 8u;
    EXPECT_EQ(an::sdk::acceptRuntimeSessionCapsule(an::sdk::kRuntimeSessionCapsuleName, &otherSize).status().code(),
              an::ErrorCode::IncompatibleArguments);
    an::sdk::RuntimeSessionCapsule otherRuntime = *record;
    otherRuntime.consumerRuntimeInterfaceVersion += 1u;
    const auto runtimeRefused =
        an::sdk::acceptRuntimeSessionCapsule(an::sdk::kRuntimeSessionCapsuleName, &otherRuntime);
    ASSERT_FALSE(runtimeRefused.hasValue());
    EXPECT_NE(std::string(runtimeRefused.status().message()).find("consumer runtime interface"), std::string::npos);
    an::sdk::RuntimeSessionCapsule otherEngine = *record;
    otherEngine.engineAbiGeneration += 1;
    const auto engineRefused = an::sdk::acceptRuntimeSessionCapsule(an::sdk::kRuntimeSessionCapsuleName, &otherEngine);
    ASSERT_FALSE(engineRefused.hasValue());
    EXPECT_NE(std::string(engineRefused.status().message()).find("engine interface generation"), std::string::npos);
    an::sdk::RuntimeSessionCapsule noSession = *record;
    noSession.session.reset();
    EXPECT_EQ(an::sdk::acceptRuntimeSessionCapsule(an::sdk::kRuntimeSessionCapsuleName, &noSession).status().code(),
              an::ErrorCode::NotInitialized);
}

TEST(RuntimeSessionCapsuleTest, NativeWorkOnTheBorrowedSessionRunsBesidePythonCalls)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    std::shared_ptr<an::consumer::EngineSession> session;
    {
        py::gil_scoped_acquire gil;
        BorrowedSession borrowed = borrowSession();
        ASSERT_TRUE(borrowed.session.hasValue()) << borrowed.session.status().message();
        session = *borrowed.session;
    }
    // A native worker admits operations of the session the whole time Python runs engine calls in it.
    std::atomic<bool> stop{false};
    std::atomic<int> admitted{0};
    std::atomic<int> refused{0};
    std::thread native([&] {
        while (!stop.load())
        {
            an::consumer::OperationRequest request;
            request.purpose = an::consumer::OperationPurpose::Batch;
            request.maxConcurrency = 1u;
            an::Result<an::consumer::OperationScope> scope = session->admit(request);
            (scope.hasValue() ? admitted : refused).fetch_add(1);
        }
    });
    // The worker is running before Python starts, so the two overlap however the threads are scheduled.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (admitted.load() + refused.load() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    {
        py::gil_scoped_acquire gil;
        py::dict locals;
        runPython(R"PY(
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.linspace(0.0, 1.0, 64 * 48, dtype=np.float32).reshape(48, 64))
for _ in range(20):
    converted = rt.convert_pixel_type(image, "uint16")
    assert converted.properties()["pixel_type"] == "uint16"
del image, converted
)PY",
                  locals);
    }
    stop.store(true);
    native.join();
    EXPECT_GT(admitted.load(), 0);
    EXPECT_EQ(refused.load(), 0);
}

TEST(RuntimeBackendClosureTest, AModuleNameIsReducedToTheEngineBehindIt)
{
    EXPECT_EQ(an::sdk::engineModuleOf("libAfterNightEngine"), an::sdk::EngineModule::Engine);
    EXPECT_EQ(an::sdk::engineModuleOf("AfterNightEngine_d"), an::sdk::EngineModule::Engine);
    EXPECT_EQ(an::sdk::engineModuleOf("AfterNightEngineGmicAdapter"), an::sdk::EngineModule::None);
    EXPECT_EQ(an::sdk::engineModuleOf("libpython3"), an::sdk::EngineModule::None);
}

TEST(RuntimeBackendClosureTest, TheProcessHoldsTheEngineAndNotQt)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    {
        py::gil_scoped_acquire gil;
        py::dict locals;
        // Every area the bindings reach is loaded by now: images, codecs, stars and the SDK's package.
        runPython(R"PY(
import os, tempfile
import numpy as np
import _afternight_runtime as rt
from afternight import core, io, registration, stacking, calibration
image = rt.image_from_numpy(np.full((32, 32), 0.1, np.float32))
rt.find_stars(image)
with tempfile.TemporaryDirectory() as root:
    rt.save_image(image, os.path.join(root, "closure.fits"))
)PY",
                  locals);
    }
    const std::vector<std::string> modules = an::sdk::mappedModuleNames();
    ASSERT_FALSE(modules.empty());
    bool engineMapped = false;
    for (const std::string& name : modules)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
        EXPECT_EQ(lower.find("qt6"), std::string::npos) << "Qt is mapped into the backend's process: " << name;
        engineMapped = engineMapped || an::sdk::engineModuleOf(name) == an::sdk::EngineModule::Engine;
    }
    EXPECT_TRUE(engineMapped) << "the engine is not mapped, so the absence above proves nothing";
}

} // namespace
} // namespace an::tests::runtime_backend
