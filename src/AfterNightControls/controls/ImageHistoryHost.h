// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The view a history tracker records and restores, as the tracker sees it: what it shows, and how a recorded state is
/// shown again. The product's image view is one, through the history's companion; a test implements it with no widget.
///
/// Every member is called on the GUI thread.
#pragma once

#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Status.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class QWidget;

namespace an::ui::history
{

/// Told on the GUI thread whether a restored state is now the one shown.
using RestoreDone = std::function<void(const an::Status& shown)>;

/// The view whose states a history tracker records.
class ImageHistoryHost
{
public:
    virtual ~ImageHistoryHost() = default;

    /// The name the view is shown under, which every step records.
    [[nodiscard]] virtual std::string historyName() const = 0;

    /// Shows the view under the name a restored step recorded.
    virtual void setHistoryName(const std::string& name) = 0;

    /// The identity of the document the view shows, or zero for none. A restore is shown only while it is the one the
    /// restore began on.
    [[nodiscard]] virtual std::uint64_t historyDocument() const = 0;

    /// The image the view shows - the still image, or the frame of a sequence on screen - or null.
    [[nodiscard]] virtual std::shared_ptr<const an::consumer::ImageAsset> historyAsset() const = 0;

    /// The sequence the view shows a frame of, or nothing for a single image. Its current frame is the frame on
    /// screen.
    [[nodiscard]] virtual std::optional<DocumentSequence> historySequence() const = 0;

    /// Whether a mosaic frame of the sequence is demosaiced as the view shows it.
    [[nodiscard]] virtual bool historyAutoDebayer() const = 0;

    /// Shows `image`, which a restore read, in place of the image shown now, and says whether it did.
    ///
    /// @param image the restored image, a new asset of the session.
    /// @param done told whether it is shown; may run before this returns.
    virtual void restoreHistoryImage(an::consumer::ImageAsset&& image, RestoreDone done) = 0;

    /// Shows a sequence state: `sequence` when the step showed another sequence than the one shown now, or the
    /// sequence shown now otherwise, at `frameIndex`, showing `frame` there.
    ///
    /// @param sequence the step's sequence when it is another one, or nothing.
    /// @param frameIndex the zero-based frame the step is shown at.
    /// @param frame what the frame shows: the frame the step changed, or the sequence's own frame.
    /// @param done told whether it is shown; may run before this returns.
    virtual void restoreHistorySequence(std::optional<DocumentSequence> sequence, std::uint64_t frameIndex,
                                        an::consumer::ImageAsset&& frame, RestoreDone done) = 0;

    /// What undo and redo would do has changed.
    virtual void historyChanged() = 0;

    /// A step was restored: the view becomes the active one.
    virtual void activateForHistory() = 0;

    /// The parent of a failure the tracker reports while the view exists, or null.
    [[nodiscard]] virtual QWidget* historyDialogParent() const = 0;
};

} // namespace an::ui::history
