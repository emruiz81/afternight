// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/AnProxyStyle.h"

#include "controls/IAppContext.h"

#include "components/AnLayoutUtils.h"
#include "components/AnTag.h"
#include "theme/AnDesignTokens.h"
#include "theme/ComponentParts.h"
#include "theme/Elevation.h"
#include "theme/NightImage.h"
#include "theme/ObservatoryDock.h"
#include "theme/PainterGuard.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>

#include <QPointer>

#include <cmath>
#include <algorithm>
#include <optional>
#include <vector>

namespace
{

using an::ui::PainterGuard;

/// Global raw pointer — Qt owns the style object via QApplication::setStyle().
an::ui::AnProxyStyle* g_installedStyle = nullptr;

double relativeLuminance(const QColor& color)
{
    const auto convert = [](const int channel) {
        const double s = static_cast<double>(channel) / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };

    const double r = convert(color.red());
    const double g = convert(color.green());
    const double b = convert(color.blue());

    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double l1 = relativeLuminance(a);
    const double l2 = relativeLuminance(b);
    const double light = (std::max)(l1, l2);
    const double dark = (std::min)(l1, l2);
    return (light + 0.05) / (dark + 0.05);
}

// ── Helper: property string comparison ─────────────────────────────────────

bool hasProperty(const QWidget* widget, const char* name, const char* value)
{
    if (!widget)
    {
        return false;
    }

    return widget->property(name).toString() == QLatin1String(value);
}

bool hasParentProperty(const QWidget* widget, const char* name, const char* value)
{
    if (!widget)
    {
        return false;
    }

    for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
    {
        if (parent->property(name).toString() == QLatin1String(value))
        {
            return true;
        }
    }

    return false;
}

int intPropertyOrDefault(const QWidget* widget, const char* name, const int defaultValue)
{
    if (!widget || !name)
    {
        return defaultValue;
    }

    bool ok = false;
    const int value = widget->property(name).toInt(&ok);
    if (!ok || value <= 0)
    {
        return defaultValue;
    }

    return value;
}

QString stripMnemonicMarkers(const QString& text)
{
    QString result;
    result.reserve(text.size());

    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (ch != QLatin1Char('&'))
        {
            result.append(ch);
            continue;
        }

        // Keep escaped ampersands (&& -> &) and drop mnemonic markers.
        if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&'))
        {
            result.append(QLatin1Char('&'));
            ++i;
        }
    }

    return result;
}

QIcon iconFromManager(const QString& iconName);

void applyDialogButtonRole(QDialogButtonBox* buttonBox, const QDialogButtonBox::StandardButton standardButton,
                           const char* role)
{
    if (!buttonBox || !role)
    {
        return;
    }

    if (auto* button = buttonBox->button(standardButton))
    {
        button->setProperty("anProcessButtonRole", QString::fromUtf8(role));
    }
}

void applyStandardDialogButtonRoles(QDialogButtonBox* buttonBox)
{
    if (!buttonBox)
    {
        return;
    }

    applyDialogButtonRole(buttonBox, QDialogButtonBox::Ok, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Open, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Save, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::SaveAll, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Yes, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::YesToAll, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Apply, "primary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Retry, "primary");

    applyDialogButtonRole(buttonBox, QDialogButtonBox::No, "secondary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::NoToAll, "secondary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Cancel, "secondary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Close, "secondary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Ignore, "secondary");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Help, "secondary");

    applyDialogButtonRole(buttonBox, QDialogButtonBox::Reset, "neutral");

    applyDialogButtonRole(buttonBox, QDialogButtonBox::Discard, "danger");
    applyDialogButtonRole(buttonBox, QDialogButtonBox::Abort, "danger");
}

void applyDisabledTextPalette(QWidget* widget, const an::ui::ColorTokens& colors)
{
    if (!widget)
    {
        return;
    }

    const bool textWidget = qobject_cast<QLabel*>(widget) || qobject_cast<QGroupBox*>(widget);
    const bool buttonWidget = qobject_cast<QAbstractButton*>(widget);
    if (!textWidget && !buttonWidget)
    {
        return;
    }

    QPalette palette = widget->palette();
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.textTertiary);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.textTertiary);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, colors.textTertiary);
    if (buttonWidget)
    {
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.textTertiary);
    }
    widget->setPalette(palette);
}

void applyDialogButtonIcon(QDialogButtonBox* buttonBox, const QDialogButtonBox::StandardButton standardButton,
                           const QString& iconName)
{
    if (!buttonBox)
    {
        return;
    }

    if (auto* button = buttonBox->button(standardButton))
    {
        if (const QIcon icon = iconFromManager(iconName); !icon.isNull())
        {
            button->setIcon(icon);
        }
    }
}

void applyDialogButtonCustomIcon(QDialogButtonBox* buttonBox, const QDialogButtonBox::StandardButton standardButton,
                                 const QIcon& icon)
{
    if (!buttonBox || icon.isNull())
    {
        return;
    }

    if (auto* button = buttonBox->button(standardButton))
    {
        button->setIcon(icon);
    }
}

