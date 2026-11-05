# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
from PySide6 import QtCore, QtWidgets
from afternight import ui


class ExampleStandalonePreviewProcess(ui.ProcessWindow):
    def __init__(self, context):
        super().__init__(context)
        self._dialog = None
        self._status = None
        self._strength = None

        if self.context.session_info().session_id:
            QtCore.QTimer.singleShot(0, self._open_dialog)

    def get_params(self):
        return [
            ui.process_window_meta(panel_variant="native", preview_surface=False),
            {
                "id": "standalone",
                "type": "section",
                "label": "Standalone Workflow",
            },
            {
                "id": "message",
                "type": "info",
                "tone": "info",
                "text": "This process hides the stock shell and opens an extension-owned dialog.",
            },
        ]

    def _render(self, source_image, strength):
        result = source_image.clone()
        afternight.core.apply_linear_transform(
            source_image,
            result,
            1.0 + strength * 0.35,
            -strength * 0.12,
        )
        result.set_metadata("example.standalone_preview.strength", strength)
        return result

    def _current_source(self):
        view = ui.ImageView()
        return view.source_image

    def _set_status(self, message):
        if self._status is not None:
            self._status.setText(message)

    def _preview(self):
        source = self._current_source()
        if source is None:
            ui.show_warning("No active image is available.", title="Standalone Preview")
            return

        strength = self._strength.value() if self._strength is not None else 0.35
        result = self._render(source, strength)
        ui.open_image(result, title="Standalone Preview Result")
        self._set_status("Preview opened as a separate image view.")

    def _commit(self):
        source = self._current_source()
        if source is None:
            ui.show_warning("No active image is available.", title="Standalone Preview")
            return

        strength = self._strength.value() if self._strength is not None else 0.35
        result = self._render(source, strength)
        ui.commit_image(
            result,
            history_step_name="Example Standalone Preview",
            history_step_description="Committed from an extension-owned dialog",
        )
        afternight.log_info(
            f"Standalone preview committed strength={strength:.2f}",
            component="example.standalone_preview",
        )
        self._set_status("Committed result to the active view.")

    def _finish(self, _result):
        ui.close_process_window()

    def _open_dialog(self):
        if self._dialog is not None:
            return

        ui.hide_process_window()

        dialog = QtWidgets.QDialog()
        dialog.setWindowTitle("Standalone Preview")
        dialog.setAttribute(QtCore.Qt.WA_DeleteOnClose, True)
        layout = QtWidgets.QVBoxLayout(dialog)

        self._status = QtWidgets.QLabel("Adjust strength, then preview or commit.", dialog)
        self._status.setWordWrap(True)
        layout.addWidget(self._status)

        self._strength = QtWidgets.QDoubleSpinBox(dialog)
        self._strength.setRange(0.0, 1.0)
        self._strength.setSingleStep(0.05)
        self._strength.setValue(0.35)
        layout.addWidget(self._strength)

        buttons = QtWidgets.QDialogButtonBox(dialog)
        preview_button = buttons.addButton("Preview", QtWidgets.QDialogButtonBox.ActionRole)
        commit_button = buttons.addButton("Commit", QtWidgets.QDialogButtonBox.AcceptRole)
        close_button = buttons.addButton(QtWidgets.QDialogButtonBox.Close)
        layout.addWidget(buttons)

        preview_button.clicked.connect(self._preview)
        commit_button.clicked.connect(self._commit)
        close_button.clicked.connect(dialog.close)
        dialog.finished.connect(self._finish)

        self._dialog = dialog
        dialog.open()

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        del target, params, masks, weights, output_masks
        progress.set_text("Applying standalone fallback execution")
        afternight.core.apply_linear_transform(src_image, dst_image, 1.0, 0.0)
