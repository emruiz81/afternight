// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "theme/AnProxyStyle.h"

#include "controls/IAppContext.h"

#include "components/AnLayoutUtils.h"
#include "theme/AnDesignTokens.h"
#include "theme/PainterGuard.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
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

#include <algorithm>
#include <optional>
#include <vector>

namespace
{

using an::ui::PainterGuard;

/// Global raw pointer — Qt owns the style object via QApplication::setStyle().
an::ui::AnProxyStyle* g_installedStyle = nullptr;

QPixmap colorizePixmap(const QPixmap& source, const QColor& color)
{
    if (source.isNull())
    {
        return source;
    }
    QPixmap result(source.size());
    result.fill(Qt::transparent);
    result.setDevicePixelRatio(source.devicePixelRatio());

    QPainter p(&result);
    p.drawPixmap(0, 0, source);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(result.rect(), color);
    return result;
}

// ── Helper: rounded rect path ──────────────────────────────────────────────

QPainterPath roundedRectPath(const QRectF& rect, const qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return path;
}

QColor blendRgb(const QColor& base, const QColor& tint, const double ratio)
{
    const double t = std::clamp(ratio, 0.0, 1.0);
    const auto lerp = [t](const int a, const int b) { return static_cast<int>(a + (b - a) * t); };

    return QColor(lerp(base.red(), tint.red()), lerp(base.green(), tint.green()), lerp(base.blue(), tint.blue()),
                  base.alpha());
}

QColor compositeOver(const QColor& foreground, const QColor& background)
{
    const double alpha = std::clamp(static_cast<double>(foreground.alpha()) / 255.0, 0.0, 1.0);
    const auto blendChannel = [alpha](const int fg, const int bg) {
        return static_cast<int>(std::round(fg * alpha + bg * (1.0 - alpha)));
    };

    return QColor(blendChannel(foreground.red(), background.red()),
                  blendChannel(foreground.green(), background.green()),
                  blendChannel(foreground.blue(), background.blue()), 255);
}

QColor resolvedCardSurfaceColor(const an::ui::DesignTokens& tokens)
{
    QColor surface = tokens.materials.acrylicSurface;

    if (tokens.colors.cardSurfaceTint.isValid())
    {
        const bool dark = an::ui::isDarkTheme();
        surface = blendRgb(surface, tokens.colors.cardSurfaceTint, dark ? 0.56 : 0.44);
        if (dark)
        {
            // Keep tint strength while reducing luminance in dark mode.
            surface = blendRgb(surface, QColor(0, 0, 0), 0.22);
        }
    }

    if (surface.alpha() < 255)
    {
        // Resolve acrylic translucency to a stable tone to avoid nested blending seams.
        surface = compositeOver(surface, tokens.materials.micaSurface);
    }

    return surface;
}

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

QColor bestContrastTextColor(const QColor& background, const QColor& preferred, const QColor& fallbackA,
                             const QColor& fallbackB, const double minimumRatio = 3.0)
{
    const double preferredRatio = contrastRatio(background, preferred);
    if (preferredRatio >= minimumRatio)
    {
        return preferred;
    }

    const double fallbackARatio = contrastRatio(background, fallbackA);
    const double fallbackBRatio = contrastRatio(background, fallbackB);

    if (fallbackARatio >= fallbackBRatio)
    {
        return fallbackARatio >= preferredRatio ? fallbackA : preferred;
    }

    return fallbackBRatio >= preferredRatio ? fallbackB : preferred;
}

QColor disabledPrimaryButtonBackground(const an::ui::ColorTokens& colors)
{
    const bool dark = an::ui::isDarkTheme();

    QColor background = compositeOver(colors.accentDisabled, colors.bgSurface);
    background = blendRgb(background, colors.bgCard, dark ? 0.16 : 0.26);
    background = blendRgb(background, colors.textTertiary, dark ? 0.10 : 0.24);
    return background;
}

QColor disabledDangerButtonBackground(const an::ui::ColorTokens& colors)
{
    const bool dark = an::ui::isDarkTheme();

    QColor disabledDanger = colors.statusError;
    disabledDanger.setAlpha(110);

    QColor background = compositeOver(disabledDanger, colors.bgSurface);
    background = blendRgb(background, colors.bgCard, dark ? 0.15 : 0.22);
    background = blendRgb(background, colors.textTertiary, dark ? 0.08 : 0.20);
    return background;
}

QColor disabledButtonBorderColor(const an::ui::ColorTokens& colors, const QColor& background)
{
    const bool dark = an::ui::isDarkTheme();
    return blendRgb(background, colors.textTertiary, dark ? 0.16 : 0.14);
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

bool isHovered(const QStyleOption* opt)
{
    return (opt->state & QStyle::State_MouseOver) && (opt->state & QStyle::State_Enabled);
}

bool isPressed(const QStyleOption* opt)
{
    return (opt->state & QStyle::State_Sunken) && (opt->state & QStyle::State_Enabled);
}

bool isFocused(const QStyleOption* opt)
{
    return (opt->state & QStyle::State_HasFocus) && (opt->state & QStyle::State_Enabled);
}

bool isEnabled(const QStyleOption* opt)
{
    return opt->state & QStyle::State_Enabled;
}

bool isSelected(const QStyleOption* opt)
{
    return opt->state & QStyle::State_Selected;
}

bool isChecked(const QStyleOption* opt)
{
    return opt->state & QStyle::State_On;
}

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

// ── The Observatory profile's sizes ────────────────────────────────────────
//
// Under the Observatory profile the style answers the sizes of the standard widgets from the metric tokens of the
// density in force: a control's height, a tab strip's, a row's, a menu item's, a scroll bar's thickness. Every other
// answer, and every answer under the Classic profile, is the Classic one.

/// A button's padding at each side of its text, and a control's border.
constexpr int kObservatoryButtonPadding = 14;
constexpr int kObservatoryBorder = 1;

/// The narrowest a spin box is, as under the Classic profile: room for a decimal value.
constexpr int kMinimumSpinBoxWidth = 70;

bool observatoryProfile()
{
    return an::ui::styleProfile() == an::ui::StyleProfile::Observatory;
}

/// The pixel metrics the Observatory profile answers from its tokens; nothing for the ones it leaves to the Classic
/// answer.
std::optional<int> observatoryPixelMetric(const QStyle::PixelMetric metric, const an::ui::MetricTokens& metrics)
{
    switch (metric)
    {
    case QStyle::PM_ScrollBarExtent:
        return metrics.scrollBar;
    case QStyle::PM_HeaderDefaultSectionSizeVertical:
        // A table's rows: the vertical header's sections are the rows.
        return metrics.rowTable;
    default:
        return std::nullopt;
    }
}

/// Whether the Observatory profile sizes a control of this type from its tokens.
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

/// The size the Observatory profile gives a control of a type observatorySizes() names, from its contents and from the
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
        return QSize((std::max)(height, contents.width() + 2 * padding), height);
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
    case QStyle::CT_ComboBox:
        return QSize(base.width(), (std::max)(base.height(), m.control));
    case QStyle::CT_SpinBox:
        return QSize((std::max)(base.width(), kMinimumSpinBoxWidth), (std::max)(base.height(), m.control));
    case QStyle::CT_TabBarTab: {
        // A tab fills its strip; a compact tab bar's tabs, inside a panel, are a small control high.
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        const bool compact = widget != nullptr && widget->property("anTabCompact").toBool();
        const int iconExtent = tab != nullptr ? (std::max)(tab->iconSize.width(), tab->iconSize.height()) : 0;
        const int across = (std::max)(compact ? m.controlSmall : m.strip, iconExtent + 2 * tokens.spacing.xs);
        const int minimumLength = intPropertyOrDefault(widget, "anTabMinWidth", 0);
        if (verticalTab(option))
        {
            return QSize(across, (std::max)(base.height(), minimumLength));
        }
        return QSize((std::max)(base.width(), minimumLength), across);
    }
    case QStyle::CT_HeaderSection:
    case QStyle::CT_ItemViewItem:
        // A table's header and a row of a table, a list or a tree.
        return QSize(base.width(), (std::max)(base.height(), m.rowTable));
    case QStyle::CT_MenuItem: {
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (item != nullptr && item->menuItemType == QStyleOptionMenuItem::Separator)
        {
            return base;
        }
        return QSize(base.width(), (std::max)(base.height(), m.rowDense));
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
    // A density moves the sizes this style answers and the type scale; under the Classic profile neither ever moves.
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
            label->setFont(m_tokens->typography.heading1);
            // textPrimary already set above
        }
        // ── Process-shell header description ────────────────────────────
        else if (objName == QLatin1String("anProcessShellHeaderDescription"))
        {
            pal.setColor(QPalette::WindowText, m_tokens->colors.textSecondary);
            label->setPalette(pal);
            label->setFont(m_tokens->typography.body);
        }
        // ── Process-shell banner message ────────────────────────────────
        else if (objName == QLatin1String("anProcessShellBannerMessage"))
        {
            label->setFont(m_tokens->typography.body);
            // textPrimary already set above — nothing extra needed
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
        normalizeLayoutSpacingToTokens(rootLayout, m_tokens->spacing);

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
    if (observatoryProfile())
    {
        if (const std::optional<int> answer = observatoryPixelMetric(metric, m_tokens->metrics))
        {
            return *answer;
        }
    }

    switch (metric)
    {
    case PM_ButtonMargin:
        return m_tokens->spacing.sm;
    case PM_SpinBoxFrameWidth:
        return 2; // thin frame; horizontal text padding handled by SE_LineEditContents
    case PM_DefaultFrameWidth:
        return 1;
    case PM_ScrollBarExtent:
        return 12;
    case PM_SliderLength:
        return 14;
    case PM_SliderThickness:
        return 18; // room for 16px hover handle + 1px margin each side
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
    switch (element)
    {
    case PE_PanelLineEdit:
        drawLineEditFrame(option, painter, widget);
        return;

    case PE_IndicatorCheckBox:
        drawCheckBoxIndicator(option, painter, widget);
        return;

    case PE_IndicatorRadioButton:
        drawRadioButtonIndicator(option, painter, widget);
        return;

    case PE_IndicatorBranch:
        // Let the base style handle branch indicators (tree expand/collapse arrows)
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;

    case PE_PanelItemViewItem:
        drawItemViewItem(option, painter, widget);
        return;

    case PE_PanelTipLabel:
        drawToolTip(option, painter, widget);
        return;

    case PE_FrameGroupBox:
        drawGroupBox(option, painter, widget);
        return;

    case PE_Frame: {
        if (!widget)
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        // Process-shell card → elevated-surface card with round corners
        if (widget->objectName() == QLatin1String("anProcessShellCard"))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            QColor borderColor = m_tokens->colors.borderSubtle;
            QColor bgColor = resolvedCardSurfaceColor(*m_tokens);
            if (m_tokens->colors.cardSurfaceBorderTint.isValid())
            {
                borderColor = blendRgb(borderColor, m_tokens->colors.cardSurfaceBorderTint, 0.50);
            }

            painter->setPen(QPen(borderColor, 1));
            painter->setBrush(bgColor);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.lg,
                                     m_tokens->radius.lg);
            return;
        }

        // Process-shell banner → elevated surface with tone border
        if (widget->objectName() == QLatin1String("anProcessShellBanner"))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            QColor borderColor = m_tokens->colors.borderSubtle;
            const QString tone = widget->property("anProcessMessageTone").toString();
            if (tone == QLatin1String("info"))
                borderColor = m_tokens->colors.statusInfo;
            else if (tone == QLatin1String("warning"))
                borderColor = m_tokens->colors.statusWarning;
            else if (tone == QLatin1String("error"))
                borderColor = m_tokens->colors.statusError;
            painter->setPen(QPen(borderColor, 1));
            painter->setBrush(m_tokens->layers.surface3);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.md,
                                     m_tokens->radius.md);
            return;
        }

        // Generic contextual message card (used by Settings info notes).
        if (widget->objectName() == QLatin1String("anContextMessage"))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);

