# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
from afternight import ui


class ExampleBasicProcess(ui.ProcessWindow):
    def get_params(self):
        return [
            {
                "id": "strength",
                "type": "float",
                "label": "Strength",
                "default": 1.0,
                "min": 0.0,
                "max": 2.0,
                "step": 0.05,
            }
        ]

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        strength = float(params.get("strength", 1.0))
        progress.set_text(f"Applying example process at {strength:.2f}")
        afternight.core.apply_linear_transform(src_image, dst_image, strength, 0.0)
