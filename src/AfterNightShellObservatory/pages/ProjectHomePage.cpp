// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "pages/ProjectHomePage.h"

#include "app/ShowInFileBrowser.h"
#include "components/AnIconButton.h"
#include "components/AnTag.h"
#include "controls/HomeParts.h"
#include "controls/HomeProjectsRow.h"
#include "controls/IAppContext.h"
#include "core/ApplicationCore.h"
#include "processes/ImageIntegration/ui_models/IntegrationJobsModel.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <utility>

W_OBJECT_IMPL(an::ui::observatory::ProjectHomePage)

namespace an::ui::observatory
{

namespace
{

using an::ui::HomeText;
namespace parts = an::ui::parts;

constexpr char kContext[] = "an::ui::observatory::ProjectHomePage";

/// How long after the last keystroke the notes are written.
constexpr int kNotesSaveDelayMs = 500;

QString translated(const char* text)
{
    return QCoreApplication::translate(kContext, text);
}

QIcon providedIcon(const QString& name)
{
    if (an::ui::IAppContext* const context = an::ui::appContext())
    {
        if (const an::ui::IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

/// A count and its noun, singular for one.
QString counted(const std::size_t count, const char* one, const char* many)
{
    return count == 1u ? translated(one) : translated(many).arg(count);
}

/// A picture of the page - the project's, an image's - in a well cut to its corners, in Night's form under Night.
class PictureWell final : public QWidget
{
public:
    PictureWell(const int width, const int height, const qreal radius, QWidget* parent)
        : QWidget(parent), m_radius(radius)
    {
        setFixedSize(width, height);
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

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        an::ui::paintHomePicture(painter, QRectF(rect()), m_picture.shown(this, [this]() { update(); }), m_radius);
    }

private:
    an::ui::NightPicture m_picture;
    qreal m_radius = 0;
};

/// A card of the page: the second surface, its corners rounded, a head of its title and its summary over its body.
class SectionCard final : public QWidget
{
public:
    SectionCard(const QString& title, QWidget* parent) : QWidget(parent)
    {
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(parts::kProjectSectionPadding, 0, parts::kProjectSectionPadding,
                                   parts::kProjectSectionPadding);
        column->setSpacing(0);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        auto* name = new HomeText(HomeText::Role::Caps, title, this);
        name->setElideMode(Qt::ElideNone);
        name->setMinimumHeight(parts::kProjectSectionHead);
        head->addWidget(name, 0);
        head->addStretch(1);
        m_summary = new HomeText(HomeText::Role::Caption, QString(), this);
        head->addWidget(m_summary, 0);
        column->addLayout(head);
        m_body = new QVBoxLayout();
        m_body->setContentsMargins(0, 0, 0, 0);
        m_body->setSpacing(parts::kProjectIntegrationRowPadding);
        column->addLayout(m_body, 1);
    }

    [[nodiscard]] QVBoxLayout* body() const noexcept
    {
        return m_body;
    }

    void setSummary(const QString& summary)
    {
        m_summary->setText(summary);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const an::ui::DesignTokens& t = an::ui::tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.layers.surface2);
        painter.drawRoundedRect(QRectF(rect()), t.radius.lg, t.radius.lg);
    }

private:
    HomeText* m_summary = nullptr;
    QVBoxLayout* m_body = nullptr;
};

/// The storage's bar: a part each of what the folder holds and what is free on the drive, as wide as its bytes, 2 px
/// apart, each at least 4 px.
class StorageBar final : public QWidget
{
public:
    struct Part
    {
        std::uint64_t bytes = 0;
        QColor colour;
        /// Drawn as an outline over no fill: what is free.
        bool outlined = false;
    };

    explicit StorageBar(QWidget* parent) : QWidget(parent)
    {
        setFixedHeight(parts::kStorageBarHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setParts(std::vector<Part> made)
    {
        m_parts = std::move(made);
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        std::uint64_t whole = 0;
        int shown = 0;
        for (const Part& part : m_parts)
        {
            whole += part.bytes;
            shown += part.bytes > 0u ? 1 : 0;
        }
        if (whole == 0u || shown == 0)
        {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal room = width() - parts::kStorageBarGap * (shown - 1);
        qreal x = 0;
        for (const Part& part : m_parts)
        {
            if (part.bytes == 0u)
            {
                continue;
            }
            const qreal share = (std::max)(static_cast<qreal>(parts::kStorageBarLeast),
                                           room * static_cast<qreal>(part.bytes) / static_cast<qreal>(whole));
            const QRectF place(x, 0, (std::min)(share, width() - x), height());
            const qreal radius = height() / 2.0;
            if (part.outlined)
            {
                painter.setPen(QPen(part.colour, 1.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(place.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
            }
            else
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(part.colour);
                painter.drawRoundedRect(place, radius, radius);
            }
            x += share + parts::kStorageBarGap;
        }
    }

private:
    std::vector<Part> m_parts;
};

/// A swatch of the storage's legend.
class Swatch final : public QWidget
{
public:
    Swatch(QColor colour, const bool outlined, QWidget* parent)
        : QWidget(parent), m_colour(std::move(colour)), m_outlined(outlined)
    {
        setFixedSize(parts::kStorageSwatch, parts::kStorageSwatch);
    }

    void setColour(const QColor& colour)
    {
        m_colour = colour;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_outlined)
        {
            painter.setPen(QPen(m_colour, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), parts::kStorageBarGap,
                                    parts::kStorageBarGap);
        }
        else
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_colour);
            painter.drawRoundedRect(QRectF(rect()), parts::kStorageBarGap, parts::kStorageBarGap);
        }
    }

private:
    QColor m_colour;
    bool m_outlined = false;
};

/// A cell of the images' table: as wide as its column, and where a row is short down to its least - alike in every
/// row, so the columns stay aligned. What it holds fills it, a text eliding.
class TableCell final : public QWidget
{
public:
    TableCell(QWidget* content, const int width, const int least, QWidget* parent)
        : QWidget(parent), m_width(width), m_least(least)
    {
        auto* const layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        content->setParent(this);
        layout->addWidget(content, 1);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {m_width, QWidget::sizeHint().height()};
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return {m_least, QWidget::minimumSizeHint().height()};
    }

private:
    int m_width;
    int m_least;
};

/// Lays a row of the images' table out in its columns: the picture, the name, which stretches, then the others, each
/// as wide as its column where the row has room and giving way alike where it has not.
void addImageColumns(QHBoxLayout& row, QWidget* picture, QWidget* name, QWidget* type, QWidget* dimensions,
                     QWidget* history, QWidget* state, QWidget* modified)
{
    row.setContentsMargins(0, 0, 0, 0);
    row.setSpacing(parts::kHubJobColumnGap);
    row.addWidget(picture, 0);
    name->setMinimumWidth(parts::kProjectImageNameLeast);
    row.addWidget(name, 1);
    QWidget* const line = row.parentWidget();
    const auto column = [&row, line](QWidget* cell, const int width, const int least) {
        row.addWidget(new TableCell(cell, width, least, line), 0);
    };
    column(type, parts::kProjectImageTypeColumn, parts::kProjectImageTypeColumn);
    column(dimensions, parts::kProjectImageSizeColumn, parts::kProjectImageSizeLeast);
    column(history, parts::kProjectImageHistoryColumn, parts::kProjectImageHistoryLeast);
    column(state, parts::kProjectImageStateColumn, parts::kProjectImageStateLeast);
    column(modified, parts::kProjectImageModifiedColumn, parts::kProjectImageModifiedLeast);
}

/// The colours of the storage's parts: the undo history, the masters and stacked images, the calibrated and
/// registered frames, the reports, layouts and notes, what is free.
std::vector<QColor> storageColours(const an::ui::DesignTokens& t)
{
    return {t.colors.accentPrimary, t.colors.statusSuccess, t.colors.textTertiary, t.colors.statusInfo,
            t.colors.borderPrimary};
}

} // namespace

// ---- A workspace's tile

struct ProjectHomePage::WorkspaceTile::Impl
{
    WorkspaceTileFacts facts;
    WorkspaceMiniature* miniature = nullptr;
    QPushButton* open = nullptr;
    QPushButton* rename = nullptr;
};

ProjectHomePage::WorkspaceTile::WorkspaceTile(const WorkspaceTileFacts& facts, QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->facts = facts;
    setObjectName(QStringLiteral("projectHomeWorkspaceTile"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(parts::kWorkspaceTilePadding, parts::kWorkspaceTilePadding, parts::kWorkspaceTilePadding,
                            parts::kWorkspaceTilePadding);
    row->setSpacing(parts::kProjectCardsGap);
    m_impl->miniature = new WorkspaceMiniature(this);
    m_impl->miniature->setMiniatureLayout(facts.layout);
    row->addWidget(m_impl->miniature, 0);
    auto* column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(parts::kHomeTileGap / 2);
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    auto* name = new HomeText(HomeText::Role::Strong, facts.name, this);
    name->setElideMode(Qt::ElideMiddle);
    head->addWidget(name, 1);
    if (facts.last)
    {
        head->addWidget(new an::ui::AnTag(translated("last used"), an::ui::AnTag::Tone::Accent, this), 0);
    }
    column->addLayout(head);
    column->addWidget(new HomeText(HomeText::Role::Mono, factsText(), this));
    column->addStretch(1);
    auto* foot = new QHBoxLayout();
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(parts::kHomeTileGap / 2);
    m_impl->open = new QPushButton(facts.last ? translated("Continue") : translated("Open"), this);
    m_impl->open->setObjectName(QStringLiteral("projectHomeWorkspaceOpen"));
    m_impl->open->setProperty("anButtonVariant", facts.last ? QStringLiteral("tonal") : QString());
    foot->addWidget(m_impl->open, 0);
    m_impl->rename = new QPushButton(translated("Rename..."), this);
    m_impl->rename->setObjectName(QStringLiteral("projectHomeWorkspaceRename"));
    m_impl->rename->setProperty("anButtonVariant", QStringLiteral("quiet"));
    foot->addWidget(m_impl->rename, 0);
    foot->addStretch(1);
    column->addLayout(foot);
    row->addLayout(column, 1);
    setToolTip(translated("%1: the miniature is its arrangement").arg(facts.name));
}

ProjectHomePage::WorkspaceTile::~WorkspaceTile() = default;

QString ProjectHomePage::WorkspaceTile::id() const
{
    return m_impl->facts.id;
}

QString ProjectHomePage::WorkspaceTile::name() const
{
    return m_impl->facts.name;
}

QString ProjectHomePage::WorkspaceTile::factsText() const
{
    return counted(static_cast<std::size_t>(m_impl->facts.images),
                   QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 image"),
                   QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 images")) +
           QStringLiteral(" · ") + miniatureArrangementText(m_impl->facts.layout);
}

bool ProjectHomePage::WorkspaceTile::isLast() const
{
    return m_impl->facts.last;
}

WorkspaceMiniature* ProjectHomePage::WorkspaceTile::miniature() const
{
    return m_impl->miniature;
}

QAbstractButton* ProjectHomePage::WorkspaceTile::openButton() const
{
    return m_impl->open;
}

QAbstractButton* ProjectHomePage::WorkspaceTile::renameButton() const
{
    return m_impl->rename;
}

void ProjectHomePage::WorkspaceTile::paintEvent(QPaintEvent* /*event*/)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(m_impl->facts.last ? QPen(t.colors.accentLine, 1.0) : QPen(Qt::NoPen));
    painter.setBrush(t.layers.surface1);
    painter.drawRoundedRect(place, parts::kWorkspaceTileRadius, parts::kWorkspaceTileRadius);
}

// ---- The page

struct ProjectHomePage::Impl
{
    Impl(an::ui::shell::ApplicationCore& application, ImageIntegration::IntegrationJobsModel& model, Hooks given)
        : core(application), jobs(model), hooks(std::move(given))
    {
    }

    an::ui::shell::ApplicationCore& core;
    ImageIntegration::IntegrationJobsModel& jobs;
    Hooks hooks;
    std::function<bool(const QString&)> reveal;

    // The header.
    PictureWell* picture = nullptr;
    HomeText* heading = nullptr;
    an::ui::AnTag* object = nullptr;
    HomeText* objectCaption = nullptr;
    HomeText* folder = nullptr;
    an::ui::AnIconButton* showFolder = nullptr;
    HomeText* created = nullptr;
    HomeText* size = nullptr;
    QString pictureFile;
    quint64 pictureRead = 0;

    // The actions.
    QPushButton* continueButton = nullptr;
    QPushButton* addImages = nullptr;
    QPushButton* newIntegration = nullptr;
    QPushButton* settings = nullptr;
    QPushButton* close = nullptr;

    // The cards.
    SectionCard* workspacesCard = nullptr;
    QGridLayout* tiles = nullptr;
    std::vector<WorkspaceTile*> tileWidgets;
    // The empty half of the last row when the tiles are odd in number.
    QWidget* tileFiller = nullptr;
    std::vector<WorkspaceTileFacts> workspaces;
    std::map<QString, QImage> viewPictures;

    // The column at the right of the cards: narrower in a window under the narrow width, where the storage's two
    // buttons lie one over the other.
    QWidget* rightColumn = nullptr;
    QBoxLayout* storageButtons = nullptr;

    SectionCard* imagesCard = nullptr;
    QVBoxLayout* imageRowsLayout = nullptr;
    std::vector<ImageRow> imageRows;
    std::vector<QWidget*> imageRowWidgets;
    std::map<QString, QString> openIn;
    // What each image's file is, read off the GUI thread: its size, when it changed, its picture.
    std::map<QString, an::ui::HomeRecentFacts> facts;
    std::map<QString, QImage> pictures;

    SectionCard* integrationsCard = nullptr;
    std::vector<QWidget*> integrationWidgets;
    QStringList integrationRows;

    SectionCard* storageCard = nullptr;
    StorageBar* bar = nullptr;
    std::vector<std::pair<HomeText*, HomeText*>> legend;
    std::vector<HomeText*> legendNotes;
    std::vector<Swatch*> swatches;
    QPushButton* compact = nullptr;
    QPushButton* clean = nullptr;
    std::optional<an::ui::shell::ProjectStorageReport> storage;
    quint64 storageGeneration = 0;

    QPlainTextEdit* notes = nullptr;
    QTimer notesTimer;
    QString notesFolder;
    bool loadingNotes = false;

    [[nodiscard]] const an::ui::shell::ProjectDocument* project() const
    {
        return core.projects().current();
    }

    [[nodiscard]] QString projectDatabase() const
    {
        return QDir(core.projects().currentFolder())
            .filePath(QString::fromLatin1(an::ui::shell::kProjectIntegrationFolder) + QLatin1Char('/') +
                      QString::fromLatin1(ImageIntegration::kSessionDatabaseFileName));
    }
};

ProjectHomePage::ProjectHomePage(an::ui::shell::ApplicationCore& core, ImageIntegration::IntegrationJobsModel& jobs,
                                 Hooks hooks, QWidget* parent)
    : QScrollArea(parent), m_impl(std::make_unique<Impl>(core, jobs, std::move(hooks)))
{
    Impl& impl = *m_impl;
    setObjectName(QStringLiteral("observatoryProjectHome"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("observatoryProjectHomeContent"));
    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(parts::kHomePagePaddingSide, parts::kProjectHomePaddingTop, parts::kHomePagePaddingSide,
                               parts::kHomePagePaddingTop);
    column->setSpacing(parts::kProjectHomeGap);

    // The header: the picture, the eyebrow, the name, the object, the folder, when it was made, its size.
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(parts::kHomeCardPadding);
    impl.picture =
        new PictureWell(parts::kProjectHomePicture, parts::kProjectHomePicture, parts::kHomeCardRadius, content);
    header->addWidget(impl.picture, 0);
    auto* titles = new QVBoxLayout();
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(0);
    titles->addWidget(new HomeText(HomeText::Role::Caps, tr("Project"), content));
    auto* nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(parts::kHubSectionCaptionGap);
    impl.heading = new HomeText(HomeText::Role::Heading, QString(), content);
    impl.heading->setObjectName(QStringLiteral("projectHomeHeading"));
    impl.heading->setElideMode(Qt::ElideMiddle);
    nameRow->addWidget(impl.heading, 0);
    impl.object = new an::ui::AnTag(QString(), an::ui::AnTag::Tone::Accent, content);
    impl.object->setObjectName(QStringLiteral("projectHomeObject"));
    nameRow->addWidget(impl.object, 0, Qt::AlignVCenter);
    impl.objectCaption = new HomeText(HomeText::Role::Caption, QString(), content);
    nameRow->addWidget(impl.objectCaption, 0, Qt::AlignVCenter);
    nameRow->addStretch(1);
    titles->addLayout(nameRow);
    auto* meta = new QHBoxLayout();
    meta->setContentsMargins(0, 0, 0, 0);
    meta->setSpacing(parts::kHomeTileGap / 2);
    impl.folder = new HomeText(HomeText::Role::Mono, QString(), content);
    impl.folder->setObjectName(QStringLiteral("projectHomeFolder"));
    impl.folder->setElideMode(Qt::ElideLeft);
    meta->addWidget(impl.folder, 0);
    impl.showFolder = new an::ui::AnIconButton(QStringLiteral("folder_open"), tr("Show in File Browser"), content);
    impl.showFolder->setObjectName(QStringLiteral("projectHomeShowFolder"));
    meta->addWidget(impl.showFolder, 0);
    impl.created = new HomeText(HomeText::Role::Secondary, QString(), content);
    impl.created->setObjectName(QStringLiteral("projectHomeCreated"));
    meta->addWidget(impl.created, 0);
    impl.size = new HomeText(HomeText::Role::Mono, QString(), content);
    impl.size->setObjectName(QStringLiteral("projectHomeSize"));
    meta->addWidget(impl.size, 0);
    meta->addStretch(1);
    titles->addLayout(meta);
    header->addLayout(titles, 1);
    column->addLayout(header);

    // The actions.
    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(parts::kHomeTileGap / 2);
    impl.continueButton = new QPushButton(content);
    impl.continueButton->setObjectName(QStringLiteral("projectHomeContinue"));
    impl.continueButton->setProperty("anButtonVariant", QStringLiteral("primary"));
    actions->addWidget(impl.continueButton);
    impl.addImages = new QPushButton(tr("Add images..."), content);
    impl.addImages->setObjectName(QStringLiteral("projectHomeAddImages"));
    impl.addImages->setProperty("anButtonVariant", QStringLiteral("tonal"));
    actions->addWidget(impl.addImages);
    impl.newIntegration = new QPushButton(tr("New integration..."), content);
    impl.newIntegration->setObjectName(QStringLiteral("projectHomeNewIntegration"));
    impl.newIntegration->setProperty("anButtonVariant", QStringLiteral("tonal"));
    actions->addWidget(impl.newIntegration);
    impl.settings = new QPushButton(tr("Project settings..."), content);
    impl.settings->setObjectName(QStringLiteral("projectHomeSettings"));
    actions->addWidget(impl.settings);
    actions->addStretch(1);
    impl.close = new QPushButton(tr("Close project"), content);
    impl.close->setObjectName(QStringLiteral("projectHomeClose"));
    impl.close->setProperty("anButtonVariant", QStringLiteral("quiet"));
    actions->addWidget(impl.close);
    column->addLayout(actions);

    // The cards, in two columns.
    auto* grid = new QHBoxLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(parts::kProjectHomeGap);
    auto* left = new QVBoxLayout();
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(parts::kProjectHomeGap);
    impl.workspacesCard = new SectionCard(tr("Workspaces"), content);
    impl.tiles = new QGridLayout();
    impl.tiles->setContentsMargins(0, 0, 0, 0);
    impl.tiles->setSpacing(parts::kProjectSectionPadding);
    // Two tiles to a row, each as wide however many there are.
    impl.tiles->setColumnStretch(0, 1);
    impl.tiles->setColumnStretch(1, 1);
    impl.workspacesCard->body()->addLayout(impl.tiles);
    left->addWidget(impl.workspacesCard, 0);
    impl.imagesCard = new SectionCard(tr("Images and sequences"), content);
    // The card reaches down as far as the column beside it; its rows keep their own height, and what it has to spare
    // is left under the last of them.
    impl.imageRowsLayout = new QVBoxLayout();
    impl.imageRowsLayout->setContentsMargins(0, 0, 0, 0);
    impl.imageRowsLayout->setSpacing(parts::kProjectIntegrationRowPadding);
    impl.imagesCard->body()->addLayout(impl.imageRowsLayout);
    impl.imagesCard->body()->addStretch(1);
    left->addWidget(impl.imagesCard, 1);
    grid->addLayout(left, 1);
    auto* right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(parts::kProjectHomeGap);
    auto* rightColumn = new QWidget(content);
    rightColumn->setFixedWidth(parts::kProjectHomeSideColumn);
    impl.rightColumn = rightColumn;
    rightColumn->setLayout(right);
    impl.integrationsCard = new SectionCard(tr("Integrations"), rightColumn);
    right->addWidget(impl.integrationsCard, 0);
    impl.storageCard = new SectionCard(tr("Storage"), rightColumn);
    impl.storageCard->setSummary(tr("in the project folder"));
    impl.bar = new StorageBar(impl.storageCard);
    impl.storageCard->body()->addWidget(impl.bar);
    const QStringList legendNames = {tr("Undo history"), tr("Masters and stacked images"),
                                     tr("Calibrated and registered frames"), tr("Reports, layouts, notes"),
                                     tr("Free on the drive")};
    for (int index = 0; index < legendNames.size(); ++index)
    {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(parts::kHomeTileGap / 2);
        auto* swatch = new Swatch(QColor(), index == legendNames.size() - 1, impl.storageCard);
        impl.swatches.push_back(swatch);
        row->addWidget(swatch, 0, Qt::AlignTop);
        auto* names = new QVBoxLayout();
        names->setContentsMargins(0, 0, 0, 0);
        names->setSpacing(0);
        auto* name = new HomeText(HomeText::Role::Body, legendNames.at(index), impl.storageCard);
        names->addWidget(name);
        auto* note = new HomeText(HomeText::Role::Caption, QString(), impl.storageCard);
        note->hide();
        names->addWidget(note);
        impl.legendNotes.push_back(note);
        row->addLayout(names, 1);
        auto* value = new HomeText(HomeText::Role::Mono, QString(), impl.storageCard);
        value->setAlignment(Qt::AlignRight | Qt::AlignTop);
        row->addWidget(value, 0, Qt::AlignTop);
        impl.legend.emplace_back(name, value);
        impl.storageCard->body()->addLayout(row);
    }
    auto* storageButtons = new QHBoxLayout();
    storageButtons->setContentsMargins(0, 0, 0, 0);
    impl.storageButtons = storageButtons;
    impl.compact = new QPushButton(tr("Compact history..."), impl.storageCard);
    impl.compact->setObjectName(QStringLiteral("projectHomeCompactHistory"));
    storageButtons->addWidget(impl.compact, 0, Qt::AlignLeft);
    impl.clean = new QPushButton(tr("Clean intermediate files..."), impl.storageCard);
    impl.clean->setObjectName(QStringLiteral("projectHomeCleanIntermediate"));
    impl.clean->setToolTip(tr("Remove the calibrated, corrected and registered frames of this project's integration "
                              "jobs. A run makes them again from the lights."));
    impl.clean->setEnabled(false);
    storageButtons->addWidget(impl.clean, 0, Qt::AlignLeft);
    storageButtons->addStretch(1);
    impl.storageCard->body()->addLayout(storageButtons);
    right->addWidget(impl.storageCard, 0);
    auto* notesCard = new SectionCard(tr("Notes"), rightColumn);
    notesCard->setSummary(tr("saved with the project"));
    impl.notes = new QPlainTextEdit(notesCard);
    impl.notes->setObjectName(QStringLiteral("projectHomeNotes"));
    impl.notes->setAccessibleName(tr("Notes"));
    impl.notes->setPlaceholderText(tr("Notes about this project, kept in its folder as notes.md"));
    impl.notes->setMinimumHeight(parts::kProjectNotesMinHeight);
    notesCard->body()->addWidget(impl.notes, 1);
    right->addWidget(notesCard, 1);
    grid->addWidget(rightColumn, 0);
    column->addLayout(grid, 1);
    setWidget(content);

    // What the page asks for, the window answers.
    const auto connectHook = [this](QPushButton* button, std::function<void()> Hooks::* hook) {
        QObject::connect(button, &QPushButton::clicked, this, [this, hook]() {
            if (const std::function<void()>& act = m_impl->hooks.*hook)
            {
                act();
            }
        });
    };
    connectHook(impl.continueButton, &Hooks::continueProject);
    connectHook(impl.addImages, &Hooks::addImages);
    connectHook(impl.newIntegration, &Hooks::newIntegration);
    connectHook(impl.settings, &Hooks::projectSettings);
    connectHook(impl.close, &Hooks::closeProject);
    connectHook(impl.compact, &Hooks::compactHistory);
    connectHook(impl.clean, &Hooks::cleanIntermediate);
    QObject::connect(impl.showFolder, &QAbstractButton::clicked, this, [this]() {
        const QString folder = m_impl->core.projects().currentFolder();
        if (!folder.isEmpty())
        {
            (void)an::ui::showInFileBrowser(window(), folder, m_impl->reveal);
        }
    });
    QObject::connect(&impl.jobs, &ImageIntegration::IntegrationJobsModel::jobsChanged, this,
                     [this]() { rebuildIntegrations(); });
    impl.notesTimer.setSingleShot(true);
    impl.notesTimer.setInterval(kNotesSaveDelayMs);
    QObject::connect(&impl.notesTimer, &QTimer::timeout, this, [this]() { saveNotes(); });
    QObject::connect(impl.notes, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_impl->loadingNotes)
        {
            m_impl->notesTimer.start();
        }
    });
    refreshIcons();
}

ProjectHomePage::~ProjectHomePage()
{
    // What was written last reaches the file.
    if (m_impl->notesTimer.isActive())
    {
        saveNotes();
    }
}

void ProjectHomePage::refresh()
{
    Impl& impl = *m_impl;
    const an::ui::shell::ProjectDocument* const project = impl.project();
    if (project == nullptr)
    {
        impl.storage.reset();
        return;
    }
    rebuildHeader();
    rebuildImages();
    rebuildIntegrations();
    loadNotes();
    // The storage, measured on the thread pool; a measurement asked for before this one is dropped when it lands.
    impl.storage.reset();
    impl.size->setText(tr("Measuring..."));
    const quint64 generation = ++impl.storageGeneration;
    const QPointer<ProjectHomePage> self(this);
    impl.core.projects().storageReport([self, generation](const an::ui::shell::ProjectStorageReport& report) {
        if (!self || generation != self->m_impl->storageGeneration)
        {
            return;
        }
        self->m_impl->storage = report;
        self->showStorage();
        emit self->storageMeasured(report);
    });
}

void ProjectHomePage::setWorkspaces(const std::vector<WorkspaceTileFacts>& workspaces)
{
    m_impl->workspaces = workspaces;
    rebuildWorkspaces();
}

void ProjectHomePage::setImagesOpenIn(const std::map<QString, QString>& openIn)
{
    m_impl->openIn = openIn;
    rebuildImages();
}

void ProjectHomePage::setViewPicture(const QString& view, const QImage& picture)
{
    if (picture.isNull())
    {
        m_impl->viewPictures.erase(view);
    }
    else
    {
        m_impl->viewPictures[view] = picture;
    }
    for (WorkspaceTile* tile : m_impl->tileWidgets)
    {
        tile->miniature()->setPicture(view, picture);
    }
}

void ProjectHomePage::setFileReveal(std::function<bool(const QString&)> reveal)
{
    m_impl->reveal = std::move(reveal);
}

void ProjectHomePage::saveNotes()
{
    Impl& impl = *m_impl;
    impl.notesTimer.stop();
    if (impl.notesFolder.isEmpty())
    {
        return;
    }
    QSaveFile file(QDir(impl.notesFolder).filePath(QString::fromLatin1(an::ui::shell::kProjectNotesFileName)));
    if (file.open(QIODevice::WriteOnly))
    {
        (void)file.write(impl.notes->toPlainText().toUtf8());
        (void)file.commit();
    }
}

void ProjectHomePage::refreshIcons()
{
    m_impl->continueButton->setIcon(providedIcon(QStringLiteral("right")));
    m_impl->addImages->setIcon(providedIcon(QStringLiteral("add")));
    m_impl->newIntegration->setIcon(providedIcon(QStringLiteral("layers")));
    m_impl->settings->setIcon(providedIcon(QStringLiteral("settings")));
    m_impl->close->setIcon(providedIcon(QStringLiteral("close")));
    m_impl->compact->setIcon(providedIcon(QStringLiteral("history")));
}

QString ProjectHomePage::headingText() const
{
    return m_impl->heading->text();
}

an::ui::AnTag* ProjectHomePage::objectTag() const
{
    return m_impl->object;
}

QString ProjectHomePage::objectCaption() const
{
    return m_impl->objectCaption->text();
}

QString ProjectHomePage::folderText() const
{
    return m_impl->folder->text();
}

QString ProjectHomePage::createdText() const
{
    return m_impl->created->text();
}

QString ProjectHomePage::sizeText() const
{
    return m_impl->size->text();
}

QAbstractButton* ProjectHomePage::showInFileBrowserButton() const
{
    return m_impl->showFolder;
}

QAbstractButton* ProjectHomePage::continueButton() const
{
    return m_impl->continueButton;
}

QAbstractButton* ProjectHomePage::addImagesButton() const
{
    return m_impl->addImages;
}

QAbstractButton* ProjectHomePage::newIntegrationButton() const
{
    return m_impl->newIntegration;
}

QAbstractButton* ProjectHomePage::projectSettingsButton() const
{
    return m_impl->settings;
}

QAbstractButton* ProjectHomePage::closeProjectButton() const
{
    return m_impl->close;
}

std::vector<ProjectHomePage::WorkspaceTile*> ProjectHomePage::workspaceTiles() const
{
    return m_impl->tileWidgets;
}

std::vector<ProjectHomePage::ImageRow> ProjectHomePage::imageRows() const
{
    return m_impl->imageRows;
}

QStringList ProjectHomePage::integrationRows() const
{
    return m_impl->integrationRows;
}

const std::optional<an::ui::shell::ProjectStorageReport>& ProjectHomePage::storage() const noexcept
{
    return m_impl->storage;
}

std::vector<std::pair<QString, std::uint64_t>> ProjectHomePage::storageLegend() const
{
    std::vector<std::pair<QString, std::uint64_t>> legend;
    if (!m_impl->storage.has_value())
    {
        return legend;
    }
    const an::ui::shell::ProjectStorageReport& report = *m_impl->storage;
    const std::vector<std::uint64_t> bytes = {report.historyBytes + report.unlistedHistoryBytes,
                                              report.integrationBytes, report.intermediateBytes, report.otherBytes,
                                              report.freeBytes < 0 ? 0u : static_cast<std::uint64_t>(report.freeBytes)};
    for (std::size_t index = 0; index < m_impl->legend.size() && index < bytes.size(); ++index)
    {
        legend.emplace_back(m_impl->legend[index].first->text(), bytes[index]);
    }
    return legend;
}

QAbstractButton* ProjectHomePage::cleanIntermediateButton() const
{
    return m_impl->clean;
}

QAbstractButton* ProjectHomePage::compactHistoryButton() const
{
    return m_impl->compact;
}

QPlainTextEdit* ProjectHomePage::notes() const
{
    return m_impl->notes;
}

void ProjectHomePage::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    // Under the narrow width the side column is as narrow as a readout column there.
    const QWidget* const shown = window();
    const int side = shown != nullptr && shown->width() < parts::kPageNarrowWindow ? parts::kProjectHomeSideColumnNarrow
                                                                                   : parts::kProjectHomeSideColumn;
    if (m_impl->rightColumn != nullptr && m_impl->rightColumn->minimumWidth() != side)
    {
        m_impl->rightColumn->setFixedWidth(side);
    }
    if (m_impl->storageButtons != nullptr)
    {
        m_impl->storageButtons->setDirection(side == parts::kProjectHomeSideColumnNarrow ? QBoxLayout::TopToBottom
                                                                                         : QBoxLayout::LeftToRight);
    }
}

void ProjectHomePage::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    m_impl->continueButton->setText(m_impl->hooks.continueLabel ? m_impl->hooks.continueLabel() : QString());
    m_impl->continueButton->setVisible(!m_impl->continueButton->text().isEmpty());
}

void ProjectHomePage::rebuildHeader()
{
    Impl& impl = *m_impl;
    const an::ui::shell::ProjectDocument& project = *impl.project();
    impl.heading->setText(project.name);
    impl.heading->setToolTip(project.name);
    if (project.object.has_value())
    {
        impl.object->setText(project.object->name);
        switch (project.object->source)
        {
        case an::ui::shell::ProjectObjectSource::ObjectCard:
            impl.objectCaption->setText(tr("object, from the OBJECT card of its frames"));
            break;
        case an::ui::shell::ProjectObjectSource::PlateSolution:
            impl.objectCaption->setText(tr("object, from a plate solution"));
            break;
        case an::ui::shell::ProjectObjectSource::User:
            impl.objectCaption->setText(tr("object, as it was named"));
            break;
        }
    }
    impl.object->setVisible(project.object.has_value());
    impl.objectCaption->setVisible(project.object.has_value());
    const QString folder = impl.core.projects().currentFolder();
    impl.folder->setText(QDir::toNativeSeparators(folder));
    impl.folder->setToolTip(QDir::toNativeSeparators(folder));
    const QString created = tr("Created %1").arg(project.created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")));
    impl.created->setText(project.origin == an::ui::shell::ProjectOrigin::IntegrationJob
                              ? created + QLatin1Char(' ') + tr("from an integration job")
                              : created);
    impl.continueButton->setText(impl.hooks.continueLabel ? impl.hooks.continueLabel() : QString());
    impl.continueButton->setVisible(!impl.continueButton->text().isEmpty());

    // The picture: the image opened last.
    QString latest;
    QDateTime when;
    for (const an::ui::shell::ProjectImage& image : project.images)
    {
        if (image.lastOpened.isValid() && (!when.isValid() || image.lastOpened >= when))
        {
            when = image.lastOpened;
            latest = image.path;
        }
    }
    if (latest != impl.pictureFile)
    {
        impl.pictureFile = latest;
        impl.picture->setPicture(QImage());
        const quint64 read = ++impl.pictureRead;
        const QString thumbnail = latest.isEmpty() ? QString() : an::ui::homeThumbnailPath(latest);
        if (!thumbnail.isEmpty())
        {
            const QPointer<ProjectHomePage> self(this);
            an::ui::readHomeRecentFile(this, latest, thumbnail,
                                       QSize(parts::kProjectHomePicture * 2, parts::kProjectHomePicture * 2), {},
                                       [self, read](const QImage& picture, const an::ui::HomeRecentFacts& /*facts*/) {
                                           if (self && read == self->m_impl->pictureRead)
                                           {
                                               self->m_impl->picture->setPicture(picture);
                                           }
                                       });
        }
    }
}

void ProjectHomePage::rebuildImages()
{
    Impl& impl = *m_impl;
    for (QWidget* row : impl.imageRowWidgets)
    {
        row->hide();
        row->deleteLater();
    }
    impl.imageRowWidgets.clear();
    impl.imageRows.clear();
    const an::ui::shell::ProjectDocument* const project = impl.project();
    if (project == nullptr)
    {
        return;
    }
    std::size_t images = 0;
    std::size_t sequences = 0;
    for (const an::ui::shell::ProjectImage& image : project->images)
    {
        (image.kind == an::ui::shell::ProjectImageKind::Sequence ? sequences : images) += 1;
        ImageRow row;
        row.imageId = image.id;
        row.name = QFileInfo(image.path).fileName();
        row.type = an::ui::homeTypeText(image.path);
        const auto facts = impl.facts.find(image.path);
        if (facts != impl.facts.end() && facts->second.size.isValid())
        {
            row.dimensions = QStringLiteral("%1 × %2").arg(facts->second.size.width()).arg(facts->second.size.height());
        }
        if (facts != impl.facts.end() && facts->second.modified.isValid())
        {
            row.modified = facts->second.modified.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }
        row.history = QStringLiteral("—");
        if (impl.storage.has_value())
        {
            for (const an::ui::shell::ProjectImageStorage& storage : impl.storage->images)
            {
                if (storage.imageId == image.id)
                {
                    row.history =
                        counted(storage.steps, QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 step"),
                                QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 steps")) +
                        QStringLiteral(" · ") + an::ui::homeSizeText(static_cast<qint64>(storage.historyBytes));
                }
            }
        }
        const auto open = impl.openIn.find(image.id);
        row.state = open != impl.openIn.end() ? tr("Open in %1").arg(open->second) : tr("Closed");
        impl.imageRows.push_back(row);

        auto* line = new QWidget(impl.imagesCard);
        line->setObjectName(QStringLiteral("projectHomeImageRow"));
        auto* layout = new QHBoxLayout(line);
        auto* picture = new PictureWell(parts::kProjectImagePictureWidth, parts::kProjectImagePictureHeight,
                                        parts::kHomeTileRadius / 2.0, line);
        const auto held = impl.pictures.find(image.path);
        if (held != impl.pictures.end())
        {
            picture->setPicture(held->second);
        }
        auto* name = new HomeText(HomeText::Role::Mono, row.name, line);
        name->setElideMode(Qt::ElideMiddle);
        name->setToolTip(QDir::toNativeSeparators(image.path));
        auto* type = new an::ui::AnTag(row.type, an::ui::AnTag::Tone::Neutral, line);
        auto* typeCell = new QWidget(line);
        auto* typeLayout = new QHBoxLayout(typeCell);
        typeLayout->setContentsMargins(0, 0, 0, 0);
        typeLayout->addWidget(type, 0, Qt::AlignLeft | Qt::AlignVCenter);
        auto* state =
            new HomeText(open != impl.openIn.end() ? HomeText::Role::Body : HomeText::Role::Caption, row.state, line);
        addImageColumns(*layout, picture, name, typeCell, new HomeText(HomeText::Role::Mono, row.dimensions, line),
                        new HomeText(HomeText::Role::Mono, row.history, line), state,
                        new HomeText(HomeText::Role::Mono, row.modified, line));
        impl.imageRowsLayout->addWidget(line);
        impl.imageRowWidgets.push_back(line);

        // What the file is, read off the GUI thread once: its picture, its size and when it changed.
        if (facts == impl.facts.end())
        {
            impl.facts[image.path] = an::ui::HomeRecentFacts{};
            const QString path = image.path;
            const QPointer<ProjectHomePage> self(this);
            an::ui::readHomeRecentFile(
                this, path, an::ui::homeThumbnailPath(path),
                QSize(parts::kProjectImagePictureWidth * 2, parts::kProjectImagePictureHeight * 2),
                an::ui::HomeRecentProbe(&an::ui::probeHomeRecentFile),
                [self, path](const QImage& picture, const an::ui::HomeRecentFacts& read) {
                    if (!self)
                    {
                        return;
                    }
                    self->m_impl->facts[path] = read;
                    if (!picture.isNull())
                    {
                        self->m_impl->pictures[path] = picture;
                    }
                    self->rebuildImages();
                });
        }
    }
    impl.imagesCard->setSummary(counted(images, QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 image"),
                                        QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 images")) +
                                QStringLiteral(" · ") +
                                counted(sequences,
                                        QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 sequence"),
                                        QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 sequences")) +
                                QStringLiteral(" · ") + tr("referenced where they are, not copied"));
}

void ProjectHomePage::rebuildIntegrations()
{
    Impl& impl = *m_impl;
    for (QWidget* row : impl.integrationWidgets)
    {
        row->hide();
        row->deleteLater();
    }
    impl.integrationWidgets.clear();
    impl.integrationRows.clear();
    if (impl.project() == nullptr)
    {
        return;
    }
    const QString database = impl.projectDatabase();
    for (const ImageIntegration::IntegrationJob& job : impl.jobs.jobs())
    {
        if (!ImageIntegration::sameSessionDatabase(job.databasePath, database))
        {
            continue;
        }
        const QString status = ImageIntegration::integrationJobStatusText(job);
        impl.integrationRows.append(job.name + QStringLiteral(" · ") + status);
        auto* row = new QWidget(impl.integrationsCard);
        row->setObjectName(QStringLiteral("projectHomeIntegrationRow"));
        auto* column = new QVBoxLayout(row);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(parts::kHubWayTagGap);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        auto* name = new HomeText(HomeText::Role::Strong, job.name, row);
        name->setElideMode(Qt::ElideMiddle);
        head->addWidget(name, 1);
        head->addWidget(new an::ui::AnTag(status, an::ui::AnTag::Tone::Neutral, row), 0);
        column->addLayout(head);
        auto* foot = new QHBoxLayout();
        foot->setContentsMargins(0, 0, 0, 0);
        QStringList facts = {counted(static_cast<std::size_t>(job.frames),
                                     QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 frame"),
                                     QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 frames")),
                             counted(static_cast<std::size_t>(job.masters),
                                     QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 master"),
                                     QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 masters"))};
        if (job.lastRun.isValid())
        {
            facts.append(ImageIntegration::integrationJobLastRunText(job.lastRun));
            facts.append(ImageIntegration::integrationJobDurationText(job.durationMs));
        }
        foot->addWidget(new HomeText(HomeText::Role::Mono, facts.join(QStringLiteral(" · ")), row), 1);
        auto* open = new QPushButton(tr("Open"), row);
        open->setObjectName(QStringLiteral("projectHomeIntegrationOpen"));
        open->setProperty("anButtonVariant", QStringLiteral("quiet"));
        const QString databasePath = job.databasePath;
        const QString session = job.name;
        QObject::connect(open, &QPushButton::clicked, this, [this, databasePath, session]() {
            if (m_impl->hooks.openJob)
            {
                m_impl->hooks.openJob(databasePath, session);
            }
        });
        foot->addWidget(open, 0);
        column->addLayout(foot);
        impl.integrationsCard->body()->addWidget(row);
        impl.integrationWidgets.push_back(row);
    }
    if (impl.integrationWidgets.empty())
    {
        auto* none =
            new HomeText(HomeText::Role::Secondary, tr("No integration in this project yet."), impl.integrationsCard);
        impl.integrationsCard->body()->addWidget(none);
        impl.integrationWidgets.push_back(none);
    }
    impl.integrationsCard->setSummary(counted(static_cast<std::size_t>(impl.integrationRows.size()),
                                              QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 job"),
                                              QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 jobs")));
}

void ProjectHomePage::rebuildWorkspaces()
{
    Impl& impl = *m_impl;
    for (WorkspaceTile* tile : impl.tileWidgets)
    {
        tile->hide();
        tile->deleteLater();
    }
    impl.tileWidgets.clear();
    if (impl.tileFiller != nullptr)
    {
        impl.tileFiller->hide();
        impl.tileFiller->deleteLater();
        impl.tileFiller = nullptr;
    }
    int index = 0;
    for (const WorkspaceTileFacts& facts : impl.workspaces)
    {
        auto* tile = new WorkspaceTile(facts, impl.workspacesCard);
        for (const auto& [view, picture] : impl.viewPictures)
        {
            tile->miniature()->setPicture(view, picture);
        }
        const QString id = facts.id;
        QObject::connect(tile->openButton(), &QAbstractButton::clicked, this, [this, id]() {
            if (m_impl->hooks.showWorkspace)
            {
                m_impl->hooks.showWorkspace(id);
            }
        });
        QObject::connect(tile->renameButton(), &QAbstractButton::clicked, this, [this, id]() {
            if (m_impl->hooks.renameWorkspace)
            {
                m_impl->hooks.renameWorkspace(id);
            }
        });
        impl.tiles->addWidget(tile, index / 2, index % 2);
        impl.tileWidgets.push_back(tile);
        ++index;
    }
    if (index % 2 == 1)
    {
        // A lone last tile leaves the other half of its row empty: the half holds its place, and the gap before it.
        impl.tileFiller = new QWidget(impl.workspacesCard);
        impl.tiles->addWidget(impl.tileFiller, index / 2, 1);
    }
    impl.workspacesCard->setSummary(QString::number(impl.workspaces.size()) + QStringLiteral(" · ") +
                                    tr("their layouts are kept in the project"));
}

void ProjectHomePage::showStorage()
{
    Impl& impl = *m_impl;
    if (!impl.storage.has_value())
    {
        return;
    }
    const an::ui::shell::ProjectStorageReport& report = *impl.storage;
    const an::ui::DesignTokens& t = an::ui::tokens();
    const std::vector<QColor> colours = storageColours(t);
    const std::uint64_t free = report.freeBytes < 0 ? 0u : static_cast<std::uint64_t>(report.freeBytes);
    const std::vector<std::uint64_t> bytes = {report.historyBytes + report.unlistedHistoryBytes,
                                              report.integrationBytes, report.intermediateBytes, report.otherBytes,
                                              free};
    std::vector<StorageBar::Part> made;
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        made.push_back(StorageBar::Part{bytes[index], colours[index], index == bytes.size() - 1});
        impl.swatches[index]->setColour(colours[index]);
        impl.legend[index].second->setText(an::ui::homeSizeText(static_cast<qint64>(bytes[index])));
    }
    impl.bar->setParts(std::move(made));
    std::size_t steps = 0;
    for (const an::ui::shell::ProjectImageStorage& image : report.images)
    {
        steps += image.steps;
    }
    impl.legendNotes[0]->setText(counted(steps, QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "1 step"),
                                         QT_TRANSLATE_NOOP("an::ui::observatory::ProjectHomePage", "%1 steps")));
    impl.legendNotes[0]->setVisible(true);
    impl.legendNotes[2]->setText(tr("can be made again from the lights"));
    impl.legendNotes[2]->setVisible(report.intermediateBytes > 0u);
    const QStorageInfo drive(report.folder);
    impl.legend[4].first->setText(drive.isValid() && !drive.rootPath().isEmpty()
                                      ? tr("Free on %1").arg(QDir::toNativeSeparators(drive.rootPath()))
                                      : tr("Free on the drive"));
    const std::uint64_t whole = report.historyBytes + report.unlistedHistoryBytes + report.integrationBytes +
                                report.intermediateBytes + report.otherBytes;
    impl.size->setText(an::ui::homeSizeText(static_cast<qint64>(whole)));
    impl.compact->setEnabled(report.historyBytes + report.unlistedHistoryBytes > 0u);
    impl.clean->setEnabled(report.intermediateBytes > 0u);
    rebuildImages();
}

void ProjectHomePage::loadNotes()
{
    Impl& impl = *m_impl;
    const QString folder = impl.core.projects().currentFolder();
    if (folder == impl.notesFolder && !folder.isEmpty())
    {
        return;
    }
    if (impl.notesTimer.isActive())
    {
        saveNotes();
    }
    impl.notesFolder = folder;
    QFile file(QDir(folder).filePath(QString::fromLatin1(an::ui::shell::kProjectNotesFileName)));
    const QString text = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    impl.loadingNotes = true;
    impl.notes->setPlainText(text);
    impl.loadingNotes = false;
}

} // namespace an::ui::observatory
