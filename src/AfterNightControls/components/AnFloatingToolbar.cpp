// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "components/AnFloatingToolbar.h"

#include "components/AnActionButton.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QResizeEvent>

#include <verdigris/wobjectimpl.h>

#include <algorithm>
#include <array>
#include <functional>
#include <vector>

W_OBJECT_IMPL(an::ui::AnFloatingToolbar)

namespace an::ui
{

namespace
{

[[nodiscard]] QFont toolbarTextFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall);
}

[[nodiscard]] QFont toolbarNameFont()
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.body, t.metrics.fontSmall, QFont::DemiBold);
}

[[nodiscard]] QFont toolbarZoomFont(const AnFloatingToolbar::Form form)
{
    const DesignTokens& t = tokens();
    return parts::sizedFont(t.typography.monospace,
                            form == AnFloatingToolbar::Form::Small ? t.metrics.fontCaption : t.metrics.fontSmall);
}

/// Whether the display mode a combo shows is its stretched one: the last of its modes, as "Autostretch" follows
/// "Linear".
[[nodiscard]] bool showsStretched(const QComboBox* combo)
{
    return combo != nullptr && combo->count() > 1 && combo->currentIndex() == combo->count() - 1;
}

/// A part over the scrim that paints itself: hover and keyboard focus kept for its painting.
class ScrimButton : public QAbstractButton
{
public:
    explicit ScrimButton(QWidget* parent) : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void paintState(QPainter& painter, const QRectF& bounds, const qreal radius) const
    {
        const DesignTokens& t = tokens();
        if ((m_hovered && isEnabled()) || isDown())
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(
                parts::atShare(t.colors.textOnImage, isDown() ? parts::kPressedLayer : parts::kHoverLayer));
            painter.drawRoundedRect(bounds, radius, radius);
        }
        if (m_keyboardFocus && isEnabled())
        {
            parts::paintFocusRing(painter, bounds, radius);
        }
    }

    void focusInEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = parts::isKeyboardFocusReason(event->reason());
        QAbstractButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        m_keyboardFocus = false;
        QAbstractButton::focusOutEvent(event);
        update();
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        QAbstractButton::leaveEvent(event);
        update();
    }

    bool m_hovered = false;
    bool m_keyboardFocus = false;
};

/// Follows a combo of the view: its current item, its items and whether it is enabled. Tells `changed` of each.
class ComboMirror final : public QObject
{
public:
    ComboMirror(QObject* owner, std::function<void()> changed) : QObject(owner), m_changed(std::move(changed)) {}

    void setCombo(QComboBox* combo)
    {
        if (m_combo == combo)
        {
            return;
        }
        for (const QMetaObject::Connection& connection : m_connections)
        {
            QObject::disconnect(connection);
        }
        m_connections.clear();
        if (m_combo)
        {
            m_combo->removeEventFilter(this);
        }
        m_combo = combo;
        if (combo != nullptr)
        {
            combo->installEventFilter(this);
            const auto tell = [this]() { m_changed(); };
            m_connections.push_back(
                QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, tell));
            m_connections.push_back(QObject::connect(combo, &QObject::destroyed, this, tell));
            if (QAbstractItemModel* model = combo->model())
            {
                m_connections.push_back(QObject::connect(model, &QAbstractItemModel::rowsInserted, this, tell));
                m_connections.push_back(QObject::connect(model, &QAbstractItemModel::rowsRemoved, this, tell));
                m_connections.push_back(QObject::connect(model, &QAbstractItemModel::modelReset, this, tell));
                m_connections.push_back(QObject::connect(model, &QAbstractItemModel::dataChanged, this, tell));
            }
        }
        m_changed();
    }

    [[nodiscard]] QComboBox* combo() const
    {
        return m_combo.data();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_combo && event->type() == QEvent::EnabledChange)
        {
            m_changed();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_changed;
    QPointer<QComboBox> m_combo;
    std::vector<QMetaObject::Connection> m_connections;
};

