// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a floating toolbar keeps clear of: the player strips of the sequences under it.

#include "AfterNightShellObservatoryExport.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>

#include <functional>
#include <vector>

class QEvent;
class QWidget;

namespace an::ui::observatory
{

/// Finds the player strips a floating toolbar is to keep clear of, and says when they change. A sequence keeps its
/// player at the bottom of its own view, and a toolbar floats centred at the bottom of the area of views it is placed
/// over, so it would cover a player under it; its host hands it the strips this finds
/// (`AnFloatingToolbar::placeOver`), and it is lifted above those it would cover. A host places its toolbar again when
/// told: a strip found is shown, hidden, moved or resized, or a view found is shown, hidden, moved, resized or given
/// its player.
class AFTERNIGHTSHELLOBSERVATORY_API ToolbarClearance final : public QObject
{
public:
    /// @param changed runs when a strip or a view it found changes as above; never while it runs already.
    explicit ToolbarClearance(std::function<void()> changed, QObject* parent = nullptr);
    ~ToolbarClearance() override;

    ToolbarClearance(const ToolbarClearance&) = delete;
    ToolbarClearance& operator=(const ToolbarClearance&) = delete;
    ToolbarClearance(ToolbarClearance&&) = delete;
    ToolbarClearance& operator=(ToolbarClearance&&) = delete;

    /// The player strips shown of the views in `area` - `area` itself when it is a view - in the coordinates of `over`,
    /// the widget a toolbar floats in; a view in another window is left out. Watches those views and their strips
    /// from then on.
    [[nodiscard]] QList<QRect> playersIn(QWidget& area, const QWidget& over);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void watch(QObject& object);

    std::function<void()> m_changed;
    std::vector<QPointer<QObject>> m_watched;
    bool m_telling = false;
};

} // namespace an::ui::observatory
