// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The settings a project starts from, as the settings store holds them: where a new project is offered, whether an
/// integration job makes one, how a history's steps are compressed and whether the project used last is opened at the
/// start. The Projects page shows and stores them; the New Project dialog and the project service read them.
#pragma once

#include "AfterNightControlsExport.h"
#include "app/FilePolicies.h"
#include "controls/ImageHistoryStepCodec.h"

#include <QString>

#include <optional>

namespace an::ui
{

/// The folder a new project is offered in when the settings name none: `AfterNight Projects` in the person's
/// documents. The folder is named, never made.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString defaultProjectsLocation();

/// The compression a name states - `none`, `lz4` or `zstd`, as `history::stepCompressionName` writes them, compared
/// after trimming and without case; nothing for any other text.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::optional<history::StepCompression>
stepCompressionFromName(const QString& name);

/// Whether a name is one a project takes: its folder is named by it, so it is a name a folder can take on every
/// system the application runs on - not empty, no blank at either end, no dot at its end, none of `< > : " / \ | ? *`
/// nor a control character, none of the device names Windows reserves (`CON`, `NUL`, `COM1`...), and no longer than
/// 128 characters.
[[nodiscard]] AFTERNIGHTCONTROLS_API bool isValidProjectName(const QString& name);

/// What the settings store states about projects.
struct ProjectSettings
{
    /// The folder a new project is offered in; never empty.
    QString defaultLocation;

    /// Whether an integration job run with no project open makes one in its output folder.
    bool createForIntegrationJobs = true;

    /// How a history's steps are compressed, which a new project starts from.
    history::StepCompression historyCompression = history::StepCompression::Lz4;

    /// Whether the project used last is opened as the application starts.
    bool openLastOnStartup = false;

    /// Field-wise equality.
    [[nodiscard]] friend bool operator==(const ProjectSettings& left, const ProjectSettings& right) = default;
};

/// Reads the project settings: a location that is empty or blank reads as `defaultProjectsLocation()`, a compression
/// that is none of the three names as LZ4, and a switch that is neither `1` nor `0` as its default. No store reads as
/// the defaults.
///
/// @param read the settings store.
/// @return the settings.
[[nodiscard]] AFTERNIGHTCONTROLS_API ProjectSettings readProjectSettings(const SettingReader& read);

/// Stores the project settings, in the texts `readProjectSettings` reads.
///
/// @param settings the settings.
/// @param write the settings store.
AFTERNIGHTCONTROLS_API void writeProjectSettings(const ProjectSettings& settings, const SettingWriter& write);

} // namespace an::ui
