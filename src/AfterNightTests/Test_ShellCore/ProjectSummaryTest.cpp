// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// What Home reads of a project's folder - its name, its object and origin, what it holds, the steps of its images'
// histories, its size and the image opened last - and a folder with no project read as none; and the storage report,
// which tells the frames a run makes on its way from the masters it keeps and is measured off the GUI thread.

#include "projects/ProjectService.h"

#include "ShellCoreTestSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using an::tests::shellcore::MemorySettings;
using an::ui::shell::ProjectCreateRequest;
using an::ui::shell::ProjectImageKind;
using an::ui::shell::ProjectObject;
using an::ui::shell::ProjectObjectSource;
using an::ui::shell::ProjectService;
using an::ui::shell::ProjectStorageReport;
using an::ui::shell::ProjectSummary;

bool writeBytes(const QString& path, const qint64 size)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(static_cast<int>(size), 'x')) == size;
}

std::uint64_t bytesBelow(const QString& folder)
{
    std::uint64_t bytes = 0;
    QDirIterator files(folder, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        bytes += static_cast<std::uint64_t>(QFileInfo(files.next()).size());
    }
    return bytes;
}

class ProjectSummaryCase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_root.isValid());
        m_service = std::make_unique<ProjectService>();
        m_service->setSettings(&m_settings);
    }

    [[nodiscard]] QString under(const QString& relative) const
    {
        return QDir(m_root.path()).filePath(relative);
    }

    QTemporaryDir m_root;
    MemorySettings m_settings;
    std::unique_ptr<ProjectService> m_service;
};

using ProjectSummaryRead = ProjectSummaryCase;

} // namespace

// A project's folder read for Home: its document's facts, the steps its images' histories keep, its size and the image
// opened last.
TEST_F(ProjectSummaryRead, ReadsWhatAProjectsFolderHolds)
{
    ProjectCreateRequest request;
    request.name = QStringLiteral("Carina Nebula");
    request.location = m_root.path();
    request.object = ProjectObject{QStringLiteral("NGC 3372"), ProjectObjectSource::ObjectCard};
    request.origin = an::ui::shell::ProjectOrigin::IntegrationJob;
    ASSERT_TRUE(m_service->create(request).ok());
    const QString folder = m_service->currentFolder();
    const QString first = under(QStringLiteral("frames/rgb.fit"));
    const QString second = under(QStringLiteral("frames/lum.fit"));
    const QString sequence = under(QStringLiteral("frames/saturn.ser"));
    for (const QString& file : {first, second, sequence})
    {
        ASSERT_TRUE(writeBytes(file, 64));
    }
    ASSERT_NE(m_service->addImage(first, ProjectImageKind::Image), nullptr);
    QThread::msleep(5);
    ASSERT_NE(m_service->addImage(sequence, ProjectImageKind::Sequence), nullptr);
    QThread::msleep(5);
    ASSERT_NE(m_service->addImage(second, ProjectImageKind::Image), nullptr);
    ASSERT_TRUE(m_service->close());

    const ProjectSummary summary = an::ui::shell::readProjectSummary(folder);
    ASSERT_TRUE(summary.read);
    EXPECT_EQ(summary.name, QStringLiteral("Carina Nebula"));
    ASSERT_TRUE(summary.object.has_value());
    EXPECT_EQ(summary.object->name, QStringLiteral("NGC 3372"));
    EXPECT_EQ(summary.origin, an::ui::shell::ProjectOrigin::IntegrationJob);
    EXPECT_EQ(summary.images, 2);
    EXPECT_EQ(summary.sequences, 1);
    EXPECT_EQ(summary.steps, 0u);
    EXPECT_EQ(summary.bytes, bytesBelow(folder));
    EXPECT_EQ(QDir::cleanPath(summary.lastImage), QDir::cleanPath(second));
    EXPECT_TRUE(summary.lastOpened.isValid());

    const ProjectSummary none = an::ui::shell::readProjectSummary(under(QStringLiteral("frames")));
    EXPECT_FALSE(none.read) << "a folder with no project was read as one";
}

// The storage report tells the frames a run makes on its way - calibrated, corrected and registered lights - from the
// masters, stacks and reports a run keeps, and is measured off the GUI thread.
TEST_F(ProjectSummaryRead, TheStorageReportTellsTheIntermediateFramesApartOffTheGuiThread)
{
    ProjectCreateRequest request;
    request.name = QStringLiteral("Storage");
    request.location = m_root.path();
    ASSERT_TRUE(m_service->create(request).ok());
    const QDir integration(QDir(m_service->currentFolder()).filePath(QStringLiteral("integration")));
    ASSERT_TRUE(writeBytes(integration.filePath(QStringLiteral("job/stacked_L.fit")), 300));
    ASSERT_TRUE(writeBytes(integration.filePath(QStringLiteral("job/calibrated_lights/a.fit")), 1000));
    ASSERT_TRUE(writeBytes(integration.filePath(QStringLiteral("job/ca_corrected_lights/a.fit")), 2000));
    ASSERT_TRUE(writeBytes(integration.filePath(QStringLiteral("job/registered_lights/group/a.fit")), 4000));

    std::optional<ProjectStorageReport> measured;
    m_service->storageReport([&measured](const ProjectStorageReport& report) { measured = report; });
    EXPECT_FALSE(measured.has_value()) << "the report was told before the call returned";
    QElapsedTimer timer;
    timer.start();
    while (!measured.has_value() && timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    ASSERT_TRUE(measured.has_value());
    EXPECT_EQ(measured->intermediateBytes, 7000u);
    EXPECT_EQ(measured->integrationBytes, 300u);
    EXPECT_FALSE(measured->measuredOnGuiThread);
}
