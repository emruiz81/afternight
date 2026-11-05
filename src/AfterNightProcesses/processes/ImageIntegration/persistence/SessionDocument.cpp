// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file SessionDocument.cpp
 * @brief The session document's format 3 encoding and its strict reader.
 */

#include "stdafx.hpp"
#include "SessionDocument.h"

#include "processes/ImageIntegration/core/PersistedChoiceMaps.h"
#include "processes/ImageIntegration/core/RegistrationHashUtils.h"

#include <QJsonArray>
#include <QJsonValue>

#include <span>

namespace ImageIntegration
{

namespace
{

/// The text the model holds for one key of a choice.
struct Spelling
{
    const char* key;
    const char* text;
};

// Each table is the text the product's own dialogs and defaults put in the model for a key. A field whose table is
// empty holds the key itself.
constexpr Spelling kStackMethodSpellings[] = {{"average", "Average"},
                                              {"sum", "Sum"},
                                              {"median", "Median"},
                                              {"minimum", "Minimum"},
                                              {"maximum", "Maximum"},
                                              {"auto-adaptive-weighted-average", "Auto-Adaptive Weighted Average"},
                                              {"sigma-clipping", "Sigma Clipping"},
                                              {"mad-sigma-clipping", "MAD Clipping"},
                                              {"median-sigma", "Median Sigma"},
                                              {"winsorized-sigma-clipping", "Winsorized Sigma Clipping"},
                                              {"linear-fit-clipping", "Linear Fit"},
                                              {"percentile-clipping", "Percentile"},
                                              {"gesdt", "GESDT"}};

constexpr Spelling kRejectionSpellings[] = {{"none", "None"},
                                            {"sigma-clipping", "Sigma Clipping"},
                                            {"mad-sigma-clipping", "MAD Clipping"},
                                            {"median-sigma", "Median Sigma"},
                                            {"winsorized-sigma-clipping", "Winsorized Sigma Clipping"},
                                            {"linear-fit-clipping", "Linear Fit"},
                                            {"percentile-clipping", "Percentile"},
                                            {"gesdt", "GESDT"}};

constexpr Spelling kWeightingSpellings[] = {
    {"none", "None"},   {"star-count", "Number of stars"},   {"weighted-fwhm", "Weighted FWHM"},
    {"noise", "Noise"}, {"image-count", "Number of images"}, {"photometric", "Photometric"}};

constexpr Spelling kStarCountScalingSpellings[] = {{"linear", "Linear"},
                                                   {"quadratic", "Quadratic"},
                                                   {"exponential", "Exponential"},
                                                   {"roundness-weighted", "Roundness Weighted"}};

constexpr Spelling kDrizzleKernelSpellings[] = {{"square", "Square"},     {"point", "Point"},
                                                {"turbo", "Turbo"},       {"gaussian", "Gaussian"},
                                                {"lanczos2", "Lanczos2"}, {"lanczos3", "Lanczos3"}};

constexpr Spelling kCfaSpellings[] = {
    {"auto", "Auto"}, {"rggb", "RGGB"}, {"bggr", "BGGR"}, {"gbrg", "GBRG"}, {"grbg", "GRBG"}};

constexpr Spelling kInterpolationSpellings[] = {{"auto", "Auto"},
                                                {"nearest", "Nearest Neighbor"},
                                                {"bilinear", "Bilinear"},
                                                {"bicubic", "Bicubic"},
                                                {"area", "Pixel Area Relation"},
                                                {"lanczos4", "Lanczos4"},
                                                {"bilinear-exact", "Bit Exact Bilinear"},
                                                {"nearest-exact", "Bit Exact Nearest Neighbor"}};

constexpr Spelling kDemosaicSpellings[] = {{"auto", "default"}, {"fast", "Bilinear"}, {"vng4", "VNG4"},  {"ahd", "AHD"},
                                           {"amaze", "AMaZE"},  {"rcd", "RCD"},       {"lmmse", "LMMSE"}};

constexpr Spelling kTransformSpellings[] = {{"similarity", "Similarity"},
                                            {"affine", "Affine"},
                                            {"homography", "Homography"},
                                            {"polynomial2", "Polynomial 2"},
                                            {"polynomial3", "Polynomial 3"},
                                            {"polynomial4", "Polynomial 4"},
                                            {"auto", "Auto"}};

/// One stored choice: the field, its table, and the spelling of each key in the model.
struct ChoiceField
{
    const char* field;
    PersistedChoice choice;
    std::span<const Spelling> spellings;
    bool integer = false;
};

constexpr ChoiceField kStackingFields[] = {
    {"stackMethod", PersistedChoice::StackMethod, kStackMethodSpellings},
    {"rejectionMethod", PersistedChoice::RejectionMethod, kRejectionSpellings},
    {"weighting", PersistedChoice::Weighting, kWeightingSpellings},
    {"starCountScaling", PersistedChoice::StarCountScaling, kStarCountScalingSpellings},
    {"normalizationMode", PersistedChoice::NormalizationMode, {}, true},
    {"globalNormalizationMethod", PersistedChoice::GlobalNormalizationMethod, {}, true},
    {"scaleEstimator", PersistedChoice::ScaleEstimator, {}, true},
    {"normalizationReferenceMode", PersistedChoice::NormalizationReferenceMode, {}, true},
    {"registeredOutputPolicy", PersistedChoice::RegisteredOutputPolicy, {}, true}};

constexpr ChoiceField kDrizzleFields[] = {{"kernel", PersistedChoice::DrizzleKernel, kDrizzleKernelSpellings},
                                          {"cfaPattern", PersistedChoice::CfaPattern, kCfaSpellings}};

constexpr ChoiceField kRegistrationFields[] = {
    {"alignmentMode", PersistedChoice::AlignmentMode, {}},
    {"interpolation", PersistedChoice::Interpolation, kInterpolationSpellings},
    {"cfaPattern", PersistedChoice::CfaPattern, kCfaSpellings},
    {"debayerMethod", PersistedChoice::DemosaicMethod, kDemosaicSpellings},
    {"transformation", PersistedChoice::TransformModel, kTransformSpellings}};

constexpr ChoiceField kChromaticAberrationFields[] = {
    {"transformType", PersistedChoice::TransformModel, kTransformSpellings},
    {"interpolation", PersistedChoice::Interpolation, kInterpolationSpellings}};

constexpr ChoiceField kModelFields[] = {
    {"lightCalibrationCacheMode", PersistedChoice::LightCalibrationCacheMode, {}, true}};

constexpr const char* kFrameTypes[] = {"BIAS", "DARK", "FLAT", "LIGHT"};

enum class Direction
{
    /// From the stored document into the model's JSON.
    Read,
    /// From the model's JSON into a format 3 document.
    Write
};

/// A walk over one document, collecting the first refusal.
struct Walk
{
    Direction direction;
    QString context;
    QString refusal;

