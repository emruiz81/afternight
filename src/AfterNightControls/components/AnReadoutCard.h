// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A card of the readout: a header that folds it, and a body of figures that may be stale while they are measured
/// again.

#include "AfterNightControlsExport.h"

#include <QAbstractButton>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <verdigris/wobjectdefs.h>

class QEvent;
class QHBoxLayout;
class QKeyEvent;
class QPaintEvent;
class QVBoxLayout;

namespace an::ui
{

class AnIconButton;

/// A card's header: a section header high, its caps title at the left, a summary after it and its tools at the right.
/// It is a button - a click, Space or Enter folds the card or opens it - and tells assistive technology its title, with
/// "folded" when the card is.
class AFTERNIGHTCONTROLS_API AnReadoutCardHeader final : public QAbstractButton
{
    W_OBJECT(AnReadoutCardHeader)

public:
    explicit AnReadoutCardHeader(const QString& title, QWidget* parent = nullptr);

    [[nodiscard]] QString title() const;

    /// What the card says of itself at the header's right, before its tools; elided when there is no room.
    void setSummary(const QString& summary);
    [[nodiscard]] QString summary() const;

    /// The row the tools sit in, right-aligned.
    [[nodiscard]] QHBoxLayout* toolRow() const noexcept;

    /// Whether the card it heads is folded, which its accessible name says.
    void setFolded(bool folded);

    /// The rectangle the summary is drawn in, which ends where the tools begin.
    [[nodiscard]] QRect summaryRect() const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void describe();
    void applyMetrics();

    QString m_summary;
    QHBoxLayout* m_tools = nullptr;
    bool m_folded = false;
};

/// The readout's card: the surface `surface2` with a 12 px radius, a header that folds it and a body under it, a
/// panel's padding at its sides and foot. A value being measured again is stale: a card draws it at 55 % until the new
/// one lands, and never blanks it.
class AFTERNIGHTCONTROLS_API AnReadoutCard : public QWidget
{
    W_OBJECT(AnReadoutCard)

public:
    explicit AnReadoutCard(const QString& title, QWidget* parent = nullptr);

    [[nodiscard]] QString title() const;

    /// The header's summary.
    void setSummary(const QString& summary);
    [[nodiscard]] QString summary() const;

    /// Adds a tool at the header's right, after the ones added before.
    ///
    /// @param iconKey the product's icon it shows.
    /// @param text what it does, its tooltip and accessible name.
    /// @return the tool, which the card owns.
    AnIconButton* addHeaderTool(const QString& iconKey, const QString& text);

    /// Adds another widget at the header's right, after what was added before: a range picker.
    void addHeaderWidget(QWidget* widget);

    /// The header.
    [[nodiscard]] AnReadoutCardHeader* header() const noexcept;

    /// The body's layout, a column a panel's gap apart.
    [[nodiscard]] QVBoxLayout* bodyLayout() const noexcept;

    /// The widget holding the body.
    [[nodiscard]] QWidget* body() const noexcept;

    /// Whether the body is hidden under the header.
    [[nodiscard]] bool isFolded() const noexcept;
    void setFolded(bool folded);

    /// Whether its values are being measured again, and drawn at 55 %.
    [[nodiscard]] bool isStale() const noexcept;
    void setStale(bool stale);

    /// Told when the card is folded or opened, by the person or by `setFolded`.
void foldedChanged(bool folded) W_SIGNAL(foldedChanged, folded)

    protected :
    /// Told when the stale state changed, for a card whose values are children of their own.
    virtual void staleChanged();

    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyMetrics();

    AnReadoutCardHeader* m_header = nullptr;
    QWidget* m_body = nullptr;
    QVBoxLayout* m_bodyLayout = nullptr;
    bool m_folded = false;
    bool m_stale = false;
};

} // namespace an::ui
