// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QTextEdit>

#include "processes/ImageIntegration/dialogs/ImportProgressDialog.h"

namespace
{
void ensureApp()
{
    if (!QApplication::instance())
    {
        static int argc = 0;
        static char* argv[] = {nullptr};
        static QApplication app(argc, argv);
        (void)app;
    }
}

QString readRepoSourceFile(const QString& relativePath)
{
    QStringList seeds = {QDir::currentPath(), QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()};
    if (QCoreApplication::instance())
    {
        seeds.append(QCoreApplication::applicationDirPath());
    }

    for (const QString& seed : seeds)
    {
        QDir dir(seed);
        while (true)
        {
            const QString candidate = dir.filePath(relativePath);
            if (QFileInfo::exists(candidate))
            {
                QFile file(candidate);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    return QString::fromUtf8(file.readAll());
                }
            }

            if (!dir.cdUp())
            {
                break;
            }
        }
    }

    return {};
}
} // namespace

TEST(ImportProgressDialogTest, ImageIntegrationImportProgressUsesAsyncDialog)
{
    // performImportWithProgress() lives in ImageIntegrationProcessImport.cpp, beside the import
    // session it drives.
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcessImport.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const int functionStart = source.indexOf(QStringLiteral("void ImageIntegrationProcess::performImportWithProgress"));
    ASSERT_GE(functionStart, 0);
    const int nextFunction =
        source.indexOf(QStringLiteral("\nvoid ImageIntegrationProcess::importFilesDialog"), functionStart);
    ASSERT_GT(nextFunction, functionStart);

    const QString importFunction = source.mid(functionStart, nextFunction - functionStart);
    EXPECT_FALSE(importFunction.contains(QRegularExpression(QStringLiteral(R"(\.exec\s*\()"))));
    EXPECT_TRUE(importFunction.contains(QRegularExpression(QStringLiteral(R"(\->open\s*\()"))));
    EXPECT_TRUE(importFunction.contains(QStringLiteral("QMetaObject::invokeMethod")));
}

TEST(ImportProgressDialogTest, ImageIntegrationFrameMutationConfirmationsUseAsyncDialogs)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const auto functionSource = [&source](const QString& signature, const QString& nextSignature) {
        const int start = source.indexOf(signature);
        if (start < 0)
        {
            return QString();
        }
        const int end = source.indexOf(nextSignature, start + signature.size());
        if (end <= start)
        {
            return QString();
        }
        return source.mid(start, end - start);
    };

    const QStringList snippets = {
        functionSource(QStringLiteral("void ImageIntegrationProcess::onClearSelected"),
                       QStringLiteral("void ImageIntegrationProcess::onClearAllInTab")),
        functionSource(QStringLiteral("void ImageIntegrationProcess::onClearAllInTab"),
                       QStringLiteral("void ImageIntegrationProcess::onClearAll")),
        functionSource(QStringLiteral("void ImageIntegrationProcess::onClearAll"),
                       QStringLiteral("void ImageIntegrationProcess::onReset")),
        functionSource(QStringLiteral("void ImageIntegrationProcess::onReset"),
                       QStringLiteral("void ImageIntegrationProcess::autoDetectMasters")),
    };

    for (const QString& snippet : snippets)
    {
        ASSERT_FALSE(snippet.isEmpty());
        EXPECT_FALSE(snippet.contains(QRegularExpression(QStringLiteral(R"(\.exec\s*\()"))));
        EXPECT_FALSE(snippet.contains(
            QRegularExpression(QStringLiteral(R"(QMessageBox::(warning|information|critical|question)\s*\()"))));
        EXPECT_TRUE(snippet.contains(QStringLiteral("openNonBlockingQuestionBox")));
    }
}

TEST(ImportProgressDialogTest, ImageIntegrationImportIssueDetailsUseAsyncDialog)
{
    // Same move as above: the import issue reporting lives in ImageIntegrationProcessImport.cpp.
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcessImport.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const int issuesStart = source.indexOf(QStringLiteral("if (importResult.hasIssues())"));
    ASSERT_GE(issuesStart, 0);
    const int batchStart = source.indexOf(QStringLiteral("PreparedImportBatch batch"), issuesStart);
    ASSERT_GT(batchStart, issuesStart);

    const QString issueSnippet = source.mid(issuesStart, batchStart - issuesStart);
    EXPECT_TRUE(issueSnippet.contains(QStringLiteral("openNonBlockingMessageBox")));
    EXPECT_TRUE(issueSnippet.contains(QStringLiteral("importResult.detailedText()")));
    EXPECT_FALSE(issueSnippet.contains(QRegularExpression(QStringLiteral(R"(\.exec\s*\()"))));
}

