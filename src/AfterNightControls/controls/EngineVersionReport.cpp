// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The product's version, and the engine's version and libraries as the engine reports them.

#include "stdafx.hpp"
#include "controls/EngineVersionReport.h"

#include <AfterNightEngine/Core/DependencyReport.h>
#include <AfterNightEngine/Core/Version.h>

#include <QCoreApplication>

#include <array>
#include <string_view>

namespace an::ui
{

namespace
{

/// How a library the engine names is shown, and where it is published.
struct LibraryEntry
{
    std::string_view name;
    const char* label;
    const char* url;
};

constexpr std::array<LibraryEntry, 13u> kLibraries{{
    {"exiv2", "Exiv2", "https://exiv2.org/"},
    {"ffmpeg", "FFmpeg", "https://ffmpeg.org/"},
    {"fftw3", "FFTW", "http://www.fftw.org/"},
    {"gsl", "GNU Scientific Library", "https://www.gnu.org/software/gsl/"},
    {"highway", "Highway", "https://github.com/google/highway"},
    {"libjpeg-turbo", "libjpeg-turbo", "https://libjpeg-turbo.org/"},
    {"libpng", "libpng", "http://www.libpng.org/pub/png/libpng.html"},
    {"libraw", "LibRaw", "https://www.libraw.org/"},
    {"libtiff", "LibTIFF", "https://libtiff.gitlab.io/libtiff/"},
    {"nlohmann-json", "JSON for Modern C++", "https://github.com/nlohmann/json"},
    {"onetbb", "oneTBB", "https://uxlfoundation.github.io/oneTBB/"},
    {"openfits", "OpenFITS", "https://github.com/astroformats/openfits"},
    {"openxisf", "OpenXISF", "https://github.com/astroformats/openxisf"},
}};

[[nodiscard]] const LibraryEntry* findLibrary(const std::string_view name) noexcept
{
    for (const LibraryEntry& entry : kLibraries)
    {
        if (entry.name == name)
        {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

QString productVersion()
{
    return QCoreApplication::applicationVersion().trimmed();
}

QString engineVersionText()
{
    return QStringLiteral("%1 (interface generation %2)")
        .arg(QString::fromLatin1(an::engineVersionString()))
        .arg(an::engineAbiGeneration());
}

std::vector<EngineLibrary> engineLibraries()
{
    std::vector<an::DependencyVersion> report(an::engineDependencyCount());
    if (!an::engineDependencies(report).ok())
    {
        return {};
    }

    std::vector<EngineLibrary> libraries;
    libraries.reserve(report.size());
    for (const an::DependencyVersion& dependency : report)
    {
        const QString name = QString::fromLatin1(dependency.name == nullptr ? "" : dependency.name);
        const LibraryEntry* known = findLibrary(dependency.name == nullptr ? std::string_view() : dependency.name);
        libraries.push_back(EngineLibrary{.name = name,
                                          .label = known != nullptr ? QString::fromLatin1(known->label) : name,
                                          .url = known != nullptr ? QString::fromLatin1(known->url) : QString(),
                                          .linkage = QString::fromLatin1(an::dependencyLinkageName(dependency.linkage)),
                                          .compiled = QString::fromLatin1(dependency.compiled.text).trimmed(),
                                          .loaded = QString::fromLatin1(dependency.loaded.text).trimmed()});
    }
    return libraries;
}

QString engineLibraryVersionText(const EngineLibrary& library)
{
    if (library.compiled.isEmpty() && library.loaded.isEmpty())
    {
        return QStringLiteral("—");
    }
    if (library.compiled.isEmpty())
    {
        return library.loaded;
    }
    if (library.loaded.isEmpty() || library.loaded == library.compiled)
    {
        return library.compiled;
    }
    return QStringLiteral("%1 (loaded %2)").arg(library.compiled, library.loaded);
}

} // namespace an::ui
