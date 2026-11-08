// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "controls/annotation/AnnotationSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace an::ui
{
namespace
{

/// The version of the stored text, written for a later reader; every version is read field by field.
constexpr int kStoredVersion = 1;

/// A deep-sky selection's values for each level of detail.
[[nodiscard]] AnnotationDeepSkySelection deepSkyOf(const AnnotationDetail detail)
{
    AnnotationDeepSkySelection selection;
    switch (detail)
    {
    case AnnotationDetail::Essential:
        selection.namedOnly = true;
        selection.magnitudeLimit = kAnnotationNoMagnitudeLimit;
        selection.unmeasured = true;
        selection.minimumSize = 0.0;
        break;
    case AnnotationDetail::Standard:
    case AnnotationDetail::Custom:
        break;
    case AnnotationDetail::Detailed:
        selection.magnitudeLimit = 14.0;
        selection.unmeasured = true;
        selection.minimumSize = 0.0012;
        break;
    case AnnotationDetail::Complete:
        selection.magnitudeLimit = kAnnotationNoMagnitudeLimit;
        selection.unmeasured = true;
        selection.minimumSize = 0.0;
        selection.otherObjects = true;
        break;
    }
    return selection;
}

/// A star selection's values for each level of detail.
[[nodiscard]] AnnotationStarSelection starsOf(const AnnotationDetail detail)
{
    AnnotationStarSelection selection;
    switch (detail)
    {
    case AnnotationDetail::Essential:
        selection.magnitudeLimit = 2.5;
        selection.bayerLetters = false;
        break;
    case AnnotationDetail::Standard:
    case AnnotationDetail::Custom:
        break;
    case AnnotationDetail::Detailed:
        selection.magnitudeLimit = 5.5;
        break;
    case AnnotationDetail::Complete:
        selection.magnitudeLimit = kAnnotationFaintestStar;
        break;
    }
    return selection;
}

/// A colour of a palette, from its red, green and blue bytes and an opacity byte.
[[nodiscard]] QColor rgba(const int red, const int green, const int blue, const int alpha = 255)
{
    return QColor(red, green, blue, alpha);
}

// The palettes' colours are the pictures' content, not the interface's: they are the defaults a person starts from
// and keeps, so they are stated here rather than taken from the design tokens, which follow the application's theme.
[[nodiscard]] AnnotationColors auroraColors()
{
    AnnotationColors colors;
    colors.grid = rgba(127, 178, 255);
    colors.galaxy = rgba(255, 200, 97);
    colors.nebula = rgba(255, 122, 147);
    colors.planetaryNebula = rgba(79, 216, 232);
    colors.openCluster = rgba(155, 227, 138);
    colors.globularCluster = rgba(208, 168, 255);
    colors.otherObject = rgba(184, 196, 214);
    colors.star = rgba(244, 247, 255);
    colors.text = rgba(244, 247, 255);
    colors.halo = rgba(5, 7, 12, 200);
    colors.decoration = rgba(244, 247, 255);
    colors.panel = rgba(10, 15, 26, 178);
    colors.panelText = rgba(238, 242, 248);
    colors.accent = rgba(255, 200, 97);
    return colors;
}

[[nodiscard]] AnnotationColors moonlightColors()
{
    AnnotationColors colors;
    colors.grid = rgba(255, 255, 255);
    colors.galaxy = rgba(242, 244, 248);
    colors.nebula = rgba(242, 244, 248);
    colors.planetaryNebula = rgba(242, 244, 248);
    colors.openCluster = rgba(242, 244, 248);
    colors.globularCluster = rgba(242, 244, 248);
    colors.otherObject = rgba(214, 219, 228);
    colors.star = rgba(255, 255, 255);
    colors.text = rgba(255, 255, 255);
    colors.halo = rgba(0, 0, 0, 200);
    colors.decoration = rgba(255, 255, 255);
    colors.panel = rgba(0, 0, 0, 150);
    colors.panelText = rgba(255, 255, 255);
    colors.accent = rgba(255, 255, 255);
    return colors;
}

[[nodiscard]] AnnotationColors emberColors()
{
    AnnotationColors colors;
    colors.grid = rgba(255, 59, 48);
    colors.galaxy = rgba(255, 107, 94);
    colors.nebula = rgba(255, 138, 128);
    colors.planetaryNebula = rgba(255, 82, 82);
    colors.openCluster = rgba(255, 112, 67);
    colors.globularCluster = rgba(255, 138, 101);
    colors.otherObject = rgba(229, 115, 115);
    colors.star = rgba(255, 205, 210);
    colors.text = rgba(255, 205, 210);
    colors.halo = rgba(16, 0, 0, 210);
    colors.decoration = rgba(255, 138, 128);
    colors.panel = rgba(26, 0, 0, 178);
    colors.panelText = rgba(255, 205, 210);
    colors.accent = rgba(255, 82, 82);
    return colors;
}

[[nodiscard]] AnnotationColors inkColors()
{
    AnnotationColors colors;
    colors.grid = rgba(31, 42, 68);
    colors.galaxy = rgba(138, 90, 0);
    colors.nebula = rgba(161, 18, 58);
    colors.planetaryNebula = rgba(0, 112, 122);
    colors.openCluster = rgba(46, 125, 50);
    colors.globularCluster = rgba(94, 53, 177);
    colors.otherObject = rgba(69, 90, 100);
    colors.star = rgba(17, 17, 17);
    colors.text = rgba(17, 17, 17);
    colors.halo = rgba(255, 255, 255, 220);
    colors.decoration = rgba(17, 17, 17);
    colors.panel = rgba(255, 255, 255, 205);
    colors.panelText = rgba(17, 17, 17);
    colors.accent = rgba(161, 18, 58);
    return colors;
}

constexpr std::array kPalettes{AnnotationPalette::Aurora, AnnotationPalette::Moonlight, AnnotationPalette::Ember,
                               AnnotationPalette::Ink};

constexpr std::array kDetails{AnnotationDetail::Essential, AnnotationDetail::Standard, AnnotationDetail::Detailed,
                              AnnotationDetail::Complete};

/// One name of an enumeration's value in the stored text.
template <typename EnumT> struct NamedValue
{
    EnumT value;
    std::string_view name;
};

constexpr std::array<NamedValue<AnnotationLabelText>, 3> kLabelTexts{{
    {AnnotationLabelText::Designation, "designation"},
    {AnnotationLabelText::Name, "name"},
    {AnnotationLabelText::DesignationAndName, "designationAndName"},
}};

constexpr std::array<NamedValue<AnnotationGridDensity>, 3> kGridDensities{{
    {AnnotationGridDensity::Coarse, "coarse"},
    {AnnotationGridDensity::Normal, "normal"},
    {AnnotationGridDensity::Fine, "fine"},
}};

constexpr std::array<NamedValue<AnnotationLabelBacking>, 3> kBackings{{
    {AnnotationLabelBacking::Halo, "halo"},
    {AnnotationLabelBacking::Badge, "badge"},
    {AnnotationLabelBacking::Plain, "plain"},
}};

constexpr std::array<NamedValue<AnnotationMarker>, 2> kMarkers{{
    {AnnotationMarker::Outline, "outline"},
    {AnnotationMarker::Brackets, "brackets"},
}};

constexpr std::array<NamedValue<AnnotationCorner>, 4> kCorners{{
    {AnnotationCorner::TopLeft, "topLeft"},
    {AnnotationCorner::TopRight, "topRight"},
    {AnnotationCorner::BottomLeft, "bottomLeft"},
    {AnnotationCorner::BottomRight, "bottomRight"},
}};

constexpr std::array<NamedValue<AnnotationStretch>, 2> kStretches{{
    {AnnotationStretch::AsShown, "asShown"},
    {AnnotationStretch::AsStored, "asStored"},
}};

constexpr std::array<NamedValue<AnnotationBackground>, 2> kBackgrounds{{
    {AnnotationBackground::Image, "image"},
    {AnnotationBackground::Transparent, "transparent"},
}};

template <typename EnumT, std::size_t N>
[[nodiscard]] QString nameOf(const std::array<NamedValue<EnumT>, N>& table, const EnumT value)
{
    for (const NamedValue<EnumT>& entry : table)
    {
        if (entry.value == value)
        {
            return QString::fromUtf8(entry.name.data(), static_cast<qsizetype>(entry.name.size()));
        }
    }
    return QString::fromUtf8(table.front().name.data(), static_cast<qsizetype>(table.front().name.size()));
}

template <typename EnumT, std::size_t N>
[[nodiscard]] EnumT readEnum(const QJsonObject& object, const char* key, const std::array<NamedValue<EnumT>, N>& table,
                             const EnumT fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString())
    {
        return fallback;
    }
    const QString text = value.toString().trimmed();
    for (const NamedValue<EnumT>& entry : table)
    {
        if (text == QLatin1String(entry.name.data(), static_cast<qsizetype>(entry.name.size())))
        {
            return entry.value;
        }
    }
    return fallback;
}

[[nodiscard]] bool readBool(const QJsonObject& object, const char* key, const bool fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isBool() ? value.toBool() : fallback;
}

[[nodiscard]] double readNumber(const QJsonObject& object, const char* key, const double minimum, const double maximum,
                                const double fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble())
    {
        return fallback;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && number >= minimum && number <= maximum ? number : fallback;
}

[[nodiscard]] QString readText(const QJsonObject& object, const char* key, const QString& fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : fallback;
}

[[nodiscard]] QColor readColor(const QJsonObject& object, const char* key, const QColor& fallback)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isString())
    {
        return fallback;
    }
    const QColor color = QColor::fromString(value.toString().trimmed());
    return color.isValid() ? color : fallback;
}

