// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - where a pipeline run writes its intermediates.
//
// The integration pipeline writes a calibrated and an aligned copy of every light - roughly twice the
// light set - and a measurement removes them with the run, or a variance sweep fills the disk. A later
// stage measured on its own needs exactly those intermediates as its input, though, so a run may be
// told to write them into a directory it names and leave them there. That directory is the caller's:
// it is refused unless it is new or empty, so a run never mixes its frames into another run's, and it
// is never removed.
#pragma once

#include <filesystem>
#include <string>

namespace an::bench
{

/// A pipeline run's scratch root.
class PipelineScratch final
{
public:
    /// Creates the root.
    ///
    /// With `kept` empty, the root is `prefix` followed by the process id under the process temporary
    /// directory, emptied first and removed with everything in it when this object is destroyed. With
    /// `kept` named, the root is that directory: created when absent, refused when it already holds
    /// anything, and left in place when this object is destroyed.
    ///
    /// @param prefix the temporary root's name before the process id.
    /// @param kept the caller's directory, as a UTF-8 path, or empty for a temporary root.
    PipelineScratch(const std::string& prefix, const std::string& kept);

    /// Removes a temporary root and everything in it; leaves a kept one.
    ~PipelineScratch();

    PipelineScratch(const PipelineScratch&) = delete;
    PipelineScratch& operator=(const PipelineScratch&) = delete;

    /// Whether the root exists and is empty and ready for the run.
    /// @return true when the run may write into `path()`.
    [[nodiscard]] bool ok() const noexcept
    {
        return m_ok;
    }

    /// Why the root is not ready, naming it; empty when it is.
    /// @return the reason.
    [[nodiscard]] const std::string& error() const noexcept
    {
        return m_error;
    }

    /// Whether the root is the caller's and outlives this object.
    /// @return true for a kept root.
    [[nodiscard]] bool kept() const noexcept
    {
        return m_kept;
    }

    /// The root.
    /// @return the root as a path string.
    [[nodiscard]] std::string path() const
    {
        return m_path.string();
    }

private:
    std::filesystem::path m_path;
    std::string m_error;
    bool m_kept = false;
    bool m_ok = false;
};

} // namespace an::bench
