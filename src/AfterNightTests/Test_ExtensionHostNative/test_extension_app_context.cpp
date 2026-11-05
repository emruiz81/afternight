// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <gtest/gtest.h>

#include "ExtensionAppContext.h"
#include "HostNativeTestSupport.h"
#include "app/AfterNightAppListener.h"
#include "app/FilePolicies.h"
#include "controls/ImageViewControl.h"
#include "controls/settings/SettingsKeys.h"
#include "session/ImageDocument.h"

#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecRegistry.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>

#include <QTemporaryDir>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

class RecordingViewListener final : public AfterNightAppListener
{
public:
    explicit RecordingViewListener(an::ui::IAppEventBus* event_bus) : AfterNightAppListener(event_bus) {}

    void onViewControlCreated(const ImageViewControl* ctrl) override
    {
        created.push_back(ctrl);
    }

    void onViewControlDestroyed(const ImageViewControl* ctrl) override
    {
        destroyed.push_back(ctrl);
    }

    void onActiveViewControlChanged(ImageViewControl* ctrl) override
    {
        active.push_back(ctrl);
    }

    void onViewControlRenamed(const std::string& new_name, const std::string& old_name,
                              const ImageViewControl*) override
    {
        renamed.emplace_back(new_name, old_name);
    }

    std::vector<const ImageViewControl*> created;
    std::vector<const ImageViewControl*> destroyed;
    std::vector<ImageViewControl*> active;
    std::vector<std::pair<std::string, std::string>> renamed;
};

} // anonymous namespace

