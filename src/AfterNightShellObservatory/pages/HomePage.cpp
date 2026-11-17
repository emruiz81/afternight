// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/HomePage.h"

#include "controls/DashboardPreferences.h"
#include "controls/DashboardSettings.h"
#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardUtils.h"
#include "controls/HomeContinueRow.h"
#include "controls/HomeParts.h"
#include "controls/HomeProjectsRow.h"
#include "controls/HomeQuickActions.h"
#include "controls/HomeRecentGrid.h"
#include "controls/HomeWelcome.h"
#include "controls/IAppContext.h"
#include "projects/ProjectService.h"
#include "theme/ComponentParts.h"

#include <QDate>
#include <QHBoxLayout>
#include <QLocale>
#include <QPointer>
#include <QThreadPool>
#include <QSet>
#include <QShowEvent>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <deque>
#include <string>

W_OBJECT_IMPL(an::ui::observatory::HomePage)

namespace an::ui::observatory
{

namespace
{

/// The recent files as the application keeps them, most recent first, each once and normalized - as the dashboard
/// reads them.
std::deque<QString> recentFiles()
{
    std::deque<QString> files;
    an::ui::IAppContext* context = an::ui::appContext();
    an::ui::IRecentFilesService* service = context != nullptr ? context->getRecentFilesService() : nullptr;
    if (service == nullptr)
    {
        return files;
    }
    QSet<QString> seen;
    for (const std::string& entry : service->getRecentFilesList())
    {
        const QString path = DashboardUtils::normalizeRecentFilePath(QString::fromUtf8(entry));
        if (!path.isEmpty() && !seen.contains(path))
        {
            seen.insert(path);
            files.push_back(path);
        }
    }
    return files;
}

/// What a project's card says, from what its folder holds.
an::ui::HomeProjectSummary cardOf(const an::ui::shell::RecentProject& recent, const an::ui::shell::ProjectSummary& read)
{
    an::ui::HomeProjectSummary card;
    card.folder = recent.folder;
    card.name = read.read ? read.name : recent.name;
    card.present = read.read;
    if (!read.read)
    {
        return card;
    }
    if (read.object.has_value())
    {
        card.object = read.object->name;
        switch (read.object->source)
        {
        case an::ui::shell::ProjectObjectSource::ObjectCard:
            card.objectSource = QCoreApplication::translate("an::ui::observatory::HomePage", "the OBJECT card");
            break;
        case an::ui::shell::ProjectObjectSource::PlateSolution:
            card.objectSource = QCoreApplication::translate("an::ui::observatory::HomePage", "a plate solution");
            break;
        case an::ui::shell::ProjectObjectSource::User:
            card.objectSource = QCoreApplication::translate("an::ui::observatory::HomePage", "its name");
            break;
        }
    }
    card.images = read.images;
    card.sequences = read.sequences;
    card.integrations = read.integrations;
    card.steps = read.steps;
    card.workspaces = read.workspaces;
    card.bytes = static_cast<qint64>(read.bytes);
    card.lastOpened = read.lastOpened;
    card.fromIntegrationJob = read.origin == an::ui::shell::ProjectOrigin::IntegrationJob;
    card.picture = read.lastImage;
    return card;
}

} // namespace

struct HomePage::Impl
{
    an::ui::HomeText* date = nullptr;
    an::ui::HomeQuickActions* actions = nullptr;
    an::ui::HomeWelcome* welcome = nullptr;
    an::ui::HomeContinueRow* continueRow = nullptr;
    an::ui::HomeRecentGrid* recent = nullptr;
    an::ui::HomeProjectsRow* projectsRow = nullptr;
    DashboardThumbnailCache cache{256};
    bool welcomeShown = true;
    bool hasRecentFiles = false;
    // The projects used last and what their folders hold, read on the thread pool; a read asked for after it drops it.
    std::vector<an::ui::HomeProjectSummary> projects;
    quint64 projectsRead = 0;
};

HomePage::HomePage(QWidget* parent) : QScrollArea(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("observatoryHomePage"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("observatoryHomeContent"));
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(an::ui::parts::kHomePagePaddingSide, an::ui::parts::kHomePagePaddingTop,
                               an::ui::parts::kHomePagePaddingSide, an::ui::parts::kHomePagePaddingTop);
    column->setSpacing(an::ui::parts::kHomeSectionGap);

    // The date over the heading, the quick actions at their right.
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    auto* titles = new QVBoxLayout();
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(0);
    m_impl->date = new an::ui::HomeText(an::ui::HomeText::Role::Caps, QString(), content);
    m_impl->date->setObjectName(QStringLiteral("observatoryHomeDate"));
    titles->addWidget(m_impl->date);
    auto* heading = new an::ui::HomeText(an::ui::HomeText::Role::Heading, tr("Home"), content);
    heading->setObjectName(QStringLiteral("observatoryHomeHeading"));
    heading->setElideMode(Qt::ElideNone);
    titles->addWidget(heading);
    header->addLayout(titles, 0);
    header->addStretch(1);
    m_impl->actions = new an::ui::HomeQuickActions(content);
    // What this shell offers beside the dashboard's actions: a project.
    m_impl->actions->addRowAction(QString::fromLatin1(kNewProjectAction), tr("New project"),
                                  QStringLiteral("folder_closed"),
                                  tr("Make a project: a folder that keeps the history of its images. Optional - "
                                     "without one, AfterNight works as it always has."));
    header->addWidget(m_impl->actions, 0, Qt::AlignBottom);
    column->addLayout(header);

    m_impl->welcome = new an::ui::HomeWelcome(content);
    column->addWidget(m_impl->welcome, 1);
    m_impl->continueRow = new an::ui::HomeContinueRow(content);
    m_impl->continueRow->setThumbnailCache(&m_impl->cache);
    column->addWidget(m_impl->continueRow);
    m_impl->projectsRow = new an::ui::HomeProjectsRow(content);
    m_impl->projectsRow->hide();
    column->addWidget(m_impl->projectsRow);
    m_impl->recent = new an::ui::HomeRecentGrid(content);
    m_impl->recent->setThumbnailCache(&m_impl->cache);
    column->addWidget(m_impl->recent);
    column->addStretch(0);
    setWidget(content);

    // What the parts ask for, in the dashboard's signals.
    an::ui::HomeQuickActions* const actions = m_impl->actions;
    QObject::connect(actions, &an::ui::HomeQuickActions::openFileRequested, this,
                     [this]() { emit openFileRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::newSequenceRequested, this,
                     [this]() { emit newSequenceRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::imageIntegrationRequested, this,
                     [this]() { emit imageIntegrationRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::openExtensionManagerRequested, this,
                     [this]() { emit openExtensionManagerRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::clearRecentFilesRequested, this,
                     [this]() { emit clearRecentFilesRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::systemInformationRequested, this,
                     [this]() { emit systemInformationRequested(); });
    QObject::connect(actions, &an::ui::HomeQuickActions::rowActionRequested, this, [this](const QString& actionId) {
        if (actionId == QLatin1String(kNewProjectAction))
        {
            emit newProjectRequested();
        }
    });
    QObject::connect(m_impl->welcome, &an::ui::HomeWelcome::newProjectRequested, this,
                     [this]() { emit newProjectRequested(); });
    QObject::connect(m_impl->welcome, &an::ui::HomeWelcome::openFileRequested, this,
                     [this]() { emit openFileRequested(); });
    QObject::connect(m_impl->welcome, &an::ui::HomeWelcome::imageIntegrationRequested, this,
                     [this]() { emit imageIntegrationRequested(); });
    QObject::connect(m_impl->welcome, &an::ui::HomeWelcome::openHelpRequested, this,
                     [this]() { emit openHelpRequested(); });
    QObject::connect(m_impl->continueRow, &an::ui::HomeContinueRow::openRecentFileRequested, this,
                     [this](const QString& filePath) { emit openRecentFileRequested(filePath); });
    QObject::connect(m_impl->continueRow, &an::ui::HomeContinueRow::openProjectRequested, this,
                     [this](const QString& folder) { emit openProjectRequested(folder); });
    QObject::connect(m_impl->projectsRow, &an::ui::HomeProjectsRow::openProjectRequested, this,
                     [this](const QString& folder) { emit openProjectRequested(folder); });
    QObject::connect(m_impl->projectsRow, &an::ui::HomeProjectsRow::openOtherProjectRequested, this,
                     [this]() { emit openOtherProjectRequested(); });
    QObject::connect(m_impl->recent, &an::ui::HomeRecentGrid::openFileRequested, this,
                     [this](const QString& filePath) { emit openRecentFileRequested(filePath); });
    QObject::connect(m_impl->recent, &an::ui::HomeRecentGrid::removeFileRequested, this,
                     [this](const QString& filePath) {
                         emit removeRecentFileRequested(filePath);
                         reloadRecentFiles();
                     });

    reloadRecentFiles();
}

HomePage::~HomePage() = default;

void HomePage::reloadRecentFiles()
{
    const std::deque<QString> files = recentFiles();
    m_impl->hasRecentFiles = !files.empty();
    showRows();
    m_impl->continueRow->setRecentFiles(files);
    m_impl->recent->setRecentFiles(files);
    m_impl->actions->setActionEnabled(QString::fromUtf8(DashboardPreferences::ACTION_ID_CLEAR_RECENTS), !files.empty());
}

void HomePage::setRecentProjects(const std::vector<an::ui::shell::RecentProject>& recent)
{
    const quint64 read = ++m_impl->projectsRead;
    if (recent.empty())
    {
        m_impl->projects.clear();
        m_impl->projectsRow->setProjects({});
        m_impl->continueRow->setLastProject(std::nullopt);
        showRows();
        return;
    }
    // What each folder holds is read on the thread pool, and shown when it lands.
    const QPointer<HomePage> self(this);
    QThreadPool::globalInstance()->start([self, read, recent]() {
        std::vector<an::ui::HomeProjectSummary> cards;
        for (const an::ui::shell::RecentProject& project : recent)
        {
            cards.push_back(cardOf(project, an::ui::shell::readProjectSummary(project.folder)));
        }
        QCoreApplication* const application = QCoreApplication::instance();
        if (application == nullptr)
        {
            return;
        }
        QMetaObject::invokeMethod(
            application,
            [self, read, cards = std::move(cards)]() mutable {
                if (!self || read != self->m_impl->projectsRead)
                {
                    return;
                }
                self->m_impl->projects = std::move(cards);
                self->m_impl->projectsRow->setProjects(self->m_impl->projects);
                self->m_impl->continueRow->setLastProject(self->m_impl->projects.front());
                self->showRows();
            },
            Qt::QueuedConnection);
    });
}

void HomePage::showRows()
{
    // The welcome while there is nothing to show yet: no recent file and no project used last.
    const bool projects = !m_impl->projects.empty();
    m_impl->welcomeShown = !m_impl->hasRecentFiles && !projects;
    m_impl->welcome->setVisible(m_impl->welcomeShown);
    m_impl->continueRow->setVisible(!m_impl->welcomeShown);
    m_impl->projectsRow->setVisible(projects);
    m_impl->recent->setVisible(m_impl->hasRecentFiles);
}

void HomePage::recentFilesCleared()
{
    // The pins go with the files they pinned, as the dashboard forgets them.
    an::ui::storePinnedRecentFiles({});
    an::ui::storePinnedFeaturedFile(QString());
    m_impl->cache.clear();
    reloadRecentFiles();
}

void HomePage::refreshIcons()
{
    m_impl->actions->refreshIcons();
    m_impl->welcome->refreshIcons();
    m_impl->continueRow->refreshIcons();
    m_impl->projectsRow->refreshIcons();
    m_impl->recent->refreshIcons();
}

bool HomePage::showsWelcome() const
{
    return m_impl->welcomeShown;
}

an::ui::HomeQuickActions* HomePage::quickActions() const noexcept
{
    return m_impl->actions;
}

an::ui::HomeContinueRow* HomePage::continueRow() const noexcept
{
    return m_impl->continueRow;
}

an::ui::HomeRecentGrid* HomePage::recentGrid() const noexcept
{
    return m_impl->recent;
}

an::ui::HomeWelcome* HomePage::welcome() const noexcept
{
    return m_impl->welcome;
}

an::ui::HomeProjectsRow* HomePage::projectsRow() const noexcept
{
    return m_impl->projectsRow;
}

void HomePage::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    // The day the page is shown on. The recent files are read when they change - the window says so - never because
    // the page is shown: changing place asks the engine for nothing.
    m_impl->date->setText(QLocale().toString(QDate::currentDate(), QStringLiteral("dddd · d MMMM yyyy")));
}

} // namespace an::ui::observatory
