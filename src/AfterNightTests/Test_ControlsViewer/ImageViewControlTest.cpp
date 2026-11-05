// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The image view over the session bridge: a document shown and owned, a file read and shown on a worker, a read or a
/// presentation superseded while work is on its way, edits recorded in the view's history before they are shown, run in
/// order and undone in order, an edit the history cannot keep never shown, the processing lock, the demosaic setting a
/// standard open honours, a sequence document the view owns, a mask held to the shape the overlay can show, the
/// multiply showing what the view shows, what a presentation costs and how far it was reduced, the analysis a painted
/// presentation was made through, the painted buffer's lifetime, the write, the static preview's calibration, and what
/// a view presents of a camera frame: its display proxy zoomed out, the part it shows at full zoom, the mask with it,
/// one request per update and one measurement of the whole image per revision, and the image painted to its edges at
/// every zoom and scaling; and a preview frame's part of the document painted as a layer over the presentation it was
/// made for.

#include "Test_ControlsViewer/EditHoldingTheWriter.h"
#include "Test_ControlsViewer/ViewerTestContext.h"

#include "controls/ImageViewThemeColors.h"
#include "session/CommitTicket.h"
#include "session/ImageReads.h"
#include "theme/AnDesignTokens.h"
#include "widgets/PresentationBridge.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/Provenance.h>

#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace an::tests::viewer;

/// A grey Float32 image whose sample at (x, y) is (x + 10 y) / 100.
[[nodiscard]] std::optional<an::ui::ImageDocument> numberedDocument(const an::consumer::EngineSession& session,
                                                                    std::uint32_t width, std::uint32_t height)
{
    return makeDocument<float>(
        session, width, height, Channels::Grey,
        [](std::uint32_t x, std::uint32_t y, std::uint32_t) { return static_cast<float>(x + 10u * y) / 100.0f; });
}

/// A grey Float32 image of one value.
[[nodiscard]] std::optional<an::ui::ImageDocument> flatDocument(const an::consumer::EngineSession& session, float value,
                                                                std::uint32_t width = 8u, std::uint32_t height = 8u)
{
    return makeDocument<float>(session, width, height, Channels::Grey,
                               [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
}

/// The detail of the last entry of an asset's lineage.
[[nodiscard]] std::string lastLineageDetail(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    if (!lease.hasValue())
    {
        return {};
    }
    std::string detail;
    lease->visit([&](const auto& typed) {
        const auto entries = typed.metadata().provenance().entries();
        if (!entries.empty())
        {
            detail = entries.back().detail();
        }
    });
    return detail;
}

/// Whether a message was kept.
[[nodiscard]] bool said(const ViewerTestContext& context, const QString& message)
{
    return context.messages.contains(message);
}

class ImageViewControlTest : public ::testing::Test
{
protected:
    /// Opens a context of a width and installs it for the case.
    void open(std::uint32_t width = 2u, an::consumer::MemoryAllowances memory = {})
    {
        m_context = openViewerContext(width, memory);
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
    }

    [[nodiscard]] const an::consumer::EngineSession& session() const
    {
        return m_context->engineSession();
    }

    void TearDown() override
    {
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    QTemporaryDir m_directory;
};

TEST_F(ImageViewControlTest, AViewShowsTheDocumentItIsHandedAndClosesTheOneBefore)
{
    open();
    ViewHandle view = makeView("shown");
    std::optional<an::ui::ImageDocument> first = flatDocument(session(), 0.25f, 16u, 8u);
    ASSERT_TRUE(first.has_value());
    const std::uint64_t firstIdentity = first->identity();

    view->setDocument(std::move(*first));
    ASSERT_TRUE(settle(*view));
    EXPECT_TRUE(view->hasImage());
    EXPECT_EQ(view->document().identity(), firstIdentity);
    ASSERT_NE(view->getViewWidget()->presentedImage(), nullptr);
    EXPECT_EQ(view->getViewWidget()->imageSize(), QSize(16, 8));

    std::optional<an::ui::ImageDocument> second = flatDocument(session(), 0.5f);
    ASSERT_TRUE(second.has_value());
    an::Result<an::consumer::AssetReadLease> stillReading = view->document().read();
    ASSERT_TRUE(stillReading.hasValue());
    an::ui::RequestTracker tracker;
    const an::Result<an::ui::CommitTicket> againstFirst = tracker.issue(view->document());
    ASSERT_TRUE(againstFirst.hasValue()) << againstFirst.status().message();
    ASSERT_EQ(againstFirst->verdict(), an::ui::CommitVerdict::Commit);
    view->setDocument(std::move(*second));
    EXPECT_EQ(againstFirst->verdict(), an::ui::CommitVerdict::DocumentClosed)
        << "the document the view let go is closed at once, so nothing captured against it publishes";
    ASSERT_TRUE(settle(*view));
    EXPECT_NE(view->document().identity(), firstIdentity);
    EXPECT_EQ(view->getViewWidget()->imageSize(), QSize(8, 8));
    EXPECT_TRUE(stillReading->valid()) << "work already reading the document it let go finishes on its own";
}

TEST_F(ImageViewControlTest, AFileIsReadOnAWorkerAndShownUnderItsName)
{
    open();
    const QString path = m_directory.filePath(QStringLiteral("frame.fits"));
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session(), 6u, 4u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * x; });
        ASSERT_TRUE(asset.has_value());
        writeFitsFile(session(), *asset, path);
    }
    ViewHandle view = makeView("read");
    view->loadImageFromFile(path.toStdString());
    EXPECT_FALSE(view->hasImage()) << "the read lands later, from a worker";
    ASSERT_TRUE(settle(*view));

    ASSERT_TRUE(view->hasImage());
    EXPECT_EQ(view->document().names().sourcePath, path);
    EXPECT_EQ(view->document().names().displayName, QStringLiteral("frame"));
    EXPECT_EQ(view->getFileName(), path.toStdString());
    const std::optional<float> sample = sampleOf<float>(*view->document().asset(), 3u, 1u);
    ASSERT_TRUE(sample.has_value());
    EXPECT_NEAR(*sample, 0.3f, 1e-6f);
    EXPECT_EQ(view->workTelemetry().readsCommitted, 1u);
}

TEST_F(ImageViewControlTest, AReadThatFailsIsReportedAndShowsNothing)
{
    open();
    ViewHandle view = makeView("missing");
    view->loadImageFromFile(m_directory.filePath(QStringLiteral("absent.fits")).toStdString());
    ASSERT_TRUE(settle(*view));
    EXPECT_FALSE(view->hasImage());
    EXPECT_EQ(view->workTelemetry().readsFailed, 1u);
}

// A stale completion: the earlier read is still waiting when the later one is asked for, and it never shows.
TEST_F(ImageViewControlTest, ALaterReadSupersedesOneStillOnItsWay)
{
    open(1u);
    const QString first = m_directory.filePath(QStringLiteral("first.fits"));
    const QString second = m_directory.filePath(QStringLiteral("second.fits"));
    for (const auto& [path, value] : {std::pair{first, 0.25f}, std::pair{second, 0.75f}})
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session(), 4u, 4u, Channels::Grey, [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
        ASSERT_TRUE(asset.has_value());
        writeFitsFile(session(), *asset, path);
    }
    ViewHandle view = makeView("superseded");
    {
        // The session's one thread is held, so both reads wait for it.
        std::optional<an::consumer::OperationScope> gate = admitted(session());
        ASSERT_TRUE(gate.has_value());
        view->loadImageFromFile(first.toStdString());
        view->loadImageFromFile(second.toStdString());
        pumpQuietly();
        EXPECT_FALSE(view->hasImage());
    }
    ASSERT_TRUE(settle(*view));

    EXPECT_EQ(view->document().names().sourcePath, second);
    const std::optional<float> sample = sampleOf<float>(*view->document().asset(), 1u, 1u);
    ASSERT_TRUE(sample.has_value());
    EXPECT_FLOAT_EQ(*sample, 0.75f);
    const ImageViewWorkTelemetry telemetry = view->workTelemetry();
    EXPECT_EQ(telemetry.readsRequested, 2u);
    EXPECT_EQ(telemetry.readsCommitted, 1u);
    EXPECT_EQ(telemetry.readsDiscarded, 1u);
}

// A stale presentation: the earlier one waits for the session, a later one supersedes it, and only the later paints.
TEST_F(ImageViewControlTest, ALaterPresentationSupersedesOneStillOnItsWay)
{
    open(1u);
    ViewHandle view = makeView("presentations");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 8u, 4u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const ImageViewPresentationTelemetry before = view->getViewWidget()->presentationTelemetry();
    {
        std::optional<an::consumer::OperationScope> gate = admitted(session());
        ASSERT_TRUE(gate.has_value());
        view->getViewWidget()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
        AutostretchParams params;
        params.targetBackground = 0.5;
        view->getViewWidget()->setAutostretchSettings(params);
        pumpQuietly();
    }
    ASSERT_TRUE(settle(*view));

    const ImageViewPresentationTelemetry after = view->getViewWidget()->presentationTelemetry();
    EXPECT_EQ(after.committed - before.committed, 1u) << "only the request still current paints";
    EXPECT_GE(after.discarded - before.discarded, 1u) << "the superseded presentation is discarded unpainted";
    EXPECT_DOUBLE_EQ(view->getViewWidget()->getAutostretchSettings().targetBackground, 0.5);
}

// A presentation takes its read lease before it asks to be admitted. The view's own edit holds its writer and then asks
// to be admitted, and nothing stops a presentation while an edit runs, so at a width of one a presentation admitted
// first would hold the only thread while it waited for the writer, and the edit would wait for the thread.
TEST_F(ImageViewControlTest, APresentationWaitingForAnEditsWriterHoldsNoThreadTheEditNeeds)
{
    open(1u);
    ViewHandle view = makeView("presented while edited");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 8u, 4u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const ImageViewPresentationTelemetry before = view->getViewWidget()->presentationTelemetry();

    an::tests::viewer::EditHoldingTheWriter edit(*view);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    view->getViewWidget()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the presentation was admitted while it could not read the image";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the edit waited for the thread the presentation held while the presentation waited for the edit's writer";

    ASSERT_TRUE(settle(*view));
    EXPECT_GE(view->getViewWidget()->presentationTelemetry().committed - before.committed, 1u);
}

