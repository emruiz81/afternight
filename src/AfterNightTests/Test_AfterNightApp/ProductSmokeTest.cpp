// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// The restored product's smoke on its own main window: every format the product reads opened through the file menu, an
// image saved in every container the product writes and opened again with the samples it was saved with, and a
// process of every framework family the product's menus launch - a typed process applied, a dialog, a non-interactive
// action, an own-thread window and the top-level integration windows, the Lite one among them - each over a view, as
// a person runs them.

#include "ApplicationDriving.h"
#include "app/FileCompanions.h"
#include "controls/ImageListTableHelpers.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/NewSequenceControl.h"
#include "controls/TopLevelProcessBaseControl.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QPushButton>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using an::tests::app::Answered;
using an::tests::app::answerEveryMessageBox;
using an::tests::app::chooseInTheNextFileDialog;
using an::tests::app::isA;
using an::tests::app::isNamed;
using an::tests::app::processEventsUntil;
using an::tests::app::visibleWidget;

namespace
{

// The sample at a position of an image as a fraction of its representation's full scale, or NaN for a representation
// the product's writers do not make.
double fractionAt(const an::consumer::AnyImage& image, std::uint32_t x, std::uint32_t y, std::uint32_t channel = 0u)
{
    const auto read = [x, y, channel](const auto& typed, double scale) {
        if (!typed.view().has_value())
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const auto row = typed.view()->row(channel, y);
        return row.has_value() && x < row->size() ? static_cast<double>((*row)[x]) / scale
                                                  : std::numeric_limits<double>::quiet_NaN();
    };
    if (const auto* typed = std::get_if<an::Image<float>>(&image))
    {
        return read(*typed, 1.0);
    }
    if (const auto* typed = std::get_if<an::Image<double>>(&image))
    {
        return read(*typed, 1.0);
    }
    if (const auto* typed = std::get_if<an::Image<std::uint16_t>>(&image))
    {
        return read(*typed, 65535.0);
    }
    if (const auto* typed = std::get_if<an::Image<std::uint8_t>>(&image))
    {
        return read(*typed, 255.0);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

// The sample at a position of what a view shows, as a fraction of full scale, or NaN.
double viewFractionAt(const ImageViewControl& view, std::uint32_t x, std::uint32_t y)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = view.document().asset();
    if (!asset)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::optional<an::consumer::AssetReadLease> lease = an::tests::app::readOnTheGuiThread(*asset);
    return lease.has_value() ? fractionAt(lease->image(), x, y) : std::numeric_limits<double>::quiet_NaN();
}

// A grey Float32 image of a session whose samples rise from a quarter across the columns and down the rows, smooth
// enough that a lossy container keeps it within a few codes.
std::optional<an::consumer::ImageAsset> gradientAsset(const an::consumer::EngineSession& session, std::uint32_t width,
                                                      std::uint32_t height)
{
    an::Result<an::consumer::SessionHold> hold = session.hold();
    if (!hold.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageDescriptor> descriptor =
        an::tests::app::monoFloatDescriptor(hold->engine(), width, height);
    if (!descriptor.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::Image<float>> image = an::Image<float>::create(
        hold->engine(), *descriptor, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
    if (!image.hasValue())
    {
        return std::nullopt;
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = image->write();
        if (!access.has_value())
        {
            return std::nullopt;
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const auto row = access->view().row(0u, y);
            if (!row.has_value())
            {
                return std::nullopt;
            }
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                (*row)[x] = 0.25f + 0.5f *
                                        (static_cast<float>(x) / static_cast<float>(width - 1u) +
                                         static_cast<float>(y) / static_cast<float>(height - 1u)) /
                                        2.0f;
            }
        }
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, an::consumer::AnyImage{std::move(*image)});
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*asset);
}

// The process window of a class the menus opened, shown now, or null.
TopLevelProcessBaseControl* shownProcessWindow(const char* className)
{
    return dynamic_cast<TopLevelProcessBaseControl*>(visibleWidget([className](QWidget* widget) {
        return dynamic_cast<TopLevelProcessBaseControl*>(widget) != nullptr && isNamed(widget, className);
    }));
}

} // namespace

class ProductSmoke : public an::tests::app::RealWindowCase
{
protected:
    // Saves the active view through the file menu's Save As to a file of the container its extension names.
    void saveThroughTheFileMenu(const QString& path)
    {
        Answered dialog;
        chooseInTheNextFileDialog({path}, &dialog);
        invoke("actionSave_as");
        EXPECT_TRUE(dialog.seen);
        EXPECT_FALSE(dialog.refused) << dialog.refusal.toStdString();
        EXPECT_TRUE(processEventsUntil([&]() { return QFileInfo::exists(path); }, 20000))
            << path.toStdString() << " was not written";
    }

    // Two grey frames on disk, and the SER sequence, the image list and the AVI made of them.
    struct Sequences
    {
        QString ser;
        QString imageList;
        QString avi;
    };

    Sequences writeSequences()
    {
        Sequences made;
        std::vector<ImageListProcessHelpers::ImageListEntry> entries;
        for (int frame = 0; frame < 2; ++frame)
        {
            const QString path = filePath(QStringLiteral("frame-%1.fits").arg(frame));
            writeFile(path, an::CodecFormat::Fits, 0.25f + 0.25f * static_cast<float>(frame));
            std::optional<an::consumer::ImageAsset> asset = an::tests::app::filledAsset(session(), 0.25f);
            EXPECT_TRUE(asset.has_value());
            if (!asset.has_value())
            {
                return made;
            }
            an::Result<an::CodecMetadata> codec =
                an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{});
            EXPECT_TRUE(codec.hasValue());
            entries.push_back(ImageListProcessHelpers::ImageListEntry{
                .path = path,
                .descriptor = *asset->descriptor(),
                .metadata =
                    an::ImageMetadata::create(an::AcquisitionMetadata{}, an::ImageSemantics{},
                                              an::AstrometricSolution{}, an::Provenance{}, *codec, std::nullopt)});
        }
        made.ser = filePath(QStringLiteral("frames.ser"));
        made.imageList = filePath(QStringLiteral("frames.anseq"));
        made.avi = filePath(QStringLiteral("frames.avi"));
        for (const auto& [kind, path] : {std::pair{an::ui::NewSequenceKind::Ser, made.ser},
                                         std::pair{an::ui::NewSequenceKind::ImageList, made.imageList}})
        {
            const an::Status written = an::ui::makeNewSequence(
                session(), an::ui::NewSequenceRequest{.files = entries, .kind = kind, .path = path}, {});
            EXPECT_TRUE(written.ok()) << written.message();
        }
        an::Result<an::ui::ImageDocument> ser =
            an::ui::openSequenceDocument(session(), made.ser, an::SequenceFormat::Ser, {});
        EXPECT_TRUE(ser.hasValue()) << ser.status().message();
        if (ser.hasValue() && ser->sequence().has_value())
        {
            const an::Status written = an::ui::writeSequenceFile(session(), *ser->sequence(), made.avi, {});
            EXPECT_TRUE(written.ok()) << written.message();
        }
        return made;
    }
};

// ---- Every format the product reads, opened through the file menu.

TEST_F(ProductSmoke, TheFileMenuOpensEveryFormatTheProductReads)
{
    // A mosaic is offered for demosaicing; a person accepts.
    answerEveryMessageBox(std::make_shared<Answered>());

    struct Still
    {
        const char* name;
        an::CodecFormat container;
    };
    for (const Still& still : {Still{"still.fits", an::CodecFormat::Fits}, Still{"still.xisf", an::CodecFormat::Xisf},
                               Still{"still.tiff", an::CodecFormat::Tiff}, Still{"still.png", an::CodecFormat::Png},
                               Still{"still.jpg", an::CodecFormat::Jpeg}})
    {
        SCOPED_TRACE(still.name);
        const QString path = filePath(QString::fromLatin1(still.name));
        writeFile(path, still.container, 0.25f);
        ImageViewControl* const view = openThroughTheFileMenu(path);
        ASSERT_NE(view, nullptr);
        EXPECT_FALSE(view->isSequence());
        const std::optional<an::ImageDescriptor> descriptor = view->document().asset()->descriptor();
        ASSERT_TRUE(descriptor.has_value());
        EXPECT_EQ(descriptor->extent().width(), 8u);
        EXPECT_EQ(descriptor->extent().height(), 4u);
        EXPECT_NEAR(viewFractionAt(*view, 0u, 0u), 0.25, 1.0 / 255.0);
    }

    {
        SCOPED_TRACE("camera.dng");
        const QString path = writeDng();
        ImageViewControl* const view = openThroughTheFileMenu(path);
        ASSERT_NE(view, nullptr);
        EXPECT_FALSE(view->isSequence());
    }

    const Sequences sequences = writeSequences();
    for (const QString& path : {sequences.ser, sequences.imageList, sequences.avi})
    {
        SCOPED_TRACE(path.toStdString());
        ASSERT_TRUE(QFileInfo::exists(path));
        ImageViewControl* const view = openThroughTheFileMenu(path);
        ASSERT_NE(view, nullptr);
        EXPECT_TRUE(view->isSequence());
        ASSERT_TRUE(view->document().sequence().has_value());
    }
}

// ---- An image saved in every container the product writes opens again with its samples.

TEST_F(ProductSmoke, AnImageSavedInEveryContainerTheProductWritesOpensAgainWithItsSamples)
{
    answerEveryMessageBox(std::make_shared<Answered>());

    struct Container
    {
        const char* name;
        double tolerance;
    };
    // The floating containers keep every sample; PNG keeps them to the code its export writes, JPEG within a few codes.
    for (const Container& container :
         {Container{"saved.fits", 1e-6}, Container{"saved.xisf", 1e-6}, Container{"saved.tiff", 1e-6},
          Container{"saved.png", 1.0 / 255.0}, Container{"saved.jpg", 6.0 / 255.0}})
    {
        SCOPED_TRACE(container.name);
        std::optional<an::consumer::ImageAsset> asset = gradientAsset(session(), 16u, 8u);
        ASSERT_TRUE(asset.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
            std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("gradient"), .sourcePath = {}});
        ASSERT_TRUE(document.hasValue());
        ImageViewControl* const shown = context().getViewFactory()->createNewImageViewControlFromImage(
            nullptr, std::move(*document), nullptr, "gradient");
        ASSERT_NE(shown, nullptr);
        s_window->setActiveImageViewControl(shown);

        const QString path = filePath(QString::fromLatin1(container.name));
        saveThroughTheFileMenu(path);
        const std::vector<std::pair<std::uint32_t, std::uint32_t>> points{{0u, 0u}, {15u, 0u}, {7u, 4u}, {15u, 7u}};
        std::vector<double> saved;
        for (const auto& [x, y] : points)
        {
            saved.push_back(viewFractionAt(*shown, x, y));
        }
        // The saved view now shows the file it was saved to; it is closed, and the file opened again in a view of its
        // own.
        const QPointer<ImageViewControl> watched(shown);
        invoke("actionClose");
        ASSERT_TRUE(processEventsUntil([&]() {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return watched.isNull();
        }));
        ImageViewControl* const reopened = openThroughTheFileMenu(path);
        ASSERT_NE(reopened, nullptr);
        for (std::size_t point = 0u; point < points.size(); ++point)
        {
            EXPECT_NEAR(viewFractionAt(*reopened, points[point].first, points[point].second), saved[point],
                        container.tolerance)
                << "at " << points[point].first << "," << points[point].second;
        }
    }
}

