// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the pointwise and tone processes' cases share: the eight processes as one table - the action and the registry
/// descriptor each is launched through, how its window is opened and set to a mode its behaviour records hold,
/// and the records it is held to - the targets they open on, made from the fixture layer's scenes, and the run of one
/// Apply through a window as a person runs it.
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

namespace an::tests::processes::pointwise
{

/// A window of one of the eight processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(ImageRTPreviewControl* window) const;
};
using WindowHandle = std::unique_ptr<ImageRTPreviewControl, WindowDeleter>;

/// A window's body run twice as an Apply runs it over one image: into a copy of the image, and into a destination of
/// the image's shape that starts holding none of it, every byte of it a marker.
struct BodyOverDestinations
{
    /// What the window declared its body writes, for its values.
    an::ui::ProcessBodyCoverage coverage = an::ui::ProcessBodyCoverage::WritesPart;

    /// What the body left in each destination; empty when that run failed, which fails the case.
    std::optional<an::tests::fixtures::FixtureSamples> overCopy;
    std::optional<an::tests::fixtures::FixtureSamples> overUnwritten;
};

/// One of the eight processes this family holds.
struct PointwiseProcess
{
    /// A name for a case's parameter: "BrightnessContrast".
    const char* name = "";

    /// The registered action that launches it.
    const char* action = "";

    /// The id of the registry descriptor the launcher acts for.
    const char* descriptorId = "";

    /// The class the launcher opens.
    const char* className = "";

    /// The name it attaches to a view under, and titles its steps with.
    const char* processName = "";

    /// The file of its behaviour records, without the extension: "brightness-and-contrast".
    const char* records = "";

    /// The name of the operation on the processing-state cards, as the result's cards state it.
    const char* stateOperation = "";

    /// Whether it refuses a grey image.
    bool colourOnly = false;

    /// Whether its dialog offers the channel selector.
    bool perChannel = false;

    /// Whether its dialog offers Apply at its own values, which for most of the eight leave the image as it is.
    bool applyAtDefaults = false;

    /// A mode of its records that changes the scenes, which the cases that need a result use.
    const char* changingMode = "";

    /// Opens its window over a target, as the launcher's entry point does, without the delete-on-close the framework
    /// sets, and with the preview off.
    std::function<WindowHandle(ImageViewControl& target)> open;

    /// Opens a window whose body asks for its run's stop before it starts, so the engine sees a stopped execution at
    /// its first checkpoint.
    std::function<WindowHandle(ImageViewControl& target)> openStopping;

    /// Sets an open window's widgets to a mode of its records, `-` for the dialog's own values, as a person sets them.
    /// Fails the case for a mode the table does not know.
    std::function<void(ImageRTPreviewControl& window, std::string_view mode)> configure;

    /// The history step an Apply of a mode over a target records, as the window describes it before the run.
    std::function<an::ui::ProcessStep(ImageViewControl& target, std::string_view mode)> describe;

    /// Runs the body of a window set to a mode over a target, as an Apply runs it, into a copy of the target's image
    /// and into a destination that starts holding none of it.
    std::function<BodyOverDestinations(ImageViewControl& target, std::string_view mode)> runBodyOverDestinations;

    /// How the history describes the step of `changingMode` over a colour image.
    const char* changingStep = "";
};

/// The eight processes, in a stable order.
[[nodiscard]] const std::vector<PointwiseProcess>& pointwiseProcesses();

/// The process whose records live in a corpus file, or null.
[[nodiscard]] const PointwiseProcess* processOfRecords(std::string_view records);

/// The process launched by an action, or null.
[[nodiscard]] const PointwiseProcess* processOfAction(std::string_view action);

/// A view of a document over a fixture scene, its history begun as the application begins it; fails the case when the
/// scene cannot be made.
[[nodiscard]] ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene,
                                   const std::string& name = "scene");

/// A view of a document over a new asset, its history begun as the application begins it.
[[nodiscard]] ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name = "target");

/// Drops a view a case made, after taking it out of the context's views as closing it would, so a window opened later
/// in the case lists only the views that still exist.
void dropView(ProcessTestContext& context, ViewHandle& view);

/// What a document holds now, as neutral samples; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document);

/// Turns the event loop until a window's runs and its preview view's work have all landed.
[[nodiscard]] bool settleWindow(const ImageRTPreviewControl& window);

/// Applies a window to its target as the Apply button does, and waits for the run and the view to land.
[[nodiscard]] bool applyAndSettle(ImageRTPreviewControl& window, ImageViewControl& target);

/// The checked-in records of a corpus file, by its name without the extension; fails the case when it cannot be read.
[[nodiscard]] std::vector<an::tests::fixtures::Expectation> recordsOf(std::string_view records);

/// The mode a record captured, from its id `<file>.<mode>/<input>`: `-` for the file's default.
[[nodiscard]] std::string modeOf(const an::tests::fixtures::Expectation& record);

/// The largest absolute difference between two sets of samples of one shape, NaN matching NaN and a NaN against a
/// number counting as infinite.
[[nodiscard]] double largestDifference(const an::tests::fixtures::FixtureSamples& left,
                                       const an::tests::fixtures::FixtureSamples& right);

} // namespace an::tests::processes::pointwise
