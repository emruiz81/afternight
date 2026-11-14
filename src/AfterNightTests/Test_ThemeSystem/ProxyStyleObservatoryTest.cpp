// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The Observatory look of the standard widgets: what the proxy style paints under the Observatory profile, one case per
// family of primitives, each run under dark, light and a tinted preset at both densities and painted at four device
// pixel ratios. Every expected colour is read from the tokens in force, so one case covers every look; a text's colour
// is read from what the painting asked for, never from glyph pixels. Under the Classic profile the style paints through
// its own code alone, which the dispatch case proves with a counter.

#include "fixtures/ObservatoryLook.h"
#include "fixtures/PaintRecorder.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"
#include "theme/Elevation.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

namespace
{

using an::ui::ColorTokens;
using an::ui::DesignTokens;
using an::ui::StyleProfile;
using an::ui::testing::colourName;
using an::ui::testing::kRenderRatios;
using an::ui::testing::layered;
using an::ui::testing::ObservatoryLook;
using an::ui::testing::observatoryLooks;
using an::ui::testing::ObservatoryStyleScope;
using an::ui::testing::PaintRecorder;
using an::ui::testing::RecordedText;
using an::ui::testing::RenderedPicture;
using an::ui::testing::sameColour;

/// A widget whose protected initStyleOption a case calls, to paint the widget's own option in a state of its choice.
template <typename WidgetT> class Probed : public WidgetT
{
public:
    using WidgetT::initStyleOption;
    using WidgetT::WidgetT;
};

/// The opacity a disabled control is painted at.
constexpr qreal kDisabled = 0.38;

/// The state layer's share over a surface when hovered and when pressed.
constexpr qreal kHover = 0.08;
constexpr qreal kPressed = 0.12;

/// Runs a check under every look, the style installed under the Observatory profile; widgets the check makes are made
/// in the look they are painted in.
void forEachLook(const std::function<void(ObservatoryStyleScope&, const DesignTokens&)>& check)
{
    ObservatoryStyleScope scope(observatoryLooks()[0]);
    for (const ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        scope.setLook(look);
        check(scope, an::ui::tokens());
    }
}

/// Expects a colour, saying where it was read and what was there.
void expectColour(const QColor& actual, const QColor& expected, const std::string& where, const int tolerance = 2)
{
    EXPECT_TRUE(sameColour(actual, expected, tolerance))
        << where << ": " << colourName(actual) << ", expected " << colourName(expected);
}

/// The background a case paints over: the panel surface a control usually sits on.
QColor panel(const DesignTokens& t)
{
    return t.layers.surface1;
}

/// What a painting drew as text, painting it again on a recorder.
std::vector<RecordedText> textsOf(const QSize size, const std::function<void(QPainter&)>& paint)
{
    PaintRecorder recorder(size);
    {
        QPainter painter(&recorder);
        paint(painter);
    }
    return recorder.texts();
}

/// The first run of recorded text holding a part, in any case, or a run with no text that fails the case.
RecordedText textHolding(const std::vector<RecordedText>& texts, const QString& part)
{
    for (const RecordedText& text : texts)
    {
        if (text.text.contains(part, Qt::CaseInsensitive))
        {
            return text;
        }
    }
    ADD_FAILURE() << "no text holding '" << part.toStdString() << "' was painted";
    return RecordedText{};
}

/// A font's size in logical pixels: the pixel size it states, or its point size at 96 dots per inch.
int logicalPixels(const QFont& font)
{
    return font.pixelSize() > 0 ? font.pixelSize() : qRound(font.pointSizeF() * 96.0 / 72.0);
}

/// Pumps the event loop until `holds` answers true or a second has passed.
bool eventually(const std::function<bool()>& holds)
{
    const QDeadlineTimer deadline(1000);
    while (!holds())
    {
        if (deadline.hasExpired())
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

/// The option a push button paints with, read from its public state: its text, its icon, its size.
QStyleOptionButton plainButtonOption(QPushButton& button)
{
    QStyleOptionButton option;
    option.initFrom(&button);
    option.text = button.text();
    option.icon = button.icon();
    option.iconSize = button.iconSize();
    return option;
}

/// A push button's option at a size, in a state the case adds.
QStyleOptionButton buttonOption(Probed<QPushButton>& button, const QStyle::State extra = {})
{
    QStyleOptionButton option;
    button.initStyleOption(&option);
    option.state |= extra;
    return option;
}

} // namespace

// A push button's five roles, each through the variant properties: primary is filled with the accent and written in the
// text on it, tonal with the accent's container and its text, outlined - the default - has the outline and no fill,
// quiet neither, destructive is filled with the error colour. Each is a control high with the controls' radius, its
// text semibold.
TEST(ProxyStyleObservatory, PushButtonRolesTakeTheirColoursFromTheTokens)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        struct Role
        {
            const char* name;
            const char* property;
            const char* value;
            QColor fill;
            QColor edge;
            QColor text;
        };
        const QColor onError =
            an::ui::bestContrastTextColor(c.statusError, c.textOnAccent, c.textPrimary, c.bgPrimary, 4.5);
        const std::vector<Role> roles{
            {"primary", "anButtonVariant", "primary", c.accentPrimary, c.accentPrimary, c.textOnAccent},
            {"primary, by the dialog role", "anProcessButtonRole", "primary", c.accentPrimary, c.accentPrimary,
             c.textOnAccent},
            {"tonal", "anButtonVariant", "tonal", c.accentContainer, c.accentContainer, c.textOnAccentContainer},
            {"outlined", nullptr, nullptr, bg, an::ui::compositeOver(c.borderPrimary, bg), c.textPrimary},
            {"outlined, by the dialog role", "anProcessButtonRole", "secondary", bg,
             an::ui::compositeOver(c.borderPrimary, bg), c.textPrimary},
            {"quiet", "anButtonVariant", "quiet", bg, bg, c.textAccent},
            {"quiet, by the dialog role", "anProcessButtonRole", "neutral", bg, bg, c.textAccent},
            {"destructive", "anButtonVariant", "destructive", c.statusError, c.statusError, onError},
            {"destructive, by the dialog role", "anProcessButtonRole", "danger", c.statusError, c.statusError, onError},
        };
        for (const Role& role : roles)
        {
            SCOPED_TRACE(role.name);
            Probed<QPushButton> button(QStringLiteral("OK"));
            if (role.property != nullptr)
            {
                button.setProperty(role.property, QString::fromLatin1(role.value));
            }
            button.resize(120, button.sizeHint().height());
            EXPECT_EQ(button.height(), t.metrics.control);
            const QStyleOptionButton option = buttonOption(button);
            const int h = button.height();
            for (const qreal ratio : kRenderRatios)
            {
                const RenderedPicture picture(button.size(), ratio, bg, [&](QPainter& p) {
                    scope.style()->drawControl(QStyle::CE_PushButton, &option, &p, &button);
                });
                expectColour(picture.at(6, h / 2.0), role.fill, "fill at ratio " + std::to_string(ratio));
                expectColour(picture.at(0.2, h / 2.0), role.edge, "edge at ratio " + std::to_string(ratio));
                // The controls' radius: the corner is the background, a few pixels in the shape is the fill.
                expectColour(picture.at(1.0, 1.0), bg, "corner at ratio " + std::to_string(ratio));
                expectColour(picture.at(4.5, 4.5), role.fill, "inside the corner at ratio " + std::to_string(ratio));
            }
            const RecordedText label = textHolding(
                textsOf(button.size(),
                        [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_PushButton, &option, &p, &button); }),
                QStringLiteral("OK"));
            expectColour(label.colour, role.text, "the text", 0);
            EXPECT_EQ(label.font.weight(), QFont::DemiBold);
        }
    });
}

