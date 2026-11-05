# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe app/view helpers shared by full and lite hosted extensions."""

import logging as _logging
import os as _os
import sys as _sys
import tempfile as _tempfile
import threading as _threading
import traceback as _traceback

try:
    import _afternight_host as _host_runtime  # type: ignore
except Exception:  # pragma: no cover - import depends on the host executable
    _host_runtime = None


_HOST_LOGGING_INSTALLED = False
_RUNTIME_IMAGE_FACTORY = None


def _set_runtime_image_adapter(factory):
    """Install the GPL/runtime image adapter without importing it here."""
    global _RUNTIME_IMAGE_FACTORY
    _RUNTIME_IMAGE_FACTORY = factory


def _host_available():
    return _host_runtime is not None


def _host_has_control_socket():
    if not _host_available():
        return False
    probe = getattr(_host_runtime, "has_control_socket", None)
    if probe is None:
        return True
    try:
        return bool(probe())
    except Exception:
        return False


def _session_env(name, default=""):
    return _os.environ.get(f"AFTERNIGHT_{name}", default)


def _session_env_bool(name, default=False):
    value = _session_env(name)
    if value == "":
        return bool(default)
    return value.strip().lower() in {"1", "true", "yes", "on"}


class UnsupportedBackendError(RuntimeError):
    """Raised when the selected SDK backend cannot serve a requested domain."""


def _host_call(name, *args, **kwargs):
    if not _host_available():
        raise RuntimeError("AfterNight host runtime bridge is unavailable")
    _install_host_logging_hooks()
    return getattr(_host_runtime, name)(*args, **kwargs)


def _selected_sdk_backend():
    if _host_available():
        try:
            return str(dict(_host_call("session_info") or {}).get("sdk_backend", "runtime"))
        except Exception:
            pass
    return _session_env("SDK_BACKEND", "runtime")


# What a lent image's sidecar holds beside its header cards: records the host writes and reads back whole - the
# header and EXIF maps and lists and the astrometric solution - none of which is a card.
_STRUCTURED_SIDECAR_KEYS = frozenset({"fits_header", "fits_cards", "exif_header", "exif_entries", "astrometry"})


def _card_metadata(metadata):
    """The entries of an image's metadata that are header cards: the sidecar's structured records and any other
    nested value are left out, since an image made from the metadata holds each entry as one card."""
    result = {}
    for key, value in dict(metadata or {}).items():
        if str(key) in _STRUCTURED_SIDECAR_KEYS or isinstance(value, (dict, list, tuple, set)):
            continue
        result[key] = value
    return result


def _stringify_metadata_dict(metadata):
    result = {}
    for key, value in _card_metadata(metadata).items():
        result[str(key)] = "" if value is None else str(value)
    return result


def _wrap_runtime_image(runtime_image):
    return ImageHandle(runtime_image=runtime_image)


def _coerce_to_runtime_image(image):
    if not isinstance(image, ImageHandle):
        raise TypeError("Expected an afternight.ImageHandle instance")
    if image._runtime_image is not None:
        return image._runtime_image
    if _selected_sdk_backend() in {"protocol", "rpc"}:
        raise UnsupportedBackendError(
            "The selected lite SDK backend does not expose direct AfterNight Engine-backed modules. "
            "Use sdk_backend='runtime' for GPL-compatible full hosted extensions, or use "
            "the protocol-safe app/view APIs with your own processing code."
        )
    if _RUNTIME_IMAGE_FACTORY is None:
        raise RuntimeError("AfterNight runtime image adapter is unavailable")
    return _RUNTIME_IMAGE_FACTORY(image.to_numpy(), _stringify_metadata_dict(image.metadata))


def _severity_from_logging_level(level):
    if level >= _logging.ERROR:
        return "error"
    if level >= _logging.WARNING:
        return "warning"
    if level >= _logging.INFO:
        return "info"
    if level >= _logging.DEBUG:
        return "debug"
    return "trace"


def _host_log_event_safe(severity, component, message):
    if not _host_available():
        return

    text = str(message).strip()
    if not text:
        return

    try:
        _host_runtime.log_event(str(severity), str(component), text)
    except Exception:
        pass


def emit_settings_action_update(result):
    """Send a transient settings-action update to the host, when available."""

    if not _host_available():
        return False
    try:
        _host_call("settings_action_update", dict(result or {}))
    except Exception:
        return False
    return True


class _AfterNightHostLoggingHandler(_logging.Handler):
    def emit(self, record):
        try:
            message = self.format(record) if self.formatter else record.getMessage()
        except Exception:
            message = record.getMessage()

        _host_log_event_safe(
            _severity_from_logging_level(record.levelno),
            record.name or "python.logging",
            message,
        )


