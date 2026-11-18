# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Protocol-safe UI/app result helpers for lite hosted extensions."""

from .protocol import _host_available, _host_call


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
    "commit_image",
    "confirm",
    "open_image",
    "show_error",
    "show_info",
    "show_warning",
    "tokens",
]
