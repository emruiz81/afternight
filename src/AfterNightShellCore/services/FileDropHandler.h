// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Files dropped on a shell's window.
///
/// A drag is taken when one of the files it carries is an image or a sequence AfterNight can likely open, judged by its
/// suffix. A drop hands every local file it carries to the opener the shell names, which keeps those it can open and
/// opens them as the person's own: a link that names no local file is left out.

#include "AfterNightShellCoreExport.h"

#include <QStringList>

#include <functional>

class QMimeData;

namespace an::ui::shell
{

class AFTERNIGHTSHELLCORE_API FileDropHandler
{
public:
    /// What the drop handler needs that is the shell's: the opener of the dropped files.
    struct Hooks
    {
        std::function<void(const QStringList& files)> openFiles;
    };

    void setHooks(Hooks hooks);

    /// Whether a drag carrying `data` is taken: one of its local files is likely an image or a sequence.
    [[nodiscard]] static bool takes(const QMimeData& data);
    /// The local files `data` carries, in order.
    [[nodiscard]] static QStringList localFiles(const QMimeData& data);

    /// Hands the local files `data` carries to be opened; whether it carried any, and so whether the drop is taken.
    bool drop(const QMimeData& data) const;

private:
    Hooks m_hooks;
};

} // namespace an::ui::shell