    [[nodiscard]] bool refused() const noexcept
    {
        return !refusal.isEmpty();
    }

    void refuse(const QString& where, const char* field, const QJsonValue& value)
    {
        if (refused())
        {
            return;
        }
        const QString text = value.isString()   ? QStringLiteral("'%1'").arg(value.toString())
                             : value.isDouble() ? QString::number(value.toDouble())
                                                : QStringLiteral("a value of another type");
        refusal = QStringLiteral("The %1 stores %2 for %3 %4, which this version does not know; it was not changed.")
                      .arg(context, text, where, QString::fromLatin1(field));
    }

    void refuseImagePoints(const QString& where, const char* field)
    {
        if (refused())
        {
            return;
        }
        refusal = QStringLiteral("The %1 stores %2 %3 without the pixel convention its positions are in; it was not "
                                 "changed.")
                      .arg(context, where, QString::fromLatin1(field));
    }
};

[[nodiscard]] QString spellingOf(const ChoiceField& field, const QString& key)
{
    if (field.spellings.empty())
    {
        return key;
    }
    for (const Spelling& spelling : field.spellings)
    {
        if (key == QLatin1String(spelling.key))
        {
            return QString::fromLatin1(spelling.text);
        }
    }
    return {};
}

/// Reads or writes one field of an object in place.
void convertField(QJsonObject& object, const ChoiceField& field, const QString& where, Walk& walk)
{
    const QString name = QString::fromLatin1(field.field);
    if (walk.refused() || !object.contains(name))
    {
        return;
    }
    const QJsonValue value = object.value(name);

    // What the stored or held value means: a key of the field's table, or nothing.
    PersistedChoiceValue meaning;
    if (walk.direction == Direction::Read)
    {
        // A format 3 document holds keys and nothing else; a key reads as itself.
        if (value.isString() && persistedChoiceKeys(field.choice).contains(value.toString()))
        {
            meaning = PersistedChoiceValue{value.toString(), true};
        }
    }
    else if (field.integer)
    {
        if (value.isDouble() && value.toDouble() == static_cast<double>(value.toInt()))
        {
            meaning = persistedChoiceFromInteger(field.choice, value.toInt());
        }
    }
    else if (value.isString())
    {
        meaning = persistedChoiceFromText(field.choice, value.toString());
    }
    if (!meaning.known)
    {
        walk.refuse(where, field.field, value);
        return;
    }

    if (walk.direction == Direction::Write)
    {
        object.insert(name, meaning.key);
        return;
    }
    if (field.integer)
    {
        object.insert(name, *integerForKey(field.choice, meaning.key));
        return;
    }
    const QString spelling = spellingOf(field, meaning.key);
    if (spelling.isEmpty())
    {
        walk.refuse(where, field.field, value);
        return;
    }
    object.insert(name, spelling);
}

void convertFields(QJsonObject& object, const std::span<const ChoiceField> fields, const QString& where, Walk& walk)
{
    for (const ChoiceField& field : fields)
    {
        convertField(object, field, where, walk);
    }
}

[[nodiscard]] QString groupName(const QJsonObject& record)
{
    const QJsonValue key = record.value(QStringLiteral("groupKey"));
    if (key.isObject())
    {
        return key.toObject().value(QStringLiteral("value")).toString();
    }
    return key.toString();
}

/// Refuses a stored set of image points that does not state the engine's pixel convention: its positions would be read
/// half a pixel from where they stand were it another convention's.
void checkImagePoints(const QJsonObject& registration, const QString& where, Walk& walk)
{
    const QLatin1String conventionKey(RegistrationHashUtils::kImagePointPixelConventionKey);
    const QLatin1String convention(RegistrationHashUtils::kImagePointPixelConvention);
    for (const QJsonValue& set : registration.value(QStringLiteral("manualControlPoints")).toArray())
    {
        if (set.toObject().value(conventionKey).toString() != convention)
        {
            walk.refuseImagePoints(where, "manualControlPoints");
            return;
        }
    }
    const QJsonValue comet = registration.value(QStringLiteral("cometTracking"));
    if (comet.isObject() && comet.toObject().value(conventionKey).toString() != convention)
    {
        walk.refuseImagePoints(where, "cometTracking");
    }
}

/// Walks every choice of a document - the model's own JSON or a stored one - in place.
void walkDocument(QJsonObject& document, Walk& walk)
{
    convertFields(document, kModelFields, QStringLiteral("the session's"), walk);

    QJsonObject stackingRoot = document.value(QStringLiteral("stackingSettings")).toObject();
    for (const char* type : kFrameTypes)
    {
        const QString typeName = QString::fromLatin1(type);
        if (!stackingRoot.contains(typeName))
        {
            continue;
        }
        QJsonArray records = stackingRoot.value(typeName).toArray();
        for (qsizetype index = 0; index < records.size() && !walk.refused(); ++index)
        {
            QJsonObject record = records.at(index).toObject();
            QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
            const QString where = QStringLiteral("the %1 group '%2' stacking setting").arg(typeName, groupName(record));
            convertFields(settings, kStackingFields, where, walk);
            if (settings.value(QStringLiteral("drizzle")).isObject())
            {
                QJsonObject drizzle = settings.value(QStringLiteral("drizzle")).toObject();
                convertFields(drizzle, kDrizzleFields, where + QStringLiteral(" drizzle"), walk);
                settings.insert(QStringLiteral("drizzle"), drizzle);
            }
            record.insert(QStringLiteral("settings"), settings);
            records.replace(index, record);
        }
        stackingRoot.insert(typeName, records);
    }
    if (document.contains(QStringLiteral("stackingSettings")))
    {
        document.insert(QStringLiteral("stackingSettings"), stackingRoot);
    }

    if (!document.contains(QStringLiteral("registrationSettings")))
    {
        return;
    }
    QJsonArray registrations = document.value(QStringLiteral("registrationSettings")).toArray();
    for (qsizetype index = 0; index < registrations.size() && !walk.refused(); ++index)
    {
        QJsonObject record = registrations.at(index).toObject();
        QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
        const QString where = QStringLiteral("the registration group '%1' setting").arg(groupName(record));
        convertFields(settings, kRegistrationFields, where, walk);
        if (settings.value(QStringLiteral("chromaticAberration")).isObject())
        {
            QJsonObject aberration = settings.value(QStringLiteral("chromaticAberration")).toObject();
            convertFields(aberration, kChromaticAberrationFields, where + QStringLiteral(" chromatic aberration"),
                          walk);
            settings.insert(QStringLiteral("chromaticAberration"), aberration);
        }
        if (walk.direction == Direction::Read)
        {
            checkImagePoints(settings, where, walk);
        }
        record.insert(QStringLiteral("settings"), settings);
        registrations.replace(index, record);
    }
    document.insert(QStringLiteral("registrationSettings"), registrations);
}

} // namespace

int storedSessionSchema(const QJsonObject& stored)
{
    const QJsonValue value = stored.value(QLatin1String(kSessionSchemaKey));
    if (value.isUndefined())
    {
        return 0;
    }
    if (!value.isDouble() || value.toDouble() != static_cast<double>(value.toInt()) || value.toInt() <= 0)
    {
        return -1;
    }
    return value.toInt();
}

QString sessionFormatRefusal(const QString& context, const int storedSchema)
{
    if (storedSchema == 0)
    {
        return QStringLiteral("The %1 states no format version; this version of AfterNight reads format %2.")
            .arg(context)
            .arg(kSessionSchemaVersion);
    }
    if (storedSchema < 0)
    {
        return QStringLiteral("The %1 states a format version that is not a format number; this version of AfterNight "
                              "reads format %2.")
            .arg(context)
            .arg(kSessionSchemaVersion);
    }
    return QStringLiteral("The %1 was written in format %2; this version of AfterNight reads format %3.")
        .arg(context)
        .arg(storedSchema)
        .arg(kSessionSchemaVersion);
}

SessionDocumentResult readSessionDocument(const QJsonObject& stored, const QString& context)
{
    SessionDocumentResult result;
    result.storedSchema = storedSessionSchema(stored);
    if (result.storedSchema != kSessionSchemaVersion)
    {
        result.refusal = sessionFormatRefusal(context, result.storedSchema);
        return result;
    }
    QJsonObject document = stored;
    document.remove(QLatin1String(kSessionSchemaKey));
    Walk walk{Direction::Read, context, {}};
    walkDocument(document, walk);
    if (walk.refused())
    {
        result.refusal = walk.refusal;
        return result;
    }
    result.document = std::move(document);
    return result;
}

SessionDocumentResult writeSessionDocument(const QJsonObject& model, const QString& context)
{
    SessionDocumentResult result;
    result.storedSchema = kSessionSchemaVersion;
    QJsonObject document = model;
    Walk walk{Direction::Write, context, {}};
    walkDocument(document, walk);
    if (walk.refused())
    {
        result.refusal = walk.refusal;
        return result;
    }
    document.insert(QLatin1String(kSessionSchemaKey), kSessionSchemaVersion);
    result.document = std::move(document);
    return result;
}

} // namespace ImageIntegration
