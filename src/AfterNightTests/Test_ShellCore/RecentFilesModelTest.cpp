// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The recent files: the limit the settings give, the order the file keeps, what reading it repairs, and a list that
/// is saved, read back and cleared.

#include "ShellCoreTestSupport.h"

#include "services/RecentFilesModel.h"

#include "controls/settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <deque>
#include <string>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::ui::shell::RecentFilesModel;
using Files = std::deque<std::string>;

class RecentFilesModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_folder.isValid());
    }

    /// A file of the case's folder, as the list names it.
    [[nodiscard]] std::string file(const QString& name) const
    {
        return QDir(m_folder.path()).filePath(name).toStdString();
    }

    /// The list's file, written line by line as the case gives it.
    void writeListFile(const QStringList& lines) const
    {
        QFile out(listFilePath());
        ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
        QTextStream stream(&out);
        stream.setEncoding(QStringConverter::Utf8);
        for (const QString& line : lines)
        {
            stream << line << '\n';
        }
    }

    /// The list's file, line by line; empty when it does not exist.
    [[nodiscard]] QStringList readListFile() const
    {
        QFile in(listFilePath());
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return {};
        }
        QTextStream stream(&in);
        stream.setEncoding(QStringConverter::Utf8);
        QStringList lines;
        for (QString line = stream.readLine(); !line.isNull(); line = stream.readLine())
        {
            lines.push_back(line);
        }
        return lines;
    }

    [[nodiscard]] QString listFilePath() const
    {
        return QDir(m_folder.path()).filePath(QStringLiteral("recent_files"));
    }

    [[nodiscard]] RecentFilesModel modelOverTheFolder()
    {
        RecentFilesModel model;
        model.setStorage(m_folder.path(), &m_settings);
        return model;
    }

    QTemporaryDir m_folder;
    MemorySettings m_settings;
};

TEST_F(RecentFilesModelTest, TheLimitIsTheSettingOrTwentyByDefault)
{
    RecentFilesModel unstored;
    EXPECT_EQ(unstored.limit(), 20u) << "with no settings store";

    RecentFilesModel model = modelOverTheFolder();
    EXPECT_EQ(model.limit(), 20u) << "with nothing stored";

    const QString key = QString::fromLatin1(SettingsKeys::GENERAL_RECENT_FILES_LIMIT);
    m_settings.upsert(key, QStringLiteral("7"));
    EXPECT_EQ(model.limit(), 7u);
    for (const QString& unusable : {QStringLiteral("0"), QStringLiteral("-3"), QStringLiteral("many")})
    {
        m_settings.upsert(key, unusable);
        EXPECT_EQ(model.limit(), 20u) << unusable.toStdString();
    }
}

TEST_F(RecentFilesModelTest, KeepsTheFilesInTheOrderItsFileListsThem)
{
    writeListFile({QString::fromStdString(file(QStringLiteral("c.fits"))),
                   QString::fromStdString(file(QStringLiteral("a.fits"))),
                   QString::fromStdString(file(QStringLiteral("b.fits")))});
    RecentFilesModel model = modelOverTheFolder();

    model.load();

    EXPECT_EQ(model.entries(),
              (Files{file(QStringLiteral("c.fits")), file(QStringLiteral("a.fits")), file(QStringLiteral("b.fits"))}));
}

TEST_F(RecentFilesModelTest, ReadsNoMoreThanTheLimit)
{
    m_settings.upsert(QString::fromLatin1(SettingsKeys::GENERAL_RECENT_FILES_LIMIT), QStringLiteral("2"));
    const QStringList lines{QString::fromStdString(file(QStringLiteral("1.fits"))),
                            QString::fromStdString(file(QStringLiteral("2.fits"))),
                            QString::fromStdString(file(QStringLiteral("3.fits")))};
    writeListFile(lines);
    RecentFilesModel model = modelOverTheFolder();

    model.load();

    EXPECT_EQ(model.entries(), (Files{file(QStringLiteral("1.fits")), file(QStringLiteral("2.fits"))}));
    EXPECT_EQ(readListFile(), lines) << "a list that needed no repair is not written";
}

// A file left by an older version, or edited by hand, is read without its repeated and blank lines, and is written
// again as it was read.
TEST_F(RecentFilesModelTest, LeavesOutRepeatedAndBlankLinesAndRewritesTheFile)
{
    const QString first = QString::fromStdString(file(QStringLiteral("first.fits")));
    const QString second = QString::fromStdString(file(QStringLiteral("second.xisf")));
    writeListFile({first, QString(), QStringLiteral("   "), second, first});
    RecentFilesModel model = modelOverTheFolder();

    model.load();

    EXPECT_EQ(model.entries(), (Files{first.toStdString(), second.toStdString()}));
    EXPECT_EQ(readListFile(), (QStringList{first, second}));
}

TEST_F(RecentFilesModelTest, ReadsBackTheListItSaved)
{
    RecentFilesModel model = modelOverTheFolder();
    model.entriesForEdit() = Files{file(QStringLiteral("Orión M42.fits")), file(QStringLiteral("NGC 7000.xisf"))};
    model.save();

    RecentFilesModel later = modelOverTheFolder();
    later.load();

    EXPECT_EQ(later.entries(), model.entries());
    // Reading again replaces the list rather than adding to it.
    later.load();
    EXPECT_EQ(later.entries(), model.entries());
}

TEST_F(RecentFilesModelTest, ClearingEmptiesTheListAndItsFile)
{
    RecentFilesModel model = modelOverTheFolder();
    model.entriesForEdit() = Files{file(QStringLiteral("a.fits")), file(QStringLiteral("b.fits"))};
    model.save();

    model.clear();

    EXPECT_TRUE(model.entries().empty());
    EXPECT_TRUE(readListFile().isEmpty());
    RecentFilesModel later = modelOverTheFolder();
    later.load();
    EXPECT_TRUE(later.entries().empty());
}

TEST_F(RecentFilesModelTest, WithNoFolderTheListIsKeptInMemoryOnly)
{
    RecentFilesModel model;
    model.entriesForEdit() = Files{file(QStringLiteral("a.fits"))};

    model.save();
    model.load();

    EXPECT_EQ(model.entries(), (Files{file(QStringLiteral("a.fits"))})) << "nothing was read over the list";
    EXPECT_FALSE(QFile::exists(listFilePath()));
}

} // namespace
