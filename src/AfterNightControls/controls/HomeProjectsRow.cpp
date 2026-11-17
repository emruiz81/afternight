// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeProjectsRow.h"

#include "components/AnTag.h"
#include "controls/HomeParts.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <QCoreApplication>
#include <QDir>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <algorithm>

W_OBJECT_IMPL(an::ui::HomeProjectCard)
W_OBJECT_IMPL(an::ui::HomeProjectsRow)

namespace an::ui
{

namespace
{

QIcon providedIcon(const QString& name)
{
    if (IAppContext* const context = appContext())
    {
        if (const IIconProviderPtr icons = context->getIconProvider())
        {
            return icons->getIcon(name);
        }
    }
    return {};
}

/// A count and its noun, singular for one.
QString counted(const int count, const char* one, const char* many)
{
    return count == 1 ? QCoreApplication::translate("an::ui::HomeProjectsRow", one)
                      : QCoreApplication::translate("an::ui::HomeProjectsRow", many).arg(count);
}

/// A part of a card that lets the card take the mouse.
template <typename Part> Part* passive(Part* part)
{
    part->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return part;
}

/// The Open project... tile: a dashed outline over no fill, the open folder's icon over its text.
class OpenProjectTile final : public QAbstractButton
{
public:
    explicit OpenProjectTile(QWidget* parent) : QAbstractButton(parent)
    {
        setText(QCoreApplication::translate("an::ui::HomeProjectsRow", "Open project..."));
        setAccessibleName(text());
        setToolTip(QCoreApplication::translate("an::ui::HomeProjectsRow", "Open the folder of an existing project"));
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedWidth(parts::kProjectOpenTileWidth);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        m_icon = providedIcon(QStringLiteral("folder_open"));
    }

    void refreshIcon()
    {
        m_icon = providedIcon(QStringLiteral("folder_open"));
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {parts::kProjectOpenTileWidth, parts::kProjectCardPicture + 2 * parts::kProjectCardPadding};
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPen dashed(t.colors.borderPrimary, 1.0);
        dashed.setDashPattern({parts::kHomeDropZoneDash, parts::kHomeDropZoneDash});
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(place, parts::kHomeCardRadius, parts::kHomeCardRadius);
        parts::paintStateLayer(painter, place, parts::kHomeCardRadius, underMouse(), isDown());
        if (hasFocus())
        {
            parts::paintFocusRing(painter, QRectF(rect()), parts::kHomeCardRadius);
        }
        const QFont font = HomeText::fontFor(HomeText::Role::Secondary);
        const QFontMetricsF metrics(font);
        const qreal icon = parts::kHomeFeatureIcon;
        const qreal top = (height() - icon - parts::kHomeTileGap / 2.0 - metrics.height()) / 2.0;
        parts::paintIcon(painter, m_icon, QRectF((width() - icon) / 2.0, top, icon, icon), t.colors.textSecondary);
        painter.setFont(font);
        painter.setPen(t.colors.textSecondary);
        painter.drawText(QRectF(0, top + icon + parts::kHomeTileGap / 2.0, width(), metrics.height()), Qt::AlignCenter,
                         metrics.elidedText(text(), Qt::ElideRight, width() - 2.0 * parts::kProjectCardPadding));
    }

    void enterEvent(QEnterEvent* event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    QIcon m_icon;
};

} // namespace

QString homeProjectCountsText(const HomeProjectSummary& summary, const bool history)
{
    QStringList parts;
    parts.append(counted(summary.images, QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "1 image"),
                         QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "%1 images")));
    if (!history && summary.sequences > 0)
    {
        parts.append(counted(summary.sequences, QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "1 sequence"),
                             QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "%1 sequences")));
    }
    if (summary.integrations > 0)
    {
        parts.append(counted(summary.integrations, QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "1 integration"),
                             QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "%1 integrations")));
    }
    const int steps = static_cast<int>(summary.steps);
    parts.append(history ? counted(steps, QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "1 history step"),
                                   QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "%1 history steps"))
                         : counted(steps, QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "1 step"),
                                   QT_TRANSLATE_NOOP("an::ui::HomeProjectsRow", "%1 steps")));
    return parts.join(QStringLiteral(" · "));
}

QString homeSizeText(const qint64 bytes)
{
    if (bytes < 0)
    {
        return {};
    }
    constexpr double kGigabyte = 1000.0 * 1000.0 * 1000.0;
    return QLocale().toString(static_cast<double>(bytes) / kGigabyte, 'f', 1) + QStringLiteral(" GB");
}

// ---- A project's card

struct HomeProjectCard::Impl
{
    HomeProjectSummary summary;
    NightPicture picture;
    quint64 read = 0;
    bool keyboardFocus = false;

