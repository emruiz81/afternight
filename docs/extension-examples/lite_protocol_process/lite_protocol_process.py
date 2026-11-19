# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class ExampleLiteProtocolProcess(LiteProcessExtension):
    """A protocol extension that owns its interface (``capabilities.interface = "window"``).

    AfterNight lists its session as a task and draws nothing of it: the extension shows what it shows itself - a window
    of its own toolkit, or another program's it starts - and raises it in on_raise_window() when a person asks to see it.
    """

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

    def on_raise_window(self):
        # A Lite extension raises the window it owns here, when a person asks to see it (Show on its task). This
        # template owns none: it records that it was asked.
        raised = int(self.settings.get("raise_requests", 0) or 0) + 1
        self.settings.set("raise_requests", raised)
        self.log_info(f"Asked to raise the extension's window ({raised})", component="example.lite_protocol_process")