// A view's mask layer reads the mask's view under a lease taken before it asks to be admitted: an edit of the mask's
// view holds that writer and then asks to be admitted.
TEST_F(ImageViewControlTest, AMaskLayerWaitingForAnEditOfTheMasksViewHoldsNoThreadTheEditNeeds)
{
    open(1u);
    ViewHandle target = makeView("masked");
    ViewHandle mask = makeView("its mask");
    std::optional<an::ui::ImageDocument> image = numberedDocument(session(), 16u, 16u);
    std::optional<an::ui::ImageDocument> maskDocument =
        makeDocument<float>(session(), 16u, 16u, Channels::Grey,
                            [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 8u ? 0.0f : 1.0f; });
    ASSERT_TRUE(image.has_value() && maskDocument.has_value());
    target->setDocument(std::move(*image));
    mask->setDocument(std::move(*maskDocument));
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(settle(*target));
    ASSERT_NE(target->getViewWidget()->maskLayer(), nullptr);

    an::tests::viewer::EditHoldingTheWriter edit(*mask);
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.holdsTheWriter(); }));
    const std::uint64_t admittedBefore = session().admission().admitted;
    target->setInvertMask(true);
    EXPECT_FALSE(an::tests::viewer::admittedBeyond(session(), admittedBefore))
        << "the mask layer was admitted while it could not read the mask";
    edit.askToBeAdmitted();
    ASSERT_TRUE(pumpUntil([&edit]() { return edit.finished(); }));
    EXPECT_TRUE(edit.wasAdmitted())
        << "the mask's edit waited for the thread the layer held while the layer waited for the mask's writer";

    ASSERT_TRUE(settle(*target));
    ASSERT_TRUE(settle(*mask));
    EXPECT_NE(target->getViewWidget()->maskLayer(), nullptr);
    target->removeCurrentMask();
}

// Two edits that each read the image the other changes take what they share in one order of identity, so neither holds
// what the other waits for, at any width. The edit of the first image holds its writer and waits for its read of the
// second behind a third writer; the edit of the second, asked meanwhile, waits for its read of the first before it
// takes its own writer, so once the third writer lets go the first edit reads, lands and lets the second run.
TEST_F(ImageViewControlTest, TwoEditsThatEachReadTheImageTheOtherChangesBothRun)
{
    open(4u);
    ViewHandle one = makeView("one");
    ViewHandle other = makeView("other");
    std::optional<an::ui::ImageDocument> oneDocument = flatDocument(session(), 0.25f);
    std::optional<an::ui::ImageDocument> otherDocument = flatDocument(session(), 0.5f);
    ASSERT_TRUE(oneDocument.has_value() && otherDocument.has_value());
    one->setDocument(std::move(*oneDocument));
    other->setDocument(std::move(*otherDocument));
    ASSERT_TRUE(settle(*one));
    ASSERT_TRUE(settle(*other));
    const bool oneIsLower = one->document().asset()->identity() < other->document().asset()->identity();
    ImageViewControl& lower = oneIsLower ? *one : *other;
    ImageViewControl& upper = oneIsLower ? *other : *one;
    const std::shared_ptr<const an::consumer::ImageAsset> lowerImage = lower.document().asset();
    const std::shared_ptr<const an::consumer::ImageAsset> upperImage = upper.document().asset();

    struct Edits
    {
        std::atomic<bool> lowerRead{false};
        std::atomic<bool> upperRead{false};
        std::atomic<int> finished{0};
    };
    auto edits = std::make_shared<Edits>();
    // Each edit reads the other image through the lease its run took, and changes nothing.
    const auto readingEdit = [edits](std::shared_ptr<an::ui::ImageReads> reads, std::uint64_t otherIdentity,
                                     std::atomic<bool> Edits::* read) {
        return [edits, reads, otherIdentity, read](const an::consumer::EngineSession&,
                                                   const an::consumer::AssetWriteLease&,
                                                   std::stop_token) -> an::Result<an::consumer::ImageAsset> {
            (*edits).*read = reads->find(otherIdentity) != nullptr;
            return an::Status::failure(an::ErrorCode::Cancelled, "the case's edit changes nothing");
        };
    };

    std::optional<an::Result<an::ui::ImageDocumentWriter>> third(upper.document().write());
    ASSERT_TRUE(third->hasValue());

    auto lowerReads = std::make_shared<an::ui::ImageReads>();
    lowerReads->name(upperImage);
    lower.editImage(
        "Read the upper image", "", readingEdit(lowerReads, upperImage->identity(), &Edits::lowerRead),
        [edits](bool) { ++edits->finished; }, lowerReads);
    ASSERT_TRUE(pumpUntil([&lowerImage]() { return !lowerImage->tryRead().hasValue(); }))
        << "the lower image's edit never took its writer";

    auto upperReads = std::make_shared<an::ui::ImageReads>();
    upperReads->name(lowerImage);
    upper.editImage(
        "Read the lower image", "", readingEdit(upperReads, lowerImage->identity(), &Edits::upperRead),
        [edits](bool) { ++edits->finished; }, upperReads);
    pumpQuietly();

    third.reset();
    ASSERT_TRUE(pumpUntil([&edits]() { return edits->finished == 2; }))
        << "each edit holds its own writer and waits for its read of the other's image";
    EXPECT_TRUE(edits->lowerRead);
    EXPECT_TRUE(edits->upperRead);
    ASSERT_TRUE(settle(lower));
    ASSERT_TRUE(settle(upper));
}

// The view's history holds an edit's step when the view first shows the edit, and keeps both states: undo shows the
// one the edit replaced and redo the edit again.
TEST_F(ImageViewControlTest, AnEditIsRecordedBeforeTheViewShowsIt)
{
    open();
    ViewHandle view = makeView("mirror");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(document.has_value());
    const std::uint64_t documentIdentity = document->identity();
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));
    const std::uint64_t before = view->document().asset()->identity();
    view->resetPendingChanges();
    an::ui::IViewHistory* history = view->viewHistory();
    ASSERT_NE(history, nullptr);
    EXPECT_FALSE(history->hasUndo());

    StepShownWith shown(view.get());
    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));

    ASSERT_FALSE(shown.steps.empty());
    EXPECT_EQ(shown.steps.front(), "Image Horizontal Mirror") << "the view showed the edit before its history held it";
    EXPECT_EQ(history->previousStepName(), "Image Horizontal Mirror");
    EXPECT_NE(view->document().asset()->identity(), before);
    EXPECT_EQ(view->document().identity(), documentIdentity)
        << "an edit changes the document's image, not the document";
    const std::optional<float> mirrored = sampleOf<float>(*view->document().asset(), 0u, 0u);
    ASSERT_TRUE(mirrored.has_value());
    EXPECT_FLOAT_EQ(*mirrored, 0.02f);
    EXPECT_TRUE(view->hasPendingChanges());
    EXPECT_EQ(view->workTelemetry().editsCommitted, 1u);

    ASSERT_TRUE(undoTo(*view, std::string(), "Image Horizontal Mirror"));
    EXPECT_EQ(sampleOf<float>(*view->document().asset(), 0u, 0u), 0.0f) << "undo shows the state the edit replaced";
    ASSERT_TRUE(history->redo());
    ASSERT_TRUE(pumpUntil([history]() { return history->previousStepName() == "Image Horizontal Mirror"; }));
    EXPECT_EQ(sampleOf<float>(*view->document().asset(), 0u, 0u), 0.02f) << "redo shows the edit again";
}

// A turn the view makes of its own image is said under its name when it fails - here because the session refuses to
// admit it - as a process says a failure of its own run under its title; the image is left as it was.
TEST_F(ImageViewControlTest, ATurnThatFailsIsSaidUnderItsName)
{
    open();
    ViewHandle view = makeView("refused turn");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const std::uint64_t before = view->document().asset()->identity();

    m_context->closeAdmission();
    view->rotate90DegreeRight();
    ASSERT_TRUE(pumpUntil([this]() { return !m_context->errorTexts.isEmpty(); })) << "the view says why";
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("error:Image Rotation")))
        << m_context->messages.join(QStringLiteral(", ")).toStdString();
    EXPECT_EQ(view->document().asset()->identity(), before);
    EXPECT_EQ(view->workTelemetry().editsDiscarded, 1u);
}

// A session whose pending-snapshot allowance the edited image outgrows refuses the copy the history keeps of it, and
// the edit is reported and never shown.
TEST_F(ImageViewControlTest, AnEditTheHistoryRefusesIsNeverShown)
{
    open(2u, historyCannotKeepEdits());
    ViewHandle view = makeView("refused");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));
    const std::uint64_t before = view->document().asset()->identity();

    view->rotate180Degree();
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(pumpUntil([this]() { return said(*m_context, QStringLiteral("error:Image History")); }))
        << "the history reports the step it could not keep";
    ASSERT_FALSE(m_context->errorTexts.isEmpty());
    EXPECT_TRUE(m_context->errorTexts.back().contains(QStringLiteral("its snapshot cannot be kept")))
        << m_context->errorTexts.back().toStdString();

    EXPECT_EQ(view->document().asset()->identity(), before);
    const std::optional<float> unchanged = sampleOf<float>(*view->document().asset(), 0u, 0u);
    ASSERT_TRUE(unchanged.has_value());
    EXPECT_FLOAT_EQ(*unchanged, 0.0f);
    EXPECT_EQ(view->workTelemetry().editsDiscarded, 1u);
    EXPECT_EQ(view->workTelemetry().editsCommitted, 0u);
    EXPECT_FALSE(view->viewHistory()->hasUndo());
}

TEST_F(ImageViewControlTest, EditsRunOneAtATimeInTheOrderAsked)
{
    open();
    ViewHandle view = makeView("ordered");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));

    // A quarter turn right and then a horizontal mirror is the transpose; the other order is not.
    view->rotate90DegreeRight();
    view->horizontalMirror();
    ASSERT_TRUE(settle(*view));

    EXPECT_EQ(view->viewHistory()->previousStepName(), "Image Horizontal Mirror");
    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_EQ(view->imageDescriptor()->extent().width(), 2u);
    EXPECT_EQ(view->imageDescriptor()->extent().height(), 3u);
    for (std::uint32_t y = 0u; y < 3u; ++y)
    {
        for (std::uint32_t x = 0u; x < 2u; ++x)
        {
            const std::optional<float> sample = sampleOf<float>(*view->document().asset(), x, y);
            ASSERT_TRUE(sample.has_value());
            EXPECT_FLOAT_EQ(*sample, static_cast<float>(y + 10u * x) / 100.0f) << x << "," << y;
        }
    }

    // Undo goes back through the steps in the order they were recorded.
    ASSERT_TRUE(undoTo(*view, "Image Rotation", "Image Horizontal Mirror"));
    ASSERT_TRUE(undoTo(*view, std::string(), "Image Rotation"));
    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_EQ(view->imageDescriptor()->extent().width(), 3u) << "two undos leave the image as it was read";
}

