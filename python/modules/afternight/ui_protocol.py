# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe UI/app result helpers for lite hosted extensions, and the calls that describe a process window.

An extension describes its process window as data - the list ``get_params()`` returns, its look stated by
``process_window_meta()`` - and the application draws it: a ``runtime`` extension's and a ``protocol`` one's alike,
since the description is protocol data. ``hide_process_window()`` and ``close_process_window()`` hide and close that
window, or the task the application shows it as.
"""

from .protocol import _host_available, _host_call


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
    """Hide the process window, or put away the task the application draws it as; it stays and is shown again as any
    task is. False without a host, or with a host that has no window and draws no panel."""
    if _host_available():
        return bool(_host_call("process_window_hide"))
    return False


def close_process_window():
    """Close the process window, or the task the application draws it as: the session ends. False without a host."""
    if _host_available():
        return bool(_host_call("process_window_close"))
    return False


def open_image(image_or_array, title="Extension Image", metadata=None):
    if not _host_available():
        return False

    array = image_or_array
    if hasattr(image_or_array, "to_numpy"):
        array = image_or_array.to_numpy()
        if metadata is None:
            try:
                metadata = dict(image_or_array.metadata or {})
            except Exception:
                metadata = {}

    return bool(
        _host_call(
            "ui_open_image",
            array,
            dict(metadata or {}),
            str(title),
        )
    )


def commit_image(image_or_array,
                 history_step_name="Extension Apply",
                 history_step_description="Applied extension execution result",
                 metadata=None,
                 allow_resized_output=False):
    if not _host_available():
        return False

    array = image_or_array
    if hasattr(image_or_array, "to_numpy"):
        array = image_or_array.to_numpy()
        if metadata is None:
            try:
                metadata = dict(image_or_array.metadata or {})
            except Exception:
                metadata = {}

    return bool(
        _host_call(
            "view_commit_numpy",
            array,
            dict(metadata or {}),
            str(history_step_name),
            str(history_step_description),
            bool(allow_resized_output),
        )
    )


#: The names of the entries ``tokens()`` answers, in the order the application states them: the choices in force -
#: whether the theme is dark, the style profile, the contrast profile, the accent, the surface tint, its strength, the
#: preset they make, the density and Night - and the tokens they resolve to - the colours, the surface ladder, the
#: sizes, the spacing, the radii and the type. A host that was sent no theme answers none of them.
THEME_STATE_NAMES = (
    "is_dark",
    "style_profile",
    "contrast_profile",
    "accent_family",
    "surface_tint_family",
    "tint_strength",
    "preset",
    "density",
    "night",
    "colors",
    "layers",
    "metrics",
    "spacing",
    "radius",
    "typography",
)


def tokens():
    """The application's theme state as the host was last sent it, by the names of ``THEME_STATE_NAMES``; empty
    without a host, or before a host is sent one."""
    if not _host_available():
        return {}
    try:
        return dict(_host_call("theme_tokens") or {})
    except Exception:
        return {}


def show_error(message, title="Extension Error"):
    if _host_available():
        return _host_call("ui_show_error", title, str(message))
    return None


def show_warning(message, title="Extension Warning"):
    if _host_available():
        return _host_call("ui_show_warning", title, str(message))
    return None


def show_info(message, title="Extension Message"):
    if _host_available():
        return _host_call("ui_show_info", title, str(message))
    return None


def confirm(message, title="Extension Confirmation", warn=False):
    if _host_available():
        return bool(_host_call("ui_confirm", title, str(message), bool(warn)))
    return False


__all__ = [
    "THEME_STATE_NAMES",
    "close_process_window",
    "commit_image",
    "confirm",
    "hide_process_window",
    "open_image",
    "process_window_meta",
    "show_error",
    "show_info",
    "show_warning",
    "tokens",
]
