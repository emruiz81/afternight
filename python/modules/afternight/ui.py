# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Native-process-window helpers plus protocol result helpers for afternight.ui."""

from . import (
    ExtensionContext,
    ImageHandle,
    ProcessExtension,
    RTPreviewExtension,
    Settings,
    ViewRef,
    WorkflowContext,
    WorkflowError,
    WorkflowExtension,
    WorkflowProgress,
    _host_available,
    _host_call,
    current_session,
    list_views,
    session_paths,
)
from .ui_protocol import (
    close_process_window,
    commit_image,
    confirm,
    hide_process_window,
    open_image,
    process_window_meta,
    show_error,
    show_info,
    show_warning,
    tokens,
)


ImageView = ViewRef


class ProcessWindow(ProcessExtension):
    """Base for extensions targeting the native process window."""


class RTPreviewProcess(RTPreviewExtension):
    """Base for preview-capable native process window extensions."""


class WorkflowProcess(WorkflowExtension):
    """Base for workflow extensions targeting the native process window."""


__all__ = [
    "ExtensionContext",
    "ImageHandle",
    "ImageView",
    "ProcessWindow",
    "RTPreviewProcess",
    "RTPreviewExtension",
    "Settings",
    "ViewRef",
    "WorkflowContext",
    "WorkflowError",
    "WorkflowExtension",
    "WorkflowProcess",
    "WorkflowProgress",
    "close_process_window",
    "commit_image",
    "confirm",
    "current_session",
    "hide_process_window",
    "list_views",
    "open_image",
    "process_window_meta",
    "session_paths",
    "show_error",
    "show_info",
    "show_warning",
    "tokens",
]
