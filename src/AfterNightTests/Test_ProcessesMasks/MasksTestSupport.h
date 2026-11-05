// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the mask, star and background processes' cases share: the processes as one table - the action and the
/// registry descriptor each is launched through, how its window is opened and set to a mode its behaviour records hold,
/// where its result lands and the records it is held to - the targets they open on, made from the fixture layer's
/// scenes and from images a case states, and the run of one Apply through a window as a person runs it.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "controls/ImageRTPreviewControl.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QCheckBox>
#include <QComboBox>
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

namespace an::tests::processes::masks
{

/// A window of one of the processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(ImageRTPreviewControl* window) const;
};
using WindowHandle = std::unique_ptr<ImageRTPreviewControl, WindowDeleter>;

/// Where an Apply's result lands.
enum class ResultLanding
{
    /// In the target's own document, as one step of its history.
    InPlace,

    /// In a new view of its own beside the target, the window closing after it.
    NewView
};

/// One of the processes this family holds.
struct MasksProcess
{
    /// A name for a case's parameter: "Binarize".
    const char* name = "";

    /// The registered action that launches it.
    const char* action = "";

    /// The id of the registry descriptor the launcher acts for.
    const char* descriptorId = "";

    /// The class the launcher opens.
    const char* className = "";

    /// The name it attaches to a view under, and titles its steps with.
    const char* processName = "";

    /// The file of its behaviour records, without the extension, or empty when it has none.
    const char* records = "";

    /// What the result's processing-state cards name the operation; empty for a process that records none.
    const char* stateOperation = "";

    /// Where an Apply's result lands.
    ResultLanding landing = ResultLanding::InPlace;

    /// The name a new view's document is given, after the target's: " - MASK".
    const char* newViewSuffix = "";

    /// Whether its dialog offers the channel selector.
    bool perChannel = false;

    /// Whether it reads a starless image of the target from another view, which a case opens beside the target as
    /// "starless" before the window opens: the corpus's own starless scene for a record, the target's background for
    /// the cases' own scenes.
    bool needsStarless = false;

    /// Whether it refuses a sequence, as a process that needs a still image does.
    bool stillImageOnly = false;

    /// The corpus scene its window cases open on, which `changingMode` changes.
    const char* scene = "";

    /// Whether its preview computes only once the window's Preview button asks, and shows the target until then.
    bool previewOnButton = false;

    /// Whether its preview shows the target, which the result is drawn over, rather than the result.
    bool previewShowsTarget = false;

    /// Whether a preview area shows what an Apply makes of that area alone: false where the result is a new image or
    /// is placed by positions over the whole target.
    bool previewArea = false;

    /// Whether its result moves smoothly with the samples, so an integer target's result is within about a code of the
    /// result of the very values held in Float64.
    bool smooth = false;

    /// A mode of its records - or of its own cases - that changes the cases' scene.
    const char* changingMode = "";

    /// How the step of `changingMode` over a colour image is described.
    const char* changingStep = "";

    /// Opens its window over a target, as the launcher's entry point does, without the delete-on-close the framework
    /// sets, and with the preview off.
    std::function<WindowHandle(ImageViewControl& target)> open;

    /// Opens a window whose body asks for its run's stop before it starts, so the engine sees a stopped execution at
    /// its first checkpoint.
    std::function<WindowHandle(ImageViewControl& target)> openStopping;

    /// Sets an open window's widgets to a mode, `-` for the dialog's own values, as a person sets them. Fails the case
    /// for a mode the table does not know.
    std::function<void(ImageRTPreviewControl& window, std::string_view mode)> configure;

    /// The history step an Apply of a mode over a target records, as the window describes it before the run.
    std::function<an::ui::ProcessStep(ImageViewControl& target, std::string_view mode)> describe;
};

/// The processes, in a stable order.
[[nodiscard]] const std::vector<MasksProcess>& masksProcesses();

/// The process launched by an action, or null.
[[nodiscard]] const MasksProcess* processOfAction(std::string_view action);

/// The process whose records live in a corpus file, or null.
[[nodiscard]] const MasksProcess* processOfRecords(std::string_view records);

/// The processes whose records the corpus holds.
[[nodiscard]] std::vector<std::size_t> processesWithRecords();

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets a spin box, a double spin box, a check box, a checkable group box or a combo box's index by object name.
void setInt(QWidget& window, const char* name, int value);
void setDouble(QWidget& window, const char* name, double value);
void setChecked(QWidget& window, const char* name, bool checked);
void setGroupChecked(QWidget& window, const char* name, bool checked);
void setIndex(QWidget& window, const char* name, int index);
void setText(QWidget& window, const char* name, const QString& text);

/// A view of a document over a fixture scene, its history begun as the application begins it; fails the case when the
/// scene cannot be made.
[[nodiscard]] ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene,
                                   const std::string& name = "scene");

/// A view of a document over a new asset, its history begun as the application begins it.
[[nodiscard]] ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name = "target");

/// Drops a view a case made, after taking it out of the context's views as closing it would.
void dropView(ProcessTestContext& context, ViewHandle& view);

/// What a document holds now, as neutral samples; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document);

/// The samples of an asset; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset);

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] bool settleWindow(const ImageRTPreviewControl& window);

/// Applies a window to its target as the Apply button does, and waits for the run and the view to land.
[[nodiscard]] bool applyAndSettle(ImageRTPreviewControl& window, ImageViewControl& target);

/// The views the context made since a count of them, in the order it made them: the new images a run published.
[[nodiscard]] std::vector<ImageViewControl*> viewsMadeSince(const ProcessTestContext& context, std::size_t count);

/// Applies a window and returns what its result holds: the target's document for a process that writes in place, the
/// one new view's document for one that makes a new image. Fails the case when the run does not land as the process
/// lands its result.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> applyAndRead(ProcessTestContext& context,
                                                                              const MasksProcess& process,
                                                                              ImageRTPreviewControl& window,
                                                                              ImageViewControl& target);

/// The checked-in records of a corpus file, by its name without the extension; fails the case when it cannot be read.
[[nodiscard]] std::vector<an::tests::fixtures::Expectation> recordsOf(std::string_view records);

/// The mode a record captured, from its id `<file>.<mode>/<input>`: `-` for the file's default.
[[nodiscard]] std::string modeOf(const an::tests::fixtures::Expectation& record);

/// The largest absolute difference between two sets of samples of one shape, NaN matching NaN and a NaN against a
/// number counting as infinite.
[[nodiscard]] double largestDifference(const an::tests::fixtures::FixtureSamples& left,
                                       const an::tests::fixtures::FixtureSamples& right);

} // namespace an::tests::processes::masks
