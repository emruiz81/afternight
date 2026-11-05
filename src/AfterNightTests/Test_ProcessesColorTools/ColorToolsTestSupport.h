// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the colour calibration tools' cases share: the four processes as one table - the action and the registry
/// descriptor each is launched through, how its window is opened and set to a mode its behaviour records hold,
/// the button that applies it and the records it is held to - the reference views a run reads beside its target, and
/// the run of one Apply through a window as a person runs it.
#pragma once

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::processes::colortools
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// A window of one of the processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const;
};
using WindowHandle = std::unique_ptr<QWidget, WindowDeleter>;

/// The name of the view a case opens as a run's reference.
inline constexpr const char* kReferenceView = "reference";

/// One of the processes this family holds.
struct ColorTool
{
    /// A name for a case's parameter: "Scnr".
    const char* name = "";

    /// The registered action that launches it.
    const char* action = "";

    /// The id of the registry descriptor the launcher acts for.
    const char* descriptorId = "";

    /// The class the launcher opens.
    const char* className = "";

    /// The name it attaches to a view under, and titles its steps with.
    const char* processName = "";

    /// The file of its behaviour records, without the extension.
    const char* records = "";

    /// The scene its window cases open on.
    const char* scene = "";

    /// Whether its window needs its view to itself.
    bool exclusive = false;

    /// Whether a run reads a reference view the case opens beside the target - the linear match's - rather than the
    /// target itself, which is every other tool's default.
    bool needsReference = false;

    /// The button that applies the dialog.
    QDialogButtonBox::StandardButton apply = QDialogButtonBox::Apply;

    /// Whether the window closes once its result is shown.
    bool closesWhenShown = true;

    /// The title a failure is reported under.
    const char* errorTitle = "";

    /// The sentence a failure's text opens with, before its reason.
    const char* errorLead = "";

    /// Opens its window over a target, as the launcher's entry point does, without the delete-on-close the framework
    /// sets.
    std::function<WindowHandle(ImageViewControl* target)> open;

    /// Opens a window whose run asks for its own stop once it is admitted.
    std::function<WindowHandle(ImageViewControl* target)> openStopping;

    /// Sets an open window's widgets to a mode of its records, `-` for the dialog's own values, as a person sets them.
    /// Fails the case for a mode the table does not know.
    std::function<void(QWidget& window, std::string_view mode)> configure;

    /// Makes a window read a view by name as its reference - the linear match's reference, and both references of the
    /// colour calibration and the background neutralization; nothing for SCNR.
    std::function<void(QWidget& window, const QString& view)> chooseReference;
};

/// The processes, in a stable order: colour calibration, background neutralization, linear match, SCNR.
[[nodiscard]] const std::vector<ColorTool>& colorTools();

/// The process whose records live in a corpus file, or null.
[[nodiscard]] const ColorTool* toolOfRecords(std::string_view records);

/// The process of a name, or null.
[[nodiscard]] const ColorTool* toolNamed(std::string_view name);

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets a double spin box, a check box, a checkable group box - emitting its click, as a person's click does - a spin
/// box or a combo box's text by object name.
void setDouble(QWidget& window, const char* name, double value);
void setInt(QWidget& window, const char* name, int value);
void setChecked(QWidget& window, const char* name, bool checked);
void clickGroup(QWidget& window, const char* name, bool checked);
void setComboText(QWidget& window, const char* name, const QString& text);

/// The button of a window that applies its dialog, or null.
[[nodiscard]] QPushButton* applyButtonOf(QWidget& window, const ColorTool& tool);

/// Presses a window's apply button; fails the case when it is disabled.
void pressApply(QWidget& window, const ColorTool& tool);

/// Whether a window's runs have all landed.
[[nodiscard]] bool windowIdle(const QWidget& window);

/// Turns the event loop until a window's runs, the views' work and the workers have all landed.
[[nodiscard]] bool settleWindow(ProcessTestContext& context, const QWidget& window);

/// Presses apply and waits for the run to land: true when it landed.
[[nodiscard]] bool applyAndSettle(ProcessTestContext& context, QWidget& window, const ColorTool& tool);

/// The asset of a fixture scene; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 std::string_view scene);

/// The samples of a fixture scene, materialized; fails the case when they cannot be.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> sceneSamples(std::string_view scene);

/// A copy of an asset's image under other metadata, beside an explicit mask when one is given.
[[nodiscard]] std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const an::ImageMetadata& metadata,
                                                                   std::optional<an::ValidityMask> mask = std::nullopt);

/// A copy of an asset's image in another representation, at corresponding full scales, under its own metadata.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
convertedTo(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& source, an::SampleType target);

/// The metadata a well-described image carries: its object and exposure, a linear meaning, a solution, and the cards a
/// file stated.
[[nodiscard]] an::ImageMetadata describedMetadata();

/// A sample of a record or a result as a fraction of full scale: an unsigned integer of its largest value, a signed one
/// counted from its smallest, a floating sample as it is.
[[nodiscard]] double fractionOf(const an::tests::fixtures::FixtureSamples& samples, std::uint32_t x, std::uint32_t y,
                                std::uint32_t channel);

/// The largest absolute difference between two sets of samples of one shape, as fractions of full scale; infinite for
/// two shapes.
[[nodiscard]] double largestFractionDifference(const an::tests::fixtures::FixtureSamples& left,
                                               const an::tests::fixtures::FixtureSamples& right);

/// The median of values, averaging the two middle ones of an even count, as the engine's exact rank does; NaN for none.
[[nodiscard]] double medianOf(std::vector<double> values);

/// The median absolute deviation of values about their median.
[[nodiscard]] double deviationOf(const std::vector<double>& values);

/// The samples of one channel as fractions of full scale that a predicate of the position and the fraction keeps, in
/// row order.
template <typename KeepT>
[[nodiscard]] std::vector<double> channelFractions(const an::tests::fixtures::FixtureSamples& samples,
                                                   std::uint32_t channel, KeepT keep)
{
    std::vector<double> values;
    for (std::uint32_t y = 0u; y < samples.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < samples.width(); ++x)
        {
            const double value = fractionOf(samples, x, y, channel);
            if (keep(x, y, value))
            {
                values.push_back(value);
            }
        }
    }
    return values;
}

} // namespace an::tests::processes::colortools