def _log_formatted_exception(component, exc_type, exc_value, exc_traceback):
    formatted = "".join(
        _traceback.format_exception(exc_type, exc_value, exc_traceback)
    ).strip()
    if formatted:
        _host_log_event_safe("error", component, formatted)


def _install_host_logging_hooks():
    global _HOST_LOGGING_INSTALLED

    if _HOST_LOGGING_INSTALLED or not _host_available():
        return

    root_logger = _logging.getLogger()
    if not any(isinstance(handler, _AfterNightHostLoggingHandler) for handler in root_logger.handlers):
        handler = _AfterNightHostLoggingHandler()
        handler.setLevel(_logging.NOTSET)
        handler.setFormatter(_logging.Formatter("%(message)s"))
        root_logger.addHandler(handler)

    if root_logger.level == _logging.NOTSET or root_logger.level > _logging.DEBUG:
        root_logger.setLevel(_logging.DEBUG)

    _logging.captureWarnings(True)

    def _host_excepthook(exc_type, exc_value, exc_traceback):
        _log_formatted_exception("python.exception", exc_type, exc_value, exc_traceback)

    def _host_thread_excepthook(args):
        thread_name = getattr(getattr(args, "thread", None), "name", None) or "unknown"
        _log_formatted_exception(
            f"python.thread.{thread_name}",
            args.exc_type,
            args.exc_value,
            args.exc_traceback,
        )

    def _host_unraisablehook(args):
        object_text = ""
        target = getattr(args, "object", None)
        if target is not None:
            try:
                object_text = f" [object={target!r}]"
            except Exception:
                object_text = ""
        formatted = "".join(
            _traceback.format_exception(
                args.exc_type,
                args.exc_value,
                args.exc_traceback,
            )
        ).strip()
        if formatted:
            _host_log_event_safe("error", "python.unraisable", formatted + object_text)

    _sys.excepthook = _host_excepthook
    if hasattr(_threading, "excepthook"):
        _threading.excepthook = _host_thread_excepthook
    if hasattr(_sys, "unraisablehook"):
        _sys.unraisablehook = _host_unraisablehook

    _HOST_LOGGING_INSTALLED = True


class Settings:
    """Minimal extension-scoped settings facade."""

    def __init__(self):
        self._values = {}

    def get(self, key, default=None):
        if _host_available():
            return _host_call("settings_get", key, default)
        return self._values.get(key, default)

    def set(self, key, value):
        if _host_available():
            _host_call("settings_set", key, value)
        self._values[key] = value

    def remove(self, key):
        if _host_available():
            _host_call("settings_remove", key)
        self._values.pop(key, None)

    def keys(self):
        if _host_available():
            return list(_host_call("settings_keys"))
        return list(self._values.keys())


class SessionInfo:
    """Immutable metadata about the active extension session."""

    def __init__(
        self,
        *,
        extension_id="",
        session_id="",
        request_id="",
        view_name="",
        launch_mode="single_image",
        sdk_backend="runtime",
        runtime_target="",
        is_standalone=False,
    ):
        self.extension_id = str(extension_id)
        self.session_id = str(session_id)
        self.request_id = str(request_id)
        self.view_name = str(view_name)
        self.launch_mode = str(launch_mode)
        self.sdk_backend = str(sdk_backend)
        self.runtime_target = str(runtime_target)
        self.is_standalone = bool(is_standalone)


class SessionPaths:
    """Host-managed scratch and artifact paths for the active session."""

    def __init__(self, *, temp_dir="", artifacts_dir=""):
        self._temp_dir = str(temp_dir)
        self._artifacts_dir = str(artifacts_dir)

    def temp_dir(self):
        return self._temp_dir

    def artifacts_dir(self):
        return self._artifacts_dir


def current_session():
    if _host_available():
        values = dict(_host_call("session_info") or {})
        return SessionInfo(
            extension_id=values.get("extension_id", ""),
            session_id=values.get("session_id", ""),
            request_id=values.get("request_id", ""),
            view_name=values.get("view_name", ""),
            launch_mode=values.get("launch_mode", "single_image"),
            sdk_backend=values.get("sdk_backend", "runtime"),
            runtime_target=values.get("runtime_target", ""),
            is_standalone=values.get("is_standalone", False),
        )

    return SessionInfo(
        extension_id=_session_env("EXTENSION_ID"),
        session_id=_session_env("SESSION_ID"),
        launch_mode=_session_env("LAUNCH_MODE", "single_image"),
        sdk_backend=_selected_sdk_backend(),
        is_standalone=_session_env_bool("IS_STANDALONE"),
    )


