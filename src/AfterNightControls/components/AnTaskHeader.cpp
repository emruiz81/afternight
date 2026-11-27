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
#include <QHelpEvent>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <vector>

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

// ---- A job's type

[[nodiscard]] QFont jobEyebrowFont()
{
    const DesignTokens& t = tokens();
    return parts::capsFont(t.typography.caption, t.metrics.fontCaption);
}

[[nodiscard]] QFont jobTitleFont()
{
    return parts::sizedFont(tokens().typography.heading1, parts::kJobHeaderTitleFont, QFont::DemiBold);
}

[[nodiscard]] QFont jobFactsFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.monospace, t.metrics.fontSmall);
}

[[nodiscard]] QFont jobMeterLabelFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

[[nodiscard]] QFont jobMeterTextFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.monospace, t.metrics.fontCaption);
}

/// How high a job's three lines are together.
[[nodiscard]] int jobTextHeight()
{
    return QFontMetrics(jobEyebrowFont()).height() + QFontMetrics(jobTitleFont()).height() + parts::kJobHeaderFactsGap +
           QFontMetrics(jobFactsFont()).height();
}

/// What an action says it does: its tooltip, or its text without the mnemonic's mark.
[[nodiscard]] QString saidBy(const QAction& action)
{
    QString text = action.text();
    text.remove(QLatin1Char('&'));
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    const QString tip = action.toolTip();
    // An action with no tooltip of its own answers its text for one.
    QString plainTip = tip;
    plainTip.remove(QLatin1Char('&'));
    return tip.isEmpty() || plainTip == text || tip == action.text() ? text : tip;
}

/// An action's text as a button shows it: without the mnemonic's mark and the keys after a tab.
[[nodiscard]] QString buttonText(const QAction& action)
{
    QString text = action.text();
    text.remove(QLatin1Char('&'));
    const qsizetype tab = text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
    {
        text.truncate(tab);
    }
    return text;
}

} // namespace

struct AnTaskHeader::Impl
{
    QImage thumbnail;
    /// The thumbnail shown under Night in its Night form, made off the GUI thread.
    NightPicture picture;
    qint64 pictureKey = 0;
    /// The process's glyph, drawn on a tile while there is no picture, and its icon.
    QString glyphKey;
    QIcon glyph;
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

    /// The job the header says in place of a process and its image, its buttons, and what follows its actions.
    std::optional<JobHeading> job;
    std::vector<AnIconButton*> actButtons;
    QPushButton* primaryButton = nullptr;
    std::vector<QMetaObject::Connection> jobConnections;
    /// Where the job's text ends, where its measure's text and bar lie - empty when the measure is left out - and where
    /// the hairline before the ways out stands; laid out with the buttons.
    int jobTextRight = 0;
    QRect meterText;
    QRect meterBar;
    int separatorX = -1;

    /// How wide the measure's label and text are drawn, a space between them.
    [[nodiscard]] int meterTextWidth() const
    {
        if (!job.has_value() || !job->meter.has_value())
        {
            return 0;
        }
        int width = parts::textWidth(jobMeterTextFont(), job->meter->text);
        if (!job->meter->label.isEmpty())
        {
            const QFont labelFont = jobMeterLabelFont();
            width += parts::textWidth(labelFont, job->meter->label) +
                     QFontMetrics(labelFont).horizontalAdvance(QLatin1Char(' '));
        }
        return width;
    }

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
        // Animations turned off while it slid: the bar stops where a bar drawn with them off stands.
        if (!AnimationUtils::animationsEnabled())
        {
            m_impl->slide.stop();
            m_impl->slideStep = 0;
            update();
            return;
        }
        ++m_impl->slideStep;
        update(runBarRect().isEmpty()
                   ? QRect()
                   : QRect(0, height() - parts::kTaskHeaderRunBar, width(), parts::kTaskHeaderRunBar));
    });
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

AnTaskHeader::~AnTaskHeader()
{
    for (const QMetaObject::Connection& connection : m_impl->jobConnections)
    {
        QObject::disconnect(connection);
    }
    delete m_impl;
}

