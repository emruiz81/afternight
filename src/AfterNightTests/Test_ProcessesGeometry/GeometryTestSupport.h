// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the geometry and filter-array processes' cases share: the processes as one table - the action and the registry
/// descriptor each is launched through, how its window is opened and set to a mode its behaviour records hold,
/// where its result lands and the records it is held to - the mosaics and sequences the cases run on, and the run of
/// one Apply through a window as a person runs it.
#pragma once

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightEngine/Image/CfaPattern.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QRadioButton>
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

namespace an::tests::processes::geometry
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// A window of one of the processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const;
};
using WindowHandle = std::unique_ptr<QWidget, WindowDeleter>;

/// Where a process's result lands.
enum class Landing
{
    /// In the target's own document, or its sequence, as one step of its history.
    InPlace,

    /// In new views beside the target.
    NewViews,

    /// In one new view a top-level window opens, from four views it names.
    TopLevel
};

/// One of the processes this family holds.
struct GeometryProcess
{
    /// A name for a case's parameter: "Crop".
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

    /// Where its result lands.
    Landing landing = Landing::InPlace;

    /// The scene its window cases open on.
    const char* scene = "";

    /// Whether its window needs its view to itself.
    bool exclusive = false;

    /// The title a failure is reported under.
    const char* errorTitle = "";

    /// Opens its window over a target, as the launcher's entry point does, without the delete-on-close the framework
    /// sets; a top-level window ignores the target.
    std::function<WindowHandle(ImageViewControl* target)> open;

    /// Opens a window whose run asks for its own stop once it is admitted.
    std::function<WindowHandle(ImageViewControl* target)> openStopping;

    /// Sets an open window's widgets to a mode of its records, `-` for the dialog's own values, as a person sets them.
    /// Fails the case for a mode the table does not know.
    std::function<void(QWidget& window, std::string_view mode)> configure;
};

/// The processes, in a stable order.
[[nodiscard]] const std::vector<GeometryProcess>& geometryProcesses();

/// The process launched by an action, or null.
[[nodiscard]] const GeometryProcess* processOfAction(std::string_view action);

/// The process whose records live in a corpus file, or null.
[[nodiscard]] const GeometryProcess* processOfRecords(std::string_view records);

/// The processes whose result lands in a given way, by index into `geometryProcesses`.
[[nodiscard]] std::vector<std::size_t> processesLanding(Landing landing);

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets a spin box, a double spin box, a check box, a checkable group box, a radio button or a combo box's text by
/// object name, as a person does - emitting what the widget emits.
void setInt(QWidget& window, const char* name, int value);
void setDouble(QWidget& window, const char* name, double value);
void setChecked(QWidget& window, const char* name, bool checked);
void setGroupChecked(QWidget& window, const char* name, bool checked);
void clickRadio(QWidget& window, const char* name);
void setComboText(QWidget& window, const char* name, const QString& text);

/// Presses a window's Ok button, which is its Apply; fails the case when it is disabled.
void pressOk(QWidget& window);

/// Whether a window's runs have all landed.
[[nodiscard]] bool windowIdle(const QWidget& window);

/// Turns the event loop until a window's runs, the views' work and the workers have all landed.
[[nodiscard]] bool settleWindow(ProcessTestContext& context, const QWidget& window);

/// Presses Ok and waits for the run to land: true when it landed.
[[nodiscard]] bool applyAndSettle(ProcessTestContext& context, QWidget& window);

/// The metadata a well-described image carries: its object and exposure, a linear meaning, a solution, and the cards a
/// file stated - the object, and a linear processing-state card.
[[nodiscard]] an::ImageMetadata describedMetadata();

/// A copy of an asset's image under other metadata, beside an explicit mask when one is given.
[[nodiscard]] std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const an::ImageMetadata& metadata,
                                                                   std::optional<an::ValidityMask> mask = std::nullopt);

/// A copy of an asset's image in another representation, at corresponding full scales, under its own metadata.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
convertedTo(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& source, an::SampleType target);

/// Opens a sequence file in a view whose player demosaics a mosaic frame as it shows it, made active.
[[nodiscard]] ImageViewControl* openDemosaicedSequenceView(ProcessTestContext& context, const QString& path,
                                                           an::SequenceFormat format, const std::string& name);

/// The samples of a fixture scene, materialized; fails the case when they cannot be.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> sceneSamples(std::string_view scene);

/// The largest absolute difference between two sets of samples of one shape outside a band at the edges, NaN matching
/// NaN; infinite for two shapes.
[[nodiscard]] double largestInteriorDifference(const an::tests::fixtures::FixtureSamples& left,
                                               const an::tests::fixtures::FixtureSamples& right, std::uint32_t band);

} // namespace an::tests::processes::geometry
