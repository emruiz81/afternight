// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// A stand-in for the sequence player the viewer reaches through its companion seam, in this suite, which compiles the
/// viewer's closure rather than linking the Controls library: it shows the frame the document states, plays nothing
/// and writes nothing, and records what the viewer asks of it.

#include "Test_ControlsSeams/ViewCompanionsStub.h"

#include "controls/ImageViewControl.h"

#include <AfterNightEngine/Core/ErrorCode.h>

#include <QWidget>

namespace an::tests::viewer
{

namespace
{

/// A sequence player that shows the frame the document states and plays nothing.
class StandInPlayer final : public an::ui::IViewSequencePlayer
{
public:
    explicit StandInPlayer(::ImageViewControl& view) : m_view(view), m_widget(std::make_unique<QWidget>()) {}

    [[nodiscard]] QWidget* widget() override
    {
        return m_widget.get();
    }

    [[nodiscard]] bool isPlaying() const override
    {
        return m_playing;
    }

    void pause() override
    {
        m_playing = false;
    }

    void resume() override
    {
        m_playing = true;
    }

    void stop() override
    {
        m_playing = false;
    }

    [[nodiscard]] std::uint64_t currentFrame() const override
    {
        const std::optional<an::ui::DocumentSequence> sequence = m_view.document().sequence();
        return sequence.has_value() ? sequence->currentFrame : 0u;
    }

    void setCurrentFrame(std::uint64_t, std::function<void()> shown) override
    {
        if (shown)
        {
            shown();
        }
    }

    void setEnabled(bool enabled) override
    {
        ++(enabled ? companionRecord().playersEnabled : companionRecord().playersDisabled);
    }

    void shutdown() override
    {
        m_playing = false;
    }

    [[nodiscard]] an::Status saveTo(const QString&) override
    {
        return an::Status::failure(an::ErrorCode::Unsupported, "the stand-in player writes nothing");
    }

private:
    ::ImageViewControl& m_view;
    std::unique_ptr<QWidget> m_widget;
    bool m_playing = false;
};

} // namespace

} // namespace an::tests::viewer

namespace an::ui
{

std::unique_ptr<IViewSequencePlayer> createViewSequencePlayer(::ImageViewControl& view)
{
    ++an::tests::viewer::companionRecord().sequencePlayersMade;
    return std::make_unique<an::tests::viewer::StandInPlayer>(view);
}

} // namespace an::ui
