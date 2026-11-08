// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <functional>

#include <QString>

class QWidget;

namespace an::ui
{

/// Shows a file in the system file manager with it selected; answers whether the file manager was asked to.
using FileRevealFunction = std::function<bool(const QString& absoluteFilePath)>;

/// Shows a file in the system file manager with the file selected - what every "Show in File Browser" entry does.
///
/// A file no longer on disk, moved or deleted since it was named, is not handed to the file manager: a warning over
/// `parent` says so instead, and is shown without waiting for its answer.
///
/// @param parent the window the warning is shown over; may be null.
/// @param path the file, as a local path; an empty path shows nothing.
/// @param reveal what shows the file; when empty, `revealInFileManager`.
/// @return what `reveal` answered; false when the file was not handed to it.
AFTERNIGHTCONTROLS_API bool showInFileBrowser(QWidget* parent, const QString& path,
                                              const FileRevealFunction& reveal = {});

} // namespace an::ui
