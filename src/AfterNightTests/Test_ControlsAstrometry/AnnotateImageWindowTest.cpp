// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Annotate Image window over a view, opened as the view's toolbar opens it: the preview it lays out from the
/// catalogues' entries on a worker, laid out again for a changed level of detail, the picture it exports to a file of
/// the size chosen and refuses to write transparent into a JPEG file, the picture it opens as an image of its own, the
/// image with no solution it says it cannot annotate, and the choices it keeps for the next window.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/FilePolicies.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/annotation/AnnotateImageControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"

#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::cardValue;
using an::tests::astrometry::documentCards;
using an::tests::astrometry::fixedCatalogs;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::makeCardDocument;
using an::tests::astrometry::makeView;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::pumpUntil;
using an::tests::astrometry::settle;
using an::tests::astrometry::tangentPlaneCards;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::ViewHandle;

/// A field of 0.01 degrees a pixel about (120, 10) on a 400 by 300 frame, its target named.
[[nodiscard]] std::vector<an::CodecCard> fieldCards()
{
    std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(200.5, 150.5, 120.0, 10.0, 0.01));
    addCard(cards, "RADESYS", "'ICRS'");
    addCard(cards, "OBJECT", "'Test Field'");
    addCard(cards, "FOCALLEN", "432");
    return cards;
}

/// A deep-sky catalogue of a galaxy and an open cluster in the field and a star-like entry only the complete level
/// marks, and a bright-star catalogue of one named star.
[[nodiscard]] QByteArray deepSkyText()
{
    return QByteArray(
        "id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,object_type,magnitude\n"
        "NGC 1001,Bright One,120.5,10.6,40.0,24.0,10,Galaxy,8.0\n"
        "NGC 1002,,119.5,9.5,20.0,20.0,0,Open Cluster,9.5\n"
        "NGC 1005,,120.2,9.4,0.0,0.0,0,Star,12.0\n");
}

[[nodiscard]] QByteArray brightStarText()
{
    return QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                      "HIP 1,Alpha Field,alf Fld,119.4,10.3,1.5,A0V\n");
}

/// The application context of the cases, whose view factory opens the pictures it is handed in views it keeps.
class PictureViewsContext final : public ViewerTestContext, public an::ui::IViewFactory
{
public:
    using ViewerTestContext::ViewerTestContext;

    [[nodiscard]] an::ui::IViewFactory* getViewFactory() override
    {
        return this;
    }

    ImageViewControl* createNewImageViewControlFromImage(ImageViewControl* /*customViewControl*/,
                                                         an::ui::ImageDocument&& document, QWidget* /*parent*/,
                                                         const std::string& /*historyDesc*/, ViewControlMode /*mode*/,
                                                         ImageViewControl* /*neighbour*/, bool /*keepFocusedNeighbour*/,
                                                         bool /*pendingChanges*/) override
    {
        opened.push_back(makeView("picture-" + std::to_string(opened.size())));
        opened.back()->setDocument(std::move(document));
        return opened.back().get();
    }

    ImageViewControl* createNewImageViewControlFromSequence(ImageViewControl* /*customViewControl*/,
                                                            an::ui::ImageDocument&& /*document*/, QWidget* /*parent*/,
                                                            bool /*autoDebayer*/, const std::string& /*historyDesc*/,
                                                            ViewControlMode /*mode*/, ImageViewControl* /*neighbour*/,
                                                            bool /*keepFocusedNeighbour*/,
                                                            bool /*pendingChanges*/) override
    {
        return nullptr;
    }

    /// The views the factory opened, in order.
    std::vector<ViewHandle> opened;
};

class AnnotateImageWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        std::optional<an::tests::controls::TestSessionProvider> provider = an::tests::viewer::openFileProvider(2u);
        ASSERT_TRUE(provider.has_value());
        m_context = std::make_unique<PictureViewsContext>(std::move(provider->owned()));
        m_installed.emplace(*m_context);
        m_view = makeView("annotate");
    }

    void TearDown() override
    {
        if (m_window)
        {
            EXPECT_TRUE(pumpUntil([this]() { return !m_window || !m_window->isWorking(); }));
            m_window->close();
        }
        m_context->opened.clear();
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    void show(std::vector<an::CodecCard> cards)
    {
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(m_context->engineSession(), 400u, 300u, std::move(cards));
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
        m_view->initializeNewImageHistory("Image opened");
        ASSERT_TRUE(settle(*m_view));
    }

    /// Opens the window as the view's toolbar does, reading the cases' catalogues, and waits for its preview.
    an::ui::AnnotateImageControl& open()
    {
        QWidget* opened = an::ui::openViewTool(an::ui::ViewTool::AnnotateImage, *m_view);
        m_window = dynamic_cast<an::ui::AnnotateImageControl*>(opened);
        EXPECT_NE(m_window, nullptr);
        m_window->setCatalogSource(fixedCatalogs(deepSkyText(), brightStarText()));
        return *m_window;
    }

    [[nodiscard]] bool settled() const
    {
        return pumpUntil([this]() { return m_window && !m_window->isWorking() && m_window->scene() != nullptr; });
    }

    [[nodiscard]] std::vector<QString> designations() const
    {
        std::vector<QString> found;
        for (const an::ui::AnnotationMark& mark : m_window->scene()->marks)
        {
            found.push_back(mark.designation.isEmpty() ? mark.name : mark.designation);
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    QTemporaryDir m_directory;
    std::unique_ptr<PictureViewsContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
    QPointer<an::ui::AnnotateImageControl> m_window;
};

TEST_F(AnnotateImageWindowTest, TheWindowPreviewsTheAnnotationsOfTheSolvedImageAndIsOnePerView)
{
    show(fieldCards());
    ASSERT_TRUE(m_view->canAnnotateCurrentImage());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());

    EXPECT_EQ(designations(), (std::vector<QString>{QStringLiteral("NGC 1001"), QStringLiteral("NGC 1002"),
                                                    QStringLiteral("alf Fld")}));
    ASSERT_NE(window.layout(), nullptr);
    EXPECT_EQ(window.scene()->panel.title, QStringLiteral("Test Field"));
    auto* summary = window.findChild<QLabel*>(QStringLiteral("annotateSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("3 objects"))) << summary->text().toStdString();
    auto* exportButton = window.findChild<QPushButton*>(QStringLiteral("annotateExport"));
    ASSERT_NE(exportButton, nullptr);
    EXPECT_TRUE(exportButton->isEnabled());

    // Asking again raises the window already open.
    EXPECT_EQ(an::ui::openViewTool(an::ui::ViewTool::AnnotateImage, *m_view), &window);
}

TEST_F(AnnotateImageWindowTest, AnotherLevelOfDetailLaysThePreviewOutAgain)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());
    const std::shared_ptr<const an::ui::AnnotationScene> before = window.scene();

    auto* complete = window.findChild<QPushButton*>(QStringLiteral("annotateDetailComplete"));
    ASSERT_NE(complete, nullptr);
    complete->click();
    ASSERT_TRUE(pumpUntil([&]() { return !window.isWorking() && window.scene() != before; }));

    EXPECT_EQ(an::ui::annotationDetailOf(window.settings().content), an::ui::AnnotationDetail::Complete);
    EXPECT_EQ(designations(), (std::vector<QString>{QStringLiteral("NGC 1001"), QStringLiteral("NGC 1002"),
                                                    QStringLiteral("NGC 1005"), QStringLiteral("alf Fld")}));
}

TEST_F(AnnotateImageWindowTest, ThePictureIsExportedAtTheSizeChosenAndATransparentOneIsNotWrittenAsJpeg)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());
    an::ui::AnnotationSettings settings = window.settings();
    settings.output.longEdge = 200u;
    window.setSettings(settings);
    ASSERT_TRUE(settled());

    const QString path = m_directory.filePath(QStringLiteral("annotated.tif"));
    window.exportTo(path);
    ASSERT_TRUE(pumpUntil([&]() { return window.picturesMade() == 1u || window.lastOutcome().contains("could not"); }))
        << window.lastOutcome().toStdString();
    ASSERT_EQ(window.picturesMade(), 1u) << window.lastOutcome().toStdString();
    EXPECT_TRUE(window.lastOutcome().startsWith(QStringLiteral("Saved"))) << window.lastOutcome().toStdString();
    // Read back as the application reads a file.
    an::Result<std::shared_ptr<const an::CodecRegistry>> registry = an::ui::createProductCodecRegistry();
    ASSERT_TRUE(registry.hasValue());
    an::ui::ViewReadRequest request;
    request.path = path;
    request.registry = registry->get();
    an::Result<an::ui::ViewReadResult> read = an::ui::readViewImage(m_context->engineSession(), request, {}, nullptr);
    ASSERT_TRUE(read.hasValue()) << read.status().message();
    an::Result<an::consumer::AssetReadLease> lease = read->asset.read({});
    ASSERT_TRUE(lease.hasValue());
    lease->visit([](const auto& typed) {
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        ASSERT_TRUE(described.has_value());
        EXPECT_EQ(described->extent().width(), 200u);
        EXPECT_EQ(described->extent().height(), 150u);
        EXPECT_EQ(described->extent().channelCount(), 3u);
    });

    settings.output.background = an::ui::AnnotationBackground::Transparent;
    window.setSettings(settings);
    const QString jpeg = m_directory.filePath(QStringLiteral("annotated.jpg"));
    window.exportTo(jpeg);
    EXPECT_TRUE(window.lastOutcome().contains(QStringLiteral("JPEG"))) << window.lastOutcome().toStdString();
    EXPECT_FALSE(QFileInfo::exists(jpeg));
    EXPECT_EQ(window.picturesMade(), 1u);
}