            QColor borderColor = m_tokens->colors.borderSubtle;
            QColor fillColor = m_tokens->layers.surface2;

            const QString tone = widget->property("anContextMessageTone").toString();
            if (tone == QLatin1String("info"))
            {
                borderColor = m_tokens->colors.statusInfo;
            }
            else if (tone == QLatin1String("warning"))
            {
                borderColor = m_tokens->colors.statusWarning;
            }
            else if (tone == QLatin1String("error"))
            {
                borderColor = m_tokens->colors.statusError;
            }

            painter->setPen(QPen(borderColor, 1));
            painter->setBrush(fillColor);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.sm,
                                     m_tokens->radius.sm);
            return;
        }

        // Item views → subtle border
        if (qobject_cast<const QAbstractItemView*>(widget) ||
            (widget->parentWidget() && qobject_cast<const QAbstractItemView*>(widget->parentWidget())))
        {
            const PainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.sm,
                                     m_tokens->radius.sm);
            return;
        }

        // Generic rounded-card QFrame driven by anFrameRadius property.
        // Set QPalette::Window for fill, QPalette::Mid for border colour,
        // and optionally anFrameBorderDashed = true for a dashed outline.
        if (qobject_cast<const QFrame*>(widget))
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

        // Everything else → let Fusion decide
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }

    case PE_FrameTabWidget: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->colors.bgSurface);
        painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.sm,
                                 m_tokens->radius.sm);
        return;
    }

    case PE_FrameMenu: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->materials.acrylicSurface);
        painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.sm,
                                 m_tokens->radius.sm);
        return;
    }

    case PE_PanelMenu: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_tokens->materials.acrylicSurface);
        painter->drawRoundedRect(QRectF(option->rect), m_tokens->radius.sm, m_tokens->radius.sm);
        return;
    }

    case PE_PanelMenuBar: {
        const PainterGuard guard(painter);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->materials.solidSurface);
        painter->drawRect(option->rect);
        return;
    }

    case PE_PanelStatusBar: {
        const PainterGuard guard(painter);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->materials.solidSurface);
        painter->drawRect(option->rect);
        return;
    }

    case PE_PanelToolBar: {
        const PainterGuard guard(painter);
        QColor toolbarBackground = m_tokens->materials.solidSurface;
        if (widget && widget->property("anUseCardBackground").toBool())
        {
            const QColor paletteBackground = option ? option->palette.color(QPalette::Window) : QColor();
            toolbarBackground = (paletteBackground.isValid() && paletteBackground.alpha() == 255)
                                    ? paletteBackground
                                    : resolvedCardSurfaceColor(*m_tokens);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(toolbarBackground);
        painter->drawRect(option->rect);
        // Bottom border
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
        return;
    }

    case PE_IndicatorToolBarSeparator: {
        const PainterGuard guard(painter);
        const auto& c = m_tokens->colors;

        // Use a neutral token color instead of accent-tinted borders so separators
        // blend with the toolbar in both light and dark modes.
        QColor lineColor = c.textTertiary;
        lineColor.setAlpha(an::ui::isDarkTheme() ? 132 : 118);
        painter->setPen(QPen(lineColor, 1));

        const bool horizontalToolBar = option->state & State_Horizontal;
        const int inset = m_tokens->spacing.xs;
        if (horizontalToolBar)
        {
            const int x = option->rect.center().x();
            const int y1 = option->rect.top() + inset;
            const int y2 = option->rect.bottom() - inset;
            painter->drawLine(x, y1, x, y2);
        }
        else
        {
            const int y = option->rect.center().y();
            const int x1 = option->rect.left() + inset;
            const int x2 = option->rect.right() - inset;
            painter->drawLine(x1, y, x2, y);
        }
        return;
    }

    case PE_PanelButtonCommand:
        drawPushButton(option, painter, widget);
        return;

    case PE_PanelButtonTool: {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const auto& c = m_tokens->colors;
        const auto r = m_tokens->radius.sm;
        QColor bgColor = Qt::transparent;
        QColor borderColor = Qt::transparent;

        if (isPressed(option))
        {
            bgColor = c.bgCardSecondary;
            borderColor = c.borderPrimary;
        }
        else if (isChecked(option))
        {
            bgColor = c.bgCardSecondary;
            borderColor = c.borderPrimary;
        }
        else if (isHovered(option))
        {
            bgColor = c.bgHover;
            borderColor = c.borderSubtle;
        }

        if (bgColor != Qt::transparent)
        {
            painter->setPen(QPen(borderColor, 1));
            painter->setBrush(bgColor);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
        }
        return;
    }

    case PE_IndicatorTabClose: {
        // Custom token-tinted tab close indicator — replaces ugly default icon.
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto& c = m_tokens->colors;
        const QRectF rect = option->rect;

        // Choose color based on hover/press state
        QColor xColor = c.textTertiary;
        if (isPressed(option))
            xColor = c.statusError;
        else if (isHovered(option))
            xColor = c.textPrimary;

        // Draw subtle circular background on hover/press
        if (isHovered(option) || isPressed(option))
        {
            QColor bgColor = isPressed(option) ? c.bgCardSecondary : c.bgHover;
            painter->setPen(Qt::NoPen);
            painter->setBrush(bgColor);
            painter->drawEllipse(rect.adjusted(1, 1, -1, -1));
        }

        // Draw × cross
        const qreal inset = rect.width() * 0.30;
        const QRectF crossRect = rect.adjusted(inset, inset, -inset, -inset);
        QPen pen(xColor, 1.5, Qt::SolidLine, Qt::RoundCap);
        painter->setPen(pen);
        painter->drawLine(crossRect.topLeft(), crossRect.bottomRight());
        painter->drawLine(crossRect.topRight(), crossRect.bottomLeft());
        return;
    }

    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// drawControl
// ════════════════════════════════════════════════════════════════════════════

void AnProxyStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                               const QWidget* widget) const
{
    switch (element)
    {
    case CE_PushButtonBevel:
        drawPushButton(option, painter, widget);
        return;

    case CE_PushButtonLabel:
        drawPushButtonLabel(option, painter, widget);
        return;

    case CE_TabBarTabShape:
        drawTabBarTab(option, painter, widget);
        return;

    case CE_TabBarTabLabel:
        if (const auto* tabOpt = qstyleoption_cast<const QStyleOptionTab*>(option))
        {
            const PainterGuard guard(painter);
            const bool forceUnselected = widget && widget->property("forceUnselected").toBool();
            const bool selected = !forceUnselected && (tabOpt->state & State_Selected);
            painter->setPen(selected ? m_tokens->colors.textAccent : m_tokens->colors.textSecondary);
            if (selected)
            {
                auto font = painter->font();
                font.setWeight(QFont::DemiBold);
                painter->setFont(font);
            }
            QProxyStyle::drawControl(element, option, painter, widget);
        }
        return;

    case CE_ProgressBar:
        // Let Fusion dispatch to CE_ProgressBarGroove, CE_ProgressBarContents,
        // and CE_ProgressBarLabel which we override individually below.
        QProxyStyle::drawControl(element, option, painter, widget);
        return;

    case CE_ProgressBarGroove: {
        // Draw only the groove background — the contents are handled separately.
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->colors.bgSurface);
        const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->drawRoundedRect(rect, m_tokens->radius.sm, m_tokens->radius.sm);
        return;
    }

    case CE_ProgressBarContents:
        drawProgressBarContents(option, painter, widget);
        return;

    case CE_ProgressBarLabel: {
        if (const auto* pbOpt = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
        {
            const PainterGuard guard(painter);
            painter->setPen(m_tokens->colors.textPrimary);
            painter->drawText(option->rect, Qt::AlignCenter, pbOpt->text);
        }
        return;
    }

    case CE_HeaderSection:
        drawHeaderSection(option, painter, widget);
        return;

    case CE_HeaderLabel: {
        if (const auto* headerOpt = qstyleoption_cast<const QStyleOptionHeader*>(option))
        {
            const PainterGuard guard(painter);
            painter->setPen(m_tokens->colors.textSecondary);
            auto font = painter->font();
            font.setWeight(QFont::DemiBold);
            painter->setFont(font);
            const QRect textRect = option->rect.adjusted(m_tokens->spacing.sm, 0, -m_tokens->spacing.sm, 0);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, headerOpt->text);
        }
        return;
    }

    case CE_MenuItem: {
        // Set the correct palette colours on the option and let Fusion
        // handle the full menu-item rendering (icons, submenu arrows,
        // shortcuts, check marks).  We only paint the selection background
        // and separators ourselves.
        if (const auto* menuOpt = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
        {
            const auto& c = m_tokens->colors;

            if (menuOpt->menuItemType == QStyleOptionMenuItem::Separator)
            {
                const PainterGuard guard(painter);
                const int y = option->rect.center().y();
                painter->setPen(QPen(c.borderSubtle, 1));
                painter->drawLine(option->rect.left() + m_tokens->spacing.sm, y,
                                  option->rect.right() - m_tokens->spacing.sm, y);
                return;
            }

            // Paint selection highlight background
            if (isSelected(option) && isEnabled(option))
            {
                const PainterGuard guard(painter);
                painter->setPen(Qt::NoPen);
                painter->setBrush(c.bgHover);
                painter->drawRect(option->rect);
            }

            // Create a copy with correct palette for Fusion text/icon rendering
            QStyleOptionMenuItem adjusted(*menuOpt);
            adjusted.palette.setColor(QPalette::WindowText, isEnabled(option)
                                                                ? (isSelected(option) ? c.textAccent : c.textPrimary)
                                                                : c.textTertiary);
            adjusted.palette.setColor(QPalette::Text, isEnabled(option)
                                                          ? (isSelected(option) ? c.textAccent : c.textPrimary)
                                                          : c.textTertiary);
            adjusted.palette.setColor(QPalette::HighlightedText, c.textAccent);
            adjusted.palette.setColor(QPalette::ButtonText, c.textPrimary);

            // Remove Selected state so Fusion won't paint its own highlight
            adjusted.state &= ~QStyle::State_Selected;

            QProxyStyle::drawControl(element, &adjusted, painter, widget);
        }
        return;
    }

    case CE_MenuBarItem: {
        drawMenuBarItem(option, painter, widget);
        return;
    }

    case CE_MenuBarEmptyArea: {
        const PainterGuard guard(painter);
        painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
        painter->setBrush(m_tokens->materials.solidSurface);
        painter->drawRect(option->rect);
        return;
    }

    case CE_ToolBar: {
        // For card-tinted toolbars, bypass base-style gradients and draw a flat
        // surface so the tone matches adjacent card backgrounds exactly.
        if (widget && widget->property("anUseCardBackground").toBool())
        {
            const PainterGuard guard(painter);
            const QColor paletteBackground = option ? option->palette.color(QPalette::Window) : QColor();
            const QColor toolbarBackground = (paletteBackground.isValid() && paletteBackground.alpha() == 255)
                                                 ? paletteBackground
                                                 : resolvedCardSurfaceColor(*m_tokens);

            painter->setPen(Qt::NoPen);
            painter->setBrush(toolbarBackground);
            painter->drawRect(option->rect);
            painter->setPen(QPen(m_tokens->colors.borderSubtle, 1));
            painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
            return;
        }

        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }

    case CE_ComboBoxLabel: {
        if (const auto* comboOpt = qstyleoption_cast<const QStyleOptionComboBox*>(option))
        {
            // Editable combos own an internal QLineEdit that paints the text.
            // Let the base style handle this path to avoid double painting.
            if (comboOpt->editable)
            {
                QProxyStyle::drawControl(element, option, painter, widget);
                return;
            }

            const PainterGuard guard(painter);
            const auto& c = m_tokens->colors;
            const bool cbEnabled = isEnabled(option);
            const QColor textColor = cbEnabled ? c.textPrimary : c.textTertiary;

            QRect editRect = subControlRect(CC_ComboBox, comboOpt, SC_ComboBoxEditField, widget);

            // Draw currentIcon if present
            if (!comboOpt->currentIcon.isNull())
            {
                const QSize iconSize = comboOpt->iconSize.isValid() ? comboOpt->iconSize : QSize(16, 16);
                const int iconY = editRect.top() + (editRect.height() - iconSize.height()) / 2;
                const QRect iconRect(editRect.left(), iconY, iconSize.width(), iconSize.height());

                const QIcon::Mode mode = cbEnabled ? QIcon::Normal : QIcon::Disabled;
                comboOpt->currentIcon.paint(painter, iconRect, Qt::AlignCenter, mode);

                constexpr int iconTextGap = 6;
                editRect.setLeft(editRect.left() + iconSize.width() + iconTextGap);
            }

            painter->setClipRect(editRect);
            painter->setPen(textColor);
            const QString elided = painter->fontMetrics().elidedText(comboOpt->currentText, Qt::ElideMiddle,
                                                                     (std::max)(0, editRect.width()));
            painter->drawText(editRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, elided);
        }
        return;
    }

    case CE_ToolButtonLabel:
        drawToolButton(option, painter, widget);
        return;

    case CE_Splitter:
        drawSplitterHandle(option, painter, widget);
        return;

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

    case CE_ItemViewItem: {
        // Background handled by PE_PanelItemViewItem; adjust selected text color
        // against the effective fill (accent with alpha composited on base).
        if (const auto* viewOpt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            QStyleOptionViewItem adjusted(*viewOpt);
            if (isSelected(option))
            {
                QColor selectionFill = m_tokens->colors.accentPrimary;
                selectionFill.setAlpha(an::ui::isDarkTheme() ? 116 : 84);

                const QColor effectiveSelectionBg =
                    compositeOver(selectionFill, adjusted.palette.color(QPalette::Base));
                const QColor selectedTextColor = bestContrastTextColor(
                    effectiveSelectionBg, m_tokens->colors.textOnAccent, QColor(255, 255, 255), QColor(0, 0, 0), 4.5);

                adjusted.palette.setColor(QPalette::HighlightedText, selectedTextColor);
                adjusted.palette.setColor(QPalette::Text, selectedTextColor);
                adjusted.palette.setColor(QPalette::WindowText, selectedTextColor);
            }

            QProxyStyle::drawControl(element, &adjusted, painter, widget);
            return;
        }

        QProxyStyle::drawControl(element, option, painter, widget);
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
    switch (control)
    {
    case CC_ComboBox:
        drawComboBox(option, painter, widget);
        return;

    case CC_SpinBox:
        drawSpinBox(option, painter, widget);
        return;

    case CC_Slider:
        drawSlider(option, painter, widget);
        return;

    case CC_ScrollBar:
        drawScrollBar(option, painter, widget);
        return;

    case CC_GroupBox: {
        if (const auto* groupOpt = qstyleoption_cast<const QStyleOptionGroupBox*>(option))
        {
            // Frame — use the SC_GroupBoxFrame rect so the frame starts below the title.
            QStyleOptionGroupBox frameOpt(*groupOpt);
            frameOpt.subControls &= ~QStyle::SC_GroupBoxLabel;
            if (frameOpt.subControls & QStyle::SC_GroupBoxFrame)
            {
                frameOpt.rect = subControlRect(CC_GroupBox, groupOpt, SC_GroupBoxFrame, widget);
                drawGroupBox(&frameOpt, painter, widget);
            }

            // Title label
            if (groupOpt->subControls & QStyle::SC_GroupBoxLabel)
            {
                const QRect titleRect = subControlRect(CC_GroupBox, groupOpt, SC_GroupBoxLabel, widget);
                const PainterGuard guard(painter);

                painter->setPen(isEnabled(option) ? m_tokens->colors.textAccent : m_tokens->colors.textTertiary);
                auto font = painter->font();
                font.setWeight(QFont::DemiBold);
                painter->setFont(font);
                painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, groupOpt->text);
            }

            // Checkbox (if checkable)
            if (groupOpt->subControls & QStyle::SC_GroupBoxCheckBox)
            {
                QStyleOptionButton checkOpt;
                checkOpt.QStyleOption::operator=(*groupOpt);
                checkOpt.rect = subControlRect(CC_GroupBox, groupOpt, SC_GroupBoxCheckBox, widget);
                if (groupOpt->state & State_On)
                {
                    checkOpt.state |= State_On;
                }
                else
                {
                    checkOpt.state &= ~State_On;
                }
                drawPrimitive(PE_IndicatorCheckBox, &checkOpt, painter, widget);
            }
        }
        return;
    }

    default:
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// sizeFromContents
// ════════════════════════════════════════════════════════════════════════════

QSize AnProxyStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize,
                                     const QWidget* widget) const
{
    if (observatoryProfile() && observatorySizes(type))
    {
        return observatorySizeFromContents(type, option, contentsSize,
                                           QProxyStyle::sizeFromContents(type, option, contentsSize, widget), *m_tokens,
                                           widget);
    }

    switch (type)
    {
    case CT_PushButton: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        // Larger minimum for dialog button box buttons (Ok/Cancel/Apply/etc.)
        if (widget && widget->parentWidget() && qobject_cast<QDialogButtonBox*>(widget->parentWidget()))
        {
            size = size.expandedTo(QSize(90, 32));
        }
        else
        {
            size = size.expandedTo(QSize(70, 24));
        }
        return size;
    }

    case CT_LineEdit: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        size.setHeight(qMax(size.height(), 24));
        return size;
    }

    case CT_ComboBox: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        size.setHeight(qMax(size.height(), 24));
        return size;
    }

    case CT_SpinBox: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        size.setHeight(qMax(size.height(), 24));
        size.setWidth(qMax(size.width(), 70)); // ensure enough room for decimal values
        return size;
    }

    case CT_TabBarTab: {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        double heightScale = 1.0;
        if (widget)
        {
            bool ok = false;
            const double propertyScale = widget->property("anTabHeightScale").toDouble(&ok);
            if (ok && propertyScale > 0.0)
            {
                heightScale = propertyScale;
            }
        }
        if (heightScale != 1.0)
        {
            size.setHeight(qMax(16, qRound(size.height() * heightScale)));
        }
        const int minWidth = intPropertyOrDefault(widget, "anTabMinWidth", 0);
        const int minHeight = intPropertyOrDefault(widget, "anTabMinHeight", 0);
        if (minWidth > 0)
        {
            size.setWidth(qMax(size.width(), minWidth));
        }
        if (minHeight > 0)
        {
            size.setHeight(qMax(size.height(), minHeight));
        }
        return size;
    }

    default:
        return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// subControlRect / subElementRect
