// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Draws an annotated picture's annotations: the grid, the marks, the leader lines, the labels and the decorations of
/// a scene and its layout, in the colours and the backing a style states, through any `QPainter`.
///
/// The painter's transform maps the image's pixels onto its device - a picture at full size, a smaller one, the
/// window's preview - and the drawing is clipped to the frame. Every length was resolved by the layout, so the drawing
/// scales as one: what is thin in the preview is as thin, relative to the image, in the picture.
///
/// Draws on any thread a `QPainter` may paint a `QImage` on.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/annotation/AnnotationLayout.h"
#include "controls/annotation/AnnotationScene.h"
#include "controls/annotation/AnnotationSettings.h"

#include <QColor>

class QPainter;

namespace an::ui
{

/// The colour a mark of a kind is drawn in.
///
/// @param colors the colours.
/// @param kind the mark's kind.
/// @return the colour.
[[nodiscard]] AFTERNIGHTCONTROLS_API QColor annotationMarkColor(const AnnotationColors& colors,
                                                                AnnotationMarkKind kind);

/// Draws a scene's annotations as its layout places them, in the style's colours, over whatever the painter's device
/// holds. The painter is left as it was handed.
///
/// @param painter the painter, its transform from the image's pixels to its device.
/// @param scene the scene.
/// @param layout the scene's layout under the style.
/// @param style the style.
AFTERNIGHTCONTROLS_API void paintAnnotations(QPainter& painter, const AnnotationScene& scene,
                                             const AnnotationLayout& layout, const AnnotationStyle& style);

} // namespace an::ui