def session_paths():
    if _host_available():
        values = dict(_host_call("session_paths") or {})
        return SessionPaths(
            temp_dir=values.get("temp_dir", ""),
            artifacts_dir=values.get("artifacts_dir", ""),
        )

    return SessionPaths()


class ExtensionContext:
    """Host-backed session context passed to extension constructors."""

    def __init__(self, settings=None):
        self.settings = settings if isinstance(settings, Settings) else Settings()

    def session_info(self):
        return current_session()

    def session_paths(self):
        return session_paths()

    def get(self, key, default=None):
        return self.settings.get(key, default)

    def set(self, key, value):
        self.settings.set(key, value)

    def remove(self, key):
        self.settings.remove(key)

    def keys(self):
        return self.settings.keys()


def _coerce_extension_context(context_or_settings):
    if isinstance(context_or_settings, ExtensionContext):
        return context_or_settings
    if isinstance(context_or_settings, Settings):
        return ExtensionContext(context_or_settings)
    if context_or_settings is None:
        return ExtensionContext()
    raise TypeError("Extension constructors expect an ExtensionContext or Settings instance")


class Progress:
    """Minimal cooperative progress facade used during bootstrap/inspection."""

    def __init__(self):
        self.value = 0.0
        self.text = ""
        self._cancelled = False

    def set_value(self, value):
        self.value = float(value)
        if _host_available() and hasattr(_host_runtime, "progress_set_value"):
            _host_runtime.progress_set_value(self.value)

    def set_text(self, text):
        self.text = str(text)
        if _host_available() and hasattr(_host_runtime, "progress_set_text"):
            _host_runtime.progress_set_text(self.text)

    def is_cancelled(self):
        if _host_available():
            return bool(_host_call("progress_is_cancelled"))
        return self._cancelled


class WorkflowError(RuntimeError):
    """Raised when a workflow-scoped host operation cannot be completed."""


class WorkflowProgress(Progress):
    """Progress facade for multi-step workflow extensions."""

    def __init__(self, parent=None, stage="", weight=1.0):
        super().__init__()
        self.parent = parent
        self.stage = str(stage)
        self.weight = float(weight)

    def set_stage(self, name, weight=1.0):
        stage = WorkflowProgress(parent=self, stage=str(name), weight=weight)
        self.set_text(str(name))
        return stage

    def set_value(self, fraction):
        value = float(fraction)
        if value < 0.0:
            value = 0.0
        if value > 1.0:
            value = 1.0
        super().set_value(value * 100.0)
        if self.parent is not None:
            self.parent.set_value(value)

    def set_text(self, text):
        prefix = f"{self.stage}: " if self.stage else ""
        super().set_text(prefix + str(text))


class WorkflowContext:
    """Host-backed workflow session context for launch_mode='workflow'."""

    def __init__(self):
        self._fallback_root = None
        self._fallback_working_dir = None

    def _fallback_base(self):
        if self._fallback_root is None:
            self._fallback_root = _tempfile.mkdtemp(prefix="afternight-workflow-")
        return self._fallback_root

    def _fallback_path(self, *parts):
        path = _os.path.join(self._fallback_base(), *parts)
        _os.makedirs(_os.path.dirname(path) if _os.path.splitext(path)[1] else path, exist_ok=True)
        return path

    def _call_path(self, function_name, *args):
        if not _host_available() or not hasattr(_host_runtime, function_name):
            return None
        try:
            value = _host_call(function_name, *args)
        except Exception as exc:
            raise WorkflowError(str(exc)) from exc
        return str(value)

    def get_working_dir(self):
        path = self._call_path("workflow_get_working_dir")
        if path is not None:
            return path
        if self._fallback_working_dir is None:
            self._fallback_working_dir = self._fallback_path("outputs")
        return self._fallback_working_dir

    def set_working_dir(self, path):
        requested = _os.fspath(path)
        result = self._call_path("workflow_set_working_dir", requested)
        if result is not None:
            self._fallback_working_dir = result
            return result
        _os.makedirs(requested, exist_ok=True)
        self._fallback_working_dir = requested
        return requested

    def temp_dir(self):
        path = self._call_path("workflow_temp_dir")
        if path is not None:
            return path
        return self._fallback_path("tmp")

    def output_path(self, name):
        result = self._call_path("workflow_output_path", str(name))
        if result is not None:
            return result
        output = _os.path.join(self._fallback_path("outputs"), str(name))
        _os.makedirs(_os.path.dirname(output), exist_ok=True)
        return output

    def get_sequences(self):
        host_sequences = []
        if _host_available() and hasattr(_host_runtime, "workflow_get_sequences"):
            try:
                host_sequences = list(_host_call("workflow_get_sequences") or [])
            except Exception as exc:
                raise WorkflowError(str(exc)) from exc

        from . import sequences as _sequences
        return host_sequences + list(_sequences.list_sequences())

    def open_result_sequence(self, path):
        if _host_available() and hasattr(_host_runtime, "workflow_open_result_sequence"):
            try:
                return bool(_host_call("workflow_open_result_sequence", _os.fspath(path)))
            except Exception as exc:
                raise WorkflowError(str(exc)) from exc
        return True


