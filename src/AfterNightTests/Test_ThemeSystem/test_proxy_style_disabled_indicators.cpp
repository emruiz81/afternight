// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include <cstdlib>
#include <cmath>
#include <functional>
#include <QAbstractButton>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPoint>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionFrame>
#include <QStyleOptionToolBar>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

namespace
{

using an::ui::testing::layered;

/// The opacity the proxy style paints a disabled control at, whole.
constexpr qreal kDisabledOpacity = 0.38;

bool colorNear(const QColor& actual, const QColor& expected, const int tolerance = 3)
{
    return std::abs(actual.red() - expected.red()) <= tolerance &&
           std::abs(actual.green() - expected.green()) <= tolerance &&
           std::abs(actual.blue() - expected.blue()) <= tolerance &&
           std::abs(actual.alpha() - expected.alpha()) <= tolerance;
}

/// Pixels of a rendered indicator that the style paints whole in one colour: the centre, inside a check box's fill
/// clear of its mark, on the mark's second stroke, on a check box's border, on a radio button's ring, and between the
/// ring and the dot.
constexpr QPoint kCentre(10, 10);
constexpr QPoint kCheckFill(4, 4);
constexpr QPoint kCheckMark(11, 10);
constexpr QPoint kCheckBorder(2, 10);
constexpr QPoint kRadioRing(3, 9);
constexpr QPoint kRadioGap(6, 6);

/// The surface the indicators are drawn on: the panel surface a control usually sits on.
QColor panelSurface()
{
    return an::ui::tokens().layers.surface1;
}

/// Runs a check under the dark theme and under the light one, and puts the theme preference back.
void forEachTheme(const std::function<void()>& check)
{
    const an::ui::ThemePreference original = an::ui::themePreference();
    for (const an::ui::ThemePreference theme : {an::ui::ThemePreference::Dark, an::ui::ThemePreference::Light})
    {
        SCOPED_TRACE(theme == an::ui::ThemePreference::Dark ? "dark" : "light");
        an::ui::setThemePreference(theme);
        check();
    }
    an::ui::setThemePreference(original);
    an::ui::refreshTokens();
}

/// An indicator drawn in a state on a twenty-pixel square of the surface, its sixteen-pixel box centred on (10, 10).
QImage renderPrimitive(const QStyle::PrimitiveElement element, const QStyle::State state, const QColor& surface)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    EXPECT_NE(style, nullptr);
    if (!style)
    {
        return QImage();
    }

    style->refreshThemeTokens();

    QStyleOptionButton option;
    option.rect = QRect(0, 0, 20, 20);
    option.state = state;

    QImage image(20, 20, QImage::Format_ARGB32_Premultiplied);
    image.fill(surface);

    QPainter painter(&image);
    style->drawPrimitive(element, &option, &painter, nullptr);
    painter.end();

    return image;
}