// The first edit starts at once and the rest wait, so it takes three to tell a queue from a stack.
TEST_F(ImageViewControlTest, EditsQueuedBehindARunningEditRunInTheOrderAsked)
{
    open();
    ViewHandle view = makeView("queued");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->initializeNewImageHistory("Image read from a file");
    ASSERT_TRUE(settle(*view));

    // A quarter turn right, a horizontal mirror and a quarter turn left is a vertical mirror.
    view->rotate90DegreeRight();
    view->horizontalMirror();
    view->rotate90DegreeLeft();
    ASSERT_TRUE(settle(*view));

    EXPECT_EQ(view->viewHistory()->previousStepName(), "Image Rotation");
    ASSERT_TRUE(view->imageDescriptor().has_value());
    EXPECT_EQ(view->imageDescriptor()->extent().width(), 3u);
    EXPECT_EQ(view->imageDescriptor()->extent().height(), 2u);
    for (std::uint32_t y = 0u; y < 2u; ++y)
    {
        for (std::uint32_t x = 0u; x < 3u; ++x)
        {
            const std::optional<float> sample = sampleOf<float>(*view->document().asset(), x, y);
            ASSERT_TRUE(sample.has_value());
            EXPECT_FLOAT_EQ(*sample, static_cast<float>(x + 10u * (1u - y)) / 100.0f) << x << "," << y;
        }
    }
    EXPECT_EQ(view->workTelemetry().editsCommitted, 3u);

    // Undo goes back through the three steps in the order they were recorded.
    ASSERT_TRUE(undoTo(*view, "Image Horizontal Mirror", "Image Rotation"));
    ASSERT_TRUE(undoTo(*view, "Image Rotation", "Image Horizontal Mirror"));
    ASSERT_TRUE(undoTo(*view, std::string(), "Image Rotation"));
}

TEST_F(ImageViewControlTest, AnEditOfADocumentTheViewLetGoIsDiscarded)
{
    open();
    ViewHandle view = makeView("replaced");
    std::optional<an::ui::ImageDocument> first = numberedDocument(session(), 3u, 2u);
    std::optional<an::ui::ImageDocument> second = flatDocument(session(), 0.5f);
    ASSERT_TRUE(first.has_value() && second.has_value());
    view->setDocument(std::move(*first));
    ASSERT_TRUE(settle(*view));
    const std::uint64_t secondAsset = second->asset()->identity();
    {
        // The edit waits for the document's writer while the case reads it, and keeps the document it edits alive.
        an::Result<an::consumer::AssetReadLease> reading = view->document().read();
        ASSERT_TRUE(reading.hasValue());
        an::ui::RequestTracker tracker;
        const an::Result<an::ui::CommitTicket> againstFirst = tracker.issue(view->document());
        ASSERT_TRUE(againstFirst.hasValue()) << againstFirst.status().message();
        view->verticalMirror();
        pumpQuietly();
        view->setDocument(std::move(*second));
        EXPECT_EQ(againstFirst->verdict(), an::ui::CommitVerdict::DocumentClosed)
            << "a document the view lets go is closed while work it started still holds it";
    }
    ASSERT_TRUE(settle(*view));

    EXPECT_EQ(view->document().asset()->identity(), secondAsset);
    ASSERT_NE(view->viewHistory(), nullptr);
    EXPECT_FALSE(view->viewHistory()->hasUndo()) << "the history recorded an edit the view discarded";
    EXPECT_EQ(view->workTelemetry().editsCommitted, 0u);
}

// The lock was a check and then a store, so two callers could both take it.
TEST_F(ImageViewControlTest, TheProcessingLockAdmitsExactlyOneOfConcurrentCallers)
{
    open();
    ViewHandle view = makeView("locked");
    constexpr int kCallers = 8;
    for (int round = 0; round < 300; ++round)
    {
        std::atomic<int> granted{0};
        std::latch start(kCallers);
        std::vector<std::thread> callers;
        for (int caller = 0; caller < kCallers; ++caller)
        {
            callers.emplace_back([&]() {
                start.arrive_and_wait();
                if (view->tryRequestLockForProcessing())
                {
                    granted.fetch_add(1);
                }
            });
        }
        for (std::thread& caller : callers)
        {
            caller.join();
        }
        ASSERT_EQ(granted.load(), 1) << "round " << round;
        EXPECT_TRUE(view->isLockedForProcessing());
        view->releaseLockForProcessing();
        EXPECT_FALSE(view->isLockedForProcessing());
    }
}

// An asynchronous update asked for on the GUI thread waited for an event only the GUI thread could run.
TEST_F(ImageViewControlTest, AnAsynchronousUpdateNeverWaitsOnTheThreadThatAsksForIt)
{
    open();
    ViewHandle view = makeView("async");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 8u, 8u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const std::uint64_t before = view->getViewWidget()->presentationTelemetry().requested;

    const auto started = std::chrono::steady_clock::now();
    view->getViewWidget()->imageUpdatedAsync(true, true);
    view->notifyImageUpdatedAsync(true);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(1))
        << "an asynchronous update returns before its event runs";
    std::thread worker([&view]() { view->getViewWidget()->imageUpdatedAsync(true, true); });
    worker.join();
    ASSERT_TRUE(settle(*view));
    EXPECT_GE(view->getViewWidget()->presentationTelemetry().requested, before + 3u);
}

// A standard open demosaiced with RCD whatever the setting named.
TEST_F(ImageViewControlTest, AStandardOpenDemosaicsAMosaicByTheMethodTheSettingNames)
{
    open();
    const QString path = m_directory.filePath(QStringLiteral("mosaic.fits"));
    {
        const std::optional<an::CfaDescriptor> rggb = an::CfaDescriptor::create(an::CfaPattern::Rggb, 0u, 0u);
        ASSERT_TRUE(rggb.has_value());
        std::optional<an::consumer::ImageAsset> mosaic = makeAsset<std::uint16_t>(
            session(), 8u, 8u, Channels::Grey,
            [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                return static_cast<std::uint16_t>(1000u + 100u * x + 10u * y);
            },
            an::NumericDomain::NormalizedLinear, *rggb);
        ASSERT_TRUE(mosaic.has_value());
        writeFitsFile(session(), *mosaic, path);
    }
    struct Case
    {
        const char* setting;
        bool answer;
        std::optional<an::DemosaicMethod> expected;
    };
    for (const Case& item :
         {Case{"bilinear", true, an::DemosaicMethod::Fast}, Case{"vng4", true, an::DemosaicMethod::Vng4},
          Case{"dcb", true, an::DemosaicMethod::Rcd}, Case{"bilinear", false, std::nullopt}})
    {
        (void)m_context->upsert(QStringLiteral("raw/demosaicAlgorithm"), QString::fromLatin1(item.setting));
        m_context->answerYes = item.answer;
        ViewHandle view = makeView("mosaic");
        view->loadImageFromFile(path.toStdString());
        ASSERT_TRUE(settle(*view)) << item.setting;
        ASSERT_TRUE(view->imageDescriptor().has_value()) << item.setting;
        EXPECT_TRUE(said(*m_context, QStringLiteral("question:Debayer Image")));
        if (item.expected.has_value())
        {
            EXPECT_EQ(view->imageDescriptor()->extent().channelCount(), 3u) << item.setting;
            EXPECT_NE(lastLineageDetail(*view->document().asset()).find(an::demosaicMethodName(*item.expected)),
                      std::string::npos)
                << item.setting << ": " << lastLineageDetail(*view->document().asset());
        }
        else
        {
            EXPECT_TRUE(view->imageDescriptor()->isMosaiced()) << "a mosaic the person keeps is shown as recorded";
            EXPECT_TRUE(view->isCFA());
        }
    }
}

// A standard open asked the engine for the setting's Bayer method over an X-Trans mosaic, which the engine refuses, so
// no X-Trans file opened in colour: the two fast estimates run as the tabulated fast one, every other method as the
// three-pass Markesteijn estimate, and a mosaic the person keeps is shown as recorded, table and all.
TEST_F(ImageViewControlTest, AStandardOpenDemosaicsAnXTransMosaicByTheTabulatedCounterpartOfTheSetting)
{
    open();
    const QString path = m_directory.filePath(QStringLiteral("xtrans.tif"));
    ASSERT_TRUE(writeXTransTiff(session(), path));
    struct Case
    {
        const char* setting;
        bool answer;
        std::optional<an::DemosaicMethod> expected;
    };
    for (const Case& item :
         {Case{"bilinear", true, an::DemosaicMethod::XTransFast},
          Case{"vng4", true, an::DemosaicMethod::MarkesteijnThreePass},
          Case{"rcd", true, an::DemosaicMethod::MarkesteijnThreePass},
          Case{"dcb", true, an::DemosaicMethod::MarkesteijnThreePass}, Case{"bilinear", false, std::nullopt}})
    {
        (void)m_context->upsert(QStringLiteral("raw/demosaicAlgorithm"), QString::fromLatin1(item.setting));
        m_context->answerYes = item.answer;
        ViewHandle view = makeView("xtrans");
        view->loadImageFromFile(path.toStdString());
        ASSERT_TRUE(settle(*view)) << item.setting;
        ASSERT_TRUE(view->imageDescriptor().has_value()) << item.setting;
        EXPECT_TRUE(said(*m_context, QStringLiteral("question:Debayer Image")));
        if (item.expected.has_value())
        {
            EXPECT_EQ(view->imageDescriptor()->extent().channelCount(), 3u) << item.setting;
            EXPECT_FALSE(view->imageDescriptor()->isMosaiced()) << item.setting;
            EXPECT_NE(lastLineageDetail(*view->document().asset()).find(an::demosaicMethodName(*item.expected)),
                      std::string::npos)
                << item.setting << ": " << lastLineageDetail(*view->document().asset());
        }
        else
        {
            EXPECT_TRUE(view->imageDescriptor()->isMosaiced()) << "a mosaic the person keeps is shown as recorded";
            EXPECT_TRUE(view->isCFA());
            EXPECT_TRUE(view->imageDescriptor()->cfa().hasFilterTable());
        }
    }
}

// A sequence the view did not own was never copied; a view now owns its document and a copy is its own.
TEST_F(ImageViewControlTest, ASequenceViewOwnsItsDocumentAndACopyOfItsFrameIsIndependent)
{
    open();
    const QString capture = m_directory.filePath(QStringLiteral("capture.ser"));
    writeSerFile(session(), capture, 3u, 2u, 5u, [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
        return static_cast<float>(x + 10u * y + frame) / 100.0f;
    });
    std::optional<an::ui::ImageDocument> frame = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(frame.has_value());
    an::SequenceDescriptor descriptor;
    descriptor.format = an::SequenceFormat::Ser;
    descriptor.frameCount = 5u;
    frame->setSequence(an::ui::DocumentSequence{.sourcePath = capture, .descriptor = descriptor, .currentFrame = 2u});
    ViewHandle sequenceView = makeView("sequence");
    sequenceView->setSequenceDocument(std::move(*frame), false);
    ASSERT_TRUE(settle(*sequenceView));

    EXPECT_TRUE(sequenceView->isSequence());
    EXPECT_TRUE(sequenceView->isSERSequence());
    EXPECT_EQ(sequenceView->getSequenceNumOfFrames(), 5u);
    EXPECT_EQ(sequenceView->getSequenceCurrentFrameIndex(), 2u);
    ASSERT_NE(sequenceView->sequencePlayer(), nullptr) << "a sequence view lays its player out under the image";
    EXPECT_EQ(sequenceView->sequencePlayer()->currentFrame(), 2u);
    EXPECT_EQ(sequenceView->getFileName(), capture.toStdString());

    ViewHandle copy = makeView("copy");
    copy->loadImageFromOtherViewControl(sequenceView.get());
    ASSERT_TRUE(settle(*copy));
    ASSERT_TRUE(copy->hasImage());
    EXPECT_NE(copy->document().identity(), sequenceView->document().identity());
    EXPECT_NE(copy->document().asset()->identity(), sequenceView->document().asset()->identity());
    EXPECT_FALSE(copy->isSequence()) << "the copy is of the frame, not of the sequence";

    copy->horizontalMirror();
    ASSERT_TRUE(settle(*copy));
    const std::optional<float> original = sampleOf<float>(*sequenceView->document().asset(), 0u, 0u);
    ASSERT_TRUE(original.has_value());
    EXPECT_FLOAT_EQ(*original, 0.0f) << "editing the copy leaves the sequence's frame as it was";
}

