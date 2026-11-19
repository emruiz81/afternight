# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class ExampleProtocolPanelProcess(LiteProcessExtension):
    """A protocol extension whose process window AfterNight draws.

    It describes its parameters as data, as a runtime extension does, with the protocol side of the SDK only:
    AfterNight draws them as a panel in a task page, lends each frame of the preview and the Apply's image to the Lite
    host, and shows what execute_preview() and execute() write.
    """

    def get_params(self):
        return [
            ui.process_window_meta(preview_autostretch=True, preview_autostretch_default=True),
            {
                "id": "adjustment",
                "type": "section",
                "label": "Adjustment",
            },
            {
                "id": "gain",
                "type": "float",
                "label": "Gain",
                "default": 1.25,
                "min": 0.0,
                "max": 4.0,
                "step": 0.05,
            },
            {
                "id": "offset",
                "type": "float",
                "label": "Offset",
                "default": 0.0,
                "min": -0.5,
                "max": 0.5,
                "step": 0.01,
            },
        ]

    def on_parameters_changed(self, params, changed_keys, reason):
        self.log_debug(f"Parameters changed: {sorted(changed_keys)} ({reason})",
                       component="example.protocol_panel")

    def _apply(self, src_image, out_image, params):
        gain = float(params.get("gain", 1.25))
        offset = float(params.get("offset", 0.0))
        out_image.from_numpy(src_image.to_numpy() * gain + offset)
        return gain, offset

    def execute_preview(self, target, src_image, preview_image, params, progress, masks=None, weights=None):
        del target, masks, weights
        self._apply(src_image, preview_image, params)

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None, output_masks=None):
        del target, masks, weights, output_masks
        progress.set_text("Applying gain and offset")
        gain, offset = self._apply(src_image, dst_image, params)
        progress.set_value(1.0)
        dst_image.set_metadata("example.protocol_panel.gain", gain)
        dst_image.set_metadata("example.protocol_panel.offset", offset)
