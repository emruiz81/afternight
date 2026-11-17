// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A workspace's miniature: its arrangement drawn small - each group of its dock area in its place, a group of tabs
/// with its strip, each group painted with the picture of the image it shows.

#include "AfterNightShellObservatoryExport.h"

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <map>
#include <memory>
#include <vector>

class ImageViewControl;
class QPaintEvent;

namespace KDDockWidgets::QtWidgets // NOLINT(readability-identifier-naming): the dock library's own namespace.
{
class MainWindow;
} // namespace KDDockWidgets::QtWidgets

namespace an::ui::observatory
{

/// A group of a workspace's dock area: where it is, as a share of the area, and the images it holds as tabs.
struct MiniatureGroup
{
    /// Its place in the dock area, from 0 to 1 on each side.
    QRectF place;

    /// The images it holds, by their views' names, in their tabs' order.
    QStringList tabs;

    /// The tab shown.
    int current = 0;
};

/// The arrangement of a workspace's dock area.
struct MiniatureLayout
{
    std::vector<MiniatureGroup> groups;
};

/// The arrangement of the views a dock area holds: each group of tabs in its place. Views of `views` the area does not
/// hold are left out.
[[nodiscard]] AFTERNIGHTSHELLOBSERVATORY_API MiniatureLayout
miniatureLayoutOf(KDDockWidgets::QtWidgets::MainWindow& area, const std::vector<ImageViewControl*>& views);

/// An arrangement as a workspace's tile says it: "2 × 2 split" for groups that make a grid, "3 groups" for others,
/// "3 tabs" for one group of tabs, "1 image", or "empty".
[[nodiscard]] AFTERNIGHTSHELLOBSERVATORY_API QString miniatureArrangementText(const MiniatureLayout& layout);

/// The miniature: 176 x 110, the canvas's colour under it, its corners rounded; each group in its place 2 px from its
/// neighbours, a group of more than one tab with its strip - the tabs, the one shown underlined in the accent - over
/// the picture of the image shown, which covers what is left. A picture is shown in Night's form under Night, made off
/// the GUI thread; one not given yet leaves its well.
class AFTERNIGHTSHELLOBSERVATORY_API WorkspaceMiniature final : public QWidget
{
public:
    explicit WorkspaceMiniature(QWidget* parent = nullptr);
    ~WorkspaceMiniature() override;

    void setMiniatureLayout(MiniatureLayout layout);
    [[nodiscard]] const MiniatureLayout& miniatureLayout() const noexcept;

    /// The picture of an image, by its view's name; a null one takes it away.
    void setPicture(const QString& view, const QImage& picture);

    /// How many groups are painted with a picture.
    [[nodiscard]] int picturesShown() const;

    /// Whether a group is drawn with its tab strip: it holds more than one tab.
    [[nodiscard]] bool showsTabStrip(int group) const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui::observatory
