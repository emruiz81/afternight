// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A recorded baseline: a text file checked in beside the suite that reads it, holding what a part of the product was
/// when the record was made - its design tokens, its actions, its menus, its shortcuts - one entry a line. A case
/// reads the same part from the running product and compares the two in order, so a change to that part fails the
/// case until the record's entry is edited to say the change was meant.
///
/// A record is sections of entries. `[name]` at the start of a line opens a section; a line that starts with `#` and
/// an empty line belong to none. An entry is its fields joined by ` | `, an empty field written `-`; a tree is
/// entries indented four spaces a level.
///
/// With AFTERNIGHT_WRITE_BASELINE_RECORDS naming a folder, a case writes its section into the file of the record's
/// name in that folder instead of comparing - the one case in which this writes, and never outside that folder. That
/// is how a record is first made: written, read by a person, checked in. From then on a record is edited by the
/// change that means to move it, entry by entry, and is not written again whole.
#pragma once

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <vector>

namespace an::tests::fixtures
{

/// Names a folder the records are written into, instead of being compared with the checked-in ones.
inline constexpr const char* kWriteBaselineRecordsVariable = "AFTERNIGHT_WRITE_BASELINE_RECORDS";

/// A field of an entry as the record holds it: `-` for an empty one, and the characters that would break an entry -
/// the separator's bar, a tab, a line break, the escape itself - escaped.
[[nodiscard]] inline QString baselineField(const QString& text)
{
    if (text.isEmpty())
    {
        return QStringLiteral("-");
    }
    QString field;
    field.reserve(text.size());
    for (const QChar character : text)
    {
        if (character == QLatin1Char('\\'))
        {
            field += QStringLiteral("\\\\");
        }
        else if (character == QLatin1Char('|'))
        {
            field += QStringLiteral("\\|");
        }
        else if (character == QLatin1Char('\t'))
        {
            field += QStringLiteral("\\t");
        }
        else if (character == QLatin1Char('\n'))
        {
            field += QStringLiteral("\\n");
        }
        else if (character == QLatin1Char('\r'))
        {
            field += QStringLiteral("\\r");
        }
        else
        {
            field += character;
        }
    }
    return field;
}

/// An entry: its fields, each as `baselineField` writes it, joined by ` | ` and indented four spaces a level.
[[nodiscard]] inline QString baselineEntry(const QStringList& fields, int depth = 0)
{
    QStringList written;
    written.reserve(fields.size());
    for (const QString& field : fields)
    {
        written.append(baselineField(field));
    }
    return QString(depth * 4, QLatin1Char(' ')) + written.join(QStringLiteral(" | "));
}

/// One record: the checked-in file a suite's cases compare the product with, a section at a time.
class BaselineRecord
{
public:
    /// @param path the record's path from the repository's root.
    /// @param preamble the comment the record opens with, a line an element, without the `#`.
    BaselineRecord(QString path, QStringList preamble) : m_path(std::move(path)), m_preamble(std::move(preamble))
    {
        const QString writeFolder = qEnvironmentVariable(kWriteBaselineRecordsVariable);
        if (!writeFolder.isEmpty())
        {
            m_writing = true;
            m_file = QDir(writeFolder).filePath(QFileInfo(m_path).fileName());
            if (QDir().mkpath(writeFolder))
            {
                load(m_file);
            }
            return;
        }
        m_file = checkedInFile(m_path);
        if (!m_file.isEmpty())
        {
            load(m_file);
        }
    }

    /// Writes the sections the cases handed over, when the record is being written.
    ~BaselineRecord()
    {
        if (m_writing && m_changed)
        {
            (void)write();
        }
    }

    BaselineRecord(const BaselineRecord&) = delete;
    BaselineRecord& operator=(const BaselineRecord&) = delete;

    /// Whether the record is being written rather than compared.
    [[nodiscard]] bool writing() const noexcept
    {
        return m_writing;
    }

    /// The sections the record holds, in its order.
    [[nodiscard]] QStringList sections() const
    {
        QStringList names;
        for (const Section& section : m_sections)
        {
            names.append(section.name);
        }
        return names;
    }

