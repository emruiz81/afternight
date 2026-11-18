// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectFlows.h"

#include "ShellActions.h"
#include "chrome/TopBar.h"

#include "core/ApplicationCore.h"
#include "projects/JobProjects.h"

#include "app/FileDialogUtils.h"
#include "app/MessageBoxes.h"
#include "controls/GlobalSettingsPersistence.h"
#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/ProjectSettings.h"
#include "controls/settings/ProjectsSettingsPage.h"
#include "dialogs/NewProjectDialog.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>

#include <atomic>
#include <memory>
#include <utility>

namespace an::ui::observatory
{

namespace
{

using an::ui::shell::ProjectFolderState;
using an::ui::shell::ProjectLockPolicy;
using an::ui::shell::ProjectOutcome;
using an::ui::shell::ProjectRefusal;
using an::ui::shell::ProjectService;
using an::ui::shell::RecentProject;

/// How the host's file systems tell names apart.
#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

/// What the project service says a folder is, as the New Project dialog reads it.
[[nodiscard]] an::ui::NewProjectFolderState dialogStateOf(const QString& folder)
{
    switch (ProjectService::folderState(folder))
    {
    case ProjectFolderState::Project:
        return an::ui::NewProjectFolderState::Project;
    case ProjectFolderState::Occupied:
        return an::ui::NewProjectFolderState::Occupied;
    case ProjectFolderState::Free:
        break;
    }
    return an::ui::NewProjectFolderState::Free;
}

} // namespace

ProjectFlows::ProjectFlows(QWidget& window, an::ui::shell::ApplicationCore& core, ShellActions& actions, TopBar& topBar,
                           Hooks hooks)
    : QObject(nullptr), m_window(window), m_core(core), m_actions(actions), m_topBar(topBar), m_hooks(std::move(hooks))
{
    // The application's folder dialog, as the settings say which it is.
    m_chooseFolder = [this](QWidget* parent, const QString& caption, const QString& start) {
        return an::ui::dialogs::getExistingDirectory(m_core.globalSettingsStore(), parent, caption, start);
    };
    m_chooseFile = [this](QWidget* parent, const QString& caption, const QString& start) {
        return an::ui::dialogs::getOpenFileName(m_core.globalSettingsStore(), parent, caption, start);
    };

    QObject::connect(m_actions.action(ids::kNewProject), &QAction::triggered, this, [this]() { newProject(); });
    QObject::connect(m_actions.action(ids::kOpenProject), &QAction::triggered, this, [this]() { openProject(); });
    QObject::connect(m_actions.action(ids::kSwitchProject), &QAction::triggered, this, [this]() { switchProject(); });
    QObject::connect(m_actions.action(ids::kCloseProject), &QAction::triggered, this, [this]() { closeProject(); });
    QObject::connect(m_actions.action(ids::kProjectSettings), &QAction::triggered, this,
                     [this]() { projectSettings(); });

    m_topBar.setProjectMenuFiller([this](QMenu& menu) { fillSwitcherMenu(menu); });
    m_subscription = m_core.projects().subscribe([this](ProjectService::Change /*change*/) { refresh(); });

    // A job that runs with no project open is given one in its output folder when the settings ask. What the window
    // adds is said and arranged here.
    an::ui::shell::JobProjects::Hooks forJobs;
    forJobs.made = [this]() { sayProjectForJob(true); };
    forJobs.opened = [this]() {
        // A project that was there is opened as Open Project opens it: what it keeps is put back.
        if (m_hooks.restoreArrangement)
        {
            (void)m_hooks.restoreArrangement();
        }
        sayProjectForJob(false);
    };
    forJobs.refused = [this](const ProjectOutcome& outcome) { say(outcome); };
    m_core.jobProjects().setHooks(std::move(forJobs));
    refresh();
}

ProjectFlows::~ProjectFlows()
{
    m_core.jobProjects().setHooks({});
    m_core.projects().unsubscribe(m_subscription);
    m_topBar.setProjectMenuFiller({});
}

void ProjectFlows::sayProjectForJob(const bool made)
{
    const an::ui::shell::ProjectDocument* const open = m_core.projects().current();
    if (open == nullptr || !m_hooks.snackbar)
    {
        return;
    }
    const QString said = made ? tr("Project %1 made for this integration").arg(open->name)
                              : tr("Project %1 opened for this integration").arg(open->name);
    m_hooks.snackbar(said, QDir::toNativeSeparators(m_core.projects().currentFolder()), tr("Settings..."),
                     [this]() { projectSettings(); });
}

void ProjectFlows::setFolderChooser(FolderChooser chooser)
{
    m_chooseFolder = std::move(chooser);
}

void ProjectFlows::setFileChooser(FileChooser chooser)
{
    m_chooseFile = std::move(chooser);
}

QMessageBox* ProjectFlows::missingImagesMessage() const noexcept
{
    return m_missing.data();
}

an::ui::NewProjectDialog* ProjectFlows::newProjectDialog() const noexcept
{
    return m_dialog.data();
}

QMessageBox* ProjectFlows::refusalMessage() const noexcept
{
    return m_refusal.data();
}

void ProjectFlows::newProject()
{
    if (!m_dialog.isNull())
    {
        m_dialog->raise();
        m_dialog->activateWindow();
        return;
    }
    an::ui::IGlobalSettingsProvider* const settings = m_core.globalSettings();
    const an::ui::ProjectSettings stored = an::ui::readProjectSettings(
        [settings](const QString& key, const QString& fallback) { return settings->select(key, fallback); });

    an::ui::NewProjectOptions options;
    options.location = stored.defaultLocation;
    options.compression = stored.historyCompression;
    const std::vector<OpenImage> open = m_hooks.openImages ? m_hooks.openImages() : std::vector<OpenImage>();
    options.openImages = static_cast<int>(open.size());
    if (!open.empty())
    {
        // What moving their undo history into the project moves: the files of their steps.
        std::vector<ImageViewControl*> views;
        for (const OpenImage& image : open)
        {
            views.push_back(image.view.data());
        }
        options.openHistoryBytes = static_cast<qint64>(an::ui::shell::ProjectHistoryBinder::historyBytesOf(views));
    }
    options.folderState = [](const QString& folder) { return dialogStateOf(folder); };

    // Over the window, and answered when the person answers it: nothing waits meanwhile.
    auto* const dialog = new an::ui::NewProjectDialog(std::move(options), &m_window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    QObject::connect(dialog, &QDialog::accepted, this, [this, dialog]() { accepted(dialog->choice()); });
    m_dialog = dialog;
    dialog->open();
}

void ProjectFlows::accepted(const an::ui::NewProjectChoice& choice)
{
    if (choice.openExisting)
    {
        openProjectFolder(choice.folder);
        return;
    }
    // The images open now are read before the project is: they join it when the person kept the choice.
    const std::vector<OpenImage> open =
        choice.addOpenImages && m_hooks.openImages ? m_hooks.openImages() : std::vector<OpenImage>();

    an::ui::shell::ProjectCreateRequest request;
    request.name = choice.name;
    request.location = choice.location;
    request.compression = choice.compression;
    if (!choice.object.isEmpty())
    {
        request.object = an::ui::shell::ProjectObject{choice.object, an::ui::shell::ProjectObjectSource::User};
    }
    const ProjectOutcome made = m_core.projects().create(request);
    if (!made.ok())
    {
        // A project made there meanwhile is opened, as the dialog would have offered.
        if (*made.refusal == ProjectRefusal::AlreadyAProject)
        {
            openProjectFolder(made.folder);
            return;
        }
        say(made);
        return;
    }
    // Each image is the project's from here on; the steps it has go into the project or stay with the session.
    const an::ui::history::StepsMove move =
        choice.moveHistory ? an::ui::history::StepsMove::Move : an::ui::history::StepsMove::Leave;
    for (const OpenImage& image : open)
    {
        if (image.view.isNull())
        {
            continue;
        }
        (void)m_core.projectHistory().join(*image.view, move, choice.moveHistory ? moveProgress() : nullptr);
    }
}

an::ui::shell::ProjectHistoryBinder::Progress ProjectFlows::moveProgress()
{
    // Told on a worker. On one volume the steps are renamed and it is over at once; across volumes they are copied,
    // and the window says how far that is, a second apart at most, once it has taken a second.
    struct Pace
    {
        QElapsedTimer started;
        std::atomic<qint64> lastSaidMs{0};
    };
    auto pace = std::make_shared<Pace>();
    pace->started.start();
    const QPointer<ProjectFlows> self(this);
    return [self, pace](const std::uint64_t moved, const std::uint64_t total) {
        const qint64 now = pace->started.elapsed();
        if (now < 1000 || now - pace->lastSaidMs.load() < 1000)
        {
            return;
        }
        pace->lastSaidMs.store(now);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, moved, total]() {
                if (self.isNull() || !self->m_hooks.snackbar)
                {
                    return;
                }
                const QLocale locale;
                self->m_hooks.snackbar(tr("Moving the undo history into the project"),
                                       tr("%1 of %2")
                                           .arg(locale.formattedDataSize(static_cast<qint64>(moved), 1,
                                                                         QLocale::DataSizeTraditionalFormat),
                                                locale.formattedDataSize(static_cast<qint64>(total), 1,
                                                                         QLocale::DataSizeTraditionalFormat)),
                                       QString(), {});
            },
            Qt::QueuedConnection);
    };
}

