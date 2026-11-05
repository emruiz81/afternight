// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The plate-solve window over a view: the hints it fills from the image's cards as the solve itself reads them, the
/// solved field it shows, a solve run on the thread pool and written into the image as an edit the history records -
/// or set aside when the view's image changed under it - the solution withdrawn as another edit, and the warning a
/// blind solve is given while it runs.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"
#include "Test_ControlsAstrometry/SolvableField.h"

#include "app/PlateSolveTypes.h"
#include "components/AnInfoBanner.h"
#include "controls/ImageViewControl.h"
#include "controls/PlateSolveControl.h"
#include "controls/settings/SettingsKeys.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
using an::tests::astrometry::cardValue;
using an::tests::astrometry::documentCards;
using an::tests::astrometry::InstalledContext;
using an::tests::astrometry::makeCardDocument;
using an::tests::astrometry::makeView;
using an::tests::astrometry::metadataWithCards;
using an::tests::astrometry::northUpPlane;
using an::tests::astrometry::openViewerContext;
using an::tests::astrometry::pumpUntil;
using an::tests::astrometry::settle;
using an::tests::astrometry::tangentPlaneCards;
using an::tests::astrometry::ViewerTestContext;
using an::tests::astrometry::ViewHandle;
using an::tests::astrometry::wcsCardCount;

/// A solution about (157.5, 41.5) - 10h 30m and +41 30' - at the centre pixel of a 400 by 300 frame, stated in the ICRS
/// and named for its solver.
[[nodiscard]] std::vector<an::CodecCard> solvedCards(const double rightAscension = 157.5,
                                                     const double declination = 41.5)
{
    std::vector<an::CodecCard> cards =
        tangentPlaneCards(northUpPlane(200.5, 150.5, rightAscension, declination, 0.0005));
    addCard(cards, "RADESYS", "'ICRS'");
    addCard(cards, "PLTSOLVR", "'ASTAP'");
    return cards;
}

/// The window's cases: an installed context, a view and a directory for a solve's catalog.
class PlateSolveWindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_directory.isValid());
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("plate-solve");
    }

    void TearDown() override
    {
        if (m_window)
        {
            ASSERT_TRUE(pumpUntil([this]() { return !m_window || !m_window->isSolving(); }));
        }
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    /// Shows a document of a size carrying cards, its history begun as the application begins a view's.
    void show(std::vector<an::CodecCard> cards, const std::uint32_t width = 400u, const std::uint32_t height = 300u)
    {
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(m_context->engineSession(), width, height, std::move(cards));
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
        m_view->initializeNewImageHistory("Image opened");
        ASSERT_TRUE(settle(*m_view));
    }

    /// Opens the window over the view.
    an::ui::PlateSolveControl& open()
    {
        m_window = new an::ui::PlateSolveControl(m_view.get());
        return *m_window;
    }

    /// A child of the window by its object name.
    template <typename WidgetT> [[nodiscard]] WidgetT* child(const char* name) const
    {
        WidgetT* found = m_window->findChild<WidgetT*>(QString::fromLatin1(name));
        EXPECT_NE(found, nullptr) << name;
        return found;
    }

    /// Stops the solve under way with the window's own Cancel, and waits for it to end.
    [[nodiscard]] bool cancelSolve() const
    {
        for (QPushButton* button : m_window->findChildren<QPushButton*>())
        {
            if (button->text() == QStringLiteral("Cancel"))
            {
                button->click();
                break;
            }
        }
        return pumpUntil([this]() { return !m_window || !m_window->isSolving(); });
    }

    /// Installs the solvable field's catalog and points the internal solver at it.
    void configureInternalSolver()
    {
        const QString catalog = QDir(m_directory.path()).filePath(QStringLiteral("gaia_dr3_g14"));
        ASSERT_TRUE(QDir().mkpath(catalog));
        ASSERT_TRUE(
            an::tests::astrometry::field::writeGaiaCatalog(catalog, an::tests::astrometry::field::catalogStars()));
        m_context->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_DEFAULT_SOLVER), QStringLiteral("internal_gaia"));
        m_context->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_GAIA_CATALOG_DIRECTORY), catalog);
        m_context->upsert(QString::fromUtf8(SettingsKeys::PLATESOLVE_SEARCH_RADIUS), QStringLiteral("1.0"));
    }

    /// Shows the solvable field, its cards stating the target and the optics and no solution.
    void showSolvableField()
    {
        std::optional<an::consumer::ImageAsset> asset = an::tests::astrometry::field::makeFieldAsset(
            m_context->engineSession(), metadataWithCards(an::tests::astrometry::field::hintCards()));
        ASSERT_TRUE(asset.has_value());
        an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(std::move(*asset));
        ASSERT_TRUE(document.hasValue()) << document.status().message();
        m_view->setDocument(std::move(*document));
        m_view->initializeNewImageHistory("Image opened");
        ASSERT_TRUE(settle(*m_view));
    }

    QTemporaryDir m_directory;
    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
    QPointer<an::ui::PlateSolveControl> m_window;
};

} // namespace

