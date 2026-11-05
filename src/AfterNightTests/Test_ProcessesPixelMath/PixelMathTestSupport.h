// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the PixelMath cases share: the window opened over a view as the launcher's entry point opens it - with a fixed
/// random seed, or with a run that stops itself - its dialog set and applied as a person does it, and a run of the
/// support unit made directly over an asset, outside any window.
#pragma once

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "processes/PixelMathProcess.h"
#include "processes/PixelMathProcessSupport.h"

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QString>
#include <QWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::processes::pixelmath
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// The seed a case's window draws `random()` from.
inline constexpr std::uint64_t kCaseSeed = 7u;

/// The registered action and the registry descriptor the window is launched through.
inline constexpr const char* kAction = "actionPixel_Math_2";
inline constexpr const char* kDescriptorId = "transforms.actionPixel_Math_2";

/// A window, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const;
};
using WindowHandle = std::unique_ptr<PixelMathProcess, WindowDeleter>;

/// Opens the window over a target, as the entry point does, without the delete-on-close the framework sets; its runs
/// draw `random()` from `seed`.
[[nodiscard]] WindowHandle openWindow(ImageViewControl* target, std::uint64_t seed = kCaseSeed);

/// Opens a window whose run asks for its own stop once it is admitted.
[[nodiscard]] WindowHandle openStoppingWindow(ImageViewControl* target);

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets the dialog as a person does: the text of an expression tab or a symbol list, a check box, a spin box, a combo
/// box by its text, a line edit.
void setExpression(QWidget& window, an::ui::processes::PixelMathExpressionTab tab, const QString& text);
void setPlainText(QWidget& window, const char* name, const QString& text);
void setChecked(QWidget& window, const char* name, bool checked);
void setInt(QWidget& window, const char* name, int value);
void setDouble(QWidget& window, const char* name, double value);
void setComboText(QWidget& window, const char* name, const QString& text);
void setLineText(QWidget& window, const char* name, const QString& text);

/// Presses the window's Apply; fails the case when it is disabled.
void pressApply(QWidget& window);

/// Turns the event loop until the window's runs, the views' work and the workers have all landed.
[[nodiscard]] bool settleWindow(ProcessTestContext& context, const PixelMathProcess& window);

/// Presses Apply and waits for the run to land: true when it landed.
[[nodiscard]] bool applyAndSettle(ProcessTestContext& context, PixelMathProcess& window);

/// The metadata a well-described image carries: its object and exposure, a linear meaning, a solution, and the cards a
/// file stated - the object, and a linear processing-state card.
[[nodiscard]] an::ImageMetadata describedMetadata();

/// A copy of an asset's image under other metadata, beside an explicit mask when one is given.
[[nodiscard]] std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const an::ImageMetadata& metadata,
                                                                   std::optional<an::ValidityMask> mask = std::nullopt);

/// An explicit validity mask of `planes` bit-planes over a width and a height, valid wherever `valid` says so.
[[nodiscard]] std::optional<an::ValidityMask>
explicitMask(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
             std::uint32_t planes,
             const std::function<bool(std::uint32_t plane, std::uint32_t x, std::uint32_t y)>& valid);

/// The metadata of an image whose validity is an explicit mask that travels beside it.
[[nodiscard]] an::ImageMetadata explicitMaskMetadata();

/// An asset over a fixture scene; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 std::string_view scene);

/// An asset of a description whose every sample is `value(x, y, channel)`, in fractions of full scale for a floating
/// representation and in codes for an integer one; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
filledAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
            std::uint32_t channels, an::SampleType sampleType, an::NumericDomain domain,
            const std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t channel)>& value);

/// What a run of the support unit makes of an asset, outside any window, and what it reports.
struct DirectRun
{
    an::Status status = an::Status::success();
    std::optional<an::consumer::ImageAsset> result;
    an::ui::processes::PixelMathRunReport report;
};

/// Runs the support unit over an asset under an admitted operation of the session.
[[nodiscard]] DirectRun runDirect(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& target,
                                  const an::ui::processes::PixelMathSettings& settings,
                                  const an::ui::processes::PixelMathImageSet& images = {},
                                  std::uint64_t seed = kCaseSeed);

/// The settings of one expression over every channel, replacing the target.
/// The stored samples of an image of any channel count, for the images the fixture layer does not describe - an image
/// with an alpha channel among them.
struct StoredSamples
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channelCount = 0u;
    std::vector<double> values;

    /// The stored sample at a column, a row and a channel.
    [[nodiscard]] double value(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const
    {
        return values[(static_cast<std::size_t>(channel) * height + y) * width + x];
    }
};

/// Reads every stored sample of an asset under a lease of its own; fails the case when it cannot.
[[nodiscard]] std::optional<StoredSamples> storedSamples(const an::consumer::ImageAsset& asset);

[[nodiscard]] an::ui::processes::PixelMathSettings expressionSettings(const std::string& expression);

} // namespace an::tests::processes::pixelmath