[[nodiscard]] QString colorText(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

/// Each colour of a palette with the key it is stored under.
struct ColorField
{
    const char* key;
    QColor AnnotationColors::* member;
};

constexpr std::array<ColorField, 14> kColorFields{{
    {"grid", &AnnotationColors::grid},
    {"galaxy", &AnnotationColors::galaxy},
    {"nebula", &AnnotationColors::nebula},
    {"planetaryNebula", &AnnotationColors::planetaryNebula},
    {"openCluster", &AnnotationColors::openCluster},
    {"globularCluster", &AnnotationColors::globularCluster},
    {"otherObject", &AnnotationColors::otherObject},
    {"star", &AnnotationColors::star},
    {"text", &AnnotationColors::text},
    {"halo", &AnnotationColors::halo},
    {"decoration", &AnnotationColors::decoration},
    {"panel", &AnnotationColors::panel},
    {"panelText", &AnnotationColors::panelText},
    {"accent", &AnnotationColors::accent},
}};

} // namespace

AnnotationSettings defaultAnnotationSettings()
{
    AnnotationSettings settings;
    applyAnnotationDetail(settings.content, AnnotationDetail::Standard);
    settings.style.colors = annotationPaletteColors(AnnotationPalette::Aurora);
    return settings;
}

void applyAnnotationDetail(AnnotationContent& content, const AnnotationDetail detail)
{
    if (detail == AnnotationDetail::Custom)
    {
        return;
    }
    content.deepSky = deepSkyOf(detail);
    content.stars = starsOf(detail);
}

