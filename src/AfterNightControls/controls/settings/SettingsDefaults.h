// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <QString>

/// Default values for all settings in the Settings Dialog.
/// Use these when implementing resetToDefaults() or when reading settings
/// without a stored value.
namespace SettingsDefaults
{

// ═══════════════════════════════════════════════════════════════
// General Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto GENERAL_LANGUAGE = "system";
inline constexpr auto GENERAL_DEFAULT_SAVE_FORMAT = "tiff";
inline constexpr int GENERAL_RECENT_FILES_LIMIT = 20;
inline constexpr int GENERAL_RECENT_PROCESSES_LIMIT = 10;
inline constexpr bool GENERAL_CONFIRM_CLOSE_UNSAVED = true;
// GENERAL_TEMP_FILES_LOCATION: Use QStandardPaths::TempLocation at runtime
inline constexpr bool GENERAL_CLEAR_TEMP_ON_EXIT = true;
inline constexpr bool GENERAL_CHECK_APP_UPDATES_ON_STARTUP = false;
inline constexpr bool GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP = true;

// ═══════════════════════════════════════════════════════════════
// Raw Image Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto RAW_WHITE_BALANCE_MODE = "none";
inline constexpr double RAW_CUSTOM_WB_R = 1.0;
inline constexpr double RAW_CUSTOM_WB_G = 1.0;
inline constexpr double RAW_CUSTOM_WB_B = 1.0;
inline constexpr bool RAW_AUTO_ROTATE = true;
inline constexpr auto RAW_DEMOSAIC_ALGORITHM = "rcd";

// ═══════════════════════════════════════════════════════════════
// External Tools Settings
// ═══════════════════════════════════════════════════════════════
// All tool paths default to empty (not configured)
inline constexpr auto TOOLS_STARNET_PATH = "";
inline constexpr auto TOOLS_DEEPSNR_PATH = "";
inline constexpr auto TOOLS_ASTAP_PATH = "";
inline constexpr auto TOOLS_ASTROMETRY_LOCAL_PATH = "";
inline constexpr auto TOOLS_ASTROMETRY_API_KEY = "";

// ═══════════════════════════════════════════════════════════════
// Processing Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr int PROCESSING_MAX_THREADS_BATCH = 0; // 0 = Auto
inline constexpr int PROCESSING_MAX_THREADS_PREVIEW = 0; // 0 = Auto
inline constexpr bool PROCESSING_GPU_ENABLED = false; // Not yet implemented
inline constexpr auto PROCESSING_PREFERRED_GPU = "auto";
inline constexpr int PROCESSING_MEMORY_LIMIT_MB = 0; // 0 = Auto
inline constexpr int PROCESSING_PREVIEW_FRAME_RATE = 30; // previews a second while a control is dragged

// ═══════════════════════════════════════════════════════════════
// Interface Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto INTERFACE_THEME = "dark";
inline constexpr auto INTERFACE_MATERIAL_BACKDROP_MODE = "auto";
inline constexpr auto INTERFACE_THEME_CONTRAST_PROFILE = "balanced";
inline constexpr auto INTERFACE_THEME_ACCENT_FAMILY = "blue";
inline constexpr auto INTERFACE_THEME_SURFACE_TINT_FAMILY = "neutral";
inline constexpr int INTERFACE_ZOOM_SENSITIVITY = 50;
inline constexpr bool INTERFACE_ZOOM_REQUIRES_CTRL = false;
inline constexpr bool INTERFACE_LOCK_LAYOUT_BY_DEFAULT = false;
inline constexpr bool INTERFACE_REMEMBER_WINDOW_SIZE = true;
inline constexpr auto INTERFACE_RECENT_PROCESS_IDS = "";
inline constexpr auto INTERFACE_FAVORITE_PROCESS_IDS = "";
inline constexpr bool INTERFACE_ANIMATIONS_ENABLED = true;
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
inline constexpr bool INTERFACE_USE_SYSTEM_FILE_BROWSER = true;
#else
inline constexpr bool INTERFACE_USE_SYSTEM_FILE_BROWSER = false;
#endif

// ═══════════════════════════════════════════════════════════════
// Export Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto EXPORT_TIFF_COMPRESSION = "none";
inline constexpr auto EXPORT_XISF_COMPRESSION = "none"; // The form every XISF reader takes
inline constexpr bool EXPORT_XISF_BYTE_SHUFFLE = true;
inline constexpr auto EXPORT_XISF_CHECKSUM = "none";
inline constexpr int EXPORT_JPEG_QUALITY = 95;
inline constexpr int EXPORT_PNG_COMPRESSION = 6;
inline constexpr auto EXPORT_FITS_FLOAT_FORMAT = "float32";
inline constexpr bool EXPORT_EMBED_EXIF = true;
inline constexpr bool EXPORT_EMBED_HISTORY = false;
inline constexpr auto EXPORT_DEFAULT_DIRECTORY = ""; // Empty = same as source

// ═══════════════════════════════════════════════════════════════
// Catalogs & Data Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto CATALOGS_SPCC_CATALOG_DIRECTORY = "";
inline constexpr auto CATALOGS_SPCC_CATALOG_PACK = "";
inline constexpr auto CATALOGS_SPCC_REGIONAL_CACHE_REQUEST = "";
inline constexpr auto CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY = "";
inline constexpr auto CATALOGS_SPCC_RESPONSE_PROFILE_MAPPINGS = "";

// ═══════════════════════════════════════════════════════════════
// Logging Settings
// Note: These defaults are kept for reference and used by LoggingSettingsPage.
// Logging settings are stored in a separate config file (logging.conf).
// ═══════════════════════════════════════════════════════════════
// LOGGING_DIRECTORY: Use an::platform::LoggingConfig::defaultLogDirectory() at runtime
inline constexpr auto LOGGING_LEVEL = "info";
inline constexpr int LOGGING_MAX_SIZE_MB = 10;
inline constexpr int LOGGING_MAX_FILES = 5;

// ═══════════════════════════════════════════════════════════════
// Plate Solving Settings (Future)
// ═══════════════════════════════════════════════════════════════
inline constexpr auto PLATESOLVE_DEFAULT_SOLVER = "";
inline constexpr double PLATESOLVE_SEARCH_RADIUS = 10.0;
inline constexpr int PLATESOLVE_DOWNSAMPLE = 2;
inline constexpr int PLATESOLVE_TIMEOUT = 120;
inline constexpr auto PLATESOLVE_INDEX_PATH = "";
inline constexpr auto PLATESOLVE_GAIA_CATALOG_DIRECTORY = "";
inline constexpr auto PLATESOLVE_GAIA_CATALOG_PACK = "gaia_dr3_g14";
inline constexpr bool PLATESOLVE_USE_HINTS = true;
inline constexpr double PLATESOLVE_FOCAL_LENGTH_MM = 0.0;
inline constexpr double PLATESOLVE_PIXEL_SIZE_UM = 0.0;
inline constexpr double PLATESOLVE_OBSERVER_LAT = 0.0;
inline constexpr double PLATESOLVE_OBSERVER_LON = 0.0;
inline constexpr bool PLATESOLVE_ASTROMETRY_OVERLAY_ENABLED = false;
inline constexpr bool PLATESOLVE_COORDINATE_GRID_ENABLED = true;
inline constexpr bool PLATESOLVE_DISTORTION_MAP_ENABLED = false;
inline constexpr bool PLATESOLVE_DETECTED_STARS_ENABLED = false;
inline constexpr bool PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED = false;
inline constexpr bool PLATESOLVE_STAR_LABELS_ENABLED = false;

} // namespace SettingsDefaults
