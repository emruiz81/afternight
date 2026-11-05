// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The host manager's request and result paths over a view's document: a request's buffers exported from the target
/// view's document and its mask under read leases taken without waiting, a preview sub-area exported as a region, a
/// document being written answering Busy; an execute result imported on a worker and handed to the view on the GUI
/// thread, its source mappings let go, its output masks imported by name; a preview patch composited into the view's
/// image; a result landing after the session stopped dropped; and a commit with no destination or for another view
/// refused by name.

#include "Test_ControlsExtensionSeam/ExtensionSeamTestSupport.h"
#include "Test_ControlsViewer/EditHoldingTheWriter.h"

#include "app/ExtensionHostManager.h"
#include "app/SharedBufferManager.h"
#include "controls/ImageViewControl.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QRect>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{

using namespace an::tests::seam;
using an::tests::viewer::InstalledContext;
using an::tests::viewer::makeDocument;
using an::tests::viewer::makeView;
using an::tests::viewer::openViewerContext;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;
using an::tests::viewer::sampleOf;
using an::tests::viewer::settle;
using an::tests::viewer::ViewerTestContext;
using an::tests::viewer::ViewHandle;
using an::ui::extensions::ExecutionResultMessage;
using an::ui::extensions::ExtensionHostExecutionDispatchResult;
using an::ui::extensions::ExtensionHostExecutionDispatchState;
using an::ui::extensions::ExtensionHostSessionState;
using an::ui::extensions::ExtensionSessionController;
using an::ui::extensions::ManagedAuxiliaryImage;
using an::ui::extensions::ManagedNamedImageResult;
using an::ui::extensions::SharedBufferAccessMode;
using an::ui::extensions::SharedBufferDescriptor;
using an::ui::extensions::SharedBufferManager;
using an::ui::extensions::SharedBufferRole;
using an::ui::extensions::SharedImageBuffer;
using an::ui::extensions::SharedImageBufferSpec;
using an::ui::extensions::ViewCommitMessage;

using AuxiliaryProvider = std::function<std::vector<ManagedAuxiliaryImage>(ImageViewControl*, bool)>;
using SubAreaProvider = std::function<std::optional<QRect>(ImageViewControl*)>;

/// The sample the target view's image holds at a position and channel: distinct per pixel, below one.
[[nodiscard]] float targetSample(std::uint32_t x, std::uint32_t y, std::uint32_t channel) noexcept
{
    return static_cast<float>(seamSample(x, y, channel)) / 256.0f;
}

/// The provider that lends the target's current mask as the request's selection.
[[nodiscard]] AuxiliaryProvider maskProvider()
{
    return [](ImageViewControl* control, bool) {
        std::vector<ManagedAuxiliaryImage> auxiliary;
        if (ImageViewControl* const mask = control != nullptr ? control->getCurrentMask() : nullptr;
            mask != nullptr && mask->hasImage())
        {
            auxiliary.push_back(
                {SharedBufferRole::Mask, SharedBufferAccessMode::ReadOnly, std::string(), mask->document().asset()});
        }
        return auxiliary;
    };
}

[[nodiscard]] SubAreaProvider wholeImage()
{
    return [](ImageViewControl*) { return std::optional<QRect>(); };
}

[[nodiscard]] SubAreaProvider subArea(const QRect& rect)
{
    return [rect](ImageViewControl*) { return std::optional<QRect>(rect); };
}

class ExtensionHandoffTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext(sessionWidth());
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    /// The session's width.
    [[nodiscard]] virtual std::uint32_t sessionWidth() const
    {
        return 2u;
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    /// A target view showing a 4 by 3 RGB Float32 document whose samples are `targetSample`.
    [[nodiscard]] ViewHandle targetView(std::uint32_t width = 4u, std::uint32_t height = 3u)
    {
        ViewHandle view = makeView("target");
        std::optional<an::ui::ImageDocument> document = makeDocument<float>(
            session(), width, height, Channels::Rgb, targetSample,
            an::ui::DocumentNames{QStringLiteral("target"), QStringLiteral("C:/images/target.fit")});
        EXPECT_TRUE(document.has_value());
        if (document.has_value())
        {
            view->setDocument(std::move(*document));
            EXPECT_TRUE(settle(*view));
        }
        return view;
    }

    /// A controller bound to a view, with an active session of the given identity.
    [[nodiscard]] static std::unique_ptr<ExtensionSessionController> controllerFor(ImageViewControl* target,
                                                                                   const std::string& sessionId)
    {
        auto controller = std::make_unique<ExtensionSessionController>();
        controller->descriptor_id = "seam.extension";
        controller->display_name = "Seam Extension";
        controller->target_control = target;
        controller->running_session.prepared_session.session_id = sessionId;
        controller->running_session.state = ExtensionHostSessionState::Active;
        controller->running_session.runtime_ready = true;
        return controller;
    }

    /// Prepares an execute request and records its buffers as sent, as the coordinator does.
    void sendExecute(ExtensionSessionController& controller, std::vector<SharedBufferDescriptor>* descriptors)
    {
        QCborMap params;
        std::vector<SharedImageBuffer> owned;
        std::string error;
        const ExtensionSessionController::PrepareOutcome outcome =
            controller.prepareExecuteRequest(maskProvider(), wholeImage(), &params, descriptors, &owned, &error);
        ASSERT_EQ(outcome, ExtensionSessionController::PrepareOutcome::Ready) << error;
        ExtensionHostExecutionDispatchResult dispatch;
        dispatch.state = ExtensionHostExecutionDispatchState::Started;
        dispatch.request_id = "request-1";
        controller.applyExecuteDispatchResult(dispatch, std::move(owned));
        controller.running_session.in_flight_request_buffers = *descriptors;
    }

    /// A result buffer an extension would write: the source's shape, in a role, filled with `value`.
    [[nodiscard]] static std::optional<SharedBufferDescriptor>
    writeResult(const SharedBufferDescriptor& source, SharedBufferRole role, const std::string& name,
                const std::function<float(std::uint32_t, std::uint32_t, std::uint32_t)>& value)
    {
        SharedImageBufferSpec spec;
        spec.sessionId = source.mapping_name.substr(0, source.mapping_name.find('.'));
        spec.bufferId = name;
        spec.symbolicName = name;
        spec.width = source.width;
        spec.height = source.height;
        spec.channels = source.channels;
        spec.pixelType = source.pixel_type;
        spec.pixelFormat = source.pixel_format;
        spec.colorSpace = source.color_space;
        spec.accessMode = SharedBufferAccessMode::ReadWrite;
        spec.bufferRole = role;
        spec.offsetX = source.offset_x;
        spec.offsetY = source.offset_y;
        spec.sourceWidth = source.source_width;
        spec.sourceHeight = source.source_height;
        spec.metadataSidecar = source.metadata_sidecar;
        an::Result<SharedImageBuffer> buffer = SharedBufferManager().allocateBuffer(spec);
        EXPECT_TRUE(buffer.hasValue()) << buffer.status().message();
        if (!buffer.hasValue())
        {
            return std::nullopt;
        }
        fillBuffer<float>(*buffer, value);
        return buffer->descriptor();
    }

    /// Commits a destination buffer and the masks beside it to the controller's session, as the host would.
    static void commit(ExtensionSessionController& controller, const SharedBufferDescriptor& destination,
                       std::vector<SharedBufferDescriptor> masks = {}, const std::string& viewId = {})
    {
        ViewCommitMessage message;
        message.session_id = controller.sessionId();
        message.request_id = "commit-1";
        message.view_id = viewId;
        message.history_step_name = "Seam Extension";
        message.history_step_description = "What the extension did";
        message.result_buffers.push_back(destination);
        for (SharedBufferDescriptor& mask : masks)
        {
            message.result_buffers.push_back(std::move(mask));
        }
        controller.running_session.last_view_commit = std::move(message);
    }

    /// Runs an execute import to its landing on the GUI thread.
    [[nodiscard]] std::optional<ExtensionSessionController::LandedMaterialization>
    landExecute(ExtensionSessionController& controller, const bool fromWorker = false)
    {
        std::atomic<int> wakes{0};
        controller.beginExecuteMaterialization(session(), [&wakes]() { ++wakes; });
        EXPECT_TRUE(pumpUntil([&controller]() { return controller.hasLandedMaterialization(); }));
        EXPECT_FALSE(controller.materializationInFlight());
        if (fromWorker)
        {
            EXPECT_GE(wakes.load(), 1) << "a landing from the worker wakes the pump";
        }
        return controller.takeLandedMaterialization();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

/// The handoff over a session one operation wide, where work that holds the only thread while it waits for an image's
/// writer stops everything.
class ExtensionHandoffTestAtWidthOne : public ExtensionHandoffTest
{
protected:
    [[nodiscard]] std::uint32_t sessionWidth() const override
    {
        return 1u;
    }
};

} // namespace

TEST_F(ExtensionHandoffTest, ARequestExportsTheViewsImageAndItsMaskUnderReadLeases)
{
    ViewHandle target = targetView();
    ViewHandle mask = makeView("mask");
    std::optional<an::ui::ImageDocument> maskDocument =
        makeDocument<float>(session(), 4u, 3u, Channels::Grey,
                            [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x == 1u ? 1.0f : 0.0f; });
    ASSERT_TRUE(maskDocument.has_value());
    mask->setDocument(std::move(*maskDocument));
    ASSERT_TRUE(settle(*mask));
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(target->isMasked());
    ASSERT_TRUE(settle(*target));

    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-request");
    controller->process_capabilities.masks_input = true;

    QCborMap params;
    std::vector<SharedBufferDescriptor> descriptors;
    std::vector<SharedImageBuffer> owned;
    std::string error;
    ASSERT_EQ(controller->prepareExecuteRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    ASSERT_EQ(descriptors.size(), 2u);
    ASSERT_EQ(owned.size(), 2u);

    EXPECT_EQ(descriptors[0].buffer_role, SharedBufferRole::Source);
    EXPECT_EQ(descriptors[0].symbolic_name, "source");
    EXPECT_EQ(descriptors[0].access_mode, SharedBufferAccessMode::ReadWrite);
    EXPECT_EQ(descriptors[0].width, 4);
    EXPECT_EQ(descriptors[0].height, 3);
    EXPECT_EQ(descriptors[0].channels, 3);
    EXPECT_EQ(descriptors[0].pixel_format, an::ui::extensions::SharedBufferPixelFormat::RGB);
    EXPECT_FALSE(descriptors[0].offset_x.has_value());
    EXPECT_FLOAT_EQ(bufferSample<float>(owned[0], 2u, 1u, 1u), targetSample(2u, 1u, 1u));
    EXPECT_FALSE(descriptors[0].metadata_sidecar.empty()) << "the document's file name travels in the sidecar";
    EXPECT_EQ(readSidecar(descriptors[0].metadata_sidecar).value(QStringLiteral("FILENAME")).toString(),
              QStringLiteral("C:/images/target.fit"));

    EXPECT_EQ(descriptors[1].buffer_role, SharedBufferRole::Mask);
    EXPECT_EQ(descriptors[1].symbolic_name, "selection");
    EXPECT_EQ(descriptors[1].access_mode, SharedBufferAccessMode::ReadOnly);
    EXPECT_EQ(descriptors[1].channels, 1);
    EXPECT_FLOAT_EQ(bufferSample<float>(owned[1], 1u, 2u, 0u), 1.0f);
    EXPECT_FLOAT_EQ(bufferSample<float>(owned[1], 2u, 2u, 0u), 0.0f);

    // The leases were let go once the copies were made: the document can be written at once.
    an::Result<an::ui::ImageDocumentWriter> writer = target->document().tryWrite();
    EXPECT_TRUE(writer.hasValue()) << writer.status().message();
    target->removeCurrentMask();
}

TEST_F(ExtensionHandoffTest, APreviewSubAreaIsExportedAsARegionOfTheImage)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-region");

    QCborMap params;
    std::vector<SharedBufferDescriptor> descriptors;
    std::vector<SharedImageBuffer> owned;
    std::string error;
    ASSERT_EQ(controller->preparePreviewRequest(maskProvider(), subArea(QRect(1, 1, 2, 2)), &params, &descriptors,
                                                &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors[0].width, 2);
    EXPECT_EQ(descriptors[0].height, 2);
    ASSERT_TRUE(descriptors[0].offset_x.has_value() && descriptors[0].offset_y.has_value());
    EXPECT_EQ(*descriptors[0].offset_x, 1);
    EXPECT_EQ(*descriptors[0].offset_y, 1);
    ASSERT_TRUE(descriptors[0].source_width.has_value() && descriptors[0].source_height.has_value());
    EXPECT_EQ(*descriptors[0].source_width, 4);
    EXPECT_EQ(*descriptors[0].source_height, 3);
    EXPECT_FLOAT_EQ(bufferSample<float>(owned[0], 0u, 0u, 2u), targetSample(1u, 1u, 2u));
    EXPECT_FLOAT_EQ(bufferSample<float>(owned[0], 1u, 1u, 0u), targetSample(2u, 2u, 0u));

    // The whole image asked as a sub-area is no sub-area, and an execute never crops.
    descriptors.clear();
    owned.clear();
    ASSERT_EQ(controller->preparePreviewRequest(maskProvider(), subArea(QRect(0, 0, 4, 3)), &params, &descriptors,
                                                &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    EXPECT_FALSE(descriptors.at(0).offset_x.has_value());
    descriptors.clear();
    owned.clear();
    ASSERT_EQ(controller->prepareExecuteRequest(maskProvider(), subArea(QRect(1, 1, 2, 2)), &params, &descriptors,
                                                &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    EXPECT_EQ(descriptors.at(0).width, 4);
}

TEST_F(ExtensionHandoffTest, ARequestAnswersBusyWhileTheDocumentIsWrittenAndIsReadyAfter)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-busy");

    QCborMap params;
    std::vector<SharedBufferDescriptor> descriptors;
    std::vector<SharedImageBuffer> owned;
    std::string error;
    {
        an::Result<an::ui::ImageDocumentWriter> writer = target->document().tryWrite();
        ASSERT_TRUE(writer.hasValue()) << writer.status().message();
        EXPECT_EQ(
            controller->preparePreviewRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
            ExtensionSessionController::PrepareOutcome::Busy);
        EXPECT_NE(error.find("being written"), std::string::npos) << error;
        EXPECT_TRUE(descriptors.empty());
        EXPECT_TRUE(owned.empty());
        EXPECT_EQ(
            controller->prepareExecuteRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
            ExtensionSessionController::PrepareOutcome::Busy);
    }
    EXPECT_EQ(controller->preparePreviewRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    EXPECT_EQ(descriptors.size(), 1u);

    // A view with no image, or none at all, is a refusal rather than a wait.
    ViewHandle empty = makeView("empty");
    controller->target_control = empty.get();
    EXPECT_EQ(controller->prepareExecuteRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
              ExtensionSessionController::PrepareOutcome::Failed);
    controller->target_control = nullptr;
    EXPECT_EQ(controller->prepareExecuteRequest(maskProvider(), wholeImage(), &params, &descriptors, &owned, &error),
              ExtensionSessionController::PrepareOutcome::Failed);
}

TEST_F(ExtensionHandoffTest, AnExecuteResultIsImportedOnAWorkerAndHandedToTheViewOnTheGuiThread)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-execute");
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    ASSERT_EQ(descriptors.size(), 1u);
    const std::filesystem::path sourceFile = descriptors[0].mapped_file_path;

    const auto inverted = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return 1.0f - targetSample(x, y, channel);
    };
    const std::optional<SharedBufferDescriptor> destination =
        writeResult(descriptors[0], SharedBufferRole::Destination, "result", inverted);
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination);
    ASSERT_TRUE(controller->hasPendingExecuteMaterialization());

    std::optional<ExtensionSessionController::LandedMaterialization> landed = landExecute(*controller, true);
    ASSERT_TRUE(landed.has_value());
    EXPECT_EQ(landed->kind, ExtensionSessionController::MaterializationKind::Execute);
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    EXPECT_TRUE(landed->masks.empty());
    EXPECT_FALSE(controller->hasPendingExecuteMaterialization()) << "the commit was consumed when the import began";
    const std::uint64_t imported = landed->image->identity();
    EXPECT_NE(imported, target->document().asset()->identity()) << "the result is a new asset";
    EXPECT_FLOAT_EQ(*sampleOf<float>(*landed->image, 2u, 1u, 1u), inverted(2u, 1u, 1u));

    std::string presentedStep;
    std::uint64_t presentedIdentity = 0u;
    ImageViewControl* presentedTo = nullptr;
    controller->applyExecutePresentation(
        std::move(*landed->image), std::move(landed->masks),
        [&](ImageViewControl* control, an::consumer::ImageAsset&& asset, const std::string& step, const std::string&) {
            presentedTo = control;
            presentedStep = step;
            presentedIdentity = asset.identity();
            control->replaceImage(std::move(asset));
        },
        {}, {});
    EXPECT_EQ(presentedTo, target.get());
    EXPECT_EQ(presentedStep, "Seam Extension");
    EXPECT_EQ(presentedIdentity, imported);
    ASSERT_TRUE(settle(*target));
    EXPECT_EQ(target->document().asset()->identity(), imported) << "the view shows the imported asset";
    EXPECT_FLOAT_EQ(*sampleOf<float>(*target->document().asset(), 3u, 2u, 0u), inverted(3u, 2u, 0u));
    EXPECT_FALSE(controller->running_session.last_view_commit.has_value()) << "the commit is cleared once presented";
    EXPECT_TRUE(controller->owned_buffers.empty()) << "the request's mappings are let go with the result";
    EXPECT_TRUE(removable(sourceFile)) << "nothing maps the source buffer any more";
}

TEST_F(ExtensionHandoffTest, OutputMasksAreImportedByNameAndHandedToTheMaskPresenter)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-masks");
    controller->process_capabilities.masks_output = true;
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    ASSERT_EQ(descriptors.size(), 1u);

    const std::optional<SharedBufferDescriptor> destination = writeResult(
        descriptors[0], SharedBufferRole::Destination, "result",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return targetSample(x, y, channel); });
    std::optional<SharedBufferDescriptor> mask =
        writeResult(descriptors[0], SharedBufferRole::Mask, "output_mask",
                    [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 2u ? 1.0f : 0.0f; });
    ASSERT_TRUE(destination.has_value() && mask.has_value());
    commit(*controller, *destination, {*mask});

    std::optional<ExtensionSessionController::LandedMaterialization> landed = landExecute(*controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    ASSERT_EQ(landed->masks.size(), 1u);
    EXPECT_EQ(landed->masks[0].symbolic_name, "output_mask");
    EXPECT_FLOAT_EQ(*sampleOf<float>(landed->masks[0].image, 1u, 0u, 0u), 1.0f);
    EXPECT_FLOAT_EQ(*sampleOf<float>(landed->masks[0].image, 2u, 0u, 0u), 0.0f);

    std::vector<std::string> presentedMasks;
    ImageViewControl* masksFor = nullptr;
    controller->applyExecutePresentation(
        std::move(*landed->image), std::move(landed->masks),
        [](ImageViewControl*, an::consumer::ImageAsset&&, const std::string&, const std::string&) {},
        [&](ImageViewControl* control, std::vector<ManagedNamedImageResult>&& masks) {
            masksFor = control;
            for (const ManagedNamedImageResult& named : masks)
            {
                presentedMasks.push_back(named.symbolic_name);
            }
        },
        {});
    EXPECT_EQ(masksFor, target.get());
    ASSERT_EQ(presentedMasks.size(), 1u);
    EXPECT_EQ(presentedMasks[0], "output_mask");
}

TEST_F(ExtensionHandoffTest, APreviewPatchIsCompositedIntoTheViewsImage)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-preview");

    QCborMap params;
    std::vector<SharedBufferDescriptor> descriptors;
    std::vector<SharedImageBuffer> owned;
    std::string error;
    // An asymmetric area, so a composite that swaps or drops the offsets lands somewhere else.
    ASSERT_EQ(controller->preparePreviewRequest(maskProvider(), subArea(QRect(2, 0, 2, 2)), &params, &descriptors,
                                                &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    ExtensionHostExecutionDispatchResult dispatch;
    dispatch.state = ExtensionHostExecutionDispatchState::Started;
    dispatch.request_id = "preview-1";
    controller->applyPreviewDispatchResult(dispatch, std::move(owned));
    controller->running_session.in_flight_request_buffers = descriptors;

    const std::optional<SharedBufferDescriptor> patch =
        writeResult(descriptors[0], SharedBufferRole::Preview, "preview",
                    [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(patch.has_value());
    ExecutionResultMessage result;
    result.session_id = controller->sessionId();
    result.request_id = "result-1";
    result.target_request_id = "preview-1";
    result.result_kind = "preview";
    result.status = "succeeded";
    result.output_buffers.push_back(*patch);
    controller->running_session.last_preview_result = std::move(result);
    ASSERT_TRUE(controller->hasPendingPreviewMaterialization());
    ASSERT_TRUE(controller->shouldApplyPreviewResult());
    controller->markPreviewResultConsumed();

    controller->beginPreviewMaterialization(session(), {});
    ASSERT_TRUE(pumpUntil([&]() { return controller->hasLandedMaterialization(); }));
    std::optional<ExtensionSessionController::LandedMaterialization> landed = controller->takeLandedMaterialization();
    ASSERT_TRUE(landed.has_value());
    EXPECT_EQ(landed->kind, ExtensionSessionController::MaterializationKind::Preview);
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    const std::optional<an::ImageDescriptor> described = describedBy(*landed->image);
    ASSERT_TRUE(described.has_value());
    EXPECT_EQ(described->extent().width(), 4u) << "the patch is composited into the whole image";
    EXPECT_EQ(described->extent().height(), 3u);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 4u; ++x)
        {
            const bool inside = x >= 2u && y <= 1u;
            EXPECT_FLOAT_EQ(*sampleOf<float>(*landed->image, x, y, 1u), inside ? 1.0f : targetSample(x, y, 1u))
                << x << "," << y;
        }
    }

    ViewHandle preview = makeView("preview");
    ImageViewControl* previousPreview = reinterpret_cast<ImageViewControl*>(1);
    controller->applyPreviewPresentation(
        std::move(*landed->image),
        [&](ImageViewControl* control, ImageViewControl* previous, an::consumer::ImageAsset&& asset) {
            EXPECT_EQ(control, target.get());
            previousPreview = previous;
            // A preview view that shows nothing yet is given a document, as the application's view factory gives one.
            an::Result<an::ui::ImageDocument> document =
                an::ui::ImageDocument::create(std::move(asset), an::ui::DocumentNames{QStringLiteral("preview"), {}});
            EXPECT_TRUE(document.hasValue()) << document.status().message();
            if (document.hasValue())
            {
                preview->setDocument(std::move(*document));
            }
            return preview.get();
        });
    EXPECT_EQ(previousPreview, nullptr) << "the first preview has no view before it";
    EXPECT_EQ(controller->preview_control.data(), preview.get());
    ASSERT_TRUE(settle(*preview));
    EXPECT_FLOAT_EQ(*sampleOf<float>(*preview->document().asset(), 3u, 0u, 0u), 1.0f);
    EXPECT_FLOAT_EQ(*sampleOf<float>(*preview->document().asset(), 0u, 2u, 0u), targetSample(0u, 2u, 0u));
    EXPECT_TRUE(controller->owned_buffers.empty());
    controller->preview_control = nullptr;
}

// A preview patch is composited over the view's image read under a lease taken before the compositing asks to be
// admitted. An edit of the view holds its writer and then asks to be admitted, so at a width of one a compositing
// admitted first would hold the only thread while it waited for the writer, and the edit would wait for the thread.
TEST_F(ExtensionHandoffTestAtWidthOne, APreviewPatchWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-preview-held");

    QCborMap params;
    std::vector<SharedBufferDescriptor> descriptors;
    std::vector<SharedImageBuffer> owned;
    std::string error;
    ASSERT_EQ(controller->preparePreviewRequest(maskProvider(), subArea(QRect(2, 0, 2, 2)), &params, &descriptors,
                                                &owned, &error),
              ExtensionSessionController::PrepareOutcome::Ready)
        << error;
    ExtensionHostExecutionDispatchResult dispatch;
    dispatch.state = ExtensionHostExecutionDispatchState::Started;
    dispatch.request_id = "preview-1";
    controller->applyPreviewDispatchResult(dispatch, std::move(owned));
    controller->running_session.in_flight_request_buffers = descriptors;
    const std::optional<SharedBufferDescriptor> patch =
        writeResult(descriptors[0], SharedBufferRole::Preview, "preview",
                    [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(patch.has_value());
    ExecutionResultMessage result;
    result.session_id = controller->sessionId();
    result.request_id = "result-1";
    result.target_request_id = "preview-1";
    result.result_kind = "preview";
    result.status = "succeeded";
    result.output_buffers.push_back(*patch);
    controller->running_session.last_preview_result = std::move(result);
    ASSERT_TRUE(controller->hasPendingPreviewMaterialization());
    ASSERT_TRUE(controller->shouldApplyPreviewResult());
    controller->markPreviewResultConsumed();

    an::tests::viewer::EditHoldingTheWriter edit(*target);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    controller->beginPreviewMaterialization(session(), {});
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the compositing was admitted while it could not read the view's image";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the compositing held while the compositing waited for the edit's writer";

    ASSERT_TRUE(pumpUntil([&]() { return controller->hasLandedMaterialization(); }));
    std::optional<ExtensionSessionController::LandedMaterialization> landed = controller->takeLandedMaterialization();
    ASSERT_TRUE(landed.has_value());
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    EXPECT_FLOAT_EQ(*sampleOf<float>(*landed->image, 3u, 0u, 1u), 1.0f);
    EXPECT_FLOAT_EQ(*sampleOf<float>(*landed->image, 0u, 2u, 1u), targetSample(0u, 2u, 1u));
    ASSERT_TRUE(settle(*target));
}

TEST_F(ExtensionHandoffTest, AResultLandingAfterTheSessionStoppedIsDropped)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-stopped");
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    const std::optional<SharedBufferDescriptor> destination = writeResult(
        descriptors.at(0), SharedBufferRole::Destination, "result",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return targetSample(x, y, channel); });
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination);

    // Stopped while the import is on its way: its landing finds the session gone and is dropped.
    std::atomic<int> wakes{0};
    controller->beginExecuteMaterialization(session(), [&wakes]() { ++wakes; });
    controller->cancelMaterialization();
    pumpQuietly();
    QThreadPool::globalInstance()->waitForDone();
    pumpQuietly();
    EXPECT_FALSE(controller->hasLandedMaterialization());
    EXPECT_FALSE(controller->materializationInFlight());
    EXPECT_EQ(wakes.load(), 0) << "a dropped landing wakes nothing";

    // Stopped after it landed and before the pump took it: taken by nobody.
    commit(*controller, *destination);
    controller->last_consumed_execute_commit_request_id.clear();
    std::optional<ExtensionSessionController::LandedMaterialization> landed = landExecute(*controller);
    ASSERT_TRUE(landed.has_value());
    commit(*controller, *destination);
    controller->last_consumed_execute_commit_request_id.clear();
    controller->beginExecuteMaterialization(session(), {});
    ASSERT_TRUE(pumpUntil([&]() { return controller->hasLandedMaterialization(); }));
    controller->cancelMaterialization();
    EXPECT_FALSE(controller->hasLandedMaterialization());
    EXPECT_FALSE(controller->takeLandedMaterialization().has_value());
}

// An import the session stopped before its worker ran lands nothing when the worker does: every thread of the pool is
// held, so the import is queued behind them and the stop is certain to come first; once the threads are let go the
// import runs, finds its generation moved, and drops its result without waking the pump.
TEST_F(ExtensionHandoffTest, AnImportStoppedBeforeItsWorkerRunsLandsNothing)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-gated");
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    const std::optional<SharedBufferDescriptor> destination = writeResult(
        descriptors.at(0), SharedBufferRole::Destination, "result",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return targetSample(x, y, channel); });
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination);

    QThreadPool* const pool = QThreadPool::globalInstance();
    const int threads = pool->maxThreadCount();
    std::latch release(1);
    std::atomic<int> holding{0};
    for (int thread = 0; thread < threads; ++thread)
    {
        pool->start([&holding, &release]() {
            ++holding;
            release.wait();
        });
    }
    const bool held = pumpUntil([&holding, threads]() { return holding.load() == threads; });
    if (!held)
    {
        release.count_down();
    }
    ASSERT_TRUE(held) << "the pool's threads were not all held";

    std::atomic<int> wakes{0};
    controller->beginExecuteMaterialization(session(), [&wakes]() { ++wakes; });
    EXPECT_TRUE(controller->materializationInFlight()) << "the import is queued behind the held threads";
    controller->cancelMaterialization();
    EXPECT_FALSE(controller->materializationInFlight());

    release.count_down();
    pool->waitForDone();
    pumpQuietly();
    EXPECT_FALSE(controller->hasLandedMaterialization()) << "an import the session stopped landed anyway";
    EXPECT_FALSE(controller->takeLandedMaterialization().has_value());
    EXPECT_EQ(wakes.load(), 0) << "a dropped landing woke the pump";
}

