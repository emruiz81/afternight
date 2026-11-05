// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QString>
#include <QtWidgets/QMainWindow>
#include "controls/ImageViewControlListener.h"
#include "SexagesimalFormatter.h"

class ImageViewControl;
class QLabel;

namespace an::ui
{

/// The status bar's right ascension under the cursor: hours, minutes and seconds to a hundredth.
[[nodiscard]] inline QString formatRightAscensionSexagesimal(const double ra_deg)
{
    return rightAscensionLabel(ra_deg, 2u);
}

/// The status bar's declination under the cursor: degrees, arcminutes and arcseconds to a tenth.
[[nodiscard]] inline QString formatDeclinationSexagesimal(const double dec_deg)
{
    return declinationLabel(dec_deg, 1u);
}
} // namespace an::ui

class AFTERNIGHTCONTROLS_API ImageInfoStatusBarWidget
{
public:
    ImageInfoStatusBarWidget();

    void initialize(QStatusBar* bar);

    // Update all info
    void updateImageInfo(ImageViewControl* control);

    // Just update the zoom info
    void updateImageZoomInfo(ImageViewControl* control);

    void updateCelestialCoords(double ra_deg, double dec_deg);
    void clearCelestialCoords();

    void clearImageInfo();

    ~ImageInfoStatusBarWidget();

private:
    std::unique_ptr<QLabel> m_ImageResolution;

    std::unique_ptr<QLabel> m_ImageZoom;

    std::unique_ptr<QLabel> m_ra_label;

    std::unique_ptr<QLabel> m_dec_label;
};