TEST_F(ImageViewControlTest, TheSequencePlayerIsHeldWhileAProcessHasTheView)
{
    open();
    const QString capture = m_directory.filePath(QStringLiteral("capture.ser"));
    writeSerFile(session(), capture, 3u, 2u, 3u,
                 [](std::uint32_t frame, std::uint32_t x, std::uint32_t) { return static_cast<float>(x + frame); });
    std::optional<an::ui::ImageDocument> frame = numberedDocument(session(), 3u, 2u);
    ASSERT_TRUE(frame.has_value());
    an::SequenceDescriptor descriptor;
    descriptor.format = an::SequenceFormat::Ser;
    descriptor.frameCount = 3u;
    frame->setSequence(an::ui::DocumentSequence{.sourcePath = capture, .descriptor = descriptor});
    ViewHandle view = makeView("sequence");
    view->setSequenceDocument(std::move(*frame), false);
    ASSERT_TRUE(settle(*view));
    an::ui::IViewSequencePlayer* player = view->sequencePlayer();
    ASSERT_NE(player, nullptr);
    ASSERT_TRUE(pumpUntil([player]() { return player->widget()->isEnabled(); }))
        << "the player answers once its sequence is open";

    ASSERT_TRUE(view->tryRequestLockForProcessing());
    pumpQuietly();
    EXPECT_FALSE(player->widget()->isEnabled()) << "the player answered while a process held the view";
    view->releaseLockForProcessing();
    pumpQuietly();
    EXPECT_TRUE(player->widget()->isEnabled());
}

// A four-channel image passed the selector's colour check and its alpha was tinted as a fourth layer.
TEST_F(ImageViewControlTest, AMaskThatIsNotOneOrThreeChannelsOfTheImagesSizeIsRefused)
{
    open();
    ViewHandle target = makeView("target");
    std::optional<an::ui::ImageDocument> image = flatDocument(session(), 0.5f, 4u, 4u);
    ASSERT_TRUE(image.has_value());
    target->setDocument(std::move(*image));

    const auto constant = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; };
    ViewHandle rgba = makeView("rgba");
    ViewHandle rgb = makeView("rgb");
    ViewHandle small = makeView("small");
    ViewHandle grey = makeView("grey");
    std::optional<an::ui::ImageDocument> rgbaDocument =
        makeDocument<float>(session(), 4u, 4u, Channels::Rgba, constant);
    std::optional<an::ui::ImageDocument> rgbDocument = makeDocument<float>(session(), 4u, 4u, Channels::Rgb, constant);
    std::optional<an::ui::ImageDocument> smallDocument =
        makeDocument<float>(session(), 3u, 3u, Channels::Grey, constant);
    std::optional<an::ui::ImageDocument> greyDocument =
        makeDocument<float>(session(), 4u, 4u, Channels::Grey, constant);
    ASSERT_TRUE(rgbaDocument && rgbDocument && smallDocument && greyDocument);
    rgba->setDocument(std::move(*rgbaDocument));
    rgb->setDocument(std::move(*rgbDocument));
    small->setDocument(std::move(*smallDocument));
    grey->setDocument(std::move(*greyDocument));

    for (ImageViewControl* refused : {rgba.get(), rgb.get(), small.get()})
    {
        m_context->messages.clear();
        target->setCurrentMask(refused);
        EXPECT_FALSE(target->isMasked()) << refused->getUniqueName();
        EXPECT_TRUE(said(*m_context, QStringLiteral("warning:Mask Reference"))) << refused->getUniqueName();
    }
    target->setCurrentMask(grey.get());
    EXPECT_TRUE(target->isMasked());
    ASSERT_TRUE(settle(*target));
    ASSERT_NE(target->getViewWidget()->maskLayer(), nullptr);
    EXPECT_EQ(target->getViewWidget()->maskLayer()->width(), 4u);
    target->removeCurrentMask();
}

// A multiply overlay shows what the view shows times the mask, never the raw samples, and follows the image when it
// changes.
TEST_F(ImageViewControlTest, AMultiplyShowsTheImageAsTheViewShowsItAndFollowsItsChanges)
{
    open();
    ViewHandle target = makeView("target");
    ViewHandle mask = makeView("mask");
    std::optional<an::ui::ImageDocument> dark =
        makeDocument<float>(session(), 8u, 8u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return 0.01f * static_cast<float>(x + y);
        });
    std::optional<an::ui::ImageDocument> whole = flatDocument(session(), 1.0f);
    ASSERT_TRUE(dark.has_value() && whole.has_value());
    target->setDocument(std::move(*dark));
    mask->setDocument(std::move(*whole));
    target->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    target->setCurrentMask(mask.get());
    target->setMaskRenderingMode(MaskRenderingMode::multiply_mode);
    ASSERT_TRUE(settle(*target));

    const auto layerMatchesShown = [&target]() {
        const std::shared_ptr<const an::ui::PresentedImage> layer = target->getViewWidget()->maskLayer();
        const std::shared_ptr<const an::ui::PresentedImage> shown = target->getViewWidget()->presentedImage();
        if (!layer || !shown || layer->width() != shown->width() || layer->height() != shown->height())
        {
            return false;
        }
        for (std::uint32_t y = 0u; y < shown->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < shown->width(); ++x)
            {
                const std::uint8_t* painted = layer->pixel(x, y);
                const std::uint8_t* presented = shown->pixel(x, y);
                if (painted[0] != presented[0] || painted[3] != 255u)
                {
                    return false;
                }
            }
        }
        return true;
    };
    EXPECT_TRUE(layerMatchesShown()) << "a mask of one multiplies what the view shows, stretched";
    const std::uint8_t brightestShown = target->getViewWidget()->presentedImage()->pixel(7u, 7u)[0];
    EXPECT_GT(brightestShown, 64u) << "the image is shown stretched, not near black";

    const std::uint64_t layersBefore = target->getViewWidget()->presentationTelemetry().masksCommitted;
    std::optional<an::consumer::ImageAsset> brighter =
        makeAsset<float>(session(), 8u, 8u, Channels::Grey,
                         [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.1f * static_cast<float>(x); });
    ASSERT_TRUE(brighter.has_value());
    target->replaceImage(std::move(*brighter));
    ASSERT_TRUE(settle(*target));
    EXPECT_GT(target->getViewWidget()->presentationTelemetry().masksCommitted, layersBefore)
        << "a change of the image is a new layer";
    EXPECT_TRUE(layerMatchesShown()) << "the layer shows the new image";
    target->removeCurrentMask();
}

// Zooming out rescaled the mask layer's previous, already rescaled pixmap, compounding the resampling.
TEST_F(ImageViewControlTest, AZoomedOutMaskIsScaledFromTheFullLayerEveryTime)
{
    open();
    const auto gradient = [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return static_cast<float>(x + y) / 128.0f;
    };
    const auto checker = [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
        return ((x / 2u + y / 2u) % 2u) == 0u ? 1.0f : 0.0f;
    };
    std::vector<ViewHandle> targets;
    std::vector<ViewHandle> masks;
    for (int index = 0; index < 2; ++index)
    {
        targets.push_back(makeView("target" + std::to_string(index)));
        masks.push_back(makeView("mask" + std::to_string(index)));
        std::optional<an::ui::ImageDocument> image = makeDocument<float>(session(), 64u, 64u, Channels::Grey, gradient);
        std::optional<an::ui::ImageDocument> mask = makeDocument<float>(session(), 64u, 64u, Channels::Grey, checker);
        ASSERT_TRUE(image.has_value() && mask.has_value());
        targets.back()->setDocument(std::move(*image));
        masks.back()->setDocument(std::move(*mask));
        targets.back()->setCurrentMask(masks.back().get());
        ASSERT_TRUE(settle(*targets.back()));
    }

    // One view zooms out a step at a time, the other goes straight to the same zoom.
    ImageViewWidget* stepped = targets[0]->getViewWidget();
    ImageViewWidget* direct = targets[1]->getViewWidget();
    for (int step = 0; step < 3; ++step)
    {
        stepped->scaleImageView(0.8);
        pumpQuietly();
    }
    direct->scaleImageView(stepped->getCurrentScaleFactor(), true);
    ASSERT_TRUE(settle(*targets[0]) && settle(*targets[1]));
    ASSERT_DOUBLE_EQ(stepped->getCurrentScaleFactor(), direct->getCurrentScaleFactor());
    ASSERT_EQ(stepped->maskLayer()->width(), 64u) << "the layer is presented with the image, at its reduction";
    ASSERT_EQ(direct->maskLayer()->width(), stepped->maskLayer()->width());

    const QImage steppedPaint = stepped->grab().toImage();
    const QImage directPaint = direct->grab().toImage();
    ASSERT_EQ(steppedPaint.size(), directPaint.size());
    int differing = 0;
    for (int y = 0; y < steppedPaint.height(); ++y)
    {
        for (int x = 0; x < steppedPaint.width(); ++x)
        {
            differing += steppedPaint.pixel(x, y) != directPaint.pixel(x, y) ? 1 : 0;
        }
    }
    EXPECT_EQ(differing, 0) << "a mask zoomed out step by step is painted as the one scaled once";
    for (ViewHandle& target : targets)
    {
        target->removeCurrentMask();
    }
}

// What a view's presentations cost is kept: the engine's time on a worker and the time the GUI thread took to adopt
// what it painted, which makes no pixmap of the bytes; a request for what is painted already costs nothing.
TEST_F(ImageViewControlTest, WhatAPresentationCostIsKeptInTheViewsTelemetry)
{
    open();
    ViewHandle view = makeView("costs");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 256u, 192u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));

    const ImageViewPresentationTelemetry shown = view->getViewWidget()->presentationTelemetry();
    EXPECT_GE(shown.presented, 1u);
    EXPECT_GT(shown.longestPresent, std::chrono::nanoseconds::zero());
    EXPECT_GE(shown.presentTime, shown.longestPresent);
    EXPECT_EQ(shown.pixmapsMade, 0u) << "the view paints the presented bytes as they are";
    EXPECT_EQ(shown.pixmapTime, std::chrono::nanoseconds::zero());
    EXPECT_GT(shown.longestAdopt, std::chrono::nanoseconds::zero());
    EXPECT_GE(shown.adoptTime, shown.longestAdopt);
    EXPECT_EQ(shown.presentedFactor, 1u) << "a view at full zoom presents every pixel it shows";

    view->getViewWidget()->refreshDisplayImage();
    ASSERT_TRUE(settle(*view));
    const ImageViewPresentationTelemetry again = view->getViewWidget()->presentationTelemetry();
    EXPECT_GT(again.requested, shown.requested);
    EXPECT_EQ(again.presented, shown.presented) << "what is painted already is not presented again";
    EXPECT_EQ(again.pixmapsMade, shown.pixmapsMade);
    EXPECT_EQ(again.adoptTime, shown.adoptTime);
}