bool imageContainsColor(const QImage& image, const QColor& expected, const int tolerance = 6, const int minMatches = 2)
{
    int matchCount = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (colorNear(image.pixelColor(x, y), expected, tolerance))
            {
                ++matchCount;
                if (matchCount >= minMatches)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

QColor resolvedCardSurfaceColorForCurrentTokens()
{
    const auto& tokenSet = an::ui::tokens();
    QColor surface = tokenSet.materials.acrylicSurface;
    if (tokenSet.colors.cardSurfaceTint.isValid())
    {
        surface =
            QColor(static_cast<int>(surface.red() + (tokenSet.colors.cardSurfaceTint.red() - surface.red()) *
                                                        (an::ui::isDarkTheme() ? 0.56 : 0.44)),
                   static_cast<int>(surface.green() + (tokenSet.colors.cardSurfaceTint.green() - surface.green()) *
                                                          (an::ui::isDarkTheme() ? 0.56 : 0.44)),
                   static_cast<int>(surface.blue() + (tokenSet.colors.cardSurfaceTint.blue() - surface.blue()) *
                                                         (an::ui::isDarkTheme() ? 0.56 : 0.44)),
                   surface.alpha());
        if (an::ui::isDarkTheme())
        {
            surface =
                QColor(static_cast<int>(surface.red() * (1.0 - 0.22)), static_cast<int>(surface.green() * (1.0 - 0.22)),
                       static_cast<int>(surface.blue() * (1.0 - 0.22)), surface.alpha());
        }
    }

    if (surface.alpha() < 255)
    {
        const QColor base = tokenSet.materials.micaSurface;
        const double a = static_cast<double>(surface.alpha()) / 255.0;
        surface = QColor(static_cast<int>(std::round(surface.red() * a + base.red() * (1.0 - a))),
                         static_cast<int>(std::round(surface.green() * a + base.green() * (1.0 - a))),
                         static_cast<int>(std::round(surface.blue() * a + base.blue() * (1.0 - a))), 255);
    }

    return surface;
}

} // namespace

// A disabled check box is its enabled look painted whole at the disabled opacity, in both themes: checked, the accent's
// fill and the mark in the text on the accent, each laid over what is under it at 38 %; unchecked, the border in the
// secondary text at 38 % with the surface left inside. Neither shows a colour at its full strength.
TEST(ThemeSystemProxyStyle, DisabledCheckbox_UsesDisabledTokenColors)
{
    forEachTheme([] {
        const auto& colors = an::ui::tokens().colors;
        const QColor surface = panelSurface();
        const QImage checked = renderPrimitive(QStyle::PE_IndicatorCheckBox, QStyle::State_On, surface);
        const QImage unchecked = renderPrimitive(QStyle::PE_IndicatorCheckBox, QStyle::State_None, surface);

        const QColor fill = layered(colors.accentPrimary, kDisabledOpacity, surface);
        const QColor mark = layered(colors.textOnAccent, kDisabledOpacity, fill);
        EXPECT_TRUE(imageContainsColor(checked, fill));
        EXPECT_TRUE(colorNear(checked.pixelColor(kCheckFill), fill)) << "the fill";
        EXPECT_TRUE(imageContainsColor(checked, mark));
        EXPECT_TRUE(colorNear(checked.pixelColor(kCheckMark), mark)) << "the mark";
        EXPECT_FALSE(imageContainsColor(checked, colors.accentPrimary));

        const QColor border = layered(colors.textSecondary, kDisabledOpacity, surface);
        EXPECT_TRUE(imageContainsColor(unchecked, border));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCheckBorder), border)) << "the border";
        EXPECT_FALSE(imageContainsColor(unchecked, colors.textSecondary));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCentre), surface));
    });
}

// A disabled radio button is its enabled look painted whole at the disabled opacity, in both themes: checked, the
// accent's ring and dot laid over the surface at 38 %; unchecked, the ring in the secondary text at 38 % with the
// surface left inside. Neither shows a colour at its full strength.
TEST(ThemeSystemProxyStyle, DisabledRadio_UsesDisabledTokenColors)
{
    forEachTheme([] {
        const auto& colors = an::ui::tokens().colors;
        const QColor surface = panelSurface();
        const QImage checked = renderPrimitive(QStyle::PE_IndicatorRadioButton, QStyle::State_On, surface);
        const QImage unchecked = renderPrimitive(QStyle::PE_IndicatorRadioButton, QStyle::State_None, surface);

        const QColor accent = layered(colors.accentPrimary, kDisabledOpacity, surface);
        EXPECT_TRUE(imageContainsColor(checked, accent));
        EXPECT_TRUE(colorNear(checked.pixelColor(kRadioRing), accent)) << "the ring";
        EXPECT_TRUE(colorNear(checked.pixelColor(kCentre), accent)) << "the dot";
        EXPECT_FALSE(imageContainsColor(checked, colors.accentPrimary));

        const QColor ring = layered(colors.textSecondary, kDisabledOpacity, surface);
        EXPECT_TRUE(imageContainsColor(unchecked, ring));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kRadioRing), ring)) << "the ring";
        EXPECT_FALSE(imageContainsColor(unchecked, colors.textSecondary));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCentre), surface));
    });
}

