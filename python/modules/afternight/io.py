# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Runtime-backed image I/O helpers for the public AfterNight SDK."""

from . import _coerce_to_runtime_image, _require_runtime_module, _wrap_runtime_image


def load(path, load_metadata=True):
    runtime_module = _require_runtime_module()
    return _wrap_runtime_image(runtime_module.load_image(str(path), bool(load_metadata)))


def save(image, path, options=None):
    runtime_module = _require_runtime_module()
    runtime_module.save_image(_coerce_to_runtime_image(image), str(path), options)


def is_supported_path(path):
    runtime_module = _require_runtime_module()
    return bool(runtime_module.is_supported_path(str(path)))


__all__ = [
    "is_supported_path",
    "load",
    "save",
]