// The paint draws the presented bytes as they are and makes no pixmap of them; the one pixmap a view makes on the GUI
// thread is the quick-zoom lens's, of the small region under it, and the telemetry counts it.
TEST_F(ImageViewControlTest, ThePaintMakesNoPixmapAndTheLensItsOwnSmallOne)
{
    open();
    ViewHandle view = makeView("lens");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 256u, 192u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    const ImageViewPresentationTelemetry before = widget->presentationTelemetry();

    widget->repaint();
    EXPECT_EQ(widget->presentationTelemetry().pixmapsMade, before.pixmapsMade) << "the paint made a pixmap";

    const auto send = [widget](QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF at(20.0, 20.0);
        QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &event);
    };
    send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    const ImageViewPresentationTelemetry lensed = widget->presentationTelemetry();
    send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(lensed.pixmapsMade, before.pixmapsMade + 1u) << "the lens made no pixmap, or more than its own";
    EXPECT_GE(lensed.pixmapTime, before.pixmapTime);
}

// A view presents a zoomed-out image's display proxy and says by how much it was reduced; at full zoom it presents
// every pixel again.
TEST_F(ImageViewControlTest, TheViewSaysHowFarThePresentationItPaintedWasReduced)
{
    open();
    ViewHandle view = makeView("factor", ViewControlMode::rt_preview);
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 256u, 192u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));

    view->getViewWidget()->scaleImageView(0.25, true);
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->getViewWidget()->presentationTelemetry().presentedFactor, 4u);
    ASSERT_NE(view->getViewWidget()->presentedImage(), nullptr);
    EXPECT_EQ(view->getViewWidget()->presentedImage()->width(), 64u);

    view->getViewWidget()->zoom100();
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(view->getViewWidget()->presentationTelemetry().presentedFactor, 1u);
    EXPECT_EQ(view->getViewWidget()->presentedImage()->width(), 256u);
}

// A view says which balance and statistics the presentation it painted was made through: nothing before it paints,
// nothing measured for a linear presentation, and the statistics of the image it shows in autostretch.
TEST_F(ImageViewControlTest, AViewSaysWhichAnalysisItsPaintedPresentationWasMadeThrough)
{
    open();
    ViewHandle view = makeView("analysis");
    EXPECT_FALSE(view->getViewWidget()->presentedAnalysis().has_value()) << "nothing is painted yet";
    std::optional<an::ui::ImageDocument> document =
        makeDocument<float>(session(), 16u, 8u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return 0.1f + 0.01f * static_cast<float>(x);
        });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const std::optional<an::DisplayPresentationAnalysis> linear = view->getViewWidget()->presentedAnalysis();
    ASSERT_TRUE(linear.has_value());
    EXPECT_FALSE(linear->measured) << "a linear presentation measures nothing";

    view->getViewWidget()->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    const std::optional<an::DisplayPresentationAnalysis> stretched = view->getViewWidget()->presentedAnalysis();
    ASSERT_TRUE(stretched.has_value());
    EXPECT_TRUE(stretched->measured);
    EXPECT_EQ(stretched->colorChannels, 1u);
    EXPECT_EQ(stretched->measuredSamples[0], 128u);
    EXPECT_NEAR(stretched->medians[0], 0.175, 0.006) << "the median of the image's sixteen columns";
}

// The painted bytes belong to their presentation: replacing the image never pulls them from under a paint.
TEST_F(ImageViewControlTest, APaintedPresentationOutlivesTheImageItWasPresentedFrom)
{
    open();
    ViewHandle view = makeView("buffers");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 8u, 4u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    std::shared_ptr<const an::ui::PresentedImage> held = view->getViewWidget()->presentedImage();
    ASSERT_NE(held, nullptr);
    const std::vector<std::uint8_t> bytes(held->data(), held->data() + held->byteCount());
    QImage painted = an::ui::presentedQImage(held);

    std::optional<an::consumer::ImageAsset> other = makeAsset<float>(
        session(), 16u, 16u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.9f; });
    ASSERT_TRUE(other.has_value());
    view->replaceImage(std::move(*other));
    ASSERT_TRUE(settle(*view));
    ASSERT_NE(view->getViewWidget()->presentedImage(), held);
    EXPECT_EQ(view->getViewWidget()->imageSize(), QSize(16, 16));

    EXPECT_EQ(std::memcmp(held->data(), bytes.data(), bytes.size()), 0) << "the earlier bytes are unchanged";
    held.reset();
    ASSERT_FALSE(painted.isNull());
    EXPECT_EQ(painted.width(), 8);
    const std::uint32_t pixelBytes = static_cast<std::uint32_t>(painted.depth() / 8);
    EXPECT_EQ(qGray(painted.pixel(7, 3)), bytes[3u * painted.bytesPerLine() + 7u * pixelBytes])
        << "a QImage keeps its presentation alive after everything else let go";
}

TEST_F(ImageViewControlTest, AViewWritesItsImageAndClearsItsPendingChanges)
{
    open();
    ViewHandle view = makeView("saved");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 5u, 3u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    view->markPendingChanges();
    const QString path = m_directory.filePath(QStringLiteral("saved.fits"));

    const an::Status saved = view->saveImageToFile(path.toStdString());
    ASSERT_TRUE(saved.ok()) << saved.message();
    pumpQuietly();
    EXPECT_FALSE(view->hasPendingChanges());
    EXPECT_EQ(view->document().names().sourcePath, path);
    EXPECT_EQ(view->workTelemetry().writesCompleted, 1u);

    an::Result<an::ui::ViewReadResult> read =
        an::ui::readViewImage(session(), an::ui::ViewReadRequest{.path = path}, {}, nullptr);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    const std::optional<float> sample = sampleOf<float>(read->asset, 4u, 2u);
    ASSERT_TRUE(sample.has_value());
    EXPECT_FLOAT_EQ(*sample, 0.24f);

    const an::Status refused = view->saveImageToFile(m_directory.filePath(QStringLiteral("x.nothing")).toStdString());
    EXPECT_TRUE(refused.failed());
    EXPECT_EQ(view->workTelemetry().writesFailed, 1u);
}

TEST_F(ImageViewControlTest, AStaticPreviewShowsTheLightCalibratedByItsMasters)
{
    open();
    const QString light = m_directory.filePath(QStringLiteral("light.fits"));
    const QString bias = m_directory.filePath(QStringLiteral("bias.fits"));
    for (const auto& [path, value] : {std::pair{light, 0.5f}, std::pair{bias, 0.125f}})
    {
        std::optional<an::consumer::ImageAsset> asset = makeAsset<float>(
            session(), 8u, 8u, Channels::Grey, [value](std::uint32_t, std::uint32_t, std::uint32_t) { return value; });
        ASSERT_TRUE(asset.has_value());
        writeFitsFile(session(), *asset, path);
    }
    ViewHandle view = makeView("preview", ViewControlMode::st_preview);
    an::ui::CalibrationPreviewConfig config;
    config.available = true;
    config.biasPath = bias;
    config.settings.cosmeticCorrection = false;
    view->setCalibrationPreviewConfig(config);
    view->setCalibrationPreviewVisible(true);
    auto* calibrated = view->findChild<QCheckBox*>(QStringLiteral("chkCalibrationPreview"));
    ASSERT_NE(calibrated, nullptr);
    calibrated->setChecked(true);
    ASSERT_TRUE(view->isCalibrationPreviewEnabled());

    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(view->hasImage());
    const std::optional<float> sample = sampleOf<float>(*view->document().asset(), 3u, 3u);
    ASSERT_TRUE(sample.has_value());
    EXPECT_NEAR(*sample, 0.375f, 1e-6f) << "the preview shows the light less its bias";

    calibrated->setChecked(false);
    view->loadImageFromFile(light.toStdString());
    ASSERT_TRUE(settle(*view));
    const std::optional<float> raw = sampleOf<float>(*view->document().asset(), 3u, 3u);
    ASSERT_TRUE(raw.has_value());
    EXPECT_FLOAT_EQ(*raw, 0.5f);
}

} // namespace

namespace
{
/// Counts the paint events the scroll area of one view and its viewport receive while it is installed.
class ScrollAreaPaintCounter final : public QObject
{
public:
    explicit ScrollAreaPaintCounter(ImageViewScrollArea* area) : m_area(area)
    {
        m_area->installEventFilter(this);
        m_area->viewport()->installEventFilter(this);
    }

    ~ScrollAreaPaintCounter() override
    {
        m_area->viewport()->removeEventFilter(this);
        m_area->removeEventFilter(this);
    }

