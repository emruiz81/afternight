// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The host's startup and teardown scenarios, each in a process of its own.
///
/// What they hold the host to, as section "One session for native and Python work" of the host's design states it: the
/// host loads the backend by name and speaks its interface version; it borrows exactly the session the backend opened,
/// through the embedded module's checked capsule, and lends that one to its native windows; its teardown closes
/// admission, destroys the native windows, drains the thread pool, lets its borrow go and finalizes the interpreter
/// before it asks the backend to shut down; a backend that still sees an owner - a native image left behind, or a
/// capsule Python still holds - refuses by name and stays loaded; a refused capsule stops the start and leaves nothing
/// behind; and the module can be initialized and shut down again.

#include "RuntimeBackendTestSupport.h"

#include "HostRuntimeScenarios.h"

#include "ExtensionAppContext.h"
#include "ExtensionNativeProcessWindow.h"
#include "HostRuntimeSession.h"
#include "RuntimeSessionCapsule.h"
#include "controls/ImageViewControl.h"
#include "fixtures/TestImageGenerators.h"
#include "fixtures/harness/TestHarness.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <pybind11/eval.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace an::tests::host_native
{

namespace
{

namespace py = pybind11;

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

[[nodiscard]] bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

/// The host's start: the backend loaded beside this executable, probed and initialized, and the interpreter started.
[[nodiscard]] bool startHostRuntime(Checks& checks, an::ui::extensions::RuntimeBackendModule& backend)
{
    const std::filesystem::path path =
        an::ui::extensions::runtimeBackendModulePath(an::tests::harness::executablePath().parent_path());
    checks.expect(!path.empty(), "the runtime backend module is deployed beside the suite");
    if (path.empty())
    {
        return false;
    }
    const an::Status loaded = backend.load(path);
    checks.expect(loaded.ok(), std::string("the backend loads: ") + loaded.message());
    if (loaded.failed())
    {
        return false;
    }
    checks.expect(backend.abiVersion() == an::ui::extensions::kRuntimeBackendAbiVersion,
                  "the backend speaks the host's interface version");
    const an::Status probed = backend.probe();
    checks.expect(probed.ok(), std::string("the probe succeeds: ") + probed.message());
    checks.expect(Py_IsInitialized() == 0, "the probe starts no interpreter");
    const an::Status initialized = backend.initialize();
    checks.expect(initialized.ok(), std::string("the backend initializes: ") + initialized.message());
    checks.expect(backend.initialized(), "the backend says it is initialized");
    std::string error;
    const bool started = an::tests::runtime_backend::startInterpreter(&error);
    checks.expect(started, "the interpreter starts: " + error);
    return initialized.ok() && started;
}

/// A source view showing an image of the borrowed session, and a native window over it.
struct NativeOwners
{
    std::unique_ptr<ImageViewControl> view;
    std::unique_ptr<an::ui::extensions::ExtensionNativeProcessWindow> window;
};

[[nodiscard]] NativeOwners openNativeWindow(Checks& checks, const an::consumer::EngineSession& session)
{
    NativeOwners owners;
    owners.view = std::make_unique<ImageViewControl>();
    owners.view->initialize("scenario_source", ViewControlMode::st_preview);
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::createGreyFloatAsset(session, 8u, 6u, 0.5);
    checks.expect(asset.hasValue(), "an image of the borrowed session is made");
    if (asset.hasValue())
    {
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset));
        checks.expect(document.hasValue(), "the image becomes a document");
        if (document.hasValue())
        {
            owners.view->setDocument(std::move(*document));
        }
    }
    owners.window = std::make_unique<an::ui::extensions::ExtensionNativeProcessWindow>("Scenario Native Window",
                                                                                       "scenario-session", nullptr);
    owners.window->setSourceView(owners.view.get());
    checks.expect(owners.window->sourceImage() != nullptr, "the native window starts from the view's image");
    return owners;
}

