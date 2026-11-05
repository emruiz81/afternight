// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The service interfaces Controls reaches the application through: the context lends the
/// one engine session; the view factory takes a document and either makes a view that owns it or leaves it with the
/// caller; a temporary sequence is asked for by the engine's container; the output intent the export settings decide
/// comes back as an engine result; and the process-wide context is installed, read and cleared.

#include "ControlsServicesTestSupport.h"

#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace
{

using an::tests::services::filledDocument;
using an::tests::services::InstalledContext;
using an::tests::services::monoFloatDescriptor;
using an::tests::services::openContext;
using an::tests::services::TestAppContext;
using an::ui::DocumentNames;
using an::ui::DocumentSequence;
using an::ui::IAppContext;
using an::ui::ImageDocument;

TEST(ServiceInterfacesTest, TheContextLendsTheOneSessionEveryDocumentIsMadeIn)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    IAppContext& app = *context;
    an::ui::IEngineSessionProvider* provider = app.getEngineSessionProvider();
    ASSERT_NE(provider, nullptr);
    const an::consumer::EngineSession& session = provider->engineSession();
    EXPECT_EQ(&session, &context->engineSession());

    const std::uint64_t assetsBefore = session.outstanding().assets;
    std::optional<ImageDocument> document = filledDocument(session, 1.0f);
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(session.outstanding().assets, assetsBefore + 1u)
        << "a document built through the context's session is not counted against it";
    document.reset();
    EXPECT_EQ(session.outstanding().assets, assetsBefore);
}

TEST(ServiceInterfacesTest, AViewMadeFromADocumentOwnsIt)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<ImageDocument> document =
        filledDocument(context->engineSession(), 2.0f, DocumentNames{.displayName = QStringLiteral("M42")});
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();

    IAppContext& app = *context;
    ImageViewControl* view = app.getViewFactory()->createNewImageViewControlFromImage(
        nullptr, std::move(*document), nullptr, "Opened", ViewControlMode::st_preview);

    ASSERT_NE(view, nullptr);
    EXPECT_FALSE(document->valid()) << "the caller still holds the document the view was made from";
    EXPECT_EQ(view->document().identity(), identity);
    EXPECT_EQ(view->document().names().displayName, QStringLiteral("M42"));
    ASSERT_EQ(context->requests.size(), 1u);
    EXPECT_EQ(context->requests.front().historyDescription, "Opened");
    EXPECT_EQ(view->getViewControlMode(), ViewControlMode::st_preview);
    EXPECT_FALSE(view->isSequence());
}

TEST(ServiceInterfacesTest, AViewThatIsNotMadeLeavesTheDocumentWithItsCaller)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    context->refuseViews = true;
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 3.0f);
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();

    ImageViewControl* view =
        context->getViewFactory()->createNewImageViewControlFromImage(nullptr, std::move(*document));

    EXPECT_EQ(view, nullptr);
    ASSERT_TRUE(document->valid()) << "a refused view took the document anyway";
    EXPECT_EQ(document->identity(), identity);
    EXPECT_TRUE(document->isOpen());
}

TEST(ServiceInterfacesTest, ASequenceViewIsMadeFromADocumentThatStatesItsSequence)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<ImageDocument> single = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(single.has_value());
    EXPECT_EQ(context->getViewFactory()->createNewImageViewControlFromSequence(nullptr, std::move(*single)), nullptr)
        << "a document that states no sequence made a sequence view";
    EXPECT_TRUE(single->valid());

    std::optional<ImageDocument> frame = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(frame.has_value());
    DocumentSequence sequence;
    sequence.sourcePath = QStringLiteral("capture.ser");
    sequence.descriptor.format = an::SequenceFormat::Ser;
    sequence.descriptor.frameCount = 12u;
    sequence.currentFrame = 3u;
    frame->setSequence(sequence);

    ImageViewControl* view = context->getViewFactory()->createNewImageViewControlFromSequence(
        nullptr, std::move(*frame), nullptr, true, "Sequence opened");
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->isSequence());
    EXPECT_TRUE(view->isSequenceAutodebayerEnabled());
    ASSERT_TRUE(view->document().sequence().has_value());
    EXPECT_EQ(view->document().sequence()->descriptor.format, an::SequenceFormat::Ser);
    EXPECT_EQ(view->document().sequence()->currentFrame, 3u);
}

TEST(ServiceInterfacesTest, ATemporarySequenceIsAskedForByTheEngineContainer)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    context->tempDirectory = QStringLiteral("scratch");
    an::ui::ITempFileService* temp = context->getTempFileService();

    const std::string ser = temp->createTempFilePathForNewSequence(an::SequenceFormat::Ser);
    const std::string list = temp->createTempFilePathForNewSequence(an::SequenceFormat::ImageList);

    ASSERT_EQ(context->sequenceFormatsAsked.size(), 2u);
    EXPECT_EQ(context->sequenceFormatsAsked[0], an::SequenceFormat::Ser);
    EXPECT_EQ(context->sequenceFormatsAsked[1], an::SequenceFormat::ImageList);
    EXPECT_TRUE(temp->isTempFile(ser));
    EXPECT_NE(ser, list);
}

TEST(ServiceInterfacesTest, TheOutputIntentTheSettingsDecideComesBackAsAnEngineResult)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    context->tiffCompression = an::OutputCompression::Deflate;
    const std::optional<an::ImageDescriptor> source = monoFloatDescriptor(16u, 8u);
    ASSERT_TRUE(source.has_value());
    const an::ui::IOutputIntentProvider* provider = context->getOutputIntentProvider();
    ASSERT_NE(provider, nullptr);

    an::Result<an::OutputIntent> tiff = provider->buildOutputIntent(*source, an::CodecFormat::Tiff);
    ASSERT_TRUE(tiff.hasValue()) << tiff.status().message();
    EXPECT_EQ(tiff->sampleType(), an::SampleType::Float32);
    EXPECT_EQ(tiff->compression(), an::OutputCompression::Deflate);

    an::Result<an::OutputIntent> raw = provider->buildOutputIntent(*source, an::CodecFormat::Raw);
    ASSERT_FALSE(raw.hasValue());
    EXPECT_EQ(raw.status().code(), an::ErrorCode::Unsupported);
}

TEST(ServiceInterfacesTest, TheProcessWideContextIsInstalledReadAndCleared)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(an::ui::appContext(), nullptr) << "a context was left installed by an earlier case";
    {
        const InstalledContext installed(*context);
        EXPECT_EQ(an::ui::appContext(), context.get());
    }
    EXPECT_EQ(an::ui::appContext(), nullptr);
}

TEST(ServiceInterfacesTest, TheTemporaryDirectoryComesFromTheContextOrTheSystem)
{
    EXPECT_EQ(an::ui::ensureTempDirPathOrSystemTemp(), QDir::tempPath());

    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    context->tempDirectory = root.filePath(QStringLiteral("created-on-demand"));
    const InstalledContext installed(*context);
    EXPECT_EQ(an::ui::ensureTempDirPathOrSystemTemp(), context->tempDirectory);
    EXPECT_TRUE(QDir(context->tempDirectory).exists());

    context->tempDirectory = QStringLiteral("   ");
    EXPECT_EQ(an::ui::ensureTempDirPathOrSystemTemp(), QDir::tempPath());
}

} // namespace
