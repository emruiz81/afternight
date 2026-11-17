// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectFlows.h"

#include "ShellActions.h"
#include "chrome/TopBar.h"

#include "core/ApplicationCore.h"

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

    QObject::connect(m_actions.action(ids::kNewProject), &QAction::triggered, this, [this]() { newProject(); });
    QObject::connect(m_actions.action(ids::kOpenProject), &QAction::triggered, this, [this]() { openProject(); });
    QObject::connect(m_actions.action(ids::kSwitchProject), &QAction::triggered, this, [this]() { switchProject(); });
    QObject::connect(m_actions.action(ids::kCloseProject), &QAction::triggered, this, [this]() { closeProject(); });
    QObject::connect(m_actions.action(ids::kProjectSettings), &QAction::triggered, this,
                     [this]() { projectSettings(); });

    m_topBar.setProjectMenuFiller([this](QMenu& menu) { fillSwitcherMenu(menu); });
    m_subscription = m_core.projects().subscribe([this](ProjectService::Change /*change*/) { refresh(); });
    refresh();
}

ProjectFlows::~ProjectFlows()
{
    m_core.projects().unsubscribe(m_subscription);
    m_topBar.setProjectMenuFiller({});
}

void ProjectFlows::setFolderChooser(FolderChooser chooser)
{
    m_chooseFolder = std::move(chooser);
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
    const ProjectOutcome opened = m_core.projects().open(folder, policy);
    if (!opened.ok())
    {
        say(opened);
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
    if (restored.has_value() && !restored->ok())
    {
        m_restoreRefusal = restored;
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

void ProjectFlows::say(const ProjectOutcome& outcome)
{
    if (outcome.ok())
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
