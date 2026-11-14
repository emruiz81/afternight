// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

namespace SettingsKeys
{

// ═══════════════════════════════════════════════════════════════
// General Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto GENERAL_LANGUAGE = "general/language";
inline constexpr auto GENERAL_DEFAULT_SAVE_FORMAT = "general/defaultSaveFormat";
inline constexpr auto GENERAL_RECENT_FILES_LIMIT = "general/recentFilesLimit";
inline constexpr auto GENERAL_RECENT_PROCESSES_LIMIT = "general/recentProcessesLimit";
inline constexpr auto GENERAL_CONFIRM_CLOSE_UNSAVED = "general/confirmCloseUnsaved";
inline constexpr auto GENERAL_TEMP_FILES_LOCATION = "general/tempFilesLocation";
inline constexpr auto GENERAL_CLEAR_TEMP_ON_EXIT = "general/clearTempOnExit";
inline constexpr auto GENERAL_CHECK_APP_UPDATES_ON_STARTUP = "general/checkAppUpdatesOnStartup";
inline constexpr auto GENERAL_CHECK_EXTENSION_UPDATES_ON_STARTUP = "general/checkExtensionUpdatesOnStartup";

// ═══════════════════════════════════════════════════════════════
// Raw Image Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto RAW_WHITE_BALANCE_MODE = "raw/whiteBalanceMode";
inline constexpr auto RAW_CUSTOM_WB_R = "raw/customWbR";
inline constexpr auto RAW_CUSTOM_WB_G = "raw/customWbG";
inline constexpr auto RAW_CUSTOM_WB_B = "raw/customWbB";
inline constexpr auto RAW_AUTO_ROTATE = "raw/autoRotate";
inline constexpr auto RAW_DEMOSAIC_ALGORITHM = "raw/demosaicAlgorithm";

// ═══════════════════════════════════════════════════════════════
// External Tools Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto TOOLS_STARNET_PATH = "tools/starnetPath";
inline constexpr auto TOOLS_DEEPSNR_PATH = "tools/deepsnrPath";
inline constexpr auto TOOLS_ASTAP_PATH = "tools/astapPath";
inline constexpr auto TOOLS_ASTROMETRY_LOCAL_PATH = "tools/astrometryLocalPath";
inline constexpr auto TOOLS_ASTROMETRY_API_KEY = "tools/astrometryApiKey";

// ═══════════════════════════════════════════════════════════════
// Processing Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto PROCESSING_MAX_THREADS_BATCH = "processing/maxThreadsBatch";
inline constexpr auto PROCESSING_MAX_THREADS_PREVIEW = "processing/maxThreadsPreview";
inline constexpr auto PROCESSING_GPU_ENABLED = "processing/gpuEnabled";
inline constexpr auto PROCESSING_PREFERRED_GPU = "processing/preferredGpu";
inline constexpr auto PROCESSING_MEMORY_LIMIT_MB = "processing/memoryLimitMb";
inline constexpr auto PROCESSING_PREVIEW_FRAME_RATE = "processing/previewFrameRate";

// ═══════════════════════════════════════════════════════════════
// Interface Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto INTERFACE_THEME = "interface/theme";
inline constexpr auto INTERFACE_MATERIAL_BACKDROP_MODE = "interface/materialBackdropMode";
inline constexpr auto INTERFACE_THEME_CONTRAST_PROFILE = "interface/themeContrastProfile";
inline constexpr auto INTERFACE_THEME_ACCENT_FAMILY = "interface/themeAccentFamily";
inline constexpr auto INTERFACE_THEME_SURFACE_TINT_FAMILY = "interface/themeSurfaceTintFamily";
inline constexpr auto INTERFACE_THEME_TINT_STRENGTH = "interface/themeTintStrength";
inline constexpr auto INTERFACE_DENSITY = "interface/density";
inline constexpr auto INTERFACE_ZOOM_SENSITIVITY = "interface/zoomSensitivity";
inline constexpr auto INTERFACE_ZOOM_REQUIRES_CTRL = "interface/zoomRequiresCtrl";
inline constexpr auto INTERFACE_LOCK_LAYOUT_BY_DEFAULT = "interface/lockLayoutByDefault";
inline constexpr auto INTERFACE_REMEMBER_WINDOW_SIZE = "interface/rememberWindowSize";
inline constexpr auto INTERFACE_WINDOW_GEOMETRY_X = "interface/windowGeometryX";
inline constexpr auto INTERFACE_WINDOW_GEOMETRY_Y = "interface/windowGeometryY";
inline constexpr auto INTERFACE_WINDOW_GEOMETRY_WIDTH = "interface/windowGeometryWidth";
inline constexpr auto INTERFACE_WINDOW_GEOMETRY_HEIGHT = "interface/windowGeometryHeight";
inline constexpr auto INTERFACE_WINDOW_MAXIMIZED = "interface/windowMaximized";
inline constexpr auto INTERFACE_DASHBOARD_SETTINGS_SCHEMA_VERSION = "interface/dashboardSettingsSchemaVersion";
inline constexpr auto INTERFACE_DASHBOARD_ACTION_ORDER = "interface/dashboardActionOrder";
inline constexpr auto INTERFACE_DASHBOARD_HIDDEN_ACTIONS = "interface/dashboardHiddenActions";
inline constexpr auto INTERFACE_DASHBOARD_PINNED_RECENTS = "interface/dashboardPinnedRecents";
inline constexpr auto INTERFACE_DASHBOARD_PINNED_HERO_FILE = "interface/dashboardPinnedHeroFile";
inline constexpr auto INTERFACE_DASHBOARD_RECENT_SORT_MODE = "interface/dashboardRecentSortMode";
inline constexpr auto INTERFACE_DASHBOARD_RECENT_SEARCH_TEXT = "interface/dashboardRecentSearchText";
inline constexpr auto INTERFACE_DASHBOARD_LAST_ACTION = "interface/dashboardLastAction";
inline constexpr auto INTERFACE_RECENT_PROCESS_IDS = "interface/recentProcessIds";
inline constexpr auto INTERFACE_FAVORITE_PROCESS_IDS = "interface/favoriteProcessIds";
inline constexpr auto INTERFACE_ANIMATIONS_ENABLED = "interface/animationsEnabled";
inline constexpr auto INTERFACE_USE_SYSTEM_FILE_BROWSER = "interface/useSystemFileBrowser";
// The shell the application starts with, by its id; read at start, so a change takes effect at the next one.
inline constexpr auto INTERFACE_SHELL = "interface/shell";

// ═══════════════════════════════════════════════════════════════
// Export Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto EXPORT_TIFF_COMPRESSION = "export/tiffCompression";
inline constexpr auto EXPORT_XISF_COMPRESSION = "export/xisfCompression";
inline constexpr auto EXPORT_XISF_BYTE_SHUFFLE = "export/xisfByteShuffle";
inline constexpr auto EXPORT_XISF_CHECKSUM = "export/xisfChecksum";
inline constexpr auto EXPORT_JPEG_QUALITY = "export/jpegQuality";
inline constexpr auto EXPORT_PNG_COMPRESSION = "export/pngCompression";
inline constexpr auto EXPORT_FITS_FLOAT_FORMAT = "export/fitsFloatFormat";
inline constexpr auto EXPORT_EMBED_EXIF = "export/embedExif";
inline constexpr auto EXPORT_EMBED_HISTORY = "export/embedHistory";
inline constexpr auto EXPORT_DEFAULT_DIRECTORY = "export/defaultDirectory";

// ═══════════════════════════════════════════════════════════════
// Catalogs & Data Settings
// ═══════════════════════════════════════════════════════════════
inline constexpr auto CATALOGS_SPCC_CATALOG_DIRECTORY = "catalogs/spccCatalogDirectory";
inline constexpr auto CATALOGS_SPCC_CATALOG_PACK = "catalogs/spccCatalogPack";
inline constexpr auto CATALOGS_SPCC_REGIONAL_CACHE_REQUEST = "catalogs/spccRegionalCacheRequest";
inline constexpr auto CATALOGS_SPCC_RESPONSE_REPOSITORY_DIRECTORY = "catalogs/spccResponseRepositoryDirectory";
inline constexpr auto CATALOGS_SPCC_RESPONSE_PROFILE_MAPPINGS = "catalogs/spccResponseProfileMappings";

// ═══════════════════════════════════════════════════════════════
// Logging Settings
// Note: Logging settings are stored in a separate config file (logging.conf)
// that can be read before Qt initialization. These keys are kept for
// reference but should not be used with GlobalSettingsPersistence.
// ═══════════════════════════════════════════════════════════════
// inline constexpr auto LOGGING_DIRECTORY = "logging/directory";
// inline constexpr auto LOGGING_LEVEL = "logging/level";
// inline constexpr auto LOGGING_MAX_SIZE_MB = "logging/maxSizeMb";
// inline constexpr auto LOGGING_MAX_FILES = "logging/maxFiles";

// ═══════════════════════════════════════════════════════════════
// Plate Solving Settings (Future)
// ═══════════════════════════════════════════════════════════════
inline constexpr auto PLATESOLVE_DEFAULT_SOLVER = "platesolve/defaultSolver";
inline constexpr auto PLATESOLVE_SEARCH_RADIUS = "platesolve/searchRadius";
inline constexpr auto PLATESOLVE_DOWNSAMPLE = "platesolve/downsample";
inline constexpr auto PLATESOLVE_TIMEOUT = "platesolve/timeout";
inline constexpr auto PLATESOLVE_INDEX_PATH = "platesolve/indexPath";
inline constexpr auto PLATESOLVE_GAIA_CATALOG_DIRECTORY = "platesolve/gaiaCatalogDirectory";
inline constexpr auto PLATESOLVE_GAIA_CATALOG_PACK = "platesolve/gaiaCatalogPack";
inline constexpr auto PLATESOLVE_USE_HINTS = "platesolve/useHints";
inline constexpr auto PLATESOLVE_FOCAL_LENGTH_MM = "platesolve/focalLengthMm";
inline constexpr auto PLATESOLVE_PIXEL_SIZE_UM = "platesolve/pixelSizeUm";
inline constexpr auto PLATESOLVE_OBSERVER_LAT = "platesolve/observerLatitude";
inline constexpr auto PLATESOLVE_OBSERVER_LON = "platesolve/observerLongitude";
inline constexpr auto PLATESOLVE_RECENT_OBJECTS = "platesolve/recentObjects";
inline constexpr auto PLATESOLVE_ASTROMETRY_OVERLAY_ENABLED = "platesolve/astrometryOverlayEnabled";
inline constexpr auto PLATESOLVE_COORDINATE_GRID_ENABLED = "platesolve/coordinateGridEnabled";
inline constexpr auto PLATESOLVE_DISTORTION_MAP_ENABLED = "platesolve/distortionMapEnabled";
inline constexpr auto PLATESOLVE_DETECTED_STARS_ENABLED = "platesolve/detectedStarsEnabled";
inline constexpr auto PLATESOLVE_CATALOG_ANNOTATIONS_ENABLED = "platesolve/catalogAnnotationsEnabled";
inline constexpr auto PLATESOLVE_STAR_LABELS_ENABLED = "platesolve/starLabelsEnabled";
// The magnitude limit a view's annotations kept before they took a level of detail: read only to choose the level that
// names about as much, while no view choices are stored
inline constexpr auto PLATESOLVE_CATALOG_MAGNITUDE_LIMIT = "platesolve/catalogMagnitudeLimit";

// Annotate Image: every choice of an annotated picture but its title, as storedAnnotationSettings writes them
inline constexpr auto ANNOTATION_SETTINGS = "annotation/settings";

// A view's live annotations: the level of detail, the labels and the palette its menu chose, as
// storedAnnotationViewChoices writes them
inline constexpr auto ANNOTATION_VIEW_CHOICES = "annotation/view";

} // namespace SettingsKeys
