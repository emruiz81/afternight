// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "components/AnTaskHeader.h"

#include "components/AnIconButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"
#include "theme/NightImage.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>

#include <algorithm>

#include <verdigris/wobjectimpl.h>

W_OBJECT_IMPL(an::ui::AnTaskHeader)

namespace an::ui
{

namespace
{

[[nodiscard]] QString translated(const char* text)
{
    return QCoreApplication::translate("AnTaskHeader", text);
}

[[nodiscard]] QFont nameFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontBody, QFont::DemiBold);
}

[[nodiscard]] QFont onFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontBody);
}

[[nodiscard]] QFont stateFont(const AnTaskHeader::State state)
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall,
                            state == AnTaskHeader::State::Running ? QFont::DemiBold : QFont::Normal);
}

[[nodiscard]] QColor stateColour(const AnTaskHeader::State state)
{
    const DesignTokens& t = tokens();
    switch (state)
    {
    case AnTaskHeader::State::UpToDate:
        return t.colors.textSecondary;
    case AnTaskHeader::State::Running:
        return t.colors.textAccent;
    case AnTaskHeader::State::Failed:
        return t.colors.statusError;
    case AnTaskHeader::State::Applied:
    case AnTaskHeader::State::None:
        break;
    }
    return t.colors.textSecondary;
}

} // namespace

struct AnTaskHeader::Impl
{
    QImage thumbnail;
    /// The thumbnail shown under Night in its Night form, made off the GUI thread.
    NightPicture picture;
    qint64 pictureKey = 0;
    QString processName;
    QString targetName;
    State state = State::None;
    QString stateText;
    std::optional<double> progress;
    bool floating = false;
    AnIconButton* floatButton = nullptr;
    AnIconButton* closeButton = nullptr;
    AnIconButton* menuButton = nullptr;
    QTimer slide;
    int slideStep = 0;

    /// Where the buttons begin: the text ends a gap before them.
    [[nodiscard]] int buttonsLeft(const AnTaskHeader& header) const
    {
        int left = header.width() - parts::kTaskHeaderPaddingEnd;
        for (const AnIconButton* button : {closeButton, floatButton, menuButton})
        {
            if (button != nullptr && !button->isHidden())
            {
                left = (std::min)(left, button->x());
            }
        }
        return left;
    }
};

