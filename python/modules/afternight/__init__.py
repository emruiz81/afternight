# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Host-shipped AfterNight Python package scaffold for the extension SDK."""

import importlib as _importlib

from .protocol import (
    ExtensionContext,
    ImageHandle,
    Progress,
    SessionInfo,
    SessionPaths,
    Settings,
    UnsupportedBackendError,
    ViewRef,
    WorkflowContext,
    WorkflowError,
    WorkflowProgress,
    _card_metadata,
    _coerce_extension_context,
    _coerce_to_runtime_image,
    _host_available,
    _host_call,
    _host_has_control_socket,
    _selected_sdk_backend,
    _wrap_runtime_image,
    current_session,
    emit_settings_action_update,
    list_views,
    log,
    log_debug,
    log_error,
    log_info,
    log_trace,
    log_warning,
    session_paths,
    snapshot_view,
)


__version__ = "1.0.0"


_RUNTIME_EXPORTS = {
    "ProcessExtension",
    "RTPreviewExtension",
    "WorkflowExtension",
    "_require_runtime_module",
}

_LAZY_SUBMODULES = {
    "calibration",
    "core",
    "io",
    "lite",
    "protocol",
    "registration",
    "runtime",
    "sequences",
    "stacking",
    "ui",
    "ui_protocol",
}


def __getattr__(name):
    if name in _RUNTIME_EXPORTS:
        module = _importlib.import_module(f"{__name__}.runtime")
        value = getattr(module, name)
        globals()[name] = value
        return value
    if name in _LAZY_SUBMODULES:
        module = _importlib.import_module(f"{__name__}.{name}")
        globals()[name] = module
        return module
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = [
    "__version__",
    "ExtensionContext",
    "ImageHandle",
    "ProcessExtension",
    "Progress",
    "RTPreviewExtension",
    "SessionInfo",
    "SessionPaths",
    "Settings",
    "UnsupportedBackendError",
    "ViewRef",
    "WorkflowContext",
    "WorkflowError",
    "WorkflowExtension",
    "WorkflowProgress",
    "calibration",
    "core",
    "current_session",
    "emit_settings_action_update",
    "io",
    "list_views",
    "lite",
    "log",
    "log_debug",
    "log_error",
    "log_info",
    "log_trace",
    "log_warning",
    "protocol",
    "registration",
    "runtime",
    "sequences",
    "session_paths",
    "snapshot_view",
    "stacking",
    "ui",
    "ui_protocol",
]