/// A combo's items as a menu that chooses among them, its current item checked; a separator where the combo has one.
void popUpChoices(QComboBox& combo, QWidget& at)
{
    QMenu menu(&at);
    for (int index = 0; index < combo.count(); ++index)
    {
        if (combo.itemData(index, Qt::AccessibleDescriptionRole).toString() == QStringLiteral("separator"))
        {
            menu.addSeparator();
            continue;
        }
        QAction* item = menu.addAction(combo.itemIcon(index), combo.itemText(index));
        item->setCheckable(true);
        item->setChecked(index == combo.currentIndex());
        item->setData(index);
    }
    // Over the bottom of an image the menu opens upwards, from the field's top.
    const QPoint origin = at.mapToGlobal(QPoint(0, 0));
    const QPoint above(origin.x(), origin.y() - menu.sizeHint().height());
    QPointer<QComboBox> guard(&combo);
    if (QAction* chosen = menu.exec(above); chosen != nullptr && guard)
    {
        guard->setCurrentIndex(chosen->data().toInt());
    }
}

/// The channel: the view's channel shown as a field, its items listed when clicked.
class ChoiceField final : public ScrimButton
{
public:
    explicit ChoiceField(QWidget* parent) : ScrimButton(parent)
    {
        QObject::connect(this, &QAbstractButton::clicked, this, [this]() {
            if (QComboBox* combo = m_combo.data(); combo != nullptr && combo->isEnabled())
            {
                popUpChoices(*combo, *this);
            }
        });
    }

    void follow(QComboBox* combo)
    {
        m_combo = combo;
        setText(combo != nullptr ? combo->currentText() : QString());
        const QString name = combo != nullptr ? combo->accessibleName() : QString();
        setAccessibleName(name);
        setToolTip(combo != nullptr ? combo->toolTip() : QString());
        setEnabled(combo != nullptr && combo->isEnabled());
        update();
    }

    void setHeight(const int height)
    {
        m_height = height;
        updateGeometry();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(parts::kFloatingToolbarChannelWidth, m_height);
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
        if (!isEnabled())
        {
            painter.setOpacity(parts::kDisabledOpacity);
        }
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = t.radius.sm;
        paintState(painter, bounds, radius);
        painter.setPen(QPen(parts::atShare(t.colors.textOnImage, parts::kOnImageOutlineShare), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, radius, radius);

        const qreal chevron = parts::kFloatingToolbarChevron;
        const qreal padding = parts::kFloatingToolbarChoicePadding;
        const QRectF chevronSquare(width() - padding - chevron, (height() - chevron) / 2.0, chevron, chevron);
        parts::paintIcon(painter, QIcon(QStringLiteral(":/AfterNight/icons/SVG/chevron_down.svg")), chevronSquare,
                         t.colors.textOnImageSecondary);

        const QFont font = toolbarTextFont();
        const QRectF textRoom(padding, 0.0, (std::max)(0.0, chevronSquare.left() - padding / 2.0 - padding), height());
        painter.setFont(font);
        painter.setPen(t.colors.textOnImage);
        painter.drawText(textRoom, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(font).elidedText(text(), Qt::ElideRight, textRoom.width()));
    }

private:
    QPointer<QComboBox> m_combo;
    int m_height = 28;
};

/// One segment of the display mode: chosen, the accent over images with its text; otherwise the secondary text.
class SegmentButton final : public ScrimButton
{
public:
    SegmentButton(QWidget* parent, const int index) : ScrimButton(parent), m_index(index)
    {
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
    }

    [[nodiscard]] int index() const noexcept
    {
        return m_index;
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(2 * parts::kFloatingToolbarChoicePadding + parts::textWidth(toolbarTextFont(), text()), height());
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds(rect());
        // A segment at an end keeps inside the outline's rounded corner.
        if (const QWidget* outline = parentWidget(); outline != nullptr)
        {
            const qreal radius = (std::max)(0.0, static_cast<qreal>(t.radius.sm) - 1.0);
            QPainterPath inside;
            inside.addRoundedRect(QRectF(outline->rect()).adjusted(1.0, 1.0, -1.0, -1.0), radius, radius);
            painter.setClipPath(inside.translated(-QPointF(pos())));
        }
        if (isChecked())
        {
            painter.fillRect(bounds, t.colors.accentOnImage);
        }
        else if ((m_hovered && isEnabled()) || isDown())
        {
            painter.fillRect(
                bounds, parts::atShare(t.colors.textOnImage, isDown() ? parts::kPressedLayer : parts::kHoverLayer));
        }
        painter.setFont(toolbarTextFont());
        painter.setPen(isChecked() ? t.colors.textOnAccentImage : t.colors.textOnImageSecondary);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    int m_index = 0;
};

/// The display mode as segments, one an item of the view's display mode, sharing one outline.
class ModeSegments final : public QWidget
{
public:
    explicit ModeSegments(QWidget* parent) : QWidget(parent) {}

