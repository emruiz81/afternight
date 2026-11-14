// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "services/RecentFilesModel.h"

#include "controls/DashboardUtils.h"
#include "controls/IAppContext.h"
#include "controls/settings/SettingsDefaults.h"
#include "controls/settings/SettingsKeys.h"

#include <QFile>
#include <QIODevice>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace an::ui::shell
{

namespace
{

constexpr std::size_t kDefaultLimit = SettingsDefaults::GENERAL_RECENT_FILES_LIMIT;

} // namespace

void RecentFilesModel::setStorage(QString folder, an::ui::IGlobalSettingsProvider* settings)
{
    m_folder = std::move(folder);
    m_settings = settings;
}

std::size_t RecentFilesModel::limit() const
{
    if (m_settings != nullptr)
    {
        const QString limitStr =
            m_settings->select(SettingsKeys::GENERAL_RECENT_FILES_LIMIT, QString::number(kDefaultLimit));
        bool ok = false;
        const int limit = limitStr.toInt(&ok);
        if (ok && limit > 0)
        {
            return static_cast<std::size_t>(limit);
        }
    }

    return kDefaultLimit;
}

const std::deque<std::string>& RecentFilesModel::entries() const noexcept
{
    return m_entries;
}

std::deque<std::string>& RecentFilesModel::entriesForEdit() noexcept
{
    return m_entries;
}

void RecentFilesModel::load()
{
    if (m_folder.isEmpty())
    {
        return;
    }

    m_entries.clear();
    QFile file(m_folder + "/recent_files");
    bool recentFilesSanitized = false;

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);

        const std::size_t recentFilesLimit = limit();
        QString line = in.readLine();
        while (!line.isNull())
        {
            const QString normalizedPath = DashboardUtils::normalizeRecentFilePath(line);
            recentFilesSanitized = recentFilesSanitized || (normalizedPath != line);

            if (!normalizedPath.isEmpty())
            {
                const std::string normalizedPathStd = normalizedPath.toUtf8().toStdString();
                const auto duplicateIt = std::find(m_entries.begin(), m_entries.end(), normalizedPathStd);
                if (duplicateIt == m_entries.end())
                {
                    m_entries.push_back(normalizedPathStd);
                    if (m_entries.size() >= recentFilesLimit)
                    {
                        break;
                    }
                }
                else
                {
                    recentFilesSanitized = true;
                }
            }
            else
            {
                recentFilesSanitized = true;
            }

            line = in.readLine();
        }

        file.close();
    }

    if (recentFilesSanitized)
    {
        save();
    }
}

void RecentFilesModel::save() const
{
    if (m_folder.isEmpty())
    {
        return;
    }

    QFile file(m_folder + "/recent_files");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(false);

    for (const auto& item : m_entries)
    {
        out << QString::fromUtf8(item);
        out << '\n';
    }

    file.close();
}

void RecentFilesModel::clear()
{
    m_entries.clear();
    save();
}

} // namespace an::ui::shell
