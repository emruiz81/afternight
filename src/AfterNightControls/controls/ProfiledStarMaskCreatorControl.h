// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QtWidgets/QMainWindow>
#include "ProfiledStarMaskCreatorControlUiBuilder.h"

#include "StarsFilterControlHandler.h"
#include "ImageViewControl.h"
#include "controls/QSliderSpinBox.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Stars/StarMask.h>
#include <AfterNightEngine/Core/Result.h>

#include <cstdint>
#include <span>
#include <stop_token>

class QString;

namespace an::ui
{

/// What a profiled star mask paints.
struct ProfiledStarMaskRequest
{
    /// Whether each star is a radial falloff of `shape` rather than a solid ellipse.
    bool shaped = false;

    /// The factor a solid ellipse's axes are multiplied by.
    double scaleFactor = 1.0;

    /// The standard deviation, in pixels, the solid ellipses are blurred by; zero leaves their edges hard.
    double softenEdges = 0.0;

    /// The falloff a shaped star is painted with.
    an::StarMaskShape shape = an::StarMaskShape::TruncatedBell;

    /// The factor a shaped star's radius is multiplied by.
    double sizeScale = 0.5;

    /// The pixels added to a shaped star's radius after scaling.
    double sizeIncrease = 0.0;
};

/// Paints a star list into a new mask: one grey Float32 image of the frame's size whose samples run from zero, where no
/// star reaches, to one.
///
/// @param session the session the mask is painted in and belongs to.
/// @param stars the stars, in the frame's engine pixel coordinates.
/// @param width the frame's width.
/// @param height the frame's height.
/// @param request what is painted.
/// @param stop stops the painting.
/// @return the mask; or why it could not be painted.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::Result<an::consumer::ImageAsset>
makeProfiledStarMask(const an::consumer::EngineSession& session, std::span<const an::DetectedStar> stars,
                     std::uint32_t width, std::uint32_t height, const ProfiledStarMaskRequest& request,
                     std::stop_token stop = {});

/// The falloff a shape selector's entry names.
///
/// @param text the entry.
/// @return the shape; the truncated bell for an entry that names none.
[[nodiscard]] AFTERNIGHTCONTROLS_API an::StarMaskShape starMaskShapeFromText(const QString& text);

} // namespace an::ui

// The profiled star-mask window: paints the process's stars into a new mask on a worker and shows it in a new view
// beside the one the stars were detected on.
class AFTERNIGHTCONTROLS_API ProfiledStarMaskCreatorControl : public QDialog, public StarsFilterControlBase
{
public:
    ProfiledStarMaskCreatorControl(StarsFilterControlHandler* parent);

    void updateFilter() override;

    bool isPreviewEnabled() const override;

    void forceClose() override;

    // What the window's controls ask the mask for.
    [[nodiscard]] an::ui::ProfiledStarMaskRequest maskRequest() const;

    // Paints the mask on a worker; the new view appears when it lands. Returns false, with nothing started, when there
    // is no view or no session.
    bool generateMask();

    // The window's controls, for the cases that drive it.
    [[nodiscard]] const an::ui::controls::ProfiledStarMaskCreatorControlUiElements& ui() const;

    ~ProfiledStarMaskCreatorControl();

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void on_scaleFactorChanged(double value);
    void on_softenEdgesChanged(double value);
    void on_percentPixelIncreaseChanged(double value);
    void on_pixelIncreaseChanged(int value);

    void on_BinaryAreasGroupBoxClicked(bool on);
    void on_ShapedAreasGroupBoxClicked(bool on);

private:
    virtual void done(int r) override;

    void initialize();

    QSliderDoubleSpinBoxUPtr scaleFactorControl;
    QSliderDoubleSpinBoxUPtr softenEdgesControl;
    QSliderDoubleSpinBoxUPtr percentPixelIncreaseControl;
    QSliderSpinBoxUPtr pixelIncreaseControl;

    an::ui::controls::ProfiledStarMaskCreatorControlUiElements m_ui;

    bool m_IgnoreLastGroupUpdatedEvent = false;
};