void AnTaskHeader::setJob(const std::optional<JobHeading>& job)
{
    Impl& impl = *m_impl;
    if (impl.job == job)
    {
        return;
    }
    // The buttons are made again only when the actions they stand for change: a job said again with other facts keeps
    // them, and the focus one of them holds.
    const bool sameActs =
        impl.job.has_value() && job.has_value() && impl.job->acts == job->acts && impl.job->primary == job->primary;
    const bool heightChanges = impl.job.has_value() != job.has_value();
    impl.job = job;
    if (!sameActs)
    {
        for (const QMetaObject::Connection& connection : impl.jobConnections)
        {
            QObject::disconnect(connection);
        }
        impl.jobConnections.clear();
        // A button may be the one whose press brought the new job: it goes once that press has returned.
        for (AnIconButton* button : impl.actButtons)
        {
            button->hide();
            button->deleteLater();
        }
        impl.actButtons.clear();
        if (impl.primaryButton != nullptr)
        {
            impl.primaryButton->hide();
            impl.primaryButton->deleteLater();
            impl.primaryButton = nullptr;
        }
        if (job.has_value())
        {
            for (const JobHeading::Act& act : job->acts)
            {
                auto* button = new AnIconButton(act.iconKey, QString(), this);
                button->setObjectName(QStringLiteral("taskHeaderAct.%1").arg(act.iconKey));
                impl.actButtons.push_back(button);
                const QPointer<QAction> action = act.action;
                const auto follow = [button, action]() {
                    button->setEnabled(!action.isNull() && action->isEnabled());
                    if (!action.isNull())
                    {
                        button->setDescription(saidBy(*action));
                    }
                };
                follow();
                if (!action.isNull())
                {
                    impl.jobConnections.push_back(QObject::connect(action.data(), &QAction::changed, button, follow));
                    impl.jobConnections.push_back(QObject::connect(action.data(), &QObject::destroyed, button,
                                                                   [button]() { button->setEnabled(false); }));
                }
                QObject::connect(button, &QAbstractButton::clicked, this, [action]() {
                    if (!action.isNull() && action->isEnabled())
                    {
                        action->trigger();
                    }
                });
                button->show();
            }
            if (!job->primary.isNull())
            {
                auto* button = new QPushButton(this);
                button->setObjectName(QStringLiteral("taskHeaderPrimary"));
                button->setProperty("anButtonVariant", QStringLiteral("primary"));
                impl.primaryButton = button;
                const QPointer<QAction> action = job->primary;
                const auto follow = [this, button, action]() {
                    if (action.isNull())
                    {
                        button->setEnabled(false);
                        return;
                    }
                    button->setEnabled(action->isEnabled());
                    const QString text = buttonText(*action);
                    const bool resized = text != button->text();
                    button->setText(text);
                    button->setToolTip(saidBy(*action) == text ? QString() : saidBy(*action));
                    if (resized)
                    {
                        resizeEvent(nullptr);
                        update();
                    }
                };
                follow();
                impl.jobConnections.push_back(QObject::connect(action.data(), &QAction::changed, button, follow));
                impl.jobConnections.push_back(QObject::connect(action.data(), &QObject::destroyed, button,
                                                               [button]() { button->setEnabled(false); }));
                QObject::connect(button, &QAbstractButton::clicked, this, [action]() {
                    if (!action.isNull() && action->isEnabled())
                    {
                        action->trigger();
                    }
                });
                button->show();
            }
        }
    }
    if (job.has_value() && !sameActs)
    {
        // The keyboard meets the buttons in the order they are read in: the acts, the menus, the primary act, the ways
        // out.
        QWidget* before = nullptr;
        const auto next = [&before](QWidget* button) {
            if (button == nullptr || button->isHidden())
            {
                return;
            }
            if (before != nullptr)
            {
                QWidget::setTabOrder(before, button);
            }
            before = button;
        };
        for (AnIconButton* button : impl.actButtons)
        {
            next(button);
        }
        next(impl.menuButton);
        next(impl.primaryButton);
        next(impl.floatButton);
        next(impl.closeButton);
    }
    if (job.has_value())
    {
        setAccessibleName(job->eyebrow.isEmpty() ? job->title : QStringLiteral("%1: %2").arg(job->eyebrow, job->title));
        setAccessibleDescription(job->facts);
    }
    else
    {
        setProcessName(impl.processName);
        setAccessibleDescription(impl.stateText);
    }
    if (heightChanges)
    {
        updateGeometry();
    }
    resizeEvent(nullptr);
    update();
}

const std::optional<JobHeading>& AnTaskHeader::job() const noexcept
{
    return m_impl->job;
}

QList<QAbstractButton*> AnTaskHeader::jobActButtons() const
{
    QList<QAbstractButton*> buttons;
    for (AnIconButton* button : m_impl->actButtons)
    {
        buttons.append(button);
    }
    return buttons;
}

QAbstractButton* AnTaskHeader::jobPrimaryButton() const noexcept
{
    return m_impl->primaryButton;
}

QRect AnTaskHeader::jobMeterRect() const
{
    return m_impl->job.has_value() ? m_impl->meterBar : QRect();
}