void ProjectFlows::openProject()
{
    if (!m_chooseFolder)
    {
        return;
    }
    // From the open project's folder's place, or where new projects are made.
    an::ui::IGlobalSettingsProvider* const settings = m_core.globalSettings();
    const an::ui::ProjectSettings stored = an::ui::readProjectSettings(
        [settings](const QString& key, const QString& fallback) { return settings->select(key, fallback); });
    const QString current = m_core.projects().currentFolder();
    QString start = current.isEmpty() ? stored.defaultLocation : QFileInfo(current).absolutePath();
    if (!QFileInfo(start).isDir())
    {
        start = QDir::homePath();
    }
    const QString folder = m_chooseFolder(&m_window, tr("Open Project"), start);
    if (!folder.isEmpty())
    {
        openProjectFolder(folder);
    }
}

void ProjectFlows::openProjectFolder(const QString& folder, const ProjectLockPolicy policy)
{
    ProjectService& projects = m_core.projects();
    const QString before = projects.currentFolder();
    const ProjectOutcome opened = projects.open(folder, policy);
    if (!opened.ok())
    {
        say(opened);
        return;
    }
    // A project that opened - not the one that was open already - gets its workspaces and its images back.
    if (before.compare(projects.currentFolder(), kPathCase) != 0 && m_hooks.restoreArrangement)
    {
        (void)m_hooks.restoreArrangement();
    }
}