QIcon buildTwoModeSvgIcon(const QString& svgPath, const QColor& normalColor, const QColor& disabledColor)
{
    if (svgPath.isEmpty())
    {
        return QIcon();
    }

    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            constexpr QSize iconSize(64, 64);
            QIcon icon;
            icon.addPixmap(iconProvider->loadSvgFileAsPixmap(svgPath, iconSize, normalColor), QIcon::Normal,
                           QIcon::Off);
            icon.addPixmap(iconProvider->loadSvgFileAsPixmap(svgPath, iconSize, disabledColor), QIcon::Disabled,
                           QIcon::Off);
            return icon;
        }
    }

    return QIcon();
}

void applyStandardDialogButtonIcons(QDialogButtonBox* buttonBox)
{
    if (!buttonBox)
    {
        return;
    }

    const auto& c = an::ui::tokens().colors;

    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Ok, QStringLiteral("check"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Apply, QStringLiteral("check"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Yes, QStringLiteral("check"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Open, QStringLiteral("file_open"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Save, QStringLiteral("file_save"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::SaveAll, QStringLiteral("file_save"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Close, QStringLiteral("close"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Cancel, QStringLiteral("close"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Reset, QStringLiteral("refresh"));
    applyDialogButtonIcon(buttonBox, QDialogButtonBox::Help, QStringLiteral("help"));

    // Danger-role buttons are rendered on a statusError background, not accentPrimary.
    // Compute optimal icon color against statusError independently.
    const double crWhite = contrastRatio(QColor(255, 255, 255), c.statusError);
    const double crBlack = contrastRatio(QColor(0, 0, 0), c.statusError);
    const QColor dangerIconColor = (crWhite >= 4.5 || crWhite > crBlack) ? QColor(255, 255, 255) : QColor(0, 0, 0);

    const QIcon discardIcon =
        buildTwoModeSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/delete.svg"), dangerIconColor, c.textTertiary);
    applyDialogButtonCustomIcon(buttonBox, QDialogButtonBox::Discard, discardIcon);

    const QIcon abortIcon =
        buildTwoModeSvgIcon(QStringLiteral(":/AfterNight/icons/SVG/close.svg"), dangerIconColor, c.textTertiary);
    applyDialogButtonCustomIcon(buttonBox, QDialogButtonBox::Abort, abortIcon);
}

// ── Helper: state flags ────────────────────────────────────────────────────

QIcon iconFromManager(const QString& iconName)
{
    if (iconName.isEmpty())
    {
        return QIcon();
    }

    if (auto* context = an::ui::appContext())
    {
        if (const auto iconProvider = context->getIconProvider())
        {
            return iconProvider->getIcon(iconName);
        }
    }

    return QIcon();
}

// ── The sizes ──────────────────────────────────────────────────────────────
//
// The style answers the sizes of the standard widgets from the metric tokens of the density in force: a control's
// height, a tab strip's, a row's, a menu item's, a scroll bar's thickness. Every other answer is the one the style's
// own code below gives, or the base style's.

/// A button's padding at each side of its text, and a control's border.
constexpr int kObservatoryButtonPadding = 14;
constexpr int kObservatoryBorder = 1;

/// The narrowest a spin box is: room for a decimal value.
constexpr int kMinimumSpinBoxWidth = 70;

/// The width of the combo box a size is measured on, to learn what its text field leaves around the text: wider than
/// any combo box's padding and arrow.
constexpr int kComboProbeWidth = 1000;

/// A slider's handle: the knob's halo fits it, along and across.
constexpr int kObservatorySliderHandle = 22;

/// A menu's panel line, and the room between it and the items.
constexpr int kObservatoryMenuPanel = 1;
constexpr int kObservatoryMenuMargin = 4;

/// A tooltip's text from its surface's edge, past its line.
constexpr int kObservatoryToolTipPadding = 5;

/// The shortest a scroll bar's handle is.
constexpr int kObservatoryScrollHandleMinimum = 24;

/// What a menu item keeps beside its text that the base style's width does not hold: its padding and gaps.
constexpr int kObservatoryMenuItemRoom = 8;

/// The pixel metrics answered from the tokens; nothing for the ones the style's own code below answers.
std::optional<int> observatoryPixelMetric(const QStyle::PixelMetric metric, const an::ui::MetricTokens& metrics)
{
    switch (metric)
    {
    case QStyle::PM_ScrollBarExtent:
        return metrics.scrollBar;
    case QStyle::PM_HeaderDefaultSectionSizeVertical:
        // A table's rows: the vertical header's sections are the rows.
        return metrics.rowTable;
    case QStyle::PM_SliderThickness:
    case QStyle::PM_SliderLength:
    case QStyle::PM_SliderControlThickness:
        return kObservatorySliderHandle;
    case QStyle::PM_MenuPanelWidth:
        return kObservatoryMenuPanel;
    case QStyle::PM_MenuHMargin:
    case QStyle::PM_MenuVMargin:
        return kObservatoryMenuMargin;
    case QStyle::PM_MenuBarPanelWidth:
        return 0;
    case QStyle::PM_ToolTipLabelFrameWidth:
        return kObservatoryToolTipPadding;
    case QStyle::PM_ScrollBarSliderMin:
        return kObservatoryScrollHandleMinimum;
    case QStyle::PM_TabCloseIndicatorWidth:
    case QStyle::PM_TabCloseIndicatorHeight:
        return an::ui::kObservatoryDockTabClose;
    default:
        return std::nullopt;
    }
}

/// How much wider a text is in another font than in the one a widget measured it in: the room a semibold button or
/// tab, a header in small capitals or a shortcut in the monospace needs past what the widget asked for.
int widerBy(const QString& text, const QFont& painted, const QFont& measured)
{
    if (text.isEmpty())
    {
        return 0;
    }
    QString plain = text;
    plain.remove(QLatin1Char('&'));
    return (std::max)(0,
                      QFontMetrics(painted).horizontalAdvance(plain) - QFontMetrics(measured).horizontalAdvance(plain));
}

/// The font a widget measured its contents in.
QFont measuredFont(const QWidget* widget)
{
    return widget != nullptr ? widget->font() : QApplication::font();
}

QFont semibold(QFont font)
{
    font.setWeight(QFont::DemiBold);
    return font;
}

/// Whether the style sizes a control of this type from its tokens.
bool observatorySizes(const QStyle::ContentsType type)
{
    switch (type)
    {
    case QStyle::CT_PushButton:
    case QStyle::CT_ToolButton:
    case QStyle::CT_LineEdit:
    case QStyle::CT_ComboBox:
    case QStyle::CT_SpinBox:
    case QStyle::CT_TabBarTab:
    case QStyle::CT_HeaderSection:
    case QStyle::CT_ItemViewItem:
    case QStyle::CT_MenuItem:
        return true;
    default:
        return false;
    }
}

bool verticalTab(const QStyleOption* option)
{
    const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
    if (tab == nullptr)
    {
        return false;
    }
    switch (tab->shape)
    {
    case QTabBar::RoundedWest:
    case QTabBar::RoundedEast:
    case QTabBar::TriangularWest:
    case QTabBar::TriangularEast:
        return true;
    default:
        return false;
    }
}

/// The quiet tag a menu's entry of a text shows after it: the tag of the menu's action of that text
/// (`parts::kMenuTagProperty`). A menu measures an entry before it is placed, so the action is found by its text.
QString menuTagOfText(const QStyleOptionMenuItem& item, const QWidget* widget)
{
    const auto* const menu = qobject_cast<const QMenu*>(widget);
    if (menu == nullptr)
    {
        return {};
    }
    const qsizetype tab = item.text.indexOf(QLatin1Char('\t'));
    const QString label = tab >= 0 ? item.text.left(tab) : item.text;
    for (const QAction* const action : menu->actions())
    {
        if (action != nullptr && !action->isSeparator() && action->text() == label)
        {
            const QString tag = action->property(an::ui::parts::kMenuTagProperty).toString();
            if (!tag.isEmpty())
            {
                return tag;
            }
        }
    }
    return {};
}

/// The size the style gives a control of a type observatorySizes() names, from its contents and from the
/// base style's answer for them. A control is never smaller than its contents need: a large icon or font grows it.
QSize observatorySizeFromContents(const QStyle::ContentsType type, const QStyleOption* option, const QSize& contents,
                                  const QSize& base, const an::ui::DesignTokens& tokens, const QWidget* widget)
{
    const an::ui::MetricTokens& m = tokens.metrics;
    const int inset = tokens.spacing.xs + kObservatoryBorder;
    switch (type)
    {
    case QStyle::CT_PushButton: {
        // A control high, its text padded at both sides; an icon button, with no text, is a square.
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        const bool hasText = button != nullptr && !button->text.isEmpty();
        const int height = (std::max)(m.control, contents.height() + 2 * inset);
        const int padding = hasText ? kObservatoryButtonPadding + kObservatoryBorder : inset;
        // The text is painted semibold; the button measured it at its own weight.
        const QFont font = measuredFont(widget);
        const int text = hasText ? widerBy(button->text, semibold(font), font) : 0;
        return QSize((std::max)(height, contents.width() + text + 2 * padding), height);
    }
    case QStyle::CT_ToolButton:
        // An icon button is a control square; text beside or under the icon widens or heightens it.
        return QSize((std::max)(m.control, contents.width() + 2 * inset),
                     (std::max)(m.control, contents.height() + 2 * inset));
    case QStyle::CT_LineEdit: {
        // A field is a control high. A line edit with no frame is a spin box's or a combo box's text, sized by them.
        const auto* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
        if (frame == nullptr || frame->lineWidth <= 0)
        {
            return base;
        }
        return QSize(base.width(), (std::max)(base.height(), m.control));
    }
    case QStyle::CT_ComboBox: {
        // A control high; in a strip that holds no more - a status line's - the extra-small control's height.
        const bool extraSmall =
            widget != nullptr && widget->property(an::ui::parts::kControlHeightProperty).toString() ==
                                     QLatin1String(an::ui::parts::kControlHeightExtraSmall);
        return QSize(base.width(), extraSmall ? m.controlXSmall : (std::max)(base.height(), m.control));
    }
    case QStyle::CT_SpinBox:
        return QSize((std::max)(base.width(), kMinimumSpinBoxWidth), (std::max)(base.height(), m.control));
    case QStyle::CT_TabBarTab: {
        // A tab fills its strip; a compact tab bar's tabs, inside a panel, are a small control high.
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        const bool compact = widget != nullptr && widget->property("anTabCompact").toBool();
        const int iconExtent = tab != nullptr ? (std::max)(tab->iconSize.width(), tab->iconSize.height()) : 0;
        const int across = (std::max)(compact ? m.controlSmall : m.strip, iconExtent + 2 * tokens.spacing.xs);
        const int minimumLength = intPropertyOrDefault(widget, "anTabMinWidth", 0);
        // A tab's text is painted semibold; the tab bar measured it at its own weight.
        const QFont font = measuredFont(widget);
        const int text = tab != nullptr ? widerBy(tab->text, semibold(font), font) : 0;
        if (verticalTab(option))
        {
            return QSize(across, (std::max)(base.height() + text, minimumLength));
        }
        return QSize((std::max)(base.width() + text, minimumLength), across);
    }
    case QStyle::CT_HeaderSection: {
        // A table's header: its label is painted in small capitals of the caption, which the header did not measure.
        const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
        QFont caps = tokens.typography.caption;
        caps.setWeight(QFont::DemiBold);
        caps.setCapitalization(QFont::AllUppercase);
        caps.setLetterSpacing(QFont::PercentageSpacing, 108.0);
        const int text = header != nullptr ? widerBy(header->text, caps, measuredFont(widget)) : 0;
        return QSize(base.width() + text, (std::max)(base.height(), m.rowTable));
    }
    case QStyle::CT_ItemViewItem:
        // A row of a table, a list or a tree.
        return QSize(base.width(), (std::max)(base.height(), m.rowTable));
    case QStyle::CT_MenuItem: {
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (item != nullptr && item->menuItemType == QStyleOptionMenuItem::Separator)
        {
            return base;
        }
        // A shortcut is painted in the monospace at the caption's size; the menu measured it in its own font.
        int shortcut = 0;
        if (item != nullptr)
        {
            const qsizetype tab = item->text.indexOf(QLatin1Char('\t'));
            if (tab >= 0)
            {
                QFont mono = tokens.typography.monospace;
                mono.setPointSizeF(m.fontCaption * 72.0 / 96.0);
                shortcut = widerBy(item->text.mid(tab + 1), mono, item->font);
            }
        }
        // A tag after the text widens the entry by itself and the room before it.
        const QString tag = item != nullptr ? menuTagOfText(*item, widget) : QString();
        const int tagged = tag.isEmpty() ? 0
                                         : an::ui::parts::kMenuTagGap +
                                               an::ui::AnTag::tagSize(tag, an::ui::AnTag::Tone::Outlined).width();
        return QSize(base.width() + shortcut + tagged + kObservatoryMenuItemRoom,
                     (std::max)(base.height(), m.rowDense));
    }
    default:
        return base;
    }
}

} // anonymous namespace

namespace an::ui
{

// ════════════════════════════════════════════════════════════════════════════
// Construction
// ════════════════════════════════════════════════════════════════════════════

AnProxyStyle::AnProxyStyle(QStyle* baseStyle)
    : QProxyStyle(baseStyle ? baseStyle : QStyleFactory::create(QStringLiteral("Fusion"))), m_tokens(&tokens())
{
}

AnProxyStyle::~AnProxyStyle() = default;

void AnProxyStyle::refreshThemeTokens()
{
    const DesignTokens* const previous = m_tokens;
    refreshTokens();
    m_tokens = &tokens();
    // The shadows were rendered in the colours of the theme that goes.
    dropElevationImages();
    // A density moves the sizes this style answers and the type scale.
    const bool sizesChanged =
        previous != nullptr && (previous->metrics != m_tokens->metrics || previous->typography != m_tokens->typography);

    // Update the application palette to reflect the new token colours.
    // Without this, widgets that rely on palette() (QLabel, QGroupBox title,
    // Fusion-drawn fallback elements) would keep the stale palette from the
    // previous theme.
    if (auto* app = qApp)
    {
        QPalette pal = app->palette();
        setupPalette(pal);
        app->setPalette(pal);
        if (sizesChanged)
        {
            app->setFont(m_tokens->typography.body);
        }

        // Force re-polish all widgets in the application
        for (auto* widget : app->allWidgets())
        {
            polish(widget);
            widget->update();
        }

        if (sizesChanged)
        {
            // Every widget is told its style changed, as Qt tells it when a style is set: it drops the size hint it
            // keeps and asks its layout to run again, so a shown window takes the new sizes. The new font tells only
            // the widgets that take the application's; a widget given a font of its own, and what it holds, hears of
            // the change this way alone. A window's own updateGeometry() would not do: it has no layout above it.
            std::vector<QPointer<QWidget>> widgets;
            for (QWidget* widget : QApplication::allWidgets())
            {
                widgets.emplace_back(widget);
            }
            for (const QPointer<QWidget>& widget : widgets)
            {
                if (!widget.isNull())
                {
                    QEvent styleChange(QEvent::StyleChange);
                    QCoreApplication::sendEvent(widget.data(), &styleChange);
                }
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Polish
// ════════════════════════════════════════════════════════════════════════════

void AnProxyStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    if (!widget)
    {
        return;
    }

    if (auto* buttonBox = qobject_cast<QDialogButtonBox*>(widget))
    {
        applyStandardDialogButtonRoles(buttonBox);
        applyStandardDialogButtonIcons(buttonBox);
    }

    if (auto* messageBox = qobject_cast<QMessageBox*>(widget))
    {
        if (messageBox->objectName().isEmpty())
        {
            messageBox->setObjectName(QStringLiteral("anMessageBox"));
        }
        messageBox->setProperty("anDialogVariant", QStringLiteral("messageBox"));
        messageBox->setAttribute(Qt::WA_StyledBackground, true);

        if (auto* buttonBox = messageBox->findChild<QDialogButtonBox*>())
        {
            applyStandardDialogButtonRoles(buttonBox);
            applyStandardDialogButtonIcons(buttonBox);
        }
    }

    // Enable hover tracking for interactive widgets
    if (qobject_cast<QPushButton*>(widget) || qobject_cast<QToolButton*>(widget) || qobject_cast<QMenuBar*>(widget) ||
        qobject_cast<QMenu*>(widget) || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) ||
        qobject_cast<QSlider*>(widget) || qobject_cast<QScrollBar*>(widget) || qobject_cast<QTabBar*>(widget) ||
        qobject_cast<QAbstractItemView*>(widget) || qobject_cast<QCheckBox*>(widget) ||
        qobject_cast<QRadioButton*>(widget) || qobject_cast<QLineEdit*>(widget) || qobject_cast<QSplitter*>(widget))
    {
        widget->setAttribute(Qt::WA_Hover, true);
    }

    // Set label color via palette
    if (auto* label = qobject_cast<QLabel*>(widget))
    {
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, m_tokens->colors.textPrimary);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, m_tokens->colors.textTertiary);
        label->setPalette(pal);

        // ── Process-shell header title ──────────────────────────────────
        const QString objName = label->objectName();
        if (objName == QLatin1String("anProcessShellHeaderTitle"))
        {
            // A process window's header is a window's heading.
            label->setFont(an::ui::parts::windowHeadingTitleFont(*m_tokens));
            // textPrimary already set above
        }
        // ── Process-shell header description ────────────────────────────
        else if (objName == QLatin1String("anProcessShellHeaderDescription"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.textSecondary);
            label->setPalette(pal);
            label->setFont(an::ui::parts::windowHeadingDescriptionFont(*m_tokens));
        }
        // ── Process-shell banner message ────────────────────────────────
        else if (objName == QLatin1String("anProcessShellBannerMessage"))
        {
            label->setFont(m_tokens->typography.body);
            // textPrimary already set above — nothing extra needed
        }
        // ── A window's heading: its eyebrow, its title, its description ──
        else if (objName == QLatin1String("anWindowHeadingEyebrow"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.textSecondary);
            label->setPalette(pal);
            label->setFont(an::ui::parts::windowHeadingEyebrowFont(*m_tokens));
        }
        else if (objName == QLatin1String("anWindowHeadingTitle"))
        {
            label->setFont(an::ui::parts::windowHeadingTitleFont(*m_tokens));
        }
        else if (objName == QLatin1String("anWindowHeadingDescription"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.textSecondary);
            label->setPalette(pal);
            label->setFont(an::ui::parts::windowHeadingDescriptionFont(*m_tokens));
        }
        // Handle anStatusTone property
        const QString tone = label->property("anStatusTone").toString();
        auto toneFont = label->font();
        toneFont.setWeight(QFont::DemiBold);
        if (tone == QLatin1String("success"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.statusSuccess);
            label->setPalette(pal);
            label->setFont(toneFont);
        }
        else if (tone == QLatin1String("warning"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.statusWarning);
            label->setPalette(pal);
            label->setFont(toneFont);
        }
        else if (tone == QLatin1String("error"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.statusError);
            label->setPalette(pal);
            label->setFont(toneFont);
        }
        else if (tone == QLatin1String("secondary"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.textSecondary);
            label->setPalette(pal);
        }
    }

    applyDisabledTextPalette(widget, m_tokens->colors);

    if (auto* header = qobject_cast<QHeaderView*>(widget))
    {
        header->setFont(m_tokens->typography.heading3);
    }

    // Set QToolTip palette and make the window translucent so
    // the rounded corners don't show black rectangular artifacts.
    if (widget->inherits("QTipLabel"))
    {
        QPalette pal = widget->palette();
        pal.setColor(QPalette::ToolTipBase, m_tokens->colors.bgCardPrimary);
        pal.setColor(QPalette::ToolTipText, m_tokens->colors.textPrimary);
        widget->setPalette(pal);
        widget->setAttribute(Qt::WA_TranslucentBackground, true);

        // Add margins to make room for the shadow
        widget->setContentsMargins(0, 0, 3, 3);
    }

    // ── Process-shell card (QFrame#anProcessShellCard) ──────────────────────
    // Promote NoFrame → StyledPanel so PE_Frame is invoked; the card
    // drawing is handled there (elevated surface + rounded border).
    if (auto* frame = qobject_cast<QFrame*>(widget))
    {
        if (frame->objectName() == QLatin1String("anProcessShellCard") && frame->frameShape() != QFrame::StyledPanel)
        {
            frame->setFrameShape(QFrame::StyledPanel);
        }
    }

    // ── Process-shell banner (QWidget#anProcessShellBanner) ─────────────────
    // Promote to StyledPanel via QFrame if possible; otherwise use
    // WA_StyledBackground so the palette Window colour fills the area.
    if (widget->objectName() == QLatin1String("anProcessShellBanner"))
    {
        if (auto* frame = qobject_cast<QFrame*>(widget))
        {
            if (frame->frameShape() != QFrame::StyledPanel)
                frame->setFrameShape(QFrame::StyledPanel);
        }
        else
        {
            widget->setAttribute(Qt::WA_StyledBackground, true);
            QPalette pal = widget->palette();
            pal.setColor(QPalette::Window, m_tokens->colors.bgCardPrimary);
            widget->setPalette(pal);
            widget->setAutoFillBackground(true);
        }
    }

    if (QLayout* rootLayout = widget->layout())
    {
        // A layout set to the density's gap or padding keeps it.
        const KeptSpacings kept{m_tokens->metrics.gap, m_tokens->metrics.pad};
        normalizeLayoutSpacingToTokens(rootLayout, m_tokens->spacing, kept);

        if (qobject_cast<QDialog*>(widget))
        {
            const bool keepMargins = rootLayout->property("anKeepLayoutMargins").toBool() ||
                                     rootLayout->property("anKeepPreviewLayoutMargins").toBool();
            if (!keepMargins)
            {
                const QMargins dialogMargins(m_tokens->spacing.xl, m_tokens->spacing.xl, m_tokens->spacing.xl,
                                             m_tokens->spacing.xl);
                if (rootLayout->contentsMargins() != dialogMargins)
                {
                    rootLayout->setContentsMargins(dialogMargins);
                }
            }
        }
    }

    polishObservatory(widget);
}

void AnProxyStyle::polish(QPalette& palette)
{
    setupPalette(palette);
}

void AnProxyStyle::polish(QApplication* app)
{
    QProxyStyle::polish(app);

    if (app)
    {
        QPalette pal = app->palette();
        setupPalette(pal);
        app->setPalette(pal);
        app->setFont(m_tokens->typography.body);
    }
}

void AnProxyStyle::unpolish(QWidget* widget)
{
    QProxyStyle::unpolish(widget);
}

void AnProxyStyle::setupPalette(QPalette& palette) const
{
    const auto& c = m_tokens->colors;
    const auto& layers = m_tokens->layers;
    const auto& materials = m_tokens->materials;

    palette.setColor(QPalette::Window, materials.micaSurface);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::Base, layers.surface1);
    palette.setColor(QPalette::AlternateBase, materials.acrylicSurface);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::BrightText, c.textAccent);
    palette.setColor(QPalette::Button, layers.surface2);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::Highlight, c.accentPrimary);
    palette.setColor(QPalette::HighlightedText, c.textOnAccent);
    palette.setColor(QPalette::ToolTipBase, materials.acrylicSurface);
    palette.setColor(QPalette::ToolTipText, c.textPrimary);
    palette.setColor(QPalette::PlaceholderText, c.textTertiary);
    palette.setColor(QPalette::Light, layers.surface4);
    palette.setColor(QPalette::Mid, c.borderSubtle);
    palette.setColor(QPalette::Dark, c.borderSubtle);
    palette.setColor(QPalette::Midlight, c.borderPrimary);
    palette.setColor(QPalette::Shadow, c.shadowOverlay);
    palette.setColor(QPalette::Link, c.accentPrimary);
    palette.setColor(QPalette::LinkVisited, c.textAccent);

    // Disabled colors
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textTertiary);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.textTertiary);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textTertiary);
    palette.setColor(QPalette::Disabled, QPalette::Base, materials.solidSurface);
    palette.setColor(QPalette::Disabled, QPalette::Button, materials.solidSurface);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, c.accentDisabled);
}

