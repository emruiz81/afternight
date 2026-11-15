// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnHistoryStrip.h"

#include "components/AnActionButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnimationUtils.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAction>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <vector>

W_OBJECT_IMPL(an::ui::AnHistoryStrip)

namespace an::ui
{

namespace
{

[[nodiscard]] QFont chipFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall, QFont::DemiBold);
}

[[nodiscard]] QFont chipNoteFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontCaption);
}

[[nodiscard]] QFont labelFont()
{
    return parts::capsFont(tokens().typography.body, tokens().metrics.fontCaption);
}

[[nodiscard]] QString labelText()
{
    return QObject::tr("History");
}

/// A chip of the strip.
class HistoryChip final : public QAbstractButton
{
public:
    enum class Kind
    {
        Step,
        Current,
        Inherited,
        Pending,
        Overflow
    };

    HistoryChip(QWidget* parent, const Kind kind) : QAbstractButton(parent), m_kind(kind)
    {
        setFocusPolicy(kind == Kind::Inherited ? Qt::NoFocus : Qt::TabFocus);
        setAttribute(Qt::WA_Hover, kind == Kind::Step || kind == Kind::Overflow);
        if (kind == Kind::Step || kind == Kind::Overflow)
        {
            setCursor(Qt::PointingHandCursor);
        }
    }

    [[nodiscard]] Kind kind() const noexcept
    {
        return m_kind;
    }

    /// The step's index in the steps, for a step's chip.
    std::size_t step = 0u;

    /// The small text after a pending chip's name: "pending", "writing".
    void setNote(const QString& note)
    {
        m_note = note;
        updateGeometry();
    }

    void setWriting(const bool writing)
    {
        m_writing = writing;
        if (writing && AnimationUtils::animationsEnabled())
        {
            if (m_spinner == nullptr)
            {
                m_spinner = new QTimer(this);
                m_spinner->setInterval(parts::kHistorySpinnerFrameMs);
                QObject::connect(m_spinner, &QTimer::timeout, this, [this]() {
                    m_angle = (m_angle + 360 * parts::kHistorySpinnerFrameMs / parts::kHistorySpinnerTurnMs) % 360;
                    update();
                });
            }
            m_spinner->start();
        }
        else if (m_spinner != nullptr)
        {
            m_spinner->stop();
        }
        update();
    }

