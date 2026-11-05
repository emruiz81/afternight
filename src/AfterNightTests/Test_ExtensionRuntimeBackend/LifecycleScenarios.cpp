// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The lifecycle scenarios: each starts and ends the backend's session, or the interpreter, the way the full host does,
/// in a process of its own, and answers through its exit code. `RuntimeBackendLifecycleTest` runs each.
///
/// What they hold the backend to, as the full host relies on it: the probe and the initialization start no interpreter;
/// the probe before initialization leaves nothing behind; initialization is idempotent and can follow a shutdown; a
/// shutdown is refused - visibly, and with the session still working - while Python holds an image, an array or the
/// session capsule, and succeeds once the host has finalized its interpreter; a host that refuses the session capsule
/// can still take the backend down cleanly; and a process that already holds the engine the backend replaces is refused
/// before anything starts.

#include "RuntimeBackendTestSupport.h"
#include "RuntimeSessionCapsule.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace an::tests::runtime_backend
{

namespace
{

/// The checks of one scenario, each failure printed as it is found.
class Checks
{
public:
    void expect(const bool held, const std::string& what)
    {
        if (!held)
        {
            std::fprintf(stderr, "FAILED: %s\n", what.c_str());
            m_failed = true;
        }
    }

    [[nodiscard]] int exitCode() const noexcept
    {
        return m_failed ? 1 : 0;
    }

private:
    bool m_failed = false;
};

/// Runs Python source in the interpreter's main namespace and says whether it raised.
[[nodiscard]] bool python(const char* source, std::string* error = nullptr)
{
    py::gil_scoped_acquire gil;
    try
    {
        py::exec(source, py::globals());
        return true;
    }
    catch (const std::exception& failure)
    {
        if (error != nullptr)
        {
            *error = failure.what();
        }
        return false;
    }
}

[[nodiscard]] int probeBeforeInitialize()
{
    Checks checks;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    checks.expect(exports != nullptr, "the backend loads: " + error);
    if (exports == nullptr)
    {
        return checks.exitCode();
    }
    checks.expect(Py_IsInitialized() == 0, "no interpreter runs before the probe");
    for (int round = 0; round < 3; ++round)
    {
        checks.expect(exports->probe() == 1, "the probe before initialization succeeds: " + exports->lastErrorText());
        checks.expect(exports->lastErrorText().empty(), "a successful probe leaves no error");
    }
    checks.expect(Py_IsInitialized() == 0, "the probe starts no interpreter");
    // The temporary context the probe built is gone, so a session can be opened and closed after it.
    checks.expect(exports->initialize() == 1, "initialization after a probe succeeds: " + exports->lastErrorText());
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(), "the shutdown after a probe leaves no error");
    return checks.exitCode();
}

[[nodiscard]] int initializeIsIdempotentAndRepeatable()
{
    Checks checks;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    checks.expect(exports != nullptr, "the backend loads: " + error);
    if (exports == nullptr)
    {
        return checks.exitCode();
    }
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(), "a shutdown before any initialization does nothing");
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        checks.expect(exports->initialize() == 1, "initialization succeeds: " + exports->lastErrorText());
        checks.expect(exports->initialize() == 1, "a second initialization succeeds and changes nothing");
        checks.expect(exports->lastErrorText().empty(), "an idempotent initialization leaves no error");
        checks.expect(exports->probe() == 1, "the probe after initialization succeeds: " + exports->lastErrorText());
        checks.expect(Py_IsInitialized() == 0, "initialization starts no interpreter");
        exports->shutdown();
        checks.expect(exports->lastErrorText().empty(),
                      "a shutdown with nothing held succeeds: " + exports->lastErrorText());
        exports->shutdown();
        checks.expect(exports->lastErrorText().empty(), "a second shutdown does nothing");
    }
    return checks.exitCode();
}

