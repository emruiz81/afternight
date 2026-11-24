// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The opener of the tool windows a view shows over its document - the statistics, the histogram, the FITS header,
/// the Exif fields, the summary, the astrometric solution, the plate solve and the annotated picture - defined beside
/// the windows, as the view's companion seam declares it, so the view compiles with none of their headers.
///
/// Each window is a child of the view, deleted when it is closed and with the view, and one per view: asking for one
/// already open raises it. The statistics, histogram, FITS header, Exif and summary windows disable the view's entry
/// that opens them while they are open, as they always did; the readout's buttons, which no window disables, raise the
/// open one rather than open another.

#include "stdafx.hpp"

#include "controls/ImageAstrometryInfoControl.h"
#include "controls/ImageEXIFViewerControl.h"
#include "controls/ImageFITSHeaderViewerControl.h"
#include "controls/ImageHistogramControl.h"
#include "controls/ImageStatisticsControl.h"
#include "controls/ImageSummaryViewerControl.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControl.h"
#include "controls/PlateSolveControl.h"
#include "controls/annotation/AnnotateImageControl.h"

#include <QWidget>

namespace an::ui
{

namespace
{

/// Raises the window of a kind the view already has open, or opens one.
template <typename WindowT> QWidget* raiseOrOpen(::ImageViewControl& view)
{
    // Not every window declares a meta-object of its own, so the view's children are matched by their type. A window
    // closed and not yet deleted is hidden, and is not the one open.
    for (QObject* child : view.children())
    {
        if (auto* open = dynamic_cast<WindowT*>(child); open != nullptr && open->isVisible())
        {
            open->raise();
            open->activateWindow();
            return open;
        }
    }
    auto* window = new WindowT(&view);
    window->show();
    return window;
}

} // namespace

QWidget* openViewTool(const ViewTool tool, ::ImageViewControl& view)
{
    switch (tool)
    {
    case ViewTool::Statistics:
        return raiseOrOpen<ImageStatisticsControl>(view);
    case ViewTool::Histogram:
        return raiseOrOpen<ImageHistogramControl>(view);
    case ViewTool::FitsHeader:
        return raiseOrOpen<ImageFITSHeaderViewerControl>(view);
    case ViewTool::Exif:
        return raiseOrOpen<ImageEXIFViewerControl>(view);
    case ViewTool::Summary:
        return raiseOrOpen<ImageSummaryViewerControl>(view);
    case ViewTool::AstrometryInfo:
        return raiseOrOpen<ImageAstrometryInfoControl>(view);
    case ViewTool::PlateSolve:
        return raiseOrOpen<PlateSolveControl>(view);
    case ViewTool::AnnotateImage:
        return raiseOrOpen<AnnotateImageControl>(view);
    }
    return nullptr;
}

} // namespace an::ui