    void follow(QComboBox* combo)
    {
        m_combo = combo;
        const int count = combo != nullptr ? combo->count() : 0;
        if (static_cast<int>(m_segments.size()) != count)
        {
            for (SegmentButton* segment : m_segments)
            {
                delete segment;
            }
            m_segments.clear();
            for (int index = 0; index < count; ++index)
            {
                auto* segment = new SegmentButton(this, index);
                QObject::connect(segment, &QAbstractButton::clicked, this, [this, index]() {
                    if (m_combo && m_combo->isEnabled())
                    {
                        m_combo->setCurrentIndex(index);
                    }
                    follow(m_combo.data());
                });
                segment->show();
                m_segments.push_back(segment);
            }
        }
        for (SegmentButton* segment : m_segments)
        {
            segment->setText(combo->itemText(segment->index()));
            segment->setAccessibleName(combo->itemText(segment->index()));
            segment->setToolTip(combo->itemData(segment->index(), Qt::ToolTipRole).toString());
            segment->setChecked(segment->index() == combo->currentIndex());
        }
        setAccessibleName(combo != nullptr ? combo->accessibleName() : QString());
        setEnabled(combo != nullptr && combo->isEnabled());
        layoutSegments();
        updateGeometry();
        update();
    }

    [[nodiscard]] SegmentButton* segment(const int index) const
    {
        return index >= 0 && index < static_cast<int>(m_segments.size()) ? m_segments[static_cast<std::size_t>(index)]
                                                                         : nullptr;
    }

    void setHeight(const int height)
    {
        m_height = height;
        updateGeometry();
        layoutSegments();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        int width = 0;
        for (const SegmentButton* segment : m_segments)
        {
            width += segment->sizeHint().width();
        }
        // A hairline between two segments, and the outline at each end.
        width += (std::max)(0, static_cast<int>(m_segments.size()) - 1) + 2;
        return QSize(width, m_height);
    }

    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutSegments();
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (!isEnabled())
        {
            painter.setOpacity(parts::kDisabledOpacity);
        }
        const QColor line = parts::atShare(t.colors.textOnImage, parts::kOnImageOutlineShare);
        const qreal radius = t.radius.sm;
        // The chosen segment's fill is the segment's own; the outer corners are cut to the outline's.
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.setPen(QPen(line, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shape);
        for (std::size_t index = 1; index < m_segments.size(); ++index)
        {
            const qreal x = m_segments[index]->geometry().left() - 0.5;
            painter.drawLine(QPointF(x, 1.0), QPointF(x, height() - 1.0));
        }
    }

private:
    void layoutSegments()
    {
        int x = 1;
        for (SegmentButton* segment : m_segments)
        {
            const int width = segment->sizeHint().width();
            segment->setGeometry(x, 1, width, (std::max)(0, height() - 2));
            x += width + 1;
        }
    }

    QPointer<QComboBox> m_combo;
    std::vector<SegmentButton*> m_segments;
    int m_height = 28;
};

/// The display mode in the small form: one toggle, on while the view shows its image stretched.
class ModeToggle final : public ScrimButton
{
public:
    explicit ModeToggle(QWidget* parent) : ScrimButton(parent)
    {
        setCheckable(true);
        setIcon(QIcon(QStringLiteral(":/AfterNight/icons/SVG/contrast.svg")));
        QObject::connect(this, &QAbstractButton::clicked, this, [this]() {
            if (QComboBox* combo = m_combo.data(); combo != nullptr && combo->isEnabled() && combo->count() > 1)
            {
                combo->setCurrentIndex(showsStretched(combo) ? 0 : combo->count() - 1);
            }
            follow(m_combo.data());
        });
    }

    void follow(QComboBox* combo)
    {
        m_combo = combo;
        setChecked(showsStretched(combo));
        const QString mode = combo != nullptr ? combo->currentText() : QString();
        setText(QObject::tr("Display mode: %1").arg(mode));
        setToolTip(text());
        setAccessibleName(text());
        setEnabled(combo != nullptr && combo->isEnabled());
        update();
    }