void ProjectFlows::switchProject()
{
    m_topBar.showProjectMenu();
}

void ProjectFlows::closeProject()
{
    (void)m_core.projects().close();
}

void ProjectFlows::projectSettings()
{
    if (m_hooks.openSettingsAt)
    {
        m_hooks.openSettingsAt(an::ui::ProjectsSettingsPage::title());
    }
}

void ProjectFlows::restoreLastProject()
{
    const std::optional<ProjectOutcome> restored = m_core.projects().openLastIfAsked();
    if (!restored.has_value())
    {
        return;
    }
    if (!restored->ok())
    {
        m_restoreRefusal = restored;
        return;
    }
    // The project is open before the window shows; its images open meanwhile, and are arranged once they are.
    if (m_hooks.restoreArrangement)
    {
        (void)m_hooks.restoreArrangement();
    }
}

void ProjectFlows::sayRestoreRefusal()
{
    if (!m_restoreRefusal.has_value() || !m_hooks.snackbar)
    {
        return;
    }
    // The application started with no project and says why; Open... tries the project as Open Project does.
    const QString folder = m_restoreRefusal->folder;
    const QString why = m_restoreRefusal->why;
    m_restoreRefusal.reset();
    m_hooks.snackbar(tr("The project %1 was not opened").arg(QFileInfo(folder).fileName()), why, tr("Open..."),
                     [this, folder]() { openProjectFolder(folder); });
}