// A push button's states: hovered and pressed lay the state colour over the fill at 8 and 12 %, a checked outlined
// button is tonal, a disabled button is painted whole at 38 % opacity, and a primary one hovers and presses in the
// accent's own tones.
TEST(ProxyStyleObservatory, PushButtonStatesLayerTheStateColour)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QPushButton> outlined(QStringLiteral("Reset"));
        outlined.resize(120, t.metrics.control);
        Probed<QPushButton> primary(QStringLiteral("Apply"));
        primary.setProperty("anButtonVariant", QStringLiteral("primary"));
        primary.resize(120, t.metrics.control);
        const auto fillOf = [&](Probed<QPushButton>& button, const QStyleOptionButton& option) {
            const RenderedPicture picture(button.size(), 1.0, bg, [&](QPainter& p) {
                scope.style()->drawControl(QStyle::CE_PushButton, &option, &p, &button);
            });
            return picture.at(6, button.height() / 2.0);
        };

        expectColour(fillOf(outlined, buttonOption(outlined, QStyle::State_MouseOver)),
                     layered(c.stateLayer, kHover, bg), "hovered");
        expectColour(fillOf(outlined, buttonOption(outlined, QStyle::State_Sunken)),
                     layered(c.stateLayer, kPressed, bg), "pressed");
        expectColour(fillOf(outlined, buttonOption(outlined, QStyle::State_On)), c.accentContainer, "checked");
        expectColour(fillOf(primary, buttonOption(primary, QStyle::State_MouseOver)), c.accentHover, "primary hovered");
        expectColour(fillOf(primary, buttonOption(primary, QStyle::State_Sunken)), c.accentPressed, "primary pressed");

        QStyleOptionButton disabled = buttonOption(primary);
        disabled.state &= ~QStyle::State_Enabled;
        expectColour(fillOf(primary, disabled), layered(c.accentPrimary, kDisabled, bg), "primary disabled");
        const RecordedText label = textHolding(
            textsOf(primary.size(),
                    [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_PushButton, &disabled, &p, &primary); }),
            QStringLiteral("Apply"));
        EXPECT_NEAR(label.opacity, kDisabled, 0.01) << "a disabled button's text is painted at its opacity";
        expectColour(label.colour, c.textOnAccent, "a disabled button keeps its text colour", 0);
    });
}

// The focus ring - two pixels of the focus colour - is drawn only for focus that came from the keyboard: a button
// clicked into focus shows none, one tabbed to shows it. So are a check box's and a tab's.
TEST(ProxyStyleObservatory, FocusRingOnlyForKeyboardFocus)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QPushButton> button(QStringLiteral("Apply"));
        button.resize(120, t.metrics.control);
        for (const qreal ratio : kRenderRatios)
        {
            const auto paint = [&](const QStyle::State state) {
                const QStyleOptionButton option = buttonOption(button, state);
                return RenderedPicture(button.size(), ratio, bg, [&](QPainter& p) {
                    scope.style()->drawControl(QStyle::CE_PushButton, &option, &p, &button);
                });
            };
            const RenderedPicture clicked = paint(QStyle::State_HasFocus);
            expectColour(clicked.at(2.5, button.height() / 2.0), bg, "focus from a click");
            const RenderedPicture tabbed = paint(QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
            expectColour(tabbed.at(0.2, button.height() / 2.0), c.borderFocus, "the ring's outer pixel");
            expectColour(tabbed.at(1.5, button.height() / 2.0), c.borderFocus, "the ring's inner pixel");
        }

        // A check box's and a tab's focus frame.
        Probed<QCheckBox> check(QStringLiteral("Linked"));
        check.resize(check.sizeHint());
        QStyleOptionFocusRect frame;
        frame.initFrom(&check);
        frame.rect = QRect(0, 0, 60, 20);
        frame.state |= QStyle::State_HasFocus;
        const RenderedPicture unframed(QSize(60, 20), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_FrameFocusRect, &frame, &p, &check);
        });
        expectColour(unframed.at(0.2, 10), bg, "a check box's frame without keyboard focus");
        frame.state |= QStyle::State_KeyboardFocusChange;
        const RenderedPicture framed(QSize(60, 20), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_FrameFocusRect, &frame, &p, &check);
        });
        expectColour(framed.at(0.2, 10), c.borderFocus, "a check box's frame with keyboard focus");
        expectColour(framed.at(30, 10), bg, "inside the frame");
    });
}

// A tool button is quiet until hovered: no fill at rest, the state colour hovered and pressed, the accent's container
// when checked, and its text in the container's text then.
TEST(ProxyStyleObservatory, ToolButtonIsQuietUntilHoveredAndTonalWhenChecked)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QToolButton> button;
        button.setText(QStringLiteral("Grid"));
        button.setToolButtonStyle(Qt::ToolButtonTextOnly);
        button.resize(80, t.metrics.control);
        const auto paint = [&](const QStyle::State state) {
            QStyleOptionToolButton option;
            button.initStyleOption(&option);
            option.state |= state;
            if (state & QStyle::State_Sunken)
            {
                // A button held down: its own part is the active one, as the button says when it is down.
                option.activeSubControls = QStyle::SC_ToolButton;
            }
            return RenderedPicture(button.size(), 1.0, bg, [&](QPainter& p) {
                scope.style()->drawComplexControl(QStyle::CC_ToolButton, &option, &p, &button);
            });
        };
        expectColour(paint(QStyle::State_Raised).at(4, 16), bg, "at rest");
        expectColour(paint(QStyle::State_Raised | QStyle::State_MouseOver).at(4, 16), layered(c.stateLayer, kHover, bg),
                     "hovered");
        expectColour(paint(QStyle::State_Sunken).at(4, 16), layered(c.stateLayer, kPressed, bg), "pressed");
        expectColour(paint(QStyle::State_On).at(4, 16), c.accentContainer, "checked");

        QStyleOptionToolButton checked;
        button.initStyleOption(&checked);
        checked.state |= QStyle::State_On;
        const RecordedText label =
            textHolding(textsOf(button.size(),
                                [&](QPainter& p) {
                                    scope.style()->drawComplexControl(QStyle::CC_ToolButton, &checked, &p, &button);
                                }),
                        QStringLiteral("Grid"));
        expectColour(label.colour, c.textOnAccentContainer, "a checked tool button's text", 0);
    });
}