    HomeText* name = nullptr;
    AnTag* object = nullptr;
    HomeText* folder = nullptr;
    HomeText* counts = nullptr;
    HomeText* foot = nullptr;
    AnTag* origin = nullptr;
};

HomeProjectCard::HomeProjectCard(QWidget* parent) : QAbstractButton(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeProjectCard"));
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(parts::kProjectCardPadding + parts::kProjectCardPicture + parts::kProjectCardsGap,
                            parts::kProjectCardPadding, parts::kProjectCardPadding, parts::kProjectCardPadding);
    auto* body = new QVBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(parts::kHomeTileGap / 2);
    m_impl->name = passive(new HomeText(HomeText::Role::Strong, QString(), this));
    m_impl->name->setElideMode(Qt::ElideMiddle);
    head->addWidget(m_impl->name, 1);
    m_impl->object = passive(new AnTag(QString(), AnTag::Tone::Accent, this));
    head->addWidget(m_impl->object, 0);
    body->addLayout(head);
    m_impl->folder = passive(new HomeText(HomeText::Role::Mono, QString(), this));
    m_impl->folder->setElideMode(Qt::ElideLeft);
    body->addWidget(m_impl->folder);
    m_impl->counts = passive(new HomeText(HomeText::Role::Caption, QString(), this));
    body->addWidget(m_impl->counts);
    m_impl->foot = passive(new HomeText(HomeText::Role::Mono, QString(), this));
    body->addWidget(m_impl->foot);
    m_impl->origin = passive(new AnTag(tr("from an integration job"), AnTag::Tone::Outlined, this));
    m_impl->origin->setIcon(providedIcon(QStringLiteral("layers")));
    body->addWidget(m_impl->origin, 0, Qt::AlignLeft);
    row->addLayout(body, 1);
}

HomeProjectCard::~HomeProjectCard() = default;

void HomeProjectCard::setSummary(const HomeProjectSummary& summary)
{
    const bool samePicture = summary.picture == m_impl->summary.picture;
    m_impl->summary = summary;
    setAccessibleName(summary.name);
    m_impl->name->setText(summary.name);
    m_impl->name->setToolTip(summary.name);
    m_impl->object->setText(summary.object);
    m_impl->object->setToolTip(summary.objectSource.isEmpty() ? QString() : tr("From %1").arg(summary.objectSource));
    m_impl->object->setVisible(!summary.object.isEmpty());
    m_impl->folder->setText(QDir::toNativeSeparators(summary.folder));
    m_impl->folder->setToolTip(QDir::toNativeSeparators(summary.folder));
    m_impl->counts->setText(summary.present ? homeProjectCountsText(summary, false) : tr("Its folder is not there"));
    m_impl->foot->setText(footText());
    m_impl->origin->setVisible(summary.fromIntegrationJob);
    setToolTip(tr("Open %1").arg(summary.name));
    // The picture of the image opened last, read off the GUI thread as a recent file's.
    if (!samePicture || m_impl->picture.isNull())
    {
        const quint64 read = ++m_impl->read;
        m_impl->picture.clear();
        const QString thumbnail = summary.picture.isEmpty() ? QString() : homeThumbnailPath(summary.picture);
        if (!thumbnail.isEmpty())
        {
            const QPointer<HomeProjectCard> self(this);
            readHomeRecentFile(this, summary.picture, thumbnail,
                               QSize(parts::kProjectCardPicture * 2, parts::kProjectCardPicture * 2), {},
                               [self, read](const QImage& picture, const HomeRecentFacts& /*facts*/) {
                                   if (self && read == self->m_impl->read && !picture.isNull())
                                   {
                                       self->m_impl->picture.setPicture(picture);
                                       self->update();
                                   }
                               });
        }
    }
    updateGeometry();
    update();
}

const HomeProjectSummary& HomeProjectCard::summary() const noexcept
{
    return m_impl->summary;
}

QString HomeProjectCard::countsText() const
{
    return m_impl->counts->text();
}

QString HomeProjectCard::footText() const
{
    QStringList parts;
    const QString size = homeSizeText(m_impl->summary.bytes);
    if (!size.isEmpty())
    {
        parts.append(size);
    }
    if (m_impl->summary.lastOpened.isValid())
    {
        parts.append(
            tr("opened %1").arg(m_impl->summary.lastOpened.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))));
    }
    return parts.join(QStringLiteral(" · "));
}

QString HomeProjectCard::folderText() const
{
    return m_impl->summary.folder;
}

AnTag* HomeProjectCard::objectTag() const
{
    return m_impl->object;
}

AnTag* HomeProjectCard::originTag() const
{
    return m_impl->origin;
}

QSize HomeProjectCard::sizeHint() const
{
    const QSize contents = layout() != nullptr ? layout()->sizeHint() : QSize();
    return {(std::max)(contents.width(), parts::kHomeTileMinWidth),
            (std::max)(contents.height(), parts::kProjectCardPicture + 2 * parts::kProjectCardPadding)};
}