// ════════════════════════════════════════════════════════════════════════════

QRect AnProxyStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl,
                                   const QWidget* widget) const
{
    if (control == CC_SpinBox)
    {
        const QRect r = option->rect;
        const int buttonWidth = 20;

        switch (subControl)
        {
        case SC_SpinBoxUp:
            return QRect(r.right() - buttonWidth + 1, r.top(), buttonWidth, r.height() / 2);
        case SC_SpinBoxDown:
            return QRect(r.right() - buttonWidth + 1, r.top() + r.height() / 2, buttonWidth,
                         r.height() - r.height() / 2);
        case SC_SpinBoxEditField: {
            const int lPad = m_tokens->spacing.sm; // 8px left padding for text
            return QRect(r.left() + lPad, r.top(), r.width() - buttonWidth - lPad, r.height());
        }
        case SC_SpinBoxFrame:
            return r;
        default:
            break;
        }
    }

    if (control == CC_ComboBox)
    {
        const QRect r = option->rect;
        const int buttonWidth = 24;

        switch (subControl)
        {
        case SC_ComboBoxArrow:
            return QRect(r.right() - buttonWidth + 1, r.top(), buttonWidth, r.height());
        case SC_ComboBoxEditField:
            return QRect(r.left() + 8, r.top(), r.width() - buttonWidth - 8, r.height());
        case SC_ComboBoxFrame:
            return r;
        case SC_ComboBoxListBoxPopup:
            return r;
        default:
            break;
        }
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
// Widget-specific drawing methods
// ════════════════════════════════════════════════════════════════════════════

// ── GroupBox ────────────────────────────────────────────────────────────────

void AnProxyStyle::drawGroupBox(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.md;

    // Check if this is a "flat" group box (process shell style)
    const bool isFlat =
        widget && (hasProperty(widget, "anGroupVariant", "flat") ||
                   (qobject_cast<const QGroupBox*>(widget) && qobject_cast<const QGroupBox*>(widget)->isFlat()));

    QColor bgColor;
    QColor borderColor;

    if (!isEnabled(option))
    {
        bgColor = c.bgSurface;
        borderColor = c.borderSubtle;
    }
    else if (isFlat)
    {
        bgColor = Qt::transparent;
        borderColor = c.borderSubtle;
    }
    else
    {
        bgColor = c.bgCard;
        borderColor = c.borderPrimary;

        // Card surface tint is intentionally limited to groupbox/card frames
        // so menus, window backgrounds, and unrelated controls remain neutral.
        if (c.cardSurfaceTint.isValid())
        {
            const bool dark = an::ui::isDarkTheme();
            bgColor = blendRgb(bgColor, c.cardSurfaceTint, dark ? 0.52 : 0.40);
            if (dark)
            {
                // Keep tint presence strong, while pushing luminance lower in dark mode.
                bgColor = blendRgb(bgColor, QColor(0, 0, 0), 0.22);
            }
        }
        if (c.cardSurfaceBorderTint.isValid())
        {
            borderColor = blendRgb(borderColor, c.cardSurfaceBorderTint, 0.45);
        }
    }

    painter->setPen(QPen(borderColor, 1));
    painter->setBrush(bgColor);

    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);
}

// ── PushButton ─────────────────────────────────────────────────────────────

void AnProxyStyle::drawPushButton(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    const bool isPrimary = hasProperty(widget, "buttonVariant", "primary") ||
                           hasProperty(widget, "anButtonVariant", "primary") ||
                           hasProperty(widget, "anProcessButtonRole", "primary");
    const bool isSecondary = hasProperty(widget, "anProcessButtonRole", "secondary");
    const bool isNeutral = hasProperty(widget, "anProcessButtonRole", "neutral");
    const bool isDanger = hasProperty(widget, "anProcessButtonRole", "danger");
    const bool isActive = isPressed(option) || isChecked(option);

    QColor bgColor;
    QColor borderColor;

    if (!isEnabled(option))
    {
        if (isPrimary)
        {
            bgColor = disabledPrimaryButtonBackground(c);
            borderColor = disabledButtonBorderColor(c, bgColor);
        }
        else if (isDanger)
        {
            bgColor = disabledDangerButtonBackground(c);
            borderColor = disabledButtonBorderColor(c, bgColor);
        }
        else
        {
            bgColor = c.bgCard;
            borderColor = c.borderSubtle;
        }
    }
    else if (isActive)
    {
        if (isPrimary)
        {
            bgColor = c.accentPressed;
            borderColor = c.accentPressed;
        }
        else if (isDanger)
        {
            bgColor = c.statusError.darker(112);
            borderColor = bgColor;
        }
        else
        {
            bgColor = c.bgCardSecondary;
            borderColor = c.borderPrimary;
        }
    }
    else if (isHovered(option))
    {
        if (isPrimary)
        {
            bgColor = c.accentHover;
            borderColor = c.accentHover;
        }
        else if (isDanger)
        {
            bgColor = c.statusError.lighter(112);
            borderColor = bgColor;
        }
        else
        {
            // Keep background unchanged on hover — only highlight the border.
            // This gives consistent behavior across light and dark themes:
            // hover = border highlight only, pressed = full background shift.
            bgColor = c.bgCard;
            borderColor = c.textAccent;
        }
    }
    else
    {
        if (isPrimary)
        {
            bgColor = c.accentPrimary;
            borderColor = c.accentPrimary;
        }
        else if (isDanger)
        {
            bgColor = c.statusError;
            borderColor = c.statusError;
        }
        else if (isSecondary)
        {
            bgColor = Qt::transparent;
            borderColor = c.borderSubtle;
        }
        else if (isNeutral)
        {
            bgColor = c.bgCard;
            borderColor = c.borderSubtle; // very subtle dash, slightly brighter than button bg
        }
        else
        {
            bgColor = c.bgCard;
            borderColor = c.borderPrimary;
        }
    }

    // Neutral variant uses dashed border
    Qt::PenStyle lineStyle = Qt::SolidLine;
    if (isNeutral && !isHovered(option) && !isActive)
    {
        lineStyle = Qt::DashLine;
    }

    painter->setPen(QPen(borderColor, 1, lineStyle));
    painter->setBrush(bgColor);

    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);
}

