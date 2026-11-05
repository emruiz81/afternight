// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the combination processes' cases share: the processes as one table - the action and the registry descriptor
/// each is launched through, how its window is opened over a target and the views it reads beside it, how its widgets
/// are set to a mode its behaviour records hold, how its run is started and known to have landed, where its result
/// lands and the records it is held to - the targets and side views they open, made from the fixture layer's scenes
/// and from images a case states, and the run of one Apply through a window as a person runs it.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "controls/ImageRTPreviewControl.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
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

namespace an::tests::processes::combinations
{

/// A window of one of the processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const;
};
using WindowHandle = std::unique_ptr<QWidget, WindowDeleter>;

/// Where an Apply's result lands.
enum class ResultLanding
{
    /// In the target's own document, as one step of its history.
    InPlace,

    /// In a new view of its own beside the target.
    NewView
};

/// A view a process reads beside its target, named as the process's selectors show it.
struct SideView
{
    std::string name;
    ViewHandle view;
};

/// The views a case opens for one run: the target, and what the process reads beside it.
struct Targets
{
    ViewHandle target;
    std::vector<SideView> sides;

    /// The side view of a name, or null.
    [[nodiscard]] ImageViewControl* side(std::string_view name) const;
};

/// One of the processes this family holds.
struct CombinationsProcess
{
    /// A name for a case's parameter: "ImageBlend".
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

    /// What the result's processing-state cards name the operation; empty where the process records nothing or keeps
    /// the sources' linearity.
    const char* stateOperation = "";

    /// Where an Apply's result lands.
    ResultLanding landing = ResultLanding::InPlace;

    /// Whether the window is a top-level one, opened over no view and choosing its target among the open views.
    bool topLevel = false;

    /// Whether the window is the framework's real-time preview window, with the preview, the history step and the
    /// run telemetry that come with it.
    bool realtimePreview = true;

    /// Whether the window offers the framework's preview box, whose preview shows what an Apply makes.
    bool previewBox = true;

    /// Whether its preview offers an area that shows what an Apply makes of that area alone.
    bool previewArea = false;

    /// Whether the result keeps the target's representation: a combination into the target's own image does, one
    /// written as a new Float32 image or in the higher of two images' representations does not.
    bool keepsRepresentation = false;

    /// Whether it refuses a sequence, as a process that needs a still image does.
    bool stillImageOnly = false;

    /// Whether its result moves smoothly with the samples, so an integer target's result is within about a code of
    /// the result of the very values held in Float64.
    bool smooth = false;

    /// Whether the run leaves the target as it was, as the comparison does.
    bool leavesTarget = false;

    /// The corpus scene its window cases open on.
    const char* scene = "";

    /// A mode of its records - or of its own cases - the window cases run.
    const char* changingMode = "";

    /// How the step of `changingMode` over the scene is described, for a process that records one.
    const char* changingStep = "";

    /// Opens the views the process reads beside a target over a scene's samples, named as `configure` expects them,
    /// and adds them to the context's views.
    std::function<std::vector<SideView>(ProcessTestContext& context, const an::tests::fixtures::FixtureSamples& scene,
                                        const std::string& sceneName)>
        openSides;

    /// Opens its window over a target - null for a top-level window - as the launcher's entry point does, without the
    /// delete-on-close the framework sets, and with the preview off.
    std::function<WindowHandle(ImageViewControl* target)> open;

    /// Opens a window whose body asks for its run's stop before it starts, so the engine sees a stopped execution at
    /// its first checkpoint.
    std::function<WindowHandle(ImageViewControl* target)> openStopping;

    /// Sets an open window's widgets to a mode, `-` for the dialog's own values, as a person sets them, over the
    /// targets a case opened. Fails the case for a mode the table does not know.
    std::function<void(QWidget& window, const Targets& targets, std::string_view mode)> configure;

    /// Starts the window's run as its Apply or Ok button does.
    std::function<void(QWidget& window)> apply;

    /// Whether the window's runs and its preview's work have all landed.
    std::function<bool(const QWidget& window)> idle;

    /// How many runs the window published.
    std::function<std::uint64_t(const QWidget& window)> committed;

    /// The history step an Apply of a mode over the targets records, as the window describes it before the run.
    std::function<an::ui::ProcessStep(const Targets& targets, std::string_view mode)> describe;
};

/// The processes, in a stable order.
[[nodiscard]] const std::vector<CombinationsProcess>& combinationsProcesses();

/// The process launched by an action, or null.
[[nodiscard]] const CombinationsProcess* processOfAction(std::string_view action);

