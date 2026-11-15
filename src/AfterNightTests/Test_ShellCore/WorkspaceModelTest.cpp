// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The workspaces as the shell core keeps them, with no widget: created and named "Workspace N", renamed, closed but
// never the only one, reordered, one of them the current one, each locked or in a window of its own, and the observers
// told of every change.

#include "views/WorkspaceModel.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

namespace
{

using Model = an::ui::shell::WorkspaceModel;
using Change = an::ui::shell::WorkspaceModel::Change;

/// The workspaces' names, in the model's order.
[[nodiscard]] QStringList namesOf(const Model& model)
{
    QStringList names;
    for (const an::ui::shell::Workspace& workspace : model.workspaces())
    {
        names.append(workspace.name);
    }
    return names;
}

/// Where a file of the repository is: the first of that path at or above the working directory or the executable's.
[[nodiscard]] QString repositoryFile(const QString& path)
{
    for (const QString& seed : {QDir::currentPath(), QCoreApplication::applicationDirPath()})
    {
        QDir folder(seed);
        do
        {
            if (QFileInfo(folder.filePath(path)).isFile())
            {
                return folder.filePath(path);
            }
        } while (folder.cdUp());
    }
    return {};
}

} // namespace

// A model starts with none; each workspace made is "Workspace N", in the order made, and the first made is the current
// one; one made later leaves the current one as it is. A name given is the workspace's.
TEST(WorkspaceModel, CreatesWorkspacesNamedInOrder)
{
    Model model;
    EXPECT_EQ(model.count(), 0u);
    EXPECT_TRUE(model.current().isEmpty());

    const QString first = model.create();
    ASSERT_FALSE(first.isEmpty());
    EXPECT_EQ(model.current(), first);
    const QString second = model.create();
    const QString third = model.create(QStringLiteral("Masters"));
    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_EQ(model.current(), first);
    EXPECT_EQ(namesOf(model),
              (QStringList{QStringLiteral("Workspace 1"), QStringLiteral("Workspace 2"), QStringLiteral("Masters")}));
    EXPECT_EQ(model.indexOf(third), 2);
    ASSERT_NE(model.find(second), nullptr);
    EXPECT_EQ(model.find(second)->name, QStringLiteral("Workspace 2"));
    EXPECT_EQ(model.find(QStringLiteral("no such workspace")), nullptr);
    EXPECT_EQ(model.indexOf(QStringLiteral("no such workspace")), -1);
}

// A new workspace takes the lowest number no other workspace is named by; an id is never given twice.
TEST(WorkspaceModel, NamesANewWorkspaceByTheLowestNumberFree)
{
    Model model;
    const QString first = model.create();
    const QString second = model.create();
    (void)model.create();
    ASSERT_TRUE(model.close(second));
    EXPECT_EQ(model.nextName(), QStringLiteral("Workspace 2"));
    const QString again = model.create();
    EXPECT_EQ(model.find(again)->name, QStringLiteral("Workspace 2"));
    EXPECT_NE(again, second) << "an id is never given again";
    ASSERT_TRUE(model.rename(first, QStringLiteral("Lights")));
    EXPECT_EQ(model.nextName(), QStringLiteral("Workspace 1"));
}

// A workspace is renamed to the name given, its ends trimmed; an empty name, or an id the model does not hold, changes
// nothing.
TEST(WorkspaceModel, RenamesAndRefusesAnEmptyName)
{
    Model model;
    const QString id = model.create();
    EXPECT_TRUE(model.rename(id, QStringLiteral("  Masters  ")));
    EXPECT_EQ(model.find(id)->name, QStringLiteral("Masters"));
    EXPECT_FALSE(model.rename(id, QStringLiteral("   ")));
    EXPECT_EQ(model.find(id)->name, QStringLiteral("Masters"));
    EXPECT_FALSE(model.rename(QStringLiteral("no such workspace"), QStringLiteral("Flats")));
}

// A workspace closes, but never the only one.
TEST(WorkspaceModel, ClosesAWorkspaceButNeverTheOnlyOne)
{
    Model model;
    const QString only = model.create();
    EXPECT_FALSE(model.canClose(only));
    EXPECT_FALSE(model.close(only));
    EXPECT_EQ(model.count(), 1u);

    const QString second = model.create();
    EXPECT_TRUE(model.canClose(only));
    EXPECT_TRUE(model.canClose(second));
    EXPECT_TRUE(model.close(second));
    EXPECT_EQ(model.count(), 1u);
    EXPECT_EQ(model.find(second), nullptr);
    EXPECT_FALSE(model.close(second)) << "a workspace closes once";
    EXPECT_FALSE(model.canClose(only));
}