// ---- A process of every framework family, launched from the menus over a view.

TEST_F(ProductSmoke, ATypedProcessLaunchedFromTheMenuAppliesToTheActiveView)
{
    ImageViewControl* const view = makeView(QStringLiteral("target"), 0.25f);
    ASSERT_NE(view, nullptr);
    const std::shared_ptr<const an::consumer::ImageAsset> before = view->document().asset();

    invoke("actionAdjustBrightness_Contrast");

    TopLevelProcessBaseControl* window = nullptr;
    ASSERT_TRUE(processEventsUntil([&]() {
        window = shownProcessWindow("AdjustBrightnessAndContrastProcess");
        return window != nullptr;
    }));
    auto* const typed = dynamic_cast<ImageRTPreviewControl*>(window);
    ASSERT_NE(typed, nullptr);
    typed->applyProcess();

    // The Apply is an edit of the view: it shows a new image, recorded in the history before it is shown.
    EXPECT_TRUE(processEventsUntil([&]() { return view->document().asset() != before; }, 20000));
}

TEST_F(ProductSmoke, AnOwnThreadWindowLaunchesFromTheMenuOverTheActiveView)
{
    ImageViewControl* const view = makeView(QStringLiteral("target"));
    ASSERT_NE(view, nullptr);

    invoke("actionCrop");

    EXPECT_TRUE(processEventsUntil([]() { return shownProcessWindow("CropProcess") != nullptr; }));
}