    int paints = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
        {
            ++paints;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    ImageViewScrollArea* m_area = nullptr;
};

/// Runs the event loop for a while, as an idle window does.
void idle(const int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}
} // namespace

// A view that shows an image and is left alone paints nothing more: its scroll area takes the theme's colours as it
// paints, so a theme switch applies, and taking colours it already has asks for no paint - which, set again on every
// paint, kept the view's margins repainting thousands of times a second.
TEST_F(ImageViewControlTest, AViewLeftAloneRepaintsNothingAndStillTakesTheThemesColours)
{
    open();
    ViewHandle view = makeView("idle");
    std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 200u, 100u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    idle(300);

    ImageViewScrollArea* area = view->getScrollArea();
    ASSERT_NE(area, nullptr);
    {
        const ScrollAreaPaintCounter counter(area);
        idle(500);
        EXPECT_LE(counter.paints, 2) << "a view left alone for half a second repainted its scroll area";
    }

    const an::ui::ThemePreference before = an::ui::themePreference();
    const bool wasDark = an::ui::isDarkTheme();
    an::ui::setThemePreference(wasDark ? an::ui::ThemePreference::Light : an::ui::ThemePreference::Dark);
    ASSERT_NE(an::ui::isDarkTheme(), wasDark);
    // A scroll area paints its viewport through its own paint event; a theme switch repaints both.
    area->viewport()->update();
    idle(100);
    const QColor expected = an::ui::imageViewScrollAreaWindowColor(an::ui::isDarkTheme());
    EXPECT_EQ(area->viewport()->palette().color(QPalette::Active, QPalette::Window), expected);
    EXPECT_EQ(area->palette().color(QPalette::Inactive, QPalette::Dark), expected);
    an::ui::setThemePreference(before);
}

namespace
{

/// A camera-sized document of sixteen-bit codes: a background with a slow gradient and some structure.
[[nodiscard]] std::optional<an::ui::ImageDocument> cameraDocument(const an::consumer::EngineSession& session,
                                                                  Channels channels)
{
    return makeDocument<std::uint16_t>(
        session, 6000u, 4000u, channels, [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
            return static_cast<std::uint16_t>(1500u + (x * 7u + y * 13u + channel * 101u) % 2048u + x / 20u);
        });
}

/// The reduction a view at a zoom presents its display proxy at: the largest that leaves it at least what it paints.
[[nodiscard]] std::uint32_t expectedFactor(ImageViewWidget& widget)
{
    const double deviceScale = widget.getCurrentScaleFactor() * widget.devicePixelRatioF();
    return deviceScale >= 1.0 ? 1u : static_cast<std::uint32_t>(std::floor(1.0 / deviceScale + 1.0e-9));
}

/// The part of the image a view shows now, in its own pixels.
[[nodiscard]] QRect shownPart(ImageViewControl& view)
{
    ImageViewWidget* widget = view.getViewWidget();
    QWidget* viewport = view.getScrollArea()->viewport();
    const QRect visible = QRect(widget->mapFrom(viewport, QPoint(0, 0)), viewport->size()).intersected(widget->rect());
    const double scale = widget->getCurrentScaleFactor();
    return QRect(QPoint(static_cast<int>(std::floor(visible.left() / scale)),
                        static_cast<int>(std::floor(visible.top() / scale))),
                 QPoint(static_cast<int>(std::ceil((visible.right() + 1) / scale)) - 1,
                        static_cast<int>(std::ceil((visible.bottom() + 1) / scale)) - 1))
        .intersected(QRect(QPoint(0, 0), widget->imageSize()));
}

/// The budget of one adoption of presented bytes on the GUI thread, with room for a loaded test host.
constexpr std::chrono::milliseconds kAdoptBudget{16};

} // namespace

// A zoomed-out view presented and converted every sample - seventy-two million for a camera frame - and an
// autostretched one did so on every update, then made a pixmap of them on the GUI thread and scaled it there. It now
// presents its display proxy, reduced to about the size it paints, in bytes it paints as they are, in every mode, and
// stretches it by statistics of every sample of the image.
TEST_F(ImageViewControlTest, AZoomedOutViewPresentsItsDisplayProxyAtTheSizeItPaintsInEveryMode)
{
    open(4u);
    ViewHandle view = makeView("proxy");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Rgb);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    ASSERT_TRUE(settle(*view));

    for (const ImageViewMode mode : {ImageViewMode::LINEAR_MODE, ImageViewMode::AUTOSTRETCH_MODE})
    {
        widget->setViewMode(mode);
        ASSERT_TRUE(settle(*view));
        const std::uint32_t factor = expectedFactor(*widget);
        ASSERT_GT(factor, 1u) << "the view fits a camera frame in less than its pixels";
        const ImageViewPresentationTelemetry telemetry = widget->presentationTelemetry();
        EXPECT_EQ(telemetry.presentedFactor, factor);
        const std::shared_ptr<const an::ui::PresentedImage> presented = widget->presentedImage();
        ASSERT_NE(presented, nullptr);
        EXPECT_EQ(presented->width(), 6000u / factor);
        EXPECT_EQ(presented->height(), 4000u / factor);
        const double painted = 6000.0 * widget->getCurrentScaleFactor() * widget->devicePixelRatioF();
        EXPECT_GE(presented->width(), static_cast<std::uint32_t>(std::floor(painted)))
            << "the proxy is no smaller than what is painted";
        EXPECT_LT(presented->width(), static_cast<std::uint32_t>(2.0 * painted) + 1u)
            << "and less than twice it, whatever the mode";
        EXPECT_EQ(presented->format(), an::ui::PresentedFormat::Bgra8Premultiplied);
        EXPECT_EQ(telemetry.pixmapsMade, 0u) << "the view paints the presented bytes, it makes no pixmap of them";
        EXPECT_LT(telemetry.longestAdopt, kAdoptBudget);
        EXPECT_GE(telemetry.proxiesReduced, 1u);
        if (mode == ImageViewMode::AUTOSTRETCH_MODE)
        {
            const std::optional<an::DisplayPresentationAnalysis> analysis = widget->presentedAnalysis();
            ASSERT_TRUE(analysis.has_value());
            EXPECT_TRUE(analysis->measured);
            EXPECT_FALSE(analysis->realtimePreview);
            EXPECT_EQ(analysis->promotion, an::PresentationPromotion::Gradient);
            EXPECT_EQ(analysis->measuredSamples[0], 6000u * 4000u)
                << "the stretch's statistics are the whole image's, not the proxy's";
        }
    }
    EXPECT_EQ(widget->presentationTelemetry().proxiesReduced, 1u) << "the mode changed, the proxy did not";
}

// At full zoom a view presented its whole image, so a camera frame cost every sample and a pixmap of all of them on the
// GUI thread per presentation. It presents the part it shows with a margin; a short pan paints from what is presented,
// and one beyond it presents the part shown then.
TEST_F(ImageViewControlTest, AtFullZoomTheViewPresentsWhatItShowsAndPresentsAgainWhenItPansPastIt)
{
    open(4u);
    ViewHandle view = makeView("region");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Grey);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    widget->zoom100();
    ASSERT_TRUE(settle(*view));

    const QRect region = widget->presentedRegion();
    const QRect shown = shownPart(*view);
    ASSERT_FALSE(shown.isEmpty());
    EXPECT_TRUE(region.contains(shown)) << "what is shown is presented";
    EXPECT_LT(region.width(), 6000) << "and not the whole image";
    EXPECT_LT(region.height(), 4000);
    ASSERT_NE(widget->presentedImage(), nullptr);
    EXPECT_EQ(widget->presentedImage()->width(), static_cast<std::uint32_t>(region.width()));
    EXPECT_EQ(widget->presentationTelemetry().presentedFactor, 1u);
    EXPECT_EQ(widget->presentationTelemetry().pixmapsMade, 0u);
    EXPECT_LT(widget->presentationTelemetry().longestAdopt, kAdoptBudget);
    ASSERT_TRUE(widget->presentedAnalysis().has_value());
    EXPECT_EQ(widget->presentedAnalysis()->measuredSamples[0], 6000u * 4000u)
        << "a region is stretched by the whole image's statistics";

    QScrollBar* across = view->getScrollArea()->horizontalScrollBar();
    const ImageViewPresentationTelemetry beforeShortPan = widget->presentationTelemetry();
    across->setValue(across->value() + 8);
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(widget->presentationTelemetry().requested, beforeShortPan.requested)
        << "a short pan paints from what is presented";

    across->setValue(across->maximum());
    ASSERT_TRUE(settle(*view));
    const QRect panned = widget->presentedRegion();
    EXPECT_GT(widget->presentationTelemetry().presented, beforeShortPan.presented);
    EXPECT_TRUE(panned.contains(shownPart(*view))) << "the part shown after a long pan is presented";
    EXPECT_EQ(panned.right(), 5999);
    EXPECT_EQ(widget->presentationTelemetry().wholeImageMeasured, 1u)
        << "the whole image was measured once for its revision, not once per region";
}

// A zoom asks for a presentation before the scroll bars move the view to keep its centre, so that request is of the
// part around where the view was. When the view then moved, what was painted could still hold what was shown while
// what was on its way did not, and it landed over it: every other step of a zoom in about the centre left part of the
// view unpainted until the next. Each step of a zoom in, and of the zoom out after it, paints all the view shows.
TEST_F(ImageViewControlTest, EveryStepOfAZoomPaintsAllTheViewShows)
{
    open(4u);
    ViewHandle view = makeView("zoomed");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Grey);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    ASSERT_TRUE(settle(*view));

    const auto paintsAllItShows = [&](const char* zoom, int step) {
        // A reduced presentation covers every whole block of its factor, and no more of the right and bottom edges.
        const std::uint32_t factor = (std::max)(1u, widget->presentationTelemetry().presentedFactor);
        const QSize image = widget->imageSize();
        const QRect blocks(0, 0, image.width() / static_cast<int>(factor) * static_cast<int>(factor),
                           image.height() / static_cast<int>(factor) * static_cast<int>(factor));
        const QRect shown = shownPart(*view).intersected(blocks);
        const QRect painted = widget->presentedRegion();
        EXPECT_TRUE(painted.contains(shown))
            << zoom << " step " << step << " at " << widget->getCurrentScaleFactor() << ": shows " << shown.x() << ','
            << shown.y() << ' ' << shown.width() << 'x' << shown.height() << ", paints " << painted.x() << ','
            << painted.y() << ' ' << painted.width() << 'x' << painted.height();
    };
    for (int step = 1; step <= 16; ++step)
    {
        widget->zoomIn();
        ASSERT_TRUE(settle(*view));
        paintsAllItShows("in", step);
    }
    for (int step = 1; step <= 16; ++step)
    {
        widget->zoomOut();
        ASSERT_TRUE(settle(*view));
        paintsAllItShows("out", step);
    }
}

// A pan beyond what is painted asks for the part shown then; a pan straight back, before that lands, shows what is
// painted again, and the presentation on its way, of the part the view left, is not painted over it.
TEST_F(ImageViewControlTest, APanAwayAndBackBeforeItsPresentationLandsKeepsWhatIsShownPainted)
{
    open(4u);
    ViewHandle view = makeView("panned");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Grey);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    ASSERT_TRUE(settle(*view));
    widget->zoom100();
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(widget->presentedRegion().contains(shownPart(*view)));
    ASSERT_LT(widget->presentedRegion().width(), 6000) << "a part of the image is presented";

    // Twice: the second time the part shown on the way back is exactly the part painted, which asks for nothing new.
    QScrollBar* across = view->getScrollArea()->horizontalScrollBar();
    const int start = across->value();
    for (int pan = 1; pan <= 2; ++pan)
    {
        const ImageViewPresentationTelemetry before = widget->presentationTelemetry();
        across->setValue(across->maximum());
        ASSERT_GT(widget->presentationTelemetry().requested, before.requested) << "the far part is asked for";
        across->setValue(start);
        ASSERT_TRUE(settle(*view));
        const QRect shown = shownPart(*view);
        const QRect painted = widget->presentedRegion();
        EXPECT_TRUE(painted.contains(shown)) << "pan " << pan << " shows " << shown.x() << ',' << shown.y() << ' '
                                             << shown.width() << 'x' << shown.height() << ", paints " << painted.x()
                                             << ',' << painted.y() << ' ' << painted.width() << 'x' << painted.height();
    }
}

namespace
{

/// The pixels of a part of a picture that differ from one colour: in its first and last row, in its first and last
/// column short of those rows, and within it.
struct PixelsNotOf
{
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
    int inside = 0;