// A field - a line edit, a combo box, a spin box - is the third surface with a one-pixel outline and the controls'
// radius; focused, a two-pixel border in the focus colour; disabled, painted at 38 % opacity.
TEST(ProxyStyleObservatory, FieldsAreTheThirdSurfaceWithAnOutline)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        const QColor outline = an::ui::compositeOver(c.borderPrimary, c.bgInput);

        Probed<QLineEdit> edit;
        edit.resize(160, edit.sizeHint().height());
        EXPECT_EQ(edit.height(), t.metrics.control);
        Probed<QComboBox> combo;
        combo.addItem(QStringLiteral("Linear"));
        combo.resize(160, combo.sizeHint().height());
        EXPECT_EQ(combo.height(), t.metrics.control);
        Probed<QSpinBox> spin;
        spin.resize(160, spin.sizeHint().height());
        EXPECT_EQ(spin.height(), t.metrics.control);

        using Paint = std::function<void(QPainter&, QStyle::State)>;
        const std::vector<std::pair<const char*, Paint>> fields{
            {"line edit",
             [&](QPainter& p, const QStyle::State state) {
                 QStyleOptionFrame option;
                 edit.initStyleOption(&option);
                 option.state = (option.state & ~QStyle::State_Enabled) | state;
                 scope.style()->drawPrimitive(QStyle::PE_PanelLineEdit, &option, &p, &edit);
             }},
            {"combo box",
             [&](QPainter& p, const QStyle::State state) {
                 QStyleOptionComboBox option;
                 combo.initStyleOption(&option);
                 option.state = (option.state & ~QStyle::State_Enabled) | state;
                 scope.style()->drawComplexControl(QStyle::CC_ComboBox, &option, &p, &combo);
             }},
            {"spin box",
             [&](QPainter& p, const QStyle::State state) {
                 QStyleOptionSpinBox option;
                 spin.initStyleOption(&option);
                 option.state = (option.state & ~QStyle::State_Enabled) | state;
                 scope.style()->drawComplexControl(QStyle::CC_SpinBox, &option, &p, &spin);
             }},
        };
        const QSize size(160, t.metrics.control);
        for (const auto& [name, paint] : fields)
        {
            SCOPED_TRACE(name);
            for (const qreal ratio : kRenderRatios)
            {
                const RenderedPicture rest(size, ratio, bg, [&](QPainter& p) { paint(p, QStyle::State_Enabled); });
                expectColour(rest.at(4, size.height() / 2.0), c.bgInput, "fill");
                expectColour(rest.at(0.2, size.height() / 2.0), outline, "outline");
                expectColour(rest.at(2.5, size.height() / 2.0), c.bgInput, "inside the outline");
                expectColour(rest.at(1.0, 1.0), bg, "corner");
                const RenderedPicture focused(
                    size, ratio, bg, [&](QPainter& p) { paint(p, QStyle::State_Enabled | QStyle::State_HasFocus); });
                expectColour(focused.at(0.2, size.height() / 2.0), c.borderFocus, "focus, outer pixel");
                expectColour(focused.at(1.5, size.height() / 2.0), c.borderFocus, "focus, inner pixel");
            }
            const RenderedPicture disabled(size, 1.0, bg, [&](QPainter& p) { paint(p, QStyle::State_None); });
            expectColour(disabled.at(4, size.height() / 2.0), layered(c.bgInput, kDisabled, bg), "disabled");
        }
    });
}

// A combo box's arrow and a spin box's buttons have columns of their own inside the field, and the text field ends
// where they start; a hovered spin box button lays the state colour over its half.
TEST(ProxyStyleObservatory, ComboBoxAndSpinBoxArrowsHaveTheirColumns)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        Probed<QComboBox> combo;
        combo.addItem(QStringLiteral("Linear"));
        combo.resize(160, t.metrics.control);
        QStyleOptionComboBox comboOption;
        combo.initStyleOption(&comboOption);
        const QRect arrow =
            scope.style()->subControlRect(QStyle::CC_ComboBox, &comboOption, QStyle::SC_ComboBoxArrow, &combo);
        const QRect text =
            scope.style()->subControlRect(QStyle::CC_ComboBox, &comboOption, QStyle::SC_ComboBoxEditField, &combo);
        EXPECT_EQ(arrow.width(), 28);
        EXPECT_EQ(arrow.right(), combo.rect().right());
        EXPECT_EQ(text.right() + 1, arrow.left());
        EXPECT_EQ(text.left(), 11) << "the text starts past the border and the field's padding";

        Probed<QSpinBox> spin;
        spin.resize(160, t.metrics.control);
        QStyleOptionSpinBox spinOption;
        spin.initStyleOption(&spinOption);
        const QRect up = scope.style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxUp, &spin);
        const QRect down =
            scope.style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxDown, &spin);
        const QRect field =
            scope.style()->subControlRect(QStyle::CC_SpinBox, &spinOption, QStyle::SC_SpinBoxEditField, &spin);
        EXPECT_EQ(up.width(), 20);
        EXPECT_EQ(up.right(), spin.rect().right() - 1) << "inside the border";
        EXPECT_EQ(down.left(), up.left());
        EXPECT_EQ(up.bottom() + 1, down.top());
        EXPECT_LE(field.right(), up.left() - 1);

        spinOption.state |= QStyle::State_MouseOver;
        spinOption.activeSubControls = QStyle::SC_SpinBoxUp;
        const RenderedPicture hovered(spin.size(), 1.0, t.layers.surface1, [&](QPainter& p) {
            scope.style()->drawComplexControl(QStyle::CC_SpinBox, &spinOption, &p, &spin);
        });
        expectColour(hovered.at(up.left() + 3, up.top() + 3), layered(c.stateLayer, kHover, c.bgInput),
                     "the hovered button");
        expectColour(hovered.at(down.left() + 3, down.bottom() - 3), c.bgInput, "the other button");
    });
}

// A check box's indicator is sixteen pixels with a radius of four: unchecked, a two-pixel border in the secondary text;
// checked, filled with the accent. A radio button's is a circle of sixteen: checked, an accent ring and an accent dot.
TEST(ProxyStyleObservatory, CheckBoxAndRadioIndicators)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QCheckBox> check(QStringLiteral("Linked"));
        Probed<QRadioButton> radio(QStringLiteral("Mean"));
        const int extent = scope.style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, &check);
        const QSize size(extent, extent);
        const auto indicator = [&](QWidget& widget, const QStyle::PrimitiveElement element, const QStyle::State state,
                                   const qreal ratio) {
            QStyleOptionButton option;
            option.initFrom(&widget);
            option.rect = QRect(QPoint(0, 0), size);
            option.state = (option.state & ~(QStyle::State_On | QStyle::State_Off)) | state;
            return RenderedPicture(size, ratio, bg,
                                   [&](QPainter& p) { scope.style()->drawPrimitive(element, &option, &p, &widget); });
        };
        const qreal mid = extent / 2.0;
        const qreal edge = mid - 8.0;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture off = indicator(check, QStyle::PE_IndicatorCheckBox, QStyle::State_Off, ratio);
            expectColour(off.at(edge + 1.0, mid), c.textSecondary, "unchecked border");
            expectColour(off.at(mid, mid), bg, "unchecked inside");
            expectColour(off.at(edge + 0.2, edge + 0.2), bg, "corner");
            const RenderedPicture on = indicator(check, QStyle::PE_IndicatorCheckBox, QStyle::State_On, ratio);
            expectColour(on.at(mid, edge + 2.0), c.accentPrimary, "checked fill");
            expectColour(on.at(edge + 1.0, mid), c.accentPrimary, "checked edge");

            const RenderedPicture radioOff =
                indicator(radio, QStyle::PE_IndicatorRadioButton, QStyle::State_Off, ratio);
            expectColour(radioOff.at(edge + 1.0, mid), c.textSecondary, "radio border");
            expectColour(radioOff.at(mid, mid), bg, "radio inside");
            const RenderedPicture radioOn = indicator(radio, QStyle::PE_IndicatorRadioButton, QStyle::State_On, ratio);
            expectColour(radioOn.at(edge + 1.0, mid), c.accentPrimary, "radio ring");
            expectColour(radioOn.at(mid, mid), c.accentPrimary, "radio dot");
            expectColour(radioOn.at(mid, edge + 3.6), bg, "between the ring and the dot");
        }
        // The indicator is sixteen pixels across.
        const RenderedPicture measured = indicator(check, QStyle::PE_IndicatorCheckBox, QStyle::State_On, 1.0);
        int painted = 0;
        for (int x = 0; x < extent; ++x)
        {
            painted += sameColour(measured.at(x + 0.5, mid), bg) ? 0 : 1;
        }
        EXPECT_EQ(painted, 16);

        QStyleOptionButton disabled;
        disabled.initFrom(&check);
        disabled.rect = QRect(QPoint(0, 0), size);
        disabled.state = QStyle::State_On;
        const RenderedPicture dim(size, 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &disabled, &p, &check);
        });
        expectColour(dim.at(mid, edge + 2.0), layered(c.accentPrimary, kDisabled, bg), "disabled");
    });
}