void AnProxyStyle::drawPushButtonLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (const auto* btnOpt = qstyleoption_cast<const QStyleOptionButton*>(option))
    {
        const PainterGuard guard(painter);

        const bool isPrimary = hasProperty(widget, "buttonVariant", "primary") ||
                               hasProperty(widget, "anButtonVariant", "primary") ||
                               hasProperty(widget, "anProcessButtonRole", "primary");
        const bool isSecondary = hasProperty(widget, "anProcessButtonRole", "secondary");
        const bool isNeutral = hasProperty(widget, "anProcessButtonRole", "neutral");
        const bool isDanger = hasProperty(widget, "anProcessButtonRole", "danger");
        const bool isActive = isPressed(option) || isChecked(option);

        QColor textColor;
        if (!isEnabled(option))
        {
            QColor disabledBackground = m_tokens->colors.bgCard;
            if (isPrimary)
            {
                disabledBackground = disabledPrimaryButtonBackground(m_tokens->colors);
            }
            else if (isDanger)
            {
                disabledBackground = disabledDangerButtonBackground(m_tokens->colors);
            }
            else if (isSecondary)
            {
                disabledBackground = m_tokens->colors.bgCard;
            }
            else if (isNeutral)
            {
                disabledBackground = m_tokens->colors.bgCard;
            }

            textColor = bestContrastTextColor(disabledBackground, m_tokens->colors.textTertiary,
                                              m_tokens->colors.textSecondary, m_tokens->colors.textPrimary, 4.0);
        }
        else if (isPrimary)
        {
            textColor = m_tokens->colors.textOnAccent;
        }
        else if (isDanger)
        {
            // Danger buttons use statusError as background, not accentPrimary,
            // so we compute optimal text color against statusError independently
            // rather than reusing textOnAccent (which targets accentPrimary).
            const double crWhite = contrastRatio(QColor(255, 255, 255), m_tokens->colors.statusError);
            const double crBlack = contrastRatio(QColor(0, 0, 0), m_tokens->colors.statusError);
            textColor = (crWhite >= 4.5 || crWhite > crBlack) ? QColor(255, 255, 255) : QColor(0, 0, 0);
        }
        else if (isActive)
        {
            textColor = m_tokens->colors.textAccent;
        }
        else
        {
            textColor = m_tokens->colors.textPrimary;
        }

        painter->setPen(textColor);
        auto font = painter->font();
        font.setWeight(QFont::Medium);
        painter->setFont(font);

        if (!btnOpt->icon.isNull())
        {
            const QIcon::Mode iconMode = isEnabled(option) ? QIcon::Normal : QIcon::Disabled;
            const QString buttonText = stripMnemonicMarkers(btnOpt->text);
            const bool shouldColorize = (isPrimary || isDanger);
            // Always fetch QIcon::Normal for colorized buttons because we
            // re-tint the pixmap to the computed textColor ourselves, even
            // when the button is disabled.
            const QIcon::Mode fetchMode = shouldColorize ? QIcon::Normal : iconMode;

            // Icon-only buttons (like path picker browse buttons) should be truly centered.
            if (buttonText.isEmpty())
            {
                const int iconInset = qMax(1, m_tokens->spacing.xs / 2);
                if (shouldColorize)
                {
                    QPixmap pm = btnOpt->icon.pixmap(btnOpt->iconSize, fetchMode);
                    pm = colorizePixmap(pm, textColor);
                    const QRect area = option->rect.adjusted(iconInset, iconInset, -iconInset, -iconInset);
                    const int pmW = static_cast<int>(pm.width() / pm.devicePixelRatio());
                    const int pmH = static_cast<int>(pm.height() / pm.devicePixelRatio());
                    const int x = area.left() + (area.width() - pmW) / 2;
                    const int y = area.top() + (area.height() - pmH) / 2;
                    painter->drawPixmap(x, y, pm);
                }
                else
                {
                    btnOpt->icon.paint(painter, option->rect.adjusted(iconInset, iconInset, -iconInset, -iconInset),
                                       Qt::AlignCenter, iconMode);
                }
            }
            else
            {
                QPixmap pm = btnOpt->icon.pixmap(btnOpt->iconSize, fetchMode);
                if (shouldColorize)
                {
                    pm = colorizePixmap(pm, textColor);
                }
                const int iconSpacing = 4;
                const int totalWidth = pm.width() / pm.devicePixelRatio() + iconSpacing +
                                       painter->fontMetrics().horizontalAdvance(buttonText);
                int x = option->rect.center().x() - totalWidth / 2;
                const int y = option->rect.center().y() - static_cast<int>(pm.height() / pm.devicePixelRatio()) / 2;
                painter->drawPixmap(x, y, pm);
                x += static_cast<int>(pm.width() / pm.devicePixelRatio()) + iconSpacing;
                painter->drawText(QRect(x, option->rect.top(), option->rect.right() - x, option->rect.height()),
                                  Qt::AlignVCenter | Qt::AlignLeft, buttonText);
            }
        }
        else
        {
            painter->drawText(option->rect, Qt::AlignCenter, stripMnemonicMarkers(btnOpt->text));
        }
    }
}

