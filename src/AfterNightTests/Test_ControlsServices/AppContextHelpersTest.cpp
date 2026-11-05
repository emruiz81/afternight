// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The helpers processes reach the application context through. A view is made from a document on the GUI thread
/// whichever thread asks, and owns the document once it is made; with no context installed, or a factory that refuses,
/// nothing is made and the document stays with its caller. The engine session, the settings, the dialogs and the
/// views are reached through the installed context, and each answers its default without one.

#include "ControlsServicesTestSupport.h"

#include "app/AppContextHelpers.h"
#include "controls/IAppContext.h"
#include "session/IEngineSessionProvider.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/IO/SequenceDescriptor.h>

#include <QCoreApplication>
#include <QThread>

#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <thread>

namespace
{

using an::tests::services::filledDocument;
using an::tests::services::InstalledContext;
using an::tests::services::openContext;
using an::tests::services::pumpUntil;
using an::tests::services::TestAppContext;
using an::ui::DocumentSequence;
using an::ui::ImageDocument;

TEST(AppContextHelpersTest, AViewAskedForOnTheGuiThreadIsMadeThereAndOwnsTheDocument)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();

    ImageViewControl* view = process_utils::createImageViewOnUiThread(nullptr, std::move(*document), nullptr, "Made");

    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->thread(), QCoreApplication::instance()->thread());
    EXPECT_EQ(view->document().identity(), identity);
    ASSERT_EQ(context->requests.size(), 1u);
    EXPECT_EQ(context->requests.front().historyDescription, "Made");
    EXPECT_FALSE(document->valid());
}

TEST(AppContextHelpersTest, AViewAskedForFromAWorkerIsMadeOnTheGuiThreadWhileTheWorkerWaits)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    const std::uint64_t identity = document->identity();
    std::atomic_bool documentLeftWithWorker{true};

    std::future<ImageViewControl*> made = std::async(std::launch::async, [&]() {
        ImageViewControl* view =
            process_utils::createImageViewOnUiThread(nullptr, std::move(*document), nullptr, "From a worker");
        documentLeftWithWorker = document->valid();
        return view;
    });
    ASSERT_TRUE(pumpUntil([&]() { return made.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }))
        << "the worker never got its view";

    ImageViewControl* view = made.get();
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->thread(), QCoreApplication::instance()->thread()) << "the view was made on the worker";
    EXPECT_EQ(view->document().identity(), identity);
    EXPECT_FALSE(documentLeftWithWorker.load());
}

TEST(AppContextHelpersTest, NoViewIsMadeWithoutAContextAndTheDocumentStaysWithItsCaller)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    ASSERT_EQ(an::ui::appContext(), nullptr);

    EXPECT_EQ(process_utils::createImageViewOnUiThread(nullptr, std::move(*document)), nullptr);
    EXPECT_TRUE(document->valid());
    EXPECT_TRUE(context->views.empty());
}

TEST(AppContextHelpersTest, AViewTheFactoryRefusesLeavesTheDocumentWithItsCaller)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    context->refuseViews = true;
    const InstalledContext installed(*context);
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());

    EXPECT_EQ(process_utils::createImageViewOnUiThread(nullptr, std::move(*document)), nullptr);
    EXPECT_TRUE(document->valid());
}

TEST(AppContextHelpersTest, ASequenceViewIsMadeFromItsDocumentWithTheDemosaicChoice)
{
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    DocumentSequence sequence;
    sequence.sourcePath = QStringLiteral("frames.anseq");
    sequence.descriptor.format = an::SequenceFormat::ImageList;
    document->setSequence(sequence);

    std::future<ImageViewControl*> made = std::async(std::launch::async, [&]() {
        return process_utils::createSequenceViewOnUiThread(nullptr, std::move(*document), nullptr, true, "Frames");
    });
    ASSERT_TRUE(pumpUntil([&]() { return made.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }));

    ImageViewControl* view = made.get();
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->isSequence());
    EXPECT_TRUE(view->isSequenceAutodebayerEnabled());
    EXPECT_EQ(view->thread(), QCoreApplication::instance()->thread());
    ASSERT_TRUE(view->document().sequence().has_value());
    EXPECT_EQ(view->document().sequence()->sourcePath, QStringLiteral("frames.anseq"));
}

TEST(AppContextHelpersTest, TheEngineSessionIsReachedThroughTheInstalledContext)
{
    EXPECT_EQ(process_utils::currentEngineSessionProvider(), nullptr);
    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    an::ui::IEngineSessionProvider* provider = process_utils::currentEngineSessionProvider();
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(&provider->engineSession(), &context->engineSession());
}

TEST(AppContextHelpersTest, SettingsDialogsAndViewsAreReachedThroughTheInstalledContext)
{
    EXPECT_EQ(process_utils::selectGlobalSetting(QStringLiteral("theme"), QStringLiteral("dark")),
              QStringLiteral("dark"));
    EXPECT_EQ(process_utils::currentActiveViewControl(), nullptr);
    EXPECT_TRUE(process_utils::listViewControls().empty());
    EXPECT_EQ(process_utils::currentGuiThreadTarget(), QCoreApplication::instance());

    std::unique_ptr<TestAppContext> context = openContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    ASSERT_TRUE(context->getGlobalSettings()->upsert(QStringLiteral("theme"), QStringLiteral("light")));
    EXPECT_EQ(process_utils::selectGlobalSetting(QStringLiteral("theme"), QStringLiteral("dark")),
              QStringLiteral("light"));

    process_utils::notifyErrorMessage(nullptr, QStringLiteral("Save failed"), QStringLiteral("disk full"));
    int answer = -1;
    process_utils::messageBoxYesNo(nullptr, QStringLiteral("Discard?"), QString(), false, &answer);
    EXPECT_EQ(context->messages,
              (QStringList{QStringLiteral("error:Save failed"), QStringLiteral("question:Discard?")}));
    EXPECT_EQ(answer, 0);

    std::optional<ImageDocument> document = filledDocument(context->engineSession(), 1.0f);
    ASSERT_TRUE(document.has_value());
    ImageViewControl* view = process_utils::createImageViewOnUiThread(nullptr, std::move(*document));
    ASSERT_NE(view, nullptr);
    context->setActiveImageViewControl(view);
    EXPECT_EQ(process_utils::currentActiveViewControl(), view);
    ASSERT_EQ(process_utils::listViewControls().size(), 1u);
    EXPECT_EQ(process_utils::listViewControls()[0], view);
}

} // namespace
