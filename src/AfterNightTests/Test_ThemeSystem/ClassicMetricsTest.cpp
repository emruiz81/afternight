// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The sizes the proxy style gives under the Classic profile, as they are recorded: the pixel metrics it answers itself
// and the ones the Observatory profile answers, which Classic leaves to the base style; the sizes it gives the controls
// a density sizes, from their contents; the sub-controls of the spin box and the combo box. The Observatory profile
// answers sizes from its metric tokens; a change that reaches the Classic answers fails here, naming the entry.

#include "fixtures/BaselineRecord.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

#include <QAbstractSpinBox>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QRect>
#include <QSize>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QWidget>

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace
{

using an::tests::fixtures::baselineEntry;
using an::tests::fixtures::BaselineRecord;

constexpr const char* kRecordPath = "src/AfterNightTests/Test_ThemeSystem/data/ClassicMetrics.txt";

QStringList recordPreamble()
{
    return {
        QStringLiteral(
            "What the proxy style answers under the Classic profile: the pixel metrics it answers itself and"),
        QStringLiteral(
            "the ones the Observatory profile answers, which Classic leaves to the base style; the sizes it"),
        QStringLiteral("gives the controls a density sizes, from their contents; the sub-controls of the spin box and"),
        QStringLiteral("the combo box."),
        QString(),
        QStringLiteral("An entry is what is asked, the case - the widget asked about or the contents given - and the"),
        QStringLiteral("answer. A size that follows the font is stated against the font, which is the host's."),
        QString(),
        QStringLiteral("Density.ClassicMetricsAreUnchanged in Test_ThemeSystem compares the running style with this"),
        QStringLiteral("record under each density. An entry is edited by the change that means to move its value; the"),
        QStringLiteral("record is not written again whole."),
    };
}

QString sizeText(const QSize& size)
{
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

QString rectText(const QRect& rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

// A length against another: "plus 8", "less 2", or "itself".
QString against(const int value, const int reference)
{
    const int difference = value - reference;
    if (difference == 0)
    {
        return QStringLiteral("itself");
    }
    return QStringLiteral("%1 %2")
        .arg(difference > 0 ? QStringLiteral("plus") : QStringLiteral("less"))
        .arg(difference > 0 ? difference : -difference);
}

/// The widgets whose properties choose a variant of an answer.
struct VariantWidgets
{
    VariantWidgets()
    {
        compactTabBar.setProperty("anTabCompact", true);
        processTabBar.setProperty("anTabCompact", true);
        processTabBar.setProperty("anTabHeightScale", 0.85);
        processTabBar.setProperty("anTabMinWidth", 170);
        processTabBar.setProperty("anTabMinHeight", 18);
        dialogTabBar.setProperty("anTabCompact", true);
        dialogTabBar.setProperty("anTabHeightScale", 1.0);
        dialogTabBar.setProperty("anTabMinWidth", 170);
        dialogTabBar.setProperty("anTabMinHeight", 22);
        processSplitter.setProperty("anSplitterVariant", QStringLiteral("process"));
        processSplitter.addWidget(insideProcessSplitter);
        dialogButton = buttonBox.addButton(QDialogButtonBox::Ok);
    }

    QTabBar plainTabBar;
    QTabBar compactTabBar;
    QTabBar processTabBar;
    QTabBar dialogTabBar;
    QSplitter processSplitter;
    QWidget* insideProcessSplitter = new QWidget;
    QPushButton plainButton;
    QDialogButtonBox buttonBox;
    QPushButton* dialogButton = nullptr;
};

QStringList pixelMetricEntries(const QStyle& style, const VariantWidgets& widgets)
{
    const std::array<std::pair<QStyle::PixelMetric, const char*>, 28u> metrics{{
        {QStyle::PM_ButtonMargin, "PM_ButtonMargin"},
        {QStyle::PM_SpinBoxFrameWidth, "PM_SpinBoxFrameWidth"},
        {QStyle::PM_DefaultFrameWidth, "PM_DefaultFrameWidth"},
        {QStyle::PM_ScrollBarExtent, "PM_ScrollBarExtent"},
        {QStyle::PM_ScrollBarSliderMin, "PM_ScrollBarSliderMin"},
        {QStyle::PM_SliderLength, "PM_SliderLength"},
        {QStyle::PM_SliderThickness, "PM_SliderThickness"},
        {QStyle::PM_TabBarTabHSpace, "PM_TabBarTabHSpace"},
        {QStyle::PM_TabBarTabVSpace, "PM_TabBarTabVSpace"},
        {QStyle::PM_LayoutHorizontalSpacing, "PM_LayoutHorizontalSpacing"},
        {QStyle::PM_LayoutVerticalSpacing, "PM_LayoutVerticalSpacing"},
        {QStyle::PM_LayoutLeftMargin, "PM_LayoutLeftMargin"},
        {QStyle::PM_LayoutTopMargin, "PM_LayoutTopMargin"},
        {QStyle::PM_LayoutRightMargin, "PM_LayoutRightMargin"},
        {QStyle::PM_LayoutBottomMargin, "PM_LayoutBottomMargin"},
        {QStyle::PM_ToolBarIconSize, "PM_ToolBarIconSize"},
        {QStyle::PM_ToolBarFrameWidth, "PM_ToolBarFrameWidth"},
        {QStyle::PM_ToolBarHandleExtent, "PM_ToolBarHandleExtent"},
        {QStyle::PM_SplitterWidth, "PM_SplitterWidth"},
        {QStyle::PM_IndicatorWidth, "PM_IndicatorWidth"},
        {QStyle::PM_IndicatorHeight, "PM_IndicatorHeight"},
        {QStyle::PM_ExclusiveIndicatorWidth, "PM_ExclusiveIndicatorWidth"},
        {QStyle::PM_ExclusiveIndicatorHeight, "PM_ExclusiveIndicatorHeight"},
        {QStyle::PM_HeaderDefaultSectionSizeVertical, "PM_HeaderDefaultSectionSizeVertical"},
        {QStyle::PM_HeaderDefaultSectionSizeHorizontal, "PM_HeaderDefaultSectionSizeHorizontal"},
        {QStyle::PM_HeaderMargin, "PM_HeaderMargin"},
        {QStyle::PM_MenuHMargin, "PM_MenuHMargin"},
        {QStyle::PM_MenuVMargin, "PM_MenuVMargin"},
    }};
    QStringList entries;
    for (const auto& [metric, name] : metrics)
    {
        entries.append(baselineEntry({QString::fromLatin1(name), QStringLiteral("no widget"),
                                      QString::number(style.pixelMetric(metric, nullptr, nullptr))}));
    }
    // The answers a widget's properties choose.
    entries.append(baselineEntry(
        {QStringLiteral("PM_TabBarTabVSpace"), QStringLiteral("a compact tab bar"),
         QString::number(style.pixelMetric(QStyle::PM_TabBarTabVSpace, nullptr, &widgets.compactTabBar))}));
    entries.append(baselineEntry(
        {QStringLiteral("PM_SplitterWidth"), QStringLiteral("a process splitter"),
         QString::number(style.pixelMetric(QStyle::PM_SplitterWidth, nullptr, &widgets.processSplitter))}));
    entries.append(baselineEntry(
        {QStringLiteral("PM_SplitterWidth"), QStringLiteral("a widget in a process splitter"),
         QString::number(style.pixelMetric(QStyle::PM_SplitterWidth, nullptr, widgets.insideProcessSplitter))}));
    return entries;
}

QStringList sizeEntries(const QStyle& style, const VariantWidgets& widgets)
{
    QStringList entries;
    const std::array<std::pair<QSize, const char*>, 2u> contents{{
        {QSize(10, 10), "contents 10x10"},
        {QSize(200, 40), "contents 200x40"},
    }};
    const auto entry = [&entries](const char* type, const QString& kase, const QString& answer) {
        entries.append(baselineEntry({QString::fromLatin1(type), kase, answer}));
    };

    for (const auto& [size, sizeName] : contents)
    {
        const QString name = QString::fromLatin1(sizeName);

        QStyleOptionButton button;
        button.text = QStringLiteral("OK");
        entry("CT_PushButton", QStringLiteral("text, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_PushButton, &button, size, &widgets.plainButton)));
        entry("CT_PushButton", QStringLiteral("text in a dialog button box, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_PushButton, &button, size, widgets.dialogButton)));
        QStyleOptionButton iconOnly;
        entry("CT_PushButton", QStringLiteral("no text, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_PushButton, &iconOnly, size, &widgets.plainButton)));

        QStyleOptionToolButton toolButton;
        entry("CT_ToolButton", name, sizeText(style.sizeFromContents(QStyle::CT_ToolButton, &toolButton, size)));

        QStyleOptionFrame lineEdit;
        lineEdit.lineWidth = 1;
        entry("CT_LineEdit", name, sizeText(style.sizeFromContents(QStyle::CT_LineEdit, &lineEdit, size)));

        QStyleOptionComboBox comboBox;
        comboBox.frame = true;
        entry("CT_ComboBox", name, sizeText(style.sizeFromContents(QStyle::CT_ComboBox, &comboBox, size)));

        QStyleOptionSpinBox spinBox;
        spinBox.frame = true;
        spinBox.buttonSymbols = QAbstractSpinBox::UpDownArrows;
        entry("CT_SpinBox", name, sizeText(style.sizeFromContents(QStyle::CT_SpinBox, &spinBox, size)));

        QStyleOptionTab tab;
        tab.shape = QTabBar::RoundedNorth;
        tab.text = QStringLiteral("Tab");
        entry("CT_TabBarTab", QStringLiteral("a tab bar, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_TabBarTab, &tab, size, &widgets.plainTabBar)));
        entry("CT_TabBarTab", QStringLiteral("a process tab bar, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_TabBarTab, &tab, size, &widgets.processTabBar)));
        entry("CT_TabBarTab", QStringLiteral("a dialog tab bar, ") + name,
              sizeText(style.sizeFromContents(QStyle::CT_TabBarTab, &tab, size, &widgets.dialogTabBar)));
    }

    QStyleOptionViewItem item;
    item.features = QStyleOptionViewItem::HasDecoration;
    item.decorationSize = QSize(16, 16);
    entry("CT_ItemViewItem", QStringLiteral("a 16x16 decoration"),
          sizeText(style.sizeFromContents(QStyle::CT_ItemViewItem, &item, QSize())));

    for (const Qt::Orientation orientation : {Qt::Horizontal, Qt::Vertical})
    {
        QStyleOptionSlider scrollBar;
        scrollBar.orientation = orientation;
        const bool horizontal = orientation == Qt::Horizontal;
        entry("CT_ScrollBar",
              horizontal ? QStringLiteral("horizontal, contents 50x12") : QStringLiteral("vertical, contents 12x50"),
              sizeText(style.sizeFromContents(QStyle::CT_ScrollBar, &scrollBar,
                                              horizontal ? QSize(50, 12) : QSize(12, 50))));
    }

    // Sized by their text: stated against the text's size in the option's font.
    QStyleOptionHeader header;
    header.text = QStringLiteral("Name");
    header.orientation = Qt::Horizontal;
    const QSize headerText = header.fontMetrics.size(0, header.text);
    const QSize headerSize = style.sizeFromContents(QStyle::CT_HeaderSection, &header, QSize());
    entry("CT_HeaderSection", QStringLiteral("the text Name"),
          QStringLiteral("the text's width %1, its height %2")
              .arg(against(headerSize.width(), headerText.width()), against(headerSize.height(), headerText.height())));

    QStyleOptionMenuItem menuItem;
    menuItem.menuItemType = QStyleOptionMenuItem::Normal;
    menuItem.text = QStringLiteral("Open");
    const int fontHeight = menuItem.fontMetrics.height();
    const QSize menuItemSize = style.sizeFromContents(QStyle::CT_MenuItem, &menuItem, QSize(300, fontHeight));
    entry("CT_MenuItem", QStringLiteral("the text Open, contents 300 wide and the font's height"),
          QStringLiteral("%1 wide, the font's height %2")
              .arg(menuItemSize.width())
              .arg(against(menuItemSize.height(), fontHeight)));
    return entries;
}

QStringList subControlEntries(const QStyle& style)
{
    QStringList entries;

    QStyleOptionSpinBox spinBox;
    spinBox.rect = QRect(0, 0, 120, 24);
    spinBox.frame = true;
    const std::array<std::pair<QStyle::SubControl, const char*>, 4u> spinBoxParts{{
        {QStyle::SC_SpinBoxUp, "SC_SpinBoxUp"},
        {QStyle::SC_SpinBoxDown, "SC_SpinBoxDown"},
        {QStyle::SC_SpinBoxEditField, "SC_SpinBoxEditField"},
        {QStyle::SC_SpinBoxFrame, "SC_SpinBoxFrame"},
    }};
    for (const auto& [part, name] : spinBoxParts)
    {
        entries.append(baselineEntry({QString::fromLatin1(name), QStringLiteral("a spin box of 120x24"),
                                      rectText(style.subControlRect(QStyle::CC_SpinBox, &spinBox, part))}));
    }

    QStyleOptionComboBox comboBox;
    comboBox.rect = QRect(0, 0, 160, 24);
    comboBox.frame = true;
    const std::array<std::pair<QStyle::SubControl, const char*>, 4u> comboBoxParts{{
        {QStyle::SC_ComboBoxArrow, "SC_ComboBoxArrow"},
        {QStyle::SC_ComboBoxEditField, "SC_ComboBoxEditField"},
        {QStyle::SC_ComboBoxFrame, "SC_ComboBoxFrame"},
        {QStyle::SC_ComboBoxListBoxPopup, "SC_ComboBoxListBoxPopup"},
    }};
    for (const auto& [part, name] : comboBoxParts)
    {
        entries.append(baselineEntry({QString::fromLatin1(name), QStringLiteral("a combo box of 160x24"),
                                      rectText(style.subControlRect(QStyle::CC_ComboBox, &comboBox, part))}));
    }
    return entries;
}

} // namespace

// What the proxy style answers under the Classic profile is the record taken before the metric tokens existed, under
// either density.
TEST(Density, ClassicMetricsAreUnchanged)
{
    const an::ui::testing::ScopedStyleProfile classic(an::ui::StyleProfile::Classic);
    const an::ui::ThemeDensity densityBefore = an::ui::themeDensity();
    const VariantWidgets widgets;
    BaselineRecord record(QString::fromLatin1(kRecordPath), recordPreamble());

    for (const an::ui::ThemeDensity density : {an::ui::ThemeDensity::Compact, an::ui::ThemeDensity::Comfortable})
    {
        SCOPED_TRACE(density == an::ui::ThemeDensity::Compact ? "compact" : "comfortable");
        an::ui::setThemeDensity(density);
        const an::ui::AnProxyStyle style;
        EXPECT_TRUE(record.matches(QStringLiteral("pixel metrics"), pixelMetricEntries(style, widgets)));
        EXPECT_TRUE(record.matches(QStringLiteral("sizes from contents"), sizeEntries(style, widgets)));
        EXPECT_TRUE(record.matches(QStringLiteral("sub-controls"), subControlEntries(style)));
    }
    an::ui::setThemeDensity(densityBefore);
    EXPECT_EQ(record.sections().size(), 3) << "the record holds a section the case no longer compares";
}