    void setSides(const int side, const int iconSide)
    {
        m_side = side;
        m_iconSide = iconSide;
        updateGeometry();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(m_side, m_side);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (!isEnabled())
        {
            painter.setOpacity(parts::kDisabledOpacity);
        }
        const QRectF bounds(rect());
        const qreal radius = parts::kIconButtonRadius;
        if (isChecked())
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(t.colors.accentOnImage);
            painter.drawRoundedRect(bounds, radius, radius);
        }
        paintState(painter, bounds, radius);
        const qreal side = m_iconSide;
        const QColor colour = isChecked() ? t.colors.textOnAccentImage
                                          : (m_hovered ? t.colors.textOnImage : t.colors.textOnImageSecondary);
        parts::paintIcon(painter, icon(), QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side), colour);
    }

private:
    QPointer<QComboBox> m_combo;
    int m_side = parts::kFloatingToolbarSmallButton;
    int m_iconSide = parts::kFloatingToolbarSmallIcon;
};

/// The More button: an icon over the scrim.
class MoreButton final : public ScrimButton
{
public:
    explicit MoreButton(QWidget* parent) : ScrimButton(parent)
    {
        setIcon(QIcon(QStringLiteral(":/AfterNight/icons/SVG/more_horiz.svg")));
        setText(QObject::tr("More"));
        setToolTip(QObject::tr("The image's menu"));
        setAccessibleName(text());
    }

    void setSides(const int side, const int iconSide)
    {
        m_side = side;
        m_iconSide = iconSide;
        updateGeometry();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(m_side, m_side);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds(rect());
        paintState(painter, bounds, parts::kIconButtonRadius);
        const qreal side = m_iconSide;
        parts::paintIcon(painter, icon(), QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side),
                         m_hovered ? t.colors.textOnImage : t.colors.textOnImageSecondary);
    }

private:
    int m_side = 30;
    int m_iconSide = parts::kFloatingToolbarIcon;
};

/// The shown view's name: a mark, then the full name elided in the middle.
class NamePart final : public QWidget
{
public:
    explicit NamePart(QWidget* parent) : QWidget(parent) {}

    void setName(const QString& name)
    {
        m_name = name;
        setToolTip(name);
        setAccessibleName(name);
        updateGeometry();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        const int needed = parts::kFloatingToolbarNamePaddingStart + parts::kFloatingToolbarNameMark +
                           parts::kFloatingToolbarNameGap + parts::textWidth(toolbarNameFont(), m_name) +
                           parts::kFloatingToolbarNamePaddingEnd;
        return QSize((std::min)(needed, parts::kFloatingToolbarNameMaximum), height());
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const DesignTokens& t = tokens();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal mark = parts::kFloatingToolbarNameMark;
        const qreal border = parts::kFloatingToolbarNameMarkBorder;
        const QRectF markSquare(parts::kFloatingToolbarNamePaddingStart + border / 2.0,
                                (height() - mark) / 2.0 + border / 2.0, mark - border, mark - border);
        painter.setPen(QPen(t.colors.accentOnImage, border));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(markSquare, 2.0, 2.0);

        const QFont font = toolbarNameFont();
        const qreal left = parts::kFloatingToolbarNamePaddingStart + mark + parts::kFloatingToolbarNameGap;
        const qreal room = (std::max)(0.0, width() - left - parts::kFloatingToolbarNamePaddingEnd);
        painter.setFont(font);
        painter.setPen(t.colors.textOnImage);
        painter.drawText(QRectF(left, 0.0, room, height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(font).elidedText(m_name, Qt::ElideMiddle, room));
    }

private:
    QString m_name;
};

/// The zoom in the monospace, centred in its width.
class ZoomPart final : public QWidget
{
public:
    explicit ZoomPart(QWidget* parent) : QWidget(parent) {}

    void setText(const QString& text)
    {
        m_text = text;
        setAccessibleName(QObject::tr("Zoom %1").arg(text));
        update();
    }