AnTaskHeader::AnTaskHeader(QWidget* parent) : QWidget(parent), m_impl(new Impl)
{
    setObjectName(QStringLiteral("taskHeader"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_impl->menuButton = new AnIconButton(QStringLiteral("more_vert"), translated("The process's menus"), this);
    m_impl->menuButton->setObjectName(QStringLiteral("taskHeaderMenu"));
    m_impl->menuButton->hide();
    m_impl->floatButton = new AnIconButton(QStringLiteral("open_in_new"), translated("Float as window"), this);
    m_impl->floatButton->setObjectName(QStringLiteral("taskHeaderFloat"));
    m_impl->closeButton = new AnIconButton(QStringLiteral("close"), translated("Close (Esc)"), this);
    m_impl->closeButton->setObjectName(QStringLiteral("taskHeaderClose"));
    QObject::connect(m_impl->floatButton, &QAbstractButton::clicked, this, [this]() { floatRequested(); });
    QObject::connect(m_impl->closeButton, &QAbstractButton::clicked, this, [this]() { closeRequested(); });
    QObject::connect(m_impl->menuButton, &QAbstractButton::clicked, this, [this]() { menuRequested(); });
    m_impl->slide.setInterval(parts::kPageRunSpinnerStep);
    QObject::connect(&m_impl->slide, &QTimer::timeout, this, [this]() {
        ++m_impl->slideStep;
        update(runBarRect().isEmpty()
                   ? QRect()
                   : QRect(0, height() - parts::kTaskHeaderRunBar, width(), parts::kTaskHeaderRunBar));
    });
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

AnTaskHeader::~AnTaskHeader()
{
    delete m_impl;
}

void AnTaskHeader::setThumbnail(const QImage& thumbnail)
{
    m_impl->thumbnail = thumbnail;
    if (thumbnail.isNull())
    {
        m_impl->picture.clear();
        m_impl->pictureKey = 0;
    }
    else if (thumbnail.cacheKey() != m_impl->pictureKey)
    {
        // The same picture handed again keeps its Night form: the window hands it at every change of the task.
        m_impl->pictureKey = thumbnail.cacheKey();
        m_impl->picture.setPicture(thumbnail);
    }
    update();
}

QImage AnTaskHeader::thumbnail() const
{
    return m_impl->thumbnail;
}

void AnTaskHeader::setProcessName(const QString& name)
{
    m_impl->processName = name;
    setAccessibleName(m_impl->targetName.isEmpty() ? name : translated("%1 on %2").arg(name, m_impl->targetName));
    update();
}

QString AnTaskHeader::processName() const
{
    return m_impl->processName;
}

void AnTaskHeader::setTargetName(const QString& name)
{
    m_impl->targetName = name;
    setProcessName(m_impl->processName);
}

QString AnTaskHeader::targetName() const
{
    return m_impl->targetName;
}

void AnTaskHeader::setState(const State state, const QString& text, const std::optional<double> progress)
{
    m_impl->state = state;
    m_impl->stateText = text;
    m_impl->progress = progress;
    setAccessibleDescription(text);
    // A run that cannot tell how far it has come slides its bar across, while animations are on.
    if (state == State::Running && !progress.has_value() && AnimationUtils::animationsEnabled() && isVisible())
    {
        m_impl->slide.start();
    }
    else
    {
        m_impl->slide.stop();
    }
    update();
}

AnTaskHeader::State AnTaskHeader::state() const noexcept
{
    return m_impl->state;
}

QString AnTaskHeader::stateText() const
{
    return m_impl->stateText;
}

std::optional<double> AnTaskHeader::progress() const
{
    return m_impl->progress;
}

void AnTaskHeader::setFloating(const bool floating)
{
    m_impl->floating = floating;
    m_impl->floatButton->setIconKey(floating ? QStringLiteral("window") : QStringLiteral("open_in_new"));
    m_impl->floatButton->setDescription(floating ? translated("Attach") : translated("Float as window"));
}

bool AnTaskHeader::isFloating() const noexcept
{
    return m_impl->floating;
}

void AnTaskHeader::setMenuButtonVisible(const bool visible)
{
    m_impl->menuButton->setVisible(visible);
    resizeEvent(nullptr);
}

AnIconButton* AnTaskHeader::floatButton() const noexcept
{
    return m_impl->floatButton;
}

AnIconButton* AnTaskHeader::closeButton() const noexcept
{
    return m_impl->closeButton;
}

AnIconButton* AnTaskHeader::menuButton() const noexcept
{
    return m_impl->menuButton;
}

QString AnTaskHeader::shownTargetName() const
{
    if (m_impl->targetName.isEmpty())
    {
        return {};
    }
    const DesignTokens& t = tokens();
    const int gap = t.metrics.gap;
    int left = parts::kTaskHeaderPaddingStart + parts::kTaskHeaderThumbnail + gap;
    left += QFontMetrics(nameFont()).horizontalAdvance(m_impl->processName) + gap;
    left += QFontMetrics(onFont()).horizontalAdvance(translated("on")) + gap;
    int right = m_impl->buttonsLeft(*this) - gap;
    if (!m_impl->stateText.isEmpty())
    {
        right -= QFontMetrics(stateFont(m_impl->state)).horizontalAdvance(m_impl->stateText) +
                 parts::kTaskHeaderStateIcon + gap / 2 + gap;
    }
    return QFontMetrics(nameFont()).elidedText(m_impl->targetName, Qt::ElideMiddle, (std::max)(0, right - left));
}

QRect AnTaskHeader::runBarRect() const
{
    if (m_impl->state != State::Running)
    {
        return {};
    }
    const int top = height() - parts::kTaskHeaderRunBar;
    if (m_impl->progress.has_value())
    {
        const double share = std::clamp(*m_impl->progress, 0.0, 1.0);
        return QRect(0, top, static_cast<int>(width() * share), parts::kTaskHeaderRunBar);
    }
    const int length = static_cast<int>(width() * parts::kTaskHeaderSlidingShare);
    const int span = (std::max)(1, width() + length);
    const int left = (m_impl->slideStep * parts::kPageRunSpinnerDegrees) % span - length;
    return QRect(left, top, length, parts::kTaskHeaderRunBar);
}

QSize AnTaskHeader::sizeHint() const
{
    const DesignTokens& t = tokens();
    return QSize(t.metrics.strip * 10, t.metrics.strip);
}

QSize AnTaskHeader::minimumSizeHint() const
{
    return QSize(0, tokens().metrics.strip);
}

bool AnTaskHeader::event(QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::StyleChange || type == QEvent::PaletteChange || type == QEvent::Polish)
    {
        updateGeometry();
        resizeEvent(nullptr);
    }
    else if (type == QEvent::Show && m_impl->state == State::Running && !m_impl->progress.has_value() &&
             AnimationUtils::animationsEnabled())
    {
        m_impl->slide.start();
    }
    else if (type == QEvent::Hide)
    {
        m_impl->slide.stop();
    }
    return QWidget::event(event);
}

void AnTaskHeader::resizeEvent(QResizeEvent* event)
{
    if (event != nullptr)
    {
        QWidget::resizeEvent(event);
    }
    const DesignTokens& t = tokens();
    int right = width() - parts::kTaskHeaderPaddingEnd;
    for (AnIconButton* button : {m_impl->closeButton, m_impl->floatButton, m_impl->menuButton})
    {
        if (button->isHidden())
        {
            continue;
        }
        const QSize size = button->sizeHint();
        right -= size.width();
        button->setGeometry(right, (height() - size.height()) / 2, size.width(), size.height());
        right -= t.metrics.gap / 2;
    }
}

void AnTaskHeader::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), t.layers.surface1);
    painter.fillRect(QRect(0, height() - 1, width(), 1), t.colors.borderSubtle);
    const int gap = t.metrics.gap;
    int x = parts::kTaskHeaderPaddingStart;
    const int middle = height() / 2;

    // The target's thumbnail: its picture, or the well's colour while there is none.
    const QRectF thumb(x, middle - parts::kTaskHeaderThumbnail / 2.0, parts::kTaskHeaderThumbnail,
                       parts::kTaskHeaderThumbnail);
    QPainterPath frame;
    frame.addRoundedRect(thumb, parts::kTaskHeaderThumbnailRadius, parts::kTaskHeaderThumbnailRadius);
    painter.save();
    painter.setClipPath(frame);
    painter.fillPath(frame, t.colors.bgWell);
    if (!m_impl->picture.isNull())
    {
        // Under Night the picture is shown in its Night form; its place keeps the well's colour until that is made.
        const QPixmap shown = m_impl->picture.shown(this, [this]() { update(); });
        if (!shown.isNull())
        {
            painter.drawPixmap(thumb, shown, QRectF(QPointF(0, 0), QSizeF(shown.size())));
        }
    }
    painter.restore();
    x += parts::kTaskHeaderThumbnail + gap;

    const auto drawText = [&](const QString& text, const QFont& font, const QColor& colour) {
        painter.setFont(font);
        painter.setPen(colour);
        const int advance = QFontMetrics(font).horizontalAdvance(text);
        painter.drawText(QRect(x, 0, advance + 1, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
        x += advance + gap;
    };
    drawText(m_impl->processName, nameFont(), t.colors.textPrimary);
    if (!m_impl->targetName.isEmpty())
    {
        drawText(translated("on"), onFont(), t.colors.textTertiary);
        drawText(shownTargetName(), nameFont(), t.colors.textPrimary);
    }

    // The state, its icon before it.
    if (!m_impl->stateText.isEmpty())
    {
        const QColor colour = m_impl->state == State::UpToDate ? t.colors.statusSuccess : stateColour(m_impl->state);
        const QRectF icon(x, middle - parts::kTaskHeaderStateIcon / 2.0, parts::kTaskHeaderStateIcon,
                          parts::kTaskHeaderStateIcon);
        painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        if (m_impl->state == State::UpToDate || m_impl->state == State::Applied)
        {
            QPainterPath check;
            check.moveTo(icon.left() + icon.width() * 0.15, icon.center().y());
            check.lineTo(icon.left() + icon.width() * 0.42, icon.bottom() - icon.height() * 0.2);
            check.lineTo(icon.right() - icon.width() * 0.1, icon.top() + icon.height() * 0.2);
            painter.drawPath(check);
        }
        else if (m_impl->state == State::Failed)
        {
            painter.drawEllipse(icon.adjusted(1, 1, -1, -1));
            painter.drawLine(QPointF(icon.center().x(), icon.top() + 3), QPointF(icon.center().x(), icon.center().y()));
            painter.drawPoint(QPointF(icon.center().x(), icon.bottom() - 3));
        }
        else
        {
            painter.drawArc(icon.adjusted(1, 1, -1, -1), 90 * 16, -270 * 16);
        }
        x += parts::kTaskHeaderStateIcon + gap / 2;
        drawText(m_impl->stateText, stateFont(m_impl->state), stateColour(m_impl->state));
    }

    // The run's bar along the foot.
    const QRect bar = runBarRect();
    if (!bar.isEmpty())
    {
        painter.fillRect(bar.intersected(rect()), t.colors.accentPrimary);
    }
}

} // namespace an::ui
