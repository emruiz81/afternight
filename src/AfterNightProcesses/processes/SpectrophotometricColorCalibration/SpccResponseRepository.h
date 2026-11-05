// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The SPCC response repository, read: the throughput profiles and white references a repository directory holds, as
/// the engine's response profiles and spectra, with what each profile's author says it models and the camera and
/// filter names it answers to.
///
/// **The engine opens no file and resolves no camera name.** Its spectral response model takes a `ResponseProfile` of
/// three curves and a `WhiteReferenceSpectrum`, and states one rule about a profile and a frame -
/// `checkResponseProfileCompatibility` - over facts the host supplies. The repository's schema, its checksums, its
/// rights review, its alias registry and the identifiers that name a one-shot-colour camera behind a filter and a
/// low-pass window are this product's, read here:
///
/// - `response_meta.json` names the components, the white references and the throughput profiles; every asset is a
///   `spcc-response-asset-v1` document whose identity, units and data digest agree with its entry.
/// - A profile's channel is the product of its components, each resampled onto the sampled XP grid
///   (`composeSpectralCurves`); a channel's components are named by `channel_component_ids`, or else are the profile's
///   components whose own channel is that channel.
/// - A profile named `<prefix>osc_sensor.<sensor>.osc_filter.<filter>.osc_lpf.<window>.<suffix>` the repository does
///   not hold is synthesized from the sensor's profile, the sensor-with-filter profile and the sensor-with-window
///   profile (`combineFilterResponses`), all three anchored on one white reference.
/// - The identifier's prefix states the instrument (`ResponseProfileKind`): a monochrome sensor alone, a monochrome
///   sensor through a broadband filter set, or a colour sensor. The prefix decides whether the sky's extinction and a
///   narrowband integration apply; the kind is how the engine is told.
/// - A camera and filter name resolve to a profile only through an alias that names both exactly, compared trimmed and
///   without regard to case; two such profiles are ambiguous.
#pragma once

#include "AfterNightProcessesExport.h"

#include <AfterNightEngine/Algorithms/Spectrophotometry/SpectralResponse.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>

#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

namespace an
{
class ExecutionContext;
} // namespace an

namespace an::ui::processes
{

/// The schema `response_meta.json` is written to.
inline constexpr const char kSpccResponseRepositorySchema[] = "spcc-response-repository-v1";

/// The schema every component and white-reference document is written to.
inline constexpr const char kSpccResponseAssetSchema[] = "spcc-response-asset-v1";

/// A camera and filter a profile answers to.
struct SpccProfileAlias
{
    /// The camera model, as the image's metadata names it; empty for any.
    QString cameraModel;

    /// The filter, as the image's metadata names it; empty for none.
    QString filterName;
};

/// One throughput profile of a repository.
struct SpccRepositoryProfile
{
    /// The three channel curves, composed on the sampled XP grid, and the instrument they describe.
    an::ResponseProfile profile;

    /// The white reference the profile is anchored on.
    QString whiteReferenceId;

    /// The components each channel was composed of, red, green and blue.
    std::array<QStringList, 3u> channelComponentIds;

    /// What the profile's author says it models.
    an::ResponseProfileCapabilities capabilities;

    /// The camera and filter names it answers to.
    std::vector<SpccProfileAlias> aliases;
};

/// One white reference of a repository.
struct SpccRepositoryWhiteReference
{
    /// The spectrum.
    an::WhiteReferenceSpectrum spectrum;

    /// The name a person reads; empty when the repository gives none.
    QString displayName;
};

/// How a camera and a filter name resolved against a repository's aliases.
struct SpccAliasResolution
{
    /// The profiles whose alias names both exactly, in repository order.
    QStringList candidateProfileIds;

    /// The one profile, when exactly one matched.
    std::optional<QString> profileId;

