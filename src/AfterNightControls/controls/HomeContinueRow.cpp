// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeContinueRow.h"

#include "components/AnTag.h"
#include "controls/DashboardSettings.h"
#include "controls/DashboardThumbnailCache.h"
#include "controls/DashboardUtils.h"
#include "controls/FeaturedFilePresenter.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"
#include "widgets/FeaturedFileCard.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::HomeContinueRow)

namespace an::ui
{

namespace
{

/// A recent file's picture in a well, cut to its corners: in Night's form under Night, made off the GUI thread.
class PictureWell final : public QWidget
{
public:
    explicit PictureWell(QWidget* parent) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPicture(const QImage& picture)
    {
        if (picture.isNull())
        {
            m_picture.clear();
        }
        else
        {
            m_picture.setPicture(picture);
        }
        update();
    }

    [[nodiscard]] bool hasPicture() const
    {
        return !m_picture.isNull();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        const QPixmap shown = m_picture.shown(this, [this]() { update(); });
        paintHomePicture(painter, QRectF(rect()), shown, parts::kHomeTileRadius);
    }

private:
    NightPicture m_picture;
};

/// A card of the row: the second surface inside a hairline, its corners rounded.
class RowCard final : public QWidget
{
public:
    explicit RowCard(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface2);
        painter.drawRoundedRect(place, parts::kHomeCardRadius, parts::kHomeCardRadius);
    }
};

QIcon providedIcon(const QString& name)
{
    if (IAppContext* context = appContext())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

} // namespace

struct HomeContinueRow::Impl
{
    FeaturedFileCard* featured = nullptr;
    FeaturedFilePresenter* presenter = nullptr;
    std::unique_ptr<DashboardThumbnailCache> ownCache = std::make_unique<DashboardThumbnailCache>(64);
    DashboardThumbnailCache* cache = nullptr;
    HomeRecentProbe probe = &probeHomeRecentFile;

    RowCard* project = nullptr;
    HomeText* projectName = nullptr;
    AnTag* projectObject = nullptr;
    PictureWell* projectPicture = nullptr;
    HomeText* projectCounts = nullptr;
    AnTag* projectWorkspaces = nullptr;
    AnTag* projectSize = nullptr;
    HomeText* projectOpened = nullptr;
    QPushButton* openProject = nullptr;
    std::optional<HomeProjectSummary> lastProject;
    quint64 projectRead = 0;

    RowCard* sequence = nullptr;
    HomeText* sequenceName = nullptr;
    PictureWell* sequencePicture = nullptr;
    HomeText* sequenceFacts = nullptr;
    HomeText* sequenceDate = nullptr;
    QPushButton* resume = nullptr;
    QString sequencePath;
    quint64 sequenceRead = 0;
    /// The last change of the thumbnail the card was read from, and whether its facts were read: a busy session
    /// leaves them unread, and the card reads again the next time the recent files are given.
    qint64 sequenceStamp = -1;
    bool sequenceFactsRead = false;

