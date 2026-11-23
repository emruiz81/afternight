// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What an image view shows around its image that a part outside it may show instead: where its toolbar is, the actions
/// and choices a toolbar shows, the steps of its history, and the notice a run lays over it while it writes the image.
///
/// The view (`controls/ImageViewControl.h`) and its history (`controls/ImageViewCompanions.h`) answer in these terms,
/// and the parts that show them - the floating toolbar, the history strip - read nothing else of the view, so they
/// reach no engine header.

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class QAbstractButton;
class QAction;
class QComboBox;
class QMenu;

namespace an::ui
{

/// Where a view's toolbar is.
enum class ViewChromeMode : std::uint8_t
{
    /// The toolbar is the view's first row. The default.
    Embedded,

    /// The view builds no toolbar row; a part outside it shows its actions (`ImageViewControl::chromeActions`).
    External
};

/// What a toolbar shows of a view: the view's own actions and choices, the same objects in either chrome mode, so that
/// whatever shows them shows the view's states, shortcuts and tool tips and triggers what the view's own row triggers.
///
/// A member is null where the view's mode has none - a preview has no history, a standard view no snapshot - and a
/// choice that is a widget is null while the view does not offer it: a real-time preview's display mode until it offers
/// its autostretch controls, a static preview's presentation choices while its owner decides them. An action the view
/// offers only sometimes - Plate Solve before a solution, Annotate Image after one - stays and is hidden.
struct ViewChromeActions
{
    // The zoom.
    QAction* fit = nullptr;
    QAction* zoomIn = nullptr;
    QAction* zoomOut = nullptr;
    QAction* zoom100 = nullptr;
    QAction* maximize = nullptr;

    // How the image is presented.
    QComboBox* channel = nullptr;
    QComboBox* displayMode = nullptr;
    QAction* autostretchSettings = nullptr;

    // The history.
    QAction* undo = nullptr;
    QAction* redo = nullptr;
    QAction* history = nullptr;

    // The windows over the image.
    QAction* statistics = nullptr;
    QAction* histogram = nullptr;
    QAction* exif = nullptr;
    QAction* fitsHeader = nullptr;
    QAction* summary = nullptr;

    // Astrometry.
    QAction* showAstrometry = nullptr;
    QMenu* astrometryMenu = nullptr;
    QAction* astrometrySettings = nullptr;
    QAction* astrometryInfo = nullptr;
    QAction* annotate = nullptr;
    QAction* plateSolve = nullptr;

    // A preview's.
    QAction* switchImage = nullptr;
    QAbstractButton* previewArea = nullptr;
    QAction* takeSnapshot = nullptr;
    QAction* statisticsOverlay = nullptr;

    [[nodiscard]] bool operator==(const ViewChromeActions&) const = default;
};

/// One step of a view's history, as a strip of its steps shows it.
struct ViewHistoryStep
{
    /// The step's name: "Curves Transformation", "Image read from a file".
    std::string name;

    /// What the step did, as the history window describes it.
    std::string description;

    /// Whether the step was inherited from the image this view's image was made from: it is shown, never restored.
    bool inherited = false;

    [[nodiscard]] bool operator==(const ViewHistoryStep&) const = default;
};

/// The steps of a view's history as its window lists them, and the one the view shows.
struct ViewHistorySteps
{
    /// The inherited steps first, oldest first, then the view's own from the state it was created with.
    std::vector<ViewHistoryStep> steps;

    /// The index in `steps` of the step the view shows; 0 when there are none.
    std::size_t current = 0u;

    [[nodiscard]] bool operator==(const ViewHistorySteps&) const = default;
};

/// What a view lays over its image while a run writes it: the image dimmed under a scrim, the toolbar's actions
/// disabled, and a pill at its centre that names the run and offers the way to it.
struct ProcessingNotice
{
    /// What runs: "Non Local Bayes is applying".
    QString text;

    /// How far the run has come, from 0 to 1, when it can tell.
    std::optional<double> progress;

    /// The label of the pill's button, "Show"; no button when it is empty or `action` is.
    QString actionLabel;

    /// What the button does: show the run.
    std::function<void()> action;
};

} // namespace an::ui
