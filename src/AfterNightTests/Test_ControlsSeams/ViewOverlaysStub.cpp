// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Stand-ins for the astrometry overlays the viewer reaches through its companion seam, in this suite, which compiles
/// the viewer's closure rather than linking the Controls library: each records what the viewer asks and draws
/// nothing.

#include "Test_ControlsSeams/ViewCompanionsStub.h"

#include "controls/ImageViewControl.h"
#include "widgets/ImageViewWidgetDisplayModule.h"

#include <memory>
#include <utility>

namespace an::tests::viewer
{

namespace
{

/// An overlay that draws nothing and keeps what it is handed.
class StandInOverlay final : public ::ImageViewWidgetDisplayModule
{
public:
    explicit StandInOverlay(an::ui::AstrometryOverlay kind) : m_kind(kind) {}

    void onDraw(QPainter&) override {}

    [[nodiscard]] an::ui::AstrometryOverlay kind() const noexcept
    {
        return m_kind;
    }

private:
    an::ui::AstrometryOverlay m_kind;
};

} // namespace

} // namespace an::tests::viewer

namespace an::ui
{

std::unique_ptr<::ImageViewWidgetDisplayModule> createAstrometryOverlay(AstrometryOverlay overlay, ::ImageViewControl&)
{
    an::tests::viewer::companionRecord().overlaysMade.push_back(overlay);
    return std::make_unique<an::tests::viewer::StandInOverlay>(overlay);
}

void setAstrometryOverlayMagnitudeLimit(::ImageViewWidgetDisplayModule&, double limit)
{
    an::tests::viewer::companionRecord().magnitudeLimits.push_back(limit);
}

void setAstrometryOverlayLayers(::ImageViewWidgetDisplayModule&, const bool deepSky, const bool stars)
{
    an::tests::viewer::companionRecord().overlayLayers.emplace_back(deepSky, stars);
}

void setAstrometryOverlayStars(::ImageViewWidgetDisplayModule& overlay, std::shared_ptr<const ViewOverlayStars> stars)
{
    const auto* standIn = dynamic_cast<const an::tests::viewer::StandInOverlay*>(&overlay);
    if (standIn != nullptr && standIn->kind() == AstrometryOverlay::DetectedStars)
    {
        an::tests::viewer::companionRecord().overlayStars.push_back(std::move(stars));
    }
}

} // namespace an::ui
