// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The channel and conversion actions' shared helpers: scene and sequence views, running an action as the menu runs
/// it, waiting for what it publishes, and reading what a document, a sequence or a record holds.

#include "Test_ProcessesChannels/ChannelsTestSupport.h"

#include "processes/NonInteractiveProcesses.h"

#include "app/AppContextHelpers.h"
#include "app/FileCompanions.h"
#include "app/ImageOperationsCoordinator.h"
#include "app/ChannelExtractor.h"
#include "controls/ProcessCompanions.h"
#include "controls/SequenceContainers.h"
#include "session/ExecutionScope.h"

#include <QCoreApplication>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace an::tests::processes::channels
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene, const std::string& name)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << "no fixture scene " << std::string(scene);
    if (image == nullptr)
    {
        return ViewHandle();
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return ViewHandle();
    }
    an::Result<an::consumer::ImageAsset> asset = adapter::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return ViewHandle();
    }
    return assetView(std::move(*asset), name);
}

ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name)
{
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return ViewHandle();
    }
    return makeTargetView(name, std::move(*document));
}

void activate(ProcessTestContext& context, ImageViewControl& view)
{
    const std::vector<ImageViewControl*> listed = context.getListOfViewControls();
    if (std::find(listed.begin(), listed.end(), &view) == listed.end())
    {
        context.addViewControl(&view);
    }
    context.setActiveImageViewControl(&view);
}

void dropView(ProcessTestContext& context, ViewHandle& view)
{
    if (view == nullptr)
    {
        return;
    }
    context.removeViewControl(view.get(), false);
    view.reset();
    QCoreApplication::processEvents();
}

std::optional<fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    EXPECT_NE(asset, nullptr);
    if (asset == nullptr)
    {
        return std::nullopt;
    }
    return assetSamples(*asset);
}

std::optional<fixtures::FixtureSamples> assetSamples(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

std::vector<fixtures::Expectation> recordsOf(std::string_view records)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return {};
    }
    std::optional<std::vector<fixtures::Expectation>> read =
        fixtures::readExpectationFile(*directory / "behavior" / (std::string(records) + ".expectations"), error);
    EXPECT_TRUE(read.has_value()) << error;
    return read.has_value() ? std::move(*read) : std::vector<fixtures::Expectation>{};
}

std::string modeOf(const fixtures::Expectation& record)
{
    const std::size_t dot = record.id.find('.');
    const std::size_t slash = record.id.find('/');
    if (dot == std::string::npos || slash == std::string::npos || slash < dot)
    {
        return std::string();
    }
    const std::string mode = record.id.substr(dot + 1u, slash - dot - 1u);
    return mode == "default" ? std::string("-") : mode;
}

void runRegisteredAction(std::string_view action)
{
    const auto* descriptor = shared::registered("non_interactive." + std::string(action));
    ASSERT_NE(descriptor, nullptr) << std::string(action);
    ASSERT_TRUE(static_cast<bool>(descriptor->launch_non_interactive)) << std::string(action);
    descriptor->launch_non_interactive();
}

an::ui::ImageOperationsCoordinator imageOperations()
{
    an::ui::ImageOperationsCoordinator::Dependencies dependencies;
    dependencies.activeViewControl = []() { return process_utils::currentActiveViewControl(); };
    dependencies.listViewControls = []() { return process_utils::listViewControls(); };
    dependencies.messageParent = []() { return process_utils::currentMainWindow(); };
    dependencies.notifyErrorMessage = [](const QString& title, const QString& message) {
        process_utils::notifyErrorMessage(process_utils::currentMainWindow(), title, message);
    };
    dependencies.nonInteractive = an::ui::nonInteractiveImageOperations();
    return an::ui::ImageOperationsCoordinator(dependencies);
}

bool settleAll(ProcessTestContext& context)
{
    const auto idle = [&context]() {
        if (QThreadPool::globalInstance()->activeThreadCount() != 0)
        {
            return false;
        }
        for (ImageViewControl* view : context.madeViews)
        {
            if (!view->isWorkIdle())
            {
                return false;
            }
        }
        // A view a case made is listed too, and only the case knows whether it still exists; the active one does.
        ImageViewControl* active = context.getActiveImageViewControl();
        return active == nullptr || active->isWorkIdle();
    };
    if (!pumpUntil(idle))
    {
        return false;
    }
    pumpQuietly();
    return idle();
}

std::vector<ImageViewControl*> awaitNewViews(ProcessTestContext& context, std::size_t before, std::size_t count,
                                             int messagesBefore)
{
    const bool landed = pumpUntil(
        [&]() { return context.madeViews.size() >= before + count || context.messages.size() > messagesBefore; });
    EXPECT_TRUE(landed) << "nothing was published";
    EXPECT_TRUE(settleAll(context));
    if (context.madeViews.size() < before)
    {
        return {};
    }
    return std::vector<ImageViewControl*>(context.madeViews.begin() + static_cast<std::ptrdiff_t>(before),
                                          context.madeViews.end());
}

bool awaitMessage(ProcessTestContext& context, int before)
{
    const bool shown = pumpUntil([&]() { return context.messages.size() > before; });
    return shown && settleAll(context);
}

