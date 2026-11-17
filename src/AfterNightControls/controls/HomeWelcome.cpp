// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/HomeWelcome.h"

#include "controls/DashboardOnboardingPanel.h"
#include "controls/DashboardSettings.h"
#include "controls/EngineVersionReport.h"
#include "controls/HomeParts.h"
#include "controls/IAppContext.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "widgets/NightPictureLabel.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <verdigris/wobjectimpl.h>

#include <array>
#include <cstdint>

W_OBJECT_IMPL(an::ui::HomeWelcome)

namespace an::ui
{

namespace
{

/// The local files a drag carries.
QStringList localFiles(const QMimeData* data)
{
    QStringList files;
    if (data == nullptr)
    {
        return files;
    }
    for (const QUrl& url : data->urls())
    {
        const QString file = url.toLocalFile();
        if (!file.isEmpty())
        {
            files << file;
        }
    }
    return files;
}

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

/// The product's version as the welcome states it: "v1.1.0", or "dev" where none is stated.
QString versionText()
{
    const QString version = productVersion();
    if (version.isEmpty())
    {
        return QStringLiteral("dev");
    }
    return version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? version : QStringLiteral("v") + version;
}

/// A card to start with: its icon on the accent's container, its title and a line under it; a click asks for what it
/// names.
class WelcomeCard final : public QAbstractButton
{
public:
    WelcomeCard(const QString& title, const QString& line, const QString& iconName, QWidget* parent)
        : QAbstractButton(parent), m_title(title), m_line(line), m_iconName(iconName)
    {
        setObjectName(QStringLiteral("homeWelcomeCard"));
        setAccessibleName(title);
        setAccessibleDescription(line);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(parts::kHomeWelcomeCardWidth, parts::kHomeWelcomeCardHeight);
    }

    void refreshIcon()
    {
        m_icon = providedIcon(m_iconName);
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = parts::kHomeCardRadius;
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface2);
        painter.drawRoundedRect(place, radius, radius);

        const qreal box = parts::kHomeWelcomeCardIcon;
        const QRectF square(place.center().x() - box / 2.0, place.top() + parts::kHomeCardPadding, box, box);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.colors.accentContainer);
        painter.drawRoundedRect(square, parts::kHomeTileRadius, parts::kHomeTileRadius);
        const qreal glyph = parts::kHomeFeatureIcon;
        parts::paintIcon(painter, m_icon,
                         QRectF(square.center().x() - glyph / 2.0, square.center().y() - glyph / 2.0, glyph, glyph),
                         t.colors.textOnAccentContainer);

        const QFont titleFont = HomeText::fontFor(HomeText::Role::Strong);
        const QFont lineFont = HomeText::fontFor(HomeText::Role::Caption);
        const qreal titleTop = square.bottom() + parts::kHomeTilePadding;
        const qreal titleHeight = QFontMetricsF(titleFont).height();
        painter.setFont(titleFont);
        painter.setPen(HomeText::colourFor(HomeText::Role::Strong));
        painter.drawText(QRectF(place.left(), titleTop, place.width(), titleHeight), Qt::AlignCenter, m_title);
        painter.setFont(lineFont);
        painter.setPen(HomeText::colourFor(HomeText::Role::Secondary));
        painter.drawText(QRectF(place.left(), titleTop + titleHeight, place.width(), QFontMetricsF(lineFont).height()),
                         Qt::AlignCenter, m_line);

        parts::paintStateLayer(painter, place, radius, m_hovered, isDown());
        if (m_keyboardFocus && hasFocus())
        {
            parts::paintFocusRing(painter, place, radius);
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void focusInEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
        QAbstractButton::focusInEvent(event);
    }

private:
    QString m_title;
    QString m_line;
    QString m_iconName;
    QIcon m_icon;
    bool m_hovered = false;
    bool m_keyboardFocus = false;
};

/// The folder before the line about projects: the application's icon in the tertiary text.
class LineIcon final : public QWidget
{
public:
    explicit LineIcon(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(parts::kHomeActionIcon, parts::kHomeActionIcon);
    }

    void setIcon(const QIcon& icon)
    {
        m_icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        parts::paintIcon(painter, m_icon, QRectF(rect()), tokens().colors.textTertiary);
    }

private:
    QIcon m_icon;
};

/// The drop zone: a dashed outline with corners of 12, lit in the accent while a file is dragged over the welcome.
class DropZone final : public QWidget
{
public:
    explicit DropZone(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("homeWelcomeDropZone"));
        setAccessibleName(QCoreApplication::translate("an::ui::HomeWelcome", "Drop images or sequences here"));
        setFixedSize(parts::kHomeDropZoneWidth, parts::kHomeDropZoneHeight);
    }