AnnotationDetail annotationDetailOf(const AnnotationContent& content)
{
    for (const AnnotationDetail detail : kDetails)
    {
        if (content.deepSky == deepSkyOf(detail) && content.stars == starsOf(detail))
        {
            return detail;
        }
    }
    return AnnotationDetail::Custom;
}

AnnotationColors annotationPaletteColors(const AnnotationPalette palette)
{
    switch (palette)
    {
    case AnnotationPalette::Moonlight:
        return moonlightColors();
    case AnnotationPalette::Ember:
        return emberColors();
    case AnnotationPalette::Ink:
        return inkColors();
    case AnnotationPalette::Aurora:
    case AnnotationPalette::Custom:
        break;
    }
    return auroraColors();
}

AnnotationPalette annotationPaletteOf(const AnnotationColors& colors)
{
    for (const AnnotationPalette palette : kPalettes)
    {
        if (colors == annotationPaletteColors(palette))
        {
            return palette;
        }
    }
    return AnnotationPalette::Custom;
}

double annotationReferenceLength(const QSize imageSize) noexcept
{
    if (imageSize.width() <= 0 || imageSize.height() <= 0)
    {
        return 0.0;
    }
    return std::sqrt(static_cast<double>(imageSize.width()) * static_cast<double>(imageSize.height()));
}

