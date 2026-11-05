# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import numpy as np

import afternight
from afternight import ui


def _as_float_image(image_handle):
    if image_handle is None:
        return None
    return np.asarray(image_handle.to_numpy(), dtype=np.float32)


def _match_channels(auxiliary, source):
    if auxiliary is None:
        return None
    if source.ndim == 3 and auxiliary.ndim == 2:
        return auxiliary[:, :, None]
    return auxiliary


class ExampleAuxiliaryBuffersProcess(ui.ProcessWindow):
    def get_params(self):
        return [
            ui.process_window_meta(panel_variant="native"),
            {
                "id": "auxiliary",
                "type": "section",
                "label": "Auxiliary Buffers",
            },
            {
                "id": "mask_strength",
                "type": "float",
                "label": "Mask Strength",
                "default": 0.50,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
            {
                "id": "weight_boost",
                "type": "float",
                "label": "Weight Boost",
                "default": 0.15,
                "min": 0.0,
                "max": 1.0,
                "step": 0.05,
            },
        ]

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        del target
        masks = dict(masks or {})
        weights = dict(weights or {})
        output_masks = dict(output_masks or {})

        progress.set_text("Combining source, selection mask, and weight map")

        source = np.asarray(src_image.to_numpy(), dtype=np.float32)
        selection = _match_channels(_as_float_image(masks.get("selection")), source)
        weight_map = _match_channels(_as_float_image(weights.get("weight_map")), source)

        result = np.array(source, copy=True)
        if selection is not None:
            mask_strength = float(params.get("mask_strength", 0.50))
            result = result * (1.0 - selection * mask_strength) + selection * mask_strength

        if weight_map is not None:
            weight_boost = float(params.get("weight_boost", 0.15))
            result = result + weight_map * weight_boost

        dst_image.from_numpy(np.clip(result, 0.0, 1.0))

        if "output_mask" in output_masks:
            height, width = source.shape[:2]
            combined = np.zeros((height, width), dtype=np.float32)
            if selection is not None:
                combined = np.maximum(combined, np.squeeze(selection))
            if weight_map is not None:
                combined = np.maximum(combined, np.squeeze(weight_map))
            output_masks["output_mask"].from_numpy(np.clip(combined, 0.0, 1.0))

        afternight.log_info(
            (
                "Auxiliary buffer example finished "
                f"mask={selection is not None} weight={weight_map is not None}"
            ),
            component="example.auxiliary_buffers",
        )