[[nodiscard]] int shutdownIsRefusedWhilePythonHoldsTheSession()
{
    Checks checks;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    checks.expect(exports != nullptr && exports->initialize() == 1, "the backend initializes: " + error);
    checks.expect(startInterpreter(&error), "the interpreter starts: " + error);
    if (exports == nullptr || !Py_IsInitialized())
    {
        return checks.exitCode();
    }
    checks.expect(python(R"PY(
import gc
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.zeros((4, 4), np.float32))
array = image.to_numpy()
capsule = rt._session_capsule()
)PY",
                         &error),
                  "Python makes an image, an array and a capsule: " + error);

    exports->shutdown();
    const std::string refused = exports->lastErrorText();
    checks.expect(!refused.empty(), "the shutdown is refused while Python holds the session");
    checks.expect(refused.find("capsule") != std::string::npos && refused.find("asset") != std::string::npos,
                  "the refusal names what holds the session: " + refused);
    checks.expect(
        python("assert float(rt.image_from_numpy(np.ones((2, 2), np.float32)).to_numpy().sum()) == 4.0\n", &error),
        "the refused backend keeps its session working: " + error);

    checks.expect(python("del image, array, capsule\ngc.collect()\n", &error), "Python lets everything go: " + error);
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(),
                  "the shutdown succeeds once nothing holds the session: " + exports->lastErrorText());
    checks.expect(python(R"PY(
try:
    rt.image_from_numpy(np.zeros((2, 2), np.float32))
except RuntimeError as error:
    assert "not initialized" in str(error), str(error)
else:
    raise AssertionError("a binding ran with no session")
)PY",
                         &error),
                  "a binding without a session is refused by name: " + error);
    checks.expect(exports->initialize() == 1, "the backend initializes again under the same interpreter");
    checks.expect(python("assert rt.image_from_numpy(np.ones((3, 2), np.uint16)).properties()['width'] == 2\n", &error),
                  "the bindings run in the new session: " + error);
    checks.expect(python("gc.collect()\n", &error), "Python collects: " + error);
    finalizeInterpreter();
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(),
                  "the backend shuts down after the interpreter: " + exports->lastErrorText());
    return checks.exitCode();
}

[[nodiscard]] int finalizingTheInterpreterReleasesRetainedArraysAndCapsules()
{
    Checks checks;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    checks.expect(exports != nullptr && exports->initialize() == 1, "the backend initializes: " + error);
    checks.expect(startInterpreter(&error), "the interpreter starts: " + error);
    if (exports == nullptr || !Py_IsInitialized())
    {
        return checks.exitCode();
    }
    checks.expect(python(R"PY(
import numpy as np
import _afternight_runtime as rt
image = rt.image_from_numpy(np.arange(24, dtype=np.float32).reshape(4, 6))
array = image.to_numpy()
slice_of_it = array[1:3, ::2]
del image
capsule = rt._session_capsule()
)PY",
                         &error),
                  "Python keeps an array, a slice of it and the capsule, and drops the image: " + error);
    exports->shutdown();
    checks.expect(!exports->lastErrorText().empty(), "the shutdown is refused while the interpreter holds them");
    finalizeInterpreter();
    checks.expect(Py_IsInitialized() == 0, "the interpreter is finalized");
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(),
                  "finalizing the interpreter released every array and capsule: " + exports->lastErrorText());
    return checks.exitCode();
}

[[nodiscard]] int aRefusedHandshakeLeavesNothingBehind()
{
    Checks checks;
    std::string error;
    const BackendExports* exports = loadBackend(&error);
    checks.expect(exports != nullptr && exports->initialize() == 1, "the backend initializes: " + error);
    checks.expect(startInterpreter(&error), "the interpreter starts: " + error);
    if (exports == nullptr || !Py_IsInitialized())
    {
        return checks.exitCode();
    }
    {
        py::gil_scoped_acquire gil;
        // The host's half of the handshake, refused: a capsule under another name is not the backend's session.
        py::object capsule = py::module_::import("_afternight_runtime").attr("_session_capsule")();
        void* pointer = PyCapsule_GetPointer(capsule.ptr(), an::sdk::kRuntimeSessionCapsuleName);
        checks.expect(pointer != nullptr, "the capsule holds a record");
        const auto refused = an::sdk::acceptRuntimeSessionCapsule("afternight.some.other.capsule", pointer);
        checks.expect(!refused.hasValue(), "a capsule under another name is refused");
        // The host abandons its start: it lets the capsule go and finalizes its interpreter.
    }
    finalizeInterpreter();
    exports->shutdown();
    checks.expect(exports->lastErrorText().empty(),
                  "the backend shuts down after a refused handshake: " + exports->lastErrorText());
    return checks.exitCode();
}

} // namespace

int runScenarioInThisProcess(const std::string& name)
{
    if (name == "probe-before-initialize")
    {
        return probeBeforeInitialize();
    }
    if (name == "initialize-is-idempotent-and-repeatable")
    {
        return initializeIsIdempotentAndRepeatable();
    }
    if (name == "shutdown-is-refused-while-python-holds-the-session")
    {
        return shutdownIsRefusedWhilePythonHoldsTheSession();
    }
    if (name == "finalizing-the-interpreter-releases-retained-arrays-and-capsules")
    {
        return finalizingTheInterpreterReleasesRetainedArraysAndCapsules();
    }
    if (name == "a-refused-handshake-leaves-nothing-behind")
    {
        return aRefusedHandshakeLeavesNothingBehind();
    }
    std::fprintf(stderr, "unknown scenario '%s'\n", name.c_str());
    return 2;
}

} // namespace an::tests::runtime_backend
