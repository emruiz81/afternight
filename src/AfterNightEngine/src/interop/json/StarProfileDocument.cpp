// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the star-profile documents, read and written through nlohmann-json.
//
// Both schemas sit under one `stars_profile` object, and the member names are spelled here exactly as
// the schemas define them. A version-one field the document leaves out reads as zero
// or false. Version two first checks that every parameter and every frame-level field is present,
// and a star field it leaves out reads as zero, false, an empty string or `"px"` for the units. A
// channel array keeps its first three entries; a profile spelling this build does not recognise
// reads as a Gaussian.
//
// Every exception the library throws stays in this file. On read, a parse or type error is
// `ErrorCode::InvalidData`; on write, a type error can only be a string that is not UTF-8 and is
// `ErrorCode::InvalidArgument`; anything else goes through the engine's exception boundary. The
// writer emits the document with a one-space indent.

#include <AfterNightEngine/Interop/StarProfileDocument.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ExceptionBoundary.h>
#include <AfterNightEngine/Core/Status.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace an
{
namespace
{
using Json = nlohmann::json;

constexpr ExternalBoundary kBoundary{
    .library = "nlohmann-json", .operation = "star profile document", .domain = ErrorDomain::Io};

[[nodiscard]] Status invalid(const char* message)
{
    return Status::failure(
        ErrorCode::InvalidData,
        {.message = message, .operation = "an::parseStarProfileDocument", .domain = ErrorDomain::Io});
}

[[nodiscard]] Status unsupported(const char* message)
{
    return Status::failure(
        ErrorCode::Unsupported,
        {.message = message, .operation = "an::parseStarProfileDocument", .domain = ErrorDomain::Io});
}

[[nodiscard]] Status limit(const char* message, std::uint64_t requested, std::uint64_t ceiling)
{
    return Status::failure(ErrorCode::LimitExceeded,
                           {.message = message,
                            .operation = "an::parseStarProfileDocument",
                            .domain = ErrorDomain::Io,
                            .context = {StatusContextField::ofCount(ContextKey::Requested, requested),
                                        StatusContextField::ofCount(ContextKey::Limit, ceiling)}});
}

template <typename ValueT> [[nodiscard]] ValueT value(const Json& object, const char* key, ValueT fallback)
{
    const auto found = object.find(key);
    return found == object.end() ? fallback : found->get<ValueT>();
}

[[nodiscard]] Result<std::string> boundedText(const Json& object, const char* key, std::string fallback,
                                              const StarProfileReadLimits& limits)
{
    std::string text = value(object, key, std::move(fallback));
    if (text.size() > limits.maximumStringBytes)
        return limit("a star-profile string exceeds the configured bound", text.size(), limits.maximumStringBytes);
    return text;
}

[[nodiscard]] PersistedPsfModel profileModel(const Json& object, const char* key)
{
    const std::string spelling = value(object, key, std::string("gaussian"));
    if (spelling == "moffat_bfree")
        return PersistedPsfModel::MoffatFreeBeta;
    if (spelling == "moffat_bfixed")
        return PersistedPsfModel::MoffatFixedBeta;
    return PersistedPsfModel::Gaussian;
}

[[nodiscard]] const char* profileSpelling(PersistedPsfModel model)
{
    switch (model)
    {
    case PersistedPsfModel::Gaussian:
        return "gaussian";
    case PersistedPsfModel::MoffatFreeBeta:
        return "moffat_bfree";
    case PersistedPsfModel::MoffatFixedBeta:
        return "moffat_bfixed";
    }
    return nullptr;
}

[[nodiscard]] Result<StarProfileV1> parseV1(const Json& root, const StarProfileReadLimits& limits)
{
    if (!root.contains("params") || !root["params"].is_object() || !root.contains("global") ||
        !root["global"].is_object())
        return invalid("a version-one profile needs params and global objects");
    const Json& parameters = root["params"];
    const Json& global = root["global"];
    StarProfileV1 profile;
    profile.parameters.detectionThreshold = value(parameters, "detection_threshold", 0.0);
    profile.parameters.roundnessTolerance = value(parameters, "roundness_tolerance", 0.0);
    profile.parameters.minimumSize = value(parameters, "min_size", 0.0);
    profile.parameters.maximumSize = value(parameters, "max_size", 0.0);
    profile.global.fullWidth = value(global, "global_fwhm", 0.0);
    profile.global.quality = value(global, "global_quality", 0.0);
    profile.global.background = value(global, "background", 0.0);
    profile.global.backgroundNoise = value(global, "background_noise", 0.0);
    profile.manuallyEdited = value(root, "manually_edited", false);
    const auto found = root.find("stars");
    if (found == root.end())
        return profile;
    if (!found->is_array())
        return invalid("a version-one profile's stars member is not an array");
    if (found->size() > limits.maximumStars)
        return limit("the star array exceeds the configured bound", found->size(), limits.maximumStars);
    profile.stars.reserve(found->size());
    for (const Json& item : *found)
    {
        if (!item.is_object())
            return invalid("a version-one star is not an object");
        profile.stars.push_back({.intensity = value(item, "intensity", 0.0),
                                 .percentage = value(item, "percentage", 0.0),
                                 .deltaRadius = value(item, "deltaRadius", 0.0),
                                 .quality = value(item, "quality", 0.0),
                                 .meanRadius = value(item, "meanRadius", 0.0),
                                 .radius = value(item, "radius", 0.0),
                                 .x = value(item, "x", 0.0),
                                 .y = value(item, "y", 0.0),
                                 .largeMajorAxis = value(item, "largeMajorAxis", 0.0),
                                 .smallMajorAxis = value(item, "smallMajorAxis", 0.0),
                                 .largeMinorAxis = value(item, "largeMinorAxis", 0.0),
                                 .smallMinorAxis = value(item, "smallMinorAxis", 0.0),
                                 .majorAxisAngle = value(item, "majorAxisAngle", 0.0)});
    }
    return profile;
}

template <std::size_t Count> void loadArray(const Json& object, const char* key, std::array<double, Count>& destination)
{
    const auto found = object.find(key);
    if (found == object.end() || !found->is_array())
        return;
    for (std::size_t index = 0u; index < Count && index < found->size(); ++index)
        destination[index] = (*found)[index].get<double>();
}

[[nodiscard]] Result<StarProfileV2> parseV2(const Json& root, const StarProfileReadLimits& limits)
{
    if (!root.contains("params") || !root["params"].is_object() || !root.contains("global") ||
        !root["global"].is_object())
        return invalid("a version-two profile needs params and global objects");
    const Json& parameters = root["params"];
    const Json& global = root["global"];
    constexpr std::array kRequiredParameters{"radius",       "sigma",       "roundness",    "focal_length",
                                             "pixel_size_x", "convergence", "relax_checks", "profile",
                                             "min_beta",     "min_A",       "max_A",        "max_r"};
    constexpr std::array kRequiredGlobal{"global_fwhm",
                                         "global_roundness",
                                         "global_quality",
                                         "global_snr",
                                         "background",
                                         "background_noise",
                                         "global_mad",
                                         "global_scale",
                                         "zero_point_G",
                                         "zero_point_error",
                                         "catalog_matched_count",
                                         "limiting_magnitude",
                                         "num_channels",
                                         "channel_background_noise",
                                         "channel_mad",
                                         "channel_scale"};
    for (const char* key : kRequiredParameters)
        if (!parameters.contains(key))
            return invalid("a version-two profile is missing a required parameter");
    for (const char* key : kRequiredGlobal)
        if (!global.contains(key))
            return invalid("a version-two profile is missing a required global field");

    StarProfileV2 profile;
    profile.parameters.radius = value(parameters, "radius", 0);
    profile.parameters.sigma = value(parameters, "sigma", 0.0);
    profile.parameters.roundness = value(parameters, "roundness", 0.0);
    profile.parameters.focalLength = value(parameters, "focal_length", 0.0);
    profile.parameters.pixelSizeX = value(parameters, "pixel_size_x", 0.0);
    profile.parameters.convergence = value(parameters, "convergence", 0);
    profile.parameters.relaxChecks = value(parameters, "relax_checks", false);
    profile.parameters.profile = profileModel(parameters, "profile");
    profile.parameters.minimumBeta = value(parameters, "min_beta", 0.0);
    profile.parameters.minimumAmplitude = value(parameters, "min_A", 0.0);
    profile.parameters.maximumAmplitude = value(parameters, "max_A", 0.0);
    profile.parameters.maximumRoundness = value(parameters, "max_r", 0.0);
    profile.global.fullWidth = value(global, "global_fwhm", 0.0);
    profile.global.roundness = value(global, "global_roundness", 0.0);
    profile.global.quality = value(global, "global_quality", 0.0);
    profile.global.signalToNoise = value(global, "global_snr", 0.0);
    profile.global.background = value(global, "background", 0.0);
    profile.global.backgroundNoise = value(global, "background_noise", 0.0);
    profile.global.medianAbsoluteDeviation = value(global, "global_mad", 0.0);
    profile.global.robustScale = value(global, "global_scale", 0.0);
    profile.global.zeroPointG = value(global, "zero_point_G", 0.0);
    profile.global.zeroPointError = value(global, "zero_point_error", 0.0);
    profile.global.catalogMatchedCount = value(global, "catalog_matched_count", std::uint64_t{0u});
    profile.global.limitingMagnitude = value(global, "limiting_magnitude", 0.0);
    profile.global.channelCount = value(global, "num_channels", std::uint32_t{1u});
    loadArray(global, "channel_background_noise", profile.global.channelBackgroundNoise);
    loadArray(global, "channel_mad", profile.global.channelMedianAbsoluteDeviation);
    loadArray(global, "channel_scale", profile.global.channelRobustScale);

    const auto found = root.find("stars");
    if (found == root.end())
        return profile;
    if (!found->is_array())
        return invalid("a version-two profile's stars member is not an array");
    if (found->size() > limits.maximumStars)
        return limit("the star array exceeds the configured bound", found->size(), limits.maximumStars);
    profile.stars.reserve(found->size());
    for (const Json& item : *found)
    {
        if (!item.is_object())
            return invalid("a version-two star is not an object");
        StarProfileV2Star star;
        auto name = boundedText(item, "name", {}, limits);
        auto units = boundedText(item, "units", "px", limits);
        auto catalog = boundedText(item, "catalog_id", {}, limits);
        if (!name)
            return name.status();
        if (!units)
            return units.status();
        if (!catalog)
            return catalog.status();
        star.name = std::move(*name);
        star.psf = {.background = value(item, "B", 0.0),
                    .amplitude = value(item, "A", 0.0),
                    .centreX = value(item, "x0", 0.0),
                    .centreY = value(item, "y0", 0.0),
                    .scaleX = value(item, "sx", 0.0),
                    .scaleY = value(item, "sy", 0.0),
                    .fullWidthX = value(item, "fwhmx", 0.0),
                    .fullWidthY = value(item, "fwhmy", 0.0),
                    .fullWidthXArcseconds = value(item, "fwhmx_arcsec", 0.0),
                    .fullWidthYArcseconds = value(item, "fwhmy_arcsec", 0.0),
                    .angle = value(item, "angle", 0.0),
                    .residualRootMeanSquare = value(item, "rmse", 0.0),
                    .saturation = value(item, "sat", 0.0),
                    .boxRadius = value(item, "R", 0),
                    .saturated = value(item, "has_saturated", false),
                    .beta = value(item, "beta", 0.0),
                    .profile = profileModel(item, "profile")};
        star.position = {.x = value(item, "xpos", 0.0), .y = value(item, "ypos", 0.0)};
        star.photometry = {.magnitude = value(item, "mag", 0.0),
                           .backgroundMagnitude = value(item, "Bmag", 0.0),
                           .magnitudeError = value(item, "s_mag", 0.0),
                           .backgroundMagnitudeError = value(item, "s_Bmag", 0.0),
                           .signalToNoise = value(item, "SNR", 0.0),
                           .valid = value(item, "photo", false),
                           .colorIndexBv = value(item, "BV", 0.0)};
        star.uncertainty = {.background = value(item, "B_err", 0.0),
                            .amplitude = value(item, "A_err", 0.0),
                            .centreX = value(item, "x_err", 0.0),
                            .centreY = value(item, "y_err", 0.0),
                            .scaleX = value(item, "sx_err", 0.0),
                            .scaleY = value(item, "sy_err", 0.0),
                            .angle = value(item, "ang_err", 0.0),
                            .beta = value(item, "beta_err", 0.0)};
        star.astrometry = {.channel = value(item, "channel", 0),
                           .units = std::move(*units),
                           .rightAscension = value(item, "ra", 0.0),
                           .declination = value(item, "dec", 0.0),
                           .hasWorldCoordinates = value(item, "has_world_coordinates", false),
                           .catalogId = std::move(*catalog),
                           .catalogMagnitudeG = value(item, "catalog_mag_G", 0.0),
                           .catalogMagnitudeBp = value(item, "catalog_mag_BP", 0.0),
                           .catalogMagnitudeRp = value(item, "catalog_mag_RP", 0.0),
                           .colorIndexBpRp = value(item, "color_index_BP_RP", 0.0),
                           .matchSeparationArcseconds = value(item, "match_separation_arcsec", 0.0),
                           .hasCatalogMatch = value(item, "has_catalog_match", false)};
        star.metrics = {.roundness = value(item, "roundness", 0.0),
                        .circularity = value(item, "circularity", 0.0),
                        .fullWidth = value(item, "fwhm", 0.0)};
        profile.stars.push_back(std::move(star));
    }
    return profile;
}

void writeV1(Json& root, const StarProfileV1& profile)
{
    root["engine"] = "v1";
    root["version"] = 1;
    root["params"]["detection_threshold"] = profile.parameters.detectionThreshold;
    root["params"]["roundness_tolerance"] = profile.parameters.roundnessTolerance;
    root["params"]["min_size"] = profile.parameters.minimumSize;
    root["params"]["max_size"] = profile.parameters.maximumSize;
    root["global"]["global_fwhm"] = profile.global.fullWidth;
    root["global"]["global_quality"] = profile.global.quality;
    root["global"]["background"] = profile.global.background;
    root["global"]["background_noise"] = profile.global.backgroundNoise;
    root["manually_edited"] = profile.manuallyEdited;
    root["stars"] = Json::array();
    for (const StarProfileV1Star& star : profile.stars)
        root["stars"].push_back({{"intensity", star.intensity},
                                 {"percentage", star.percentage},
                                 {"deltaRadius", star.deltaRadius},
                                 {"quality", star.quality},
                                 {"meanRadius", star.meanRadius},
                                 {"radius", star.radius},
                                 {"x", star.x},
                                 {"y", star.y},
                                 {"largeMajorAxis", star.largeMajorAxis},
                                 {"smallMajorAxis", star.smallMajorAxis},
                                 {"largeMinorAxis", star.largeMinorAxis},
                                 {"smallMinorAxis", star.smallMinorAxis},
                                 {"majorAxisAngle", star.majorAxisAngle}});
}

void writeV2Star(Json& item, const StarProfileV2Star& star)
{
    item = {{"name", star.name},
            {"B", star.psf.background},
            {"A", star.psf.amplitude},
            {"x0", star.psf.centreX},
            {"y0", star.psf.centreY},
            {"sx", star.psf.scaleX},
            {"sy", star.psf.scaleY},
            {"fwhmx", star.psf.fullWidthX},
            {"fwhmy", star.psf.fullWidthY},
            {"fwhmx_arcsec", star.psf.fullWidthXArcseconds},
            {"fwhmy_arcsec", star.psf.fullWidthYArcseconds},
            {"angle", star.psf.angle},
            {"rmse", star.psf.residualRootMeanSquare},
            {"sat", star.psf.saturation},
            {"R", star.psf.boxRadius},
            {"has_saturated", star.psf.saturated},
            {"beta", star.psf.beta},
            {"profile", profileSpelling(star.psf.profile)},
            {"xpos", star.position.x},
            {"ypos", star.position.y},
            {"mag", star.photometry.magnitude},
            {"Bmag", star.photometry.backgroundMagnitude},
            {"s_mag", star.photometry.magnitudeError},
            {"s_Bmag", star.photometry.backgroundMagnitudeError},
            {"SNR", star.photometry.signalToNoise},
            {"photo", star.photometry.valid},
            {"BV", star.photometry.colorIndexBv},
            {"B_err", star.uncertainty.background},
            {"A_err", star.uncertainty.amplitude},
            {"x_err", star.uncertainty.centreX},
            {"y_err", star.uncertainty.centreY},
            {"sx_err", star.uncertainty.scaleX},
            {"sy_err", star.uncertainty.scaleY},
            {"ang_err", star.uncertainty.angle},
            {"beta_err", star.uncertainty.beta},
            {"channel", star.astrometry.channel},
            {"units", star.astrometry.units},
            {"ra", star.astrometry.rightAscension},
            {"dec", star.astrometry.declination},
            {"has_world_coordinates", star.astrometry.hasWorldCoordinates},
            {"catalog_id", star.astrometry.catalogId},
            {"catalog_mag_G", star.astrometry.catalogMagnitudeG},
            {"catalog_mag_BP", star.astrometry.catalogMagnitudeBp},
            {"catalog_mag_RP", star.astrometry.catalogMagnitudeRp},
            {"color_index_BP_RP", star.astrometry.colorIndexBpRp},
            {"match_separation_arcsec", star.astrometry.matchSeparationArcseconds},
            {"has_catalog_match", star.astrometry.hasCatalogMatch},
            {"circularity", star.metrics.circularity},
            {"fwhm", star.metrics.fullWidth},
            {"roundness", star.metrics.roundness}};
}

void writeV2(Json& root, const StarProfileV2& profile)
{
    root["engine"] = "v2";
    root["version"] = 2;
    root["params"] = {{"radius", profile.parameters.radius},
                      {"sigma", profile.parameters.sigma},
                      {"roundness", profile.parameters.roundness},
                      {"focal_length", profile.parameters.focalLength},
                      {"pixel_size_x", profile.parameters.pixelSizeX},
                      {"convergence", profile.parameters.convergence},
                      {"relax_checks", profile.parameters.relaxChecks},
                      {"profile", profileSpelling(profile.parameters.profile)},
                      {"min_beta", profile.parameters.minimumBeta},
                      {"min_A", profile.parameters.minimumAmplitude},
                      {"max_A", profile.parameters.maximumAmplitude},
                      {"max_r", profile.parameters.maximumRoundness}};
    root["global"] = {{"global_fwhm", profile.global.fullWidth},
                      {"global_roundness", profile.global.roundness},
                      {"global_quality", profile.global.quality},
                      {"global_snr", profile.global.signalToNoise},
                      {"background", profile.global.background},
                      {"background_noise", profile.global.backgroundNoise},
                      {"global_mad", profile.global.medianAbsoluteDeviation},
                      {"global_scale", profile.global.robustScale},
                      {"zero_point_G", profile.global.zeroPointG},
                      {"zero_point_error", profile.global.zeroPointError},
                      {"catalog_matched_count", profile.global.catalogMatchedCount},
                      {"limiting_magnitude", profile.global.limitingMagnitude},
                      {"num_channels", profile.global.channelCount},
                      {"channel_background_noise", profile.global.channelBackgroundNoise},
                      {"channel_mad", profile.global.channelMedianAbsoluteDeviation},
                      {"channel_scale", profile.global.channelRobustScale}};
    root["stars"] = Json::array();
    for (const StarProfileV2Star& star : profile.stars)
    {
        Json item;
        writeV2Star(item, star);
        root["stars"].push_back(std::move(item));
    }
}

[[nodiscard]] bool validLimits(const StarProfileReadLimits& limits) noexcept
{
    return limits.maximumFileBytes != 0u && limits.maximumStars != 0u && limits.maximumStringBytes != 0u;
}
} // namespace

Result<StarProfileFile> parseStarProfileDocument(std::string_view text, const StarProfileReadLimits& limits)
{
    if (!validLimits(limits))
        return Status::failure(ErrorCode::InvalidArgument, {.message = "star-profile bounds are positive",
                                                            .operation = "an::parseStarProfileDocument",
                                                            .domain = ErrorDomain::Io});
    if (text.size() > limits.maximumFileBytes)
        return limit("the star-profile document exceeds the configured bound", text.size(), limits.maximumFileBytes);
    try
    {
        const Json document = Json::parse(text.begin(), text.end());
        if (!document.is_object() || !document.contains("stars_profile") || !document["stars_profile"].is_object())
            return invalid("a star-profile document needs one stars_profile object");
        const Json& root = document["stars_profile"];
        const auto engine = root.find("engine");
        const auto version = root.find("version");
        if (version != root.end() && !version->is_number_integer())
            return invalid("the star-profile schema version is not an integer");
        const int schema = version == root.end() ? 0 : version->get<int>();
        if (version != root.end() && schema == 1)
        {
            auto payload = parseV1(root, limits);
            if (!payload)
                return payload.status();
            return StarProfileFile{.generation = StarProfileGeneration::Version1, .payload = std::move(*payload)};
        }
        if (engine != root.end() && !engine->is_string())
            return invalid("the star-profile engine is not text");
        const std::string engineName = engine == root.end() ? std::string{} : engine->get<std::string>();
        if (version == root.end() && engineName == "v1")
        {
            auto payload = parseV1(root, limits);
            if (!payload)
                return payload.status();
            return StarProfileFile{.generation = StarProfileGeneration::Version1, .payload = std::move(*payload)};
        }
        if (engineName == "v2" && schema == 2)
        {
            auto payload = parseV2(root, limits);
            if (!payload)
                return payload.status();
            return StarProfileFile{.generation = StarProfileGeneration::Version2, .payload = std::move(*payload)};
        }
        return unsupported("the star-profile engine or schema version is not supported");
    }
    catch (const Json::exception&)
    {
        return invalid("the text is not JSON whose members have the star-profile schema's types");
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

Result<std::string> writeStarProfileDocument(const StarProfileFile& profile, const StarProfileReadLimits& limits)
{
    if (!validLimits(limits))
        return Status::failure(ErrorCode::InvalidArgument, {.message = "star-profile bounds are positive",
                                                            .operation = "an::writeStarProfileDocument",
                                                            .domain = ErrorDomain::Io});
    const bool v1 = profile.generation == StarProfileGeneration::Version1;
    const bool v2 = profile.generation == StarProfileGeneration::Version2;
    if ((!v1 && !v2) || (v1 && !std::holds_alternative<StarProfileV1>(profile.payload)) ||
        (v2 && !std::holds_alternative<StarProfileV2>(profile.payload)))
        return Status::failure(ErrorCode::IncompatibleArguments,
                               {.message = "the profile generation and payload alternative disagree",
                                .operation = "an::writeStarProfileDocument",
                                .domain = ErrorDomain::Io});
    try
    {
        Json document;
        Json& root = document["stars_profile"];
        if (v1)
        {
            const StarProfileV1& value = std::get<StarProfileV1>(profile.payload);
            if (value.stars.size() > limits.maximumStars)
                return limit("the star array exceeds the configured bound", value.stars.size(), limits.maximumStars);
            writeV1(root, value);
        }
        else
        {
            const StarProfileV2& value = std::get<StarProfileV2>(profile.payload);
            if (value.stars.size() > limits.maximumStars)
                return limit("the star array exceeds the configured bound", value.stars.size(), limits.maximumStars);
            for (const StarProfileV2Star& star : value.stars)
                if (star.name.size() > limits.maximumStringBytes ||
                    star.astrometry.units.size() > limits.maximumStringBytes ||
                    star.astrometry.catalogId.size() > limits.maximumStringBytes)
                    return limit(
                        "a star-profile string exceeds the configured bound",
                        (std::max)({star.name.size(), star.astrometry.units.size(), star.astrometry.catalogId.size()}),
                        limits.maximumStringBytes);
            if (profileSpelling(value.parameters.profile) == nullptr)
                return Status::failure(ErrorCode::InvalidArgument,
                                       {.message = "the persisted PSF model is not one this build knows",
                                        .operation = "an::writeStarProfileDocument",
                                        .domain = ErrorDomain::Io});
            for (const StarProfileV2Star& star : value.stars)
                if (profileSpelling(star.psf.profile) == nullptr)
                    return Status::failure(ErrorCode::InvalidArgument,
                                           {.message = "a persisted star's PSF model is not one this build knows",
                                            .operation = "an::writeStarProfileDocument",
                                            .domain = ErrorDomain::Io});
            writeV2(root, value);
        }
        std::string text = document.dump(1);
        if (text.size() > limits.maximumFileBytes)
            return limit("the serialized star-profile document exceeds the configured bound", text.size(),
                         limits.maximumFileBytes);
        return text;
    }
    catch (const Json::type_error&)
    {
        return Status::failure(ErrorCode::InvalidArgument, {.message = "a star-profile string is not UTF-8",
                                                            .operation = "an::writeStarProfileDocument",
                                                            .domain = ErrorDomain::Io});
    }
    catch (const std::exception& error)
    {
        return translateException(kBoundary, error);
    }
    catch (...)
    {
        return translateUnknownException(kBoundary);
    }
}

} // namespace an
