// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Annotate Image window over a view, opened as the view's toolbar opens it: the preview it lays out from the
/// catalogues' entries on a worker, laid out again for a changed level of detail, the picture it exports to a file of
/// the size chosen and refuses to write transparent into a JPEG file, the picture it opens as an image of its own, the
/// image with no solution it says it cannot annotate, and the choices it keeps for the next window.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/FilePolicies.h"
#include "components/AnCollapsibleSection.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/ImageViewFiles.h"
#include "controls/annotation/AnnotateImageControl.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>

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
/// marks, and a bright-star catalogue of a named star and a fainter one of no name or designation.
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
                      "HIP 1,Alpha Field,alf Fld,119.4,10.3,1.5,A0V\n"
                      "HIP 2,,,120.1,10.1,5.8,G2V\n");
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
    EXPECT_EQ(designations(),
              (std::vector<QString>{QStringLiteral("HIP 2"), QStringLiteral("NGC 1001"), QStringLiteral("NGC 1002"),
                                    QStringLiteral("NGC 1005"), QStringLiteral("alf Fld")}))
        << "the complete level names every star, by its catalogue number when it has no name";
}

// The custom palette is the person's own colours: chosen before any is set it starts from the palette shown and opens
// the colours; a colour set makes the palette custom; another palette and then the custom one bring them back.
TEST_F(AnnotateImageWindowTest, TheCustomPaletteKeepsThePersonsOwnColours)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());
    auto* palette = window.findChild<QComboBox*>(QStringLiteral("annotatePalette"));
    ASSERT_NE(palette, nullptr);
    const auto choose = [palette](const an::ui::AnnotationPalette chosen) {
        palette->setCurrentIndex(palette->findData(static_cast<int>(chosen)));
    };
    const auto shown = [palette]() { return static_cast<an::ui::AnnotationPalette>(palette->currentData().toInt()); };
    auto* colours = dynamic_cast<an::ui::AnCollapsibleSection*>(
        window.findChild<QWidget*>(QStringLiteral("annotateColorsSection")));
    ASSERT_NE(colours, nullptr);
    EXPECT_FALSE(colours->isExpanded());

    choose(an::ui::AnnotationPalette::Glacier);
    choose(an::ui::AnnotationPalette::Custom);
    EXPECT_EQ(shown(), an::ui::AnnotationPalette::Custom) << "the custom palette stays chosen";
    EXPECT_EQ(window.settings().style.colors, an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Glacier))
        << "the first time, it starts from the palette shown";
    EXPECT_TRUE(colours->isExpanded()) << "and opens the colours to be set";

    // A colour set through its button, as a person sets it.
    auto* galaxies = window.findChild<QToolButton*>(QStringLiteral("annotateColor1"));
    ASSERT_NE(galaxies, nullptr);
    galaxies->click();
    auto* dialog = window.findChild<QColorDialog*>();
    ASSERT_NE(dialog, nullptr);
    dialog->setCurrentColor(QColor(10, 200, 30));
    dialog->accept();
    EXPECT_EQ(window.settings().style.colors.galaxy, QColor(10, 200, 30));
    EXPECT_EQ(shown(), an::ui::AnnotationPalette::Custom);

    choose(an::ui::AnnotationPalette::Amber);
    EXPECT_EQ(window.settings().style.colors, an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Amber));
    choose(an::ui::AnnotationPalette::Custom);
    EXPECT_EQ(window.settings().style.colors.galaxy, QColor(10, 200, 30)) << "the person's own colours come back";
    EXPECT_EQ(window.settings().style.colors.nebula,
              an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Glacier).nebula);
}

