// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The astrometry windows that stay windows, under the Observatory profile: Plate Solve and Annotate Image over a
/// solved view, Find Object and a catalogue's download. Each has a heading, a footer whose rightmost button is the
/// primary one, text that meets the contrast rule, nothing clipped as it opens or at its least size, and opens within
/// its screen's available area - in both densities.

#include "Test_ControlsAstrometry/AstrometryTestSupport.h"

#include "app/GaiaCatalogSettings.h"
#include "components/AnWindowHeading.h"
#include "controls/PlateSolveControl.h"
#include "controls/annotation/AnnotateImageControl.h"
#include "dialogs/CatalogDownloadDialog.h"
#include "dialogs/CatalogSearchDialog.h"
#include "fixtures/WindowLook.h"

#include <QPushButton>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace
{

using an::tests::astrometry::addCard;
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
using an::tests::look::expectWindowLook;
using an::tests::look::WindowLookMeasured;

/// The suite's context and a view of a solved field, installed for a case.
class WindowLook : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = an::tests::viewer::openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
        m_view = makeView("M83_RGB");
        std::vector<an::CodecCard> cards = tangentPlaneCards(northUpPlane(200.5, 150.5, 120.0, 10.0, 0.01));
        addCard(cards, "RADESYS", "'ICRS'");
        addCard(cards, "OBJECT", "'Test Field'");
        addCard(cards, "FOCALLEN", "432");
        std::optional<an::ui::ImageDocument> document =
            makeCardDocument(m_context->engineSession(), 400u, 300u, std::move(cards));
        ASSERT_TRUE(document.has_value());
        m_view->setDocument(std::move(*document));
        m_view->initializeNewImageHistory("Image opened");
        ASSERT_TRUE(settle(*m_view));
    }

    void TearDown() override
    {
        m_view.reset();
        m_installed.reset();
        m_context.reset();
    }

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
    ViewHandle m_view;
};

/// What a window's heading says over its title.
[[nodiscard]] QString eyebrowOf(const QWidget& window)
{
    const auto* const heading = window.findChild<an::ui::AnWindowHeading*>(QStringLiteral("anWindowHeading"));
    return heading != nullptr ? heading->eyebrow() : QString();
}

} // namespace

// Plate Solve: under the Observatory profile Solve, its primary button, is the footer's rightmost, after Close, and
// the button that clears a solution is quiet.
TEST_F(WindowLook, PlateSolve)
{
    const WindowLookMeasured measured = expectWindowLook([this]() -> QWidget* {
        auto* const window = new an::ui::PlateSolveControl(m_view.get());
        if (an::ui::windowsTakeHeadings())
        {
            EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Astrometry · M83_RGB"));
            const auto* const clear = window->findChild<QPushButton*>(QStringLiteral("plateSolveClearAstrometry"));
            EXPECT_TRUE(clear != nullptr && !an::tests::look::isPrimary(*clear))
                << "the window has a second primary button";
        }
        return window;
    });
    AN_WINDOW_LOOK_REPORT(measured);
}

// Annotate Image: under the Observatory profile Export..., its primary button, is the footer's rightmost, after Close
// and Open as Image.
TEST_F(WindowLook, AnnotateImage)
{
    const auto letGoOnceIdle = [](QWidget* window) {
        auto* const annotate = dynamic_cast<an::ui::AnnotateImageControl*>(window);
        if (annotate != nullptr)
        {
            EXPECT_TRUE(pumpUntil([annotate]() { return !annotate->isWorking(); }));
        }
        an::tests::look::letGo(window);
    };
    const WindowLookMeasured measured = expectWindowLook(
        [this]() -> QWidget* {
            auto* const window = new an::ui::AnnotateImageControl(m_view.get());
            window->setCatalogSource(fixedCatalogs(
                QByteArray("id,common_name,ra_deg,dec_deg,major_axis_arcmin,minor_axis_arcmin,position_angle_deg,"
                           "object_type,magnitude\nNGC 1001,Bright One,120.5,10.6,40.0,24.0,10,Galaxy,8.0\n"),
                QByteArray("id,common_name,bayer_designation,ra_deg,dec_deg,magnitude,spectral_type\n"
                           "HIP 1,Alpha Field,alf Fld,119.4,10.3,1.5,A0V\n")));
            if (an::ui::windowsTakeHeadings())
            {
                EXPECT_EQ(eyebrowOf(*window), QStringLiteral("Analysis · M83_RGB"));
                const QList<QPushButton*> order = {window->findChild<QPushButton*>(QStringLiteral("annotateClose")),
                                                   window->findChild<QPushButton*>(QStringLiteral("annotateOpen")),
                                                   window->findChild<QPushButton*>(QStringLiteral("annotateExport"))};
                EXPECT_FALSE(order.contains(nullptr));
            }
            return window;
        },
        letGoOnceIdle);
    AN_WINDOW_LOOK_REPORT(measured);
}

TEST_F(WindowLook, FindObject)
{
    const WindowLookMeasured measured =
        expectWindowLook([]() -> QWidget* { return new an::ui::CatalogSearchDialog(QStringLiteral("M 83")); });
    AN_WINDOW_LOOK_REPORT(measured);
}

// A catalogue's download: under the Observatory profile the button that downloads is the footer's primary one.
TEST_F(WindowLook, CatalogDownload)
{
    const WindowLookMeasured measured = expectWindowLook(
        []() -> QWidget* { return new an::ui::CatalogDownloadDialog(an::ui::availableGaiaCatalogPacks()); });
    AN_WINDOW_LOOK_REPORT(measured);
}