    void setHighlighted(const bool highlighted)
    {
        m_highlighted = highlighted;
        update();
    }

    void setIcon(const QIcon& icon)
    {
        m_icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF place = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal radius = parts::kHomeDropZoneRadius;
        if (m_highlighted)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.colors.accentSofter);
            painter.drawRoundedRect(place, radius, radius);
        }
        QPen outline(m_highlighted ? t.colors.accentPrimary : t.colors.borderPrimary, 1.0);
        outline.setDashPattern({parts::kHomeDropZoneDash, parts::kHomeDropZoneDash});
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(place, radius, radius);
        const qreal icon = parts::kHomeDropZoneIcon;
        parts::paintIcon(
            painter, m_icon,
            QRectF(place.left() + parts::kHomeCardPadding * 2, place.center().y() - icon / 2.0, icon, icon),
            m_highlighted ? t.colors.textAccent : t.colors.textSecondary);
    }

private:
    QIcon m_icon;
    bool m_highlighted = false;
};

/// One thing AfterNight does: its icon in the accent, its title and a line under it.
class FeatureCard final : public QWidget
{
public:
    FeatureCard(const QString& title, const QString& line, const QString& icon, QWidget* parent)
        : QWidget(parent), m_icon(icon)
    {
        setObjectName(QStringLiteral("homeWelcomeFeature"));
        setAccessibleName(title);
        setAccessibleDescription(line);
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(parts::kHomeTilePadding, parts::kHomeTilePadding * 2 + parts::kHomeFeatureIcon,
                                   parts::kHomeTilePadding, parts::kHomeTilePadding);
        column->setSpacing(parts::kHomeTilePadding / 2);
        auto* name = new HomeText(HomeText::Role::Strong, title, this);
        name->setObjectName(QStringLiteral("homeWelcomeFeatureTitle"));
        column->addWidget(name);
        auto* under = new HomeText(HomeText::Role::Caption, line, this);
        under->setObjectName(QStringLiteral("homeWelcomeFeatureLine"));
        under->setWordWrap(true);
        under->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        column->addWidget(under, 1);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF place = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(t.colors.borderSubtle, 1.0));
        painter.setBrush(t.layers.surface1);
        painter.drawRoundedRect(place, parts::kHomeTileRadius, parts::kHomeTileRadius);
        const qreal icon = parts::kHomeFeatureIcon;
        parts::paintIcon(
            painter, QIcon(m_icon),
            QRectF(place.left() + parts::kHomeTilePadding, place.top() + parts::kHomeTilePadding, icon, icon),
            t.colors.textAccent);
    }

private:
    QString m_icon;
};

} // namespace

struct HomeWelcome::Impl
{
    NightPictureLabel* logo = nullptr;
    std::array<WelcomeCard*, 3> cards{};
    LineIcon* projectsIcon = nullptr;
    QPushButton* newProject = nullptr;
    DropZone* zone = nullptr;
    QPushButton* openFile = nullptr;
    bool highlighted = false;
};

