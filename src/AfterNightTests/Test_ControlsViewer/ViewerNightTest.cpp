// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Night in the viewer: switched on, a view presents its image again on a worker, the presentation's last stage mapping
/// every pixel to its luma in red, and the GUI thread maps nothing; a frame asked for under Night is presented in it;
/// the mask layer is red too, mapped once - a multiply of bytes already mapped is not mapped again; switched off, the
/// view presents its colours again.

#include "Test_ControlsViewer/ViewerTestContext.h"

#include "fixtures/ScopedNightMode.h"
#include "fixtures/ScopedStyleProfile.h"
#include "theme/AnDesignTokens.h"
#include "theme/NightImage.h"
#include "widgets/PresentationBridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QImage>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace
{

using namespace an::tests::viewer;

/// The luma of a presented pixel's colour, as Night maps it: blue, green and red bytes, premultiplied.
int lumaOfPresented(const std::uint8_t* pixel)
{
    return (299 * pixel[2] + 587 * pixel[1] + 114 * pixel[0] + 500) / 1000;
}

/// A colour image whose every pixel has another mix of the three channels.
[[nodiscard]] std::optional<an::ui::ImageDocument> colourDocument(const an::consumer::EngineSession& session)
{
    return makeDocument<float>(session, 16u, 8u, Channels::Rgb,
                               [](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
                                   if (channel == 0u)
                                   {
                                       return 0.05f * static_cast<float>(x);
                                   }
                                   return channel == 1u ? 0.1f * static_cast<float>(y) : 0.6f;
                               });
}

/// Tells a view what a new application palette tells every widget that takes it.
void paletteChanged(ImageViewControl& view)
{
    QEvent changed(QEvent::PaletteChange);
    QCoreApplication::sendEvent(view.getViewWidget(), &changed);
}

/// Whether every pixel of a picture is red alone.
bool redAlone(const QImage& picture)
{
    const QImage pixels = picture.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y)
    {
        for (int x = 0; x < pixels.width(); ++x)
        {
            const QRgb pixel = pixels.pixel(x, y);
            if (qGreen(pixel) != 0 || qBlue(pixel) != 0)
            {
                return false;
            }
        }
    }
    return true;
}

class ViewerNight : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = openViewerContext();
        ASSERT_NE(m_context, nullptr);
        m_installed.emplace(*m_context);
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

    std::unique_ptr<ViewerTestContext> m_context;
    std::optional<InstalledContext> m_installed;
};

} // namespace

// Switched on, Night presents the view again on a worker: every pixel is the luma of the colour it showed, in red, and
// the GUI thread mapped nothing. Switched off, the colours come back.
TEST_F(ViewerNight, PresentedFrameIsRed)
{
    const an::ui::testing::ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    ViewHandle view = makeView("night");
    std::optional<an::ui::ImageDocument> document = colourDocument(session());
    ASSERT_TRUE(document.has_value());
    view->setDocument(std::move(*document));
    ASSERT_TRUE(settle(*view));
    const std::shared_ptr<const an::ui::PresentedImage> day = view->getViewWidget()->presentedImage();
    ASSERT_NE(day, nullptr);
    ASSERT_EQ(day->format(), an::ui::PresentedFormat::Bgra8Premultiplied);
    bool coloured = false;
    for (std::uint32_t y = 0u; y < day->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < day->width(); ++x)
        {
            coloured = coloured || day->pixel(x, y)[0] != 0u || day->pixel(x, y)[1] != 0u;
        }
    }
    ASSERT_TRUE(coloured) << "the image is not shown in colour to begin with";

    {
        const an::ui::testing::ScopedNightMode night(true);
        const an::ui::testing::NightPasses before = an::ui::testing::nightPasses();
        paletteChanged(*view);
        ASSERT_TRUE(settle(*view));

        const std::shared_ptr<const an::ui::PresentedImage> red = view->getViewWidget()->presentedImage();
        ASSERT_NE(red, nullptr);
        ASSERT_NE(red, day) << "Night did not present the view again";
        ASSERT_EQ(red->width(), day->width());
        ASSERT_EQ(red->height(), day->height());
        for (std::uint32_t y = 0u; y < red->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < red->width(); ++x)
            {
                const std::uint8_t* pixel = red->pixel(x, y);
                EXPECT_EQ(pixel[0], 0u) << x << ',' << y;
                EXPECT_EQ(pixel[1], 0u) << x << ',' << y;
                EXPECT_EQ(pixel[2], lumaOfPresented(day->pixel(x, y))) << x << ',' << y;
                EXPECT_EQ(pixel[3], day->pixel(x, y)[3]) << x << ',' << y;
            }
        }
        const an::ui::testing::NightPasses after = an::ui::testing::nightPasses();
        EXPECT_EQ(after.onTheGuiThread, before.onTheGuiThread) << "the GUI thread mapped the presentation";
        EXPECT_GT(after.elsewhere, before.elsewhere);
        EXPECT_TRUE(redAlone(view->getViewWidget()->grab().toImage())) << "the view paints a colour under Night";

        view->show();
        const std::optional<an::ui::ViewPresentationSettings> frame =
            view->getViewWidget()->framePresentationSettings();
        ASSERT_TRUE(frame.has_value());
        EXPECT_TRUE(frame->night) << "a preview frame would be presented out of Night";
        view->hide();
        ASSERT_TRUE(settle(*view));
    }

    paletteChanged(*view);
    ASSERT_TRUE(settle(*view));
    const std::shared_ptr<const an::ui::PresentedImage> again = view->getViewWidget()->presentedImage();
    ASSERT_NE(again, nullptr);
    for (std::uint32_t y = 0u; y < again->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < again->width(); ++x)
        {
            for (std::uint32_t byte = 0u; byte < 4u; ++byte)
            {
                EXPECT_EQ(again->pixel(x, y)[byte], day->pixel(x, y)[byte]) << x << ',' << y;
            }
        }
    }
}