[[nodiscard]] int theHostBorrowsTheBackendSessionAndShutsDownInOrder()
{
    Checks checks;
    an::ui::extensions::RuntimeBackendModule backend;
    if (!startHostRuntime(checks, backend))
    {
        return checks.exitCode();
    }

    an::ui::extensions::HostEngineSession session;
    const an::Status borrowed = session.borrowFromInterpreter();
    checks.expect(borrowed.ok(), std::string("the host borrows the backend's session: ") + borrowed.message());
    if (!session.borrowed())
    {
        return checks.exitCode();
    }
    // The backend's seat and the host's borrow, and nothing else: the capsule the host read is not kept.
    checks.expect(session.owners() == 2, "the session is the backend's, held by its seat and the host's borrow");

    an::ui::extensions::ExtensionAppContext context;
    context.setEngineSessionProvider(&session);
    an::ui::setAppContext(&context);
    checks.expect(&context.getEngineSessionProvider()->engineSession() == &session.engineSession(),
                  "the host's context lends the borrowed session");

    NativeOwners owners = openNativeWindow(checks, session.engineSession());
    std::string error;
    checks.expect(python("import _afternight_runtime as rt\nretained = rt._session_capsule()\n", &error),
                  "Python keeps a capsule of the session: " + error);

    const an::Status early = backend.shutdown();
    checks.expect(early.code() == an::ErrorCode::Busy, "a shutdown while the native window holds an image is refused");
    checks.expect(contains(early.message(), "image asset"),
                  std::string("the refusal names the image: ") + early.message());
    checks.expect(contains(early.message(), "session capsule"),
                  std::string("the refusal names the borrow and the capsule: ") + early.message());
    checks.expect(backend.initialized() && backend.loaded(), "a refused backend keeps its session and stays loaded");
    checks.expect(backend.unload().code() == an::ErrorCode::InvalidState, "an initialized backend is not unloaded");

    an::ui::extensions::HostTeardownSteps steps;
    bool windowsDestroyed = false;
    steps.destroyNativeWindows = [&]() {
        checks.expect(!session.engineSession()
                           .tryAdmit(an::consumer::OperationRequest{.purpose = an::consumer::OperationPurpose::Preview})
                           .hasValue(),
                      "admission is closed before the native windows go");
        owners.window->setSourceView(nullptr);
        owners.window.reset();
        owners.view.reset();
        an::ui::clearAppContext();
        windowsDestroyed = true;
    };
    steps.finalizeInterpreter = [&]() {
        checks.expect(windowsDestroyed, "the native windows go before the interpreter");
        checks.expect(!session.borrowed(), "the host's borrow is let go before the interpreter");
        const an::Status held = backend.shutdown();
        checks.expect(held.code() == an::ErrorCode::Busy, "while Python holds its capsule the backend still refuses");
        checks.expect(contains(held.message(), "session capsule") && !contains(held.message(), "image asset"),
                      std::string("only the capsule Python holds is left: ") + held.message());
        an::tests::runtime_backend::finalizeInterpreter();
    };
    const an::Status shut = an::ui::extensions::shutDownHostRuntime(session, backend, steps);
    checks.expect(shut.ok(), std::string("the host's teardown shuts the backend down: ") + shut.message());
    checks.expect(!backend.initialized() && !backend.loaded(), "a backend that shut down is unloaded");
    return checks.exitCode();
}

[[nodiscard]] int aNativeOwnerLeftKeepsTheBackendLoaded()
{
    Checks checks;
    an::ui::extensions::RuntimeBackendModule backend;
    if (!startHostRuntime(checks, backend))
    {
        return checks.exitCode();
    }
    an::ui::extensions::HostEngineSession session;
    const an::Status borrowed = session.borrowFromInterpreter();
    checks.expect(borrowed.ok(), std::string("the host borrows the backend's session: ") + borrowed.message());
    if (!session.borrowed())
    {
        return checks.exitCode();
    }
    an::ui::extensions::ExtensionAppContext context;
    context.setEngineSessionProvider(&session);
    an::ui::setAppContext(&context);
    NativeOwners owners = openNativeWindow(checks, session.engineSession());
    std::shared_ptr<const an::consumer::ImageAsset> kept = owners.view->document().asset();
    checks.expect(kept != nullptr, "an image of the view is kept past the window");

    an::ui::extensions::HostTeardownSteps steps;
    steps.destroyNativeWindows = [&]() {
        owners.window->setSourceView(nullptr);
        owners.window.reset();
        owners.view.reset();
        an::ui::clearAppContext();
    };
    steps.finalizeInterpreter = []() { an::tests::runtime_backend::finalizeInterpreter(); };
    const an::Status shut = an::ui::extensions::shutDownHostRuntime(session, backend, steps);
    checks.expect(shut.code() == an::ErrorCode::Busy, "the teardown is refused while a native image is held");
    checks.expect(contains(shut.message(), "image asset"), std::string("the refusal names it: ") + shut.message());
    checks.expect(backend.initialized() && backend.loaded(), "the backend keeps its session and stays loaded");

    kept.reset();
    const an::Status later = backend.shutdown();
    checks.expect(later.ok(), std::string("once the image goes the backend shuts down: ") + later.message());
    checks.expect(backend.unload().ok() && !backend.loaded(), "and is unloaded");
    return checks.exitCode();
}