TEST_F(ExtensionHandoffTest, AResultCommittedAsItsWindowClosedLandsBeforeTheSessionStops)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-closed-window");
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    const auto inverted = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return 1.0f - targetSample(x, y, channel);
    };
    const std::optional<SharedBufferDescriptor> destination =
        writeResult(descriptors.at(0), SharedBufferRole::Destination, "result", inverted);
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination);

    // A window that does not keep open closes right after its Apply: the commit and the close reach one pump.
    controller->running_session.native_window_close_requested = true;
    controller->running_session.state = ExtensionHostSessionState::Disconnected;
    ASSERT_TRUE(controller->closedByItsWindow());
    EXPECT_TRUE(controller->hasPendingExecuteMaterialization()) << "the committed result is still the session's";
    EXPECT_EQ(controller->advanceAfterMaterialization({}), ExtensionSessionController::PumpAction::None)
        << "stopping now removes the buffers the result is imported from";

    std::atomic<int> wakes{0};
    controller->beginExecuteMaterialization(session(), [&wakes]() { ++wakes; });
    EXPECT_FALSE(controller->hasPendingExecuteMaterialization());
    ASSERT_TRUE(pumpUntil([&controller]() { return controller->hasLandedMaterialization(); }));
    EXPECT_EQ(controller->advanceAfterMaterialization({}), ExtensionSessionController::PumpAction::None)
        << "a landed result not shown yet holds the stop";
    std::optional<ExtensionSessionController::LandedMaterialization> landed = controller->takeLandedMaterialization();
    ASSERT_TRUE(landed.has_value());
    ASSERT_TRUE(landed->image.hasValue()) << landed->image.status().message();
    const std::uint64_t imported = landed->image->identity();

    int presented = 0;
    controller->applyExecutePresentation(
        std::move(*landed->image), std::move(landed->masks),
        [&](ImageViewControl* control, an::consumer::ImageAsset&& asset, const std::string&, const std::string&) {
            ++presented;
            control->replaceImage(std::move(asset));
        },
        {}, {});
    EXPECT_EQ(presented, 1);
    ASSERT_TRUE(settle(*target));
    EXPECT_EQ(target->document().asset()->identity(), imported) << "the view shows the result its window applied";
    EXPECT_FLOAT_EQ(*sampleOf<float>(*target->document().asset(), 3u, 2u, 0u), inverted(3u, 2u, 0u));
    EXPECT_FALSE(controller->owesExecuteCommit());
    EXPECT_EQ(controller->advanceAfterMaterialization({}), ExtensionSessionController::PumpAction::StopSession)
        << "once the result is shown, the closed session stops";
}

