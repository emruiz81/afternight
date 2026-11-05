# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import pathlib

import afternight
from afternight import ui


class ExampleSettingsDiagnosticsProcess(ui.ProcessWindow):
    def get_settings_params(self):
        return [
            {
                "id": "default_strength",
                "type": "float",
                "label": "Default Strength",
                "default": 1.0,
                "min": 0.0,
                "max": 2.0,
                "step": 0.05,
            },
            {
                "id": "write_run_artifact",
                "type": "bool",
                "label": "Write Run Artifact",
                "default": True,
            },
        ]

    def get_params(self):
        return [
            ui.process_window_meta(panel_variant="native"),
            {
                "id": "run",
                "type": "section",
                "label": "Run",
            },
            {
                "id": "strength",
                "type": "float",
                "label": "Strength",
                "default": float(self.settings.get("default_strength", 1.0)),
                "min": 0.0,
                "max": 2.0,
                "step": 0.05,
            },
            {
                "id": "write_artifact",
                "type": "bool",
                "label": "Write Artifact",
                "default": bool(self.settings.get("write_run_artifact", True)),
            },
        ]

    def _write_artifact(self, session, target, strength):
        artifacts_dir = afternight.session_paths().artifacts_dir()
        if not artifacts_dir:
            return ""

        path = pathlib.Path(artifacts_dir) / "example_settings_diagnostics.txt"
        path.write_text(
            "\n".join(
                [
                    f"extension_id={session.extension_id}",
                    f"session_id={session.session_id}",
                    f"view_name={getattr(target, 'view_name', '')}",
                    f"file_name={getattr(target, 'file_name', '')}",
                    f"strength={strength:.3f}",
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        return str(path)

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        del masks, weights, output_masks
        strength = float(params.get("strength", self.settings.get("default_strength", 1.0)))
        session = afternight.current_session()

        progress.set_text("Applying settings-backed diagnostic example")
        if progress.is_cancelled():
            afternight.log_warning("Cancelled before processing started", component="example.diagnostics")
            return

        afternight.core.apply_linear_transform(src_image, dst_image, strength, 0.0)
        dst_image.set_metadata("example.settings_diagnostics.session_id", session.session_id)
        dst_image.set_metadata("example.settings_diagnostics.strength", strength)

        self.settings.set("last_strength", strength)
        self.settings.set("last_session_id", session.session_id)
        self.settings.set("last_view_name", getattr(target, "view_name", ""))

        artifact_path = ""
        if bool(params.get("write_artifact", self.settings.get("write_run_artifact", True))):
            artifact_path = self._write_artifact(session, target, strength)
            if artifact_path:
                dst_image.set_metadata("example.settings_diagnostics.artifact", artifact_path)

        progress.set_value(100)
        afternight.log_info(
            f"Settings diagnostic run finished strength={strength:.3f} artifact={artifact_path or 'none'}",
            component="example.diagnostics",
        )