[[nodiscard]] int aRefusedCapsuleStopsTheStartAndLeavesNothingBehind()
{
    Checks checks;
    an::ui::extensions::RuntimeBackendModule backend;
    if (!startHostRuntime(checks, backend))
    {
        return checks.exitCode();
    }
    an::ui::extensions::HostEngineSession session;
    {
        py::gil_scoped_acquire gil;
        py::object capsule = py::module_::import("_afternight_runtime").attr("_session_capsule")();
        void* pointer = PyCapsule_GetPointer(capsule.ptr(), an::sdk::kRuntimeSessionCapsuleName);
        const an::Status refused = session.borrow("afternight.another.session", pointer);
        checks.expect(refused.code() == an::ErrorCode::IncompatibleArguments,
                      std::string("a capsule under another name is refused: ") + refused.message());
        const an::Status empty = session.borrow(an::sdk::kRuntimeSessionCapsuleName, nullptr);
        checks.expect(empty.code() == an::ErrorCode::NotInitialized, "a capsule holding no record is refused");
    }
    checks.expect(!session.borrowed(), "a refused handshake borrows nothing");

    an::ui::extensions::HostTeardownSteps steps;
    steps.finalizeInterpreter = []() { an::tests::runtime_backend::finalizeInterpreter(); };
    const an::Status shut = an::ui::extensions::shutDownHostRuntime(session, backend, steps);
    checks.expect(shut.ok(), std::string("the partial start is taken down cleanly: ") + shut.message());
    checks.expect(!backend.loaded(), "and the backend is unloaded");
    return checks.exitCode();
}

[[nodiscard]] int theBackendCanBeInitializedAndShutDownAgain()
{
    Checks checks;
    an::ui::extensions::RuntimeBackendModule backend;
    const std::filesystem::path path =
        an::ui::extensions::runtimeBackendModulePath(an::tests::harness::executablePath().parent_path());
    checks.expect(backend.load(path).ok(), "the backend loads");
    checks.expect(backend.unload().ok() && !backend.loaded(), "a backend never initialized unloads");
    checks.expect(backend.load(path).ok(), "and loads again");
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        checks.expect(backend.initialize().ok() && backend.initialize().ok(), "initialization is idempotent");
        checks.expect(backend.load(path).code() == an::ErrorCode::InvalidState,
                      "an initialized backend is not loaded again");
        checks.expect(backend.shutdown().ok() && !backend.initialized(), "a shutdown with nothing held succeeds");
        checks.expect(backend.shutdown().ok(), "a second shutdown does nothing");
    }
    checks.expect(Py_IsInitialized() == 0, "none of it starts an interpreter");
    checks.expect(backend.unload().ok() && !backend.loaded(), "the backend unloads");
    return checks.exitCode();
}

} // namespace

int runHostScenario(const std::string& name)
{
    if (name == "the-host-borrows-the-backend-session-and-shuts-down-in-order")
    {
        return theHostBorrowsTheBackendSessionAndShutsDownInOrder();
    }
    if (name == "a-native-owner-left-keeps-the-backend-loaded")
    {
        return aNativeOwnerLeftKeepsTheBackendLoaded();
    }
    if (name == "a-refused-capsule-stops-the-start-and-leaves-nothing-behind")
    {
        return aRefusedCapsuleStopsTheStartAndLeavesNothingBehind();
    }
    if (name == "the-backend-can-be-initialized-and-shut-down-again")
    {
        return theBackendCanBeInitializedAndShutDownAgain();
    }
    std::fprintf(stderr, "unknown scenario '%s'\n", name.c_str());
    return 2;
}

} // namespace an::tests::host_native