    /// Compares a section of the record with the entries read from the product, in order; the failure names the
    /// first entry that differs. When the record is being written, the entries become the section instead.
    [[nodiscard]] ::testing::AssertionResult matches(const QString& section, const QStringList& entries)
    {
        if (m_writing)
        {
            replace(section, entries);
            m_changed = true;
            return write()
                       ? ::testing::AssertionSuccess()
                       : ::testing::AssertionFailure() << "the record could not be written to " << m_file.toStdString();
        }
        if (m_file.isEmpty())
        {
            return ::testing::AssertionFailure()
                   << "there is no record " << m_path.toStdString() << " above the working directory; the product has "
                   << entries.size() << " entries in [" << section.toStdString() << "]. " << howARecordIsMade();
        }
        const Section* recorded = nullptr;
        for (const Section& candidate : m_sections)
        {
            if (candidate.name == section)
            {
                recorded = &candidate;
            }
        }
        if (recorded == nullptr || recorded->entries.isEmpty())
        {
            return ::testing::AssertionFailure()
                   << m_path.toStdString() << " records nothing in [" << section.toStdString() << "]; the product has "
                   << entries.size() << " entries there. " << howARecordIsMade();
        }
        const qsizetype shared = (std::min)(recorded->entries.size(), entries.size());
        for (qsizetype index = 0; index < shared; ++index)
        {
            if (recorded->entries[index] != entries[index])
            {
                return ::testing::AssertionFailure()
                       << m_path.toStdString() << " [" << section.toStdString() << "], entry " << (index + 1)
                       << ":\n  the record holds: " << recorded->entries[index].toStdString()
                       << "\n  the product has:  " << entries[index].toStdString() << "\n"
                       << howARecordIsEdited();
            }
        }
        if (recorded->entries.size() > entries.size())
        {
            return ::testing::AssertionFailure()
                   << m_path.toStdString() << " [" << section.toStdString() << "]: the product ends after "
                   << entries.size()
                   << " entries and the record goes on with: " << recorded->entries[shared].toStdString() << "\n"
                   << howARecordIsEdited();
        }
        if (entries.size() > recorded->entries.size())
        {
            return ::testing::AssertionFailure()
                   << m_path.toStdString() << " [" << section.toStdString() << "]: the record ends after "
                   << recorded->entries.size()
                   << " entries and the product goes on with: " << entries[shared].toStdString() << "\n"
                   << howARecordIsEdited();
        }
        return ::testing::AssertionSuccess();
    }

private:
    struct Section
    {
        QString name;
        QStringList entries;
    };

    [[nodiscard]] static const char* howARecordIsMade()
    {
        return "A record is first made by running the case with AFTERNIGHT_WRITE_BASELINE_RECORDS naming a folder, "
               "reading what it wrote there and checking it in beside the suite.";
    }

    [[nodiscard]] static const char* howARecordIsEdited()
    {
        return "When the change is meant, edit that entry of the record; when it is not, the product changed something "
               "it was to keep.";
    }

    // The checked-in record: the first file of that path at or above the working directory, the suite's own folder or
    // this source file. Empty when there is none.
    [[nodiscard]] static QString checkedInFile(const QString& path)
    {
        QStringList seeds = {QDir::currentPath()};
        if (QCoreApplication::instance() != nullptr)
        {
            seeds.append(QCoreApplication::applicationDirPath());
        }
        seeds.append(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
        for (const QString& seed : seeds)
        {
            QDir folder(seed);
            do
            {
                const QString candidate = folder.filePath(path);
                if (QFileInfo(candidate).isFile())
                {
                    return candidate;
                }
            } while (folder.cdUp());
        }
        return {};
    }

    void load(const QString& file)
    {
        QFile source(file);
        if (!source.open(QIODevice::ReadOnly))
        {
            return;
        }
        QString text = QString::fromUtf8(source.readAll());
        text.remove(QLatin1Char('\r'));
        for (QString line : text.split(QLatin1Char('\n')))
        {
            while (!line.isEmpty() && line.back().isSpace())
            {
                line.chop(1);
            }
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            {
                continue;
            }
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
            {
                m_sections.push_back(Section{line.mid(1, line.size() - 2), {}});
                continue;
            }
            if (!m_sections.empty())
            {
                m_sections.back().entries.append(line);
            }
        }
    }

    void replace(const QString& section, const QStringList& entries)
    {
        for (Section& existing : m_sections)
        {
            if (existing.name == section)
            {
                existing.entries = entries;
                return;
            }
        }
        m_sections.push_back(Section{section, entries});
    }

    [[nodiscard]] bool write() const
    {
        QString text;
        for (const QString& line : m_preamble)
        {
            text += line.isEmpty() ? QStringLiteral("#\r\n") : QStringLiteral("# ") + line + QStringLiteral("\r\n");
        }
        for (const Section& section : m_sections)
        {
            text += QStringLiteral("\r\n[") + section.name + QStringLiteral("]\r\n");
            for (const QString& entry : section.entries)
            {
                text += entry + QStringLiteral("\r\n");
            }
        }
        QFile target(m_file);
        if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        const QByteArray bytes = text.toUtf8();
        return target.write(bytes) == bytes.size();
    }

    QString m_path;
    QStringList m_preamble;
    QString m_file;
    std::vector<Section> m_sections;
    bool m_writing = false;
    bool m_changed = false;
};

} // namespace an::tests::fixtures