    void setForm(const AnFloatingToolbar::Form form)
    {
        m_form = form;
        updateGeometry();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(m_form == AnFloatingToolbar::Form::Small ? parts::kFloatingToolbarSmallZoomWidth
                                                              : parts::kFloatingToolbarZoomWidth,
                     height());
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setFont(toolbarZoomFont(m_form));
        painter.setPen(tokens().colors.textOnImage);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    AnFloatingToolbar::Form m_form = AnFloatingToolbar::Form::Full;
};

/// A separator: a 1 px line of the text over images at 22 %, with room at each side.
class SeparatorPart final : public QWidget
{
public:
    explicit SeparatorPart(QWidget* parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return QSize(1 + 2 * parts::kFloatingToolbarSeparatorMargin, parts::kFloatingToolbarSeparatorHeight);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        const int lineHeight = (std::min)(height(), parts::kFloatingToolbarSeparatorHeight);
        painter.fillRect(QRect(parts::kFloatingToolbarSeparatorMargin, (height() - lineHeight) / 2, 1, lineHeight),
                         parts::atShare(tokens().colors.textOnImage, parts::kOnImageSeparatorShare));
    }
};

} // namespace

struct AnFloatingToolbar::Impl
{
    /// A part of the toolbar, in its order, and the group it belongs to: a separator stands between two groups shown.
    struct Part
    {
        QWidget* widget = nullptr;
        QString name;
        int group = 0;
    };

    AnFloatingToolbar* q = nullptr;
    Form form = Form::Full;
    ViewChromeActions actions;
    QString viewName;
    int viewCount = 1;
    std::optional<double> zoom;
    std::optional<QRect> area;

    NamePart* name = nullptr;
    AnActionButton* fit = nullptr;
    AnActionButton* zoomOut = nullptr;
    ZoomPart* zoomPart = nullptr;
    AnActionButton* zoomIn = nullptr;
    AnActionButton* zoom100 = nullptr;
    AnActionButton* maximize = nullptr;
    ChoiceField* channel = nullptr;
    ModeSegments* segments = nullptr;
    ModeToggle* toggle = nullptr;
    AnActionButton* autostretchSettings = nullptr;
    AnActionButton* takeSnapshot = nullptr;
    AnActionButton* statisticsOverlay = nullptr;
    AnActionButton* showAstrometry = nullptr;
    MoreButton* more = nullptr;
    ComboMirror* channelMirror = nullptr;
    ComboMirror* modeMirror = nullptr;

    std::vector<Part> partsInOrder;
    std::vector<SeparatorPart*> separators;
    bool layingOut = false;

    [[nodiscard]] int height() const
    {
        return form == Form::Small ? parts::kFloatingToolbarSmallHeight : tokens().metrics.floatingToolbar;
    }

    [[nodiscard]] int padding() const
    {
        return form == Form::Small ? parts::kFloatingToolbarSmallPadding : parts::kFloatingToolbarPadding;
    }

    [[nodiscard]] int buttonSide() const
    {
        return form == Form::Small ? parts::kFloatingToolbarSmallButton : height() - parts::kFloatingToolbarButtonInset;
    }

    [[nodiscard]] int iconSide() const
    {
        return form == Form::Small ? parts::kFloatingToolbarSmallIcon : parts::kFloatingToolbarIcon;
    }

    [[nodiscard]] bool hasActions() const
    {
        return actions != ViewChromeActions{};
    }

    /// Which parts each form shows, before the actions say whether each is shown.
    [[nodiscard]] bool inForm(const QWidget* widget) const
    {
        if (form == Form::Full)
        {
            return widget != toggle;
        }
        return widget == fit || widget == zoomOut || widget == zoomPart || widget == zoomIn || widget == toggle ||
               widget == more;
    }

