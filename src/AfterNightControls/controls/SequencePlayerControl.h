// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The player of a sequence view: the toolbar under the image that shows a frame, steps between frames and plays them,
/// over the sequence the view's document states.
///
/// A frame is read on the thread pool from a source the player keeps open for the sequence it shows, and shown on the
/// GUI thread through the view's `replaceImage` only while it is the frame last asked for; playback is a timer on the
/// GUI thread that asks for the next frame when the one before has been shown. Nothing the player does waits on the
/// GUI thread.
///
/// **The source is kept, and let go.** Opening a video indexes every packet of its stream, so the player keeps the
/// sequence's source open between frames, under the engine context's lease. It lets the source go when the view shows
/// another sequence, when the view closes - through `shutdown` - and when the player goes; a read under way closes it
/// when it lands.
#pragma once

#include "AfterNightControlsExport.h"
#include "controls/ImageViewCompanions.h"
#include "controls/ImageViewControlListener.h"
#include "controls/QSliderSpinBox.h"
#include "session/CommitTicket.h"
#include "session/ImageDocument.h"

#include <QToolBar>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class ImageViewControl;
class QAction;
class QSpinBox;
class QTimer;
class QWidgetAction;

namespace an::ui::detail
{
class SequenceFeed;
} // namespace an::ui::detail

/// The player of one sequence view, laid out under its image. Every member is called on the GUI thread.
class AFTERNIGHTCONTROLS_API SequencePlayerControl final : public QToolBar,
                                                           public an::ui::IViewSequencePlayer,
                                                           public ImageViewControlListener
{
public:
    /// A player over the sequence the view's document states, showing the frame the document states.
    ///
    /// @param view the view; it outlives the player, which it owns.
    explicit SequencePlayerControl(ImageViewControl& view);

    /// Stops playback and lets the source go.
    ~SequencePlayerControl() override;

    SequencePlayerControl(const SequencePlayerControl&) = delete;
    SequencePlayerControl& operator=(const SequencePlayerControl&) = delete;

    // ---- The view's seam.

    [[nodiscard]] QWidget* widget() override;
    [[nodiscard]] bool isPlaying() const override;
    void pause() override;
    void resume() override;
    void stop() override;
    [[nodiscard]] std::uint64_t currentFrame() const override;
    void setCurrentFrame(std::uint64_t frame, std::function<void()> shown) override;
    void setEnabled(bool enabled) override;
    void shutdown() override;
    [[nodiscard]] an::Status saveTo(const QString& path) override;

    // ---- What the view tells its listeners.

    /// The view shows another document: the player follows the sequence it states.
    void onImageLoaded(ImageViewControl* ctrl) override;

    // ---- The player's own.

    /// How many frames the sequence holds.
    [[nodiscard]] std::uint64_t frameCount() const;

    /// Shows the next frame, as the Next button does; at the last frame nothing changes.
    void showNextFrame();

    /// Shows the previous frame, as the Previous button does; at the first frame nothing changes.
    void showPreviousFrame();

    /// The frames per second playback shows.
    [[nodiscard]] double framesPerSecond() const;

    /// Plays at another rate; playback under way continues at it.
    ///
    /// @param framesPerSecond the rate, from one to sixty.
    void setFramesPerSecond(double framesPerSecond);

    /// Shows or hides the frames-per-second control, which is hidden unless a caller shows it.
    ///
    /// @param visible whether it is shown.
    void setFramesPerSecondControlVisible(bool visible);

    /// Enables or disables the Play button, with the tooltip that says why.
    ///
    /// @param enable whether it answers.
    /// @param tooltip what it says.
    void setPlayButtonEnabled(bool enable, const QString& tooltip = QString());

    /// How many frames the player has shown since it was made.
    [[nodiscard]] std::uint64_t framesShown() const;

    /// Whether no frame the player asked for is still on its way.
    [[nodiscard]] bool isIdle() const;

    /// Whether the player keeps a source of the sequence open.
    [[nodiscard]] bool holdsSource() const;

private:
    /// Follows the sequence the view's document states: a new source for another sequence, and the frame it states.
    void followDocument();

    /// Asks for a frame: read on the thread pool and shown when it lands, if it is still the frame last asked for.
    void requestFrame(std::uint64_t frame, std::function<void()> shown);

    /// What the GUI thread does with a frame a worker read.
    void frameRead(std::uint64_t frame, an::ui::CommitTicket ticket, an::Result<an::consumer::ImageAsset>&& read,
                   std::function<void()> shown);

    /// The playback timer's tick: the next frame, or the end of playback.
    void playbackTick();

    void startPlayback();
    void pausePlayback();
    void stopPlayback();
    void onPlayTriggered();
    void onFrameControlChanged(int value);
    void onFramesPerSecondChanged(int value);
    void updateControls();

    ImageViewControl& m_View;
    std::shared_ptr<an::ui::detail::SequenceFeed> m_Feed;
    an::ui::RequestTracker m_Requests;
    QTimer* m_PlayTimer = nullptr;
    QAction* m_PlayAction = nullptr;
    QAction* m_StopAction = nullptr;
    QAction* m_PreviousAction = nullptr;
    QAction* m_NextAction = nullptr;
    QSliderSpinBoxUPtr m_FrameControl;
    std::unique_ptr<QSpinBox> m_FramesPerSecondControl;
    QWidgetAction* m_FramesPerSecondAction = nullptr;
    std::uint64_t m_DocumentIdentity = 0u;
    std::uint64_t m_Current = 0u;
    std::uint64_t m_Count = 0u;
    std::uint64_t m_Asked = 0u;
    std::uint64_t m_Landed = 0u;
    std::uint64_t m_Shown = 0u;
    bool m_Playing = false;
    bool m_ShutDown = false;
    bool m_UpdatingControls = false;
};