// Under Night a mask's layer is red too. A tint is mapped where it is made; a multiply of colour bytes the presentation
// mapped already is their product, which is mapped, and is not mapped again.
TEST_F(ViewerNight, TheMaskLayerIsRedAndMappedOnce)
{
    const an::ui::testing::ScopedStyleProfile observatory(an::ui::StyleProfile::Observatory);
    const an::ui::testing::ScopedNightMode night(true);
    ViewHandle target = makeView("target");
    ViewHandle mask = makeView("mask");
    std::optional<an::ui::ImageDocument> document = colourDocument(session());
    std::optional<an::ui::ImageDocument> half =
        makeDocument<float>(session(), 16u, 8u, Channels::Grey,
                            [](std::uint32_t x, std::uint32_t, std::uint32_t) { return x < 8u ? 0.0f : 1.0f; });
    ASSERT_TRUE(document.has_value() && half.has_value());
    target->setDocument(std::move(*document));
    mask->setDocument(std::move(*half));
    target->setCurrentMask(mask.get());
    ASSERT_TRUE(settle(*target));
    ASSERT_NE(target->getViewWidget()->maskLayer(), nullptr);
    EXPECT_TRUE(redAlone(target->getViewWidget()->grab().toImage())) << "the tint is painted in its colour";

    // A mask of ones multiplies what the view shows: the layer is the mapped presentation as it is.
    std::optional<an::ui::ImageDocument> ones = makeDocument<float>(
        session(), 16u, 8u, Channels::Grey, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0f; });
    ASSERT_TRUE(ones.has_value());
    mask->setDocument(std::move(*ones));
    target->setMaskRenderingMode(MaskRenderingMode::multiply_mode);
    ASSERT_TRUE(settle(*target));
    const std::shared_ptr<const an::ui::PresentedImage> layer = target->getViewWidget()->maskLayer();
    const std::shared_ptr<const an::ui::PresentedImage> shown = target->getViewWidget()->presentedImage();
    ASSERT_NE(layer, nullptr);
    ASSERT_NE(shown, nullptr);
    ASSERT_EQ(shown->format(), an::ui::PresentedFormat::Rgb8);
    bool lit = false;
    for (std::uint32_t y = 0u; y < shown->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < shown->width(); ++x)
        {
            EXPECT_EQ(shown->pixel(x, y)[1], 0u) << "the multiplied bytes were not mapped";
            EXPECT_EQ(layer->pixel(x, y)[0], shown->pixel(x, y)[0]) << x << ',' << y;
            lit = lit || shown->pixel(x, y)[0] > 0u;
        }
    }
    EXPECT_TRUE(lit);
    const QImage painted = target->getViewWidget()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    EXPECT_TRUE(redAlone(painted)) << "the multiply is painted in colour";

    // What is painted at the centre of each image pixel is the layer, magnified as blocks: the mapped presentation's
    // red, not mapped a second time.
    const double scale = target->getViewWidget()->getCurrentScaleFactor();
    const QSize rendered(static_cast<int>(shown->width() * scale), static_cast<int>(shown->height() * scale));
    int compared = 0;
    for (std::uint32_t y = 0u; y < shown->height(); ++y)
    {
        for (std::uint32_t x = 0u; x < shown->width(); ++x)
        {
            const int px = static_cast<int>((x + 0.5) * rendered.width() / shown->width());
            const int py = static_cast<int>((y + 0.5) * rendered.height() / shown->height());
            if (px >= painted.width() || py >= painted.height() || shown->pixel(x, y)[0] < 16u)
            {
                continue;
            }
            EXPECT_NEAR(qRed(painted.pixel(px, py)), shown->pixel(x, y)[0], 1) << x << ',' << y;
            ++compared;
        }
    }
    EXPECT_GT(compared, 0) << "no pixel of the layer was painted where it could be read";
    target->removeCurrentMask();
}
