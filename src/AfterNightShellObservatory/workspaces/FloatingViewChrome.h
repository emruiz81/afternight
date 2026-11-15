// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The small floating toolbar of an image in a window of its own.

#include "AfterNightShellObservatoryExport.h"

#include "views/ViewChromeBinder.h"

#include <QObject>
#include <QPointer>

#include <memory>
#include <vector>

class ImageViewControl;
class QEvent;

namespace an::ui
{
class AnFloatingToolbar;
} // namespace an::ui

namespace an::ui::observatory
{

/// Gives a view that floats in a window of its own the small floating toolbar - Fit, Zoom Out, the zoom, Zoom In, the
/// display mode and More - 8 px above its bottom, bound to that view alone, and takes it away when the view docks
/// again. A view's chrome is outside it in this shell, so a floating window would otherwise show no tool of its view.
class AFTERNIGHTSHELLOBSERVATORY_API FloatingViewChrome final : public QObject
{
public:
    explicit FloatingViewChrome(QObject* parent = nullptr);
    ~FloatingViewChrome() override;

    FloatingViewChrome(const FloatingViewChrome&) = delete;
    FloatingViewChrome& operator=(const FloatingViewChrome&) = delete;
    FloatingViewChrome(FloatingViewChrome&&) = delete;
    FloatingViewChrome& operator=(FloatingViewChrome&&) = delete;

    /// What fills a view's menu when a toolbar's More is clicked.
    void setMenuFiller(an::ui::shell::ViewChromeBinder::MenuFiller filler);

    /// Gives every view of `views` that floats its toolbar, and takes it from one that docked or is not listed.
    void update(const std::vector<ImageViewControl*>& views);

    /// The toolbar a floating view shows; null for a view that does not float.
    [[nodiscard]] an::ui::AnFloatingToolbar* toolbarOf(const ImageViewControl* view) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry
    {
        QPointer<ImageViewControl> view;
        QPointer<an::ui::AnFloatingToolbar> toolbar;
        std::unique_ptr<an::ui::shell::ViewChromeBinder> binder;
    };

    void place(Entry& entry) const;

    an::ui::shell::ViewChromeBinder::MenuFiller m_filler;
    std::vector<Entry> m_entries;
};

} // namespace an::ui::observatory