// ════════════════════════════════════════════════════════════════════════════
// pixelMetric / styleHint
// ════════════════════════════════════════════════════════════════════════════

int AnProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (const std::optional<int> answer = observatoryPixelMetric(metric, m_tokens->metrics))
    {
        return *answer;
    }

    switch (metric)
    {
    case PM_ButtonMargin:
        return m_tokens->spacing.sm;
    case PM_SpinBoxFrameWidth:
        return 2; // thin frame; horizontal text padding handled by SE_LineEditContents
    case PM_DefaultFrameWidth:
        return 1;
    case PM_TabBarTabHSpace:
        return m_tokens->spacing.lg * 2;
    case PM_TabBarTabVSpace:
        if (widget && widget->property("anTabCompact").toBool())
        {
            return m_tokens->spacing.sm + m_tokens->spacing.xs;
        }
        return m_tokens->spacing.sm * 2;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return m_tokens->spacing.sm;
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin:
        return m_tokens->spacing.md;
    case PM_ToolBarIconSize:
        return 20;
    case PM_ToolBarFrameWidth:
        return 0;
    case PM_ToolBarHandleExtent:
        return 0;
    case PM_SplitterWidth:
        if (hasProperty(widget, "anSplitterVariant", "process") ||
            hasParentProperty(widget, "anSplitterVariant", "process"))
        {
            return 8;
        }
        return 2;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 18; // 16px indicator + 1px pen margin each side
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int AnProxyStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                            QStyleHintReturn* returnData) const
{
    if (const std::optional<int> answer = observatoryStyleHint(hint, option, widget))
    {
        return *answer;
    }

    switch (hint)
    {
    case SH_ToolTip_WakeUpDelay:
        return 300;
    case SH_EtchDisabledText:
        return 0;
    case SH_ItemView_ShowDecorationSelected:
        return 1;
    case SH_ScrollBar_ContextMenu:
        return 0;
    case SH_ComboBox_Popup:
        if (hasProperty(widget, "anComboPopupAnchored", "true"))
        {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawPrimitive
// ════════════════════════════════════════════════════════════════════════════

void AnProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                                 const QWidget* widget) const
{
    if (drawObservatoryPrimitive(element, option, painter, widget))
    {
        return;
    }

    // A frame the painters do not know: a rounded card a QFrame asks for by its anFrameRadius property - its fill the
    // palette's Window, its border the palette's Mid, dashed when anFrameBorderDashed says so - or the base style's.
    if (element == PE_Frame && qobject_cast<const QFrame*>(widget) != nullptr)
    {
        const QVariant radiusVar = widget->property("anFrameRadius");
        if (radiusVar.isValid())
        {
            const int radius = radiusVar.toInt();
            const QColor bg = option->palette.color(QPalette::Window);
            const QColor borderColor = option->palette.color(QPalette::Mid);
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            const QRectF r = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
            QPainterPath path;
            path.addRoundedRect(r, radius, radius);
            painter->fillPath(path, bg);
            const Qt::PenStyle penStyle =
                widget->property("anFrameBorderDashed").toBool() ? Qt::DashLine : Qt::SolidLine;
            painter->setPen(QPen(borderColor, 1.0, penStyle));
            painter->drawPath(path);
            return;
        }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// drawControl
// ════════════════════════════════════════════════════════════════════════════

void AnProxyStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                               const QWidget* widget) const
{
    if (drawObservatoryControl(element, option, painter, widget))
    {
        return;
    }

    switch (element)
    {
    case CE_DockWidgetTitle: {
        if (const auto* dwOpt = qstyleoption_cast<const QStyleOptionDockWidget*>(option))
        {
            const PainterGuard guard(painter);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_tokens->layers.surface2);
            painter->drawRect(option->rect);
            // Bottom border
            painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
            painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
            // Title text
            painter->setPen(m_tokens->colors.textPrimary);
            const QRect textRect = option->rect.adjusted(8, 0, -8, 0);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, dwOpt->title);
        }
        return;
    }

    default:
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawComplexControl
// ════════════════════════════════════════════════════════════════════════════

void AnProxyStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter,
                                      const QWidget* widget) const
{
    if (drawObservatoryComplexControl(control, option, painter, widget))
    {
        return;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// sizeFromContents
// ════════════════════════════════════════════════════════════════════════════

QSize AnProxyStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                                     const QWidget* widget) const
{
    if (observatorySizes(type))
    {
        QSize size = observatorySizeFromContents(type, option, contentsSize,
                                                 QProxyStyle::sizeFromContents(type, option, contentsSize, widget),
                                                 *m_tokens, widget);
        if (const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option); type == CT_ComboBox && combo)
        {
            // The label is painted in the text field - past the border and the field's padding, short of the arrow's
            // column - an icon's gap after its icon, none of which the base style measured: the box is as wide as its
            // contents need in that field, so no item it can show is cut.
            QStyleOptionComboBox probe = *combo;
            probe.rect = QRect(0, 0, kComboProbeWidth, size.height());
            const QRect field = subControlRect(CC_ComboBox, &probe, SC_ComboBoxEditField, widget);
            const int around = probe.rect.width() - field.width();
            const int iconGap = m_tokens->spacing.xs + m_tokens->spacing.xs / 2 + 1;
            int needed = contentsSize.width() + (combo->currentIcon.isNull() ? 0 : iconGap);
            // A box sized to its items measured each by its ink; the label is elided by its advance, which can be
            // wider.
            const auto* box = qobject_cast<const QComboBox*>(widget);
            if (box != nullptr && (box->sizeAdjustPolicy() == QComboBox::AdjustToContents ||
                                   box->sizeAdjustPolicy() == QComboBox::AdjustToContentsOnFirstShow))
            {
                const QFontMetricsF metrics(box->font());
                for (int index = 0; index < box->count(); ++index)
                {
                    int item = static_cast<int>(std::ceil(metrics.horizontalAdvance(box->itemText(index))));
                    if (!box->itemIcon(index).isNull())
                    {
                        item += box->iconSize().width() + iconGap;
                    }
                    needed = (std::max)(needed, item);
                }
            }
            size.setWidth((std::max)(size.width(), needed + around));
        }
        return size;
    }

    return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// subControlRect / subElementRect
// ════════════════════════════════════════════════════════════════════════════

QRect AnProxyStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl,
                                   const QWidget* widget) const
{
    if (const std::optional<QRect> rect = observatorySubControlRect(control, option, subControl, widget))
    {
        return *rect;
    }

    if (control == CC_GroupBox)
    {
        if (const auto* groupOpt = qstyleoption_cast<const QStyleOptionGroupBox*>(option))
        {
            const QRect r = option->rect;
            const QFontMetrics fm(groupOpt->fontMetrics);
            const int titleHeight = fm.height();
            // Vertical offset: title sits fully above the frame, with a small gap.
            const int topMargin = titleHeight + m_tokens->spacing.xs;

            switch (subControl)
            {
            case SC_GroupBoxFrame:
                return QRect(r.left(), r.top() + topMargin, r.width(), r.height() - topMargin);

            case SC_GroupBoxContents: {
                const int pad = m_tokens->spacing.xs;
                return QRect(r.left() + pad, r.top() + topMargin + pad, r.width() - 2 * pad,
                             r.height() - topMargin - 2 * pad);
            }

            case SC_GroupBoxLabel:
                return QRect(r.left() + m_tokens->spacing.md, r.top(), r.width() - 2 * m_tokens->spacing.md,
                             titleHeight);

            case SC_GroupBoxCheckBox: {
                const int indicatorSize = pixelMetric(PM_IndicatorWidth, option, widget);
                // Place checkbox to the right of the title text with
                // generous spacing so the two don't look cramped.
                const int textWidth = fm.horizontalAdvance(groupOpt->text);
                const int checkX = r.left() + m_tokens->spacing.md + textWidth + m_tokens->spacing.lg;
                const int checkY = r.top() + (titleHeight - indicatorSize) / 2;
                return QRect(checkX, checkY, indicatorSize, indicatorSize);
            }

            default:
                break;
            }
        }
    }

    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QRect AnProxyStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    switch (element)
    {
    case SE_LineEditContents: {
        QRect r = QProxyStyle::subElementRect(element, option, widget);
        // Skip extra horizontal padding for line edits embedded inside
        // spin boxes and combo boxes — those already have their own
        // edit-field rect from SC_SpinBoxEditField / SC_ComboBoxEditField.
        if (widget && widget->parentWidget() &&
            (qobject_cast<QAbstractSpinBox*>(widget->parentWidget()) ||
             qobject_cast<QComboBox*>(widget->parentWidget())))
        {
            return r;
        }
        const int hPad = m_tokens->spacing.sm; // 8px horizontal padding
        return r.adjusted(hPad, 0, -hPad, 0);
    }
    default:
        break;
    }

    return QProxyStyle::subElementRect(element, option, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// standardIcon
// ════════════════════════════════════════════════════════════════════════════

QIcon AnProxyStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption* option, const QWidget* widget) const
{
    switch (standardIcon)
    {
    case SP_DialogOkButton:
    case SP_DialogApplyButton:
    case SP_DialogYesButton: {
        if (const QIcon icon = iconFromManager(QStringLiteral("check")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_DialogCloseButton:
    case SP_DialogCancelButton:
    case SP_DialogNoButton: {
        if (const QIcon icon = iconFromManager(QStringLiteral("close")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_DialogResetButton:
    case SP_BrowserReload: {
        if (const QIcon icon = iconFromManager(QStringLiteral("refresh")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_DirOpenIcon: {
        if (const QIcon icon = iconFromManager(QStringLiteral("folder_open")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("file_open")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_DirClosedIcon:
    case SP_DirLinkIcon:
    case SP_DirLinkOpenIcon:
    case SP_DirIcon: {
        if (const QIcon icon = iconFromManager(QStringLiteral("folder_closed")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("file_open")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileIcon:
    case SP_FileLinkIcon: {
        if (const QIcon icon = iconFromManager(QStringLiteral("file_open")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("picture")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_ComputerIcon: {
        if (const QIcon icon = iconFromManager(QStringLiteral("computer")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogBack: {
        if (const QIcon icon = iconFromManager(QStringLiteral("previous")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogStart: {
        if (const QIcon icon = iconFromManager(QStringLiteral("picture")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogEnd: {
        if (const QIcon icon = iconFromManager(QStringLiteral("film")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogNewFolder: {
        if (const QIcon icon = iconFromManager(QStringLiteral("add")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogToParent: {
        if (const QIcon icon = iconFromManager(QStringLiteral("up")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogContentsView: {
        if (const QIcon icon = iconFromManager(QStringLiteral("view_grid")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("grid")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogListView: {
        if (const QIcon icon = iconFromManager(QStringLiteral("view_list")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("format_align_left")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_FileDialogDetailedView:
    case SP_FileDialogInfoView: {
        if (const QIcon icon = iconFromManager(QStringLiteral("details")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("view_list")); !icon.isNull())
        {
            return icon;
        }
        if (const QIcon icon = iconFromManager(QStringLiteral("format_align_left")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_MediaPlay: {
        if (const QIcon icon = iconFromManager(QStringLiteral("play")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_MessageBoxInformation: {
        if (const QIcon icon = iconFromManager(QStringLiteral("info")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_MessageBoxWarning: {
        if (const QIcon icon = iconFromManager(QStringLiteral("warning")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_MessageBoxCritical: {
        if (const QIcon icon = iconFromManager(QStringLiteral("priority_high")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_MessageBoxQuestion: {
        if (const QIcon icon = iconFromManager(QStringLiteral("help")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    case SP_TabCloseButton:
    case SP_DockWidgetCloseButton:
    case SP_TitleBarCloseButton: {
        if (const QIcon icon = iconFromManager(QStringLiteral("close")); !icon.isNull())
        {
            return icon;
        }
        break;
    }

    default:
        break;
    }

    return QProxyStyle::standardIcon(standardIcon, option, widget);
}

// ════════════════════════════════════════════════════════════════════════════
// Install / query
// ════════════════════════════════════════════════════════════════════════════

AnProxyStyle* installAnProxyStyle()
{
    auto* style = new AnProxyStyle();
    QApplication::setStyle(style); // Qt takes ownership
    g_installedStyle = style;
    return style;
}

AnProxyStyle* installedAnProxyStyle()
{
    return g_installedStyle;
}

void registerInstalledAnProxyStyle(AnProxyStyle* style)
{
    g_installedStyle = style;
}

} // namespace an::ui