TEST_F(ProductSmoke, TheMaskSelectorDialogLaunchesFromTheMenuOverTheActiveView)
{
    ImageViewControl* const view = makeView(QStringLiteral("target"));
    ASSERT_NE(view, nullptr);

    invoke("actionSelect_Mask");

    EXPECT_TRUE(processEventsUntil([]() {
        return visibleWidget([](QWidget* widget) { return isNamed(widget, "ImageMaskSelectorControl"); }) != nullptr;
    }));
}

TEST_F(ProductSmoke, TheNonInteractiveRemoveMaskActionTakesTheActiveViewsMaskAway)
{
    ImageViewControl* const view = makeView(QStringLiteral("target"));
    ImageViewControl* const mask = makeView(QStringLiteral("mask"), 0.5f);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(mask, nullptr);
    view->setCurrentMask(mask);
    s_window->setActiveImageViewControl(view);
    ASSERT_TRUE(view->isMasked());

    invoke("actionRemove_Mask");

    EXPECT_TRUE(processEventsUntil([&]() { return !view->isMasked(); }));
}

TEST_F(ProductSmoke, TheTopLevelIntegrationWindowsLaunchFromTheMenuTheLiteOneAmongThem)
{
    for (const auto& [action, className] : {std::pair{"actionImage_Integration", "ImageIntegrationProcess"},
                                            std::pair{"actionImage_Integration_Lite", "ImageIntegrationLiteProcess"},
                                            std::pair{"actionImage_Calibration", "ImageCalibrationProcess"}})
    {
        SCOPED_TRACE(action);
        invoke(action);
        TopLevelProcessBaseControl* window = nullptr;
        ASSERT_TRUE(processEventsUntil([&]() {
            window = shownProcessWindow(className);
            return window != nullptr;
        }));
        window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
}
