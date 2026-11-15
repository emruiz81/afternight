// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "controls/settings/SettingsSearchTable.h"

#include <QCoreApplication>

#include <initializer_list>
#include <utility>

namespace an::ui
{

namespace
{

/// A text of a page, translated in the page's own context as the page translates it.
QString text(const char* page, const char* source)
{
    return QCoreApplication::translate(page, source);
}

/// Texts of a page, translated in its context.
QStringList texts(const char* page, std::initializer_list<const char*> sources)
{
    QStringList translated;
    for (const char* source : sources)
    {
        translated.append(text(page, source));
    }
    return translated;
}

/// Rows of a page: each a label and its section, translated in the page's context.
std::vector<SettingsSearchRow> rows(const char* page,
                                    std::initializer_list<std::pair<const char*, const char*>> sources)
{
    std::vector<SettingsSearchRow> translated;
    for (const auto& [label, section] : sources)
    {
        translated.push_back(SettingsSearchRow{text(page, label), *section == '\0' ? QString() : text(page, section)});
    }
    return translated;
}

} // namespace

std::vector<SettingsSearchPage> settingsSearchPages()
{
    std::vector<SettingsSearchPage> pages;

    constexpr const char* kGeneral = "GeneralSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kGeneral, "General"),
        texts(kGeneral, {"General", "Language", "Save", "Format", "Recent", "Files", "Processes", "Temporary", "Temp",
                         "Import", "Export", "Reset", "Updates", "Extensions", "Startup"}),
        rows(kGeneral, {
                           {"Language", "Basic Settings"},
                           {"Default Save Format", "Basic Settings"},
                           {"Recent Files Limit", "Basic Settings"},
                           {"Recent Processes Limit", "Basic Settings"},
                           {"Confirm before closing unsaved images", "Behavior"},
                           {"Clear temporary files on exit", "Behavior"},
                           {"Check for AfterNight updates on startup", "Updates"},
                           {"Check for extension updates on startup", "Updates"},
                           {"Location", "Temporary Files"},
                       })});

    constexpr const char* kRaw = "RawImageSettingsPage";
    pages.push_back(
        SettingsSearchPage{text(kRaw, "Raw Images"),
                           texts(kRaw, {"Raw", "RAW", "Camera", "White Balance", "Demosaic", "Debayer", "Color Space",
                                        "sRGB", "Adobe RGB", "ProPhoto", "AHD", "VNG", "Rotate"}),
                           rows(kRaw, {
                                          {"Mode", "White Balance"},
                                          {"Auto-rotate based on EXIF orientation", "Loading Behavior"},
                                          {"Demosaic Algorithm", "Demosaic Settings"},
                                      })});

    constexpr const char* kExternal = "ExternalToolsSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kExternal, "External Tools"),
        texts(kExternal, {"External", "Tools", "StarNet", "Star Removal", "DeepSNR", "Noise Reduction"}),
        rows(kExternal, {
                            {"StarNet2", "AI Star Removal and Noise Reduction"},
                            {"DeepSNR", "AI Star Removal and Noise Reduction"},
                        })});

    constexpr const char* kInterface = "InterfaceSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kInterface, "Interface"),
        texts(kInterface,
              {"Interface", "Theme",        "Dark",         "Light",        "Backdrop",      "Material",
               "Mica",      "Acrylic",      "Fluent",       "Contrast",     "High Contrast", "Soft Contrast",
               "Accent",    "Accent Color", "Card Tint",    "Surface Tint", "GroupBox",      "Card Surface",
               "Blue",      "Teal",         "Amber",        "Plum",         "Appearance",    "Animations",
               "Animate",   "Zoom",         "Mouse",        "Wheel",        "Sensitivity",   "Ctrl",
               "Control",   "File Browser", "Native",       "System",       "Layout",        "Lock",
               "Window",    "Size",         "Preset",       "Night",        "Red",           "Density",
               "Compact",   "Comfortable",  "Tint Strength"}),
        rows(kInterface, {
                             {"Colour preset", "Colours and Size"},
                             {"Tint strength", "Colours and Size"},
                             {"Density", "Colours and Size"},
                             {"Red light for dark adaptation", "Colours and Size"},
                             {"Night", "Colours and Size"},
                             {"Start in Night", "Colours and Size"},
                             {"Theme", "Appearance"},
                             {"Theme Contrast", "Appearance"},
                             {"Accent Family", "Appearance"},
                             {"Card Surface Tint", "Appearance"},
                             {"Enable UI animations", "Appearance"},
                             {"Mouse Wheel Zoom Sensitivity", "Image Viewing"},
                             {"Hold Ctrl key to zoom with mouse wheel", "Image Viewing"},
                             {"Use system file browser", "File Browser"},
                             {"Lock dock layout by default", "Window Layout"},
                             {"Remember window size and position", "Window Layout"},
                             {"Shell", "Shell"},
                         })});

    constexpr const char* kExport = "ExportSettingsPage";
    pages.push_back(
        SettingsSearchPage{text(kExport, "Export"),
                           texts(kExport, {"Export", "Save", "TIFF", "XISF", "JPEG", "JPG", "PNG", "Compression",
                                           "Quality", "EXIF", "Metadata", "Checksum", "Zstandard", "LZ4"}),
                           rows(kExport, {
                                             {"Default Compression", "TIFF Settings"},
                                             {"Shuffle the bytes of each sample before compressing", "XISF Settings"},
                                             {"Default Compression", "XISF Settings"},
                                             {"Checksum", "XISF Settings"},
                                             {"Quality", "JPEG Settings"},
                                             {"Compression Level", "PNG Settings"},
                                             {"Embed EXIF metadata in exported files", "Metadata"},
                                             {"Embed processing history in FITS header", "Metadata"},
                                         })});

    constexpr const char* kProcessing = "ProcessingSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kProcessing, "Processing"),
        texts(kProcessing, {"Processing", "Threads", "CPU", "Cores", "Parallel", "Batch", "Preview", "Frame rate",
                            "Battery", "GPU", "CUDA", "Acceleration", "Memory", "RAM", "Performance", "Restart"}),
        rows(kProcessing, {
                              {"Auto", "Parallelism"},
                              {"Max Threads", "Parallelism"},
                              {"Updates per second while dragging", "Real-time Preview"},
                              {"Enable GPU Acceleration", "GPU Acceleration"},
                              {"Preferred GPU", "GPU Acceleration"},
                              {"Auto", "Memory"},
                              {"Memory Limit", "Memory"},
                          })});

    constexpr const char* kLogging = "LoggingSettingsPage";
    pages.push_back(SettingsSearchPage{text(kLogging, "Logging"),
                                       texts(kLogging, {"Logging", "Log", "Debug", "Trace", "Error", "Warning", "File",
                                                        "Rotation", "Level", "Directory", "Size"}),
                                       rows(kLogging, {
                                                          {"Directory", "Log File Location"},
                                                          {"Minimum Level", "Log Level"},
                                                          {"Maximum File Size", "Log Rotation"},
                                                          {"Maximum Files to Keep", "Log Rotation"},
                                                      })});

    constexpr const char* kCatalogs = "CatalogsDataSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kCatalogs, "Catalogs & Data"),
        texts(kCatalogs, {"Catalogs", "Data", "SPCC", "Gaia XP", "Spectrophotometric", "Download", "Cache"}),
        rows(kCatalogs, {
                            {"Catalog root", "Gaia DR3 Astrometric Catalogs"},
                            {"Catalog pack", "Gaia DR3 Astrometric Catalogs"},
                            {"Catalog root", "SPCC Calibration Data"},
                            {"Catalog pack", "SPCC Calibration Data"},
                            {"Pending region", "SPCC Calibration Data"},
                        })});

    constexpr const char* kPlateSolving = "PlateSolvingSettingsPage";
    pages.push_back(SettingsSearchPage{
        text(kPlateSolving, "Plate Solving"),
        texts(kPlateSolving, {"Plate Solving", "Gaia", "ASTAP", "Astrometry", "Solver", "Index", "Catalog", "Download",
                              "Downsample", "Timeout", "Search Radius", "API Key"}),
        rows(kPlateSolving, {
                                {"Default solver", "General"},
                                {"Search radius", "General"},
                                {"Downsample", "General"},
                                {"Timeout", "General"},
                                {"Executable", "ASTAP"},
                                {"Web API key", "Astrometry.net"},
                            })});

    return pages;
}

} // namespace an::ui