TEST_F(ExtensionHandoffTest, ASessionLostWithoutItsWindowClosingStopsWithoutLandingItsCommit)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-lost");
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);
    const std::optional<SharedBufferDescriptor> destination = writeResult(
        descriptors.at(0), SharedBufferRole::Destination, "result",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return targetSample(x, y, channel); });
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination);

    // A channel that dropped is not a window that closed: nothing the session sent is trusted to land.
    controller->running_session.state = ExtensionHostSessionState::Disconnected;
    EXPECT_FALSE(controller->closedByItsWindow());
    EXPECT_FALSE(controller->hasPendingExecuteMaterialization());
    EXPECT_EQ(controller->advanceAfterMaterialization({}), ExtensionSessionController::PumpAction::StopSession);
}

TEST_F(ExtensionHandoffTest, ACommitWithoutADestinationOrForAnotherViewIsRefusedByName)
{
    ViewHandle target = targetView();
    std::unique_ptr<ExtensionSessionController> controller = controllerFor(target.get(), "seam-refused");
    controller->target_view_id = "target-view";
    std::vector<SharedBufferDescriptor> descriptors;
    sendExecute(*controller, &descriptors);

    const std::optional<SharedBufferDescriptor> mask =
        writeResult(descriptors.at(0), SharedBufferRole::Mask, "only_a_mask",
                    [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(mask.has_value());
    commit(*controller, *mask);
    std::optional<ExtensionSessionController::LandedMaterialization> landed = landExecute(*controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_FALSE(landed->image.hasValue());
    EXPECT_NE(std::string(landed->image.status().message()).find("destination"), std::string::npos)
        << landed->image.status().message();

    const std::optional<SharedBufferDescriptor> destination = writeResult(
        descriptors.at(0), SharedBufferRole::Destination, "result",
        [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) { return targetSample(x, y, channel); });
    ASSERT_TRUE(destination.has_value());
    commit(*controller, *destination, {}, "another-view");
    landed = landExecute(*controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_FALSE(landed->image.hasValue());
    EXPECT_NE(std::string(landed->image.status().message()).find("unexpected host view"), std::string::npos)
        << landed->image.status().message();

    // A destination of another shape than the source is refused before anything is read.
    SharedBufferDescriptor resized = *destination;
    resized.width = 2;
    resized.row_stride = 2u * sizeof(float);
    resized.plane_stride = resized.row_stride * static_cast<std::uint64_t>(resized.height);
    resized.byte_size = resized.plane_stride * static_cast<std::uint64_t>(resized.channels);
    commit(*controller, resized, {}, "target-view");
    landed = landExecute(*controller);
    ASSERT_TRUE(landed.has_value());
    ASSERT_FALSE(landed->image.hasValue());
    EXPECT_NE(std::string(landed->image.status().message()).find("width"), std::string::npos)
        << landed->image.status().message();
}
