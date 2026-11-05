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
    commit_image,
    confirm,
    open_image,
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


def _coerce_size_pair(value, name):
    if value is None:
        return None

    if isinstance(value, dict):
        width = value.get("width")
        height = value.get("height")
    else:
        try:
            width, height = value
        except Exception as exc:
            raise ValueError(f"{name} must be a (width, height) pair") from exc

    width = int(width)
    height = int(height)
    if width <= 0 or height <= 0:
        raise ValueError(f"{name} values must be positive")

    return [width, height]


def process_window_meta(
    *,
    panel_variant="native",
    size=None,
    fixed_size=None,
    resizable=None,
    preview_surface=None,
    preview_area=None,
    sub_area=False,
    sub_area_default_enabled=None,
    sub_area_size=None,
    sub_area_label=None,
    controls_panel_width=None,
    preview_hq_default=None,
    preview_autostretch=None,
    preview_autostretch_default=None,
    header_progress=None,
    target_selector=False,
    target_channel_filter=None,
    target_selector_label=None,
    target_selector_position=None,
    tool_configuration=None,
):
    """Build the meta ParamDef consumed by the native process window."""

    if fixed_size is not None and resizable is not None:
        if bool(fixed_size) == bool(resizable):
            raise ValueError("fixed_size and resizable describe opposite states")

    meta = {
        "id": "window_meta",
        "type": "meta",
    }
    if panel_variant is not None:
        meta["panel_variant"] = str(panel_variant)

    window_size = _coerce_size_pair(size, "size")
    if window_size is not None:
        meta["window_size"] = window_size

    if fixed_size is not None:
        meta["fixed_size"] = bool(fixed_size)
    if resizable is not None:
        meta["resizable"] = bool(resizable)

    if preview_surface is not None:
        meta["preview_surface"] = bool(preview_surface)
    if preview_area is not None:
        meta["preview_area"] = bool(preview_area)

    if sub_area:
        meta["sub_area"] = True
    if sub_area_default_enabled is not None:
        meta["sub_area_default_enabled"] = bool(sub_area_default_enabled)
    area_size = _coerce_size_pair(sub_area_size, "sub_area_size")
    if area_size is not None:
        meta["sub_area_size"] = area_size
    if sub_area_label is not None:
        meta["sub_area_label"] = str(sub_area_label)

    if controls_panel_width is not None:
        controls_panel_width = int(controls_panel_width)
        if controls_panel_width <= 0:
            raise ValueError("controls_panel_width must be positive")
        meta["controls_panel_width"] = controls_panel_width
    if preview_hq_default is not None:
        meta["preview_hq_default"] = bool(preview_hq_default)
    if preview_autostretch is not None:
        meta["preview_autostretch"] = bool(preview_autostretch)
    if preview_autostretch_default is not None:
        meta["preview_autostretch_default"] = bool(preview_autostretch_default)
    if header_progress is not None:
        meta["header_progress"] = bool(header_progress)

    if target_selector_label is not None:
        meta["target_selector_label"] = str(target_selector_label)
        target_selector = True
    if target_selector_position is not None:
        meta["target_selector_position"] = str(target_selector_position)
        target_selector = True
    if target_channel_filter is not None:
        meta["target_channel_filter"] = [int(value) for value in target_channel_filter]
        target_selector = True
    if target_selector:
        meta["target_selector"] = True

    if tool_configuration is not None:
        meta["tool_configuration"] = dict(tool_configuration)

    return meta


def hide_process_window():
    if _host_available():
        return bool(_host_call("process_window_hide"))
    return False


def close_process_window():
    if _host_available():
        return bool(_host_call("process_window_close"))
    return False


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
