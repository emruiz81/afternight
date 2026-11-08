// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "app/ShowInFileBrowser.h"

#include "app/DesktopUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QWidget>

namespace an::ui
{

bool showInFileBrowser(QWidget* parent, const QString& path, const FileRevealFunction& reveal)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return false;
    }

    const QString cleaned = QDir::cleanPath(trimmed);
    const QFileInfo fileInfo(cleaned);
    if (!fileInfo.exists())
    {
        // Shown without a nested event loop: the caller - a menu, a context menu - returns at once.
        auto* box =
            new QMessageBox(QMessageBox::Warning, QObject::tr("File Not Found"),
                            QObject::tr("The file does not exist on disk:\n%1").arg(cleaned), QMessageBox::Ok, parent);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return false;
    }

    const QString absolute = fileInfo.absoluteFilePath();
    return reveal ? reveal(absolute) : revealInFileManager(absolute);
}

} // namespace an::ui