    /// Shows or hides each part as the form and the actions say; an action's button follows its action besides.
    void applyForm()
    {
        const int side = buttonSide();
        const int icon = iconSide();
        const int choice = height() - parts::kFloatingToolbarChoiceInset;
        for (AnActionButton* button : {fit, zoomOut, zoomIn, zoom100, maximize, autostretchSettings, takeSnapshot,
                                       statisticsOverlay, showAstrometry})
        {
            button->setSides(side, icon);
        }
        toggle->setSides(side, icon);
        more->setSides(side, icon);
        channel->setHeight(choice);
        segments->setHeight(choice);
        zoomPart->setForm(form);

        const auto bind = [this](AnActionButton* button, QAction* action) {
            button->setAction(inForm(button) ? action : nullptr);
        };
        bind(fit, actions.fit);
        bind(zoomOut, actions.zoomOut);
        bind(zoomIn, actions.zoomIn);
        bind(zoom100, actions.zoom100);
        bind(maximize, actions.maximize);
        bind(autostretchSettings, actions.autostretchSettings);
        bind(takeSnapshot, actions.takeSnapshot);
        bind(statisticsOverlay, actions.statisticsOverlay);
        bind(showAstrometry, actions.showAstrometry);

        name->setVisible(form == Form::Full && viewCount > 1 && !viewName.isEmpty() && hasActions());
        const bool zoomGroup = actions.fit != nullptr || actions.zoomIn != nullptr || actions.zoomOut != nullptr;
        zoomPart->setVisible(zoomGroup);
        channel->setVisible(inForm(channel) && channelMirror->combo() != nullptr);
        segments->setVisible(inForm(segments) && modeMirror->combo() != nullptr);
        toggle->setVisible(inForm(toggle) && modeMirror->combo() != nullptr);
        more->setVisible(hasActions());
        relayout();
    }

    /// The group a part is in: the small form keeps More beside the display mode, with no separator between them.
    [[nodiscard]] int groupOf(const Part& part) const
    {
        return form == Form::Small && part.widget == more ? 2 : part.group;
    }

    /// Lays the shown parts out from the left, a separator between two groups shown; its width is what they take.
    int layout(const bool place)
    {
        const int barHeight = height();
        int x = padding();
        int lastGroup = -1;
        std::size_t separatorUsed = 0u;
        bool first = true;
        for (const Part& part : partsInOrder)
        {
            if (!part.widget->isVisibleTo(q))
            {
                continue;
            }
            if (!first && groupOf(part) != lastGroup)
            {
                if (place)
                {
                    SeparatorPart* separator = separators[separatorUsed];
                    const QSize size = separator->sizeHint();
                    separator->setGeometry(x, (barHeight - size.height()) / 2, size.width(), size.height());
                    separator->show();
                }
                ++separatorUsed;
                x += separators.front()->sizeHint().width() + parts::kFloatingToolbarGap;
            }
            const QSize size = part.widget->sizeHint();
            if (place)
            {
                part.widget->setGeometry(x, (barHeight - size.height()) / 2, size.width(), size.height());
            }
            x += size.width() + parts::kFloatingToolbarGap;
            lastGroup = groupOf(part);
            first = false;
        }
        if (place)
        {
            for (std::size_t index = separatorUsed; index < separators.size(); ++index)
            {
                separators[index]->hide();
            }
        }
        if (!first)
        {
            x -= parts::kFloatingToolbarGap;
        }
        return x + padding();
    }

    void relayout()
    {
        if (layingOut)
        {
            return;
        }
        layingOut = true;
        layout(true);
        q->updateGeometry();
        if (area.has_value())
        {
            q->setGeometry(q->placementIn(*area));
        }
        q->update();
        layingOut = false;
    }