    /// Whether more than one matched.
    bool ambiguous = false;
};

/// The instrument a profile identifier states.
///
/// @param profileId the identifier.
/// @return `MonoSensorRgbFilters` for a monochrome sensor's profile through a broadband filter set, `MonoSensor` for a
///         monochrome sensor's or a monochrome filter's own, `ColorSensor` otherwise.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ResponseProfileKind spccResponseProfileKind(const QString& profileId);

/// Whether a rights review status admits an asset: approved for managed download or for bundling.
///
/// @param status the status, as written.
/// @return true for the two approved statuses.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool isApprovedSpccResponseRights(const QString& status);

/// The colour basis a frame's recorded channel basis stands for in the engine's compatibility check: linear RGB is
/// `ColorSpace::LinearRgb`, display RGB `ColorSpace::DisplayRgb`, and camera RGB - the sensor's own primaries, which
/// claim no colorimetric basis - `ColorSpace::Unspecified`.
///
/// @param basis the recorded basis.
/// @return the colour space; `ColorSpace::Unspecified` for every other basis.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::ColorSpace spccColorSpaceOfBasis(an::RecordedChannelBasis basis) noexcept;

/// What a frame's processing state says about its colour, for the engine's compatibility check.
///
/// @param state the frame's processing state.
/// @return the frame's colour state.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::FrameColorState spccFrameColorState(const an::ProcessingStateRecord& state);

/// A response repository directory, read whole.
class AFTERNIGHTPROCESSES_API SpccResponseRepository
{
public:
    /// Reads a repository: its metadata, every component and white reference, and every throughput profile composed
    /// on the sampled XP grid.
    ///
    /// @param execution the operation reading it.
    /// @param directory the repository's directory, holding `response_meta.json`.
    /// @return the repository; `ErrorCode::ExternalFailure` naming a file that cannot be read;
    ///         `ErrorCode::InvalidData` naming what in a document disagrees with the schema; or what composing a
    ///         channel refused.
    [[nodiscard]] static an::Result<SpccResponseRepository> load(const an::ExecutionContext& execution,
                                                                 const QString& directory);

    /// The repository's identity.
    [[nodiscard]] const QString& repositoryId() const noexcept;

    /// Its version.
    [[nodiscard]] const QString& version() const noexcept;

    /// The version of its alias registry, which a saved camera mapping names.
    [[nodiscard]] const QString& aliasRegistryVersion() const noexcept;

    /// Every profile, in repository order.
    [[nodiscard]] const std::vector<SpccRepositoryProfile>& profiles() const noexcept;

    /// Every white reference, in repository order.
    [[nodiscard]] const std::vector<SpccRepositoryWhiteReference>& whiteReferences() const noexcept;

    /// A profile the repository holds, or null.
    [[nodiscard]] const SpccRepositoryProfile* findProfile(const QString& profileId) const noexcept;

    /// A white reference the repository holds, or null.
    [[nodiscard]] const SpccRepositoryWhiteReference*
    findWhiteReference(const QString& whiteReferenceId) const noexcept;

    /// A profile by identifier: one the repository holds, or a one-shot-colour camera behind a filter and a window
    /// synthesized from the three profiles it is measured against.
    ///
    /// @param profileId the identifier.
    /// @return the profile; `ErrorCode::InvalidArgument` when the repository neither holds it nor holds the three it
    ///         would be synthesized from; `ErrorCode::InvalidData` when those three are anchored on different white
    ///         references or share no wavelength interval.
    [[nodiscard]] an::Result<SpccRepositoryProfile> resolveProfile(const QString& profileId) const;

    /// The profiles a camera and filter name resolve to through the aliases.
    ///
    /// @param cameraModel the camera model the image names.
    /// @param filterName the filter the image names.
    /// @return the candidates and, when there is exactly one, the profile.
    [[nodiscard]] SpccAliasResolution resolveAlias(const QString& cameraModel, const QString& filterName) const;

private:
    QString m_repositoryId;
    QString m_version;
    QString m_aliasRegistryVersion;
    QString m_rightsReviewStatus;
    std::vector<SpccRepositoryProfile> m_profiles;
    std::vector<SpccRepositoryWhiteReference> m_whiteReferences;
};

} // namespace an::ui::processes