// ── ComboBox ───────────────────────────────────────────────────────────────

void AnProxyStyle::drawComboBox(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    const bool enabled = isEnabled(option);
    const bool focused = isFocused(option);

    const QColor bgColor = enabled ? c.bgInput : c.bgSurface;
    const QColor borderColor = focused ? c.borderFocus : c.borderSubtle;
    const QColor textColor = enabled ? c.textPrimary : c.textTertiary;

    // Frame
    painter->setPen(QPen(borderColor, 1));
    painter->setBrush(bgColor);
    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);

    // Drop-down arrow area
    const QRect arrowRect = subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);
    if (isHovered(option))
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(c.bgHover);
        const QRectF arrowBg(arrowRect.left(), rect.top() + 1, arrowRect.width() - 1, rect.height() - 2);
        QPainterPath arrowPath;
        arrowPath.addRoundedRect(arrowBg, 0, 0);
        // Only round the right corners
        QPainterPath rightClip;
        rightClip.addRoundedRect(QRectF(rect).adjusted(0, 0, 0, 0), r, r);
        QPainterPath leftOnly;
        leftOnly.addRect(QRectF(rect.left(), rect.top(), arrowRect.left() - rect.left(), rect.height()));
        QPainterPath rightOnly = rightClip.subtracted(leftOnly);
        painter->setClipPath(rightOnly);
        painter->drawRect(arrowBg);
        painter->setClipping(false);
    }

    // Arrow icon
    const int arrowSize = 12;
    const QRect iconRect(arrowRect.center().x() - arrowSize / 2, arrowRect.center().y() - arrowSize / 2, arrowSize,
                         arrowSize);

    // Draw a simple downward chevron
    painter->setPen(QPen(textColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const int cx = iconRect.center().x();
    const int cy = iconRect.center().y();
    painter->drawLine(cx - 3, cy - 1, cx, cy + 2);
    painter->drawLine(cx, cy + 2, cx + 3, cy - 1);

    // Icon + text label is drawn by CE_ComboBoxLabel (see drawControl).
}

// ── SpinBox / DoubleSpinBox ────────────────────────────────────────────────

void AnProxyStyle::drawSpinBox(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    const bool enabled = isEnabled(option);
    const bool focused = isFocused(option);

    const QColor bgColor = enabled ? c.bgInput : c.bgSurface;
    const QColor borderColor = focused ? c.borderFocus : c.borderSubtle;

    // Frame
    painter->setPen(QPen(borderColor, 1));
    painter->setBrush(bgColor);
    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);

    // Up/Down buttons and arrows
    const QRect upRect = subControlRect(CC_SpinBox, option, SC_SpinBoxUp, widget);
    const QRect downRect = subControlRect(CC_SpinBox, option, SC_SpinBoxDown, widget);

    if (const auto* spinOpt = qstyleoption_cast<const QStyleOptionSpinBox*>(option))
    {
        const QColor arrowColor = enabled ? c.textSecondary : c.textTertiary;

        // Hover highlights
        if (spinOpt->activeSubControls & SC_SpinBoxUp && isHovered(option))
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.bgHover);
            painter->drawRect(upRect);
        }
        if (spinOpt->activeSubControls & SC_SpinBoxDown && isHovered(option))
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.bgHover);
            painter->drawRect(downRect);
        }

        // Up arrow chevron (shift cy down by 2px toward center)
        {
            painter->setPen(QPen(arrowColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            const int cx = upRect.center().x();
            const int cy = upRect.center().y() + 2;
            painter->drawLine(cx - 3, cy + 1, cx, cy - 2);
            painter->drawLine(cx, cy - 2, cx + 3, cy + 1);
        }

        // Down arrow chevron (shift cy up by 2px toward center)
        {
            painter->setPen(QPen(arrowColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            const int cx = downRect.center().x();
            const int cy = downRect.center().y() - 2;
            painter->drawLine(cx - 3, cy - 1, cx, cy + 2);
            painter->drawLine(cx, cy + 2, cx + 3, cy - 1);
        }
    }
}

// ── Slider ─────────────────────────────────────────────────────────────────

void AnProxyStyle::drawSlider(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    if (const auto* sliderOpt = qstyleoption_cast<const QStyleOptionSlider*>(option))
    {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto& c = m_tokens->colors;
        const bool sliderEnabled = isEnabled(option);
        const bool horizontal = (sliderOpt->orientation == Qt::Horizontal);
        QColor disabledGrooveColor = c.borderSubtle;
        disabledGrooveColor.setAlpha((std::max)(72, c.borderSubtle.alpha() * 3 / 5));

        // Groove
        const QRect grooveRect = subControlRect(CC_Slider, option, SC_SliderGroove, widget);
        if (horizontal)
        {
            const int grooveHeight = 4;
            const QRectF gRect(grooveRect.left(), grooveRect.center().y() - grooveHeight / 2, grooveRect.width(),
                               grooveHeight);

            // Full groove background
            painter->setPen(Qt::NoPen);
            painter->setBrush(sliderEnabled ? c.borderSubtle : disabledGrooveColor);
            painter->drawRoundedRect(gRect, 2, 2);

            // Filled portion (sub-page)
            const QRect handleRect = subControlRect(CC_Slider, option, SC_SliderHandle, widget);
            const QRectF filledRect(gRect.left(), gRect.top(), handleRect.center().x() - gRect.left(), gRect.height());
            painter->setBrush(sliderEnabled ? c.accentPrimary : c.accentDisabled);
            painter->drawRoundedRect(filledRect, 2, 2);
        }
        else
        {
            const int grooveWidth = 4;
            const QRectF gRect(grooveRect.center().x() - grooveWidth / 2, grooveRect.top(), grooveWidth,
                               grooveRect.height());
            painter->setPen(Qt::NoPen);
            painter->setBrush(sliderEnabled ? c.borderSubtle : disabledGrooveColor);
            painter->drawRoundedRect(gRect, 2, 2);
        }

        // Handle
        const QRect handleRect = subControlRect(CC_Slider, option, SC_SliderHandle, widget);
        const bool handleHover = sliderEnabled && (sliderOpt->activeSubControls & SC_SliderHandle) && isHovered(option);

        const int handleSize = handleHover ? 16 : 14;
        const QPointF center = QRectF(handleRect).center();
        const QRectF handleCircle(center.x() - handleSize / 2.0, center.y() - handleSize / 2.0, handleSize, handleSize);

        painter->setPen(Qt::NoPen);
        if (!sliderEnabled)
        {
            QColor disabledHandleColor = compositeOver(c.accentDisabled, c.bgSurface);
            disabledHandleColor.setAlpha(255);
            painter->setBrush(disabledHandleColor);
        }
        else
        {
            painter->setBrush(handleHover ? c.borderFocus : c.accentPrimary);
        }
        painter->drawEllipse(handleCircle);
    }
}

// ── ScrollBar ──────────────────────────────────────────────────────────────

void AnProxyStyle::drawScrollBar(const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;

    // Background
    painter->setPen(Qt::NoPen);
    painter->setBrush(c.bgSurface);
    painter->drawRect(option->rect);

    // Handle
    if (const auto* sbOpt = qstyleoption_cast<const QStyleOptionSlider*>(option))
    {
        const QRect handleRect = subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, widget);
        const bool handleHover = (sbOpt->activeSubControls & SC_ScrollBarSlider) && isHovered(option);

        const QColor handleColor = handleHover ? c.textTertiary : c.borderSubtle;
        const QRectF adjustedHandle = QRectF(handleRect).adjusted(2, 2, -2, -2);
        painter->setBrush(handleColor);
        painter->drawRoundedRect(adjustedHandle, 6, 6);
    }
}

// ── ProgressBar ────────────────────────────────────────────────────────────

void AnProxyStyle::drawProgressBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    // Groove
    painter->setPen(QPen(c.borderSubtle, 1));
    painter->setBrush(c.bgSurface);
    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);

    // Contents
    drawProgressBarContents(option, painter, widget);

    // Label
    if (const auto* pbOpt = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
    {
        if (!pbOpt->text.isEmpty())
        {
            painter->setPen(c.textPrimary);
            painter->drawText(option->rect, Qt::AlignCenter, pbOpt->text);
        }
    }
}