// A slider is a four-pixel track in the accent's track colour, filled with the accent up to its knob, a fourteen-pixel
// accent circle; hovered, a soft accent halo lies around the knob; disabled, it is painted at 38 % opacity.
TEST(ProxyStyleObservatory, SliderTrackFillAndKnob)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QSlider> slider(Qt::Horizontal);
        slider.setRange(0, 100);
        slider.setValue(50);
        slider.resize(200, slider.sizeHint().height());
        QStyleOptionSlider option;
        slider.initStyleOption(&option);
        const QRect handle =
            scope.style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
        const qreal cx = handle.center().x() + 0.5;
        const qreal cy = slider.height() / 2.0;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(slider.size(), ratio, bg, [&](QPainter& p) {
                scope.style()->drawComplexControl(QStyle::CC_Slider, &option, &p, &slider);
            });
            expectColour(picture.at(cx, cy), c.accentPrimary, "knob");
            expectColour(picture.at(cx - 5.5, cy), c.accentPrimary, "the knob's left edge");
            expectColour(picture.at(cx + 30, cy), c.accentTrack, "the track past the knob");
            expectColour(picture.at(cx - 30, cy), c.accentPrimary, "the track before the knob");
            expectColour(picture.at(cx + 30, cy - 4), bg, "above the track");
            expectColour(picture.at(cx, cy - 9), bg, "above the knob");
        }
        QStyleOptionSlider hovered = option;
        hovered.state |= QStyle::State_MouseOver;
        hovered.activeSubControls = QStyle::SC_SliderHandle;
        const RenderedPicture halo(slider.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawComplexControl(QStyle::CC_Slider, &hovered, &p, &slider);
        });
        expectColour(halo.at(cx, cy - 9), an::ui::compositeOver(c.accentSoft, bg), "the hovered knob's halo");

        QStyleOptionSlider disabled = option;
        disabled.state &= ~QStyle::State_Enabled;
        const RenderedPicture dim(slider.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawComplexControl(QStyle::CC_Slider, &disabled, &p, &slider);
        });
        expectColour(dim.at(cx, cy), layered(c.accentPrimary, kDisabled, bg), "disabled knob");
    });
}

// A progress bar is the accent's track, filled with the accent - or the status colour its tone names - for the share
// done. Without text it is four pixels thick; with text, the text is the primary text over the track and the text on
// the accent over the fill.
TEST(ProxyStyleObservatory, ProgressBarTrackAndChunk)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QProgressBar> bar;
        bar.setRange(0, 100);
        bar.setValue(40);
        bar.setTextVisible(false);
        bar.resize(200, 16);
        QStyleOptionProgressBar option;
        bar.initStyleOption(&option);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(bar.size(), ratio, bg, [&](QPainter& p) {
                scope.style()->drawControl(QStyle::CE_ProgressBar, &option, &p, &bar);
            });
            expectColour(picture.at(40, 8), c.accentPrimary, "done");
            expectColour(picture.at(150, 8), c.accentTrack, "to do");
            expectColour(picture.at(150, 4), bg, "above a bar without text");
        }

        bar.setProperty("anUsageTone", QStringLiteral("high"));
        QStyleOptionProgressBar toned;
        bar.initStyleOption(&toned);
        const RenderedPicture tonedPicture(bar.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_ProgressBar, &toned, &p, &bar);
        });
        expectColour(tonedPicture.at(40, 8), c.statusError, "a bar toned high");
        bar.setProperty("anUsageTone", QVariant());

        bar.setTextVisible(true);
        bar.resize(200, 22);
        QStyleOptionProgressBar withText;
        bar.initStyleOption(&withText);
        const RenderedPicture textPicture(bar.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_ProgressBar, &withText, &p, &bar);
        });
        expectColour(textPicture.at(150, 4), c.accentTrack, "a bar with text is its height");
        const std::vector<RecordedText> texts = textsOf(
            bar.size(), [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_ProgressBar, &withText, &p, &bar); });
        bool overTrack = false;
        bool overFill = false;
        for (const RecordedText& text : texts)
        {
            overTrack = overTrack || (text.text.contains(QStringLiteral("40")) && text.colour == c.textPrimary);
            overFill = overFill || (text.text.contains(QStringLiteral("40")) && text.colour == c.textOnAccent);
        }
        EXPECT_TRUE(overTrack) << "the text over the track is the primary text";
        EXPECT_TRUE(overFill) << "the text over the fill is the text on the accent";
    });
}

