# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Engine-backed base classes and runtime bridge helpers for full hosted extensions."""

from .protocol import (
    ExtensionContext,
    Settings,
    UnsupportedBackendError,
    _coerce_extension_context,
    _selected_sdk_backend,
    _set_runtime_image_adapter,
)


def _require_runtime_module():
    if _selected_sdk_backend() in {"protocol", "rpc"}:
        raise UnsupportedBackendError(
            "The selected lite SDK backend does not expose direct AfterNight Engine-backed modules. "
            "Use sdk_backend='runtime' for GPL-compatible full hosted extensions, or use "
            "the protocol-safe app/view APIs with your own processing code."
        )

    try:
        import _afternight_runtime as runtime_module  # type: ignore
    except (
        Exception
    ) as exc:  # pragma: no cover - import depends on host backend loading
        raise RuntimeError(
            "AfterNight runtime backend engine bindings are unavailable"
        ) from exc
    return runtime_module


def _runtime_image_from_numpy(array, metadata):
    return _require_runtime_module().image_from_numpy(array, metadata or {})


_set_runtime_image_adapter(_runtime_image_from_numpy)


class ProcessExtension:
    def __init__(self, context: ExtensionContext | Settings | None):
        self.context = _coerce_extension_context(context)
        self.settings = self.context.settings

    def get_settings_params(self):
        return []

    def handle_settings_action(self, action_id, settings_snapshot):
        return {
            "ok": False,
            "message": f"Unsupported settings action: {action_id}",
            "tone": "warning",
        }

    def get_params(self):
        return []

    def on_process_launch(self):
        """Optional lifecycle hook called once when the native process window opens."""

    def on_process_close(self, reason):
        """Optional lifecycle hook called once when the native process window closes."""

    def on_parameters_changed(self, params, changed_keys, reason):
        """Optional hook called after native parameter values change."""

    def validate_process(self, params, input_metadata):
        """Return optional diagnostics before native preview/apply execution."""
        return []

    def on_input_changed(self, input_metadata):
        """Optional hook called when the native process input image/view changes."""

    def on_execution_cancelled(self):
        """Optional hook called when native execution cancellation is requested."""

    def on_preset_loaded(self, params, preset_metadata):
        """Optional hook called after a native process preset is loaded."""

    def on_preset_saved(self, params, preset_metadata):
        """Optional hook called after a native process preset is saved."""

    def execute(
        self,
        target,
        src_image,
        dst_image,
        params,
        progress,
        masks=None,
        weights=None,
        output_masks=None,
    ):
        raise NotImplementedError


class RTPreviewExtension(ProcessExtension):
    def execute_preview(
        self,
        target,
        src_image,
        preview_image,
        params,
        progress,
        masks=None,
        weights=None,
    ):
        raise NotImplementedError


class WorkflowExtension:
    def __init__(self, context: ExtensionContext | Settings | None):
        self.context = _coerce_extension_context(context)
        self.settings = self.context.settings

    def get_settings_params(self):
        return []

    def handle_settings_action(self, action_id, settings_snapshot):
        return {
            "ok": False,
            "message": f"Unsupported settings action: {action_id}",
            "tone": "warning",
        }

    def get_params(self):
        return []

    def on_process_launch(self):
        """Optional lifecycle hook called once when the native workflow window opens."""

    def on_process_close(self, reason):
        """Optional lifecycle hook called once when the native workflow window closes."""

    def on_parameters_changed(self, params, changed_keys, reason):
        """Optional hook called after native workflow parameter values change."""

    def validate_process(self, params, input_metadata):
        """Return optional diagnostics before native workflow execution."""
        return []

    def on_input_changed(self, input_metadata):
        """Optional hook called when the native workflow input context changes."""

    def on_execution_cancelled(self):
        """Optional hook called when native execution cancellation is requested."""

    def on_preset_loaded(self, params, preset_metadata):
        """Optional hook called after a native workflow preset is loaded."""

    def on_preset_saved(self, params, preset_metadata):
        """Optional hook called after a native workflow preset is saved."""

    def execute_workflow(self, workflow, params, progress):
        raise NotImplementedError


__all__ = [
    "ProcessExtension",
    "RTPreviewExtension",
    "WorkflowExtension",
    "_require_runtime_module",
]