void AnProxyStyle::drawProgressBarContents(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (const auto* pbOpt = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
    {
        if (pbOpt->maximum <= pbOpt->minimum)
        {
            return;
        }

        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto r = m_tokens->radius.sm;
        const double progress = static_cast<double>(pbOpt->progress - pbOpt->minimum) /
                                static_cast<double>(pbOpt->maximum - pbOpt->minimum);
        const QRectF rect = QRectF(option->rect).adjusted(1, 1, -1, -1);
        const QRectF chunkRect(rect.left(), rect.top(), rect.width() * progress, rect.height());

        // Determine chunk color from anUsageTone property
        QColor chunkColor = m_tokens->colors.accentPrimary;
        if (widget)
        {
            const QString tone = widget->property("anUsageTone").toString();
            if (tone == QLatin1String("high"))
            {
                chunkColor = m_tokens->colors.statusError;
            }
            else if (tone == QLatin1String("medium"))
            {
                chunkColor = m_tokens->colors.statusWarning;
            }
            else if (tone == QLatin1String("low"))
            {
                chunkColor = m_tokens->colors.statusSuccess;
            }
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(chunkColor);
        painter->drawRoundedRect(chunkRect, r, r);
    }
}

// ── TabBar ─────────────────────────────────────────────────────────────────

void AnProxyStyle::drawTabBarTab(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (const auto* tabOpt = qstyleoption_cast<const QStyleOptionTab*>(option))
    {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto& c = m_tokens->colors;
        const auto r = m_tokens->radius.sm;

        // Support KDDW's forceUnselected property (visual-only override)
        const bool forceUnselected = widget && widget->property("forceUnselected").toBool();
        const bool selected = !forceUnselected && (tabOpt->state & State_Selected);
        const bool hovered = isHovered(option) && !selected;

        QColor bgColor;
        QColor borderColor = c.borderSubtle;

        if (selected)
        {
            bgColor = c.bgCard;
            borderColor = c.borderPrimary;
        }
        else if (hovered)
        {
            bgColor = c.bgHover;
        }
        else
        {
            bgColor = c.bgCardSecondary;
        }

        painter->setPen(QPen(borderColor, 1));
        painter->setBrush(bgColor);

        // Top-rounded rect
        QRectF tabRect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.moveTo(tabRect.bottomLeft());
        path.lineTo(tabRect.left(), tabRect.top() + r);
        path.arcTo(tabRect.left(), tabRect.top(), 2 * r, 2 * r, 180, -90);
        path.lineTo(tabRect.right() - r, tabRect.top());
        path.arcTo(tabRect.right() - 2 * r, tabRect.top(), 2 * r, 2 * r, 90, -90);
        path.lineTo(tabRect.bottomRight());

        if (selected)
        {
            // Erase bottom border for selected tab
            path.lineTo(tabRect.bottomLeft());
        }

        painter->drawPath(path);

        // Bold accent line on the active tab — default is bottom, but specific
        // tab bars can opt into a top line via anTabAccentLinePosition="top".
        if (selected)
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.accentPrimary);
            const qreal barHeight = 3.0;
            const QString accentLinePosition =
                widget ? widget->property("anTabAccentLinePosition").toString() : QString();
            const bool drawTopAccent = accentLinePosition == QLatin1String("top");
            const qreal accentY = drawTopAccent ? tabRect.top() : (tabRect.bottom() - barHeight);
            painter->drawRect(QRectF(tabRect.left(), accentY, tabRect.width(), barHeight));
        }
    }
}