    [[nodiscard]] std::vector<Part> shownInOrder() const
    {
        std::vector<Part> shown;
        for (const Part& part : partsInOrder)
        {
            if (part.widget->isVisibleTo(q))
            {
                shown.push_back(part);
            }
        }
        return shown;
    }
};

AnFloatingToolbar::AnFloatingToolbar(QWidget* parent) : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.q = this;
    setObjectName(QStringLiteral("anFloatingToolbar"));
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);

    const auto actionButton = [this](const QString& objectName) {
        auto* button = new AnActionButton(nullptr, this);
        button->setTone(AnActionButton::Tone::OnImage);
        button->setObjectName(objectName);
        return button;
    };
    d.name = new NamePart(this);
    d.name->setObjectName(QStringLiteral("anFloatingToolbarName"));
    d.fit = actionButton(QStringLiteral("anFloatingToolbarFit"));
    d.zoomOut = actionButton(QStringLiteral("anFloatingToolbarZoomOut"));
    d.zoomPart = new ZoomPart(this);
    d.zoomPart->setObjectName(QStringLiteral("anFloatingToolbarZoom"));
    d.zoomIn = actionButton(QStringLiteral("anFloatingToolbarZoomIn"));
    d.zoom100 = actionButton(QStringLiteral("anFloatingToolbarZoom100"));
    d.maximize = actionButton(QStringLiteral("anFloatingToolbarMaximize"));
    d.channel = new ChoiceField(this);
    d.channel->setObjectName(QStringLiteral("anFloatingToolbarChannel"));
    d.segments = new ModeSegments(this);
    d.segments->setObjectName(QStringLiteral("anFloatingToolbarDisplayMode"));
    d.toggle = new ModeToggle(this);
    d.toggle->setObjectName(QStringLiteral("anFloatingToolbarDisplayModeToggle"));
    d.autostretchSettings = actionButton(QStringLiteral("anFloatingToolbarAutostretchSettings"));
    d.takeSnapshot = actionButton(QStringLiteral("anFloatingToolbarTakeSnapshot"));
    d.statisticsOverlay = actionButton(QStringLiteral("anFloatingToolbarStatisticsOverlay"));
    d.showAstrometry = actionButton(QStringLiteral("anFloatingToolbarShowAstrometry"));
    d.more = new MoreButton(this);
    d.more->setObjectName(QStringLiteral("anFloatingToolbarMore"));
    QObject::connect(d.more, &QAbstractButton::clicked, this, [this]() { emit moreRequested(); });

    d.channelMirror = new ComboMirror(this, [this]() {
        m_impl->channel->follow(m_impl->channelMirror->combo());
        m_impl->relayout();
    });
    d.modeMirror = new ComboMirror(this, [this]() {
        m_impl->segments->follow(m_impl->modeMirror->combo());
        m_impl->toggle->follow(m_impl->modeMirror->combo());
        m_impl->relayout();
    });

    d.partsInOrder = {
        {d.name, QStringLiteral("name"), 0},
        {d.fit, {}, 1},
        {d.zoomOut, {}, 1},
        {d.zoomPart, QStringLiteral("zoom"), 1},
        {d.zoomIn, {}, 1},
        {d.zoom100, {}, 1},
        {d.maximize, {}, 1},
        {d.channel, QStringLiteral("channel"), 2},
        {d.segments, QStringLiteral("display mode"), 2},
        {d.toggle, QStringLiteral("display mode"), 2},
        {d.autostretchSettings, {}, 2},
        {d.takeSnapshot, {}, 3},
        {d.statisticsOverlay, {}, 3},
        {d.showAstrometry, {}, 3},
        {d.more, QStringLiteral("more"), 4},
    };
    for (int index = 0; index < 4; ++index)
    {
        auto* separator = new SeparatorPart(this);
        separator->hide();
        d.separators.push_back(separator);
    }
    // A part shown or hidden - an action the view hides, a choice it withdraws - moves the parts after it.
    for (const Impl::Part& part : d.partsInOrder)
    {
        part.widget->installEventFilter(this);
    }
    d.applyForm();
}

AnFloatingToolbar::~AnFloatingToolbar() = default;

void AnFloatingToolbar::setForm(const Form form)
{
    if (m_impl->form == form)
    {
        return;
    }
    m_impl->form = form;
    m_impl->applyForm();
}

AnFloatingToolbar::Form AnFloatingToolbar::form() const noexcept
{
    return m_impl->form;
}

void AnFloatingToolbar::setActions(const ViewChromeActions& actions)
{
    if (actions == m_impl->actions)
    {
        return;
    }
    m_impl->actions = actions;
    m_impl->channelMirror->setCombo(actions.channel);
    m_impl->modeMirror->setCombo(actions.displayMode);
    m_impl->applyForm();
}

const ViewChromeActions& AnFloatingToolbar::actions() const noexcept
{
    return m_impl->actions;
}

void AnFloatingToolbar::setViewName(const QString& name)
{
    if (name == m_impl->viewName)
    {
        return;
    }
    m_impl->viewName = name;
    m_impl->name->setName(name);
    m_impl->applyForm();
}

void AnFloatingToolbar::setViewCount(const int count)
{
    if (count == m_impl->viewCount)
    {
        return;
    }
    m_impl->viewCount = count;
    m_impl->applyForm();
}

