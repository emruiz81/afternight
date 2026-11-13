// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The versions the about dialog, the system information dialog, the dashboard and the extension manager show: the
/// product's, which the application states, and the engine's and its libraries', which the engine reports of itself.
///
/// The engine states its own version and lists the libraries it uses, each with the version its headers declared and
/// the version the loaded copy reports. The product's version is the application's to state, through
/// `QCoreApplication::applicationVersion`, and nothing here names one.
#pragma once

#include "AfterNightControlsExport.h"

#include <QString>

#include <vector>

namespace an::ui
{

/// The product's version, as the application states it; empty in a process that states none.
///
/// @return the version.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString productVersion();

/// The engine's version and binary-interface generation, as one line: "1.0.0 (interface generation 1)".
///
/// @return the line, from what the loaded engine reports.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString engineVersionText();

/// One library the engine uses, as its report states it.
struct EngineLibrary
{
    /// The engine's name for it, as `openfits` or `libjpeg-turbo`.
    QString name;

    /// What it is called where it is shown, as `OpenFITS`; the engine's name for one this table does not know.
    QString label;

    /// Where the library is published, or empty for one this table does not know.
    QString url;

    /// How it is linked: header-only, a static archive or a shared library.
    QString linkage;

    /// The version its headers declared when the engine was built; may be empty.
    QString compiled;

    /// The version the copy loaded into this process reports; may be empty.
    QString loaded;
};

/// The libraries the engine reports, in the order of its report.
///
/// @return the libraries; none when the report cannot be read.
[[nodiscard]] AFTERNIGHTCONTROLS_API std::vector<EngineLibrary> engineLibraries();

/// How a library's version is shown: the version its headers declared, with the loaded copy's beside it when that
/// differs, and a dash when neither is known.
///
/// @param library the library.
/// @return the text.
[[nodiscard]] AFTERNIGHTCONTROLS_API QString engineLibraryVersionText(const EngineLibrary& library);

} // namespace an::ui