TEST_F(PlateSolveWindowTest, TheHintsAreThePointingAndOpticsTheSolveReads)
{
    // The upper-case unit letters the solve refused before; the window and the solve read them through one reader.
    std::vector<an::CodecCard> cards;
    addCard(cards, "OBJCTRA", "'05H34M31.94S'");
    addCard(cards, "OBJCTDEC", "'-22D00M52.2S'");
    addCard(cards, "FOCALLEN", "530");
    addCard(cards, "XPIXSZ", "3.76");
    show(cards);
    open();

    EXPECT_EQ(child<QSpinBox>("plateSolveRaHours")->value(), 5);
    EXPECT_EQ(child<QSpinBox>("plateSolveRaMinutes")->value(), 34);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolveRaSeconds")->value(), 31.94, 1e-9);
    EXPECT_EQ(child<QComboBox>("plateSolveDecHemisphere")->currentIndex(), 1);
    EXPECT_EQ(child<QSpinBox>("plateSolveDecDegrees")->value(), 22);
    EXPECT_EQ(child<QSpinBox>("plateSolveDecMinutes")->value(), 0);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolveDecSeconds")->value(), 52.2, 1e-9);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolveFocalLength")->value(), 530.0, 1e-9);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolvePixelSize")->value(), 3.76, 1e-9);
}

TEST_F(PlateSolveWindowTest, AHintIsRoundedOnceAndCarriedIntoEveryField)
{
    // 10h 29m 59.9996s rounds to 10h 30m 00.000s, never to 59m 60.000s.
    std::vector<an::CodecCard> cards;
    addCard(cards, "RA", "157.4999983333");
    addCard(cards, "DEC", "41.4999999");
    show(cards);
    open();

    EXPECT_EQ(child<QSpinBox>("plateSolveRaHours")->value(), 10);
    EXPECT_EQ(child<QSpinBox>("plateSolveRaMinutes")->value(), 30);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolveRaSeconds")->value(), 0.0, 1e-9);
    EXPECT_EQ(child<QSpinBox>("plateSolveDecDegrees")->value(), 41);
    EXPECT_EQ(child<QSpinBox>("plateSolveDecMinutes")->value(), 30);
    EXPECT_NEAR(child<QDoubleSpinBox>("plateSolveDecSeconds")->value(), 0.0, 1e-9);
}

