// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/FileDropHandler.h"

#include "app/FileManager.h"

#include <QList>
#include <QMimeData>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace an::ui::shell
{

void FileDropHandler::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

bool FileDropHandler::takes(const QMimeData& data)
{
    const QStringList files = localFiles(data);
    return std::any_of(files.cbegin(), files.cend(),
                       [](const QString& file) { return an::ui::isLikelySupportedImagePath(file); });
}

QStringList FileDropHandler::localFiles(const QMimeData& data)
{
    QStringList files;
    for (const QUrl& url : data.urls())
    {
        const QString path = url.toLocalFile();
        if (!path.isEmpty())
        {
            files.append(path);
        }
    }
    return files;
}

bool FileDropHandler::drop(const QMimeData& data) const
{
    const QStringList files = localFiles(data);
    if (files.isEmpty())
    {
        return false;
    }
    if (m_hooks.openFiles)
    {
        m_hooks.openFiles(files);
    }
    return true;
}

} // namespace an::ui::shell
