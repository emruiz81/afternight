# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
import afternight
from afternight import ui


class ExampleLockedWheelhouseProcess(ui.ProcessWindow):
    def execute(self, target, src_image, dst_image, params, progress, masks=None, weights=None,
                output_masks=None):
        progress.set_text("Running locked wheelhouse example")
        afternight.core.apply_linear_transform(src_image, dst_image, 1.0, 0.0)