// A tab is text: the selected one in the primary text with a three-pixel accent bar under it, ten pixels in from each
// side; the others in the secondary text with nothing under them, the state colour when hovered. Tabs are semibold, and
// the tab bar's base is a hairline.
TEST(ProxyStyleObservatory, TabsAreUnderlinedWhenSelected)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        Probed<QTabBar> tabs;
        // Laid out at their own sizes: a hidden bar would spread them over its default width.
        tabs.setExpanding(false);
        tabs.addTab(QStringLiteral("Stars"));
        tabs.addTab(QStringLiteral("Background"));
        tabs.setCurrentIndex(0);
        tabs.resize(tabs.sizeHint());
        EXPECT_EQ(tabs.height(), t.metrics.strip);
        const QRect selected = tabs.tabRect(0);
        const QRect other = tabs.tabRect(1);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(tabs.size(), ratio, bg, [&](QPainter& p) {
                for (int i = 0; i < tabs.count(); ++i)
                {
                    QStyleOptionTab option;
                    tabs.initStyleOption(&option, i);
                    scope.style()->drawControl(QStyle::CE_TabBarTabShape, &option, &p, &tabs);
                }
            });
            const qreal barY = selected.bottom() + 1 - 1.5;
            expectColour(picture.at(selected.center().x(), barY), c.accentPrimary, "the selected tab's bar");
            expectColour(picture.at(selected.left() + 5, barY), bg, "the bar starts ten pixels in");
            expectColour(picture.at(selected.center().x(), selected.top() + 4), bg, "above the bar");
            expectColour(picture.at(other.center().x(), barY), bg, "an unselected tab has no bar");
        }
        QStyleOptionTab hovered;
        tabs.initStyleOption(&hovered, 1);
        hovered.state |= QStyle::State_MouseOver;
        const RenderedPicture hoverPicture(tabs.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_TabBarTabShape, &hovered, &p, &tabs);
        });
        expectColour(hoverPicture.at(other.left() + 6, other.center().y()), layered(c.stateLayer, kHover, bg),
                     "a hovered tab");

        const std::vector<RecordedText> texts = textsOf(tabs.size(), [&](QPainter& p) {
            for (int i = 0; i < tabs.count(); ++i)
            {
                QStyleOptionTab option;
                tabs.initStyleOption(&option, i);
                scope.style()->drawControl(QStyle::CE_TabBarTabLabel, &option, &p, &tabs);
            }
        });
        const RecordedText stars = textHolding(texts, QStringLiteral("Stars"));
        const RecordedText background = textHolding(texts, QStringLiteral("Background"));
        expectColour(stars.colour, c.textPrimary, "the selected tab's text", 0);
        expectColour(background.colour, c.textSecondary, "an unselected tab's text", 0);
        EXPECT_EQ(stars.font.weight(), QFont::DemiBold);
        EXPECT_EQ(background.font.weight(), QFont::DemiBold);
        // Semibold text fits its tab: the tab was sized for it.
        EXPECT_GE(tabs.tabRect(1).width(),
                  QFontMetrics(background.font).horizontalAdvance(QStringLiteral("Background")));

        QStyleOptionTabBarBase base;
        base.initFrom(&tabs);
        base.rect = QRect(0, 0, 200, 2);
        base.shape = QTabBar::RoundedNorth;
        const RenderedPicture basePicture(QSize(200, 2), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_FrameTabBarBase, &base, &p, &tabs);
        });
        expectColour(basePicture.at(100, 1.5), an::ui::compositeOver(c.borderSubtle, bg), "the base's hairline");
    });
}

// A tab's close button is sixteen pixels: a cross in the tertiary text at rest, in the primary text over the state
// colour when hovered, and the whole at 38 % opacity when disabled.
TEST(ProxyStyleObservatory, TabCloseButtonIsACrossOverTheStateColour)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        EXPECT_EQ(scope.style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth), 16);
        EXPECT_EQ(scope.style()->pixelMetric(QStyle::PM_TabCloseIndicatorHeight), 16);
        QWidget host;
        const auto paintClose = [&](const QStyle::State state, const qreal ratio) {
            QStyleOption option;
            option.initFrom(&host);
            option.rect = QRect(0, 0, 16, 16);
            option.state = state;
            return RenderedPicture(QSize(16, 16), ratio, bg, [&](QPainter& p) {
                scope.style()->drawPrimitive(QStyle::PE_IndicatorTabClose, &option, &p, &host);
            });
        };
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture rest = paintClose(QStyle::State_Enabled, ratio);
            expectColour(rest.at(8.0, 8.0), c.textTertiary, "the cross", 40);
            expectColour(rest.at(1.5, 8.0), bg, "no ground at rest");
            const RenderedPicture hovered = paintClose(QStyle::State_Enabled | QStyle::State_MouseOver, ratio);
            expectColour(hovered.at(8.0, 8.0), c.textPrimary, "the cross under the pointer", 40);
            expectColour(hovered.at(1.5, 8.0), layered(c.stateLayer, kHover, bg), "the state colour");
        }
        const RenderedPicture enabled = paintClose(QStyle::State_Enabled, 1.0);
        const RenderedPicture disabled = paintClose(QStyle::State_None, 1.0);
        expectColour(disabled.at(8.0, 8.0), layered(enabled.at(8.0, 8.0), kDisabled, bg), "a disabled cross", 4);
    });
}

// A group box is a card - the second surface with the cards' radius and no border - and a flat one is a hairline over
// the window. Its title is semibold in the secondary text.
TEST(ProxyStyleObservatory, GroupBoxIsACardAndAFlatOneAHairline)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = t.layers.surface0;
        Probed<QGroupBox> group(QStringLiteral("Stars"));
        group.resize(240, 120);
        QStyleOptionGroupBox option;
        group.initStyleOption(&option);
        const QRect frame =
            scope.style()->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxFrame, &group);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(group.size(), ratio, bg, [&](QPainter& p) {
                scope.style()->drawComplexControl(QStyle::CC_GroupBox, &option, &p, &group);
            });
            expectColour(picture.at(120, frame.center().y()), c.bgCard, "the card");
            expectColour(picture.at(0.2, frame.center().y()), c.bgCard, "a card has no border");
            expectColour(picture.at(frame.left() + 1.0, frame.top() + 1.0), bg, "the card's corner");
            expectColour(picture.at(frame.left() + 5.0, frame.top() + 5.0), c.bgCard, "inside the corner");
        }
        const RecordedText title = textHolding(
            textsOf(group.size(),
                    [&](QPainter& p) { scope.style()->drawComplexControl(QStyle::CC_GroupBox, &option, &p, &group); }),
            QStringLiteral("Stars"));
        expectColour(title.colour, c.textSecondary, "the title", 0);
        EXPECT_EQ(title.font.weight(), QFont::DemiBold);

        group.setFlat(true);
        QStyleOptionGroupBox flat;
        group.initStyleOption(&flat);
        const RenderedPicture flatPicture(group.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawComplexControl(QStyle::CC_GroupBox, &flat, &p, &group);
        });
        expectColour(flatPicture.at(120, frame.center().y()), bg, "a flat group has no fill");
        expectColour(flatPicture.at(120, frame.top() + 0.5), an::ui::compositeOver(c.borderSubtle, bg),
                     "a flat group's hairline");
    });
}

