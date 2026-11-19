# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension


class ExampleProtocolBatchProcess(LiteProcessExtension):
    """A protocol extension with no interface (``capabilities.interface = "none"``).

    AfterNight draws nothing of it and lists its session as a task with its progress. Its own run, started with the
    session, processes the image it was started on, commits the result as one step of that image's history, and closes
    its task.
    """

    def run(self, params=None, progress=None):
        view = self.active_view()
        if view is None or view.source_image is None:
            self.log_warning("No image to process", component="example.protocol_batch")
            ui.close_process_window()
            return

        self.set_progress(0.0, "Reading the image")
        samples = view.source_image.to_numpy()
        rows = samples.shape[0]
        result = samples.copy()
        for row in range(rows):
            if self.is_cancelled():
                self.log_info("Cancelled", component="example.protocol_batch")
                ui.close_process_window()
                return
            result[row] = 1.0 - samples[row]
            self.set_progress((row + 1) / rows, f"Row {row + 1} of {rows}")

        ui.commit_image(
            result,
            history_step_name="Example Protocol Batch",
            history_step_description="Inverted by an extension with no interface",
        )
        ui.close_process_window()