// ── HeaderSection ──────────────────────────────────────────────────────────

void AnProxyStyle::drawHeaderSection(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);

    const auto& c = m_tokens->colors;
    const auto& layers = m_tokens->layers;

    painter->setPen(Qt::NoPen);
    painter->setBrush(layers.surface2);
    painter->drawRect(option->rect);

    // Bottom and right borders
    painter->setPen(QPen(c.borderSubtle, 1));
    painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
    painter->setPen(QPen(c.borderSubtle, 1));
    painter->drawLine(option->rect.topRight(), option->rect.bottomRight());
}

// ── MenuItemvoid ───────────────────────────────────────────────────────────

void AnProxyStyle::drawMenuItem(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    if (const auto* menuOpt = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
    {
        const PainterGuard guard(painter);
        const auto& c = m_tokens->colors;

        if (menuOpt->menuItemType == QStyleOptionMenuItem::Separator)
        {
            const int y = option->rect.center().y();
            painter->setPen(QPen(c.borderSubtle, 1));
            painter->drawLine(option->rect.left() + m_tokens->spacing.sm, y,
                              option->rect.right() - m_tokens->spacing.sm, y);
            return;
        }

        // Background
        if (isSelected(option) && isEnabled(option))
        {
            painter->setPen(Qt::NoPen);
            painter->setBrush(c.bgHover);
            painter->drawRect(option->rect);
        }

        // Text
        const QRect textRect = option->rect.adjusted(m_tokens->spacing.lg, 0, -m_tokens->spacing.lg, 0);
        const QColor textColor = (isSelected(option) && isEnabled(option))
                                     ? c.textAccent
                                     : (isEnabled(option) ? c.textPrimary : c.textTertiary);

        painter->setPen(textColor);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          stripMnemonicMarkers(menuOpt->text.section(QLatin1Char('\t'), 0, 0)));

        // Shortcut
        const QString shortcut = menuOpt->text.section(QLatin1Char('\t'), 1);
        if (!shortcut.isEmpty())
        {
            painter->setPen(c.textTertiary);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignRight, shortcut);
        }

        // Check mark
        if (menuOpt->checked)
        {
            const int indicatorSize = 16;
            const QRect checkRect(option->rect.left() + 4, option->rect.center().y() - indicatorSize / 2, indicatorSize,
                                  indicatorSize);
            painter->setPen(QPen(c.accentPrimary, 2, Qt::SolidLine, Qt::RoundCap));
            painter->setBrush(Qt::NoBrush);
            // Draw a check mark
            const int cx = checkRect.center().x();
            const int cy = checkRect.center().y();
            painter->drawLine(cx - 3, cy, cx - 1, cy + 2);
            painter->drawLine(cx - 1, cy + 2, cx + 3, cy - 2);
        }
    }
}

// ── MenuBar Item ───────────────────────────────────────────────────────────

void AnProxyStyle::drawMenuBarItem(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    if (const auto* menuOpt = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
    {
        const PainterGuard guard(painter);
        painter->setRenderHint(QPainter::Antialiasing, true);

        const auto& c = m_tokens->colors;
        const bool active = isPressed(option) || isHovered(option) || isSelected(option);
        const bool enabled = isEnabled(option);

        // Background
        painter->setPen(Qt::NoPen);
        if (active && enabled)
        {
            QColor hoverFill = c.bgHover;
            if (!an::ui::isDarkTheme())
            {
                hoverFill = c.accentPrimary;
                hoverFill.setAlpha(34);
            }
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), m_tokens->radius.sm,
                                     m_tokens->radius.sm);
        }

        // Text
        QColor textColor = c.textPrimary;
        if (!enabled)
        {
            textColor = c.textTertiary;
        }
        else if (active)
        {
            textColor = c.textAccent;
        }
        painter->setPen(textColor);
        painter->drawText(option->rect, Qt::AlignCenter, stripMnemonicMarkers(menuOpt->text));
    }
}