// A view's selected row is the accent at its soft share with a three-pixel accent bar at the row's start, a hovered row
// the state colour; the selected text stays the primary text. A header is the third surface with a hairline under it
// and its labels in small capitals of the secondary text. Grid lines are hairlines.
TEST(ProxyStyleObservatory, ItemViewRowsAndHeader)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = t.layers.surface1;
        QTableWidget table(3, 3);
        const QSize row(200, t.metrics.rowTable);
        const auto item = [&](const QStyle::State state, const QStyleOptionViewItem::ViewItemPosition position) {
            QStyleOptionViewItem option;
            option.initFrom(&table);
            option.rect = QRect(QPoint(0, 0), row);
            option.state = QStyle::State_Enabled | state;
            option.viewItemPosition = position;
            return option;
        };
        for (const qreal ratio : kRenderRatios)
        {
            const QStyleOptionViewItem first = item(QStyle::State_Selected, QStyleOptionViewItem::Beginning);
            const RenderedPicture picture(row, ratio, bg, [&](QPainter& p) {
                scope.style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &first, &p, &table);
            });
            expectColour(picture.at(100, row.height() / 2.0), an::ui::compositeOver(c.accentSoft, bg), "selected");
            expectColour(picture.at(1.0, row.height() / 2.0), c.accentPrimary, "the selection's bar");
            expectColour(picture.at(4.5, row.height() / 2.0), an::ui::compositeOver(c.accentSoft, bg), "past the bar");
        }
        const QStyleOptionViewItem middle = item(QStyle::State_Selected, QStyleOptionViewItem::Middle);
        const RenderedPicture middlePicture(row, 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &middle, &p, &table);
        });
        expectColour(middlePicture.at(1.0, row.height() / 2.0), an::ui::compositeOver(c.accentSoft, bg),
                     "a cell inside the row has no bar");
        const QStyleOptionViewItem hovered = item(QStyle::State_MouseOver, QStyleOptionViewItem::OnlyOne);
        const RenderedPicture hoverPicture(row, 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &hovered, &p, &table);
        });
        expectColour(hoverPicture.at(100, row.height() / 2.0), layered(c.stateLayer, kHover, bg), "hovered");

        QStyleOptionViewItem selectedText = item(QStyle::State_Selected, QStyleOptionViewItem::OnlyOne);
        selectedText.text = QStringLiteral("M31");
        selectedText.features |= QStyleOptionViewItem::HasDisplay;
        const RecordedText cell =
            textHolding(textsOf(row,
                                [&](QPainter& p) {
                                    scope.style()->drawControl(QStyle::CE_ItemViewItem, &selectedText, &p, &table);
                                }),
                        QStringLiteral("M31"));
        expectColour(cell.colour, c.textPrimary, "selected text", 0);

        QHeaderView header(Qt::Horizontal);
        QStyleOptionHeader section;
        section.initFrom(&header);
        section.rect = QRect(0, 0, 160, t.metrics.rowTable);
        section.text = QStringLiteral("Frame");
        section.orientation = Qt::Horizontal;
        section.position = QStyleOptionHeader::Middle;
        const RenderedPicture headerPicture(section.rect.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_HeaderSection, &section, &p, &header);
        });
        expectColour(headerPicture.at(80, 8), c.bgInput, "the header");
        expectColour(headerPicture.at(80, section.rect.height() - 0.5),
                     an::ui::compositeOver(c.borderSubtle, c.bgInput), "the header's hairline");
        const RecordedText label = textHolding(
            textsOf(section.rect.size(),
                    [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_HeaderLabel, &section, &p, &header); }),
            QStringLiteral("Frame"));
        expectColour(label.colour, c.textSecondary, "the header's label", 0);
        EXPECT_EQ(label.font.capitalization(), QFont::AllUppercase);
        EXPECT_EQ(label.font.weight(), QFont::DemiBold);
        EXPECT_EQ(logicalPixels(label.font), t.metrics.fontCaption);

        EXPECT_EQ(static_cast<QRgb>(scope.style()->styleHint(QStyle::SH_Table_GridLineColor, nullptr, &table)),
                  c.borderSubtle.rgba());
    });
}

// A scroll bar has no arrows - its groove is its whole length - and its handle is the tertiary text at 40 %, 70 % when
// hovered, over nothing: the content shows under the groove.
TEST(ProxyStyleObservatory, ScrollBarHasNoArrowsAndATranslucentHandle)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = t.layers.surface1;
        Probed<QScrollBar> bar(Qt::Vertical);
        bar.setRange(0, 100);
        bar.setPageStep(20);
        bar.setValue(0);
        bar.resize(t.metrics.scrollBar, 200);
        QStyleOptionSlider option;
        bar.initStyleOption(&option);
        const auto rect = [&](const QStyle::SubControl part) {
            return scope.style()->subControlRect(QStyle::CC_ScrollBar, &option, part, &bar);
        };
        EXPECT_TRUE(rect(QStyle::SC_ScrollBarAddLine).isEmpty());
        EXPECT_TRUE(rect(QStyle::SC_ScrollBarSubLine).isEmpty());
        EXPECT_EQ(rect(QStyle::SC_ScrollBarGroove), bar.rect());
        const QRect handle = rect(QStyle::SC_ScrollBarSlider);
        EXPECT_EQ(handle.top(), 0);
        EXPECT_NEAR(handle.height(), 200 * 20 / 120, 1);
        EXPECT_EQ(rect(QStyle::SC_ScrollBarAddPage).top(), handle.bottom() + 1);
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(bar.size(), ratio, bg, [&](QPainter& p) {
                scope.style()->drawComplexControl(QStyle::CC_ScrollBar, &option, &p, &bar);
            });
            expectColour(picture.at(t.metrics.scrollBar / 2.0, handle.center().y()), layered(c.textTertiary, 0.40, bg),
                         "the handle");
            expectColour(picture.at(t.metrics.scrollBar / 2.0, 150), bg, "the groove");
        }
        QStyleOptionSlider hovered = option;
        hovered.state |= QStyle::State_MouseOver;
        hovered.activeSubControls = QStyle::SC_ScrollBarSlider;
        const RenderedPicture hoverPicture(bar.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawComplexControl(QStyle::CC_ScrollBar, &hovered, &p, &bar);
        });
        expectColour(hoverPicture.at(t.metrics.scrollBar / 2.0, handle.center().y()), layered(c.textTertiary, 0.70, bg),
                     "the hovered handle");
    });
}

// A menu is a raised surface: the third surface with a hairline and the controls' radius, its window translucent with
// room for its shadow, its items inset from that surface, its surface where it was placed. The item under the pointer
// is the accent's container in its text; a shortcut is the tertiary text in the monospace; a separator is a hairline.
TEST(ProxyStyleObservatory, MenuIsARaisedSurface)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        Probed<QMenu> menu;
        QAction* const open = menu.addAction(QStringLiteral("Open"));
        open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        menu.addSeparator();
        menu.addAction(QStringLiteral("Close"));
        menu.ensurePolished();
        const QMargins margins = an::ui::elevationWindowMargins();
        EXPECT_FALSE(margins.isNull());
        EXPECT_TRUE(menu.testAttribute(Qt::WA_TranslucentBackground));
        EXPECT_EQ(menu.contentsMargins(), margins);
        const int inset = scope.style()->pixelMetric(QStyle::PM_MenuPanelWidth, nullptr, &menu) +
                          scope.style()->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu);
        menu.resize(menu.sizeHint());
        const QRect openRect = menu.actionGeometry(open);
        EXPECT_EQ(openRect.left(), margins.left() + inset);
        EXPECT_EQ(openRect.height(), t.metrics.rowDense);

        const QRect surface = menu.rect().marginsRemoved(margins);
        const RenderedPicture picture = RenderedPicture::ofWidget(menu, 1.0, Qt::transparent);
        expectColour(picture.at(surface.left() + 2.5, openRect.center().y()), c.bgInput, "the menu's surface");
        expectColour(picture.at(surface.left() + 0.2, surface.center().y()),
                     an::ui::compositeOver(c.borderSubtle, c.bgInput), "the menu's hairline");
        EXPECT_LT(picture.at(surface.left() + 0.5, surface.top() + 0.5).alpha(), 255) << "the surface is rounded";

        QStyleOptionMenuItem item;
        menu.initStyleOption(&item, open);
        item.rect = openRect;
        const QStyleOptionMenuItem rest = item;
        item.state |= QStyle::State_Selected;
        const RenderedPicture selected(menu.size(), 1.0, c.bgInput, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_MenuItem, &item, &p, &menu);
        });
        expectColour(selected.at(openRect.left() + 2, openRect.center().y()), c.accentContainer,
                     "the item under the pointer");
        const std::vector<RecordedText> selectedTexts = textsOf(
            menu.size(), [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_MenuItem, &item, &p, &menu); });
        expectColour(textHolding(selectedTexts, QStringLiteral("Open")).colour, c.textOnAccentContainer,
                     "the item's text under the pointer", 0);
        const std::vector<RecordedText> restTexts = textsOf(
            menu.size(), [&](QPainter& p) { scope.style()->drawControl(QStyle::CE_MenuItem, &rest, &p, &menu); });
        expectColour(textHolding(restTexts, QStringLiteral("Open")).colour, c.textPrimary, "the item's text", 0);
        const RecordedText shortcut = textHolding(restTexts, QStringLiteral("Ctrl"));
        expectColour(shortcut.colour, c.textTertiary, "the shortcut", 0);
        EXPECT_EQ(shortcut.font.families().value(0), t.typography.monospace.families().value(0));

        // Placed at a point, the menu's surface - not its window - is there; a small menu, which the suite's screen
        // holds whole wherever it is placed.
        QMenu small;
        small.addAction(QStringLiteral("Go"));
        const QPoint at(20, 30);
        small.popup(at);
        ASSERT_TRUE(eventually([&small] { return small.isVisible(); }));
        EXPECT_EQ(small.geometry().topLeft() + QPoint(margins.left(), margins.top()), at);
        small.hide();
    });
}