QString writeSceneSequence(const an::consumer::EngineSession& session, const QTemporaryDir& directory,
                           const std::vector<std::string_view>& scenes, an::SequenceFormat format, const QString& name)
{
    const QString path = directory.filePath(
        name + (format == an::SequenceFormat::ImageList ? QStringLiteral(".anseq") : QStringLiteral(".ser")));
    an::Result<an::ui::SequenceWriter> writer = an::ui::SequenceWriter::create(
        path, an::ui::SequenceWriteSettings{.container = format, .frameCount = scenes.size()});
    EXPECT_TRUE(writer.hasValue()) << writer.status().message();
    if (!writer.hasValue())
    {
        return QString();
    }
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return QString();
    }
    for (const std::string_view scene : scenes)
    {
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
        EXPECT_NE(image, nullptr);
        if (image == nullptr)
        {
            return QString();
        }
        std::string error;
        const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
        EXPECT_TRUE(samples.has_value()) << error;
        if (!samples.has_value())
        {
            return QString();
        }
        an::Result<an::consumer::ImageAsset> asset = adapter::makeImageAsset(session, *image, *samples);
        EXPECT_TRUE(asset.hasValue()) << asset.status().message();
        if (!asset.hasValue())
        {
            return QString();
        }
        an::Result<an::consumer::AssetReadLease> lease = asset->read();
        EXPECT_TRUE(lease.hasValue());
        if (!lease.hasValue())
        {
            return QString();
        }
        const an::Status appended = writer->append(scope->engine(), scope->execution(), lease->image());
        EXPECT_TRUE(appended.ok()) << appended.message();
        if (appended.failed())
        {
            return QString();
        }
    }
    const an::Result<an::SequenceDescriptor> published = writer->finish(scope->execution());
    EXPECT_TRUE(published.hasValue()) << published.status().message();
    return published.hasValue() ? path : QString();
}

ImageViewControl* openSequenceView(ProcessTestContext& context, const QString& path, an::SequenceFormat format,
                                   const std::string& name)
{
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context.engineSession(), path, format, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    document->setNames(an::ui::DocumentNames{.displayName = QString::fromStdString(name), .sourcePath = path});
    ImageViewControl* view =
        context.createNewImageViewControlFromSequence(nullptr, std::move(*document), nullptr, false, "Sequence opened",
                                                      ViewControlMode::standard, nullptr, false, false);
    EXPECT_NE(view, nullptr);
    if (view == nullptr)
    {
        return nullptr;
    }
    EXPECT_TRUE(settleView(*view));
    activate(context, *view);
    // The view made by the context's factory is listed among its made views; the cases count views made after it.
    return view;
}

std::vector<fixtures::FixtureSamples> sequenceFrames(const an::consumer::EngineSession& session,
                                                     const ImageViewControl& view, bool autoDebayer)
{
    std::vector<fixtures::FixtureSamples> frames;
    const std::optional<an::ui::DocumentSequence> sequence = view.document().sequence();
    EXPECT_TRUE(sequence.has_value()) << "the view shows no sequence";
    if (!sequence.has_value())
    {
        return frames;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return frames;
    }
    an::Result<std::unique_ptr<an::ui::IProcessFrameReader>> reader =
        an::ui::openProcessFrames(session, *sequence, autoDebayer);
    EXPECT_TRUE(reader.hasValue()) << reader.status().message();
    if (!reader.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<an::ui::ProcessSequenceFrame>> frame =
            (*reader)->next(scope->engine(), scope->execution());
        EXPECT_TRUE(frame.hasValue()) << frame.status().message();
        if (!frame.hasValue() || !frame->has_value())
        {
            break;
        }
        std::optional<fixtures::FixtureSamples> samples = assetSamples((**frame).asset);
        if (!samples.has_value())
        {
            break;
        }
        frames.push_back(std::move(*samples));
    }
    scope->finish(an::Status::success());
    return frames;
}

std::vector<fixtures::FixtureSamples> expectedOutputs(const an::consumer::EngineSession& session,
                                                      const an::consumer::ImageAsset& image,
                                                      const process_utils::ChannelAction& action)
{
    std::vector<fixtures::FixtureSamples> outputs;
    an::Result<an::consumer::AssetReadLease> lease = image.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return outputs;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    EXPECT_TRUE(scope.hasValue()) << scope.status().message();
    if (!scope.hasValue())
    {
        return outputs;
    }
    an::Result<std::vector<an::consumer::ImageAsset>> made =
        process_utils::runChannelAction(*scope, an::ui::ProcessSource::of(*lease), action);
    EXPECT_TRUE(made.hasValue()) << action.action << ": " << made.status().message();
    scope->finish(an::Status::success());
    if (!made.hasValue())
    {
        return outputs;
    }
    for (const an::consumer::ImageAsset& asset : *made)
    {
        std::optional<fixtures::FixtureSamples> samples = assetSamples(asset);
        if (samples.has_value())
        {
            outputs.push_back(std::move(*samples));
        }
    }
    return outputs;
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = left.value(x, y, channel);
                const double b = right.value(x, y, channel);
                if (std::isnan(a) && std::isnan(b))
                {
                    continue;
                }
                largest = (std::max)(largest, std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity()
                                                                             : std::abs(a - b));
            }
        }
    }
    return largest;
}

std::string madeHistoryOf(const ProcessTestContext& context, const ImageViewControl* view)
{
    for (std::size_t index = 0u; index < context.madeViews.size(); ++index)
    {
        if (context.madeViews[index] == view)
        {
            return context.madeHistories[index];
        }
    }
    return std::string();
}

ImageViewControl* madeNeighbourOf(const ProcessTestContext& context, const ImageViewControl* view)
{
    for (std::size_t index = 0u; index < context.madeViews.size(); ++index)
    {
        if (context.madeViews[index] == view)
        {
            return context.madeNeighbours[index];
        }
    }
    return nullptr;
}

} // namespace an::tests::processes::channels