TEST_F(PlateSolveWindowTest, ASolvedImageShowsItsFieldRoundedOnce)
{
    // The field's centre at 10h 29m 59.996s and +41 29' 59.96": rounded once, never 60.00 seconds under the same
    // minute.
    show(solvedCards((10.0 + 29.0 / 60.0 + 59.996 / 3600.0) * 15.0, 41.0 + 29.0 / 60.0 + 59.96 / 3600.0));
    open();

    EXPECT_EQ(child<QLabel>("plateSolveFieldCenterRa")->text(), QStringLiteral("10:30:00.00"));
    EXPECT_EQ(child<QLabel>("plateSolveFieldCenterDec")->text(), QStringLiteral("+41:30:00.0"));
    EXPECT_EQ(child<QLabel>("plateSolveSolver")->text(), QStringLiteral("ASTAP"));
    EXPECT_TRUE(child<QPushButton>("plateSolveClearAstrometry")->isEnabled());
}

TEST_F(PlateSolveWindowTest, ClearingIsAnEditTheHistoryRecordsAndTheViewShows)
{
    show(solvedCards());
    ASSERT_TRUE(m_view->imageSolution().has_value());
    open();
    m_context->answerYes = true;

    child<QPushButton>("plateSolveClearAstrometry")->click();
    ASSERT_TRUE(settle(*m_view));

    EXPECT_FALSE(m_view->imageSolution().has_value());
    const std::vector<an::CodecCard> cards = documentCards(*m_view);
    EXPECT_EQ(wcsCardCount(cards), 0u);
    EXPECT_FALSE(cardValue(cards, an::ui::kPlateSolveSolverKeyword).has_value());
    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("question:Clear Astrometry")));
    ASSERT_NE(m_view->viewHistory(), nullptr);
    EXPECT_EQ(m_view->viewHistory()->previousStepName(), "Clear Astrometry");
    EXPECT_FALSE(child<QPushButton>("plateSolveClearAstrometry")->isEnabled());
}

TEST_F(PlateSolveWindowTest, ClearingIsNotDoneWhenThePersonSaysNo)
{
    show(solvedCards());
    open();
    m_context->answerYes = false;

    child<QPushButton>("plateSolveClearAstrometry")->click();
    ASSERT_TRUE(settle(*m_view));

    EXPECT_TRUE(m_view->imageSolution().has_value());
    ASSERT_NE(m_view->viewHistory(), nullptr);
    EXPECT_FALSE(m_view->viewHistory()->hasUndo());
}

TEST_F(PlateSolveWindowTest, ASolveIsWrittenIntoTheImageAsAnEditTheHistoryRecords)
{
    configureInternalSolver();
    showSolvableField();
    an::ui::PlateSolveControl& window = open();
    ASSERT_TRUE(child<QPushButton>("plateSolveSolve")->isEnabled());
    ASSERT_TRUE(child<QCheckBox>("plateSolveWriteWcs")->isChecked());

    child<QPushButton>("plateSolveSolve")->click();
    EXPECT_TRUE(window.isSolving());
    ASSERT_TRUE(pumpUntil([&window]() { return !window.isSolving(); }));
    ASSERT_TRUE(settle(*m_view));

    EXPECT_EQ(child<QLabel>("plateSolveSolver")->text(), QStringLiteral("Internal Gaia"));
    ASSERT_TRUE(m_view->imageSolution().has_value());
    EXPECT_NEAR(m_view->imageSolution()->meanPixelScaleArcsecPerPixel(), an::tests::astrometry::field::kScaleArcsec,
                0.01 * an::tests::astrometry::field::kScaleArcsec);
    const std::vector<an::CodecCard> cards = documentCards(*m_view);
    EXPECT_EQ(cardValue(cards, an::ui::kPlateSolveSolverKeyword), std::optional<std::string>("'Internal Gaia'"));
    EXPECT_EQ(cardValue(cards, "RADESYS"), std::optional<std::string>("'ICRS'"));
    EXPECT_EQ(cardValue(cards, "OBJCTRA"), std::optional<std::string>("'05 34 45'"));
    ASSERT_NE(m_view->viewHistory(), nullptr);
    EXPECT_EQ(m_view->viewHistory()->previousStepName(), "Plate Solve");
}