TEST(ExtensionAppContextTest, TracksLookupRenameAndActiveViewTransitions)
{
    an::ui::extensions::ExtensionAppContext context;
    QString openedSettingsPage;
    an::ui::extensions::ExtensionAppContext::DialogCallbacks callbacks;
    callbacks.open_settings_at = [&openedSettingsPage](const QString& pageName) { openedSettingsPage = pageName; };
    context.setDialogCallbacks(std::move(callbacks));
    RecordingViewListener listener(&context);

    context.openSettingsAt(QStringLiteral("Plate Solving"));
    EXPECT_EQ(openedSettingsPage, QStringLiteral("Plate Solving"));

    EXPECT_EQ(context.getPathMemoryService(), &context);
    EXPECT_EQ(context.getFileDialogService(), &context);
    EXPECT_EQ(context.getInterfaceSettingsRuntimeService(), &context);
    context.applyTheme(QStringLiteral("dark"));
    context.refreshAllZoomSettings();
    EXPECT_EQ(context.createNewImageViewControlFromSequence(nullptr, an::ui::ImageDocument{}, nullptr, false,
                                                            std::string(), ViewControlMode::st_preview, nullptr, false,
                                                            false),
              nullptr);
    EXPECT_TRUE(
        context.selectImageFiles(nullptr, QStringLiteral("Select Files"), QStringLiteral("__TEST__"), false).empty());
    EXPECT_TRUE(context
                    .selectFiles(nullptr, QStringLiteral("Select Profile"), QStringLiteral("__TEST__"),
                                 {QStringLiteral("Files (*.anspv1;*.anspv2)")})
                    .empty());
    EXPECT_TRUE(context
                    .saveFiles(nullptr, QStringLiteral("Save Profile"), QString(), QStringLiteral("__TEST__"),
                               {QStringLiteral("Files (*.anspv1;*.anspv2)")})
                    .empty());

    auto alpha = std::make_unique<ImageViewControl>();
    alpha->initialize("alpha", ViewControlMode::st_preview);
    auto beta = std::make_unique<ImageViewControl>();
    beta->initialize("beta", ViewControlMode::st_preview);

    context.addViewControl(alpha.get());
    EXPECT_EQ(context.trackedViewCount(), 1u);
    EXPECT_EQ(context.getViewControlByName("alpha"), alpha.get());
    EXPECT_EQ(context.getActiveImageViewControl(), alpha.get());
    ASSERT_EQ(listener.created.size(), 1u);
    EXPECT_EQ(listener.created.front(), alpha.get());
    ASSERT_EQ(listener.active.size(), 1u);
    EXPECT_EQ(listener.active.front(), alpha.get());

    context.addViewControl(beta.get());
    EXPECT_EQ(context.trackedViewCount(), 2u);
    EXPECT_EQ(context.getViewControlByName("beta"), beta.get());
    EXPECT_EQ(context.getActiveImageViewControl(), alpha.get());
    ASSERT_EQ(listener.created.size(), 2u);
    EXPECT_EQ(listener.created.back(), beta.get());
    EXPECT_EQ(listener.active.size(), 1u);
    EXPECT_EQ(context.createViewControlUniqueName("alpha"), "alpha_1");

    beta->setUniqueName("beta_renamed");
    context.renameViewControl(beta.get());
    ASSERT_EQ(listener.renamed.size(), 1u);
    EXPECT_EQ(listener.renamed.front().first, "beta_renamed");
    EXPECT_EQ(listener.renamed.front().second, "beta");
    EXPECT_EQ(context.getViewControlByName("beta"), nullptr);
    EXPECT_EQ(context.getViewControlByName("beta_renamed"), beta.get());

    context.setActiveImageViewControl(beta.get());
    EXPECT_EQ(context.getActiveImageViewControl(), beta.get());
    ASSERT_EQ(listener.active.size(), 2u);
    EXPECT_EQ(listener.active.back(), beta.get());

    context.removeViewControl(beta.get(), false);
    EXPECT_EQ(context.trackedViewCount(), 1u);
    EXPECT_EQ(context.getViewControlByName("beta_renamed"), nullptr);
    EXPECT_EQ(context.getActiveImageViewControl(), alpha.get());
    ASSERT_EQ(listener.destroyed.size(), 1u);
    EXPECT_EQ(listener.destroyed.front(), beta.get());
    ASSERT_EQ(listener.active.size(), 3u);
    EXPECT_EQ(listener.active.back(), alpha.get());

    context.removeViewControl(alpha.get(), false);
    EXPECT_EQ(context.trackedViewCount(), 0u);
    EXPECT_EQ(context.getViewControlByName("alpha"), nullptr);
    EXPECT_EQ(context.getActiveImageViewControl(), nullptr);
    ASSERT_EQ(listener.destroyed.size(), 2u);
    EXPECT_EQ(listener.destroyed.back(), alpha.get());
    ASSERT_EQ(listener.active.size(), 4u);
    EXPECT_EQ(listener.active.back(), nullptr);
}

TEST(ExtensionAppContextTest, TheViewFactoryTakesTheDocumentOnlyWhenItMakesAView)
{
    an::ui::extensions::ExtensionAppContext context;
    an::Result<an::ui::ImageDocument> made =
        an::ui::ImageDocument::create(an::tests::host_native::greyAsset(4, 3, 0.5));
    ASSERT_TRUE(made.hasValue()) << made.status().message();
    an::ui::ImageDocument document = std::move(*made);
    const std::uint64_t identity = document.identity();

    EXPECT_EQ(context.createNewImageViewControlFromImage(nullptr, std::move(document), nullptr, "refused",
                                                         ViewControlMode::st_preview, nullptr, false, false),
              nullptr)
        << "with no factory installed no view is made";
    ASSERT_TRUE(document.valid()) << "a document no view is made for stays with the caller";
    EXPECT_EQ(document.identity(), identity);
    EXPECT_TRUE(document.isOpen());

    std::unique_ptr<ImageViewControl> view;
    std::string history;
    context.setViewFactoryCallback([&](ImageViewControl*, an::ui::ImageDocument&& shown, QWidget*,
                                       const std::string& historyDesc, ViewControlMode mode, ImageViewControl*, bool,
                                       bool) -> ImageViewControl* {
        view = std::make_unique<ImageViewControl>();
        view->initialize("factory", mode);
        view->setDocument(std::move(shown));
        history = historyDesc;
        return view.get();
    });
    ImageViewControl* const shownIn = context.createNewImageViewControlFromImage(
        nullptr, std::move(document), nullptr, "made by the host", ViewControlMode::st_preview, nullptr, false, false);
    ASSERT_NE(shownIn, nullptr);
    EXPECT_FALSE(document.valid()) << "the view took the document";
    EXPECT_EQ(shownIn->document().identity(), identity);
    EXPECT_EQ(history, "made by the host");
}