// A menu bar is the first surface with a hairline under it; its item under the pointer lays the state colour.
TEST(ProxyStyleObservatory, MenuBarItemsLayerTheStateColour)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        QMenuBar bar;
        QMenu* const file = bar.addMenu(QStringLiteral("File"));
        bar.resize(300, bar.sizeHint().height());
        const QRect fileRect = bar.actionGeometry(file->menuAction());
        const RenderedPicture picture = RenderedPicture::ofWidget(bar, 1.0, t.layers.surface0);
        expectColour(picture.at(280, bar.height() / 2.0), t.materials.solidSurface, "the bar");
        expectColour(picture.at(280, bar.height() - 0.5),
                     an::ui::compositeOver(c.borderSubtle, t.materials.solidSurface), "the bar's hairline");

        QStyleOptionMenuItem item;
        item.initFrom(&bar);
        item.menuItemType = QStyleOptionMenuItem::Normal;
        item.text = QStringLiteral("File");
        item.rect = fileRect;
        item.state |= QStyle::State_Selected;
        const RenderedPicture hovered(bar.size(), 1.0, t.materials.solidSurface, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_MenuBarItem, &item, &p, &bar);
        });
        expectColour(hovered.at(fileRect.left() + 3, fileRect.center().y()),
                     layered(c.stateLayer, kHover, t.materials.solidSurface), "the item under the pointer");
    });
}

// A tooltip is a raised surface too: the fourth surface with a hairline and the small parts' radius, in the small text,
// its window translucent with room for its shadow. A tooltip is a label shown as a tooltip window, which is what the
// tooltip Qt shows is.
TEST(ProxyStyleObservatory, ToolTipIsTheFourthSurface)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        QLabel tip(QStringLiteral("Background level"), nullptr, Qt::ToolTip);
        tip.ensurePolished();
        tip.resize(tip.sizeHint());
        EXPECT_TRUE(tip.testAttribute(Qt::WA_TranslucentBackground));
        EXPECT_EQ(tip.contentsMargins(), an::ui::elevationWindowMargins());
        EXPECT_EQ(logicalPixels(tip.font()), t.metrics.fontSmall);
        expectColour(tip.palette().color(QPalette::ToolTipText), c.textPrimary, "the text", 0);
        const QRect surface = tip.rect().marginsRemoved(an::ui::elevationWindowMargins());
        ASSERT_FALSE(surface.isEmpty());
        QStyleOptionFrame option;
        option.initFrom(&tip);
        option.rect = tip.rect();
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(tip.size(), ratio, Qt::transparent, [&](QPainter& p) {
                scope.style()->drawPrimitive(QStyle::PE_PanelTipLabel, &option, &p, &tip);
            });
            expectColour(picture.at(surface.left() + 3, surface.top() + 3), t.layers.surface4, "the tooltip's surface");
            EXPECT_GT(picture.at(surface.center().x(), surface.bottom() + 6).alpha(), 0) << "the shadow under it";
        }
        // The hairline, where the surface's edge is a whole device pixel. A tooltip is short, so its shadow is densest
        // just under its edge, and the edge's anti-aliased coverage lets a trace of it through: a few levels.
        const RenderedPicture edge(tip.size(), 1.0, t.layers.surface0, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_PanelTipLabel, &option, &p, &tip);
        });
        expectColour(edge.at(surface.left() + 0.5, surface.center().y()),
                     an::ui::compositeOver(c.borderSubtle, t.layers.surface4), "the tooltip's hairline", 6);
    });
}

// A splitter's handle is a hairline, the accent's line when hovered; a process splitter's wider handle holds a grip.
TEST(ProxyStyleObservatory, SplitterHandleIsAHairline)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = t.layers.surface0;
        QSplitter splitter(Qt::Horizontal);
        splitter.addWidget(new QWidget);
        splitter.addWidget(new QWidget);
        splitter.resize(200, 100);
        QWidget* const handle = splitter.handle(1);
        const auto paint = [&](const QSize size, const QStyle::State state) {
            QStyleOption option;
            option.initFrom(handle);
            option.rect = QRect(QPoint(0, 0), size);
            option.state = QStyle::State_Enabled | QStyle::State_Horizontal | state;
            return RenderedPicture(size, 1.0, bg, [&](QPainter& p) {
                scope.style()->drawControl(QStyle::CE_Splitter, &option, &p, handle);
            });
        };
        expectColour(paint(QSize(2, 100), {}).at(0.5, 50), an::ui::compositeOver(c.borderSubtle, bg), "at rest");
        expectColour(paint(QSize(2, 100), QStyle::State_MouseOver).at(0.5, 50), an::ui::compositeOver(c.accentLine, bg),
                     "hovered");

        splitter.setProperty("anSplitterVariant", QStringLiteral("process"));
        const RenderedPicture process = paint(QSize(8, 100), {});
        expectColour(process.at(4, 50), an::ui::compositeOver(c.borderPrimary, bg), "a process splitter's grip");
        expectColour(process.at(0.5, 50), bg, "beside the grip");
    });
}

// A dialog's buttons take the roles their kind gives them: Ok is primary, Cancel outlined, Reset quiet, Discard
// destructive.
TEST(ProxyStyleObservatory, DialogButtonBoxButtonsTakeTheirRoles)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = panel(t);
        QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset |
                             QDialogButtonBox::Discard);
        box.ensurePolished();
        const std::vector<std::pair<QDialogButtonBox::StandardButton, QColor>> expected{
            {QDialogButtonBox::Ok, c.accentPrimary},
            {QDialogButtonBox::Cancel, bg},
            {QDialogButtonBox::Reset, bg},
            {QDialogButtonBox::Discard, c.statusError},
        };
        for (const auto& [which, fill] : expected)
        {
            QPushButton* const button = box.button(which);
            ASSERT_NE(button, nullptr);
            SCOPED_TRACE(button->text().toStdString());
            button->resize(120, t.metrics.control);
            const QStyleOptionButton option = plainButtonOption(*button);
            const RenderedPicture picture(button->size(), 1.0, bg, [&](QPainter& p) {
                scope.style()->drawControl(QStyle::CE_PushButton, &option, &p, button);
            });
            expectColour(picture.at(6, button->height() / 2.0), fill, "fill");
        }
        QPushButton* const reset = box.button(QDialogButtonBox::Reset);
        const QStyleOptionButton resetOption = plainButtonOption(*reset);
        const RenderedPicture resetPicture(reset->size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawControl(QStyle::CE_PushButton, &resetOption, &p, reset);
        });
        expectColour(resetPicture.at(0.2, reset->height() / 2.0), bg, "a quiet button has no outline");
    });
}

