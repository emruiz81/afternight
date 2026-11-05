# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Bundled RT preview reference extension — contrast adjustment with color modes."""

import afternight
from afternight import ui


class RTPreviewDemoExtension(ui.RTPreviewProcess):
    def get_settings_params(self):
        return [
            {
                "id": "preview_label",
                "type": "string",
                "label": "Preview Label",
                "default": "RT Preview Demo",
            }
        ]

    def get_params(self):
        return [
            ui.process_window_meta(
                panel_variant="native",
                sub_area=True,
                sub_area_size=[800, 600],
                sub_area_label="Preview: Contrast Demo",
                preview_autostretch=True,
                target_selector=True,
                target_channel_filter=[1, 3],
            ),
            {
                "id": "preview_banner",
                "type": "section",
                "label": "RT Preview",
            },
            {
                "id": "preview_summary",
                "type": "info",
                "tone": "info",
                "text": (
                    "This is the single real-time preview demo. Change the "
                    "sliders and the preview refreshes automatically. Use the "
                    "Preview area checkbox in the RT preview toolbar to limit "
                    "processing to a selected ROI."
                ),
            },
            {
                "id": "strength",
                "type": "float",
                "label": "Contrast Strength",
                "default": 0.45,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
            {
                "id": "lift",
                "type": "float",
                "label": "Black Point Lift",
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
                    ["Punchy", "punchy"],
                    ["Gentle", "gentle"],
                ],
            },
            {
                "id": "preserve_background",
                "type": "bool",
                "label": "Preserve Background",
                "default": True,
            },
        ]

    def _apply_demo_transform(self, src_image, dst_image, params, preview=False):
        strength = float(params.get("strength", 0.45))
        lift = float(params.get("lift", 0.0))
        mode = str(params.get("mode", "balanced"))
        preserve_background = bool(params.get("preserve_background", True))

        if mode == "punchy":
            scale = 1.0 + strength * 1.2
            offset = -strength * 0.5 + lift
        elif mode == "gentle":
            scale = 1.0 - strength * 0.4
            offset = strength * 0.2 + lift
        else:
            scale = 1.0 + strength * 0.6
            offset = -strength * 0.25 + lift

        if preserve_background:
            offset *= 0.5

        afternight.core.apply_linear_transform(src_image, dst_image, scale, offset)

        if preview:
            label = self.settings.get("preview_label", "RT Preview Demo")
            afternight.log_debug(
                f"{label}: preview strength={strength:.2f} mode={mode} scale={scale:.3f}",
                component="an.demo.rt_preview",
            )

    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None,
                        weights=None):
        progress.set_text("Rendering contrast preview against the current source view")
        self._apply_demo_transform(src_image, preview_image, params, preview=True)

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        progress.set_text("Applying the contrast adjustment to the committed result")
        self._apply_demo_transform(src_image, dst_image, params, preview=False)