    [[nodiscard]] bool none() const
    {
        return top == 0 && bottom == 0 && left == 0 && right == 0 && inside == 0;
    }
};

[[nodiscard]] PixelsNotOf pixelsNotOf(const QImage& picture, const QRect& part, QRgb colour)
{
    PixelsNotOf found;
    for (int y = part.top(); y <= part.bottom(); ++y)
    {
        const auto* row = reinterpret_cast<const QRgb*>(picture.constScanLine(y));
        for (int x = part.left(); x <= part.right(); ++x)
        {
            if ((row[x] & 0x00ffffffu) == (colour & 0x00ffffffu))
            {
                continue;
            }
            int& count = y == part.top()      ? found.top
                         : y == part.bottom() ? found.bottom
                         : x == part.left()   ? found.left
                         : x == part.right()  ? found.right
                                              : found.inside;
            ++count;
        }
    }
    return found;
}

/// The device pixels a widget at a rectangle of logical pixels covers at a ratio: each edge at the nearest device
/// pixel, as Qt maps a widget's rectangle to the pixels it paints.
[[nodiscard]] QRect devicePixelsOf(const QRect& logical, double ratio)
{
    const auto edge = [ratio](int at) { return qRound(at * ratio); };
    return QRect(QPoint(edge(logical.left()), edge(logical.top())),
                 QPoint(edge(logical.left() + logical.width()) - 1, edge(logical.top() + logical.height()) - 1));
}

/// A widget and its children painted at a device pixel ratio, as a screen under that scaling shows them, a number of
/// logical pixels each way from the picture's corner, as a window lays a widget away from its own.
[[nodiscard]] QImage paintedAt(QWidget& widget, double ratio, int offset)
{
    QImage picture(QSize(static_cast<int>(std::ceil((widget.width() + offset) * ratio)),
                         static_cast<int>(std::ceil((widget.height() + offset) * ratio))),
                   QImage::Format_ARGB32_Premultiplied);
    picture.setDevicePixelRatio(ratio);
    picture.fill(Qt::transparent);
    widget.render(&picture, QPoint(offset, offset));
    return picture;
}

} // namespace

// A zoomed view painted the image short of its last row and column - a reduced presentation covers whole blocks of its
// reduction, and the zoom rounds the widget's size to whole pixels - and, where a fractional scaling put the widget's
// edge at a fraction of a device pixel, ended the image there too, so the pixel blended the image with the background:
// a dark line came and went at the image's edges from one zoom to the next. The part of a flat image a view shows is
// one colour to its edges at every zoom, scrolled to either corner, at the device pixel ratios a screen scales by.
TEST_F(ImageViewControlTest, EveryZoomPaintsTheImageToItsEdges)
{
    open(4u);
    ViewHandle view = makeView("edges");
    // Of a size no reduction divides, so a reduced presentation's whole blocks never reach the right and bottom edges.
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        session(), 6001u, 4001u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 40000u; });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    ASSERT_TRUE(settle(*view));
    QScrollBar* across = view->getScrollArea()->horizontalScrollBar();
    QScrollBar* down = view->getScrollArea()->verticalScrollBar();
    QWidget* viewport = view->getScrollArea()->viewport();

    std::optional<QRgb> image;
    const auto paintsToTheEdges = [&](const char* zoom, int step) {
        for (const bool farCorner : {false, true})
        {
            across->setValue(farCorner ? across->maximum() : across->minimum());
            down->setValue(farCorner ? down->maximum() : down->minimum());
            ASSERT_TRUE(settle(*view));
            const QRect visible = widget->geometry().intersected(viewport->rect());
            ASSERT_FALSE(visible.isEmpty());
            // The viewport is painted, not the widget alone, so the widget lies where it does in it, as on the screen;
            // and once a pixel away from the picture's corner, which moves its edges by a fraction of a device pixel
            // under a fractional scaling.
            for (const double ratio : {1.0, 1.25, 1.5, 1.75})
            {
                for (const int offset : {0, 1})
                {
                    const QImage picture = paintedAt(*viewport, ratio, offset);
                    const QRect part = devicePixelsOf(visible.translated(offset, offset), ratio);
                    if (!image.has_value())
                    {
                        image = picture.pixel(part.center());
                    }
                    const PixelsNotOf unpainted = pixelsNotOf(picture, part, *image);
                    EXPECT_TRUE(unpainted.none())
                        << zoom << " step " << step << " at " << widget->getCurrentScaleFactor() << " (reduced by "
                        << widget->presentationTelemetry().presentedFactor << ", widget at " << widget->x() << ','
                        << widget->y() << ' ' << widget->width() << 'x' << widget->height() << ", ratio " << ratio
                        << ", offset " << offset << (farCorner ? ", far corner" : ", near corner")
                        << "): pixels not of the image in the top row " << unpainted.top << ", the bottom row "
                        << unpainted.bottom << ", the left column " << unpainted.left << ", the right column "
                        << unpainted.right << ", within " << unpainted.inside;
                }
            }
        }
    };
    paintsToTheEdges("fit", 0);
    for (int step = 1; step <= 14; ++step)
    {
        widget->zoomIn();
        ASSERT_TRUE(settle(*view));
        paintsToTheEdges("in", step);
    }
    // Back out and beyond the fit, where the view centres a widget smaller than it at offsets of every fraction of a
    // device pixel.
    for (int step = 1; step <= 18; ++step)
    {
        widget->zoomOut();
        ASSERT_TRUE(settle(*view));
        paintsToTheEdges("out", step);
    }
}

// A multiply demanded a presentation of every pixel to multiply, so showing a mask that way put a zoomed-out view
// back on its slowest path. The layer is made with the presentation, from the mask's proxy at the same reduction.
TEST_F(ImageViewControlTest, AMultiplyOverAZoomedOutViewIsPresentedAtItsProxysSize)
{
    open(4u);
    ViewHandle target = makeView("multiplied");
    ViewHandle mask = makeView("multiplier");
    std::optional<an::ui::ImageDocument> image = cameraDocument(session(), Channels::Grey);
    std::optional<an::ui::ImageDocument> maskDocument = makeDocument<std::uint8_t>(
        session(), 6000u, 4000u, Channels::Grey, [](std::uint32_t x, std::uint32_t, std::uint32_t) {
            return static_cast<std::uint8_t>(x < 3000u ? 255u : 64u);
        });
    ASSERT_TRUE(image.has_value() && maskDocument.has_value());
    target->setDocument(std::move(*image));
    mask->setDocument(std::move(*maskDocument));
    target->resize(900, 700);
    target->show();
    ASSERT_TRUE(settle(*target));
    ImageViewWidget* widget = target->getViewWidget();
    widget->fitInView();
    target->setCurrentMask(mask.get());
    target->setMaskRenderingMode(MaskRenderingMode::multiply_mode);
    ASSERT_TRUE(settle(*target));
    ASSERT_TRUE(settle(*mask));

    const std::uint32_t factor = expectedFactor(*widget);
    ASSERT_GT(factor, 1u);
    const std::shared_ptr<const an::ui::PresentedImage> presented = widget->presentedImage();
    const std::shared_ptr<const an::ui::PresentedImage> layer = widget->maskLayer();
    ASSERT_NE(presented, nullptr);
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(widget->presentationTelemetry().presentedFactor, factor) << "the multiply keeps the proxy";
    EXPECT_EQ(presented->width(), 6000u / factor);
    EXPECT_EQ(layer->width(), presented->width());
    EXPECT_EQ(layer->height(), presented->height());
    EXPECT_EQ(presented->format(), an::ui::PresentedFormat::Grey8) << "a multiply reads what is shown as grey bytes";
    EXPECT_EQ(widget->presentationTelemetry().pixmapsMade, 0u);
    target->removeCurrentMask();
}

// An update of a view's image asked the widget twice - through its listener and through the channel it shows - and
// the second request was coalesced into the first. It asks once.
TEST_F(ImageViewControlTest, AnUpdateOfTheImageAsksForOnePresentation)
{
    open();
    // A standard view and a real-time preview's view alike.
    for (const ViewControlMode mode : {ViewControlMode::standard, ViewControlMode::rt_preview})
    {
        const char* kind = mode == ViewControlMode::standard ? "standard" : "preview";
        ViewHandle view = makeView(std::string("asked once ") + kind, mode);
        std::optional<an::ui::ImageDocument> document = numberedDocument(session(), 16u, 8u);
        ASSERT_TRUE(document.has_value());
        view->setDocument(std::move(*document));
        ASSERT_TRUE(settle(*view));
        const ImageViewPresentationTelemetry before = view->getViewWidget()->presentationTelemetry();

        std::optional<an::consumer::ImageAsset> replacement =
            makeAsset<float>(session(), 16u, 8u, Channels::Grey,
                             [](std::uint32_t x, std::uint32_t, std::uint32_t) { return 0.05f * x; });
        ASSERT_TRUE(replacement.has_value());
        view->replaceImage(std::move(*replacement));
        ASSERT_TRUE(settle(*view));
        const ImageViewPresentationTelemetry after = view->getViewWidget()->presentationTelemetry();
        EXPECT_EQ(after.requested - before.requested, 1u) << kind;
        EXPECT_EQ(after.coalesced, before.coalesced) << kind;
        EXPECT_EQ(after.presented - before.presented, 1u) << kind;
    }
}

// A view dropped the analysis it kept on every update of its image, so an update that left the image as it was
// measured it again; the analysis is kept while its key - the revision, the channel, the promotion and the balance -
// still fits, so a zoom, a pan, a region and an update that changes nothing measure nothing.
TEST_F(ImageViewControlTest, TheWholeImageIsMeasuredOncePerRevisionWhateverIsPresentedOfIt)
{
    open(4u);
    ViewHandle view = makeView("measured once");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Grey);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    widget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    ASSERT_EQ(widget->presentationTelemetry().wholeImageMeasured, 1u);
    EXPECT_GT(widget->presentationTelemetry().wholeImageMeasureTime, std::chrono::nanoseconds::zero());
    EXPECT_LE(widget->presentationTelemetry().wholeImageMeasureTime, widget->presentationTelemetry().presentTime)
        << "the measurement is part of the presentation it served";

    const ImageViewPresentationTelemetry fitted = widget->presentationTelemetry();
    widget->imageUpdated(true, true);
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(widget->presentationTelemetry().presented, fitted.presented)
        << "an update that changes nothing presents nothing";

    widget->scaleImageView(0.5, true);
    ASSERT_TRUE(settle(*view));
    widget->zoom100();
    ASSERT_TRUE(settle(*view));
    const ImageViewPresentationTelemetry zoomed = widget->presentationTelemetry();
    EXPECT_GE(zoomed.presented, fitted.presented + 2u);
    EXPECT_EQ(zoomed.wholeImageMeasured, 1u)
        << "the analysis kept through the update serves every reduction and region of the revision";

    std::optional<an::consumer::ImageAsset> replacement = makeAsset<std::uint16_t>(
        session(), 6000u, 4000u, Channels::Grey, [](std::uint32_t x, std::uint32_t y, std::uint32_t) {
            return static_cast<std::uint16_t>(900u + (x + y) % 512u);
        });
    ASSERT_TRUE(replacement.has_value());
    view->replaceImage(std::move(*replacement));
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(widget->presentationTelemetry().wholeImageMeasured, 2u) << "a new revision is measured";
}

