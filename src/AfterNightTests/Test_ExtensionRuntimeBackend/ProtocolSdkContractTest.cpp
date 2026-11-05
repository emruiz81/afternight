// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The protocol side of the `afternight` package - the session, settings, progress, workflow, view and image facades,
/// the Lite base class, the process-window description and the result helpers - held call by call to what it answers
/// with no host, through a host bridge that serves a call, and where a call fails. These are the calls every extension
/// makes on either host, and the package did not change when the engine did; the cases hold that each still says what
/// it said, over a bridge that records what it was asked. Two bridges stand in for the two hosts: the Lite host's
/// serves the session, settings, progress, view, image and result calls and nothing of the native window, the theme,
/// the dialogs or a workflow; the full host's serves those and does not clone an image or report its control socket.

#include "RuntimeBackendTestSupport.h"

#include <gtest/gtest.h>

#include <string>

namespace an::tests::runtime_backend
{
namespace
{

/// What every case runs first: the two stand-in bridges, a scope that installs one as the host, a scope that states
/// session variables, and a check that a call is refused.
constexpr const char* kPrelude = R"PY(
import contextlib
import os
import numpy as np
import afternight
from afternight import core, lite, protocol, ui, ui_protocol


@contextlib.contextmanager
def hosted(bridge):
    saved = protocol._host_runtime
    protocol._host_runtime = bridge
    try:
        yield bridge
    finally:
        protocol._host_runtime = saved


@contextlib.contextmanager
def session_variables(**values):
    names = ["AFTERNIGHT_" + name for name in ("EXTENSION_ID", "SESSION_ID", "LAUNCH_MODE", "SDK_BACKEND",
                                                "IS_STANDALONE")]
    saved = {name: os.environ.get(name) for name in names}
    for name in names:
        os.environ.pop(name, None)
    os.environ.update({"AFTERNIGHT_" + name: value for name, value in values.items()})
    try:
        yield
    finally:
        for name, value in saved.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value


def refuses(kind, call, text=None):
    try:
        call()
    except kind as error:
        if text is not None:
            assert text in str(error), str(error)
        return error
    raise AssertionError("expected " + kind.__name__)


class CommonBridge:
    """What both hosts' bridges serve."""

    def __init__(self, backend):
        self.backend = backend
        self.calls = []
        self.settings = {}
        self.cancelled = False
        self.views = []
        self.images = {"src-1": np.arange(12, dtype=np.float32).reshape(3, 4)}
        self.metadata = {"src-1": {"OBJECT": "'M42'"}}

    def session_info(self):
        return {"extension_id": "an.test", "session_id": "s-1", "request_id": "r-1", "view_name": "M42",
                "launch_mode": "single_image", "sdk_backend": self.backend,
                "runtime_target": "windows-msvc-x86_64", "is_standalone": True}

    def session_paths(self):
        return {"temp_dir": "/session/tmp", "artifacts_dir": "/session/artifacts"}

    def log_event(self, severity, component, message):
        self.calls.append(("log", severity, component, message))

    def list_views(self):
        return list(self.views)

    def view_snapshot(self, name):
        self.calls.append(("snapshot", name))
        if name == "missing":
            raise RuntimeError("Requested view does not exist: missing")
        return {"view_id": "v-1", "view_name": name or "M42", "file_name": "m42.fits",
                "metadata": {"OBJECT": "'M42'"}, "display_settings": {"stretch": "auto"}, "width": 4, "height": 3,
                "channels": 1, "pixel_type": "float32", "source_token": "src-1",
                "auxiliary_images": {"selection": {"token": "mask-1", "buffer_role": "mask"},
                                     "weight_map": {"token": "weight-1", "buffer_role": "weight"},
                                     "untokened": {"buffer_role": "mask"}}}

    def settings_get(self, key, default):
        self.calls.append(("settings_get", key, default))
        return self.settings.get(key, default)

    def settings_set(self, key, value):
        if key == "refused":
            raise RuntimeError("Settings value is not storable")
        self.calls.append(("settings_set", key, value))
        self.settings[key] = value

    def settings_remove(self, key):
        self.calls.append(("settings_remove", key))
        self.settings.pop(key, None)

    def settings_keys(self):
        return sorted(self.settings)

    def progress_set_value(self, value):
        self.calls.append(("progress_value", value))

    def progress_set_text(self, text):
        self.calls.append(("progress_text", text))

    def progress_is_cancelled(self):
        return self.cancelled

    def image_to_numpy(self, token):
        if token not in self.images:
            raise RuntimeError("Unknown image token: " + token)
        return self.images[token]

    def image_from_numpy(self, token, array):
        self.calls.append(("from_numpy", token, tuple(np.asarray(array).shape)))
        self.images[token] = np.array(array)

    def image_properties(self, token):
        return {"width": 4, "height": 3, "channels": 1, "pixel_type": "float32", "pixel_format": "bayer_rggb",
                "color_space": "grayscale", "buffer_role": "source", "access_mode": "read_only"}

    def image_metadata(self, token):
        return dict(self.metadata.get(token, {}))