class ViewRef:
    """Read-only view reference for the isolated host runtime."""

    def __init__(
        self,
        view_id="",
        view_name="",
        file_name="",
        metadata=None,
        display_settings=None,
        width=0,
        height=0,
        channels=0,
        pixel_type="",
        source_token=None,
        auxiliary_bindings=None,
    ):
        self.view_id = view_id
        self.view_name = view_name
        self.file_name = file_name
        self._metadata = dict(metadata or {})
        self._display_settings = dict(display_settings or {})
        self._source_token = str(source_token) if source_token else None
        self._auxiliary_bindings = (
            ViewRef._normalize_auxiliary_bindings(auxiliary_bindings)
            if auxiliary_bindings is not None
            else None
        )
        self._view_info = {
            "width": int(width) if width is not None else 0,
            "height": int(height) if height is not None else 0,
            "channels": int(channels) if channels is not None else 0,
            "pixel_type": "" if pixel_type is None else str(pixel_type),
        }

    @staticmethod
    def _normalize_auxiliary_bindings(bindings):
        result = {}
        for name, binding in dict(bindings or {}).items():
            normalized = dict(binding or {})
            token = normalized.get("token")
            if not token:
                continue
            result[str(name)] = {
                "token": str(token),
                "buffer_role": str(normalized.get("buffer_role", "")),
            }
        return result

    @staticmethod
    def _filter_auxiliary_images(bindings, buffer_role):
        images = {}
        for name, binding in ViewRef._normalize_auxiliary_bindings(bindings).items():
            if binding.get("buffer_role") != buffer_role:
                continue
            images[name] = ImageHandle(token=binding["token"])
        return images

    def _current_view_info(self):
        info = dict(self._view_info)
        if _host_available() and self._source_token is None:
            info.update(dict(_host_call("current_view_info") or {}))
        return info

    @property
    def metadata(self):
        if _host_available() and self._source_token is None:
            return dict(_host_call("current_view_metadata") or {})
        return dict(self._metadata)

    @property
    def display_settings(self):
        if _host_available() and self._source_token is None:
            return dict(_host_call("current_view_display_settings") or {})
        return dict(self._display_settings)

    @property
    def source_image(self):
        if self._source_token is not None:
            return ImageHandle(token=self._source_token)
        if _host_available():
            token = _host_call("current_view_source_image_token")
            if token:
                return ImageHandle(token=token)
        return None

    @property
    def auxiliary_images(self):
        if self._auxiliary_bindings is not None:
            return {
                name: ImageHandle(token=binding["token"])
                for name, binding in self._auxiliary_bindings.items()
            }
        if not _host_available():
            return {}

        images = {}
        bindings = ViewRef._normalize_auxiliary_bindings(
            _host_call("current_view_auxiliary_image_bindings") or {}
        )
        for name, binding in bindings.items():
            images[name] = ImageHandle(token=binding["token"])
        return images

    @property
    def masks(self):
        if self._auxiliary_bindings is not None:
            return ViewRef._filter_auxiliary_images(self._auxiliary_bindings, "mask")
        if not _host_available():
            return {}
        return ViewRef._filter_auxiliary_images(
            _host_call("current_view_auxiliary_image_bindings") or {},
            "mask",
        )

    @property
    def weights(self):
        if self._auxiliary_bindings is not None:
            return ViewRef._filter_auxiliary_images(self._auxiliary_bindings, "weight")
        if not _host_available():
            return {}
        return ViewRef._filter_auxiliary_images(
            _host_call("current_view_auxiliary_image_bindings") or {},
            "weight",
        )

    @property
    def width(self):
        return int(self._current_view_info().get("width", 0))

    @property
    def height(self):
        return int(self._current_view_info().get("height", 0))

    @property
    def channels(self):
        return int(self._current_view_info().get("channels", 0))

    @property
    def pixel_type(self):
        return str(self._current_view_info().get("pixel_type", ""))


