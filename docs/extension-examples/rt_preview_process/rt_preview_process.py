# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
from afternight import ui


class ExampleRTPreviewProcess(ui.RTPreviewProcess):
    def get_settings_params(self):
        return [
            {
                "id": "preview_label",
                "type": "string",
                "label": "Preview Label",
                "default": "Example RT Preview",
            }
        ]

    def get_params(self):
        return [
            ui.process_window_meta(
                panel_variant="native",
                sub_area=True,
                sub_area_size=[800, 600],
                sub_area_label="Preview Area",
                preview_autostretch=True,
                preview_autostretch_default=True,
                target_selector=True,
            ),
            {
                "id": "adjustment",
                "type": "section",
                "label": "Adjustment",
            },
            {
                "id": "strength",
                "type": "float",
                "label": "Strength",
                "default": 0.35,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
            {
                "id": "black_point",
                "type": "float",
                "label": "Black Point",
                "default": 0.0,
                "min": -0.25,
                "max": 0.25,
                "step": 0.01,
            },
            {
                "id": "mode",
                "type": "choice",
                "label": "Mode",
                "default": "balanced",
                "options": [
                    ["Balanced", "balanced"],
                    ["Brighten", "brighten"],
                    ["Darken", "darken"],
                ],
            },
        ]

    def _scale_offset(self, params):
        strength = float(params.get("strength", 0.35))
        black_point = float(params.get("black_point", 0.0))
        mode = str(params.get("mode", "balanced"))

        if mode == "brighten":
            return 1.0 + strength * 0.35, strength * 0.20 + black_point
        if mode == "darken":
            return 1.0 - strength * 0.30, -strength * 0.10 + black_point
        return 1.0 + strength * 0.20, -strength * 0.08 + black_point

    def _apply(self, src_image, dst_image, params):
        scale, offset = self._scale_offset(params)
        afternight.core.apply_linear_transform(src_image, dst_image, scale, offset)
        return scale, offset

    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None,
                        weights=None):
        del target, masks, weights
        label = self.settings.get("preview_label", "Example RT Preview")
        progress.set_text(f"Rendering {label}")
        scale, offset = self._apply(src_image, preview_image, params)
        afternight.log_debug(
            f"Preview rendered with scale={scale:.3f} offset={offset:.3f}",
            component="example.rt_preview",
        )

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        del target, masks, weights, output_masks
        progress.set_text("Applying previewed adjustment")
        scale, offset = self._apply(src_image, dst_image, params)
        dst_image.set_metadata("example.rt_preview.scale", scale)
        dst_image.set_metadata("example.rt_preview.offset", offset)