void ProjectFlows::fillSwitcherMenu(QMenu& menu)
{
    ProjectService& projects = m_core.projects();
    const QString openFolder = projects.currentFolder();
    if (const an::ui::shell::ProjectDocument* const open = projects.current())
    {
        // The open project, checked; choosing it changes nothing.
        QAction* const current = menu.addAction(open->name);
        current->setObjectName(QStringLiteral("projectMenuCurrent"));
        current->setCheckable(true);
        current->setChecked(true);
        current->setToolTip(QDir::toNativeSeparators(openFolder));
        QObject::connect(current, &QAction::triggered, current, [current]() { current->setChecked(true); });
    }
    for (const RecentProject& recent : projects.recent())
    {
        if (!openFolder.isEmpty() && recent.folder.compare(openFolder, kPathCase) == 0)
        {
            continue;
        }
        QAction* const entry = menu.addAction(recent.name);
        entry->setObjectName(QStringLiteral("projectMenuRecent"));
        entry->setData(recent.folder);
        entry->setToolTip(QDir::toNativeSeparators(recent.folder));
        const QString folder = recent.folder;
        QObject::connect(entry, &QAction::triggered, this, [this, folder]() { openProjectFolder(folder); });
    }
    if (!menu.isEmpty())
    {
        menu.addSeparator();
    }
    menu.setToolTipsVisible(true);

    // New project..., Open project... and Close project do what the shell's actions do, and are offered as they are.
    const auto entry = [this, &menu](const char* name, const QString& text, const std::string_view actionId) {
        QAction* const action = m_actions.action(actionId);
        QAction* const made = menu.addAction(text);
        made->setObjectName(QString::fromLatin1(name));
        made->setEnabled(action != nullptr && action->isEnabled());
        if (action != nullptr)
        {
            QObject::connect(made, &QAction::triggered, action, &QAction::trigger);
        }
    };
    entry("projectMenuNew", tr("New project..."), ids::kNewProject);
    entry("projectMenuOpen", tr("Open project..."), ids::kOpenProject);
    entry("projectMenuClose", tr("Close project"), ids::kCloseProject);
}

void ProjectFlows::refresh()
{
    ProjectService& projects = m_core.projects();
    const an::ui::shell::ProjectDocument* const open = projects.current();
    const QString name = open != nullptr ? open->name : QString();
    m_topBar.setProject(name);

    // Close Project is offered while a project is open; the palette says which project the commands are about.
    if (QAction* const close = m_actions.action(ids::kCloseProject))
    {
        close->setEnabled(open != nullptr);
    }
    const QString about = tr("Project") + QStringLiteral(" · ") + (open != nullptr ? name : tr("No project"));
    for (const std::string_view id :
         {ids::kNewProject, ids::kOpenProject, ids::kSwitchProject, ids::kCloseProject, ids::kProjectSettings})
    {
        if (QAction* const action = m_actions.action(id))
        {
            action->setProperty("commandSubtitle", about);
        }
    }
    m_topBar.refreshProjectMenu();
}