QSize annotationPictureSize(const QSize imageSize, const std::uint32_t longEdge) noexcept
{
    const int longer = (std::max)(imageSize.width(), imageSize.height());
    if (longEdge == 0u || longer <= 0 || static_cast<std::int64_t>(longEdge) >= longer)
    {
        return imageSize;
    }
    const double scale = static_cast<double>(longEdge) / static_cast<double>(longer);
    const int width = (std::max)(1, static_cast<int>(std::lround(imageSize.width() * scale)));
    const int height = (std::max)(1, static_cast<int>(std::lround(imageSize.height() * scale)));
    return QSize(width, height);
}

QString storedAnnotationSettings(const AnnotationSettings& settings)
{
    const AnnotationContent& content = settings.content;
    const AnnotationStyle& style = settings.style;
    QJsonObject root;
    root.insert(QStringLiteral("version"), kStoredVersion);

    QJsonObject layers;
    layers.insert(QStringLiteral("grid"), content.layers.grid);
    layers.insert(QStringLiteral("deepSky"), content.layers.deepSky);
    layers.insert(QStringLiteral("stars"), content.layers.stars);
    layers.insert(QStringLiteral("compass"), content.layers.compass);
    layers.insert(QStringLiteral("scaleBar"), content.layers.scaleBar);
    layers.insert(QStringLiteral("infoPanel"), content.layers.infoPanel);
    root.insert(QStringLiteral("layers"), layers);

    QJsonObject deepSky;
    deepSky.insert(QStringLiteral("messier"), content.deepSky.messier);
    deepSky.insert(QStringLiteral("caldwell"), content.deepSky.caldwell);
    deepSky.insert(QStringLiteral("ngc"), content.deepSky.ngc);
    deepSky.insert(QStringLiteral("ic"), content.deepSky.ic);
    deepSky.insert(QStringLiteral("namedOnly"), content.deepSky.namedOnly);
    deepSky.insert(QStringLiteral("magnitudeLimit"), content.deepSky.magnitudeLimit);
    deepSky.insert(QStringLiteral("unmeasured"), content.deepSky.unmeasured);
    deepSky.insert(QStringLiteral("minimumSize"), content.deepSky.minimumSize);
    deepSky.insert(QStringLiteral("galaxies"), content.deepSky.galaxies);
    deepSky.insert(QStringLiteral("nebulae"), content.deepSky.nebulae);
    deepSky.insert(QStringLiteral("planetaryNebulae"), content.deepSky.planetaryNebulae);
    deepSky.insert(QStringLiteral("openClusters"), content.deepSky.openClusters);
    deepSky.insert(QStringLiteral("globularClusters"), content.deepSky.globularClusters);
    deepSky.insert(QStringLiteral("otherObjects"), content.deepSky.otherObjects);
    root.insert(QStringLiteral("deepSky"), deepSky);

    QJsonObject stars;
    stars.insert(QStringLiteral("magnitudeLimit"), content.stars.magnitudeLimit);
    stars.insert(QStringLiteral("bayerLetters"), content.stars.bayerLetters);
    root.insert(QStringLiteral("stars"), stars);

    root.insert(QStringLiteral("labelText"), nameOf(kLabelTexts, content.labelText));
    root.insert(QStringLiteral("gridDensity"), nameOf(kGridDensities, content.gridDensity));
    root.insert(QStringLiteral("gridLabels"), content.gridLabels);

    QJsonObject facts;
    facts.insert(QStringLiteral("coordinates"), content.facts.coordinates);
    facts.insert(QStringLiteral("field"), content.facts.field);
    facts.insert(QStringLiteral("optics"), content.facts.optics);
    facts.insert(QStringLiteral("camera"), content.facts.camera);
    facts.insert(QStringLiteral("filter"), content.facts.filter);
    facts.insert(QStringLiteral("date"), content.facts.date);
    facts.insert(QStringLiteral("integration"), content.facts.integration);
    root.insert(QStringLiteral("facts"), facts);
    root.insert(QStringLiteral("credit"), content.credit);

    QJsonObject colors;
    for (const ColorField& field : kColorFields)
    {
        colors.insert(QLatin1String(field.key), colorText(style.colors.*field.member));
    }
    QJsonObject look;
    look.insert(QStringLiteral("colors"), colors);
    look.insert(QStringLiteral("textSize"), style.textSize);
    look.insert(QStringLiteral("lineWidth"), style.lineWidth);
    look.insert(QStringLiteral("gridOpacity"), style.gridOpacity);
    look.insert(QStringLiteral("fontFamily"), style.fontFamily);
    look.insert(QStringLiteral("labelBacking"), nameOf(kBackings, style.labelBacking));
    look.insert(QStringLiteral("marker"), nameOf(kMarkers, style.marker));
    look.insert(QStringLiteral("labelsInObjectColor"), style.labelsInObjectColor);
    look.insert(QStringLiteral("leaderLines"), style.leaderLines);
    look.insert(QStringLiteral("unlabelledMarks"), style.unlabelledMarks);
    look.insert(QStringLiteral("panelCorner"), nameOf(kCorners, style.panelCorner));
    look.insert(QStringLiteral("compassCorner"), nameOf(kCorners, style.compassCorner));
    look.insert(QStringLiteral("scaleBarCorner"), nameOf(kCorners, style.scaleBarCorner));
    root.insert(QStringLiteral("style"), look);

    QJsonObject output;
    output.insert(QStringLiteral("longEdge"), static_cast<double>(settings.output.longEdge));
    output.insert(QStringLiteral("stretch"), nameOf(kStretches, settings.output.stretch));
    output.insert(QStringLiteral("background"), nameOf(kBackgrounds, settings.output.background));
    root.insert(QStringLiteral("output"), output);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

AnnotationSettings annotationSettingsFromStored(const QString& stored)
{
    AnnotationSettings settings = defaultAnnotationSettings();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return settings;
    }
    const QJsonObject root = document.object();
    AnnotationContent& content = settings.content;
    AnnotationStyle& style = settings.style;

    const QJsonObject layers = root.value(QStringLiteral("layers")).toObject();
    content.layers.grid = readBool(layers, "grid", content.layers.grid);
    content.layers.deepSky = readBool(layers, "deepSky", content.layers.deepSky);
    content.layers.stars = readBool(layers, "stars", content.layers.stars);
    content.layers.compass = readBool(layers, "compass", content.layers.compass);
    content.layers.scaleBar = readBool(layers, "scaleBar", content.layers.scaleBar);
    content.layers.infoPanel = readBool(layers, "infoPanel", content.layers.infoPanel);

    const QJsonObject deepSky = root.value(QStringLiteral("deepSky")).toObject();
    AnnotationDeepSkySelection& selection = content.deepSky;
    selection.messier = readBool(deepSky, "messier", selection.messier);
    selection.caldwell = readBool(deepSky, "caldwell", selection.caldwell);
    selection.ngc = readBool(deepSky, "ngc", selection.ngc);
    selection.ic = readBool(deepSky, "ic", selection.ic);
    selection.namedOnly = readBool(deepSky, "namedOnly", selection.namedOnly);
    selection.magnitudeLimit =
        readNumber(deepSky, "magnitudeLimit", -30.0, kAnnotationNoMagnitudeLimit, selection.magnitudeLimit);
    selection.unmeasured = readBool(deepSky, "unmeasured", selection.unmeasured);
    selection.minimumSize = readNumber(deepSky, "minimumSize", 0.0, 1.0, selection.minimumSize);
    selection.galaxies = readBool(deepSky, "galaxies", selection.galaxies);
    selection.nebulae = readBool(deepSky, "nebulae", selection.nebulae);
    selection.planetaryNebulae = readBool(deepSky, "planetaryNebulae", selection.planetaryNebulae);
    selection.openClusters = readBool(deepSky, "openClusters", selection.openClusters);
    selection.globularClusters = readBool(deepSky, "globularClusters", selection.globularClusters);
    selection.otherObjects = readBool(deepSky, "otherObjects", selection.otherObjects);

    const QJsonObject stars = root.value(QStringLiteral("stars")).toObject();
    content.stars.magnitudeLimit =
        readNumber(stars, "magnitudeLimit", -30.0, kAnnotationFaintestStar, content.stars.magnitudeLimit);
    content.stars.bayerLetters = readBool(stars, "bayerLetters", content.stars.bayerLetters);

    content.labelText = readEnum(root, "labelText", kLabelTexts, content.labelText);
    content.gridDensity = readEnum(root, "gridDensity", kGridDensities, content.gridDensity);
    content.gridLabels = readBool(root, "gridLabels", content.gridLabels);

    const QJsonObject facts = root.value(QStringLiteral("facts")).toObject();
    content.facts.coordinates = readBool(facts, "coordinates", content.facts.coordinates);
    content.facts.field = readBool(facts, "field", content.facts.field);
    content.facts.optics = readBool(facts, "optics", content.facts.optics);
    content.facts.camera = readBool(facts, "camera", content.facts.camera);
    content.facts.filter = readBool(facts, "filter", content.facts.filter);
    content.facts.date = readBool(facts, "date", content.facts.date);
    content.facts.integration = readBool(facts, "integration", content.facts.integration);
    content.credit = readText(root, "credit", content.credit);

    const QJsonObject look = root.value(QStringLiteral("style")).toObject();
    const QJsonObject colors = look.value(QStringLiteral("colors")).toObject();
    for (const ColorField& field : kColorFields)
    {
        style.colors.*field.member = readColor(colors, field.key, style.colors.*field.member);
    }
    style.textSize =
        readNumber(look, "textSize", kAnnotationMinimumTextSize, kAnnotationMaximumTextSize, style.textSize);
    style.lineWidth =
        readNumber(look, "lineWidth", kAnnotationMinimumLineWidth, kAnnotationMaximumLineWidth, style.lineWidth);
    style.gridOpacity = readNumber(look, "gridOpacity", 0.0, 1.0, style.gridOpacity);
    style.fontFamily = readText(look, "fontFamily", style.fontFamily);
    style.labelBacking = readEnum(look, "labelBacking", kBackings, style.labelBacking);
    style.marker = readEnum(look, "marker", kMarkers, style.marker);
    style.labelsInObjectColor = readBool(look, "labelsInObjectColor", style.labelsInObjectColor);
    style.leaderLines = readBool(look, "leaderLines", style.leaderLines);
    style.unlabelledMarks = readBool(look, "unlabelledMarks", style.unlabelledMarks);
    style.panelCorner = readEnum(look, "panelCorner", kCorners, style.panelCorner);
    style.compassCorner = readEnum(look, "compassCorner", kCorners, style.compassCorner);
    style.scaleBarCorner = readEnum(look, "scaleBarCorner", kCorners, style.scaleBarCorner);

    const QJsonObject output = root.value(QStringLiteral("output")).toObject();
    settings.output.longEdge =
        static_cast<std::uint32_t>(readNumber(output, "longEdge", 0.0, 1.0e6, settings.output.longEdge));
    settings.output.stretch = readEnum(output, "stretch", kStretches, settings.output.stretch);
    settings.output.background = readEnum(output, "background", kBackgrounds, settings.output.background);
    return settings;
}

} // namespace an::ui
