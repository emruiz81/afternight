// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What a workspace shows with no image docked in it: the images floating in windows of their own, or nothing.

#include "AfterNightShellObservatoryExport.h"

#include <QWidget>

#include <functional>
#include <vector>

class ImageViewControl;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace an::ui::observatory
{

/// The quiet page of a workspace with no docked image, centred. While images float: "The images of this workspace are
/// floating", a row per floating image - a small picture of it, its full name and `Dock here` - then `Dock all here`,
/// "or drag a window back here" and `Open file...`. With none listed: "This workspace is empty", `Open file...` and "or
/// drag an image here".
class AFTERNIGHTSHELLOBSERVATORY_API FloatingImagesPage final : public QWidget
{
public:
    /// What the page's buttons do.
    struct Dependencies
    {
        std::function<void(ImageViewControl& view)> dockHere;
        std::function<void()> dockAll;
        std::function<void()> openFile;
    };

    /// The side of a row's picture.
    static constexpr int kPictureSide = 40;

    explicit FloatingImagesPage(Dependencies dependencies, QWidget* parent = nullptr);

    /// Lists the floating images, a row each, in the order given; with none, the page says the workspace is empty.
    void setViews(const std::vector<ImageViewControl*>& views);
    /// What the page says at its top, and under its rows.
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString hint() const;
    [[nodiscard]] const std::vector<ImageViewControl*>& views() const noexcept;

    /// The `Dock here` of a listed image; null when it is not listed.
    [[nodiscard]] QPushButton* dockHereButton(const ImageViewControl* view) const;
    [[nodiscard]] QPushButton* dockAllButton() const noexcept;
    [[nodiscard]] QPushButton* openFileButton() const noexcept;
    /// The name a listed image's row shows; empty when it is not listed.
    [[nodiscard]] QString rowName(const ImageViewControl* view) const;

private:
    Dependencies m_dependencies;
    QVBoxLayout* m_rows = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_dockAll = nullptr;
    QPushButton* m_openFile = nullptr;
    std::vector<ImageViewControl*> m_views;
    std::vector<QPushButton*> m_dockHere;
    std::vector<QLabel*> m_names;
};

} // namespace an::ui::observatory
