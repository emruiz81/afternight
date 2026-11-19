# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe UI/app result helpers for lite hosted extensions, the calls that describe a process window, and what
an extension may ask of the view its session is bound to.

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


# ---- What an extension may do to the view its session is bound to
#
# Each call is a request the application validates and may refuse: it acts on the view the session is bound to - for an
# extension drawn in a page, the shapes and the display on the page's canvas, the selection area and the history on the
# task's target - and a session bound to no view is refused. A refusal raises RuntimeError with the application's reason; with no host every call does
# nothing. The application answers at once and waits for nothing: what the view became reaches the extension as any
# change of its view does.

#: The most shapes a session may keep over its view, the most points they may hold together, and the longest label.
OVERLAY_SHAPE_LIMIT = 4096
OVERLAY_POINT_LIMIT = 262144
OVERLAY_LABEL_LIMIT = 120


def _color_text(color):
    if isinstance(color, str):
        return color
    values = [int(value) for value in color]
    if len(values) not in (3, 4) or any(value < 0 or value > 255 for value in values):
        raise ValueError("a colour is '#rrggbb', '#rrggbbaa' or (r, g, b[, a]) of 0 to 255")
    return "#" + "".join(f"{value:02x}" for value in values)


def _shape_fields(shape):
    if not isinstance(shape, dict):
        raise TypeError("a shape is a mapping")
    points = []
    for point in shape.get("points", ()):
        x, y = point
        points.extend((float(x), float(y)))
    fields = {
        "kind": "polygon" if shape.get("closed", False) else "line",
        "points": points,
        "color": _color_text(shape.get("color", "#ffd54a")),
    }
    if shape.get("filled", False):
        fields["filled"] = True
    if shape.get("label"):
        fields["label"] = str(shape["label"])
    return fields


def _view_request(kind, fields):
    if not _host_available():
        return None
    return dict(_host_call("view_request", kind, fields) or {})


def overlay_add(shapes):
    """Draw shapes over the view: each a mapping of ``points`` (the image's pixels, (x, y) pairs, at least one),
    ``closed`` (a polygon; an open line otherwise), ``filled`` (a polygon only), ``color`` (``"#rrggbb"``,
    ``"#rrggbbaa"`` or an (r, g, b[, a]) tuple; a yellow by default) and ``label`` (drawn beside the shape). Returns the
    ids the application gave the shapes, in order; none with no host. The shapes are the session's: they go when it
    ends."""
    answer = _view_request("view.overlay", {"add": [_shape_fields(shape) for shape in shapes]})
    return [] if answer is None else [int(shape_id) for shape_id in answer.get("ids", [])]


def overlay_remove(ids):
    """Take away shapes the session drew, by the ids ``overlay_add()`` answered. Returns how many shapes the session
    keeps; 0 with no host."""
    answer = _view_request("view.overlay", {"remove": [int(shape_id) for shape_id in ids]})
    return 0 if answer is None else int(answer.get("shape_count", 0))


def overlay_clear():
    """Take away every shape the session drew."""
    answer = _view_request("view.overlay", {"clear": True})
    return answer is not None


def set_selection(rect=None, enabled=True):
    """Turn the view's selection area on - set to ``rect`` (x, y, width, height in the image's pixels, kept inside the
    image), or, with none, drawn by the next drag on the image - or off with ``enabled=False``. It is the view's one
    area, the one a crop and a preview's part use: refused while another process holds it. A person moves and sizes
    it; the extension is told the rectangle when the mouse is released, in its view's state. Returns the rectangle the
    area has now, or None while none is drawn (or with no host)."""
    fields = {"enabled": bool(enabled)}
    if rect is not None:
        fields["rect"] = [int(value) for value in rect]
    answer = _view_request("view.selection", fields)
    if not answer or "rect" not in answer:
        return None
    return tuple(int(value) for value in answer["rect"])


def set_display(*, zoom=None, center=None, fit=False, mode=None):
    """Set how the view is shown: ``zoom`` in percent, ``center`` a point of the image to centre, ``fit`` the whole image
    fitted to the view (never with a zoom or a centre), ``mode`` a display mode by the name the view's state reports
    (``"linear"``, ``"autostretch"``). Returns the zoom the view took, in percent; None with no host."""
    fields = {}
    if zoom is not None:
        fields["zoom"] = float(zoom)
    if center is not None:
        x, y = center
        fields["center"] = [float(x), float(y)]
    if fit:
        fields["fit"] = True
    if mode is not None:
        fields["mode"] = str(mode)
    answer = _view_request("view.display", fields)
    return None if answer is None else float(answer.get("zoom", 0.0))


def undo():
    """Ask for the view's Undo, as its action does: refused with nothing to undo, while a run holds the view and until
    the session's own commit has landed. The view becomes the step before when it is read; no request clears a
    history."""
    return _view_request("view.history", {"action": "undo"}) is not None


def redo():
    """Ask for the view's Redo, as ``undo()`` asks for its Undo."""
    return _view_request("view.history", {"action": "redo"}) is not None


__all__ = [
    "OVERLAY_LABEL_LIMIT",
    "OVERLAY_POINT_LIMIT",
    "OVERLAY_SHAPE_LIMIT",
    "THEME_STATE_NAMES",
    "close_process_window",
    "commit_image",
    "confirm",
    "hide_process_window",
    "open_image",
    "overlay_add",
    "overlay_clear",
    "overlay_remove",
    "process_window_meta",
    "redo",
    "set_display",
    "set_selection",
    "show_error",
    "show_info",
    "show_warning",
    "tokens",
    "undo",
]