    def image_set_metadata(self, token, key, value):
        self.calls.append(("set_metadata", token, key, value))
        self.metadata.setdefault(token, {})[key] = value

    def ui_open_image(self, array, metadata, title):
        self.calls.append(("open", tuple(np.asarray(array).shape), dict(metadata), title))
        return True

    def view_commit_numpy(self, array, metadata, name, description, allow_resized_output):
        self.calls.append(("commit", tuple(np.asarray(array).shape), dict(metadata), name, description,
                           allow_resized_output))
        return True


class LiteBridge(CommonBridge):
    """The Lite host's bridge: it clones an image and says whether a control socket is connected."""

    def __init__(self, backend="protocol", control_socket=True):
        super().__init__(backend)
        self.control_socket = control_socket

    def has_control_socket(self):
        return self.control_socket

    def image_clone(self, token):
        clone = token + ".clone"
        self.images[clone] = np.array(self.images[token])
        return clone


class FullBridge(CommonBridge):
    """The full host's bridge: the native window, the theme, the dialogs, a workflow and the current view."""

    def __init__(self, backend="runtime"):
        super().__init__(backend)
        self.working_dir = "/workflow/outputs"

    def settings_action_update(self, result):
        self.calls.append(("settings_action_update", result))

    def theme_tokens(self):
        return {"colors": {"accent": "#123456"}, "is_dark": True}

    def ui_show_error(self, title, message):
        self.calls.append(("error", title, message))
        return None

    def ui_show_warning(self, title, message):
        self.calls.append(("warning", title, message))
        return None

    def ui_show_info(self, title, message):
        self.calls.append(("info", title, message))
        return None

    def ui_confirm(self, title, message, warn):
        self.calls.append(("confirm", title, message, warn))
        return 1

    def process_window_hide(self):
        self.calls.append(("hide",))
        return 1

    def process_window_close(self):
        self.calls.append(("close",))
        return 0

    def workflow_get_working_dir(self):
        return self.working_dir

    def workflow_set_working_dir(self, path):
        if path == "/refused":
            raise RuntimeError("Working directory is outside the session")
        self.working_dir = path
        return path

    def workflow_temp_dir(self):
        return "/workflow/tmp"

    def workflow_output_path(self, name):
        return self.working_dir + "/" + name

    def workflow_get_sequences(self):
        return ["host-sequence"]

    def workflow_open_result_sequence(self, path):
        if path == "/refused.ser":
            raise RuntimeError("Sequence could not be opened")
        self.calls.append(("open_sequence", path))
        return 1

    def current_view_info(self):
        return {"width": 321, "height": 123, "channels": 3, "pixel_type": "uint16"}

    def current_view_metadata(self):
        return {"OBJECT": "'NGC 7000'"}

    def current_view_display_settings(self):
        return {"stretch": "linked"}

    def current_view_source_image_token(self):
        return "src-1"