void AnFloatingToolbar::setZoom(const std::optional<double> percent)
{
    m_impl->zoom = percent;
    m_impl->zoomPart->setText(zoomText());
}

QString AnFloatingToolbar::zoomText() const
{
    if (!m_impl->zoom.has_value())
    {
        return {};
    }
    return QStringLiteral("%1 %").arg(*m_impl->zoom, 0, 'f', 2);
}

QStringList AnFloatingToolbar::shownParts() const
{
    QStringList names;
    int lastGroup = -1;
    for (const Impl::Part& part : m_impl->shownInOrder())
    {
        if (!names.isEmpty() && m_impl->groupOf(part) != lastGroup)
        {
            names.append(QStringLiteral("|"));
        }
        if (!part.name.isEmpty())
        {
            names.append(part.name);
        }
        else if (const auto* button = qobject_cast<const AnActionButton*>(part.widget); button != nullptr)
        {
            names.append(button->text());
        }
        lastGroup = m_impl->groupOf(part);
    }
    return names;
}

QAbstractButton* AnFloatingToolbar::buttonFor(const QAction* action) const
{
    if (action == nullptr)
    {
        return nullptr;
    }
    for (const Impl::Part& part : m_impl->partsInOrder)
    {
        if (auto* button = qobject_cast<AnActionButton*>(part.widget); button != nullptr && button->action() == action)
        {
            return button;
        }
    }
    return nullptr;
}

QAbstractButton* AnFloatingToolbar::channelField() const
{
    return m_impl->channel;
}

QWidget* AnFloatingToolbar::displayModeSegments() const
{
    return m_impl->segments;
}

QAbstractButton* AnFloatingToolbar::displayModeSegment(const int index) const
{
    return m_impl->segments->segment(index);
}

QAbstractButton* AnFloatingToolbar::displayModeToggle() const
{
    return m_impl->toggle;
}

QAbstractButton* AnFloatingToolbar::moreButton() const
{
    return m_impl->more;
}

int AnFloatingToolbar::barHeight() const
{
    return m_impl->height();
}

int AnFloatingToolbar::barRadius() const
{
    return m_impl->form == Form::Small ? parts::kFloatingToolbarSmallRadius : parts::kFloatingToolbarRadius;
}

QRect AnFloatingToolbar::placementIn(const QRect& area) const
{
    const QSize size = sizeHint();
    const int lift = m_impl->form == Form::Small ? parts::kFloatingToolbarSmallLift : parts::kFloatingToolbarLift;
    // The bar's bottom is the lift above the area's; the edge under it hangs a pixel below.
    const int barBottom = area.bottom() + 1 - lift;
    return QRect(area.left() + (area.width() - size.width()) / 2, barBottom - barHeight(), size.width(), size.height());
}

void AnFloatingToolbar::placeOver(const QRect& area)
{
    m_impl->area = area;
    setGeometry(placementIn(area));
    raise();
}

QSize AnFloatingToolbar::sizeHint() const
{
    return QSize(m_impl->layout(false), barHeight() + 1);
}

QSize AnFloatingToolbar::minimumSizeHint() const
{
    return sizeHint();
}

bool AnFloatingToolbar::event(QEvent* event)
{
    switch (event->type())
    {
    case QEvent::StyleChange:
    case QEvent::FontChange:
        // A density changes the bar's height and its buttons'.
        m_impl->applyForm();
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

bool AnFloatingToolbar::eventFilter(QObject* watched, QEvent* event)
{
    if ((event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) && watched->parent() == this)
    {
        m_impl->relayout();
    }
    return QWidget::eventFilter(watched, event);
}

void AnFloatingToolbar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_impl->layout(true);
}

void AnFloatingToolbar::paintEvent(QPaintEvent* /*event*/)
{
    const DesignTokens& t = tokens();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bar(0.0, 0.0, width(), barHeight());
    const qreal radius = barRadius();

    // The edge under the bar: its shape a pixel lower, less the bar, as a shadow is drawn only outside what casts it.
    QPainterPath below;
    below.addRoundedRect(bar.translated(0.0, 1.0), radius, radius);
    QPainterPath shape;
    shape.addRoundedRect(bar, radius, radius);
    painter.fillPath(below.subtracted(shape), t.colors.edge);
    painter.fillPath(shape, t.colors.scrim);
}

} // namespace an::ui