TEST_F(PlateSolveWindowTest, ASolveOfAnImageThatChangedUnderItIsSetAside)
{
    configureInternalSolver();
    showSolvableField();
    an::ui::PlateSolveControl& window = open();

    child<QPushButton>("plateSolveSolve")->click();
    // The view shows another image before the solve can answer: the answer is for an image no longer shown.
    std::optional<an::ui::ImageDocument> other = makeCardDocument(m_context->engineSession(), 64u, 48u, {});
    ASSERT_TRUE(other.has_value());
    m_view->setDocument(std::move(*other));
    ASSERT_TRUE(pumpUntil([&window]() { return !window.isSolving(); }));
    ASSERT_TRUE(settle(*m_view));

    EXPECT_FALSE(m_view->imageSolution().has_value());
    ASSERT_NE(m_view->viewHistory(), nullptr);
    EXPECT_FALSE(m_view->viewHistory()->hasUndo());
    EXPECT_TRUE(child<QLabel>("plateSolveStatus")->text().contains(QStringLiteral("active image changed")))
        << child<QLabel>("plateSolveStatus")->text().toStdString();
}

TEST_F(PlateSolveWindowTest, AFailedSolveIsReportedThroughTheDialogService)
{
    configureInternalSolver();
    // A frame with no stars: the internal solver finds nothing to match and no other solver is configured.
    show(an::tests::astrometry::field::hintCards(), 256u, 192u);
    an::ui::PlateSolveControl& window = open();

    child<QPushButton>("plateSolveSolve")->click();
    ASSERT_TRUE(pumpUntil([&window]() { return !window.isSolving(); }));
    ASSERT_TRUE(settle(*m_view));

    EXPECT_TRUE(m_context->messages.contains(QStringLiteral("error:Plate Solve Failed")));
    EXPECT_FALSE(m_view->imageSolution().has_value());
    ASSERT_NE(m_view->viewHistory(), nullptr);
    EXPECT_FALSE(m_view->viewHistory()->hasUndo());
}

// A solve with no position hint - none in the window and none in the image's cards - warns that it is blind, and the
// warning stays up while the solve runs: the warning was shown and cleared in the same call, before the solve began, so
// nobody saw it.
TEST_F(PlateSolveWindowTest, ABlindSolveIsWarnedAboutWhileItRuns)
{
    configureInternalSolver();
    show({}, 256u, 192u);
    an::ui::PlateSolveControl& window = open();
    ASSERT_EQ(child<QSpinBox>("plateSolveRaHours")->value(), 0);
    ASSERT_EQ(child<QSpinBox>("plateSolveDecDegrees")->value(), 0);

    child<QPushButton>("plateSolveSolve")->click();
    ASSERT_TRUE(window.isSolving());
    // The banner declares no meta-object of its own, so it is found as the frame it is.
    const auto* banner = dynamic_cast<const an::ui::AnInfoBanner*>(child<QFrame>("plateSolveBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_FALSE(banner->isHidden()) << "the warning is shown while the solve runs";
    EXPECT_EQ(banner->tone(), an::ui::AnInfoBanner::Tone::Warning);
    EXPECT_TRUE(banner->message().contains(QStringLiteral("Blind solving"))) << banner->message().toStdString();
    ASSERT_TRUE(cancelSolve());
}

// A solve the image's cards give a position to is not warned about being blind.
TEST_F(PlateSolveWindowTest, AHintedSolveIsNotWarnedAboutBeingBlind)
{
    configureInternalSolver();
    showSolvableField();
    an::ui::PlateSolveControl& window = open();

    child<QPushButton>("plateSolveSolve")->click();
    ASSERT_TRUE(window.isSolving());
    const auto* banner = dynamic_cast<const an::ui::AnInfoBanner*>(child<QFrame>("plateSolveBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_TRUE(banner->isHidden()) << banner->message().toStdString();
    ASSERT_TRUE(pumpUntil([&window]() { return !window.isSolving(); }));
}
