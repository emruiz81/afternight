// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The status line of the Observatory window: the band along the bottom of the centre and the column, which writes the
/// texts `widgets/ImageStatusTexts.h` makes of the active image.

#include "AfterNightShellObservatoryExport.h"

#include <QString>
#include <QWidget>

class ImageViewControl;
class QComboBox;
class QEvent;
class QHBoxLayout;
class QPaintEvent;

namespace an::ui
{
class AnChip;
class AnIconButton;
} // namespace an::ui

namespace an::ui::observatory
{

/// One text of the status line: drawn in the status line's size, mono or not, in the secondary or the tertiary text
/// colour, as the tokens in force say when it paints.
class AFTERNIGHTSHELLOBSERVATORY_API StatusText final : public QWidget
{
public:
    enum class Tone
    {
        Secondary, ///< what the line says
        Tertiary ///< what the line says there is none of
    };

    explicit StatusText(bool mono, QWidget* parent = nullptr);

    void setText(const QString& text, Tone tone = Tone::Secondary);
    [[nodiscard]] const QString& text() const noexcept;
    [[nodiscard]] Tone tone() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    [[nodiscard]] QFont textFont() const;

    QString m_text;
    Tone m_tone = Tone::Secondary;
    bool m_mono = false;
};

/// The band along the bottom of the window, right of the rail: the first surface with a hairline above it, as high as
/// the density's status line. From the left, the selector of the active view (every view by its full name), the image's
/// facts and its zoom, in mono, as `widgets/ImageStatusTexts.h` words them; at the right
/// the right ascension and the declination under the cursor, "RA -" and "Dec -" in the tertiary colour while there is
/// no position to tell, the button that shows or hides the readout, and the Console chip, which opens the Session
/// Console.
class AFTERNIGHTSHELLOBSERVATORY_API StatusLine final : public QWidget
{
public:
    explicit StatusLine(QWidget* parent = nullptr);

    /// The row the line's parts are laid out in.
    [[nodiscard]] QHBoxLayout* row() const noexcept;

    /// Writes the view's facts and its zoom; nothing with no view, or a view with no image yet.
    void showImageInfo(ImageViewControl* view);
    /// Writes the view's zoom.
    void showZoom(ImageViewControl* view);
    void showCelestialCoordinates(double raDegrees, double decDegrees);
    void clearCelestialCoordinates();
    /// Clears the facts, the zoom and the position.
    void clearImageInfo();

    [[nodiscard]] StatusText* factsText() const noexcept;
    [[nodiscard]] StatusText* zoomText() const noexcept;
    [[nodiscard]] StatusText* rightAscensionText() const noexcept;
    [[nodiscard]] StatusText* declinationText() const noexcept;

    /// The selector of the active view, which the window fills and follows.
    [[nodiscard]] QComboBox* viewSelector() const noexcept;
    /// The button that shows or hides the readout column.
    [[nodiscard]] an::ui::AnIconButton* readoutButton() const noexcept;
    /// The Console chip: "Console", and how many warnings there are when there are any.
    [[nodiscard]] an::ui::AnChip* consoleChip() const noexcept;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QHBoxLayout* m_row = nullptr;
    QComboBox* m_selector = nullptr;
    an::ui::AnIconButton* m_readout = nullptr;
    an::ui::AnChip* m_console = nullptr;
    StatusText* m_facts = nullptr;
    StatusText* m_zoom = nullptr;
    StatusText* m_rightAscension = nullptr;
    StatusText* m_declination = nullptr;
};

} // namespace an::ui::observatory