// The style's controls set the text and the lines apart, each choice its own: a palette, the fill and its opacity, the
// letters' weight and slant, the dark edge behind the lines whatever the labels' backing, and the stars' naming.
TEST_F(AnnotateImageWindowTest, TheStyleControlsSetTheTextAndTheLinesApart)
{
    show(fieldCards());
    an::ui::AnnotateImageControl& window = open();
    ASSERT_TRUE(settled());
    const auto choose = [&window](const char* name, const int value) {
        auto* combo = window.findChild<QComboBox*>(QString::fromLatin1(name));
        ASSERT_NE(combo, nullptr) << name;
        const int index = combo->findData(value);
        ASSERT_GE(index, 0) << name;
        combo->setCurrentIndex(index);
    };
    const auto box = [&window](const char* name) { return window.findChild<QCheckBox*>(QString::fromLatin1(name)); };

    auto* palette = window.findChild<QComboBox*>(QStringLiteral("annotatePalette"));
    ASSERT_NE(palette, nullptr);
    EXPECT_EQ(palette->count(), static_cast<int>(an::ui::annotationPalettes().size()) + 1) << "and Custom";
    choose("annotatePalette", static_cast<int>(an::ui::AnnotationPalette::Glacier));
    EXPECT_EQ(window.settings().style.colors, an::ui::annotationPaletteColors(an::ui::AnnotationPalette::Glacier));
    EXPECT_NE(window.findChild<QFontComboBox*>(QStringLiteral("annotateFont")), nullptr);

    QCheckBox* fill = box("annotateFill");
    auto* fillOpacity = window.findChild<QWidget*>(QStringLiteral("annotateFillOpacity"));
    ASSERT_TRUE(fill != nullptr && fillOpacity != nullptr);
    EXPECT_FALSE(fillOpacity->isEnabled()) << "no fill, no opacity to set";
    fill->click();
    EXPECT_TRUE(window.settings().style.fillObjects);
    EXPECT_TRUE(fillOpacity->isEnabled());
    choose("annotateMarker", static_cast<int>(an::ui::AnnotationMarker::Brackets));
    EXPECT_FALSE(fill->isEnabled()) << "brackets have no area to fill";
    choose("annotateMarker", static_cast<int>(an::ui::AnnotationMarker::Outline));
    EXPECT_TRUE(fill->isEnabled());

    choose("annotateFontWeight", static_cast<int>(an::ui::AnnotationFontWeight::Bold));
    QCheckBox* italic = box("annotateItalic");
    ASSERT_NE(italic, nullptr);
    italic->click();
    QCheckBox* lineHalo = box("annotateLineHalo");
    ASSERT_NE(lineHalo, nullptr);
    EXPECT_FALSE(lineHalo->isChecked()) << "the lines have no dark edge until it is asked for";
    lineHalo->click();
    choose("annotateBacking", static_cast<int>(an::ui::AnnotationLabelBacking::Plain));
    choose("annotateLabelColors", 0);
    const an::ui::AnnotationStyle& style = window.settings().style;
    EXPECT_EQ(style.fontWeight, an::ui::AnnotationFontWeight::Bold);
    EXPECT_TRUE(style.italicLabels);
    EXPECT_TRUE(style.lineHalo) << "plain labels leave the lines the edge asked for";
    EXPECT_EQ(style.labelBacking, an::ui::AnnotationLabelBacking::Plain);
    EXPECT_FALSE(style.labelsInObjectColor);
    lineHalo->click();
    choose("annotateBacking", static_cast<int>(an::ui::AnnotationLabelBacking::Halo));
    EXPECT_FALSE(window.settings().style.lineHalo) << "the labels' halo leaves the lines' edge as it was";

    // The stars: every one to magnitude six, the unnamed one by its catalogue number.
    auto* starMagnitude = window.findChild<QDoubleSpinBox*>(QStringLiteral("annotateStarMagnitude"));
    ASSERT_NE(starMagnitude, nullptr);
    starMagnitude->setValue(6.0);
    choose("annotateStarNaming", static_cast<int>(an::ui::AnnotationStarNaming::CatalogueNumbers));
    ASSERT_TRUE(pumpUntil([&]() {
        return !window.isWorking() && window.scene() != nullptr &&
               std::any_of(window.scene()->marks.begin(), window.scene()->marks.end(),
                           [](const an::ui::AnnotationMark& mark) { return mark.designation == "HIP 2"; });
    }));
    EXPECT_EQ(window.settings().content.stars.naming, an::ui::AnnotationStarNaming::CatalogueNumbers);
    EXPECT_EQ(an::ui::annotationDetailOf(window.settings().content), an::ui::AnnotationDetail::Custom);
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
