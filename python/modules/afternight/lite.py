# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe authoring helpers for lite hosted AfterNight extensions.

A Lite extension shows itself one of three ways, as its manifest's ``capabilities.interface`` says: a panel the
application draws from the parameters it describes (``panel``), a window of its own (``window``), or nothing at all
(``none``). The panel is protocol data - the same description a ``runtime`` extension's process window reads - so a
Lite extension gets it without linking anything of the application.
"""

from .protocol import (
    ExtensionContext,
    ImageHandle,
    Progress,
    Settings,
    UnsupportedBackendError,
    ViewRef,
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

    def on_raise_window(self):
        """Optional hook called when a person asks to see the extension's window - Show on its task. An extension that
        owns a window raises it here; the Lite host has no window of its own to raise."""

    def run(self, params=None, progress=None):
        raise NotImplementedError


class LiteProcessExtension(LiteExtension):
    """Base class for protocol-only single-image/app-view extensions.

    A process extension may have its process window drawn by the application: it describes its parameters as a
    ``runtime`` extension does - ``get_params()``, its look stated by ``afternight.ui_protocol.process_window_meta()``
    - and the application draws them as a panel in a task page (``capabilities.interface = "panel"``). The Lite host
    then runs ``execute_preview()`` for each frame of the preview and ``execute()`` for an Apply over the images the
    application lends, tells ``on_parameters_changed()`` what a person changed, and asks ``handle_param_action()``,
    when the extension has one, what an action button sets.
    """

    def on_parameters_changed(self, params, changed_keys, reason):
        """Optional hook called when a person changes the parameters of the panel the application draws."""

    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):
        """Write a frame of the preview, ``preview_image`` from ``src_image``, for the panel the application draws."""
        raise NotImplementedError

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, output_masks=None):
        """Write the result of an Apply, ``dst_image`` from ``src_image``, for the panel the application draws."""
        raise NotImplementedError


class LiteWorkflowExtension(LiteExtension):
    """Base class for protocol-only workflow extensions."""


def _lent_images(tokens):
    return {str(name): ImageHandle(token=str(token)) for name, token in dict(tokens or {}).items()}


def _serve_panel_request(instance, kind, source_token, output_token, params, view_name="", masks=None,
                         weights=None):
    """Run one request of the panel the application draws, as the Lite host serves it: a frame of the preview
    (``kind`` "preview") through ``execute_preview()``, an Apply through ``execute()``, over the images the request
    lent, bound to their tokens by the host. Answers "succeeded", or "cancelled" when the request was cancelled while it
    ran; raises what the extension raised."""
    if instance is None:
        raise RuntimeError("No extension is loaded")
    progress = Progress()
    target = ViewRef(view_name=str(view_name or ""))
    source = ImageHandle(token=str(source_token))
    output = ImageHandle(token=str(output_token))
    extra = {}
    if masks:
        extra["masks"] = _lent_images(masks)
    if weights:
        extra["weights"] = _lent_images(weights)
    if kind == "preview":
        instance.execute_preview(target, source, output, dict(params or {}), progress, **extra)
    elif kind == "execute":
        instance.execute(target, source, output, dict(params or {}), progress, **extra)
    else:
        raise ValueError(f"A panel's request is a preview or an execute, not {kind!r}")
    return "cancelled" if progress.is_cancelled() else "succeeded"


def _serve_panel_action(instance, action_id, params):
    """Answer an action pressed in the panel the application draws: the values ``handle_param_action()`` sets, over the
    image the application shows. An extension without the hook answers that it has no actions."""
    handler = getattr(instance, "handle_param_action", None)
    if not callable(handler):
        raise RuntimeError("The extension does not implement parameter actions")
    try:
        view = _snapshot_view()
        source = view.source_image
    except Exception:
        view, source = ViewRef(), None
    result = handler(str(action_id), view, source, dict(params or {}))
    if not isinstance(result, dict):
        raise TypeError("handle_param_action() must return a dict")
    return result


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
