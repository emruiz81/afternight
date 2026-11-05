# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Bundled standalone workflow reference extension — own PySide6 dialog + manual commit."""

import afternight
from PySide6 import QtCore, QtWidgets
from afternight import ui


def _finite_array(array):
    import numpy as np

    return np.nan_to_num(np.array(array, copy=False), nan=0.0, posinf=0.0, neginf=0.0)


def _unit_float_array(array):
    import numpy as np

    result = _finite_array(array)
    if np.issubdtype(result.dtype, np.integer):
        info = np.iinfo(result.dtype)
        scale = float(max(abs(int(info.min)), abs(int(info.max)), 1))
        return result.astype(np.float32) / scale
    return result.astype(np.float32, copy=False)


def _working_source_array(source_image):
    import numpy as np

    source = _finite_array(source_image)
    if np.issubdtype(source.dtype, np.integer):
        info = np.iinfo(source.dtype)
        scale = float(max(abs(int(info.min)), abs(int(info.max)), 1))
        return source.astype(np.float32) / scale, scale, source.dtype
    return source.astype(np.float32, copy=False), 1.0, source.dtype


def _restore_source_array_scale(result, source_scale, source_dtype):
    import numpy as np

    restored = np.nan_to_num(result, nan=0.0, posinf=0.0, neginf=0.0)
    if np.issubdtype(source_dtype, np.integer):
        restored = np.clip(restored, 0.0, 1.0) * source_scale
        return restored.astype(source_dtype, copy=False)
    return restored.astype(np.float32, copy=False)


def _mono_or_matching_array(image_handle, source_array):
    if image_handle is None:
        return None

    array = _unit_float_array(image_handle.to_numpy())
    if source_array.ndim == 3 and array.ndim == 2:
        return array[:, :, None]
    return array