HomeWelcome::HomeWelcome(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setObjectName(QStringLiteral("homeWelcome"));
    setAcceptDrops(true);
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, parts::kHomeSectionGap * 2, 0, parts::kHomeSectionGap);
    auto* content = new QWidget(this);
    content->setMaximumWidth(parts::kHomeWelcomeWidth);
    outer->addStretch(1);
    outer->addWidget(content, 100);
    outer->addStretch(1);

    auto* column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(parts::kHomeTilePadding);
    const auto centred = [column](QWidget* part) { column->addWidget(part, 0, Qt::AlignHCenter); };

    m_impl->logo = new NightPictureLabel(content);
    m_impl->logo->setObjectName(QStringLiteral("homeWelcomeLogo"));
    m_impl->logo->setFixedSize(parts::kHomeWelcomeLogo, parts::kHomeWelcomeLogo);
    m_impl->logo->setAlignment(Qt::AlignCenter);
    const QPixmap logo(QStringLiteral(":/AfterNight/icons/PNG/afternight_icon_256x256_blue.png"));
    if (!logo.isNull())
    {
        m_impl->logo->setPicture(logo.scaled(parts::kHomeWelcomeLogo, parts::kHomeWelcomeLogo, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
    }
    centred(m_impl->logo);

    auto* title = new HomeText(HomeText::Role::Display, tr("Welcome to AfterNight"), content);
    title->setObjectName(QStringLiteral("homeWelcomeTitle"));
    title->setElideMode(Qt::ElideNone);
    centred(title);
    auto* subtitle = new HomeText(HomeText::Role::CapsAccent, tr("Astrophotography processing suite"), content);
    subtitle->setObjectName(QStringLiteral("homeWelcomeSubtitle"));
    subtitle->setElideMode(Qt::ElideNone);
    centred(subtitle);
    auto* description = new HomeText(HomeText::Role::Body,
                                     tr("Calibrate, align, integrate and enhance your deep-sky images."), content);
    description->setObjectName(QStringLiteral("homeWelcomeDescription"));
    description->setElideMode(Qt::ElideNone);
    centred(description);
    auto* version = new HomeText(HomeText::Role::Mono, versionText(), content);
    version->setObjectName(QStringLiteral("homeWelcomeVersion"));
    version->setElideMode(Qt::ElideNone);
    centred(version);

    column->addSpacing(parts::kHomeSectionGap);
    auto* start = new HomeText(HomeText::Role::Caps, tr("Get started"), content);
    start->setElideMode(Qt::ElideNone);
    centred(start);
    auto* cards = new QHBoxLayout();
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(parts::kHomeCardGap);
    cards->addStretch(1);
    m_impl->cards = {
        new WelcomeCard(tr("Open an image"), tr("FITS · XISF · TIFF · RAW"), QStringLiteral("folder_open"), content),
        new WelcomeCard(tr("Stack your frames"), tr("Calibrate, align and stack"), QStringLiteral("stack"), content),
        new WelcomeCard(tr("Read the guide"), tr("Every process, step by step"), QStringLiteral("help"), content)};
    for (WelcomeCard* card : m_impl->cards)
    {
        cards->addWidget(card);
    }
    cards->addStretch(1);
    column->addLayout(cards);
    QObject::connect(m_impl->cards[0], &QAbstractButton::clicked, this, [this]() { emit openFileRequested(); });
    QObject::connect(m_impl->cards[1], &QAbstractButton::clicked, this, [this]() { emit imageIntegrationRequested(); });
    QObject::connect(m_impl->cards[2], &QAbstractButton::clicked, this, [this]() { emit openHelpRequested(); });

    // One quiet line: projects are optional, and here is how one is made.
    auto* projects = new QHBoxLayout();
    projects->setContentsMargins(0, 0, 0, 0);
    projects->setSpacing(parts::kHomeTilePadding / 2);
    projects->addStretch(1);
    m_impl->projectsIcon = new LineIcon(content);
    projects->addWidget(m_impl->projectsIcon, 0, Qt::AlignVCenter);
    auto* projectsLine =
        new HomeText(HomeText::Role::Secondary,
                     tr("Projects are optional. Without one, AfterNight works as it always has."), content);
    projectsLine->setObjectName(QStringLiteral("homeWelcomeProjectsLine"));
    projectsLine->setElideMode(Qt::ElideNone);
    projects->addWidget(projectsLine, 0, Qt::AlignVCenter);
    m_impl->newProject = new QPushButton(tr("New project..."), content);
    m_impl->newProject->setObjectName(QStringLiteral("homeWelcomeNewProject"));
    m_impl->newProject->setProperty("anButtonVariant", QStringLiteral("quiet"));
    m_impl->newProject->setCursor(Qt::PointingHandCursor);
    QObject::connect(m_impl->newProject, &QPushButton::clicked, this, [this]() { emit newProjectRequested(); });
    projects->addWidget(m_impl->newProject, 0, Qt::AlignVCenter);
    projects->addStretch(1);
    column->addLayout(projects);

    // The drop zone, with Open file.
    column->addSpacing(parts::kHomeTilePadding);
    m_impl->zone = new DropZone(content);
    auto* zoneRow = new QHBoxLayout(m_impl->zone);
    zoneRow->setContentsMargins(parts::kHomeCardPadding * 3 + parts::kHomeDropZoneIcon, 0, parts::kHomeCardPadding * 2,
                                0);
    zoneRow->setSpacing(parts::kHomeTilePadding);
    auto* zoneTexts = new QVBoxLayout();
    zoneTexts->setContentsMargins(0, 0, 0, 0);
    zoneTexts->setSpacing(0);
    zoneTexts->addStretch(1);
    auto* zoneTitle = new HomeText(HomeText::Role::Strong, tr("Drop images or sequences here"), m_impl->zone);
    zoneTitle->setObjectName(QStringLiteral("homeWelcomeDropTitle"));
    zoneTexts->addWidget(zoneTitle);
    auto* zoneLine =
        new HomeText(HomeText::Role::Caption, tr("FITS, XISF, TIFF, RAW, JPEG, PNG, SER and video"), m_impl->zone);
    zoneTexts->addWidget(zoneLine);
    zoneTexts->addStretch(1);
    zoneRow->addLayout(zoneTexts, 1);
    m_impl->openFile = new QPushButton(tr("Open file..."), m_impl->zone);
    m_impl->openFile->setObjectName(QStringLiteral("homeWelcomeOpenFile"));
    m_impl->openFile->setProperty("anButtonVariant", QStringLiteral("tonal"));
    QObject::connect(m_impl->openFile, &QPushButton::clicked, this, [this]() { emit openFileRequested(); });
    zoneRow->addWidget(m_impl->openFile, 0, Qt::AlignVCenter);
    centred(m_impl->zone);

    // What AfterNight does: the first-run panel's six.
    column->addSpacing(parts::kHomeSectionGap);
    auto* does = new HomeText(HomeText::Role::Caps, tr("What you can do"), content);
    does->setElideMode(Qt::ElideNone);
    column->addWidget(does);
    auto* features = new QHBoxLayout();
    features->setContentsMargins(0, 0, 0, 0);
    features->setSpacing(parts::kHomeCardGap);
    for (const DashboardFeature& feature : dashboardFeatures())
    {
        auto* card =
            new FeatureCard(QCoreApplication::translate("an::ui::DashboardOnboardingPanel", feature.title),
                            QCoreApplication::translate("an::ui::DashboardOnboardingPanel", feature.description),
                            QString::fromUtf8(feature.icon), content);
        features->addWidget(card, 1);
    }
    column->addLayout(features);
    column->addStretch(1);
}

HomeWelcome::~HomeWelcome() = default;

void HomeWelcome::refreshIcons()
{
    for (WelcomeCard* card : m_impl->cards)
    {
        card->refreshIcon();
    }
    m_impl->zone->setIcon(providedIcon(QStringLiteral("file_open")));
    m_impl->projectsIcon->setIcon(providedIcon(QStringLiteral("folder_closed")));
    update();
}

bool HomeWelcome::isDropHighlighted() const noexcept
{
    return m_impl->highlighted;
}

bool HomeWelcome::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::Polish)
    {
        refreshIcons();
    }
    return QWidget::event(event);
}

