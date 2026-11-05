// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The closed maps from the text the product persists to what the engine is asked for: a demosaic setting to one of
/// the published demosaic methods, a Bayer-pattern choice to a stated filter array, and the method a chosen one becomes
/// over a mosaic of the other shape.
///
/// Both texts are durable - the settings database stores the demosaic key, and sessions, presets and dialog settings
/// store the pattern text - so each is read through a fixed table rather than through any engine name. A demosaic key
/// the table does not hold reads as RCD, the setting's default, and "Auto" is never a guessed pattern: it means the
/// filter array the image's own description states, and a mosaic that states none is refused. Every method a setting
/// or a dialog names is written for a two-by-two Bayer block, so over an X-Trans sensor's six-by-six table it runs as
/// its tabulated counterpart, and a stated two-by-two pattern over such an image is refused by name.
#pragma once

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <QString>

#include <array>
#include <optional>
#include <utility>

namespace an::ui
{

/// The demosaic method a persisted setting names.
struct DemosaicChoice
{
    /// The method.
    an::DemosaicMethod method = an::DemosaicMethod::Rcd;
};

/// The demosaic method a persisted demosaic setting names: `vng4`, `ahd`, `amaze`, `lmmse` and `rcd` the methods of
/// those names and `bilinear` the fast estimate the engine names `DemosaicMethod::Fast`, case and surrounding spaces
/// aside; anything else reads as RCD, the setting's default.
///
/// @param key the setting's value.
/// @return the method.
[[nodiscard]] inline DemosaicChoice demosaicMethodFromSettingsKey(const QString& key) noexcept
{
    struct Entry
    {
        const char* key;
        an::DemosaicMethod method;
    };
    static constexpr std::array<Entry, 6u> kEntries{{{"bilinear", an::DemosaicMethod::Fast},
                                                     {"vng4", an::DemosaicMethod::Vng4},
                                                     {"ahd", an::DemosaicMethod::LuminanceAhd},
                                                     {"amaze", an::DemosaicMethod::Amaze},
                                                     {"lmmse", an::DemosaicMethod::Lmmse},
                                                     {"rcd", an::DemosaicMethod::Rcd}}};
    const QString name = key.trimmed().toLower();
    for (const Entry& entry : kEntries)
    {
        if (name == QLatin1String(entry.key))
        {
            return {entry.method};
        }
    }
    return {an::DemosaicMethod::Rcd};
}

/// The method a mosaic is demosaiced with when the product chose `chosen` for it: the chosen method where it applies to
/// the mosaic's shape, and its counterpart for the other shape where it does not. Every method a setting or a dialog
/// offers is written for a two-by-two Bayer block. Over a tabulated mosaic - an X-Trans sensor's six-by-six table - the
/// two fast estimates, `Fast` (the "bilinear" setting) and `Bilinear`, become `XTransFast`, and every other method
/// becomes `MarkesteijnThreePass`, the estimate an X-Trans RAW decoder makes of every such frame. Over a
/// two-by-two mosaic `XTransFast` becomes `Fast` and either Markesteijn pass becomes `Rcd`, the setting's default, so
/// every pairing is answered with a method that applies. An image that states no filter array is answered with the
/// chosen method, which the engine then refuses as it did.
///
/// @param chosen the method a setting, a dialog or a caller chose.
/// @param mosaic the filter array the mosaic is demosaiced under.
/// @return the method that applies to the mosaic.
[[nodiscard]] constexpr an::DemosaicMethod demosaicMethodForMosaic(an::DemosaicMethod chosen,
                                                                   const an::CfaDescriptor& mosaic) noexcept
{
    if (!mosaic.isMosaiced() || an::demosaicMethodAppliesTo(chosen, mosaic.pattern()))
    {
        return chosen;
    }
    if (an::cfaPatternIsTabulated(mosaic.pattern()))
    {
        return chosen == an::DemosaicMethod::Fast || chosen == an::DemosaicMethod::Bilinear
                   ? an::DemosaicMethod::XTransFast
                   : an::DemosaicMethod::MarkesteijnThreePass;
    }
    return chosen == an::DemosaicMethod::XTransFast ? an::DemosaicMethod::Fast : an::DemosaicMethod::Rcd;
}

/// The text a Bayer-pattern choice persists for "the filter array the image states", shown as "From metadata" where a
/// dialog offers it.
inline const QString kBayerPatternFromImageText = QStringLiteral("Auto");

/// The texts of the four two-by-two filter arrays a choice can state, in the order the dialogs list them.
inline const QString kBayerPatternRggbText = QStringLiteral("RGGB");
inline const QString kBayerPatternBggrText = QStringLiteral("BGGR");
inline const QString kBayerPatternGbrgText = QStringLiteral("GBRG");
inline const QString kBayerPatternGrbgText = QStringLiteral("GRBG");

/// What a persisted Bayer-pattern choice names.
struct BayerPatternChoice
{
    /// The filter array the choice states, or nothing when it asks for the one the image's description states.
    std::optional<an::CfaPattern> stated;

    /// Whether the text is one of the five a choice persists; an unknown text reads as the image's own.
    bool known = true;
};

/// What a persisted Bayer-pattern text names: one of the four two-by-two arrays, or "Auto" for the image's own.
///
/// @param text the persisted text, compared exactly.
/// @return the choice.
[[nodiscard]] inline BayerPatternChoice bayerPatternChoiceFromText(const QString& text)
{
    static const std::array<std::pair<QString, an::CfaPattern>, 4u> kStated{
        {{kBayerPatternRggbText, an::CfaPattern::Rggb},
         {kBayerPatternBggrText, an::CfaPattern::Bggr},
         {kBayerPatternGbrgText, an::CfaPattern::Gbrg},
         {kBayerPatternGrbgText, an::CfaPattern::Grbg}}};
    for (const auto& [name, pattern] : kStated)
    {
        if (text == name)
        {
            return {pattern, true};
        }
    }
    return {std::nullopt, text == kBayerPatternFromImageText};
}

/// The filter array a mosaic is demosaiced under, by a persisted choice.
///
/// A stated pattern is placed at the image's origin. "Auto" is the filter array the image's description states, phase
/// included, and nothing is guessed: an image whose description states no filter array is refused. A stated pattern
/// over an image that states a tabulated (X-Trans) array is refused too, naming it: a two-by-two pattern does not
/// describe that sensor's samples, and demosaicing them under one would make colour of nothing they recorded.
///
/// @param choice the choice.
/// @param image the image's description.
/// @return the filter array; `ErrorCode::IncompatibleArguments` when the choice reads from the description and the
///         image states no filter array, or states a pattern over an image that states an X-Trans array; or
///         `ErrorCode::Internal` when a stated pattern has no descriptor.
[[nodiscard]] inline an::Result<an::CfaDescriptor> resolveBayerPattern(const BayerPatternChoice& choice,
                                                                       const an::ImageDescriptor& image)
{
    if (!choice.stated.has_value())
    {
        if (!image.isMosaiced())
        {
            return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                       "the image states no colour filter array to demosaic under");
        }
        return image.cfa();
    }
    if (image.isMosaiced() && an::cfaPatternIsTabulated(image.cfa().pattern()))
    {
        return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                   "the image states an X-Trans filter array, which a two-by-two Bayer pattern does "
                                   "not describe; From metadata demosaics it under its own");
    }
    const std::optional<an::CfaDescriptor> stated = an::CfaDescriptor::create(*choice.stated, 0u, 0u);
    if (!stated.has_value())
    {
        return an::Status::failure(an::ErrorCode::Internal, "a stated filter array has no description");
    }
    return *stated;
}

} // namespace an::ui
