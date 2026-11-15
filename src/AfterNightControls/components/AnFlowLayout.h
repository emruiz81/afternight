// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A layout that places its widgets in a row and wraps them onto the next row when the row is full.

#include "AfterNightControlsExport.h"

#include <QLayout>
#include <QList>
#include <QRect>
#include <QSize>

class QLayoutItem;
class QWidget;

namespace an::ui
{

/// Lays its items out left to right at their size hints, `spacing()` apart, starting a new row - `spacing()` below the
/// one before - when the next item would pass the right edge. Its height follows its width (`heightForWidth`), so a
/// parent layout gives it the rows it needs. An item wider than the layout keeps a row of its own.
class AFTERNIGHTCONTROLS_API AnFlowLayout final : public QLayout
{
public:
    explicit AnFlowLayout(QWidget* parent = nullptr);
    ~AnFlowLayout() override;

    AnFlowLayout(const AnFlowLayout&) = delete;
    AnFlowLayout& operator=(const AnFlowLayout&) = delete;

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    [[nodiscard]] QLayoutItem* takeAt(int index) override;

    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;

private:
    /// Places the items in a rectangle, or only measures when `place` is false; the height the rows take.
    int arrange(const QRect& rect, bool place) const;

    QList<QLayoutItem*> m_items;
};

} // namespace an::ui