// An enabled check box, in both themes: checked, filled with the accent under a mark in the text on the accent;
// unchecked, a border in the secondary text with the surface left inside.
TEST(ThemeSystemProxyStyle, EnabledCheckbox_UsesEnabledTokenColors)
{
    forEachTheme([] {
        const auto& colors = an::ui::tokens().colors;
        const QColor surface = panelSurface();
        const QImage checked =
            renderPrimitive(QStyle::PE_IndicatorCheckBox, QStyle::State_On | QStyle::State_Enabled, surface);
        const QImage unchecked = renderPrimitive(QStyle::PE_IndicatorCheckBox, QStyle::State_Enabled, surface);

        EXPECT_TRUE(imageContainsColor(checked, colors.accentPrimary));
        EXPECT_TRUE(colorNear(checked.pixelColor(kCheckFill), colors.accentPrimary)) << "the fill";
        EXPECT_TRUE(imageContainsColor(checked, colors.textOnAccent));
        EXPECT_TRUE(colorNear(checked.pixelColor(kCheckMark), colors.textOnAccent)) << "the mark";
        EXPECT_TRUE(imageContainsColor(unchecked, colors.textSecondary));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCheckBorder), colors.textSecondary)) << "the border";
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCentre), surface));
    });
}

// An enabled radio button, in both themes: checked, an accent ring around an accent dot with the surface between them;
// unchecked, a ring in the secondary text with the surface left inside.
TEST(ThemeSystemProxyStyle, EnabledRadio_UsesEnabledTokenColors)
{
    forEachTheme([] {
        const auto& colors = an::ui::tokens().colors;
        const QColor surface = panelSurface();
        const QImage checked =
            renderPrimitive(QStyle::PE_IndicatorRadioButton, QStyle::State_On | QStyle::State_Enabled, surface);
        const QImage unchecked = renderPrimitive(QStyle::PE_IndicatorRadioButton, QStyle::State_Enabled, surface);

        EXPECT_TRUE(imageContainsColor(checked, colors.accentPrimary));
        EXPECT_TRUE(colorNear(checked.pixelColor(kRadioRing), colors.accentPrimary)) << "the ring";
        EXPECT_TRUE(colorNear(checked.pixelColor(kCentre), colors.accentPrimary)) << "the dot";
        EXPECT_TRUE(colorNear(checked.pixelColor(kRadioGap), surface)) << "between the ring and the dot";
        EXPECT_TRUE(imageContainsColor(unchecked, colors.textSecondary));
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kRadioRing), colors.textSecondary)) << "the ring";
        EXPECT_TRUE(colorNear(unchecked.pixelColor(kCentre), surface));
    });
}

TEST(ThemeSystemProxyStyle, LayoutPixelMetricsUseSpacingTokens)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    const auto& spacing = an::ui::tokens().spacing;
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing), spacing.sm);
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutVerticalSpacing), spacing.sm);
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutLeftMargin), spacing.md);
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutTopMargin), spacing.md);
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutRightMargin), spacing.md);
    EXPECT_EQ(style->pixelMetric(QStyle::PM_LayoutBottomMargin), spacing.md);
}

TEST(ThemeSystemProxyStyle, PolishPaletteUsesLayerAndMaterialFallbackTokens)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QPalette palette;
    style->polish(palette);

    const auto& tokenSet = an::ui::tokens();
    EXPECT_EQ(palette.color(QPalette::Window), tokenSet.materials.micaSurface);
    EXPECT_EQ(palette.color(QPalette::Base), tokenSet.layers.surface1);
    EXPECT_EQ(palette.color(QPalette::AlternateBase), tokenSet.materials.acrylicSurface);
    EXPECT_EQ(palette.color(QPalette::Button), tokenSet.layers.surface2);
    EXPECT_EQ(palette.color(QPalette::ToolTipBase), tokenSet.materials.acrylicSurface);
}

