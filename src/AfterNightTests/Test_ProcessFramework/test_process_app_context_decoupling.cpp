// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Where the process windows put their temporary work: below the temporary directory the application context lends, or
/// below the system's when the context lends none - an external tool's hand-off, the list analyzer's proxies and the
/// plate solver's work directory all ask for it this way.

#include "Test_ProcessFramework/FrameworkCaseSupport.h"

#include "controls/IAppContext.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace
{

using namespace an::tests::processes;

/// Lends the suite context's temporary files or withholds them for its lifetime, and puts the lending back.
class ScopedTemporaryFiles
{
public:
    explicit ScopedTemporaryFiles(bool lend) : m_previous(framework::suiteContext().lendTemporaryFiles)
    {
        framework::suiteContext().lendTemporaryFiles = lend;
    }
    ~ScopedTemporaryFiles()
    {
        framework::suiteContext().lendTemporaryFiles = m_previous;
    }
    ScopedTemporaryFiles(const ScopedTemporaryFiles&) = delete;
    ScopedTemporaryFiles& operator=(const ScopedTemporaryFiles&) = delete;

private:
    bool m_previous;
};

} // namespace

// Temporary work goes below the directory the context's temporary-file service names; with no service lent it goes
// below the system's temporary directory.
TEST(AppContextTemporaryFilesTest, TemporaryWorkGoesBelowTheContextsDirectoryOrTheSystemsWhenNoneIsLent)
{
    {
        const ScopedTemporaryFiles lent(true);
        an::ui::ITempFileService* service = framework::suiteContext().getTempFileService();
        ASSERT_NE(service, nullptr);
        const QString expected = service->getTempDirPath();
        ASSERT_FALSE(expected.isEmpty());

        const QString chosen = an::ui::ensureTempDirPathOrSystemTemp();
        EXPECT_TRUE(QFileInfo(chosen).isDir());
        EXPECT_EQ(QFileInfo(chosen).canonicalFilePath(), QFileInfo(expected).canonicalFilePath());

        // A temporary directory a window makes there lies below the context's.
        QTemporaryDir work(chosen + QStringLiteral("/work_XXXXXX"));
        ASSERT_TRUE(work.isValid());
        EXPECT_TRUE(QFileInfo(work.path()).canonicalFilePath().startsWith(QFileInfo(expected).canonicalFilePath()));
    }
    {
        const ScopedTemporaryFiles withheld(false);
        ASSERT_EQ(framework::suiteContext().getTempFileService(), nullptr);
        EXPECT_EQ(QFileInfo(an::ui::ensureTempDirPathOrSystemTemp()).canonicalFilePath(),
                  QFileInfo(QDir::tempPath()).canonicalFilePath());
    }
}