    def current_view_auxiliary_image_bindings(self):
        return {"selection": {"token": "mask-1", "buffer_role": "mask"},
                "weight_map": {"token": "weight-1", "buffer_role": "weight"}}
)PY";

void expectProtocolPython(const char* source)
{
    const std::string failure = requireSharedRuntime();
    ASSERT_TRUE(failure.empty()) << failure;
    py::gil_scoped_acquire gil;
    py::dict scope;
    try
    {
        runPython(kPrelude, scope);
        runPython(source, scope);
    }
    catch (const std::exception& error)
    {
        FAIL() << error.what();
    }
}

TEST(ProtocolSdkContractTest, TheLiteBackendCheckReadsTheSessionsBackend)
{
    expectProtocolPython(R"PY(
assert lite.is_lite_backend("protocol") and lite.is_lite_backend("rpc")
assert not lite.is_lite_backend("runtime") and not lite.is_lite_backend("")
with session_variables():
    assert not lite.is_lite_backend(), "the runtime backend is the default"
    error = refuses(protocol.UnsupportedBackendError, lite.require_lite_backend, "sdk_backend='protocol'")
    assert isinstance(error, RuntimeError)
with session_variables(SDK_BACKEND="rpc", SESSION_ID="env-session"):
    assert lite.is_lite_backend()
    info = lite.require_lite_backend()
    assert info.sdk_backend == "rpc" and info.session_id == "env-session"
with hosted(LiteBridge(backend="protocol")):
    assert lite.is_lite_backend()
    assert lite.require_lite_backend().session_id == "s-1"
with hosted(FullBridge()), session_variables(SDK_BACKEND="protocol"):
    assert not lite.is_lite_backend(), "the host's answer outranks the environment"
    refuses(protocol.UnsupportedBackendError, lite.require_lite_backend)
)PY");
}

TEST(ProtocolSdkContractTest, ALiteExtensionServesItsSessionThroughTheProtocolBridge)
{
    expectProtocolPython(R"PY(
with hosted(LiteBridge()) as bridge:
    extension = lite.LiteExtension(None)
    assert isinstance(extension.context, protocol.ExtensionContext)
    assert extension.session_info().session_id == "s-1"
    paths = extension.session_paths()
    assert paths.temp_dir() == "/session/tmp" and paths.artifacts_dir() == "/session/artifacts"
    assert extension.list_views() == []

    bridge.views = [{"name": "M31"}, {"name": "M42", "is_active": True}]
    active = extension.active_view()
    assert active.view_name == "M42" and bridge.calls[-1] == ("snapshot", "M42")
    bridge.views = [{"view_name": "M33", "active": True}]
    assert extension.active_view().view_name == "M33"
    bridge.views = []
    assert extension.active_view().view_name == "M42" and bridge.calls[-1] == ("snapshot", "")
    bridge.control_socket = False
    assert extension.active_view() is None, "a standalone session with no control socket has no active view"

    snapshot = extension.snapshot_view("M42")
    assert isinstance(snapshot, protocol.ViewRef) and snapshot.width == 4 and snapshot.pixel_type == "float32"
    refuses(RuntimeError, lambda: extension.snapshot_view("missing"), "does not exist")

    del bridge.calls[:]
    extension.set_progress(0.25, "a quarter")
    extension.set_progress(text="text only")
    extension.set_progress(value=1)
    extension.set_progress()
    assert bridge.calls == [("progress_value", 0.25), ("progress_text", "a quarter"), ("progress_text", "text only"),
                            ("progress_value", 1.0)]
    refuses(ValueError, lambda: extension.set_progress("half"))
    refuses(TypeError, lambda: extension.set_progress(object()))
    assert extension.is_cancelled() is False
    bridge.cancelled = 1
    assert extension.is_cancelled() is True

    del bridge.calls[:]
    extension.log_trace("t")
    extension.log_debug("d", component="custom")
    extension.log_info(3)
    extension.log_warning("w")
    extension.log_error("e")
    assert bridge.calls == [("log", "trace", "extension", "t"), ("log", "debug", "custom", "d"),
                            ("log", "info", "extension", "3"), ("log", "warning", "extension", "w"),
                            ("log", "error", "extension", "e")]

    assert extension.get_settings_params() == [] and extension.get_params() == []
    for hook, arguments in (("on_launch", ()), ("on_close", ("closed",)), ("on_cancel", ()),
                            ("on_view_created", ({"name": "M42"},)), ("on_view_removed", ("v-1",)),
                            ("on_view_renamed", ({"name": "M43"},)), ("on_view_modified", ({"name": "M43"},)),
                            ("on_active_view_changed", ({"name": "M43"},))):
        assert getattr(extension, hook)(*arguments) is None, hook
    refuses(TypeError, lambda: extension.on_close())
    refuses(TypeError, lambda: extension.on_view_created())
    refuses(TypeError, lambda: extension.on_view_removed())
    refuses(TypeError, lambda: extension.on_view_renamed())
    refuses(TypeError, lambda: extension.on_view_modified())
    refuses(TypeError, lambda: extension.on_active_view_changed())
    refuses(NotImplementedError, extension.run)
    refuses(NotImplementedError, lambda: extension.run({}, extension.progress))
    assert lite._run_extension_startup(extension) is None, "a run that is not overridden is not started"

    class Started(lite.LiteProcessExtension):
        def run(self, params=None, progress=None):
            return (params, progress)

    started = Started(protocol.Settings())
    answer = lite._run_extension_startup(started)
    assert answer[0] == {} and answer[1] is started.progress
)PY");
}

TEST(ProtocolSdkContractTest, ALiteExtensionWithoutAHostAnswersItsLocalDefaults)
{
    expectProtocolPython(R"PY(
with session_variables():
    refuses(protocol.UnsupportedBackendError, lambda: lite.LiteExtension(None))
with session_variables(SDK_BACKEND="protocol", EXTENSION_ID="an.local", SESSION_ID="local",
                       LAUNCH_MODE="workflow", IS_STANDALONE="yes"):
    extension = lite.LiteExtension(protocol.Settings())
    info = extension.session_info()
    assert (info.extension_id, info.session_id, info.launch_mode, info.is_standalone) == (
        "an.local", "local", "workflow", True)
    assert extension.session_paths().temp_dir() == "" and extension.session_paths().artifacts_dir() == ""
    assert extension.list_views() == []
    assert extension.active_view() is None and extension.snapshot_view("M42") is None
    extension.set_progress(0.5, "half")
    assert extension.progress.value == 0.5 and extension.progress.text == "half"
    assert extension.is_cancelled() is False
    for log in (extension.log_trace, extension.log_debug, extension.log_info, extension.log_warning,
                extension.log_error):
        assert log("nothing listens") is None
    refuses(TypeError, lambda: lite.LiteExtension(object()), "ExtensionContext or Settings")
)PY");
}

TEST(ProtocolSdkContractTest, SettingsAndTheSessionReachTheBridgeAndKeepALocalCopy)
{
    expectProtocolPython(R"PY(
with session_variables(EXTENSION_ID="an.local", SESSION_ID="local"):
    settings = protocol.Settings()
    assert settings.get("missing") is None and settings.get("missing", 7) == 7
    settings.set("a", 1)
    settings.set("b", [1, 2])
    assert settings.get("a") == 1 and sorted(settings.keys()) == ["a", "b"]
    settings.remove("a")
    settings.remove("never set")
    assert settings.keys() == ["b"]

    info = protocol.current_session()
    assert (info.extension_id, info.session_id, info.request_id, info.view_name, info.launch_mode, info.sdk_backend,
            info.runtime_target, info.is_standalone) == ("an.local", "local", "", "", "single_image", "runtime", "",
                                                         False)
    paths = protocol.session_paths()
    assert paths.temp_dir() == "" and paths.artifacts_dir() == ""
    explicit = protocol.SessionPaths(temp_dir="/t", artifacts_dir="/a")
    assert explicit.temp_dir() == "/t" and explicit.artifacts_dir() == "/a"

    context = protocol.ExtensionContext(settings)
    assert context.settings is settings and context.session_info().session_id == "local"
    assert context.session_paths().temp_dir() == ""
    context.set("c", "three")
    assert context.get("c") == "three" and settings.get("c") == "three" and context.get("d", "x") == "x"
    assert sorted(context.keys()) == ["b", "c"]
    context.remove("c")
    assert context.keys() == ["b"]
    assert isinstance(protocol.ExtensionContext("not settings").settings, protocol.Settings)
    assert protocol.emit_settings_action_update({"ok": True}) is False, "nothing receives it without a host"

with hosted(FullBridge()) as bridge:
    settings = protocol.Settings()
    settings.set("a", 1)
    assert bridge.settings == {"a": 1} and settings._values == {"a": 1}
    assert settings.get("a") == 1 and settings.get("z", "fallback") == "fallback"
    assert settings.keys() == ["a"]
    settings.remove("a")
    assert bridge.settings == {} and settings._values == {}
    refuses(RuntimeError, lambda: settings.set("refused", 1), "not storable")
    assert "refused" not in settings._values, "a value the host refuses is not kept"

    info = protocol.current_session()
    assert (info.extension_id, info.session_id, info.request_id, info.view_name, info.sdk_backend,
            info.runtime_target, info.is_standalone) == ("an.test", "s-1", "r-1", "M42", "runtime",
                                                         "windows-msvc-x86_64", True)
    assert protocol.session_paths().artifacts_dir() == "/session/artifacts"
    context = protocol.ExtensionContext()
    assert context.session_info().request_id == "r-1" and context.session_paths().temp_dir() == "/session/tmp"
    context.set("k", "v")
    assert context.get("k") == "v" and context.keys() == ["k"]
    context.remove("k")
    assert bridge.settings == {}
    refuses(RuntimeError, lambda: context.set("refused", 1))

    assert protocol.emit_settings_action_update({"ok": True, "message": "Reset"}) is True
    assert bridge.calls[-1] == ("settings_action_update", {"ok": True, "message": "Reset"})
    assert protocol.emit_settings_action_update(None) is True and bridge.calls[-1] == ("settings_action_update", {})
    assert protocol.emit_settings_action_update(5) is False, "an update that is not a mapping is not sent"

with hosted(LiteBridge()):
    assert protocol.emit_settings_action_update({"ok": True}) is False, "the Lite host serves no settings action"
)PY");
}

TEST(ProtocolSdkContractTest, ProgressReachesTheBridgeAndAWorkflowStageNamesItsText)
{
    expectProtocolPython(R"PY(
progress = protocol.Progress()
progress.set_value("0.5")
progress.set_text(7)
assert progress.value == 0.5 and progress.text == "7" and progress.is_cancelled() is False
refuses(ValueError, lambda: progress.set_value("most"))
refuses(TypeError, lambda: progress.set_value(None))

root = protocol.WorkflowProgress()
stage = root.set_stage("Calibrating", weight=2)
assert isinstance(stage, protocol.WorkflowProgress) and stage.parent is root and stage.weight == 2.0
assert root.text == "Calibrating" and stage.stage == "Calibrating"
stage.set_text("frame 1")
assert stage.text == "Calibrating: frame 1"
stage.set_value(0.25)
assert stage.value == 25.0 and root.value == 25.0
stage.set_value(-3)
assert stage.value == 0.0 and root.value == 0.0
stage.set_value(4)
assert stage.value == 100.0 and root.value == 100.0
refuses(ValueError, lambda: stage.set_value("nearly"))
refuses(ValueError, lambda: root.set_stage("Bad", weight="heavy"))

with hosted(FullBridge()) as bridge:
    progress = protocol.Progress()
    progress.set_value(3)
    progress.set_text("working")
    assert bridge.calls == [("progress_value", 3.0), ("progress_text", "working")]
    assert progress.is_cancelled() is False
    bridge.cancelled = True
    assert progress.is_cancelled() is True
    del bridge.calls[:]
    stage = protocol.WorkflowProgress().set_stage("Stacking")
    stage.set_value(0.5)
    assert ("progress_text", "Stacking") in bridge.calls and ("progress_value", 50.0) in bridge.calls

class Silent:
    def session_info(self):
        return {}

with hosted(Silent()):
    progress = protocol.Progress()
    progress.set_value(1)
    progress.set_text("kept locally")
    assert progress.value == 1.0 and progress.text == "kept locally", "a bridge without progress keeps it here"
    refuses(AttributeError, progress.is_cancelled)
)PY");
}

TEST(ProtocolSdkContractTest, AWorkflowContextAsksTheHostAndOtherwiseWorksUnderARootOfItsOwn)
{
    expectProtocolPython(R"PY(
import shutil
import tempfile
context = protocol.WorkflowContext()
working = context.get_working_dir()
root = context._fallback_root
try:
    assert os.path.isdir(working) and working == os.path.join(root, "outputs")
    assert context.get_working_dir() == working
    temporary = context.temp_dir()
    assert os.path.isdir(temporary) and temporary == os.path.join(root, "tmp")
    output = context.output_path("reports/run.txt")
    assert output == os.path.join(root, "outputs", "reports/run.txt") and os.path.isdir(os.path.dirname(output))
    chosen = os.path.join(root, "chosen")
    assert context.set_working_dir(chosen) == chosen and os.path.isdir(chosen)
    assert context.get_working_dir() == chosen
    assert isinstance(context.get_sequences(), list)
    assert context.open_result_sequence(os.path.join(root, "result.ser")) is True
finally:
    shutil.rmtree(root, ignore_errors=True)

with hosted(FullBridge()) as bridge:
    context = protocol.WorkflowContext()
    assert context.get_working_dir() == "/workflow/outputs"
    assert context.set_working_dir("/workflow/chosen") == "/workflow/chosen"
    assert context.get_working_dir() == "/workflow/chosen" and context.output_path("a.fit") == "/workflow/chosen/a.fit"
    assert context.temp_dir() == "/workflow/tmp"
    assert context.get_sequences()[0] == "host-sequence"
    assert context.open_result_sequence("/workflow/chosen/result.ser") is True
    assert bridge.calls[-1] == ("open_sequence", "/workflow/chosen/result.ser")
    error = refuses(protocol.WorkflowError, lambda: context.set_working_dir("/refused"), "outside the session")
    assert isinstance(error, RuntimeError) and isinstance(error.__cause__, RuntimeError)
    refuses(protocol.WorkflowError, lambda: context.open_result_sequence("/refused.ser"), "could not be opened")
    assert context._fallback_root is None, "a context the host serves makes no directory of its own"

class FailingSequences(FullBridge):
    def workflow_get_sequences(self):
        raise RuntimeError("sequences are unavailable")

with hosted(FailingSequences()):
    refuses(protocol.WorkflowError, lambda: protocol.WorkflowContext().get_sequences(), "unavailable")

with hosted(LiteBridge()):
    context = protocol.WorkflowContext()
    try:
        assert context.get_working_dir().startswith(context._fallback_root), "the Lite host serves no workflow"
    finally:
        shutil.rmtree(context._fallback_root, ignore_errors=True)
)PY");
}

TEST(ProtocolSdkContractTest, AViewReferenceAsksTheHostOnlyForTheCurrentView)
{
    expectProtocolPython(R"PY(
stated = protocol.ViewRef(view_id="v", view_name="M42", metadata={"OBJECT": "'M42'"},
                          display_settings={"stretch": "auto"}, width=4, height=3, channels=1, pixel_type="float32",
                          source_token="src-1",
                          auxiliary_bindings={"selection": {"token": "mask-1", "buffer_role": "mask"},
                                              "weight_map": {"token": "weight-1", "buffer_role": "weight"},
                                              "untokened": {"buffer_role": "mask"}})
bare = protocol.ViewRef(width=None, height=None, channels=None, pixel_type=None)
for view in (stated,):
    with hosted(FullBridge()):
        assert view.metadata == {"OBJECT": "'M42'"} and view.display_settings == {"stretch": "auto"}
        assert view.source_image._token == "src-1"
        assert sorted(view.auxiliary_images) == ["selection", "weight_map"]
        assert list(view.masks) == ["selection"] and view.masks["selection"]._token == "mask-1"
        assert list(view.weights) == ["weight_map"] and view.weights["weight_map"]._token == "weight-1"
        assert (view.width, view.height, view.channels, view.pixel_type) == (4, 3, 1, "float32")
copy = stated.metadata
copy["OBJECT"] = "changed"
assert stated.metadata["OBJECT"] == "'M42'", "a view's metadata is a copy"

assert bare.metadata == {} and bare.display_settings == {} and bare.source_image is None
assert bare.auxiliary_images == {} and bare.masks == {} and bare.weights == {}
assert (bare.width, bare.height, bare.channels, bare.pixel_type) == (0, 0, 0, "")

current = ui.ImageView()
assert ui.ImageView is protocol.ViewRef
with hosted(FullBridge()):
    assert current.metadata == {"OBJECT": "'NGC 7000'"} and current.display_settings == {"stretch": "linked"}
    assert current.source_image._token == "src-1"
    assert sorted(current.auxiliary_images) == ["selection", "weight_map"]
    assert list(current.masks) == ["selection"] and list(current.weights) == ["weight_map"]
    assert (current.width, current.height, current.channels, current.pixel_type) == (321, 123, 3, "uint16")

class NoSource(FullBridge):
    def current_view_source_image_token(self):
        return ""

with hosted(NoSource()):
    assert current.source_image is None
with hosted(LiteBridge()):
    for read in (lambda: current.metadata, lambda: current.display_settings, lambda: current.source_image,
                 lambda: current.auxiliary_images, lambda: current.masks, lambda: current.weights,
                 lambda: current.width, lambda: current.height, lambda: current.channels,
                 lambda: current.pixel_type):
        refuses(AttributeError, read)
)PY");
}

TEST(ProtocolSdkContractTest, AnImageHandleReachesWhatHoldsItsSamples)
{
    expectProtocolPython(R"PY(
with hosted(LiteBridge()) as bridge:
    handle = protocol.ImageHandle(token="src-1")
    assert handle.metadata == {"OBJECT": "'M42'"}
    assert handle.properties["pixel_format"] == "bayer_rggb", "a host states the format; nothing guesses it"
    handle.set_metadata("EXPTIME", 120)
    handle.set_metadata("NOTE", None)
    assert bridge.metadata["src-1"]["EXPTIME"] == "120" and bridge.metadata["src-1"]["NOTE"] == ""
    assert handle.to_numpy().shape == (3, 4)
    handle.from_numpy(np.zeros((3, 4), np.float32))
    assert float(bridge.images["src-1"].sum()) == 0.0
    clone = handle.clone()
    assert clone._token == "src-1.clone" and clone._metadata["EXPTIME"] == "120"
    bridge.images["src-1"] = np.ones((3, 4), np.float32)
    clone.copy_from(handle)
    assert float(bridge.images["src-1.clone"].sum()) == 12.0
    missing = protocol.ImageHandle(token="gone")
    refuses(RuntimeError, missing.to_numpy, "Unknown image token")
    refuses(RuntimeError, lambda: clone.copy_from(missing), "Unknown image token")

with hosted(FullBridge()), session_variables():
    handle = protocol.ImageHandle(token="src-1")
    clone = handle.clone()
    assert clone._runtime_image is not None, "the full host clones through the runtime"
    assert clone.to_numpy().shape == (3, 4) and clone.metadata["OBJECT"] == "'M42'"

image = core.from_numpy(np.full((2, 3), 0.25, np.float32), {"FILTER": "Ha"})
assert isinstance(image, protocol.ImageHandle) and image._runtime_image is not None
assert image.metadata["FILTER"] == "Ha" and image.properties["width"] == 3
image.set_metadata("GAIN", 100)
assert image.metadata["GAIN"] == "100"
image.from_numpy(np.full((2, 3), 0.5, np.float32))
assert float(image.to_numpy()[1, 2]) == 0.5
duplicate = image.clone()
duplicate.from_numpy(np.zeros((2, 3), np.float32))
assert float(image.to_numpy()[1, 2]) == 0.5, "a clone owns its own samples"
duplicate.copy_from(image)
assert float(duplicate.to_numpy()[0, 0]) == 0.5

bare = protocol.ImageHandle(metadata={"OBJECT": "M1"})
assert bare.metadata == {"OBJECT": "M1"}
bare.set_metadata("EXPTIME", 3.5)
assert bare.metadata["EXPTIME"] == "3.5"
for call in (lambda: bare.properties, bare.to_numpy, lambda: bare.from_numpy(np.zeros(1)), bare.clone,
             lambda: bare.copy_from(image)):
    refuses(NotImplementedError, call, "host-provided")
with session_variables(SDK_BACKEND="protocol"):
    refuses(protocol.UnsupportedBackendError, bare.clone, "lite SDK backend")
refuses(AttributeError, lambda: image.copy_from(object()))
)PY");
}

// The full host lends an image with its sidecar beside its cards: the header and EXIF maps and lists and the
// astrometric solution. clone() and every runtime call over a lent image rebuilt the image from each entry as a card,
// so a record's text, far longer than a card, was refused, and clone() failed on the image of any file with header
// cards - the standalone author template's Preview and Commit among them.
TEST(ProtocolSdkContractTest, ALentImageCarryingItsSidecarsRecordsIsClonedAndProcessedWithItsCards)
{
    expectProtocolPython(R"PY(
class SidecarBridge(FullBridge):
    def __init__(self):
        super().__init__()
        cards = [{"key": "OBJECT", "value": "'M42'", "comment": "the target", "raw": "OBJECT  = 'M42'"}]
        cards += [{"key": "NOTE%03d" % index, "value": "'note %d'" % index, "comment": "a note the file carries",
                   "raw": ("NOTE%03d= 'note %d' / a note the file carries" % (index, index)).ljust(80)}
                  for index in range(60)]
        # As long as a real file's records: written as one card, longer than a card carries.
        assert len(str(cards)) > 4096
        self.metadata["src-1"] = {
            "FILENAME": "m42.fits",
            "OBJECT": "'M42'",
            "EXPTIME": "120.",
            "fits_header": {"OBJECT": "'M42'", "EXPTIME": "120."},
            "fits_cards": cards,
            "exif_header": {"Exif.Photo.ExposureTime": "120"},
            "exif_entries": [{"key": "Exif.Photo.ExposureTime", "value": "120", "tag": 33434, "count": 1}] * 40,
            "astrometry": {"crval": [83.8, -5.4], "projection": {"type": "TAN"}, "sip": {"A": [[0.0] * 5] * 5}},
        }

with hosted(SidecarBridge()) as bridge, session_variables():
    handle = protocol.ImageHandle(token="src-1")
    clone = handle.clone()
    assert clone._runtime_image is not None and clone.to_numpy().shape == (3, 4)
    assert "M42" in clone.metadata["OBJECT"] and "EXPTIME" in clone.metadata
    for record in ("fits_header", "fits_cards", "exif_header", "exif_entries", "astrometry"):
        assert record not in clone.metadata, record
    core.apply_linear_transform(handle, clone, 2.0, 0.0)
    assert float(clone.to_numpy()[2, 3]) == 22.0, "the transform reads the lent image and writes the clone"
    clone.copy_from(handle)
    assert float(clone.to_numpy()[2, 3]) == 11.0
    rebuilt = core.from_numpy(handle.to_numpy(), handle.metadata)
    assert "M42" in rebuilt.metadata["OBJECT"] and "fits_cards" not in rebuilt.metadata
    assert ui_protocol.commit_image(clone) is True
    assert bridge.calls[-1][0] == "commit" and bridge.calls[-1][1] == (3, 4)
)PY");
}

TEST(ProtocolSdkContractTest, LoggingListingAndSnapshotsNeedAHost)
{
    expectProtocolPython(R"PY(
for call in (lambda: protocol.log("info", "m"), lambda: protocol.log_trace("m"), lambda: protocol.log_debug("m"),
             lambda: protocol.log_info("m"), lambda: protocol.log_warning("m"), lambda: protocol.log_error("m")):
    assert call() is None
assert protocol.list_views() == []
refuses(RuntimeError, protocol.snapshot_view, "bridge is unavailable")
refuses(RuntimeError, lambda: protocol.snapshot_view("M42"), "bridge is unavailable")

with hosted(FullBridge()) as bridge:
    protocol.log("notice", 42, component="custom")
    protocol.log_trace("t")
    protocol.log_debug("d")
    protocol.log_info("i", "custom")
    protocol.log_warning("w")
    protocol.log_error(RuntimeError("e"))
    logged = [call for call in bridge.calls if call[0] == "log"]
    assert logged == [("log", "notice", "custom", "42"), ("log", "trace", "extension", "t"),
                      ("log", "debug", "extension", "d"), ("log", "info", "custom", "i"),
                      ("log", "warning", "extension", "w"), ("log", "error", "extension", "e")]
    bridge.views = [{"name": "M42", "is_active": True}]
    views = protocol.list_views()
    assert views == [{"name": "M42", "is_active": True}] and views is not bridge.views
    snapshot = protocol.snapshot_view()
    assert bridge.calls[-1] == ("snapshot", "") and snapshot.view_name == "M42" and snapshot.view_id == "v-1"
    assert snapshot.file_name == "m42.fits" and snapshot.source_image._token == "src-1"
    assert list(snapshot.masks) == ["selection"] and list(snapshot.weights) == ["weight_map"]
    assert protocol.snapshot_view(7).view_name == "7"
    refuses(RuntimeError, lambda: protocol.snapshot_view("missing"), "does not exist")
)PY");
}

TEST(ProtocolSdkContractTest, TheProcessWindowMetaStatesEveryKeywordItIsGiven)
{
    expectProtocolPython(R"PY(
assert ui.process_window_meta() == {"id": "window_meta", "type": "meta", "panel_variant": "native"}
assert ui.process_window_meta(panel_variant=None) == {"id": "window_meta", "type": "meta"}
meta = ui.process_window_meta(panel_variant="custom", size=(800, 600), fixed_size=True, resizable=False,
                              preview_surface=1, preview_area=0, sub_area=True, sub_area_default_enabled=1,
                              sub_area_size={"width": 64, "height": 32}, sub_area_label=5,
                              controls_panel_width="320", preview_hq_default=1, preview_autostretch=0,
                              preview_autostretch_default=1, header_progress=1, target_selector=False,
                              target_channel_filter=("1", 3), target_selector_label="Target",
                              target_selector_position="top", tool_configuration=[("zoom", True)])
assert meta == {"id": "window_meta", "type": "meta", "panel_variant": "custom", "window_size": [800, 600],
                "fixed_size": True, "resizable": False, "preview_surface": True, "preview_area": False,
                "sub_area": True, "sub_area_default_enabled": True, "sub_area_size": [64, 32],
                "sub_area_label": "5", "controls_panel_width": 320, "preview_hq_default": True,
                "preview_autostretch": False, "preview_autostretch_default": True, "header_progress": True,
                "target_channel_filter": [1, 3], "target_selector_label": "Target",
                "target_selector_position": "top", "target_selector": True,
                "tool_configuration": {"zoom": True}}, meta
assert ui.process_window_meta(target_selector=True)["target_selector"] is True
for keyword in ({"target_selector_label": "T"}, {"target_selector_position": "bottom"},
                {"target_channel_filter": [1]}):
    assert ui.process_window_meta(**keyword)["target_selector"] is True, keyword
assert "sub_area" not in ui.process_window_meta(sub_area=False)

refuses(ValueError, lambda: ui.process_window_meta(fixed_size=True, resizable=True), "opposite states")
refuses(ValueError, lambda: ui.process_window_meta(fixed_size=False, resizable=False), "opposite states")
refuses(ValueError, lambda: ui.process_window_meta(size=(0, 10)), "must be positive")
refuses(ValueError, lambda: ui.process_window_meta(size=5), "(width, height) pair")
refuses(ValueError, lambda: ui.process_window_meta(sub_area_size=(10, -1)), "must be positive")
refuses(ValueError, lambda: ui.process_window_meta(controls_panel_width=0), "must be positive")
refuses(TypeError, lambda: ui.process_window_meta(size={"width": 10}))
refuses(TypeError, lambda: ui.process_window_meta("custom"))
refuses(ValueError, lambda: ui.process_window_meta(target_channel_filter=["red"]))

assert ui.hide_process_window() is False and ui.close_process_window() is False
with hosted(FullBridge()) as bridge:
    assert ui.hide_process_window() is True and ui.close_process_window() is False
    assert bridge.calls == [("hide",), ("close",)]
with hosted(LiteBridge()):
    refuses(AttributeError, ui.hide_process_window)
    refuses(AttributeError, ui.close_process_window)
)PY");
}

TEST(ProtocolSdkContractTest, TheResultHelpersAnswerTheHostOrNothing)
{
    expectProtocolPython(R"PY(
for name in ("open_image", "commit_image", "tokens", "show_error", "show_warning", "show_info", "confirm"):
    assert getattr(ui, name) is getattr(ui_protocol, name), name
array = np.zeros((3, 4), np.float32)
assert ui_protocol.open_image(array) is False and ui_protocol.commit_image(array) is False
assert ui_protocol.tokens() == {}
assert ui_protocol.show_error("e") is None and ui_protocol.show_warning("w") is None
assert ui_protocol.show_info("i") is None and ui_protocol.confirm("c") is False

with hosted(FullBridge()) as bridge:
    assert ui_protocol.open_image(array) is True
    assert bridge.calls[-1] == ("open", (3, 4), {}, "Extension Image")
    assert ui_protocol.open_image(array, title=9, metadata={"OBJECT": "M1"}) is True
    assert bridge.calls[-1] == ("open", (3, 4), {"OBJECT": "M1"}, "9")
    assert ui_protocol.commit_image(array) is True
    assert bridge.calls[-1] == ("commit", (3, 4), {}, "Extension Apply", "Applied extension execution result", False)
    assert ui_protocol.commit_image(array, history_step_name="Step", history_step_description="What it did",
                                    metadata={"A": "1"}, allow_resized_output=1) is True
    assert bridge.calls[-1] == ("commit", (3, 4), {"A": "1"}, "Step", "What it did", True)

    handle = protocol.ImageHandle(token="src-1")
    assert ui_protocol.open_image(handle) is True
    assert bridge.calls[-1] == ("open", (3, 4), {"OBJECT": "'M42'"}, "Extension Image"), "a handle brings its cards"
    assert ui_protocol.commit_image(handle, metadata={"B": "2"}) is True
    assert bridge.calls[-1][2] == {"B": "2"}, "stated metadata replaces the handle's"

    assert ui_protocol.tokens() == {"colors": {"accent": "#123456"}, "is_dark": True}
    del bridge.calls[:]
    assert ui_protocol.show_error(1) is None and ui_protocol.show_warning("w", title="T") is None
    assert ui_protocol.show_info("i") is None and ui_protocol.confirm("c", warn=1) is True
    assert bridge.calls == [("error", "Extension Error", "1"), ("warning", "T", "w"),
                            ("info", "Extension Message", "i"), ("confirm", "Extension Confirmation", "c", True)]
    refuses(RuntimeError, lambda: ui_protocol.open_image(protocol.ImageHandle(token="gone")), "Unknown image token")

class Refusing(FullBridge):
    def theme_tokens(self):
        raise RuntimeError("no theme")

    def view_commit_numpy(self, *arguments):
        raise RuntimeError("The commit was refused")

with hosted(Refusing()):
    assert ui_protocol.tokens() == {}, "a theme the host cannot give reads as none"
    refuses(RuntimeError, lambda: ui_protocol.commit_image(array), "refused")

with hosted(LiteBridge()) as bridge:
    assert ui_protocol.tokens() == {}, "the Lite host serves no theme"
    assert ui_protocol.commit_image(array) is True and ui_protocol.open_image(array, title="Lite") is True
    for call in (lambda: ui_protocol.show_error("e"), lambda: ui_protocol.show_warning("w"),
                 lambda: ui_protocol.show_info("i"), lambda: ui_protocol.confirm("c")):
        refuses(AttributeError, call)
)PY");
}

} // namespace
} // namespace an::tests::runtime_backend
