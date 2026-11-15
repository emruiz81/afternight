// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A task's thumbnail as the rail shows it: its target's picture and where its run stands.

#include "AfterNightControlsExport.h"
#include "components/RailItem.h"
#include "theme/NightImage.h"

#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QPointF>

#include <functional>

class QObject;
class QPainter;

namespace an::ui
{

/// Paints a task in a 38 px box: its target's picture, 30 px with 6 px corners, in the middle; around it, only while
/// the task runs, a ring 2 px thick - the accent's track and the accent's arc, clockwise from the top, as far as the
/// run has come, or a quarter that turns when the run says nothing of it; at the lower right the process's glyph on a
/// 14 px disc of the fourth surface ringed by the rail's; at the upper right, for a run that ended while the task was
/// not shown, a 13 px dot - success with a check, a failure as a rounded square with a cross. A floating task's picture
/// is drawn at 45 % and the window glyph takes the process's place; a task with no target image shows the process's
/// glyph on a tile of the third surface. The state is never told by colour alone.
///
/// It keeps the picture and shows it under Night in its Night form, made off the GUI thread (`NightPicture`): the
/// picture's place is empty until the form is kept, when the owner is told to paint again.
class AFTERNIGHTCONTROLS_API AnTaskThumbnail
{
public:
    /// What a thumbnail is painted with besides its item.
    struct Look
    {
        QIcon glyph; ///< The process's glyph.
        QIcon windowGlyph; ///< The glyph of a task in a window of its own.
        qreal turn = 0.0; ///< How far a turning arc has turned, 0 to 1.
        bool hovered = false;
        const QObject* owner = nullptr; ///< What paints the thumbnail: told to paint again when a Night form is kept.
        std::function<void()> repaint;
    };

    /// Holds the picture of the task's target; a null picture holds none.
    void setPicture(const QImage& picture);

    [[nodiscard]] bool hasPicture() const;

    /// The picture as it is shown now - under Night its Night form, or a null pixmap while that is made - for a list
    /// that shows the task beside its name. `repaint` is called when the form is kept, if `owner` still lives.
    [[nodiscard]] QPixmap shownPicture(const QObject* owner, const std::function<void()>& repaint) const;

    /// Paints the task's box with its top left at `topLeft`.
    void paint(QPainter& painter, const QPointF& topLeft, const RailItem& item, const Look& look) const;

private:
    NightPicture m_picture;
    qint64 m_pictureKey = 0;
};

} // namespace an::ui
