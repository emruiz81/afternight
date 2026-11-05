# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Echo Demo — minimal bundled reference extension that inverts the active image."""

import afternight
from afternight import ui


def _negative_offset_for_pixel_type(pixel_type):
    pixel_type = str(pixel_type or "").strip().lower()
    if pixel_type in {"uint8", "8 bits (unsigned integer)"}:
        return 255.0
    if pixel_type in {"uint16", "16 bits (unsigned integer)"}:
        return 65535.0
    if pixel_type in {"uint32", "32 bits (unsigned integer)"}:
        return 4294967295.0
    return 1.0


class EchoDemoExtension(ui.ProcessWindow):
    def get_settings_params(self):
        return [
            {"id": "label", "type": "string", "label": "Status Label", "default": "Echo Demo"}
        ]

    def get_params(self):
        return [
            {
                "id": "compatibility_banner",
                "type": "section",
                "label": "Compatibility Echo",
            },
            {
                "id": "compatibility_summary",
                "type": "info",
                "text": (
                    "Inverts the active image (photographic negative) and annotates "
                    "the result with host session and view metadata. This validates "
                    "bundled discovery, ProcessWindow launch, and ViewRef compatibility "
                    "without using preview."
                ),
            },
        ]

    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        label = self.settings.get("label", "Echo Demo")
        session = afternight.current_session()
        view_name = getattr(target, "view_name", "") if target is not None else ""
        file_name = getattr(target, "file_name", "") if target is not None else ""
        pixel_type = getattr(target, "pixel_type", "") if target is not None else ""
        negative_offset = _negative_offset_for_pixel_type(pixel_type)
        afternight.log_info(
            (
                f"{label}: inverting image "
                f"view={view_name} size="
                f"{getattr(target, 'width', 0)}x{getattr(target, 'height', 0)}"
            ),
            component="an.demo.echo",
        )
        progress.set_text("Creating a negative of the active image")
        afternight.core.apply_linear_transform(src_image, dst_image, -1.0, negative_offset)
        dst_image.set_metadata("an.demo.echo.label", label)
        dst_image.set_metadata("an.demo.echo.view_name", view_name)
        dst_image.set_metadata("an.demo.echo.file_name", file_name)
        dst_image.set_metadata("an.demo.echo.width", getattr(target, "width", 0))
        dst_image.set_metadata("an.demo.echo.height", getattr(target, "height", 0))
        dst_image.set_metadata("an.demo.echo.channels", getattr(target, "channels", 0))
        dst_image.set_metadata("an.demo.echo.pixel_type", pixel_type)
        dst_image.set_metadata("an.demo.echo.session_id", session.session_id)
        dst_image.set_metadata("an.demo.echo.extension_id", session.extension_id)