TEST(ThemeSystemProxyStyle, PolishNormalizesLayoutSpacingToTokenValues)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QWidget host;
    auto* rootLayout = new QVBoxLayout(&host);
    rootLayout->setSpacing(10);
    rootLayout->setContentsMargins(10, 14, 10, 20);

    auto* nestedLayout = new QGridLayout();
    nestedLayout->setHorizontalSpacing(9);
    nestedLayout->setVerticalSpacing(14);
    nestedLayout->setContentsMargins(6, 7, 8, 9);
    rootLayout->addLayout(nestedLayout);

    style->polish(&host);

    const auto& spacing = an::ui::tokens().spacing;
    EXPECT_EQ(rootLayout->spacing(), spacing.md);
    EXPECT_EQ(rootLayout->contentsMargins(), QMargins(spacing.md, spacing.lg, spacing.md, spacing.xl));
    EXPECT_EQ(nestedLayout->horizontalSpacing(), spacing.sm);
    EXPECT_EQ(nestedLayout->verticalSpacing(), spacing.lg);
    EXPECT_EQ(nestedLayout->contentsMargins(), QMargins(spacing.sm, spacing.sm, spacing.sm, spacing.sm));
}

TEST(ThemeSystemProxyStyle, AnchoredComboPopupDisablesSelectedItemCenteredPopup)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);

    QComboBox combo;
    combo.setProperty("anComboPopupAnchored", "true");

    EXPECT_EQ(style->styleHint(QStyle::SH_ComboBox_Popup, nullptr, &combo), 0);
}

// A process window's header is a window's heading: polish gives its title and its description the fonts it gives a
// window heading's title and description - the title semibold and larger than the body, the description the small text
// at the body's weight, in the secondary text.
TEST(ThemeSystemProxyStyle, PolishAppliesTypographyRolesToHeaderLabels)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QLabel processTitle;
    processTitle.setObjectName(QStringLiteral("anProcessShellHeaderTitle"));

    QLabel processDescription;
    processDescription.setObjectName(QStringLiteral("anProcessShellHeaderDescription"));

    QLabel headingTitle;
    headingTitle.setObjectName(QStringLiteral("anWindowHeadingTitle"));

    QLabel headingDescription;
    headingDescription.setObjectName(QStringLiteral("anWindowHeadingDescription"));

    style->polish(&processTitle);
    style->polish(&processDescription);
    style->polish(&headingTitle);
    style->polish(&headingDescription);

    const auto& tokenSet = an::ui::tokens();
    const auto& typography = tokenSet.typography;
    EXPECT_DOUBLE_EQ(processTitle.font().pointSizeF(), headingTitle.font().pointSizeF());
    EXPECT_EQ(processTitle.font().weight(), headingTitle.font().weight());
    EXPECT_EQ(processTitle.font().weight(), QFont::DemiBold);
    EXPECT_GT(processTitle.font().pointSizeF(), typography.body.pointSizeF());

    EXPECT_DOUBLE_EQ(processDescription.font().pointSizeF(), headingDescription.font().pointSizeF());
    EXPECT_DOUBLE_EQ(processDescription.font().pointSizeF(), tokenSet.metrics.fontSmall * 72.0 / 96.0);
    EXPECT_EQ(processDescription.font().weight(), typography.body.weight());
    EXPECT_EQ(processDescription.palette().color(QPalette::Active, QPalette::WindowText),
              tokenSet.colors.textSecondary);
}

TEST(ThemeSystemProxyStyle, PolishAppliesHeadingTypographyToTableHeaders)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QTableWidget table(2, 2);
    style->polish(static_cast<QWidget*>(table.horizontalHeader()));

    const auto& typography = an::ui::tokens().typography;
    EXPECT_EQ(table.horizontalHeader()->font().pointSize(), typography.heading3.pointSize());
    EXPECT_EQ(table.horizontalHeader()->font().weight(), typography.heading3.weight());
}

TEST(ThemeSystemProxyStyle, PolishAssignsSemanticRolesToDialogButtonBoxButtons)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QDialogButtonBox buttonBox(QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Reset |
                               QDialogButtonBox::Discard);
    style->polish(static_cast<QWidget*>(&buttonBox));

    auto* yesButton = buttonBox.button(QDialogButtonBox::Yes);
    auto* noButton = buttonBox.button(QDialogButtonBox::No);
    auto* resetButton = buttonBox.button(QDialogButtonBox::Reset);
    auto* discardButton = buttonBox.button(QDialogButtonBox::Discard);

    ASSERT_NE(yesButton, nullptr);
    ASSERT_NE(noButton, nullptr);
    ASSERT_NE(resetButton, nullptr);
    ASSERT_NE(discardButton, nullptr);

    EXPECT_EQ(yesButton->property("anProcessButtonRole").toString(), QStringLiteral("primary"));
    EXPECT_EQ(noButton->property("anProcessButtonRole").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(resetButton->property("anProcessButtonRole").toString(), QStringLiteral("neutral"));
    EXPECT_EQ(discardButton->property("anProcessButtonRole").toString(), QStringLiteral("danger"));
}