// A real-time preview's frame over a part of the document - the preview area's patch - is painted as a layer over what
// the view paints, where that part lies, while what the view paints is its own presentation of the document under the
// frame's settings and nothing is on its way to replace it; the layer goes with the next presentation of the document.
TEST_F(ImageViewControlTest, AFramesPartOfTheDocumentIsPaintedAsALayerOverThePresentationItWasMadeFor)
{
    open(4u);
    ViewHandle view = makeView("layered");
    std::optional<an::ui::ImageDocument> document = makeDocument<std::uint16_t>(
        session(), 640u, 480u, Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<std::uint16_t>(100u * x + 37u * y + 9000u * c);
        });
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->scaleImageView(0.5, true, true);
    ASSERT_TRUE(settle(*view));
    const std::optional<an::ui::ViewPresentationSettings> settings = widget->framePresentationSettings();
    ASSERT_TRUE(settings.has_value());
    ASSERT_EQ(settings->proxyFactor, 2u);
    EXPECT_TRUE(widget->paintsDocumentAs(*settings));
    an::ui::ViewPresentationSettings reduced = *settings;
    reduced.proxyFactor = 4u;
    EXPECT_FALSE(widget->paintsDocumentAs(reduced)) << "the view paints its document at another reduction";

    // A part's presentation, covering 64 by 48 pixels of the document from (100, 60).
    std::optional<an::consumer::ImageAsset> part = makeAsset<std::uint16_t>(
        session(), 64u, 48u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 60000u; });
    ASSERT_TRUE(part.has_value());
    std::optional<an::ui::ViewPresentationOutcome> outcome;
    {
        std::optional<an::consumer::OperationScope> scope = admitted(session());
        ASSERT_TRUE(scope.has_value());
        an::Result<an::consumer::AssetReadLease> lease = part->read();
        ASSERT_TRUE(lease.hasValue());
        an::ui::ViewPresentationSettings own = *settings;
        own.proxyFactor = 1u;
        own.region.reset();
        an::Result<an::ui::ViewPresentationOutcome> presented = an::ui::presentForView(
            scope->engine(), scope->execution(), *lease, own, nullptr, &session().memory(), nullptr);
        ASSERT_TRUE(presented.hasValue()) << presented.status().message();
        outcome = std::move(*presented);
    }
    outcome->covered.x += 100u;
    outcome->covered.y += 60u;
    outcome->sourceWidth = 640u;
    outcome->sourceHeight = 480u;

    EXPECT_FALSE(widget->adoptPresentationLayer(reduced, *outcome)) << "a layer made for another reduction";
    EXPECT_EQ(widget->presentationLayer(), nullptr);
    const ImageViewPresentationTelemetry before = widget->presentationTelemetry();
    const std::shared_ptr<const an::ui::PresentedImage> painted = widget->presentedImage();
    ASSERT_TRUE(widget->adoptPresentationLayer(*settings, *outcome));
    EXPECT_EQ(widget->presentationLayer(), outcome->image);
    EXPECT_EQ(widget->presentationLayerRegion(), QRect(100, 60, 64, 48));
    EXPECT_EQ(widget->presentedImage(), painted) << "the presentation under the layer is as it was";
    EXPECT_EQ(widget->presentationTelemetry().layersAdopted - before.layersAdopted, 1u);
    EXPECT_EQ(widget->presentationTelemetry().requested, before.requested) << "the layer asked for no presentation";
    widget->repaint();

    // The layer is painted where its part lies at the view's half scale, over the presentation, and nowhere else.
    const QImage shot = widget->grab().toImage();
    const double toScreen = 0.5 * shot.devicePixelRatio();
    const auto shownAt = [&shot, toScreen](double x, double y) {
        return shot.pixelColor(static_cast<int>(x * toScreen), static_cast<int>(y * toScreen));
    };
    const QColor layered = shownAt(132.0, 84.0);
    EXPECT_EQ(shownAt(110.0, 70.0), layered) << "the part is painted in one colour";
    EXPECT_NE(shownAt(132.0, 130.0), layered) << "the layer is painted below its part";
    EXPECT_NE(shownAt(20.0, 20.0), layered) << "the layer is painted over the document's corner";

    // Another revision of the document is presented anew, and the layer goes with the presentation it was painted over;
    // while that presentation is on its way, no layer is painted over what it replaces.
    std::optional<an::consumer::ImageAsset> replacement = makeAsset<std::uint16_t>(
        session(), 640u, 480u, Channels::Rgb, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 2000u; });
    ASSERT_TRUE(replacement.has_value());
    view->replaceImage(std::move(*replacement));
    EXPECT_FALSE(widget->adoptPresentationLayer(*settings, *outcome)) << "a layer over a presentation on its way out";
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(widget->presentationLayer(), nullptr) << "the layer outlived the presentation it was painted over";
    EXPECT_TRUE(widget->presentationLayerRegion().isEmpty());
    EXPECT_TRUE(widget->paintsDocumentAs(*settings));
}

// A view that leaves autostretch and comes back presents through the analysis it measured: a linear presentation
// measures nothing, and its analysis never replaces the measured one kept under the same key, so the round trip
// measures the whole image no second time.
TEST_F(ImageViewControlTest, ALinearRoundTripKeepsTheMeasuredAnalysisAndMeasuresTheWholeImageNoSecondTime)
{
    open(4u);
    ViewHandle view = makeView("round trip");
    std::optional<an::ui::ImageDocument> document = cameraDocument(session(), Channels::Grey);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    view->resize(900, 700);
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->fitInView();
    widget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    ASSERT_EQ(widget->presentationTelemetry().wholeImageMeasured, 1u);

    for (int round = 0; round < 2; ++round)
    {
        widget->setViewMode(ImageViewMode::LINEAR_MODE);
        ASSERT_TRUE(settle(*view));
        widget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
        ASSERT_TRUE(settle(*view));
        EXPECT_EQ(widget->presentationTelemetry().wholeImageMeasured, 1u)
            << "round " << round << ": the whole image was measured again after a linear presentation";
    }
}

// The quick-zoom lens presents the image's own pixels under it, on the GUI thread, through the analysis the view
// painted with, so it measures nothing there. A grey image that replaced a colour one, its presentation not yet landed,
// leaves the view painted through an analysis of three channels, which the engine refuses for one: the lens then
// magnifies what the view painted, and the whole image is never measured on the GUI thread to stand in for it.
TEST_F(ImageViewControlTest, TheLensOverAPaintedAnalysisOfAnotherChannelCountMeasuresNothingOnTheGuiThread)
{
    open();
    ViewHandle view = makeView("lens analysis");
    const auto gradient = [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
        return 0.02f + 0.004f * static_cast<float>(x) + 0.002f * static_cast<float>(y) +
               0.05f * static_cast<float>(channel);
    };
    std::optional<an::ui::ImageDocument> colour = makeDocument<float>(session(), 96u, 64u, Channels::Rgb, gradient);
    ASSERT_TRUE(colour.has_value());
    view->setDocument(std::move(*colour));
    view->show();
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    widget->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    ASSERT_TRUE(settle(*view));
    ASSERT_TRUE(widget->presentedAnalysis().has_value() && widget->presentedAnalysis()->measured);

    const auto lensAt = [widget]() {
        const QPointF at(20.0, 20.0);
        QMouseEvent press(QEvent::MouseButtonPress, at, widget->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &press);
        const ImageViewPresentationTelemetry lensed = widget->presentationTelemetry();
        QMouseEvent release(QEvent::MouseButtonRelease, at, widget->mapToGlobal(at), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(widget, &release);
        return lensed;
    };

    const ImageViewPresentationTelemetry matching = widget->presentationTelemetry();
    const ImageViewPresentationTelemetry overMatching = lensAt();
    EXPECT_EQ(overMatching.lensesOfTheImage, matching.lensesOfTheImage + 1u)
        << "over the analysis it was painted with, the lens did not present the image's own pixels";
    EXPECT_EQ(overMatching.wholeImageMeasured, matching.wholeImageMeasured);

    std::optional<an::consumer::ImageAsset> grey = makeAsset<float>(session(), 96u, 64u, Channels::Grey, gradient);
    ASSERT_TRUE(grey.has_value());
    ASSERT_TRUE(view->document().tryReplace(std::move(*grey)).ok());
    const ImageViewPresentationTelemetry mismatched = widget->presentationTelemetry();
    const ImageViewPresentationTelemetry overMismatched = lensAt();
    EXPECT_EQ(overMismatched.wholeImageMeasured, mismatched.wholeImageMeasured)
        << "the lens measured the whole image on the GUI thread";
    EXPECT_EQ(overMismatched.lensesOfTheImage, mismatched.lensesOfTheImage)
        << "the lens presented the grey image through the colour image's analysis";
    EXPECT_EQ(overMismatched.pixmapsMade, mismatched.pixmapsMade + 1u) << "the lens magnified nothing";
    ASSERT_TRUE(settle(*view));
}

// A request made while no engine session is lent presents nothing and leaves nothing on its way: once a session is
// lent, the next request for the same presentation is made rather than taken for one already answered.
TEST_F(ImageViewControlTest, ARequestWithNoSessionLentLeavesNothingInFlightForTheNextOne)
{
    open();
    m_context->lendsSession = false;
    ViewHandle view = makeView("no session");
    std::optional<an::ui::ImageDocument> document = flatDocument(session(), 0.25f, 16u, 8u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    const ImageViewPresentationTelemetry refused = widget->presentationTelemetry();
    EXPECT_GE(refused.failed, 1u) << "a request with no session lent is refused";
    EXPECT_EQ(refused.committed, 0u);
    EXPECT_EQ(widget->presentedImage(), nullptr);

    m_context->lendsSession = true;
    widget->imageUpdated(true, true);
    ASSERT_TRUE(settle(*view));
    EXPECT_EQ(widget->presentationTelemetry().committed, 1u)
        << "the request was taken for one on its way, which the refused one never was";
    EXPECT_NE(widget->presentedImage(), nullptr);
}

// A change of the screen's device pixel ratio asks for a presentation at once: the presentation is made at the size the
// view paints in device pixels, so one made for another ratio is replaced rather than scaled until the next zoom or
// pan.
TEST_F(ImageViewControlTest, AChangeOfTheDevicePixelRatioAsksForAPresentation)
{
    open();
    ViewHandle view = makeView("ratio");
    std::optional<an::ui::ImageDocument> document = flatDocument(session(), 0.25f, 16u, 8u);
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    ImageViewWidget* widget = view->getViewWidget();
    const std::uint64_t requested = widget->presentationTelemetry().requested;

    QEvent change(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(widget, &change);
    EXPECT_EQ(widget->presentationTelemetry().requested, requested + 1u);
    ASSERT_TRUE(settle(*view));
}