    [[nodiscard]] bool hasIcon() const noexcept
    {
        return m_kind == Kind::Current || m_kind == Kind::Pending;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        int width = 2 * parts::kHistoryChipPadding + parts::textWidth(chipFont(), text());
        if (hasIcon())
        {
            width += parts::kHistoryChipIcon + parts::kHistoryChipGap - parts::kHistoryChipIconPull;
        }
        if (!m_note.isEmpty())
        {
            width += parts::kHistoryChipGap + parts::textWidth(chipNoteFont(), m_note);
        }
        return QSize(width, tokens().metrics.chip);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = bounds.height() / 2.0;

        QColor textColour = t.colors.textPrimary;
        QColor iconColour = t.colors.textPrimary;
        switch (m_kind)
        {
        case Kind::Current:
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.colors.accentContainer);
            painter.drawRoundedRect(QRectF(rect()), radius + 0.5, radius + 0.5);
            textColour = t.colors.textOnAccentContainer;
            iconColour = textColour;
            break;
        case Kind::Pending: {
            QPen dashed(t.colors.textAccent, 1.0);
            dashed.setDashPattern({parts::kHistoryPendingDash, parts::kHistoryPendingDash});
            painter.setPen(dashed);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds, radius, radius);
            iconColour = t.colors.textAccent;
            break;
        }
        case Kind::Inherited:
            painter.setPen(QPen(t.colors.borderSubtle, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds, radius, radius);
            textColour = t.colors.textTertiary;
            break;
        case Kind::Step:
        case Kind::Overflow:
            painter.setPen(QPen(t.colors.borderPrimary, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(bounds, radius, radius);
            break;
        }
        if (m_kind == Kind::Step || m_kind == Kind::Overflow)
        {
            parts::paintStateLayer(painter, QRectF(rect()), radius, underMouse() && isEnabled(), isDown());
        }
        if (hasFocus() && m_kind != Kind::Inherited)
        {
            parts::paintFocusRing(painter, QRectF(rect()), radius);
        }

        qreal x = parts::kHistoryChipPadding;
        const qreal centre = height() / 2.0;
        if (hasIcon())
        {
            const qreal side = parts::kHistoryChipIcon;
            x -= parts::kHistoryChipIconPull;
            const QRectF square(x, centre - side / 2.0, side, side);
            if (m_kind == Kind::Current)
            {
                parts::paintCheckMark(painter, square, iconColour, parts::kGlyphStroke);
            }
            else if (m_writing)
            {
                // A spinner: three quarters of a ring, turning while animations are on.
                painter.setPen(QPen(iconColour, parts::kGlyphStroke, Qt::SolidLine, Qt::RoundCap));
                painter.setBrush(Qt::NoBrush);
                const QRectF ring = square.adjusted(1.5, 1.5, -1.5, -1.5);
                painter.drawArc(ring, -m_angle * 16, 270 * 16);
            }
            else
            {
                parts::paintIcon(painter, QIcon(QStringLiteral(":/AfterNight/icons/SVG/eye.svg")), square, iconColour);
            }
            x += side + parts::kHistoryChipGap;
        }

        const QFont font = chipFont();
        const qreal noteWidth =
            m_note.isEmpty() ? 0.0 : parts::kHistoryChipGap + parts::textWidth(chipNoteFont(), m_note);
        const qreal room = (std::max)(0.0, width() - parts::kHistoryChipPadding - noteWidth - x);
        painter.setFont(font);
        painter.setPen(textColour);
        painter.drawText(QRectF(x, 0.0, room, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(font).elidedText(text(), Qt::ElideRight, room));
        if (!m_note.isEmpty())
        {
            painter.setFont(chipNoteFont());
            painter.setPen(t.colors.textSecondary);
            painter.drawText(QRectF(x + room + parts::kHistoryChipGap, 0.0, noteWidth, height()),
                             Qt::AlignLeft | Qt::AlignVCenter, m_note);
        }
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
    Kind m_kind;
    QString m_note;
    bool m_writing = false;
    QTimer* m_spinner = nullptr;
    int m_angle = 0;
};

} // namespace

struct AnHistoryStrip::Impl
{
    AnHistoryStrip* q = nullptr;
    ViewHistorySteps steps;
    std::optional<HistoryPendingStep> pending;
    QPointer<QAction> historyAction;

    std::vector<HistoryChip*> stepChips;
    HistoryChip* overflow = nullptr;
    HistoryChip* pendingChip = nullptr;
    AnActionButton* undo = nullptr;
    AnActionButton* redo = nullptr;
    AnActionButton* history = nullptr;

    /// Where the chevrons go: their squares, in the strip's coordinates.
    std::vector<QRectF> chevrons;

    [[nodiscard]] int labelWidth() const
    {
        return parts::textWidth(labelFont(), labelText());
    }

    /// The x where the chips start, and where they must end.
    [[nodiscard]] int chipsStart() const
    {
        return parts::kHistoryStripPaddingStart + labelWidth() + parts::kHistoryStripLabelGap + parts::kHistoryStripGap;
    }

    [[nodiscard]] int buttonsWidth() const
    {
        int width = 0;
        for (const AnActionButton* button : {undo, redo, history})
        {
            if (button->isVisibleTo(q))
            {
                width += button->sizeHint().width();
            }
        }
        return width;
    }

    [[nodiscard]] int chipsEnd() const
    {
        return q->width() - parts::kHistoryStripPaddingEnd - buttonsWidth() - parts::kHistoryStripGap;
    }

    /// The room a chip takes after the one before it: the gap, the chevron, the gap.
    [[nodiscard]] static int joint()
    {
        return parts::kHistoryStripGap + parts::kHistoryChevron + parts::kHistoryStripGap;
    }

    void rebuild()
    {
        for (HistoryChip* chip : stepChips)
        {
            delete chip;
        }
        stepChips.clear();
        for (std::size_t index = 0; index < steps.steps.size(); ++index)
        {
            const ViewHistoryStep& step = steps.steps[index];
            HistoryChip::Kind kind = HistoryChip::Kind::Step;
            if (step.inherited)
            {
                kind = HistoryChip::Kind::Inherited;
            }
            else if (index == steps.current)
            {
                kind = HistoryChip::Kind::Current;
            }
            auto* chip = new HistoryChip(q, kind);
            chip->step = index;
            chip->setText(QString::fromStdString(step.name));
            chip->setAccessibleName(chip->text());
            const QString description = QString::fromStdString(step.description);
            if (step.inherited)
            {
                chip->setToolTip(QObject::tr("%1: inherited from the image this one was made from").arg(chip->text()));
            }
            else if (kind == HistoryChip::Kind::Current)
            {
                chip->setToolTip(QObject::tr("%1: the image shows this step").arg(chip->text()));
            }
            else
            {
                chip->setToolTip(description.isEmpty() ? chip->text() : description);
            }
            if (kind == HistoryChip::Kind::Step)
            {
                QObject::connect(chip, &QAbstractButton::clicked, q,
                                 [this, index]() { emit q->stepClicked(static_cast<int>(index)); });
            }
            stepChips.push_back(chip);
        }

        if (pending.has_value())
        {
            pendingChip->setText(pending->processName);
            pendingChip->setAccessibleName(pending->processName);
            pendingChip->setNote(pending->writing ? QObject::tr("writing") : QObject::tr("pending"));
            pendingChip->setToolTip(pending->writing ? QObject::tr("The run is writing this step")
                                                     : QObject::tr("The open task: not in the history until Apply"));
            pendingChip->setWriting(pending->writing);
        }
        else
        {
            pendingChip->setWriting(false);
        }
        relayout();
    }

    void relayout()
    {
        chevrons.clear();
        const int stripHeight = q->height();
        const int start = chipsStart();
        const int end = chipsEnd();

        // The buttons at the right.
        int right = q->width() - parts::kHistoryStripPaddingEnd;
        for (AnActionButton* button : {history, redo, undo})
        {
            if (!button->isVisibleTo(q))
            {
                continue;
            }
            const QSize size = button->sizeHint();
            right -= size.width();
            button->setGeometry(right, (stripHeight - size.height()) / 2, size.width(), size.height());
        }

        // The steps that fit: the newest, with the oldest giving way to the overflow chip.
        const auto widthOf = [](const HistoryChip* chip) { return chip->sizeHint().width(); };
        const int pendingWidth = pending.has_value() ? joint() + widthOf(pendingChip) : 0;
        std::size_t first = 0u;
        const auto needed = [&](const std::size_t from) {
            int width = pendingWidth;
            for (std::size_t index = from; index < stepChips.size(); ++index)
            {
                width += (index > from ? joint() : 0) + widthOf(stepChips[index]);
            }
            if (from > 0u)
            {
                width += widthOf(overflow) + (from < stepChips.size() ? joint() : 0);
            }
            return width;
        };
        while (first + 1u < stepChips.size() && needed(first) > end - start)
        {
            ++first;
        }

        // A chip that still finds too little room is narrowed, its name elided, and one with less room than a chip's
        // padding and icon is not shown, unless it is no wider than that whole (the overflow chip's three dots are, in
        // a narrow font); the steps leave room for the pending chip, which comes last.
        const int least = 2 * parts::kHistoryChipPadding + parts::kHistoryChipIcon;
        const int reserve =
            pending.has_value() ? joint() + (std::min)(widthOf(pendingChip), (std::max)(0, (end - start) / 2)) : 0;
        int x = start;
        const auto place = [&](HistoryChip* chip, const bool joined, const int kept) {
            const int jointWidth = joined ? joint() : 0;
            const int wanted = chip->sizeHint().width();
            const int width = (std::min)(wanted, end - kept - x - jointWidth);
            if (width < (std::min)(least, wanted))
            {
                chip->hide();
                return false;
            }
            if (joined)
            {
                const qreal chevron = parts::kHistoryChevron;
                chevrons.emplace_back(x + parts::kHistoryStripGap, (stripHeight - chevron) / 2.0, chevron, chevron);
                x += jointWidth;
            }
            const int chipHeight = chip->sizeHint().height();
            chip->setGeometry(x, (stripHeight - chipHeight) / 2, width, chipHeight);
            chip->show();
            x += width;
            return true;
        };
        bool joined = false;
        overflow->setVisible(first > 0u);
        if (first > 0u)
        {
            joined = place(overflow, false, reserve);
        }
        for (std::size_t index = 0; index < stepChips.size(); ++index)
        {
            if (index < first)
            {
                stepChips[index]->hide();
                continue;
            }
            joined = place(stepChips[index], joined, reserve) || joined;
        }
        if (pending.has_value())
        {
            place(pendingChip, joined, 0);
        }
        else
        {
            pendingChip->hide();
        }
        q->update();
    }
};

AnHistoryStrip::AnHistoryStrip(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.q = this;
    setObjectName(QStringLiteral("anHistoryStrip"));
    setAccessibleName(labelText());

    d.overflow = new HistoryChip(this, HistoryChip::Kind::Overflow);
    d.overflow->setText(QStringLiteral("..."));
    d.overflow->setAccessibleName(tr("Earlier steps"));
    d.overflow->setToolTip(tr("Earlier steps: open the Image History window"));
    d.overflow->hide();
    QObject::connect(d.overflow, &QAbstractButton::clicked, this, [this]() {
        if (m_impl->historyAction)
        {
            m_impl->historyAction->trigger();
        }
    });
    d.pendingChip = new HistoryChip(this, HistoryChip::Kind::Pending);
    d.pendingChip->hide();

    const auto button = [this](const QString& objectName) {
        auto* made = new AnActionButton(nullptr, this);
        made->setSides(0, parts::kHistoryButtonIcon);
        made->setObjectName(objectName);
        return made;
    };
    d.undo = button(QStringLiteral("anHistoryStripUndo"));
    d.redo = button(QStringLiteral("anHistoryStripRedo"));
    d.history = button(QStringLiteral("anHistoryStripHistory"));
    for (AnActionButton* made : {d.undo, d.redo, d.history})
    {
        made->installEventFilter(this);
    }
}

AnHistoryStrip::~AnHistoryStrip() = default;

void AnHistoryStrip::setSteps(const ViewHistorySteps& steps)
{
    if (steps == m_impl->steps && m_impl->stepChips.size() == steps.steps.size())
    {
        return;
    }
    m_impl->steps = steps;
    m_impl->rebuild();
}

const ViewHistorySteps& AnHistoryStrip::steps() const noexcept
{
    return m_impl->steps;
}

void AnHistoryStrip::setPendingStep(std::optional<HistoryPendingStep> pending)
{
    if (pending == m_impl->pending)
    {
        return;
    }
    m_impl->pending = std::move(pending);
    m_impl->rebuild();
}

const std::optional<HistoryPendingStep>& AnHistoryStrip::pendingStep() const noexcept
{
    return m_impl->pending;
}

void AnHistoryStrip::setActions(QAction* undo, QAction* redo, QAction* history)
{
    m_impl->undo->setAction(undo);
    m_impl->redo->setAction(redo);
    m_impl->history->setAction(history);
    m_impl->historyAction = history;
    m_impl->relayout();
}

QStringList AnHistoryStrip::shownChips() const
{
    std::vector<const QAbstractButton*> shown;
    for (const HistoryChip* chip : m_impl->stepChips)
    {
        if (chip->isVisibleTo(this))
        {
            shown.push_back(chip);
        }
    }
    if (m_impl->overflow->isVisibleTo(this))
    {
        shown.insert(shown.begin(), m_impl->overflow);
    }
    if (m_impl->pendingChip->isVisibleTo(this))
    {
        shown.push_back(m_impl->pendingChip);
    }
    QStringList names;
    for (const QAbstractButton* chip : shown)
    {
        names.append(chip->text());
    }
    return names;
}

QAbstractButton* AnHistoryStrip::chipForStep(const std::size_t index) const
{
    if (index >= m_impl->stepChips.size())
    {
        return nullptr;
    }
    HistoryChip* chip = m_impl->stepChips[index];
    return chip->isVisibleTo(this) ? chip : nullptr;
}

QAbstractButton* AnHistoryStrip::overflowChip() const
{
    return m_impl->overflow->isVisibleTo(this) ? m_impl->overflow : nullptr;
}

QAbstractButton* AnHistoryStrip::pendingChip() const
{
    return m_impl->pending.has_value() ? m_impl->pendingChip : nullptr;
}

QAbstractButton* AnHistoryStrip::undoButton() const
{
    return m_impl->undo;
}

QAbstractButton* AnHistoryStrip::redoButton() const
{
    return m_impl->redo;
}

QAbstractButton* AnHistoryStrip::historyButton() const
{
    return m_impl->history;
}

QSize AnHistoryStrip::sizeHint() const
{
    int width = m_impl->chipsStart();
    bool joined = false;
    for (const HistoryChip* chip : m_impl->stepChips)
    {
        width += (joined ? Impl::joint() : 0) + chip->sizeHint().width();
        joined = true;
    }
    if (m_impl->pending.has_value())
    {
        width += (joined ? Impl::joint() : 0) + m_impl->pendingChip->sizeHint().width();
    }
    width += parts::kHistoryStripGap + m_impl->buttonsWidth() + parts::kHistoryStripPaddingEnd;
    return QSize(width, tokens().metrics.strip);
}

QSize AnHistoryStrip::minimumSizeHint() const
{
    return QSize(m_impl->chipsStart() + m_impl->buttonsWidth() + parts::kHistoryStripPaddingEnd,
                 tokens().metrics.strip);
}

bool AnHistoryStrip::event(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::StyleChange:
    case QEvent::FontChange:
        updateGeometry();
        m_impl->relayout();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

bool AnHistoryStrip::eventFilter(QObject* watched, QEvent* event)
{
    // A button shown or hidden as its action is moves the room the chips have.
    if ((event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) && watched->parent() == this)
    {
        m_impl->relayout();
    }
    return QWidget::eventFilter(watched, event);
}

void AnHistoryStrip::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_impl->relayout();
}

void AnHistoryStrip::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.fillRect(rect(), t.layers.surface1);
    painter.fillRect(QRect(0, 0, width(), 1), t.colors.borderSubtle);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(labelFont());
    painter.setPen(t.colors.textTertiary);
    painter.drawText(QRectF(parts::kHistoryStripPaddingStart, 0.0, m_impl->labelWidth(), height()),
                     Qt::AlignLeft | Qt::AlignVCenter, labelText());
    for (const QRectF& chevron : m_impl->chevrons)
    {
        parts::paintChevronRight(painter, chevron, t.colors.textTertiary, parts::kGlyphStroke);
    }
}

} // namespace an::ui