// The process windows' card is the second surface with the cards' radius and no border; their banner is the status
// colour's soft share with a three-pixel bar of the colour at its start.
TEST(ProxyStyleObservatory, ProcessCardAndBanner)
{
    forEachLook([](ObservatoryStyleScope& scope, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bg = t.layers.surface0;
        QFrame card;
        card.setObjectName(QStringLiteral("anProcessShellCard"));
        card.setFrameShape(QFrame::StyledPanel);
        card.resize(200, 100);
        QStyleOptionFrame option;
        option.initFrom(&card);
        option.rect = card.rect();
        option.frameShape = QFrame::StyledPanel;
        for (const qreal ratio : kRenderRatios)
        {
            const RenderedPicture picture(card.size(), ratio, bg, [&](QPainter& p) {
                scope.style()->drawPrimitive(QStyle::PE_Frame, &option, &p, &card);
            });
            expectColour(picture.at(100, 50), c.bgCard, "the card");
            expectColour(picture.at(0.2, 50), c.bgCard, "no border");
            expectColour(picture.at(1.0, 1.0), bg, "the card's corner");
        }

        QFrame banner;
        banner.setObjectName(QStringLiteral("anProcessShellBanner"));
        banner.setProperty("anProcessMessageTone", QStringLiteral("warning"));
        banner.resize(200, 40);
        QStyleOptionFrame bannerOption;
        bannerOption.initFrom(&banner);
        bannerOption.rect = banner.rect();
        const RenderedPicture bannerPicture(banner.size(), 1.0, bg, [&](QPainter& p) {
            scope.style()->drawPrimitive(QStyle::PE_Frame, &bannerOption, &p, &banner);
        });
        expectColour(bannerPicture.at(100, 20), layered(c.statusWarning, 0.16, bg), "the banner");
        expectColour(bannerPicture.at(1.5, 20), c.statusWarning, "the banner's bar");
    });
}

// The window's bars are flat: a tool bar and a status bar are the first surface, the one with a hairline under it, the
// other over it.
TEST(ProxyStyleObservatory, ChromeBarsAreFlat)
{
    forEachLook([](ObservatoryStyleScope&, const DesignTokens& t) {
        const ColorTokens& c = t.colors;
        const QColor bar = t.materials.solidSurface;
        QMainWindow window;
        QToolBar* const tools = window.addToolBar(QStringLiteral("Tools"));
        tools->addAction(QStringLiteral("Zoom"));
        window.statusBar()->showMessage(QStringLiteral("Ready"));
        window.resize(400, 300);
        window.show();
        ASSERT_TRUE(eventually([&] { return tools->isVisible() && window.statusBar()->isVisible(); }));
        const RenderedPicture toolPicture = RenderedPicture::ofWidget(*tools, 1.0, t.layers.surface0);
        expectColour(toolPicture.at(tools->width() - 10, tools->height() / 2.0), bar, "the tool bar");
        expectColour(toolPicture.at(tools->width() - 10, tools->height() - 0.5),
                     an::ui::compositeOver(c.borderSubtle, bar), "the tool bar's hairline");
        QStatusBar* const status = window.statusBar();
        const RenderedPicture statusPicture = RenderedPicture::ofWidget(*status, 1.0, t.layers.surface0);
        expectColour(statusPicture.at(status->width() - 30, status->height() / 2.0), bar, "the status bar");
        expectColour(statusPicture.at(status->width() - 30, 0.5), an::ui::compositeOver(c.borderSubtle, bar),
                     "the status bar's hairline");
    });
}

// Under the Classic profile the proxy style paints every widget through its own code: none of the Observatory profile's
// painters runs. Under the Observatory profile they do.
TEST(ProxyStyleDispatch, ClassicRunsNoObservatoryPainter)
{
    const auto paintEverything = [] {
        QWidget panel;
        auto* const layout = new QVBoxLayout(&panel);
        layout->addWidget(new QPushButton(QStringLiteral("Apply")));
        auto* const tool = new QToolButton;
        tool->setText(QStringLiteral("Grid"));
        layout->addWidget(tool);
        layout->addWidget(new QLineEdit(QStringLiteral("M31")));
        auto* const combo = new QComboBox;
        combo->addItem(QStringLiteral("Linear"));
        layout->addWidget(combo);
        layout->addWidget(new QSpinBox);
        auto* const check = new QCheckBox(QStringLiteral("Linked"));
        check->setChecked(true);
        layout->addWidget(check);
        layout->addWidget(new QRadioButton(QStringLiteral("Mean")));
        layout->addWidget(new QSlider(Qt::Horizontal));
        auto* const progress = new QProgressBar;
        progress->setValue(40);
        layout->addWidget(progress);
        auto* const tabs = new QTabWidget;
        tabs->addTab(new QWidget, QStringLiteral("Stars"));
        layout->addWidget(tabs);
        auto* const group = new QGroupBox(QStringLiteral("Output"));
        group->setLayout(new QVBoxLayout);
        group->layout()->addWidget(new QLabel(QStringLiteral("Folder")));
        layout->addWidget(group);
        auto* const table = new QTableWidget(4, 2);
        table->setHorizontalHeaderLabels({QStringLiteral("Frame"), QStringLiteral("FWHM")});
        table->selectRow(1);
        layout->addWidget(table);
        layout->addWidget(new QScrollBar(Qt::Vertical));
        auto* const splitter = new QSplitter;
        splitter->addWidget(new QWidget);
        splitter->addWidget(new QWidget);
        layout->addWidget(splitter);
        layout->addWidget(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel));
        panel.resize(400, 900);
        (void)RenderedPicture::ofWidget(panel, 1.0, Qt::black);

        QMenu menu;
        menu.addAction(QStringLiteral("Open"));
        menu.resize(menu.sizeHint());
        (void)RenderedPicture::ofWidget(menu, 1.0, Qt::black);
        QMenuBar bar;
        bar.addMenu(QStringLiteral("File"));
        bar.resize(200, bar.sizeHint().height());
        (void)RenderedPicture::ofWidget(bar, 1.0, Qt::black);
    };

    {
        const ObservatoryStyleScope classic(observatoryLooks()[0], StyleProfile::Classic);
        const std::uint64_t before = an::ui::testing::observatoryPaints();
        paintEverything();
        EXPECT_EQ(an::ui::testing::observatoryPaints(), before) << "the Classic profile ran an Observatory painter";
    }
    {
        const ObservatoryStyleScope observatory(observatoryLooks()[0]);
        const std::uint64_t before = an::ui::testing::observatoryPaints();
        paintEverything();
        EXPECT_GT(an::ui::testing::observatoryPaints(), before + 20u)
            << "the Observatory profile's painters did not run";
    }
}
