// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "projects/ProjectHistoryBinder.h"

#include "projects/ProjectService.h"

#include "controls/IAppContext.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewControlListener.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <map>
#include <utility>

namespace an::ui::shell
{

namespace
{

constexpr char kContext[] = "an::ui::shell::ProjectHistoryBinder";

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

/// A path as the project records it.
[[nodiscard]] QString cleaned(const QString& path)
{
    return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

[[nodiscard]] bool samePath(const QString& left, const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

/// Runs `told` on the GUI thread, later.
void later(std::function<void()> told)
{
    if (QCoreApplication* const application = QCoreApplication::instance())
    {
        QMetaObject::invokeMethod(application, std::move(told), Qt::QueuedConnection);
    }
}

} // namespace

struct ProjectHistoryBinder::Impl
{
    /// One view as the binder hears it: its saves, and its going.
    class Watch final : public ImageViewControlListener
    {
    public:
        Watch(Impl& owner, ImageViewControl& view) : ImageViewControlListener(&view), m_owner(owner) {}

        /// The image of the open project the view is; empty for none.
        QString imageId;

    private:
        void onImageSaved(ImageViewControl* view) override
        {
            if (view != nullptr)
            {
                m_owner.saved(*view);
            }
        }

        void onImageViewControlDestroyed(ImageViewControl* view) override
        {
            ImageViewControlListener::onImageViewControlDestroyed(view);
            m_owner.forget(view);
        }

        Impl& m_owner;
    };

    explicit Impl(ProjectService& service) : projects(service) {}

    ProjectService& projects;
    Notice notice;
    TemporaryTest isTemporary;
    std::size_t subscription = 0;
    std::map<ImageViewControl*, std::unique_ptr<Watch>> watches;
    // The watches of views that went, let go once nothing is inside one of them.
    std::vector<std::unique_ptr<Watch>> retired;
    // Says, to what was started and lands later, whether the binder is still there.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void say(const QString& text) const
    {
        if (notice)
        {
            notice(text);
            return;
        }
        if (auto* context = an::ui::appContext())
        {
            if (auto* dialogs = context->getDialogService())
            {
                dialogs->notifyInfoMessageAsync(nullptr, translated("Image History"), text);
            }
        }
    }

    /// The file a view shows as a project records it, when it is one a project takes: empty for a view that shows no
    /// file, or a file of the session's own.
    [[nodiscard]] QString projectFileOf(ImageViewControl& view) const
    {
        if (view.getViewControlMode() != ViewControlMode::standard)
        {
            return {};
        }
        const QString path = cleaned(QString::fromStdString(view.getFileName()));
        if (path.isEmpty() || (isTemporary && isTemporary(path)))
        {
            return {};
        }
        return path;
    }

    void forget(ImageViewControl* view)
    {
        const auto found = watches.find(view);
        if (found != watches.end())
        {
            // Called from inside the watch: it is let go later.
            retired.push_back(std::move(found->second));
            watches.erase(found);
        }
    }

    Watch& watchOf(ImageViewControl& view)
    {
        retired.clear();
        std::unique_ptr<Watch>& watch = watches[&view];
        if (!watch)
        {
            watch = std::make_unique<Watch>(*this, view);
        }
        return *watch;
    }

    QString join(ImageViewControl& view, const an::ui::history::StepsMove move, Progress progress)
    {
        if (!projects.isOpen())
        {
            return {};
        }
        const QString path = projectFileOf(view);
        if (path.isEmpty())
        {
            return {};
        }
        const ProjectImage* const image =
            projects.addImage(path, view.isSequence() ? ProjectImageKind::Sequence : ProjectImageKind::Image);
        if (image == nullptr)
        {
            return {};
        }
        const QString id = image->id;
        watchOf(view).imageId = id;

        // A sequence's states are not files that stand alone: it is the project's, and its history the session's.
        an::ui::history::IHistoryPlacement* const placement = an::ui::history::historyPlacementOf(view);
        if (placement == nullptr)
        {
            return id;
        }
        an::ui::history::HistoryHome home;
        home.directory = projects.historyDirectoryFor(id);
        home.compression = projects.current()->historyCompression;
        home.file = path;
        const std::weak_ptr<bool> stillThere = alive;
        const QString fileName = QFileInfo(path).fileName();
        placement->enterHome(
            home, move,
            [this, stillThere, fileName](const an::ui::history::HistoryEntry& entry) {
                if (!stillThere.expired())
                {
                    entered(entry, fileName);
                }
            },
            std::move(progress));
        return id;
    }

    /// What the person is told of a history that entered the project.
    void entered(const an::ui::history::HistoryEntry& entry, const QString& fileName) const
    {
        if (!entry.entered)
        {
            if (!entry.problem.isEmpty())
            {
                LOG_WARNING("The undo history of '{}' is not kept in the project: {}", fileName.toStdString(),
                            entry.problem.toStdString());
            }
            return;
        }
        if (entry.detached)
        {
            say(QStringLiteral("%1: %2").arg(
                fileName, translated("This file changed outside AfterNight; its earlier history is kept and "
                                     "can be opened as images or deleted")));
        }
        if (entry.stepsLeftOut > 0u)
        {
            say(entry.stepsLeftOut == 1u
                    ? translated("%1: 1 step of its undo history could not be found and was left out.").arg(fileName)
                    : translated("%1: %2 steps of its undo history could not be found and were left out.")
                          .arg(fileName)
                          .arg(entry.stepsLeftOut));
        }
    }

    /// A view was written to a file.
    void saved(ImageViewControl& view)
    {
        if (!projects.isOpen())
        {
            return;
        }
        const QString path = projectFileOf(view);
        if (path.isEmpty())
        {
            return;
        }
        Watch& watch = watchOf(view);
        if (!watch.imageId.isEmpty())
        {
            if (const ProjectImage* const image = projects.image(watch.imageId))
            {
                // The project's image, saved: its record follows the file to another name.
                if (!samePath(image->path, path))
                {
                    const QString id = watch.imageId;
                    (void)projects.setImagePath(id, path);
                }
                return;
            }
            watch.imageId.clear();
        }
        // An image that is not the project's yet joins it at this save, and the steps it has go in.
        (void)join(view, an::ui::history::StepsMove::Move, {});
    }

    /// The project closed, or its images changed: a view whose image the project no longer holds goes on with a
    /// history of the session's.
    void projectChanged()
    {
        for (const auto& [view, watch] : watches)
        {
            if (watch->imageId.isEmpty() || (projects.isOpen() && projects.image(watch->imageId) != nullptr))
            {
                continue;
            }
            watch->imageId.clear();
            if (an::ui::history::IHistoryPlacement* const placement = an::ui::history::historyPlacementOf(*view))
            {
                placement->leaveHome();
            }
        }
    }

    /// The view that has an image's history open in the project's folder; null for none.
    [[nodiscard]] an::ui::history::IHistoryPlacement* openHistoryOf(const QString& imageId) const
    {
        for (const auto& [view, watch] : watches)
        {
            if (watch->imageId != imageId)
            {
                continue;
            }
            an::ui::history::IHistoryPlacement* const placement = an::ui::history::historyPlacementOf(*view);
            if (placement != nullptr && placement->home().has_value())
            {
                return placement;
            }
        }
        return nullptr;
    }

    void compaction(const QString& imageId, const an::ui::history::HistoryCompaction& what, const bool apply,
                    CompactionDone done)
    {
        using namespace an::ui::history;
        if (!done)
        {
            done = [](const HistoryCompactionResult&) {};
        }
        const std::weak_ptr<bool> stillThere = alive;
        const auto tell = [stillThere, done](const HistoryCompactionResult& result) {
            later([stillThere, done, result]() {
                if (!stillThere.expired())
                {
                    done(result);
                }
            });
        };
        const QString directory = projects.historyDirectoryFor(imageId);
        if (directory.isEmpty())
        {
            tell(HistoryCompactionResult{});
            return;
        }
        if (IHistoryPlacement* const open = openHistoryOf(imageId))
        {
            // The view that has the image open drops the steps; the histories set aside are folders no view has.
            const HistoryCompactionResult result = apply ? open->compact(what) : open->planCompaction(what);
            if (!what.removeDetached || !result.ok)
            {
                tell(result);
                return;
            }
            QThreadPool::globalInstance()->start([directory, apply, result, tell]() {
                HistoryCompactionResult whole = result;
                whole.bytesFreed += measureStoredHistory(directory).detachedBytes;
                if (apply)
                {
                    (void)QDir(QDir(directory).filePath(QString::fromLatin1(kDetachedHistoryFolder)))
                        .removeRecursively();
                }
                tell(whole);
            });
            return;
        }
        QThreadPool::globalInstance()->start([directory, what, apply, tell]() {
            tell(apply ? compactStoredHistory(directory, what) : planStoredHistoryCompaction(directory, what));
        });
    }
};

ProjectHistoryBinder::ProjectHistoryBinder(ProjectService& projects) : m_impl(std::make_unique<Impl>(projects))
{
    Impl* const impl = m_impl.get();
    m_impl->subscription = projects.subscribe([impl](const ProjectService::Change change) {
        if (change == ProjectService::Change::Closed || change == ProjectService::Change::Images)
        {
            impl->projectChanged();
        }
    });
}

ProjectHistoryBinder::~ProjectHistoryBinder()
{
    m_impl->alive.reset();
    m_impl->projects.unsubscribe(m_impl->subscription);
    m_impl->watches.clear();
    m_impl->retired.clear();
}

void ProjectHistoryBinder::setNotice(Notice notice)
{
    m_impl->notice = std::move(notice);
}

void ProjectHistoryBinder::setTemporaryTest(TemporaryTest isTemporary)
{
    m_impl->isTemporary = std::move(isTemporary);
}

void ProjectHistoryBinder::watch(ImageViewControl& view)
{
    if (view.getViewControlMode() != ViewControlMode::standard)
    {
        return;
    }
    (void)m_impl->watchOf(view);
    // A view is told of as it is made, before it holds its image: the file it shows is read once what made it has
    // returned.
    Impl* const impl = m_impl.get();
    const std::weak_ptr<bool> stillThere = m_impl->alive;
    const QPointer<ImageViewControl> made(&view);
    later([impl, stillThere, made]() {
        if (stillThere.expired() || made.isNull() || !impl->projects.isOpen())
        {
            return;
        }
        const auto found = impl->watches.find(made.data());
        if (found != impl->watches.end() && found->second->imageId.isEmpty())
        {
            (void)impl->join(*made, an::ui::history::StepsMove::Move, {});
        }
    });
}

QString ProjectHistoryBinder::join(ImageViewControl& view, const an::ui::history::StepsMove move, Progress progress)
{
    return m_impl->join(view, move, std::move(progress));
}

QString ProjectHistoryBinder::imageOf(const ImageViewControl& view) const
{
    const auto found = m_impl->watches.find(const_cast<ImageViewControl*>(&view));
    return found != m_impl->watches.end() ? found->second->imageId : QString();
}

std::uint64_t ProjectHistoryBinder::historyBytesOf(const std::vector<ImageViewControl*>& views)
{
    std::uint64_t bytes = 0u;
    for (ImageViewControl* const view : views)
    {
        if (view == nullptr)
        {
            continue;
        }
        if (const an::ui::history::IHistoryPlacement* const placement = an::ui::history::historyPlacementOf(*view))
        {
            bytes += placement->stepFileBytes();
        }
    }
    return bytes;
}

void ProjectHistoryBinder::planCompaction(const QString& imageId, const an::ui::history::HistoryCompaction& what,
                                          CompactionDone done)
{
    m_impl->compaction(imageId, what, false, std::move(done));
}

void ProjectHistoryBinder::compact(const QString& imageId, const an::ui::history::HistoryCompaction& what,
                                   CompactionDone done)
{
    m_impl->compaction(imageId, what, true, std::move(done));
}

} // namespace an::ui::shell
