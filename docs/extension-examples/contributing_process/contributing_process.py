# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
import afternight.ui_protocol as ui
from afternight.lite import LiteProcessExtension

# The catalogue the extension keeps of its own: what a person may enable. A real package lists the scripts it fetched
# or the models a person downloaded; each one enabled becomes a process of the extension's.
CATALOGUE = {
    "deepen_shadows": {
        "display_name": "Deepen Shadows",
        "group": "Tone",
        "category": "tone_brightness",
        "summary": "Darkens the faint end of the image",
    },
    "lift_highlights": {
        "display_name": "Lift Highlights",
        "group": "Tone",
        "summary": "Brightens the bright end of the image",
    },
}


class ExampleScriptCatalogue(LiteProcessExtension):
    """A protocol extension that adds processes while it runs (``afternight.contribute_processes``).

    Started as its manifest's process, it states the entries of its catalogue a person enabled - the setting
    ``enabled``, every entry by default - and AfterNight lists each as a process of the extension: in its family's
    menu, in a submenu for its group; in the Process menu of its category when it states one; in the palette, among
    the recent processes and the favourites. Started for one of them, it reads which from its session
    (``current_session().contributed_process``) and runs that one. Neither shows an interface
    (``capabilities.interface = "none"``): each run closes its task when it ends.
    """

    def enabled(self):
        stated = self.settings.get("enabled", list(CATALOGUE))
        return [key for key in stated if key in CATALOGUE]

    def run(self, params=None, progress=None):
        started_for = afternight.current_session().contributed_process
        if started_for:
            # A real package runs the script or the model it listed here, on the image it was started on.
            self.settings.set("last_started_for", started_for)
            name = CATALOGUE.get(started_for, {}).get("display_name", started_for)
            self.log_info(f"Started for {name}", component="example.contributing")
        else:
            # The whole list each time: what is no longer enabled is withdrawn.
            entries = [{"id_suffix": key, **CATALOGUE[key]} for key in self.enabled()]
            kept = afternight.contribute_processes(entries)
            self.log_info(f"Contributed {kept} process(es)", component="example.contributing")
        ui.close_process_window()