QSize HomeProjectCard::minimumSizeHint() const
{
    return {parts::kProjectCardPicture + 2 * parts::kProjectCardPadding + parts::kProjectCardsGap +
                parts::kHomeTileMinWidth / 2,
            sizeHint().height()};
}

bool HomeProjectCard::event(QEvent* event)
{
    if (event->type() == QEvent::Polish)
    {
        m_impl->origin->setIcon(providedIcon(QStringLiteral("layers")));
    }
    return QAbstractButton::event(event);
}

void HomeProjectCard::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(t.colors.borderSubtle, 1.0));
    painter.setBrush(t.layers.surface2);
    painter.drawRoundedRect(card, parts::kHomeCardRadius, parts::kHomeCardRadius);
    parts::paintStateLayer(painter, card, parts::kHomeCardRadius, underMouse(), isDown());
    if (hasFocus() && m_impl->keyboardFocus)
    {
        parts::paintFocusRing(painter, QRectF(rect()), parts::kHomeCardRadius);
    }
    const QRectF picture(parts::kProjectCardPadding, parts::kProjectCardPadding, parts::kProjectCardPicture,
                         parts::kProjectCardPicture);
    paintHomePicture(painter, picture, m_impl->picture.shown(this, [this]() { update(); }), t.radius.md);
}

void HomeProjectCard::focusInEvent(QFocusEvent* event)
{
    m_impl->keyboardFocus = parts::isKeyboardFocusReason(event->reason());
    QAbstractButton::focusInEvent(event);
}

void HomeProjectCard::enterEvent(QEnterEvent* event)
{
    QAbstractButton::enterEvent(event);
    update();
}

void HomeProjectCard::leaveEvent(QEvent* event)
{
    QAbstractButton::leaveEvent(event);
    update();
}

// ---- The row

struct HomeProjectsRow::Impl
{
    HomeText* count = nullptr;
    std::vector<HomeProjectCard*> cards;
    OpenProjectTile* tile = nullptr;
    std::size_t shown = 0;
};

HomeProjectsRow::HomeProjectsRow(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeProjectsRow"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(parts::kHomeTileGap / 2);
    auto* head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(parts::kHubSectionCaptionGap);
    auto* title = new HomeText(HomeText::Role::Section, tr("Projects"), this);
    title->setElideMode(Qt::ElideNone);
    head->addWidget(title, 0, Qt::AlignVCenter);
    m_impl->count = new HomeText(HomeText::Role::Mono, QString(), this);
    m_impl->count->setElideMode(Qt::ElideNone);
    head->addWidget(m_impl->count, 0, Qt::AlignVCenter);
    head->addWidget(new HomeText(HomeText::Role::Secondary,
                                 tr("Optional. A project is a folder that keeps the history, the workspaces and the "
                                    "integration jobs of a body of work."),
                                 this),
                    1, Qt::AlignVCenter);
    column->addLayout(head);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(parts::kProjectCardsGap);
    for (std::size_t index = 0; index < kShown; ++index)
    {
        auto* card = new HomeProjectCard(this);
        card->hide();
        QObject::connect(card, &QAbstractButton::clicked, this,
                         [this, card]() { emit openProjectRequested(card->summary().folder); });
        m_impl->cards.push_back(card);
        row->addWidget(card, 1);
    }
    m_impl->tile = new OpenProjectTile(this);
    m_impl->tile->setObjectName(QStringLiteral("homeOpenProjectTile"));
    QObject::connect(m_impl->tile, &QAbstractButton::clicked, this, [this]() { emit openOtherProjectRequested(); });
    row->addWidget(m_impl->tile, 0);
    row->addStretch(0);
    column->addLayout(row);
}

HomeProjectsRow::~HomeProjectsRow() = default;

void HomeProjectsRow::setProjects(const std::vector<HomeProjectSummary>& projects)
{
    m_impl->shown = (std::min)(projects.size(), kShown);
    for (std::size_t index = 0; index < kShown; ++index)
    {
        HomeProjectCard* const card = m_impl->cards[index];
        if (index < m_impl->shown)
        {
            card->setSummary(projects[index]);
            card->show();
        }
        else
        {
            card->hide();
        }
    }
    m_impl->count->setText(QString::number(projects.size()));
}

std::vector<HomeProjectCard*> HomeProjectsRow::cards() const
{
    return {m_impl->cards.begin(), m_impl->cards.begin() + static_cast<std::ptrdiff_t>(m_impl->shown)};
}

QAbstractButton* HomeProjectsRow::openProjectTile() const
{
    return m_impl->tile;
}

void HomeProjectsRow::refreshIcons()
{
    m_impl->tile->refreshIcon();
}

} // namespace an::ui
