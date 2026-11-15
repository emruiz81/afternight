// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The readout's facts of an image: its size, its kind of samples, the memory it takes and its channels.

#include "AfterNightControlsExport.h"
#include "components/AnReadoutCard.h"
#include "components/ReadoutFigures.h"

#include <QString>

#include <verdigris/wobjectdefs.h>

namespace an::ui
{

class AnIconButton;
class KeyValueRows;

/// The Image card: Dimensions, Format, Size in memory and Channels as key and value rows a key-value row high, and in
/// its header the FITS Header, EXIF Viewer and Summary windows' tools. Folded, its header carries the facts in one
/// line.
class AFTERNIGHTCONTROLS_API AnImageFactsCard final : public AnReadoutCard
{
    W_OBJECT(AnImageFactsCard)

public:
    explicit AnImageFactsCard(QWidget* parent = nullptr);
    ~AnImageFactsCard() override;

    void setFacts(const ImageFactsReadout& facts);
    [[nodiscard]] const ImageFactsReadout& facts() const noexcept;

    /// A row's value as shown, by its key: "Dimensions", "Format", "Size in memory", "Channels".
    [[nodiscard]] QString value(const QString& key) const;

    /// The rows' widget.
    [[nodiscard]] QWidget* rows() const noexcept;

    [[nodiscard]] AnIconButton* fitsHeaderButton() const noexcept;
    [[nodiscard]] AnIconButton* exifButton() const noexcept;
    [[nodiscard]] AnIconButton* summaryButton() const noexcept;

void fitsHeaderRequested() W_SIGNAL(fitsHeaderRequested) void exifRequested()
    W_SIGNAL(exifRequested) void summaryRequested() W_SIGNAL(summaryRequested)

        private : void refill();

    ImageFactsReadout m_facts;
    KeyValueRows* m_rows = nullptr;
    AnIconButton* m_fitsHeader = nullptr;
    AnIconButton* m_exif = nullptr;
    AnIconButton* m_summary = nullptr;
};

} // namespace an::ui
