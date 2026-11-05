// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the channel and conversion actions' cases share: views of the fixture layer's scenes and of sequences written
/// from them, the registered actions and the image menu's operations run as the menu runs them, the wait for what a
/// run publishes - new views, an edit shown, a message - and what a document, a view's sequence or a record holds, as
/// neutral samples.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "app/ImageOperationsCoordinator.h"
#include "processes/ChannelActions.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::processes::channels
{

/// A view of a document over a fixture scene, its history begun as the application begins it; fails the case when the
/// scene cannot be made.
[[nodiscard]] ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene,
                                   const std::string& name = "scene");

/// A view of a document over a new asset, its history begun as the application begins it.
[[nodiscard]] ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name = "target");

/// Makes a view the active one and lists it among the context's views, as the application does when it is focused.
void activate(ProcessTestContext& context, ImageViewControl& view);

/// Drops a view a case made, after taking it out of the context's views as closing it would.
void dropView(ProcessTestContext& context, ViewHandle& view);

/// What a document holds now, as neutral samples; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document);

/// What an asset holds, as neutral samples; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> assetSamples(const an::consumer::ImageAsset& asset);

/// The checked-in records of a corpus file, by its name without the extension; fails the case when it cannot be read.
[[nodiscard]] std::vector<an::tests::fixtures::Expectation> recordsOf(std::string_view records);

/// The mode a record captured, from its id `<file>.<mode>/<input>`: `-` for the file's default.
[[nodiscard]] std::string modeOf(const an::tests::fixtures::Expectation& record);

/// Runs a registered channel or conversion action, as the image menu does, on the active view.
void runRegisteredAction(std::string_view action);

/// The image operations coordinator the application builds, over the installed context and the non-interactive
/// processes' operations.
[[nodiscard]] an::ui::ImageOperationsCoordinator imageOperations();

/// Turns the event loop until a run's workers have finished and what they queued has landed, and the views have
/// finished their own work.
[[nodiscard]] bool settleAll(ProcessTestContext& context);

/// Turns the event loop until the context has made `count` views after the first `before`, or a message was shown
/// after the first `messagesBefore`; then settles. Returns the views made, in order.
[[nodiscard]] std::vector<ImageViewControl*> awaitNewViews(ProcessTestContext& context, std::size_t before,
                                                           std::size_t count, int messagesBefore);

/// Turns the event loop until a message was shown after the first `before`; then settles.
[[nodiscard]] bool awaitMessage(ProcessTestContext& context, int before);

/// Writes an image list of fixture scenes, one frame each, into a directory, and returns its path; fails the case
/// when it cannot.
[[nodiscard]] QString writeSceneSequence(const an::consumer::EngineSession& session, const QTemporaryDir& directory,
                                         const std::vector<std::string_view>& scenes,
                                         an::SequenceFormat format = an::SequenceFormat::ImageList,
                                         const QString& name = QStringLiteral("frames"));

/// Opens a sequence file in a view, as the application opens one, made the active view; fails the case when it cannot.
[[nodiscard]] ImageViewControl* openSequenceView(ProcessTestContext& context, const QString& path,
                                                 an::SequenceFormat format, const std::string& name = "frames");

/// Every frame of the sequence a view shows, read in order as neutral samples; fails the case when it cannot.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureSamples>
sequenceFrames(const an::consumer::EngineSession& session, const ImageViewControl& view, bool autoDebayer = false);

/// The images an action makes of one image, run directly through the table: what a view or a frame of it is expected
/// to hold. Fails the case when the action refuses.
[[nodiscard]] std::vector<an::tests::fixtures::FixtureSamples>
expectedOutputs(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& image,
                const process_utils::ChannelAction& action);

/// The largest absolute difference between two sets of samples of one shape, NaN matching NaN and a NaN against a
/// number counting as infinite; infinite as well for two shapes.
[[nodiscard]] double largestDifference(const an::tests::fixtures::FixtureSamples& left,
                                       const an::tests::fixtures::FixtureSamples& right);

/// The text of the history step a view begins with, as its factory was asked for it.
[[nodiscard]] std::string madeHistoryOf(const ProcessTestContext& context, const ImageViewControl* view);

/// The view a made view was asked to sit beside.
[[nodiscard]] ImageViewControl* madeNeighbourOf(const ProcessTestContext& context, const ImageViewControl* view);

} // namespace an::tests::processes::channels
