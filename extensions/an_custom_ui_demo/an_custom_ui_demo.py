# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Bundled custom UI reference extension — iterative film fade effect."""

import afternight
from afternight import ui


class CustomUiDemoExtension(ui.ProcessWindow):
    def get_settings_params(self):
        return [
            {
                "id": "default_output_name",
                "type": "string",
                "label": "Default Output Name",
                "default": "Custom UI Demo",
            }
        ]

    def get_params(self):
        return [
            {
                "id": "window_meta",
                "type": "meta",
                "panel_variant": "custom",
            },
            {
                "id": "panel_banner",
                "type": "section",
                "label": "Film Fade Studio",
            },
            {
                "id": "panel_summary",
                "type": "info",
                "tone": "warning",
                "text": (
                    "This demo intentionally leans into a more editorial, studio "
                    "style layout. It is execute-only, uses the same native "
                    "AfterNight controls under the hood, and should feel clearly "
                    "different from the standard process panels."
                ),
            },
            {
                "id": "strength",
                "type": "double",
                "label": "Fade Amount",
                "default": 0.5,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
            {
                "id": "iterations",
                "type": "int",
                "label": "Layer Passes",
                "default": 4,
                "min": 1,
                "max": 12,
                "step": 1,
            },
            {
                "id": "sigma",
                "type": "double",
                "label": "Glow Radius",
                "default": 1.2,
                "min": 0.1,
                "max": 5.0,
                "step": 0.1,
            },
            {
                "id": "use_mask",
                "type": "bool",
                "label": "Use Mask",
                "default": True,
            },
            {
                "id": "mode",
                "type": "choice",
                "label": "Render Strategy",
                "default": "fast",
                "options": [
                    ["Fast", "fast"],
                    ["Quality", "quality"],
                ],
            },
            {
                "id": "output_section",
                "type": "section",
                "label": "Delivery",
            },
            {
                "id": "output_hint",
                "type": "info",
                "tone": "info",
                "text": (
                    "Output fields stay in the panel so the demo still exercises "
                    "typed parameter capture and settings-backed defaults, but "
                    "without any RT preview surface."
                ),
            },
            {
                "id": "output_name",
                "type": "string",
                "label": "Rendered Name",
                "default": self.settings.get("default_output_name", "Custom UI Demo"),
                "placeholder": "Film fade render",
            },
            {
                "id": "export_dir",
                "type": "string",
                "label": "Export Folder",
                "default": "/tmp/output",
            },
            {
                "id": "quality_preset",
                "type": "choice",
                "label": "Preset",
                "default": "balanced",
                "options": [
                    ["Fast", "fast"],
                    ["Balanced", "balanced"],
                    ["Best", "best"],
                ],
            },
        ]

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        strength = float(params.get("strength", 0.5))
        sigma = float(params.get("sigma", 1.2))
        iterations = int(params.get("iterations", 4))
        use_mask = bool(params.get("use_mask", True))
        mode = str(params.get("mode", "fast"))
        output_name = str(params.get("output_name", "Custom UI Demo")).strip() or "Custom UI Demo"
        export_dir = str(params.get("export_dir", "/tmp/output")).strip() or "/tmp/output"
        quality_preset = str(params.get("quality_preset", "balanced"))

        effective_passes = iterations if mode == "quality" else 1

        progress.set_text(
            f"Applying {quality_preset} film fade ({effective_passes} pass{'es' if effective_passes != 1 else ''})"
        )

        per_pass = strength * 0.15 if mode == "quality" else strength * 0.3
        if quality_preset == "fast":
            per_pass *= 0.8
        elif quality_preset == "best":
            per_pass *= 1.2

        warmth = (sigma - 1.0) * 0.02

        scale = 1.0 - per_pass
        offset = per_pass * 0.6 + warmth
        if use_mask:
            offset *= 0.7

        if effective_passes == 1:
            total_scale = scale
            total_offset = offset
        else:
            total_scale = scale ** effective_passes
            if abs(scale - 1.0) > 1e-12:
                total_offset = offset * (1.0 - scale ** effective_passes) / (1.0 - scale)
            else:
                total_offset = offset * effective_passes

        afternight.core.apply_linear_transform(src_image, dst_image, total_scale, total_offset)
        progress.set_value(100)

        dst_image.set_metadata("an.demo.custom_ui.output_name", output_name)
        dst_image.set_metadata("an.demo.custom_ui.export_dir", export_dir)
        dst_image.set_metadata("an.demo.custom_ui.quality_preset", quality_preset)
        dst_image.set_metadata("an.demo.custom_ui.total_passes", effective_passes)

        afternight.log_info(
            f"Film fade applied: strength={strength:.2f} sigma={sigma:.1f} "
            f"passes={effective_passes} preset={quality_preset}",
            component="an.demo.custom_ui",
        )
