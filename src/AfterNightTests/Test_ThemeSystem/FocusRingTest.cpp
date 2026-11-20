// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The focus ring as a person meets it in a window under the Observatory profile: the controls of a row - a button, a
/// tool button, a check box, a radio button, a slider and a tab bar - clicked into the focus draw no ring, and tabbed
/// to draw one, two pixels of the focus colour; a field - a line edit, a spin box, a combo box - draws its focus border
/// whenever it holds the focus, as a browser shows `:focus-visible` on a text field it was clicked into: the border
/// says where the typing goes. The style draws them; the window's keyboard-focus flag, which Qt sets for a focus from
/// the keyboard and clears at a press of the pointer, is what tells the two apart.

#include "fixtures/ObservatoryLook.h"
#include "theme/AnDesignTokens.h"
#include "theme/AnProxyStyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QThread>
#include <QToolButton>
#include <QWidget>
#include <QWindow>
#include <QtTest/QTest>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using an::ui::testing::colourName;
using an::ui::testing::observatoryLooks;
using an::ui::testing::ObservatoryStyleScope;
using an::ui::testing::RenderedPicture;
using an::ui::testing::sameColour;

/// Makes a window the active one; under the suites' `minimal` platform a window of its own, shown and gone, delivers
/// the activation that platform queues.
bool activate(QWidget& window)
{
    if (QGuiApplication::platformName() == QLatin1String("minimal"))
    {
        QWindow delivers;
        delivers.create();
        window.activateWindow();
        delivers.setVisible(true);
        delivers.setVisible(false);
    }
    else
    {
        window.activateWindow();
    }
    QElapsedTimer timer;
    timer.start();
    while (!window.isActiveWindow() && timer.elapsed() < 2000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(2);
    }
    return window.isActiveWindow();
}

/// The pixels of a control's picture, at a ratio, that are the focus colour.
int focusPixels(QWidget& control, const qreal ratio)
{
    const an::ui::DesignTokens& t = an::ui::tokens();
    const RenderedPicture picture = RenderedPicture::ofWidget(control, ratio, t.layers.surface1);
    const QImage& image = picture.image();
    int found = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            found += sameColour(image.pixelColor(x, y), t.colors.borderFocus, 6) ? 1 : 0;
        }
    }
    return found;
}

/// A control as a failure names it.
std::string nameOf(const QWidget* control)
{
    return std::string(control->metaObject()->className()) + " '" + control->objectName().toStdString() + "'";
}

/// A row of controls in a window, the focus given to each from the pointer and from the keyboard.
class FocusRing : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_window.setObjectName(QStringLiteral("focusRingWindow"));
        auto* const row = new QHBoxLayout(&m_window);
        // What the pointer presses to clear the keyboard's flag, as a press anywhere in a window does.
        m_neutral = new QLabel(QStringLiteral("Neutral"), &m_window);
        row->addWidget(m_neutral);
        m_controls.push_back(new QPushButton(QStringLiteral("Apply"), &m_window));
        auto* const tool = new QToolButton(&m_window);
        tool->setText(QStringLiteral("Tool"));
        tool->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tool->setFocusPolicy(Qt::StrongFocus);
        m_controls.push_back(tool);
        m_controls.push_back(new QCheckBox(QStringLiteral("Linked"), &m_window));
        m_controls.push_back(new QRadioButton(QStringLiteral("Mean"), &m_window));
        auto* const slider = new QSlider(Qt::Horizontal, &m_window);
        slider->setFocusPolicy(Qt::StrongFocus);
        m_controls.push_back(slider);
        auto* const tabs = new QTabBar(&m_window);
        tabs->addTab(QStringLiteral("One"));
        tabs->addTab(QStringLiteral("Two"));
        tabs->setFocusPolicy(Qt::StrongFocus);
        m_controls.push_back(tabs);
        m_fields.push_back(new QLineEdit(QStringLiteral("text"), &m_window));
        m_fields.push_back(new QSpinBox(&m_window));
        auto* const combo = new QComboBox(&m_window);
        combo->addItems({QStringLiteral("Linear"), QStringLiteral("Cubic")});
        combo->setFocusPolicy(Qt::StrongFocus);
        m_fields.push_back(combo);
        for (QWidget* control : m_controls)
        {
            control->setObjectName(QString::fromLatin1(control->metaObject()->className()));
            row->addWidget(control);
        }
        for (QWidget* field : m_fields)
        {
            field->setObjectName(QString::fromLatin1(field->metaObject()->className()));
            row->addWidget(field);
        }
        m_window.resize(1100, 60);
        m_window.show();
        m_active = activate(m_window);
    }

    /// The focus given to a control as a press of the pointer gives it: the press clears the window's keyboard flag,
    /// and the focus follows the click.
    void focusFromThePointer(QWidget& control)
    {
        QTest::mouseClick(m_neutral, Qt::LeftButton);
        control.setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
    }

    /// The focus given to a control as Tab gives it: Tab pressed on the widget before it in the focus chain.
    void focusFromTheKeyboard(QWidget& control)
    {
        QWidget* before = control.previousInFocusChain();
        while (before != nullptr && before != &control && (before->focusPolicy() & Qt::TabFocus) == 0)
        {
            before = before->previousInFocusChain();
        }
        ASSERT_NE(before, nullptr);
        before->setFocus(Qt::MouseFocusReason);
        QTest::keyClick(before, Qt::Key_Tab);
        QCoreApplication::processEvents();
        ASSERT_EQ(QApplication::focusWidget(), &control) << "Tab from " << nameOf(before) << " went elsewhere";
    }

    QWidget m_window;
    QLabel* m_neutral = nullptr;
    std::vector<QWidget*> m_controls;
    std::vector<QWidget*> m_fields;
    bool m_active = false;
};