class StandalonePreviewDemoExtension(ui.ProcessWindow):
    def __init__(self, context):
        super().__init__(context)
        self._dialog = None
        self._view_label = None
        self._status_label = None
        self._execute_button = None
        self._strength_spin = None
        self._lift_spin = None
        self._mode_combo = None
        self._apply_mask_checkbox = None
        self._apply_weight_checkbox = None
        self._view_poll_timer = None
        self._views_list = None
        self._closing = False

        if self.context.session_info().session_id:
            self._create_dialog()

    def get_settings_params(self):
        return [
            {
                "id": "window_title",
                "type": "string",
                "label": "Window Title",
                "default": "Standalone Workflow Demo",
            }
        ]

    def get_params(self):
        return [
            {
                "id": "window_meta",
                "type": "meta",
                "panel_variant": "native",
                "preview_surface": False,
            },
            {
                "id": "standalone_section",
                "type": "section",
                "label": "Standalone Window",
            },
            {
                "id": "standalone_summary",
                "type": "info",
                "tone": "info",
                "text": (
                    "At runtime this demo hides the stock process shell, opens "
                    "its own PySide6 dialog, reads the active image through "
                    "ViewRef, and commits the result back with ui.commit_image()."
                ),
            },
        ]

    def _gamma(self, params):
        strength = float(params.get("strength", 0.45))
        mode = str(params.get("mode", "balanced"))

        gamma = 1.0 / (1.0 + strength)
        if mode == "contrast":
            gamma *= 0.85
        elif mode == "gentle":
            gamma = 1.0 / (1.0 + strength * 0.5)
        return gamma

    def _render_array(self, source_image, params, masks, weights):
        import numpy as np

        source, source_scale, source_dtype = _working_source_array(
            source_image.to_numpy() if hasattr(source_image, "to_numpy") else source_image
        )
        gamma = self._gamma(params)
        lift = float(params.get("lift", 0.05))

        clipped = np.clip(source, 0.0, None)
        result = np.power(clipped, gamma) + lift

        if bool(params.get("apply_mask", True)) and masks and "selection" in masks:
            selection = _mono_or_matching_array(masks["selection"], source)
            if selection is not None:
                extra_glow = np.power(clipped, gamma * 0.7) - np.power(clipped, gamma)
                strength = float(params.get("strength", 0.45))
                result += selection * extra_glow * (0.3 + strength * 0.5)

        if bool(params.get("apply_weight", True)) and weights and "weight_map" in weights:
            weight_map = _mono_or_matching_array(weights["weight_map"], source)
            if weight_map is not None:
                strength = float(params.get("strength", 0.45))
                result += weight_map * strength * 0.15

        return _restore_source_array_scale(result, source_scale, source_dtype)

    def _collect_params(self):
        if (
            self._strength_spin is None
            or self._lift_spin is None
            or self._mode_combo is None
            or self._apply_mask_checkbox is None
            or self._apply_weight_checkbox is None
        ):
            raise RuntimeError("Standalone dialog controls are not initialized")

        return {
            "strength": self._strength_spin.value(),
            "lift": self._lift_spin.value(),
            "mode": self._mode_combo.currentData(),
            "apply_mask": self._apply_mask_checkbox.isChecked(),
            "apply_weight": self._apply_weight_checkbox.isChecked(),
        }

    def _current_view(self):
        return ui.ImageView()

    def _set_status(self, text):
        if self._status_label is not None:
            self._status_label.setText(text)

    def _set_busy(self, busy, text):
        if self._execute_button is not None:
            self._execute_button.setEnabled(not busy and self._current_view().source_image is not None)
        if self._strength_spin is not None:
            self._strength_spin.setEnabled(not busy)
        if self._lift_spin is not None:
            self._lift_spin.setEnabled(not busy)
        if self._mode_combo is not None:
            self._mode_combo.setEnabled(not busy)
        if self._apply_mask_checkbox is not None:
            self._apply_mask_checkbox.setEnabled(not busy)
        if self._apply_weight_checkbox is not None:
            self._apply_weight_checkbox.setEnabled(not busy)
        self._set_status(text)

    def _refresh_view_state(self):
        view = self._current_view()
        source_image = view.source_image
        session = afternight.current_session()

        # Refresh open views list.
        if self._views_list is not None:
            self._views_list.clear()
            try:
                views = afternight.list_views()
                for v in views:
                    name = v.get("name", "?")
                    w = v.get("width", 0)
                    h = v.get("height", 0)
                    ch = v.get("channels", 0)
                    active = " [active]" if v.get("is_active") else ""
                    mask_info = ""
                    if v.get("has_mask"):
                        mask_state = "enabled" if v.get("mask_enabled") else "disabled"
                        mask_info = f"  mask:{mask_state}"
                    self._views_list.addItem(f"{name} ({w}x{h}, {ch}ch{mask_info}){active}")
            except Exception:
                self._views_list.addItem("(unable to query views)")

        if source_image is None:
            if self._view_label is not None:
                self._view_label.setText("Waiting for the active image to arrive from AfterNight.")
            if self._execute_button is not None:
                self._execute_button.setEnabled(False)
            return

        if self._view_label is not None:
            self._view_label.setText(
                f"Active view: {session.view_name or 'Current View'} "
                f"({view.width} x {view.height}, {view.channels} channel{'s' if view.channels != 1 else ''})"
            )
        if self._execute_button is not None:
            self._execute_button.setEnabled(True)

    def _run_dialog_workflow(self):
        view = self._current_view()
        source_image = view.source_image
        if source_image is None:
            ui.show_warning("No active image is available yet.", title="Standalone Workflow Demo")
            return

        params = self._collect_params()
        self._set_busy(True, "Rendering the standalone result and committing it back to AfterNight...")

        try:
            result = source_image.clone()
            result.from_numpy(self._render_array(source_image, params, view.masks, view.weights))
            result.set_metadata("an.demo.standalone_preview.mode", str(params["mode"]))
            result.set_metadata(
                "an.demo.standalone_preview.apply_mask",
                bool(params["apply_mask"]),
            )
            result.set_metadata(
                "an.demo.standalone_preview.apply_weight",
                bool(params["apply_weight"]),
            )
            result.set_metadata("an.demo.standalone_preview.workflow", "standalone_dialog")

            ui.commit_image(
                result,
                history_step_name="Standalone Workflow Demo",
                history_step_description="Applied standalone extension-host dialog result",
            )
            afternight.log_info(
                (
                    "Standalone dialog committed result "
                    f"mode={params['mode']} strength={params['strength']:.2f}"
                ),
                component="an.demo.standalone_preview",
            )
            self._closing = True
            if self._dialog is not None:
                self._dialog.close()
        except Exception as exc:
            afternight.log_error(str(exc), component="an.demo.standalone_preview")
            self._set_busy(False, "Execution failed.")
            ui.show_error(str(exc), title="Standalone Workflow Demo")

    def _on_dialog_finished(self, _result):
        if self._view_poll_timer is not None:
            self._view_poll_timer.stop()
        self._closing = True
        ui.close_process_window()

    def _create_dialog(self):
        ui.hide_process_window()

        dialog = QtWidgets.QDialog()
        dialog.setWindowTitle(self.settings.get("window_title", "Standalone Workflow Demo"))
        dialog.setMinimumSize(460, 480)

        root = QtWidgets.QVBoxLayout(dialog)

        summary = QtWidgets.QLabel(
            "This reference plugin owns its own Qt dialog and execution button. "
            "It does not use execute_preview() and commits the processed result back "
            "to AfterNight explicitly through the SDK."
        )
        summary.setWordWrap(True)
        root.addWidget(summary)

        # --- Open views section (demonstrates afternight.list_views()) ---
        views_group = QtWidgets.QGroupBox("Open Views (via afternight.list_views())", dialog)
        views_layout = QtWidgets.QVBoxLayout(views_group)
        self._views_list = QtWidgets.QListWidget(dialog)
        self._views_list.setMaximumHeight(100)
        views_layout.addWidget(self._views_list)
        root.addWidget(views_group)

        self._view_label = QtWidgets.QLabel("Waiting for the active image to arrive from AfterNight.")
        self._view_label.setWordWrap(True)
        root.addWidget(self._view_label)

        form = QtWidgets.QFormLayout()

        self._strength_spin = QtWidgets.QDoubleSpinBox(dialog)
        self._strength_spin.setRange(0.0, 1.0)
        self._strength_spin.setSingleStep(0.05)
        self._strength_spin.setValue(0.45)
        form.addRow("Strength", self._strength_spin)

        self._lift_spin = QtWidgets.QDoubleSpinBox(dialog)
        self._lift_spin.setRange(-0.25, 0.35)
        self._lift_spin.setSingleStep(0.01)
        self._lift_spin.setValue(0.05)
        form.addRow("Lift", self._lift_spin)

        self._mode_combo = QtWidgets.QComboBox(dialog)
        self._mode_combo.addItem("Balanced", "balanced")
        self._mode_combo.addItem("Contrast", "contrast")
        self._mode_combo.addItem("Gentle", "gentle")
        form.addRow("Mode", self._mode_combo)

        root.addLayout(form)

        self._apply_mask_checkbox = QtWidgets.QCheckBox("Use selection mask when available", dialog)
        self._apply_mask_checkbox.setChecked(True)
        root.addWidget(self._apply_mask_checkbox)

        self._apply_weight_checkbox = QtWidgets.QCheckBox("Use weight map when available", dialog)
        self._apply_weight_checkbox.setChecked(True)
        root.addWidget(self._apply_weight_checkbox)

        self._status_label = QtWidgets.QLabel(
            "The custom dialog reads the active view through ui.ImageView() and commits with ui.commit_image()."
        )
        self._status_label.setWordWrap(True)
        root.addWidget(self._status_label)

        buttons = QtWidgets.QDialogButtonBox(dialog)
        self._execute_button = buttons.addButton("Execute", QtWidgets.QDialogButtonBox.AcceptRole)
        buttons.addButton(QtWidgets.QDialogButtonBox.Close)
        self._execute_button.clicked.connect(self._run_dialog_workflow)
        buttons.rejected.connect(dialog.close)
        root.addWidget(buttons)

        dialog.finished.connect(self._on_dialog_finished)

        self._view_poll_timer = QtCore.QTimer(dialog)
        self._view_poll_timer.setInterval(150)
        self._view_poll_timer.timeout.connect(self._refresh_view_state)
        self._view_poll_timer.start()

        self._dialog = dialog
        QtCore.QTimer.singleShot(0, self._refresh_view_state)
        dialog.show()