// ── ToolButton ─────────────────────────────────────────────────────────────

void AnProxyStyle::drawToolButton(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    // Just render via the base proxy for the actual label content (icon + text),
    // but with the right palette colors. The frame is handled by the ToolButton
    // section in drawPrimitive for PE_PanelButtonTool.
    QProxyStyle::drawControl(CE_ToolButtonLabel, option, painter, widget);
}

// ── CheckBox Indicator ─────────────────────────────────────────────────────

void AnProxyStyle::drawCheckBoxIndicator(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const bool enabled = isEnabled(option);
    const int size = 16;
    // Inset by 0.5px so the 1px anti-aliased pen stays fully inside the rect.
    const QRectF indicatorRect =
        QRectF(option->rect.center().x() - size / 2.0, option->rect.center().y() - size / 2.0, size, size)
            .adjusted(0.5, 0.5, -0.5, -0.5);

    if (isChecked(option))
    {
        const QColor checkBgColor = enabled ? c.accentPrimary : c.accentDisabled;
        painter->setPen(QPen(checkBgColor, 1));
        painter->setBrush(checkBgColor);
        painter->drawRoundedRect(indicatorRect, 3, 3);

        const QColor checkMarkColor = enabled ? c.textOnAccent : c.textTertiary;
        painter->setPen(QPen(checkMarkColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        const double cx = indicatorRect.center().x();
        const double cy = indicatorRect.center().y();
        painter->drawLine(QPointF(cx - 3, cy), QPointF(cx - 1, cy + 3));
        painter->drawLine(QPointF(cx - 1, cy + 3), QPointF(cx + 4, cy - 3));
    }
    else
    {
        const QColor borderColor = enabled ? c.borderPrimary : c.borderSubtle;
        const QColor fillColor = enabled ? c.bgInput : c.bgSurface;
        painter->setPen(QPen(borderColor, 1));
        painter->setBrush(fillColor);
        painter->drawRoundedRect(indicatorRect, 3, 3);
    }
}

// ── RadioButton Indicator ──────────────────────────────────────────────────

void AnProxyStyle::drawRadioButtonIndicator(const QStyleOption* option, QPainter* painter,
                                            const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const bool enabled = isEnabled(option);
    const int size = 16;
    // Inset by 0.5px so the 1px anti-aliased pen stays fully inside the rect.
    const QRectF indicatorRect =
        QRectF(option->rect.center().x() - size / 2.0, option->rect.center().y() - size / 2.0, size, size)
            .adjusted(0.5, 0.5, -0.5, -0.5);

    if (isChecked(option))
    {
        const QColor checkBgColor = enabled ? c.accentPrimary : c.accentDisabled;
        painter->setPen(QPen(checkBgColor, 1));
        painter->setBrush(checkBgColor);
        painter->drawEllipse(indicatorRect);

        // Inner dot
        const double dotSize = 6;
        const QRectF dotRect(indicatorRect.center().x() - dotSize / 2, indicatorRect.center().y() - dotSize / 2,
                             dotSize, dotSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(enabled ? c.textOnAccent : c.textTertiary);
        painter->drawEllipse(dotRect);
    }
    else
    {
        const QColor borderColor = enabled ? c.borderPrimary : c.borderSubtle;
        const QColor fillColor = enabled ? c.bgInput : c.bgSurface;
        painter->setPen(QPen(borderColor, 1));
        painter->setBrush(fillColor);
        painter->drawEllipse(indicatorRect);
    }
}

// ── Item View Item ─────────────────────────────────────────────────────────

void AnProxyStyle::drawItemViewItem(const QStyleOption* option, QPainter* painter, const QWidget* /*widget*/) const
{
    const PainterGuard guard(painter);
    const auto& c = m_tokens->colors;

    if (isSelected(option))
    {
        QColor selectionFill = c.accentPrimary;
        selectionFill.setAlpha(an::ui::isDarkTheme() ? 116 : 84);

        painter->setPen(Qt::NoPen);
        painter->setBrush(selectionFill);
        painter->drawRect(option->rect);

        painter->setPen(QPen(c.borderFocus, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
    }
    else if (isHovered(option) && isEnabled(option))
    {
        QColor hoverFill = c.bgHover;
        hoverFill.setAlpha(an::ui::isDarkTheme() ? 210 : 165);
        painter->setPen(Qt::NoPen);
        painter->setBrush(hoverFill);
        painter->drawRect(option->rect);
    }
}

// ── LineEdit Frame ─────────────────────────────────────────────────────────

void AnProxyStyle::drawLineEditFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    // Don't draw frames for line edits embedded in spin boxes or combo boxes
    if (widget)
    {
        if (auto* parent = widget->parentWidget())
        {
            if (qobject_cast<QAbstractSpinBox*>(parent) || qobject_cast<QComboBox*>(parent))
            {
                return;
            }
        }
    }

    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const auto& c = m_tokens->colors;
    const auto r = m_tokens->radius.sm;

    const bool enabled = isEnabled(option);
    const bool focused = isFocused(option);

    const QColor bgColor = enabled ? c.bgInput : c.bgSurface;
    const QColor borderColor = focused ? c.borderFocus : c.borderSubtle;

    painter->setPen(QPen(borderColor, 1));
    painter->setBrush(bgColor);
    const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(rect, r, r);
}

// ── ToolTip ────────────────────────────────────────────────────────────────

void AnProxyStyle::drawToolTip(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    QMargins margins(0, 0, 3, 3);
    if (widget)
    {
        margins = widget->contentsMargins();
    }

    const QRectF bgRect = QRectF(option->rect).marginsRemoved(margins).adjusted(0.5, 0.5, -0.5, -0.5);

    // Draw shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_tokens->colors.shadowOverlay);
    painter->drawRoundedRect(bgRect.translated(3, 3), m_tokens->radius.sm, m_tokens->radius.sm);

    // Draw background
    painter->setPen(QPen(m_tokens->colors.borderPrimary, 1));
    painter->setBrush(m_tokens->colors.bgCardPrimary);
    painter->drawRoundedRect(bgRect, m_tokens->radius.sm, m_tokens->radius.sm);
}

// ── Splitter Handle ────────────────────────────────────────────────────────

void AnProxyStyle::drawSplitterHandle(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PainterGuard guard(painter);

    const bool isProcessSplitter = hasProperty(widget, "anSplitterVariant", "process") ||
                                   hasParentProperty(widget, "anSplitterVariant", "process");

    if (!isProcessSplitter)
    {
        const QColor color = isHovered(option) ? m_tokens->colors.borderFocus : m_tokens->colors.borderSubtle;
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRect(option->rect);
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool hovered = isHovered(option);
    const bool verticalHandle = option->rect.height() > option->rect.width();
    const QColor grooveColor = hovered ? m_tokens->colors.borderFocus : m_tokens->colors.borderPrimary;

    if (hovered)
    {
        QColor hoverFill = m_tokens->colors.borderFocus;
        hoverFill.setAlpha(20);
        painter->setPen(Qt::NoPen);
        painter->setBrush(hoverFill);
        painter->drawRoundedRect(QRectF(option->rect).adjusted(1.0, 1.0, -1.0, -1.0), 4.0, 4.0);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(grooveColor);

    constexpr int grooveThickness = 2;
    if (verticalHandle)
    {
        const int x = option->rect.center().x() - (grooveThickness / 2);
        painter->drawRoundedRect(QRectF(x, option->rect.top() + 6, grooveThickness, option->rect.height() - 12), 1.0,
                                 1.0);
    }
    else
    {
        const int y = option->rect.center().y() - (grooveThickness / 2);
        painter->drawRoundedRect(QRectF(option->rect.left() + 6, y, option->rect.width() - 12, grooveThickness), 1.0,
                                 1.0);
    }
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
