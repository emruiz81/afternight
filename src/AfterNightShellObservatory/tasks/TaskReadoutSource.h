// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// A task's preview as the readout reads it.

#include "AfterNightShellObservatoryExport.h"

#include "readout/ReadoutController.h"

#include <QPointer>

#include <cstdint>
#include <functional>
#include <memory>

class ImageRTPreviewControl;

namespace an::ui::observatory
{

/// The frames a task's preview shows, and the image they are previews of, for the compact readout of the task's page:
/// each frame the process's preview view shows, settled or made while a control is held, numbered as it is shown; the
/// target's image as the original. A process gone, or with its preview off, shows none.
///
/// The source may go while its process goes: the task model hears of a window as the window's base class is destroyed,
/// after its preview framework's part is. So the source never calls the process as it goes - the listener it handed the
/// process holds a flag that says the source is gone - and reads the process only through a guard. Used on the GUI
/// thread.
class AFTERNIGHTSHELLOBSERVATORY_API TaskReadoutSource final : public an::ui::shell::IReadoutFrameSource
{
public:
    explicit TaskReadoutSource(ImageRTPreviewControl& process);
    ~TaskReadoutSource() override;

    TaskReadoutSource(const TaskReadoutSource&) = delete;
    TaskReadoutSource& operator=(const TaskReadoutSource&) = delete;
    TaskReadoutSource(TaskReadoutSource&&) = delete;
    TaskReadoutSource& operator=(TaskReadoutSource&&) = delete;

    [[nodiscard]] std::optional<Frame> latestFrame() const override;
    [[nodiscard]] std::shared_ptr<const an::consumer::ImageAsset> original() const override;
    [[nodiscard]] ImageViewControl* target() const override;
    void setFrameListener(std::function<void()> listener) override;

private:
    QPointer<ImageRTPreviewControl> m_process;
    std::function<void()> m_listener;
    bool m_interactive = false;
    /// Whether the source is still there, read by the listener the process holds.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace an::ui::observatory