// The current workspace closed, the one after it is the current one, or the one before when it was the last.
TEST(WorkspaceModel, TheCurrentOneMovesToItsNeighbourWhenClosed)
{
    Model model;
    const QString first = model.create();
    const QString second = model.create();
    const QString third = model.create();
    ASSERT_TRUE(model.setCurrent(second));
    EXPECT_EQ(model.current(), second);
    EXPECT_FALSE(model.setCurrent(QStringLiteral("no such workspace")));
    EXPECT_EQ(model.current(), second);

    ASSERT_TRUE(model.close(second));
    EXPECT_EQ(model.current(), third);
    ASSERT_TRUE(model.close(third));
    EXPECT_EQ(model.current(), first);
}

// A workspace moves to another place in the order - the last place for one past the end - and the current one stays.
TEST(WorkspaceModel, ReordersAndKeepsTheCurrent)
{
    Model model;
    const QString first = model.create();
    (void)model.create();
    const QString third = model.create();
    ASSERT_TRUE(model.move(third, 0));
    EXPECT_EQ(namesOf(model), (QStringList{QStringLiteral("Workspace 3"), QStringLiteral("Workspace 1"),
                                           QStringLiteral("Workspace 2")}));
    EXPECT_EQ(model.current(), first);
    ASSERT_TRUE(model.move(third, 10));
    EXPECT_EQ(model.indexOf(third), 2);
    EXPECT_FALSE(model.move(QStringLiteral("no such workspace"), 0));
}

// Each workspace is locked or free, attached or in a window of its own, on its own.
TEST(WorkspaceModel, KeepsTheLockAndTheWindowOfEachWorkspace)
{
    Model model;
    const QString first = model.create();
    const QString second = model.create();
    EXPECT_FALSE(model.find(first)->locked);
    EXPECT_FALSE(model.find(first)->detached);

    ASSERT_TRUE(model.setLocked(first, true));
    ASSERT_TRUE(model.setDetached(second, true));
    EXPECT_TRUE(model.find(first)->locked);
    EXPECT_FALSE(model.find(first)->detached);
    EXPECT_FALSE(model.find(second)->locked);
    EXPECT_TRUE(model.find(second)->detached);
    EXPECT_FALSE(model.setLocked(QStringLiteral("no such workspace"), true));
}

// Every change is told to the observers, what changed and of which workspace; a change refused, or one that changes
// nothing, is told to none, and an observer that left hears nothing more.
TEST(WorkspaceModel, TellsItsObserversOfEveryChange)
{
    Model model;
    std::vector<std::pair<Change, QString>> heard;
    const std::size_t token =
        model.subscribe([&heard](const Change change, const QString& id) { heard.emplace_back(change, id); });
    const QString first = model.create();
    const QString second = model.create();
    (void)model.rename(second, QStringLiteral("Masters"));
    (void)model.setCurrent(second);
    (void)model.setCurrent(second);
    (void)model.setLocked(second, true);
    (void)model.setDetached(second, true);
    (void)model.move(second, 0);
    (void)model.close(second);
    (void)model.close(first);
    const std::vector<std::pair<Change, QString>> expected = {
        {Change::Added, first},    {Change::Current, first}, {Change::Added, second},    {Change::Renamed, second},
        {Change::Current, second}, {Change::Locked, second}, {Change::Detached, second}, {Change::Moved, second},
        {Change::Current, first},  {Change::Closed, second},
    };
    EXPECT_EQ(heard, expected);

    model.unsubscribe(token);
    (void)model.create();
    EXPECT_EQ(heard.size(), expected.size());
}

// The model has no widget: what it includes is the standard library and Qt's strings.
TEST(WorkspaceModel, IncludesNoWidgetHeader)
{
    const QSet<QString> allowedQt = {QStringLiteral("QString"), QStringLiteral("QStringList"),
                                     QStringLiteral("QtGlobal")};
    const QRegularExpression include(QStringLiteral(R"(^\s*#\s*include\s*([<"])([^>"]+)[>"])"),
                                     QRegularExpression::MultilineOption);
    for (const QString& path : {QStringLiteral("src/AfterNightShellCore/views/WorkspaceModel.h"),
                                QStringLiteral("src/AfterNightShellCore/views/WorkspaceModel.cpp")})
    {
        const QString file = repositoryFile(path);
        ASSERT_FALSE(file.isEmpty()) << path.toStdString() << " was not found";
        QFile source(file);
        ASSERT_TRUE(source.open(QIODevice::ReadOnly)) << file.toStdString();
        QRegularExpressionMatchIterator matches = include.globalMatch(QString::fromUtf8(source.readAll()));
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(2);
            if (match.captured(1) == QStringLiteral("<") && name.startsWith(QLatin1Char('Q')))
            {
                EXPECT_TRUE(allowedQt.contains(name))
                    << path.toStdString() << " includes <" << name.toStdString() << ">";
            }
            else if (match.captured(1) == QStringLiteral("\""))
            {
                EXPECT_TRUE(name == QStringLiteral("stdafx.hpp") || name == QStringLiteral("views/WorkspaceModel.h") ||
                            name == QStringLiteral("AfterNightShellCoreExport.h"))
                    << path.toStdString() << " includes \"" << name.toStdString() << "\"";
            }
        }
    }
}