TEST_F(AnnotateImageWindowTest, ThePictureOpensAsAnImageThatKeepsTheFramesFactsAndItsSolution)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());

    window.openAsImage();
    ASSERT_TRUE(pumpUntil([&]() { return window.picturesMade() == 1u || window.lastOutcome().contains("could"); }))
        << window.lastOutcome().toStdString();
    ASSERT_EQ(m_context->opened.size(), 1u) << window.lastOutcome().toStdString();
    ImageViewControl& picture = *m_context->opened.front();
    ASSERT_TRUE(settle(picture));
    const std::vector<an::CodecCard> cards = documentCards(picture);
    EXPECT_EQ(cardValue(cards, "OBJECT"), std::optional<std::string>("'Test Field'"));
    EXPECT_EQ(cardValue(cards, "FOCALLEN"), std::optional<std::string>("432"));
    // The picture is the image's size and states the image's solution, so it can be annotated again.
    EXPECT_TRUE(picture.canAnnotateCurrentImage());
    ASSERT_TRUE(picture.imageSolution().has_value());
    EXPECT_NEAR(picture.imageSolution()->meanPixelScaleArcsecPerPixel(), 36.0, 1.0e-6);
}

TEST_F(AnnotateImageWindowTest, AnImageWithoutASolutionIsSaidToNeedOneAndOffersNoPicture)
{
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJECT", "'Unsolved'");
    show(cards);
    EXPECT_FALSE(m_view->canAnnotateCurrentImage());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(pumpUntil([&]() { return !window.isWorking(); }));

    EXPECT_EQ(window.scene(), nullptr);
    auto* exportButton = window.findChild<QPushButton*>(QStringLiteral("annotateExport"));
    ASSERT_NE(exportButton, nullptr);
    EXPECT_FALSE(exportButton->isEnabled());
    window.openAsImage();
    EXPECT_TRUE(window.lastOutcome().contains(QStringLiteral("solution"))) << window.lastOutcome().toStdString();
    EXPECT_TRUE(m_context->opened.empty());
}

TEST_F(AnnotateImageWindowTest, TheChoicesButTheTitleAreKeptForTheNextWindow)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());
    an::ui::AnnotationSettings settings = window.settings();
    settings.content.credit = QStringLiteral("E. Ruiz");
    settings.content.title = QStringLiteral("For this image only");
    settings.style.colors = an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Ember);
    settings.style.textSize = 1.5;
    window.setSettings(settings);
    ASSERT_TRUE(settled());
    window.close();
    ASSERT_TRUE(pumpUntil([this]() { return m_window.isNull(); }));

    an::ui::AnnotateImageControl& again = open();
    ASSERT_TRUE(settled());
    EXPECT_EQ(again.settings().content.credit, QStringLiteral("E. Ruiz"));
    EXPECT_TRUE(again.settings().content.title.isEmpty());
    EXPECT_EQ(an::ui::annotationPaletteOf(again.settings().style.colors), an::ui::AnnotationPalette::Ember);
    EXPECT_DOUBLE_EQ(again.settings().style.textSize, 1.5);
}

} // namespace
