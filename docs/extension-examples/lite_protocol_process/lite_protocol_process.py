# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class ExampleLiteProtocolProcess(LiteProcessExtension):
    def get_params(self):
        return [
            {
                "id": "note",
                "type": "info",
                "tone": "info",
                "text": "Lite extensions use protocol-safe app/view services and own their UI.",
            }
        ]

    def on_launch(self):
        info = self.session_info()
        self.log_info(
            f"Lite protocol launch session={info.session_id} standalone={info.is_standalone}",
            component="example.lite_protocol_process",
        )
        theme_tokens = ui.tokens()
        if theme_tokens:
            self.log_debug("Host theme tokens are available.", component="example.lite_protocol_process")

        views = self.list_views()
        if not views:
            self.log_info("No host image views are currently available.", component="example.lite_protocol_process")
            return

        active = self.active_view()
        if active is not None:
            afternight.log_info(
                f"Active view: {active.view_name}",
                component="example.lite_protocol_process",
            )
