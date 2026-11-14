// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Files dropped on the window: a drag is taken when one of its files is an image or a sequence AfterNight can likely
/// open, and a drop hands every local file it carries to be opened - the opener keeps those it can open - as the
/// window did.

#include "ShellCoreTestSupport.h"

#include "core/ApplicationCore.h"
#include "services/FileDropHandler.h"

#include <gtest/gtest.h>

#include <QList>
#include <QMimeData>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

namespace
{

using an::tests::shellcore::ScopedStorageRoot;
using an::ui::shell::ApplicationCore;
using an::ui::shell::FileDropHandler;

/// What a drag of these files and links carries.
std::unique_ptr<QMimeData> carrying(const QStringList& files, const QList<QUrl>& links = {})
{
    QList<QUrl> urls;
    for (const QString& file : files)
    {
        urls.append(QUrl::fromLocalFile(file));
    }
    urls.append(links);
    auto data = std::make_unique<QMimeData>();
    data->setUrls(urls);
    return data;
}

class FileDropHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_core.fileDrops().setHooks({.openFiles = [this](const QStringList& files) { m_opened.append(files); }});
    }

    void TearDown() override
    {
        m_core.fileDrops().setHooks({});
    }

    [[nodiscard]] QString file(const char* name) const
    {
        return m_root.filePath(QString::fromLatin1(name));
    }

    QTemporaryDir m_root;
    ScopedStorageRoot m_scopedRoot{m_root.path()};
    ApplicationCore m_core;
    QList<QStringList> m_opened;
};

} // namespace

// A drag of an image is taken, whatever the case of its suffix.
TEST_F(FileDropHandlerTest, TakesADragOfAnImage)
{
    for (const char* name : {"light.fits", "light.FIT", "stack.xisf", "frame.tiff", "raw.CR3", "picture.jpg"})
    {
        EXPECT_TRUE(FileDropHandler::takes(*carrying({file(name)}))) << name;
    }
}

// A drag of a sequence - a SER file, AfterNight's own, a video - is taken.
TEST_F(FileDropHandlerTest, TakesADragOfASequence)
{
    for (const char* name : {"planet.ser", "frames.anseq", "capture.avi", "capture.mp4"})
    {
        EXPECT_TRUE(FileDropHandler::takes(*carrying({file(name)}))) << name;
    }
}

// A drag of a file AfterNight cannot open, or of a link that names no local file, is not taken.
TEST_F(FileDropHandlerTest, DoesNotTakeADragOfAnUnsupportedFile)
{
    EXPECT_FALSE(FileDropHandler::takes(*carrying({file("notes.txt")})));
    EXPECT_FALSE(FileDropHandler::takes(*carrying({}, {QUrl(QStringLiteral("https://example.org/light.fits"))})));
    EXPECT_FALSE(FileDropHandler::takes(QMimeData()));
}

// A drag of several files is taken when one of them can be opened.
TEST_F(FileDropHandlerTest, TakesADragOfSeveralFilesWhenOneCanBeOpened)
{
    EXPECT_TRUE(FileDropHandler::takes(*carrying({file("notes.txt"), file("light.fits")})));
    EXPECT_FALSE(FileDropHandler::takes(*carrying({file("notes.txt"), file("readme.md")})));
}

// A drop hands every local file it carries to be opened, once and in order, and is taken; a link that names no local
// file is left out, and a drop of none opens nothing and is not taken.
TEST_F(FileDropHandlerTest, ADropHandsEveryLocalFileToBeOpened)
{
    const std::unique_ptr<QMimeData> several = carrying({file("light.fits"), file("planet.ser"), file("notes.txt")},
                                                        {QUrl(QStringLiteral("https://example.org/dark.fits"))});

    EXPECT_TRUE(m_core.fileDrops().drop(*several));

    ASSERT_EQ(m_opened.size(), 1);
    EXPECT_EQ(m_opened.front(), (QStringList{file("light.fits"), file("planet.ser"), file("notes.txt")}));

    EXPECT_FALSE(m_core.fileDrops().drop(*carrying({}, {QUrl(QStringLiteral("https://example.org/dark.fits"))})));
    EXPECT_EQ(m_opened.size(), 1);
}
