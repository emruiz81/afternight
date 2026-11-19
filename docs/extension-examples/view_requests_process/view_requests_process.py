# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import time

import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension

COMPONENT = "example.view_requests"


def ask_until_answered(request, waits_for, seconds=10.0):
    """Ask again a request AfterNight refused only because the view was not ready for it yet - a commit of the session's
    still landing, an Undo not shown yet - until it is answered or ``seconds`` have gone by. AfterNight answers every
    request at once and waits for nothing, so what waits is the extension."""
    deadline = time.monotonic() + seconds
    while True:
        try:
            return request()
        except RuntimeError as error:
            if time.monotonic() > deadline or not any(reason in str(error) for reason in waits_for):
                raise
            time.sleep(0.05)


class ExampleViewRequests(LiteProcessExtension):
    """A protocol extension with no interface that acts on the view it was started on beyond its pixels.

    It draws two shapes over the image and takes one away; turns the view's selection area on with no rectangle, so
    the next drag on the image draws one, and reads back the rectangle a person drew; zooms in on the image's centre;
    commits a step of the image's history; undoes it and redoes it. What it saw is kept in its settings. Every request
    is AfterNight's to refuse, with its reason.
    """

    def run(self, params=None, progress=None):
        view = self.active_view()
        if view is None or view.source_image is None:
            self.log_warning("No image to act on", component=COMPONENT)
            ui.close_process_window()
            return
        samples = view.source_image.to_numpy()
        height, width = samples.shape[0], samples.shape[1]

        # Two shapes in the image's pixels, then one taken away: the polygon and its label stay.
        ids = ui.overlay_add([
            {
                "points": [(width * 0.25, height * 0.25), (width * 0.75, height * 0.25),
                           (width * 0.75, height * 0.75), (width * 0.25, height * 0.75)],
                "closed": True,
                "color": "#ffd54a",
                "label": "Centre",
            },
            {"points": [(0, 0), (width, height)], "color": (90, 200, 255)},
        ])
        self.settings.set("shapes_kept", ui.overlay_remove(ids[1:]))

        # The selection area, drawn by a person: the view's state carries its rectangle once the mouse is released.
        ui.set_selection()
        area = self.wait_for_area(view.view_name, seconds=float(self.settings.get("area_wait_seconds", 30.0)))
        self.settings.set("area", list(area) if area else [])

        self.settings.set("zoom", ui.set_display(zoom=200, center=(width / 2, height / 2)))

        ui.commit_image(1.0 - samples, history_step_name="Example View Requests",
                        history_step_description="Inverted by the view requests template")
        ask_until_answered(ui.undo, ["has not landed", "nothing to undo"])
        ask_until_answered(ui.redo, ["nothing to redo"])
        self.settings.set("done", True)
        self.log_info("Drew, selected, zoomed, undid and redid", component=COMPONENT)
        ui.close_process_window()

    def wait_for_area(self, view_name, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and not self.is_cancelled():
            for state in self.list_views():
                state = dict(state or {})
                if (state.get("name") or state.get("view_name")) != view_name or not state.get("selection_area_enabled"):
                    continue
                area = dict(state.get("selection_area") or {})
                if int(area.get("width", 0)) > 0 and int(area.get("height", 0)) > 0:
                    return (int(area["x"]), int(area["y"]), int(area["width"]), int(area["height"]))
            time.sleep(0.05)
        return None