    std::deque<QString> recent;
    std::deque<QString> images;
    QString pinned;
};

HomeContinueRow::HomeContinueRow(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeContinueRow"));
    setFixedHeight(parts::kHomeContinueRowHeight);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(parts::kHomeCardGap);

    // The featured image: the dashboard's card and presenter.
    m_impl->featured = new FeaturedFileCard(this);
    // The dashboard gives the card a height; on Home it fills the row.
    m_impl->featured->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_impl->featured->hide();
    m_impl->presenter = new FeaturedFilePresenter(this);
    m_impl->presenter->setThumbnailPathResolver([](const QString& path) { return homeThumbnailPath(path); });
    m_impl->presenter->setThumbnailCacheTileSize(parts::kHomeContinueRowHeight * 2);
    m_impl->presenter->setPreferredDecodeSize(
        QSize(parts::kHomeContinueRowHeight * 5, parts::kHomeContinueRowHeight * 2));
    m_impl->cache = m_impl->ownCache.get();
    m_impl->presenter->setThumbnailCache(m_impl->cache);
    QObject::connect(m_impl->presenter, &FeaturedFilePresenter::featuredFileReady, this,
                     [this](const QString& filePath, const QString& displayName, const QString& metadataText,
                            const QPixmap& backgroundPixmap, const bool pinned) {
                         m_impl->featured->setFeaturedFile(filePath, displayName, metadataText, backgroundPixmap,
                                                           pinned);
                         m_impl->featured->show();
                     });
    QObject::connect(m_impl->presenter, &FeaturedFilePresenter::noFeaturedFileAvailable, this, [this]() {
        m_impl->featured->clearFeaturedFile();
        m_impl->featured->hide();
    });
    QObject::connect(m_impl->featured, &FeaturedFileCard::continueRequested, this, [this](const QString& filePath) {
        recordDashboardLastAction(QCoreApplication::translate("MainDashboard", "Continue featured file"));
        emit openRecentFileRequested(filePath);
    });
    QObject::connect(m_impl->featured, &FeaturedFileCard::pinToggled, this,
                     [this](const QString& filePath, const bool pinned) { setPinned(filePath, pinned); });
    row->addWidget(m_impl->featured, 1);

    // The project used last.
    m_impl->project = new RowCard(this);
    m_impl->project->setObjectName(QStringLiteral("homeLastProjectCard"));
    m_impl->project->setFixedWidth(parts::kHomeSideCardWidth);
    auto* projectCard = new QVBoxLayout(m_impl->project);
    projectCard->setContentsMargins(parts::kHomeCardPadding, parts::kHomeCardPadding, parts::kHomeCardPadding,
                                    parts::kHomeCardPadding);
    projectCard->setSpacing(parts::kHomeTileGap / 2);
    auto* projectHead = new QHBoxLayout();
    projectHead->setContentsMargins(0, 0, 0, 0);
    projectHead->addWidget(new HomeText(HomeText::Role::Caps, tr("Last project"), m_impl->project), 1);
    m_impl->projectObject = new AnTag(QString(), AnTag::Tone::Accent, m_impl->project);
    projectHead->addWidget(m_impl->projectObject, 0);
    projectCard->addLayout(projectHead);
    m_impl->projectName = new HomeText(HomeText::Role::Section, QString(), m_impl->project);
    m_impl->projectName->setObjectName(QStringLiteral("homeLastProjectName"));
    m_impl->projectName->setElideMode(Qt::ElideMiddle);
    projectCard->addWidget(m_impl->projectName);
    m_impl->projectPicture = new PictureWell(m_impl->project);
    projectCard->addWidget(m_impl->projectPicture, 1);
    m_impl->projectCounts = new HomeText(HomeText::Role::Mono, QString(), m_impl->project);
    m_impl->projectCounts->setObjectName(QStringLiteral("homeLastProjectCounts"));
    projectCard->addWidget(m_impl->projectCounts);
    auto* projectTags = new QHBoxLayout();
    projectTags->setContentsMargins(0, 0, 0, 0);
    projectTags->setSpacing(parts::kHubWayTagGap);
    m_impl->projectWorkspaces = new AnTag(QString(), AnTag::Tone::Neutral, m_impl->project);
    projectTags->addWidget(m_impl->projectWorkspaces, 0);
    m_impl->projectSize = new AnTag(QString(), AnTag::Tone::Neutral, m_impl->project);
    projectTags->addWidget(m_impl->projectSize, 0);
    projectTags->addStretch(1);
    projectCard->addLayout(projectTags);
    auto* projectFoot = new QHBoxLayout();
    projectFoot->setContentsMargins(0, 0, 0, 0);
    m_impl->projectOpened = new HomeText(HomeText::Role::Mono, QString(), m_impl->project);
    projectFoot->addWidget(m_impl->projectOpened, 1);
    m_impl->openProject = new QPushButton(tr("Open project"), m_impl->project);
    m_impl->openProject->setObjectName(QStringLiteral("homeLastProjectOpen"));
    m_impl->openProject->setProperty("anButtonVariant", QStringLiteral("tonal"));
    QObject::connect(m_impl->openProject, &QPushButton::clicked, this, [this]() {
        if (m_impl->lastProject.has_value())
        {
            emit openProjectRequested(m_impl->lastProject->folder);
        }
    });
    projectFoot->addWidget(m_impl->openProject, 0);
    projectCard->addLayout(projectFoot);
    m_impl->project->hide();
    row->addWidget(m_impl->project, 0);

    // The last sequence.
    m_impl->sequence = new RowCard(this);
    m_impl->sequence->setObjectName(QStringLiteral("homeLastSequenceCard"));
    m_impl->sequence->setFixedWidth(parts::kHomeSideCardWidth);
    auto* card = new QVBoxLayout(m_impl->sequence);
    card->setContentsMargins(parts::kHomeCardPadding, parts::kHomeCardPadding, parts::kHomeCardPadding,
                             parts::kHomeCardPadding);
    card->setSpacing(parts::kHomeTileGap / 2);
    auto* eyebrow = new HomeText(HomeText::Role::Caps, tr("Last sequence"), m_impl->sequence);
    eyebrow->setObjectName(QStringLiteral("homeLastSequenceEyebrow"));
    card->addWidget(eyebrow);
    m_impl->sequenceName = new HomeText(HomeText::Role::Section, QString(), m_impl->sequence);
    m_impl->sequenceName->setObjectName(QStringLiteral("homeLastSequenceName"));
    m_impl->sequenceName->setElideMode(Qt::ElideMiddle);
    card->addWidget(m_impl->sequenceName);
    m_impl->sequencePicture = new PictureWell(m_impl->sequence);
    m_impl->sequencePicture->setObjectName(QStringLiteral("homeLastSequencePicture"));
    card->addWidget(m_impl->sequencePicture, 1);
    m_impl->sequenceFacts = new HomeText(HomeText::Role::Mono, QString(), m_impl->sequence);
    m_impl->sequenceFacts->setObjectName(QStringLiteral("homeLastSequenceFacts"));
    card->addWidget(m_impl->sequenceFacts);
    auto* foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    m_impl->sequenceDate = new HomeText(HomeText::Role::Mono, QString(), m_impl->sequence);
    m_impl->sequenceDate->setObjectName(QStringLiteral("homeLastSequenceDate"));
    foot->addWidget(m_impl->sequenceDate, 1);
    m_impl->resume = new QPushButton(tr("Resume"), m_impl->sequence);
    m_impl->resume->setObjectName(QStringLiteral("homeLastSequenceResume"));
    m_impl->resume->setProperty("anButtonVariant", QStringLiteral("tonal"));
    m_impl->resume->setToolTip(tr("Open the last sequence again"));
    QObject::connect(m_impl->resume, &QPushButton::clicked, this, [this]() {
        if (!m_impl->sequencePath.isEmpty())
        {
            recordDashboardLastAction(tr("Resume the last sequence"));
            emit openRecentFileRequested(m_impl->sequencePath);
        }
    });
    foot->addWidget(m_impl->resume, 0);
    card->addLayout(foot);
    m_impl->sequence->hide();
    row->addWidget(m_impl->sequence, 0);
}

HomeContinueRow::~HomeContinueRow() = default;

bool HomeContinueRow::event(QEvent* event)
{
    // The pin's icons are drawn in the tokens' colours in force when the row is polished, and again when the
    // application's icons are refreshed.
    if (event != nullptr && event->type() == QEvent::Polish)
    {
        refreshIcons();
    }
    return QWidget::event(event);
}

void HomeContinueRow::setThumbnailCache(DashboardThumbnailCache* cache)
{
    m_impl->cache = cache != nullptr ? cache : m_impl->ownCache.get();
    m_impl->presenter->setThumbnailCache(m_impl->cache);
}

void HomeContinueRow::setFactsProbe(HomeRecentProbe probe)
{
    m_impl->probe = std::move(probe);
}

void HomeContinueRow::setRecentFiles(const std::deque<QString>& recentFiles)
{
    m_impl->recent = recentFiles;
    m_impl->images.clear();
    QString lastSequence;
    for (const QString& path : recentFiles)
    {
        if (isHomeSequence(path))
        {
            if (lastSequence.isEmpty())
            {
                lastSequence = path;
            }
        }
        else
        {
            m_impl->images.push_back(path);
        }
    }

    // A pinned file no longer recent, or no longer on disk, is unpinned - as the dashboard unpins it.
    QString pinned = loadPinnedFeaturedFile();
    if (!pinned.isEmpty())
    {
        const QFileInfo info(pinned);
        if (std::find(recentFiles.cbegin(), recentFiles.cend(), pinned) == recentFiles.cend() || !info.exists() ||
            !info.isFile())
        {
            pinned.clear();
            storePinnedFeaturedFile(QString());
        }
    }
    m_impl->pinned = pinned;
    refreshFeatured();
    showSequence(lastSequence);
}

void HomeContinueRow::refreshIcons()
{
    const ColorTokens& c = tokens().colors;
    if (IAppContext* context = appContext())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            const QSize size(parts::kHomeActionIcon, parts::kHomeActionIcon);
            m_impl->featured->setPinIcons(
                QIcon(
                    icons->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/star.svg"), size, c.textOnImage)),
                QIcon(icons->loadSvgFileAsPixmap(QStringLiteral(":/AfterNight/icons/SVG/star_filled.svg"), size,
                                                 c.accentPinned)));
        }
    }
    m_impl->resume->setIcon(providedIcon(QStringLiteral("play")));
    m_impl->featured->refreshTheme();
}