TEST(ThemeSystemProxyStyle, PolishAssignsMessageBoxIdentityAndButtonRoles)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Delete item"));
    box.setText(QStringLiteral("This action is irreversible."));
    box.setInformativeText(QStringLiteral("This cannot be undone."));
    box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
    style->polish(static_cast<QWidget*>(&box));

    EXPECT_EQ(box.objectName(), QStringLiteral("anMessageBox"));
    EXPECT_EQ(box.property("anDialogVariant").toString(), QStringLiteral("messageBox"));

    auto* discardButton = box.button(QMessageBox::Discard);
    auto* cancelButton = box.button(QMessageBox::Cancel);
    ASSERT_NE(discardButton, nullptr);
    ASSERT_NE(cancelButton, nullptr);

    EXPECT_EQ(discardButton->property("anProcessButtonRole").toString(), QStringLiteral("danger"));
    EXPECT_EQ(cancelButton->property("anProcessButtonRole").toString(), QStringLiteral("secondary"));
    EXPECT_EQ(box.findChild<QWidget*>(QStringLiteral("anMessageBoxCard")), nullptr);
}

TEST(ThemeSystemProxyStyle, CardTintToolbarMatchesProcessShellCardFill)
{
    an::ui::AnProxyStyle* style = an::ui::installedAnProxyStyle();
    if (!style)
    {
        style = an::ui::installAnProxyStyle();
    }

    ASSERT_NE(style, nullptr);
    style->refreshThemeTokens();

    QToolBar toolbar;
    toolbar.setProperty("anUseCardBackground", true);
    QPalette toolbarPalette = toolbar.palette();
    const QColor toolbarFill = resolvedCardSurfaceColorForCurrentTokens();
    toolbarPalette.setColor(QPalette::Active, QPalette::Window, toolbarFill);
    toolbarPalette.setColor(QPalette::Inactive, QPalette::Window, toolbarFill);
    toolbarPalette.setColor(QPalette::Disabled, QPalette::Window, toolbarFill);
    toolbar.setPalette(toolbarPalette);

    QStyleOptionToolBar toolbarOption;
    toolbarOption.initFrom(&toolbar);
    toolbarOption.rect = QRect(0, 0, 80, 24);

    QImage toolbarImage(80, 24, QImage::Format_ARGB32_Premultiplied);
    toolbarImage.fill(Qt::transparent);
    {
        QPainter painter(&toolbarImage);
        style->drawControl(QStyle::CE_ToolBar, &toolbarOption, &painter, &toolbar);
    }

    QFrame processCard;
    processCard.setObjectName(QStringLiteral("anProcessShellCard"));
    processCard.setFrameShape(QFrame::StyledPanel);
    style->polish(&processCard);

    QStyleOptionFrame cardOption;
    cardOption.initFrom(&processCard);
    cardOption.rect = QRect(0, 0, 80, 24);

    QImage cardImage(80, 24, QImage::Format_ARGB32_Premultiplied);
    cardImage.fill(Qt::transparent);
    {
        QPainter painter(&cardImage);
        style->drawPrimitive(QStyle::PE_Frame, &cardOption, &painter, &processCard);
    }

    // Compare interior pixels (away from borders and anti-aliased corners).
    const QColor toolbarCenter = toolbarImage.pixelColor(40, 10);
    const QColor cardCenter = cardImage.pixelColor(40, 10);

    EXPECT_TRUE(colorNear(toolbarCenter, cardCenter, 0))
        << "toolbar=" << toolbarCenter.name(QColor::HexArgb).toStdString()
        << " card=" << cardCenter.name(QColor::HexArgb).toStdString();
}
