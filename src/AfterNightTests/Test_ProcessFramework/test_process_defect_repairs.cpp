// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Reset puts a process dialog back to the values it opens with: the multiscale sharpen's scales, the range selection's
/// lightness, the total variation denoise's epsilon and the profiled recombination's edge softening.

#include "Test_ProcessFramework/ProcessWindowCaseSupport.h"

#include "processes/ProfiledStarmaskRecombinationProcess.h"
#include "processes/RangeSelectionProcess.h"
#include "processes/SharpenAlphaProcess.h"
#include "processes/TotalVariationDenoiseProcess.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::framework;

/// One dialog value Reset must restore: the window it lives in, how it is read as text and how a person changes it.
struct ResetValue
{
    std::string process;
    std::function<WindowHandle<QWidget>(ImageViewControl&)> open;
    std::function<QString(const QWidget&)> read;
    std::function<void(const QWidget&)> change;
};

/// A spin box's value as text, or nothing when the window has no such box.
template <typename SpinT> QString spinText(const QWidget& window, const char* name)
{
    const auto* spin = widgetNamed<SpinT>(window, name);
    return spin != nullptr ? QString::number(spin->value()) : QString();
}

/// Moves a spin box to another value of its range.
template <typename SpinT> void moveSpin(const QWidget& window, const char* name)
{
    if (auto* spin = widgetNamed<SpinT>(window, name))
    {
        spin->setValue(spin->value() == spin->maximum() ? spin->minimum() : spin->maximum());
    }
}

template <typename WindowT> WindowHandle<QWidget> openTyped(ImageViewControl& target)
{
    return WindowHandle<QWidget>(openOver<WindowT>(target).release());
}

[[nodiscard]] std::vector<ResetValue> resetValues()
{
    return {
        ResetValue{"Multiscale Alpha Sharpen", openTyped<SharpenAlphaProcess>,
                   [](const QWidget& window) { return spinText<QSpinBox>(window, "numOfScalesValue"); },
                   [](const QWidget& window) { moveSpin<QSpinBox>(window, "numOfScalesValue"); }},
        ResetValue{"Range Selection", openTyped<RangeSelectionProcess>,
                   [](const QWidget& window) {
                       const auto* box = widgetNamed<QCheckBox>(window, "lightness");
                       return box == nullptr ? QString()
                                             : (box->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
                   },
                   [](const QWidget& window) {
                       if (auto* box = widgetNamed<QCheckBox>(window, "lightness"))
                       {
                           box->setChecked(!box->isChecked());
                       }
                   }},
        ResetValue{"Total Variation Denoise", openTyped<TotalVariationDenoiseProcess>,
                   [](const QWidget& window) { return spinText<QDoubleSpinBox>(window, "epsValue"); },
                   [](const QWidget& window) { moveSpin<QDoubleSpinBox>(window, "epsValue"); }},
        ResetValue{"Profiled Starmask Recombination",
                   [](ImageViewControl&) {
                       WindowHandle<QWidget> window(new ProfiledStarmaskRecombinationProcess());
                       window->setAttribute(Qt::WA_DeleteOnClose, false);
                       return window;
                   },
                   [](const QWidget& window) { return spinText<QDoubleSpinBox>(window, "softenEdgesValue"); },
                   [](const QWidget& window) { moveSpin<QDoubleSpinBox>(window, "softenEdgesValue"); }},
    };
}

// Reset restores the value each dialog opens with: a value a person changed goes back to the one the window showed
// when it opened.
TEST(ProcessDefectRepairs, ResetRestoresTheValueEachDialogOpensWith)
{
    const std::unique_ptr<ContextView> target =
        floatView("target", 32u, 24u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return 0.1f + 0.01f * static_cast<float>((x + 2u * y + channel) % 16u);
        });
    ASSERT_TRUE(target != nullptr && *target);
    for (const ResetValue& value : resetValues())
    {
        SCOPED_TRACE(value.process);
        WindowHandle<QWidget> window = value.open(**target);
        ASSERT_NE(window, nullptr);
        const QString opened = value.read(*window);
        ASSERT_FALSE(opened.isEmpty());
        value.change(*window);
        ASSERT_NE(value.read(*window), opened) << "the case did not change the value";
        auto* box = widgetNamed<QDialogButtonBox>(*window, "buttonBox");
        ASSERT_NE(box, nullptr);
        QPushButton* reset = box->button(QDialogButtonBox::Reset);
        ASSERT_NE(reset, nullptr);
        ASSERT_TRUE(reset->isEnabled());
        reset->click();
        EXPECT_EQ(value.read(*window), opened);
    }
}

} // namespace