TEST(ImportProgressDialogTest, ImageIntegrationExecutionNoticesUseAsyncDialogs)
{
    const QString source = readRepoSourceFile(
        QStringLiteral("src/AfterNightProcesses/processes/ImageIntegration/ImageIntegrationProcess.cpp"));
    ASSERT_FALSE(source.isEmpty());

    const auto functionSource = [&source](const QString& signature, const QString& nextSignature) {
        const int start = source.indexOf(signature);
        if (start < 0)
        {
            return QString();
        }
        const int end = source.indexOf(nextSignature, start + signature.size());
        if (end <= start)
        {
            return QString();
        }
        return source.mid(start, end - start);
    };

    const QStringList snippets = {
        functionSource(QStringLiteral("void ImageIntegrationProcess::onExecute"),
                       QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup")),
        functionSource(QStringLiteral("void ImageIntegrationProcess::onRunIntegrationForGroup"),
                       QStringLiteral("namespace\n{")),
        functionSource(QStringLiteral("void ImageIntegrationProcess::onRunRegistrationForAllGroups"),
                       QStringLiteral("void ImageIntegrationProcess::onFixRequested")),
    };

    for (const QString& snippet : snippets)
    {
        ASSERT_FALSE(snippet.isEmpty());
        EXPECT_TRUE(snippet.contains(QStringLiteral("openNonBlockingMessageBox")));
        EXPECT_FALSE(snippet.contains(
            QRegularExpression(QStringLiteral(R"(QMessageBox::(warning|information|critical)\s*\()"))));
    }
}

TEST(ImportProgressDialogTest, ShowsCurrentFile)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(10);
    dlg.setCurrentFile(QStringLiteral("M42_001.fit"));

    ASSERT_TRUE(dlg.currentLabel() != nullptr);
    EXPECT_TRUE(dlg.currentLabel()->text().contains(QStringLiteral("M42_001.fit")));
}

TEST(ImportProgressDialogTest, ShowsMetadataErrorsInline)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(10);
    dlg.setIssueCount(2);
    dlg.setIssueDetails(
        {QStringLiteral("M42_001.fit: Corrupt header"), QStringLiteral("M42_002.fit: Missing keyword")});

    ASSERT_TRUE(dlg.issuesLabel() != nullptr);
    EXPECT_TRUE(dlg.issuesLabel()->text().contains(QStringLiteral("2")));

    ASSERT_TRUE(dlg.issuesTextEdit() != nullptr);
    EXPECT_FALSE(dlg.issuesTextEdit()->isHidden());
    EXPECT_TRUE(dlg.issuesTextEdit()->toPlainText().contains(QStringLiteral("Corrupt header")));
}

TEST(ImportProgressDialogTest, HidesIssueDetailsWhenNone)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(10);
    dlg.setIssueCount(0);
    dlg.setIssueDetails({});

    ASSERT_TRUE(dlg.issuesTextEdit() != nullptr);
    EXPECT_TRUE(dlg.issuesTextEdit()->isHidden());
}

TEST(ImportProgressDialogTest, RejectMarksImportCancelled)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(3);

    dlg.reject();

    EXPECT_TRUE(dlg.isCancelled());
}

TEST(ImportProgressDialogTest, WindowCloseMarksImportCancelled)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(3);

    QCloseEvent event;
    QApplication::sendEvent(&dlg, &event);

    EXPECT_TRUE(dlg.isCancelled());
}

TEST(ImportProgressDialogTest, CompletedProgressDoesNotMarkImportCancelled)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(3);

    dlg.setProgress(3);

    EXPECT_FALSE(dlg.isCancelled());
}

TEST(ImportProgressDialogTest, CancelButtonMarksImportCancelled)
{
    ensureApp();

    ImageIntegration::ImportProgressDialog dlg(3);
    ASSERT_TRUE(dlg.cancelButton() != nullptr);

    dlg.cancelButton()->click();

    EXPECT_TRUE(dlg.isCancelled());
}