void ProjectFlows::reportMissingImages(const std::vector<an::ui::shell::ProjectMissingImage>& missing)
{
    if (missing.empty())
    {
        return;
    }
    if (!m_missing.isNull())
    {
        m_missing->close();
    }
    // One message for all of them, over the window, answered when the person answers it.
    auto* const box = new QMessageBox(&m_window);
    box->setObjectName(QStringLiteral("projectMissingImagesMessage"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::WindowModal);
    box->setIcon(QMessageBox::Warning);
    box->setWindowTitle(tr("Project"));
    box->setText(missing.size() == 1
                     ? tr("An image of this project was not found and was not opened.")
                     : tr("%1 images of this project were not found and were not opened.").arg(missing.size()));
    QStringList files;
    for (const an::ui::shell::ProjectMissingImage& image : missing)
    {
        files.append(QDir::toNativeSeparators(image.path));
    }
    box->setInformativeText(files.join(QLatin1Char('\n')));
    QPushButton* const locate = box->addButton(tr("Locate..."), QMessageBox::AcceptRole);
    locate->setObjectName(QStringLiteral("projectLocateImages"));
    QPushButton* const remove = box->addButton(tr("Remove from the project"), QMessageBox::DestructiveRole);
    remove->setObjectName(QStringLiteral("projectRemoveImages"));
    box->setDefaultButton(box->addButton(QMessageBox::Close));
    QObject::connect(locate, &QPushButton::clicked, this, [this, missing]() { locateMissingImages(missing); });
    QObject::connect(remove, &QPushButton::clicked, this, [this, missing]() {
        // The project forgets them; their files, wherever they are, and their undo history are not touched.
        for (const an::ui::shell::ProjectMissingImage& image : missing)
        {
            (void)m_core.projects().removeImage(image.imageId);
        }
    });
    an::ui::applyModernMessageBoxButtonRoles(*box);
    m_missing = box;
    box->open();
}

void ProjectFlows::locateMissingImages(const std::vector<an::ui::shell::ProjectMissingImage>& missing)
{
    if (!m_chooseFile)
    {
        return;
    }
    ProjectService& projects = m_core.projects();
    for (const an::ui::shell::ProjectMissingImage& image : missing)
    {
        // The project may have closed, or forgotten the image, since the message was shown.
        if (projects.image(image.imageId) == nullptr)
        {
            continue;
        }
        // From where the file was, or the nearest folder above it that is still there.
        QString start = image.path;
        for (QFileInfo place(start); !place.exists();)
        {
            const QString parent = place.absolutePath();
            if (parent == place.absoluteFilePath() || parent.isEmpty())
            {
                start = QDir::homePath();
                break;
            }
            start = parent;
            place = QFileInfo(start);
        }
        const QString found = m_chooseFile(&m_window, tr("Locate %1").arg(QFileInfo(image.path).fileName()), start);
        if (found.isEmpty())
        {
            // The person stopped: the others are asked about the next time the project opens.
            break;
        }
        // The project's record follows the file, and the image opens where the project had it.
        if (projects.setImagePath(image.imageId, found) && m_hooks.openImage)
        {
            m_hooks.openImage(QDir::cleanPath(QDir::fromNativeSeparators(found)), image.imageId);
        }
    }
}

void ProjectFlows::say(const ProjectOutcome& outcome)
{
    // A project the person kept open is not said: the question about its image was the person's own answer, and what
    // keeps a project whose images are still opening says so itself.
    if (outcome.ok() || *outcome.refusal == ProjectRefusal::OpenProjectKept)
    {
        return;
    }
    if (!m_refusal.isNull())
    {
        m_refusal->close();
    }
    auto* const box = new QMessageBox(&m_window);
    box->setObjectName(QStringLiteral("projectRefusalMessage"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::WindowModal);
    box->setIcon(QMessageBox::Warning);
    box->setWindowTitle(tr("Project"));
    box->setText(outcome.why);
    if (*outcome.refusal == ProjectRefusal::Locked)
    {
        // The other session cannot be told crashed from running: the person says, and the lock is taken.
        const QString folder = outcome.folder;
        QPushButton* const anyway = box->addButton(tr("Open anyway"), QMessageBox::AcceptRole);
        anyway->setObjectName(QStringLiteral("projectOpenAnyway"));
        box->setDefaultButton(box->addButton(QMessageBox::Cancel));
        QObject::connect(anyway, &QPushButton::clicked, this,
                         [this, folder]() { openProjectFolder(folder, ProjectLockPolicy::TakeOver); });
    }
    else
    {
        box->addButton(QMessageBox::Ok);
    }
    an::ui::applyModernMessageBoxButtonRoles(*box);
    m_refusal = box;
    box->open();
}

} // namespace an::ui::observatory