class ImageHandle:
    """Shared-buffer image wrapper surfaced to extension code."""

    def __init__(self, token=None, metadata=None, runtime_image=None):
        self._token = token
        self._metadata = dict(metadata or {})
        self._runtime_image = runtime_image

    @property
    def metadata(self):
        if _host_available() and self._token is not None:
            return _host_call("image_metadata", self._token)
        if self._runtime_image is not None:
            return dict(self._runtime_image.metadata())
        return self._metadata

    @property
    def properties(self):
        if _host_available() and self._token is not None:
            return dict(_host_call("image_properties", self._token) or {})
        if self._runtime_image is not None:
            return dict(self._runtime_image.properties())
        # What an image is - its format, a mosaic's pattern - is stated by the host or the runtime that holds it, and
        # is never guessed from an array's shape.
        raise NotImplementedError("ImageHandle is host-provided at runtime")

    def set_metadata(self, key, value):
        key = str(key)
        value = "" if value is None else str(value)
        if _host_available() and self._token is not None:
            _host_call("image_set_metadata", self._token, key, value)
        elif self._runtime_image is not None:
            self._runtime_image.set_metadata(key, value)
        self._metadata[key] = value

    def to_numpy(self):
        """Return a NumPy view of the image buffer.

        The returned array is only valid for the duration of the current
        execute/execute_preview call. Do not store it beyond that scope.

        Under the runtime backend the array is the image's own samples,
        writable and never copied, and every native call on the image reads
        what was written through it. Code that writes the array from another
        thread, or through a NumPy routine that releases the GIL, while a
        native call on the same image runs must synchronize itself.
        from_numpy() gives the image new storage; arrays taken before it keep
        the old samples.
        """
        if _host_available() and self._token is not None:
            return _host_call("image_to_numpy", self._token)
        if self._runtime_image is not None:
            return self._runtime_image.to_numpy()
        raise NotImplementedError("ImageHandle is host-provided at runtime")

    def from_numpy(self, array):
        if _host_available() and self._token is not None:
            _host_call("image_from_numpy", self._token, array)
            return
        if self._runtime_image is not None:
            self._runtime_image.from_numpy(array)
            return
        raise NotImplementedError("ImageHandle is host-provided at runtime")

    def clone(self):
        if self._runtime_image is not None:
            return ImageHandle(runtime_image=self._runtime_image.clone())
        if _host_available() and self._token is not None and hasattr(_host_runtime, "image_clone"):
            return ImageHandle(token=_host_call("image_clone", self._token), metadata=self.metadata)
        return _wrap_runtime_image(_coerce_to_runtime_image(self).clone())

    def copy_from(self, other):
        self.from_numpy(other.to_numpy())


def log(severity, message, component="extension"):
    if _host_available():
        _host_call("log_event", severity, component, str(message))


def log_trace(message, component="extension"):
    log("trace", message, component)


def log_debug(message, component="extension"):
    log("debug", message, component)


def log_info(message, component="extension"):
    log("info", message, component)


def log_warning(message, component="extension"):
    log("warning", message, component)


def log_error(message, component="extension"):
    log("error", message, component)


if _host_available():
    _install_host_logging_hooks()


def list_views():
    """Return a list of dicts describing all open image views in the host."""
    if _host_available():
        return list(_host_call("list_views"))
    return []


def snapshot_view(view_name=None):
    """Return a read-only snapshot of a host image view."""
    name = "" if view_name is None else str(view_name)
    values = dict(_host_call("view_snapshot", name) or {})
    return ViewRef(
        view_id=values.get("view_id", ""),
        view_name=values.get("view_name", name),
        file_name=values.get("file_name", ""),
        metadata=values.get("metadata") or {},
        display_settings=values.get("display_settings") or {},
        width=values.get("width", 0),
        height=values.get("height", 0),
        channels=values.get("channels", 0),
        pixel_type=values.get("pixel_type", ""),
        source_token=values.get("source_token"),
        auxiliary_bindings=values.get("auxiliary_images") or {},
    )


__all__ = [
    "ExtensionContext",
    "ImageHandle",
    "Progress",
    "SessionInfo",
    "SessionPaths",
    "Settings",
    "UnsupportedBackendError",
    "ViewRef",
    "WorkflowContext",
    "WorkflowError",
    "WorkflowProgress",
    "current_session",
    "emit_settings_action_update",
    "list_views",
    "log",
    "log_debug",
    "log_error",
    "log_info",
    "log_trace",
    "log_warning",
    "session_paths",
    "snapshot_view",
]