TEST(ExtensionAppContextTest, TemporarySequencesAreNamedForTheirContainerInTheSessionDirectory)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    an::ui::extensions::ExtensionAppContext context;
    context.setSessionDirectories(root.filePath(QStringLiteral("temp")).toStdString());
    const QString temp = context.getTempDirPath();

    const QString ser = QString::fromStdString(context.createTempFilePathForNewSequence(an::SequenceFormat::Ser));
    const QString list =
        QString::fromStdString(context.createTempFilePathForNewSequence(an::SequenceFormat::ImageList));
    const QString video = QString::fromStdString(context.createTempFilePathForNewSequence(an::SequenceFormat::Video));
    EXPECT_TRUE(ser.startsWith(temp) && ser.endsWith(QStringLiteral(".ser"))) << ser.toStdString();
    EXPECT_TRUE(list.endsWith(QStringLiteral(".anseq"))) << list.toStdString();
    EXPECT_TRUE(video.endsWith(QStringLiteral(".mkv"))) << video.toStdString();
    EXPECT_NE(ser, QString::fromStdString(context.createTempFilePathForNewSequence(an::SequenceFormat::Ser)))
        << "every path is a new one";
    EXPECT_TRUE(context.isTempFile(ser.toStdString()));
}

TEST(ExtensionAppContextTest, OutputIntentsFollowTheExportSettingsTheHostWasGiven)
{
    an::ui::extensions::ExtensionAppContext context;
    ASSERT_EQ(context.getOutputIntentProvider(), &context);
    an::Result<an::consumer::SessionHold> hold = an::tests::host_native::suiteSession().hold();
    ASSERT_TRUE(hold.hasValue());
    const an::consumer::ImageAsset sample = an::tests::host_native::greyAsset(16, 8, 0.25);
    const std::optional<an::ImageDescriptor> source = sample.descriptor();
    ASSERT_TRUE(source.has_value());
    const an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::ui::createProductCodecRegistry();
    ASSERT_TRUE(registry.hasValue());

    context.upsert(QString::fromLatin1(SettingsKeys::EXPORT_JPEG_QUALITY), QStringLiteral("42"));
    an::ui::ExportSettings expectedSettings;
    expectedSettings.jpegQuality = 42u;
    for (const an::CodecFormat container : {an::CodecFormat::Fits, an::CodecFormat::Xisf, an::CodecFormat::Tiff,
                                            an::CodecFormat::Png, an::CodecFormat::Jpeg})
    {
        const an::Result<an::OutputIntent> built = context.buildOutputIntent(*source, container);
        const an::Result<an::OutputIntent> expected =
            an::ui::buildExportIntent(expectedSettings, **registry, *source, container);
        ASSERT_TRUE(built.hasValue()) << built.status().message();
        ASSERT_TRUE(expected.hasValue()) << expected.status().message();
        EXPECT_EQ(*built, *expected);
    }
    const an::Result<an::OutputIntent> jpeg = context.buildOutputIntent(*source, an::CodecFormat::Jpeg);
    ASSERT_TRUE(jpeg.hasValue());
    EXPECT_EQ(jpeg->compressionLevel(), std::optional<std::uint32_t>(42u)) << "the quality the host was given";
    EXPECT_EQ(jpeg->sampleType(), an::SampleType::UInt8);
}