/// The process of a name; fails the case for a name the table does not have.
[[nodiscard]] const CombinationsProcess& processNamed(std::string_view name);

/// The processes whose records the corpus holds.
[[nodiscard]] std::vector<std::size_t> processesWithRecords();

/// The processes a predicate holds for, by index.
[[nodiscard]] std::vector<std::size_t> processesWhere(const std::function<bool(const CombinationsProcess&)>& holds);

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets a spin box, a double spin box, a check box or a combo box by object name.
void setInt(QWidget& window, const char* name, int value);
void setDouble(QWidget& window, const char* name, double value);
void setChecked(QWidget& window, const char* name, bool checked);
void setIndex(QWidget& window, const char* name, int index);
void setCurrentText(QWidget& window, const char* name, const QString& text);

/// Selects a view by name in a combo box; fails the case when the box does not offer it.
void selectView(QWidget& window, const char* name, const std::string& view);

/// A scene's samples; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> sceneSamples(std::string_view scene);

/// A new Float32 asset of a scene's values.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 const an::tests::fixtures::FixtureSamples& scene);

/// A new grey Float32 asset of one plane of a scene's values.
[[nodiscard]] std::optional<an::consumer::ImageAsset> planeAsset(const an::consumer::EngineSession& session,
                                                                 const an::tests::fixtures::FixtureSamples& scene,
                                                                 std::uint32_t plane);

/// A view of a document over a fixture scene, its history begun as the application begins it; fails the case when the
/// scene cannot be made.
[[nodiscard]] ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene,
                                   const std::string& name = "scene");

/// A view of a document over a new asset, its history begun as the application begins it.
[[nodiscard]] ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name = "target");

/// Opens a target over an asset and, beside it, the views the process reads, each added to the context's views; the
/// target too when the process is a top-level one, which finds it among them.
[[nodiscard]] Targets openTargets(ProcessTestContext& context, const CombinationsProcess& process,
                                  an::consumer::ImageAsset&& asset, const std::string& name = "target",
                                  const std::string& sceneName = "");

/// The process's scene, opened as a target with its side views.
[[nodiscard]] Targets openScene(ProcessTestContext& context, const CombinationsProcess& process);

/// Drops a view a case made, after taking it out of the context's views as closing it would.
void dropView(ProcessTestContext& context, ViewHandle& view);

/// Drops every view of a case's targets.
void dropTargets(ProcessTestContext& context, Targets& targets);

/// The value in the unit interval a code of a representation stands for.
[[nodiscard]] double unitValueOf(double code, an::tests::fixtures::FixtureSampleType type);

/// Samples restated as Float32 fractions of full scale.
[[nodiscard]] an::tests::fixtures::FixtureSamples unitSamples(const an::tests::fixtures::FixtureSamples& samples);

/// What a document holds now, as neutral samples; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document);

/// The samples of an asset; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset);

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] bool settleWindow(const CombinationsProcess& process, const QWidget& window);

/// Applies a window to its targets as the Apply button does, and waits for the run and the views to land.
[[nodiscard]] bool applyAndSettle(const CombinationsProcess& process, QWidget& window, const Targets& targets);

/// The views the context made since a count of them, in the order it made them: the new images a run published.
[[nodiscard]] std::vector<ImageViewControl*> viewsMadeSince(const ProcessTestContext& context, std::size_t count);

/// What a run's result holds: the target's document, or the new view's; null when the run made none.
[[nodiscard]] const an::ui::ImageDocument* resultDocument(const ProcessTestContext& context,
                                                          const CombinationsProcess& process, const Targets& targets,
                                                          std::size_t viewsBefore);

/// Applies a window and returns what its result holds: the target's document for a process that writes in place, the
/// one new view's document for one that makes a new image. Fails the case when the run does not land as the process
/// lands its result.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples>
applyAndRead(ProcessTestContext& context, const CombinationsProcess& process, QWidget& window, const Targets& targets);

/// The checked-in records of a corpus file, by its name without the extension; fails the case when it cannot be read.
[[nodiscard]] std::vector<an::tests::fixtures::Expectation> recordsOf(std::string_view records);

/// The mode a record captured, from its id `<file>.<mode>/<input>`: `-` for the file's default.
[[nodiscard]] std::string modeOf(const an::tests::fixtures::Expectation& record);

/// The largest absolute difference between two sets of samples of one shape, NaN matching NaN and a NaN against a
/// number counting as infinite.
[[nodiscard]] double largestDifference(const an::tests::fixtures::FixtureSamples& left,
                                       const an::tests::fixtures::FixtureSamples& right);

} // namespace an::tests::processes::combinations
