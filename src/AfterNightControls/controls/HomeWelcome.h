// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// Home with nothing to show yet - no recent file: on a quiet star field, the logo and the version; three cards to
/// start with - Open an image, Stack your frames, Read the guide - and under them one quiet line, "Projects are
/// optional. Without one, AfterNight works as it always has.", with New project...; a drop zone for images and
/// sequences with Open file; and the six things AfterNight does that the dashboard's first-run panel shows. A file
/// dragged over it lights the drop zone, and a file dropped on it is opened as a file dropped on the dashboard is.

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <verdigris/wobjectdefs.h>

#include <memory>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QPaintEvent;

namespace an::ui
{

class AFTERNIGHTCONTROLS_API HomeWelcome : public QWidget
{
    W_OBJECT(HomeWelcome)

public:
    explicit HomeWelcome(QWidget* parent = nullptr);
    ~HomeWelcome() override;

    /// Takes the icons the application's icon provider gives now, in the tokens' colours in force.
    void refreshIcons();

    /// Whether a file is being dragged over the welcome, so its drop zone is lit.
    [[nodiscard]] bool isDropHighlighted() const noexcept;

    void openFileRequested() W_SIGNAL(openFileRequested);
    void imageIntegrationRequested() W_SIGNAL(imageIntegrationRequested);
    void openHelpRequested() W_SIGNAL(openHelpRequested);
    void newProjectRequested() W_SIGNAL(newProjectRequested);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setDropHighlighted(bool highlighted);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace an::ui