void HomeWelcome::paintEvent(QPaintEvent* /*event*/)
{
    // A quiet star field: the same stars at every paint, a few to the area, in the tertiary text at most at a share.
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    const int count = (width() * height()) / parts::kHomeStarArea;
    std::uint32_t state = 0x9E3779B9u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    for (int star = 0; star < count; ++star)
    {
        const qreal x = (next() % 10000u) / 10000.0 * width();
        const qreal y = (next() % 10000u) / 10000.0 * height();
        const qreal share = parts::kHomeStarShare * (0.25 + (next() % 1000u) / 1333.0);
        const qreal size = (next() % 4u) == 0u ? 1.6 : 1.0;
        painter.setBrush(parts::atShare(t.colors.textTertiary, share));
        painter.drawEllipse(QPointF(x, y), size / 2.0, size / 2.0);
    }
}

void HomeWelcome::dragEnterEvent(QDragEnterEvent* event)
{
    if (event != nullptr && !localFiles(event->mimeData()).isEmpty())
    {
        event->acceptProposedAction();
        setDropHighlighted(true);
        return;
    }
    if (event != nullptr)
    {
        event->ignore();
    }
}

void HomeWelcome::dragLeaveEvent(QDragLeaveEvent* /*event*/)
{
    setDropHighlighted(false);
}

void HomeWelcome::dropEvent(QDropEvent* event)
{
    setDropHighlighted(false);
    if (event == nullptr)
    {
        return;
    }
    const QStringList files = localFiles(event->mimeData());
    if (files.isEmpty())
    {
        event->ignore();
        return;
    }
    recordDashboardLastAction(QCoreApplication::translate("MainDashboard", "Open dropped files"));
    if (IAppContext* context = appContext())
    {
        if (IRecentFilesService* recents = context->getRecentFilesService())
        {
            recents->openDroppedFiles(files);
        }
    }
    event->acceptProposedAction();
}

void HomeWelcome::setDropHighlighted(const bool highlighted)
{
    m_impl->highlighted = highlighted;
    m_impl->zone->setHighlighted(highlighted);
}

} // namespace an::ui
