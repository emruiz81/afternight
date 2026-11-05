// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The layout a process dialog with a real-time preview is built in: the main splitter between its controls and its
/// preview, how the preview pane is shown, hidden and collapsed, the window constraints a hidden preview needs, and the
/// default preview area.
///
/// Every function is called on the GUI thread and touches nothing but the widgets it is given.
#pragma once

#include "AfterNightControlsExport.h"

#include <QRect>
#include <QSize>

class QBoxLayout;
class QLayout;
class QSplitter;
class QWidget;

namespace an::ui
{

/// The space between a process dialog's controls and its preview, in pixels.
inline constexpr int kProcessMainPanelsSpacing = 24;

/// The narrowest a shown preview pane may be, in pixels.
inline constexpr int kProcessMinPreviewPaneWidth = 200;

/// The edge of the preview area a process proposes when it states none, in image pixels.
inline constexpr int kProcessDefaultPreviewAreaEdge = 600;

/// The splitter between a process dialog's controls and its preview: a splitter laid out directly in the main layout,
/// or the one named "mainSplitter" inside a widget of it or inside the layout's own widget.
///
/// @param mainLayout the dialog's main layout, or null.
/// @return the splitter, or null when there is none.
[[nodiscard]] AFTERNIGHTCONTROLS_API QSplitter* findMainProcessSplitter(QBoxLayout* mainLayout);

/// Shows or hides the preview pane - the splitter's second widget - and the handle before it. A hidden pane has no
/// width at all, so the splitter's size leaves it out; a shown one is at least `kProcessMinPreviewPaneWidth` wide.
///
/// @param splitter the main splitter, or null; one with fewer than two widgets is left alone.
/// @param visible whether the pane is shown.
AFTERNIGHTCONTROLS_API void setPreviewPaneVisible(QSplitter* splitter, bool visible);

/// Gives all of the splitter's width to the controls pane, for a dialog that opens with its preview off.
///
/// @param splitter the main splitter, or null; one with fewer than two widgets is left alone.
AFTERNIGHTCONTROLS_API void collapsePreviewPane(QSplitter* splitter);

/// The widget the preview layout lays out in, which is shown and hidden with the preview.
///
/// @param previewLayout the preview layout, or null.
/// @return the widget, or null.
[[nodiscard]] AFTERNIGHTCONTROLS_API QWidget* findPreviewPanelWidget(QLayout* previewLayout);

/// Gives every slider row of a controls panel - a label, a spin box and a slider in one row - the width of the widest
/// row label, so the sliders line up.
///
/// @param controlsPanel the panel, or null.
AFTERNIGHTCONTROLS_API void alignSliderRowLabels(QWidget* controlsPanel);

/// A maximum window size a window can be given back: an edge that is zero, negative or beyond Qt's largest becomes
/// Qt's largest.
///
/// @param size the size saved.
/// @return the size to restore.
[[nodiscard]] AFTERNIGHTCONTROLS_API QSize sanitizeMaxSize(const QSize& size);

/// The preview area a process proposes over an image: a rectangle of the requested size -
/// `kProcessDefaultPreviewAreaEdge` on an edge it does not state - centred on the image and held inside it.
///
/// @param imageBounds the image, in its own pixels.
/// @param requestedSize the size the process asked for.
/// @return the area, or an empty rectangle for an empty image.
[[nodiscard]] AFTERNIGHTCONTROLS_API QRect centeredPreviewArea(const QRect& imageBounds, const QSize& requestedSize);

} // namespace an::ui