// A control clicked into the focus draws no ring; tabbed to, it draws the ring; a field draws its focus border either
// way. Under every look, at 1 and 2 device pixels a pixel.
TEST_F(FocusRing, KeyboardOnly)
{
    if (!m_active)
    {
        GTEST_SKIP() << "the platform gives the window no activation, so no control of it has the focus";
    }
    ObservatoryStyleScope scope(observatoryLooks()[0]);
    for (const an::ui::testing::ObservatoryLook& look : observatoryLooks())
    {
        SCOPED_TRACE(look.name);
        scope.setLook(look);
        QCoreApplication::processEvents();
        for (QWidget* control : m_controls)
        {
            focusFromThePointer(*control);
            ASSERT_EQ(QApplication::focusWidget(), control);
            ASSERT_FALSE(m_window.testAttribute(Qt::WA_KeyboardFocusChange));
            for (const qreal ratio : {1.0, 2.0})
            {
                EXPECT_EQ(focusPixels(*control, ratio), 0)
                    << nameOf(control) << " clicked into the focus draws a ring at " << ratio << " ("
                    << colourName(an::ui::tokens().colors.borderFocus) << ")";
            }
            focusFromTheKeyboard(*control);
            ASSERT_TRUE(m_window.testAttribute(Qt::WA_KeyboardFocusChange));
            for (const qreal ratio : {1.0, 2.0})
            {
                EXPECT_GT(focusPixels(*control, ratio), 0)
                    << nameOf(control) << " tabbed to draws no ring at " << ratio;
            }
        }
        for (QWidget* field : m_fields)
        {
            focusFromThePointer(*field);
            ASSERT_EQ(QApplication::focusWidget(), field);
            EXPECT_GT(focusPixels(*field, 1.0), 0) << nameOf(field) << " clicked into draws no focus border";
            focusFromTheKeyboard(*field);
            EXPECT_GT(focusPixels(*field, 1.0), 0) << nameOf(field) << " tabbed to draws no focus border";
        }
        // Without the focus, nothing of the focus colour.
        m_neutral->setFocus(Qt::MouseFocusReason);
        QCoreApplication::processEvents();
        for (QWidget* control : m_controls)
        {
            EXPECT_EQ(focusPixels(*control, 1.0), 0) << nameOf(control) << " draws the focus colour unfocused";
        }
        for (QWidget* field : m_fields)
        {
            EXPECT_EQ(focusPixels(*field, 1.0), 0) << nameOf(field) << " draws the focus colour unfocused";
        }
    }
}

} // namespace
