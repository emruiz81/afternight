# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe authoring helpers for lite hosted AfterNight extensions."""

from .protocol import (
    ExtensionContext,
    Progress,
    Settings,
    UnsupportedBackendError,
    _coerce_extension_context,
    _host_available,
    _host_has_control_socket,
    current_session,
    list_views as _list_views,
    log_debug as _log_debug,
    log_error as _log_error,
    log_info as _log_info,
    log_trace as _log_trace,
    log_warning as _log_warning,
    session_paths as _session_paths,
    snapshot_view as _snapshot_view,
)


_LITE_BACKENDS = {"protocol", "rpc"}


def is_lite_backend(backend=None):
    """Return whether *backend* names a lite-hosted SDK backend."""
    selected = current_session().sdk_backend if backend is None else str(backend)
    return selected in _LITE_BACKENDS


def require_lite_backend():
    """Return the current session info or raise if a full-host backend is active."""
    info = current_session()
    if info.sdk_backend not in _LITE_BACKENDS:
        raise UnsupportedBackendError(
            "afternight.lite can only be used from lite hosted extensions "
            "(sdk_backend='protocol' or sdk_backend='rpc')."
        )
    return info


class LiteExtension:
    """Base class for extension-owned UI/lifecycle sessions in the lite host.

    Lite extensions use the protocol-safe app/view services exposed by
    ``afternight`` and own their UI themselves. They do not use the native
    process-window classes from ``afternight.ui``. Use ``afternight.ui_protocol``
    for protocol-safe result presentation helpers.
    """

    def __init__(self, context: ExtensionContext | Settings | None = None):
        self.context = _coerce_extension_context(context)
        self.settings = self.context.settings
        self.progress = Progress()
        self._session_info = require_lite_backend()

    def session_info(self):
        return current_session()

    def session_paths(self):
        return _session_paths()

    def list_views(self):
        return _list_views()

    def active_view(self):
        for view in self.list_views():
            data = dict(view or {})
            if data.get("is_active") or data.get("active"):
                return self.snapshot_view(data.get("name") or data.get("view_name"))
        if not _host_available() or not _host_has_control_socket():
            return None
        return self.snapshot_view()

    def snapshot_view(self, view_name=None):
        if not _host_available():
            return None
        return _snapshot_view(view_name)

    def set_progress(self, value=None, text=None):
        if value is not None:
            self.progress.set_value(value)
        if text is not None:
            self.progress.set_text(text)

    def is_cancelled(self):
        return self.progress.is_cancelled()

    def log_trace(self, message, component="extension"):
        _log_trace(message, component)

    def log_debug(self, message, component="extension"):
        _log_debug(message, component)

    def log_info(self, message, component="extension"):
        _log_info(message, component)

    def log_warning(self, message, component="extension"):
        _log_warning(message, component)

    def log_error(self, message, component="extension"):
        _log_error(message, component)

    def get_settings_params(self):
        return []

    def get_params(self):
        return []

    def on_launch(self):
        """Optional hook called once when the lite session starts."""

    def on_close(self, reason):
        """Optional hook called once when the lite session closes."""

    def on_cancel(self):
        """Optional hook called when host cancellation is requested."""

    def on_view_created(self, view):
        """Optional hook called when the host reports a new image view."""

    def on_view_removed(self, view_id):
        """Optional hook called when the host reports a removed image view."""

    def on_view_renamed(self, view):
        """Optional hook called when the host reports a renamed image view."""

    def on_view_modified(self, view):
        """Optional hook called when the host reports view metadata/image changes."""

    def on_active_view_changed(self, view):
        """Optional hook called when the host active image view changes."""

    def run(self, params=None, progress=None):
        raise NotImplementedError


class LiteProcessExtension(LiteExtension):
    """Base class for protocol-only single-image/app-view extensions."""


class LiteWorkflowExtension(LiteExtension):
    """Base class for protocol-only workflow extensions."""


def _run_extension_startup(instance, params=None, progress=None):
    """Run extension-owned startup only when the extension overrides ``run``."""
    if instance is None:
        return None
    run_method = getattr(instance, "run", None)
    if not callable(run_method):
        return None
    if getattr(type(instance), "run", None) is LiteExtension.run:
        return None
    startup_params = {} if params is None else params
    startup_progress = progress if progress is not None else getattr(instance, "progress", None)
    return run_method(startup_params, startup_progress)


__all__ = [
    "LiteExtension",
    "LiteProcessExtension",
    "LiteWorkflowExtension",
    "is_lite_backend",
    "require_lite_backend",
]