FeaturedFileCard* HomeContinueRow::featuredCard() const
{
    return m_impl->featured;
}

QString HomeContinueRow::featuredFilePath() const
{
    return m_impl->featured->isVisibleTo(this) ? m_impl->featured->currentFilePath() : QString();
}

bool HomeContinueRow::isFeaturedPinned() const
{
    return !m_impl->pinned.isEmpty() && m_impl->featured->currentFilePath() == m_impl->pinned;
}

QWidget* HomeContinueRow::sequenceCard() const
{
    return m_impl->sequence;
}

void HomeContinueRow::setLastProject(const std::optional<HomeProjectSummary>& project)
{
    const bool samePicture = project.has_value() && m_impl->lastProject.has_value() &&
                             project->picture == m_impl->lastProject->picture && m_impl->projectPicture->hasPicture();
    m_impl->lastProject = project;
    if (!project.has_value())
    {
        m_impl->project->hide();
        return;
    }
    m_impl->projectName->setText(project->name);
    m_impl->projectName->setToolTip(QDir::toNativeSeparators(project->folder));
    m_impl->projectObject->setText(project->object);
    m_impl->projectObject->setVisible(!project->object.isEmpty());
    m_impl->projectCounts->setText(homeProjectCountsText(*project, true));
    m_impl->projectWorkspaces->setText(project->workspaces == 1 ? tr("1 workspace")
                                                                : tr("%1 workspaces").arg(project->workspaces));
    m_impl->projectWorkspaces->setVisible(project->workspaces > 0);
    m_impl->projectSize->setText(homeSizeText(project->bytes));
    m_impl->projectSize->setVisible(project->bytes >= 0);
    m_impl->projectOpened->setText(
        project->lastOpened.isValid()
            ? tr("opened %1").arg(project->lastOpened.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")))
            : QString());
    m_impl->project->show();
    if (!samePicture)
    {
        showProjectPicture();
    }
}

QWidget* HomeContinueRow::lastProjectCard() const
{
    return m_impl->project;
}

QString HomeContinueRow::lastProjectName() const
{
    return m_impl->lastProject.has_value() ? m_impl->lastProject->name : QString();
}

QString HomeContinueRow::lastProjectCountsText() const
{
    return m_impl->projectCounts->text();
}

QAbstractButton* HomeContinueRow::openProjectButton() const
{
    return m_impl->openProject;
}

void HomeContinueRow::showProjectPicture()
{
    const quint64 read = ++m_impl->projectRead;
    m_impl->projectPicture->setPicture(QImage());
    if (!m_impl->lastProject.has_value() || m_impl->lastProject->picture.isEmpty())
    {
        return;
    }
    const QString file = m_impl->lastProject->picture;
    const QString thumbnail = homeThumbnailPath(file);
    if (thumbnail.isEmpty())
    {
        return;
    }
    const QPointer<HomeContinueRow> self(this);
    readHomeRecentFile(this, file, thumbnail, QSize(parts::kHomeSideCardWidth * 2, parts::kHomeContinueRowHeight), {},
                       [self, read](const QImage& picture, const HomeRecentFacts& /*facts*/) {
                           if (self && read == self->m_impl->projectRead)
                           {
                               self->m_impl->projectPicture->setPicture(picture);
                           }
                       });
}

QString HomeContinueRow::lastSequencePath() const
{
    return m_impl->sequencePath;
}

bool HomeContinueRow::hasSequencePicture() const
{
    return m_impl->sequencePicture->hasPicture();
}

QString HomeContinueRow::sequenceFactsText() const
{
    return m_impl->sequenceFacts->text();
}

QAbstractButton* HomeContinueRow::resumeButton() const
{
    return m_impl->resume;
}

void HomeContinueRow::setPinned(const QString& filePath, const bool pinned)
{
    const QString path = DashboardUtils::normalizeRecentFilePath(filePath);
    if (pinned)
    {
        if (path.isEmpty() || path == m_impl->pinned)
        {
            return;
        }
        m_impl->pinned = path;
    }
    else
    {
        if (path != m_impl->pinned)
        {
            return;
        }
        m_impl->pinned.clear();
    }
    storePinnedFeaturedFile(m_impl->pinned);
    refreshFeatured();
}

void HomeContinueRow::refreshFeatured()
{
    if (m_impl->images.empty())
    {
        m_impl->featured->clearFeaturedFile();
        m_impl->featured->hide();
        return;
    }
    m_impl->presenter->setPinnedHeroFilePath(m_impl->pinned);
    m_impl->presenter->setRecentFilePaths(m_impl->images);
    m_impl->presenter->refresh();
}

void HomeContinueRow::showSequence(const QString& filePath)
{
    m_impl->sequence->setVisible(!filePath.isEmpty());
    const QString thumbnail = filePath.isEmpty() ? QString() : homeThumbnailPath(filePath);
    const QFileInfo thumbnailInfo(thumbnail);
    const qint64 stamp =
        !thumbnail.isEmpty() && thumbnailInfo.isFile() ? thumbnailInfo.lastModified().toMSecsSinceEpoch() : 0;
    if (filePath == m_impl->sequencePath && stamp == m_impl->sequenceStamp && m_impl->sequenceFactsRead)
    {
        return;
    }
    if (filePath != m_impl->sequencePath)
    {
        m_impl->sequencePath = filePath;
        m_impl->sequenceName->setText(QFileInfo(filePath).fileName());
        m_impl->sequenceFacts->setText(homeTypeText(filePath));
        m_impl->sequenceDate->clear();
        m_impl->sequencePicture->setPicture(QImage());
    }
    m_impl->sequenceStamp = stamp;
    m_impl->sequenceFactsRead = false;
    if (filePath.isEmpty())
    {
        return;
    }
    // The picture and the facts are read off the GUI thread; a read the card has moved past is dropped.
    const quint64 read = ++m_impl->sequenceRead;
    const QSize pictureSize(parts::kHomeSideCardWidth * 2, parts::kHomeTilePicture * 2);
    readHomeRecentFile(
        this, filePath, stamp != 0 ? thumbnail : QString(), pictureSize, m_impl->probe,
        [this, read, filePath](const QImage& picture, const HomeRecentFacts& facts) {
            if (read != m_impl->sequenceRead)
            {
                return;
            }
            // A file gone is not read again; a busy session's refusal is.
            m_impl->sequenceFactsRead =
                facts.size.isValid() || facts.frameCount.has_value() || !facts.modified.isValid();
            if (!picture.isNull())
            {
                m_impl->sequencePicture->setPicture(picture);
            }
            HomeRecentFacts shown = facts;
            shown.modified = QDateTime();
            const QString line = homeFactsText(shown);
            m_impl->sequenceFacts->setText(line.isEmpty() ? homeTypeText(filePath)
                                                          : homeTypeText(filePath) + QStringLiteral(" · ") + line);
            m_impl->sequenceDate->setText(
                facts.modified.isValid() ? QLocale::c().toString(facts.modified, QStringLiteral("yyyy-MM-dd HH:mm"))
                                         : QString());
        });
}

} // namespace an::ui
