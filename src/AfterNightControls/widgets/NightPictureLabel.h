// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"
#include "theme/NightImage.h"

#include <QLabel>

namespace an::ui
{

/// A label that shows a picture: as it is, or under Night in its Night form, made off the GUI thread (`NightPicture`).
/// It shows nothing until the form is kept, and shows the picture again in the right form when Night is switched on or
/// off, which reaches it as a change of the application's palette.
class AFTERNIGHTCONTROLS_API NightPictureLabel : public QLabel
{
public:
    explicit NightPictureLabel(QWidget* parent = nullptr);

    /// Shows a picture.
    void setPicture(const QPixmap& picture);

    /// The picture as it was handed over.
    [[nodiscard]] const QPixmap& picture() const
    {
        return m_picture.picture();
    }

protected:
    void changeEvent(QEvent* event) override;

private:
    /// Shows the picture in the form Night asks for, or nothing while that is made.
    void showPicture();

    NightPicture m_picture;
};

} // namespace an::ui