QString AnTaskHeader::shownJobTitle() const
{
    if (!m_impl->job.has_value())
    {
        return {};
    }
    const int room = (std::max)(0, m_impl->jobTextRight - parts::kJobHeaderPaddingStart);
    return QFontMetrics(jobTitleFont()).elidedText(m_impl->job->title, Qt::ElideRight, room);
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

void AnTaskHeader::setGlyphKey(const QString& key)
{
    if (key == m_impl->glyphKey)
    {
        return;
    }
    m_impl->glyphKey = key;
    // The product's icon of the key; the header draws it in its own colour, as the rail does.
    m_impl->glyph = key.isEmpty() ? QIcon() : QIcon(QStringLiteral(":/AfterNight/icons/SVG/%1.svg").arg(key));
    update();
}

QString AnTaskHeader::glyphKey() const
{
    return m_impl->glyphKey;
}

void AnTaskHeader::setProcessName(const QString& name)
{
    m_impl->processName = name;
    if (!m_impl->job.has_value())
    {
        // A header that says a job is named by the job.
        setAccessibleName(m_impl->targetName.isEmpty() ? name : translated("%1 on %2").arg(name, m_impl->targetName));
    }
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
    if (!m_impl->job.has_value())
    {
        setAccessibleDescription(text);
    }
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
    if (m_impl->job.has_value())
    {
        return QSize(t.metrics.strip * 10, jobTextHeight() + 2 * parts::kJobHeaderPaddingVertical);
    }
    return QSize(t.metrics.strip * 10, t.metrics.strip);
}

QSize AnTaskHeader::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
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
    else if (type == QEvent::ToolTip && m_impl->job.has_value() && m_impl->job->meter.has_value() &&
             !m_impl->meterBar.isEmpty())
    {
        // The measure says what it is made of where it is drawn.
        const auto* const help = static_cast<const QHelpEvent*>(event);
        const QRect measure = m_impl->meterText.united(m_impl->meterBar);
        if (measure.contains(help->pos()) && !m_impl->job->meter->toolTip.isEmpty())
        {
            QToolTip::showText(help->globalPos(), m_impl->job->meter->toolTip, this, measure);
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
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
    Impl& impl = *m_impl;
    if (impl.job.has_value())
    {
        // From the right: Close, Float as window, a hairline, the primary act, the menu button, the acts, the measure.
        const int gap = parts::kJobHeaderPartsGap;
        int right = width() - parts::kJobHeaderPaddingEnd;
        const auto place = [this, &right, gap](QWidget* widget) {
            const QSize size = widget->sizeHint();
            right -= size.width();
            widget->setGeometry(right, (height() - size.height()) / 2, size.width(), size.height());
            right -= gap;
        };
        place(impl.closeButton);
        place(impl.floatButton);
        impl.separatorX = right;
        right -= 1 + gap;
        if (impl.primaryButton != nullptr)
        {
            place(impl.primaryButton);
        }
        if (!impl.menuButton->isHidden())
        {
            place(impl.menuButton);
        }
        for (auto button = impl.actButtons.rbegin(); button != impl.actButtons.rend(); ++button)
        {
            place(*button);
        }
        impl.meterText = QRect();
        impl.meterBar = QRect();
        impl.jobTextRight = right + gap - parts::kJobHeaderGap;
        if (impl.job->meter.has_value())
        {
            // The measure keeps its place only where the name keeps its least width beside it.
            const int block = (std::max)(impl.meterTextWidth(), parts::kJobHeaderMeterWidth);
            const int blockRight = right - parts::kJobHeaderMeterMargin;
            const int blockLeft = blockRight - block;
            const int textRight = blockLeft - parts::kJobHeaderGap;
            if (textRight - parts::kJobHeaderPaddingStart >= parts::kJobHeaderTextLeast)
            {
                const int textHeight =
                    (std::max)(QFontMetrics(jobMeterLabelFont()).height(), QFontMetrics(jobMeterTextFont()).height());
                const int blockHeight = textHeight + parts::kJobHeaderMeterGap + parts::kJobHeaderMeterBar;
                const int top = (height() - blockHeight) / 2;
                impl.meterText = QRect(blockLeft, top, block, textHeight);
                impl.meterBar =
                    QRect(blockRight - parts::kJobHeaderMeterWidth, top + textHeight + parts::kJobHeaderMeterGap,
                          parts::kJobHeaderMeterWidth, parts::kJobHeaderMeterBar);
                impl.jobTextRight = textRight;
            }
        }
        return;
    }
    int right = width() - parts::kTaskHeaderPaddingEnd;
    for (AnIconButton* button : {impl.closeButton, impl.floatButton, impl.menuButton})
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
    if (m_impl->job.has_value())
    {
        const Impl& impl = *m_impl;
        const JobHeading& job = *impl.job;
        const int left = parts::kJobHeaderPaddingStart;
        const int room = (std::max)(0, impl.jobTextRight - left);
        int top = (height() - jobTextHeight()) / 2;
        const auto line = [&painter, left, room, &top](const QString& text, const QFont& font, const QColor& colour) {
            const QFontMetrics metrics(font);
            painter.setFont(font);
            painter.setPen(colour);
            painter.drawText(QRect(left, top, room, metrics.height()), Qt::AlignVCenter | Qt::AlignLeft,
                             metrics.elidedText(text, Qt::ElideRight, room));
            top += metrics.height();
        };
        line(job.eyebrow, jobEyebrowFont(), t.colors.textAccent);
        line(job.title, jobTitleFont(), t.colors.textPrimary);
        top += parts::kJobHeaderFactsGap;
        line(job.facts, jobFactsFont(), t.colors.textSecondary);

        // The measure: its label and its text over its bar, at the right of its place.
        if (job.meter.has_value() && !impl.meterBar.isEmpty())
        {
            const QFont labelFont = jobMeterLabelFont();
            const QFont textFont = jobMeterTextFont();
            const int textWidth = parts::textWidth(textFont, job.meter->text);
            painter.setFont(textFont);
            painter.setPen(t.colors.textPrimary);
            painter.drawText(
                QRect(impl.meterText.right() + 1 - textWidth, impl.meterText.top(), textWidth, impl.meterText.height()),
                Qt::AlignVCenter | Qt::AlignLeft, job.meter->text);
            if (!job.meter->label.isEmpty())
            {
                painter.setFont(labelFont);
                painter.setPen(t.colors.textSecondary);
                painter.drawText(QRect(impl.meterText.left(), impl.meterText.top(), impl.meterText.width() - textWidth,
                                       impl.meterText.height()),
                                 Qt::AlignVCenter | Qt::AlignLeft, job.meter->label);
            }
            const QRectF bar(impl.meterBar);
            painter.setPen(Qt::NoPen);
            painter.setBrush(job.meter->exceeded ? t.colors.statusErrorSoft : t.colors.statusSuccessSoft);
            painter.drawRoundedRect(bar, parts::kJobHeaderMeterRadius, parts::kJobHeaderMeterRadius);
            const double share = std::isfinite(job.meter->share) ? std::clamp(job.meter->share, 0.0, 1.0) : 0.0;
            if (share > 0.0)
            {
                painter.setBrush(job.meter->exceeded ? t.colors.statusError : t.colors.statusSuccess);
                painter.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * share, bar.height()),
                                        parts::kJobHeaderMeterRadius, parts::kJobHeaderMeterRadius);
            }
        }
        if (impl.separatorX >= 0)
        {
            painter.fillRect(
                QRect(impl.separatorX, (height() - parts::kJobHeaderSeparator) / 2, 1, parts::kJobHeaderSeparator),
                t.colors.borderSubtle);
        }
        const QRect runBar = runBarRect();
        if (!runBar.isEmpty())
        {
            painter.fillRect(runBar.intersected(rect()), t.colors.accentPrimary);
        }
        return;
    }
    const int gap = t.metrics.gap;
    int x = parts::kTaskHeaderPaddingStart;
    const int middle = height() / 2;

    // The target's thumbnail: its picture; with none - a task with no target image, or one whose thumbnail is not made
    // yet - the process's glyph on a tile, as the rail draws such a task.
    const QRectF thumb(x, middle - parts::kTaskHeaderThumbnail / 2.0, parts::kTaskHeaderThumbnail,
                       parts::kTaskHeaderThumbnail);
    QPainterPath frame;
    frame.addRoundedRect(thumb, parts::kTaskHeaderThumbnailRadius, parts::kTaskHeaderThumbnailRadius);
    painter.save();
    painter.setClipPath(frame);
    if (m_impl->picture.isNull())
    {
        painter.fillPath(frame, t.layers.surface3);
        const qreal glyph = parts::kTaskHeaderTileGlyph;
        const QPointF centre = thumb.center();
        parts::paintIcon(painter, m_impl->glyph,
                         QRectF(centre.x() - glyph / 2.0, centre.y() - glyph / 2.0, glyph, glyph),
                         t.colors.textPrimary);
    }
    else
    {
        painter.fillPath(frame, t.colors.bgWell);
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
