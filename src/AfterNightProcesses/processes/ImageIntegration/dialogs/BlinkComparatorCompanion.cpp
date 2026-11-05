// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The standalone registration's way to the integration's blink comparator: shown over the window that asks, never
/// run, and telling the window the frames the person excluded when it closes.

#include "stdafx.hpp"

#include "processes/ImageIntegration/dialogs/BlinkComparatorDialog.h"
#include "processes/core/WorkflowToolCompanions.h"

#include <utility>

namespace an::ui::processes
{

void openStandaloneBlinkComparator(QWidget* parent, const QStringList& frames,
                                   std::function<void(const QStringList& excluded)> closed)
{
    auto* dialog = new BlinkComparatorDialog(frames, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &QDialog::finished, dialog, [dialog, closed = std::move(closed)](int) {
        if (closed)
        {
            closed(dialog->comparisonResult().excludedPaths);
        }
    });
    dialog->open();
}

} // namespace an::ui::processes
