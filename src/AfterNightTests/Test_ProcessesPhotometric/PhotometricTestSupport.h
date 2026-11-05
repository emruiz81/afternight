// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the photometric and spectrophotometric colour calibrations' cases share: plate-solved RGB star fields whose
/// stars' colours are known, a Gaia catalog and an SPCC spectrum catalog of those stars and a response repository
/// written as the product installs them, the targets the windows open on, and the waiting a case does for a window's
/// readiness and runs to land.
///
/// **A field's colours are planted, so the gains a calibration should find are known.** The photometric field gives
/// each star a Gaia BP minus RP colour and renders its red and blue through the colour model the calibration predicts
/// from, times a cast per channel; a correct calibration's gains undo the cast. The spectrophotometric field gives each
/// star a sampled spectrum, integrates it through the repository's response the way the engine does to learn its
/// colour, and renders it times a cast; a correct calibration's gains undo the cast and the white reference's colour.
#pragma once

#include "Test_ProcessesShared/ProcessesSharedTestSupport.h"

#include "processes/PhotometricColorCalibration/PhotometricColorCalibrationProcess.h"
#include "processes/SpectrophotometricColorCalibration/SpectrophotometricColorCalibrationProcess.h"

#include <AfterNightEngine/Algorithms/Astrometry/WcsSolution.h>
#include <AfterNightEngine/IO/ProcessingStateCards.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageMetadata.h>

#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::photometric
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// The plate solution's sky: square pixels of 2.4 arcseconds, turned 17 degrees, about the frame's centre.
inline constexpr double kScaleArcsec = 2.4;

/// One star of a field.
struct PlantedStar
{
    /// Where it is, in engine pixel coordinates.
    double x = 0.0;
    double y = 0.0;

    /// Its peak in each channel, red, green and blue.
    std::array<double, 3u> amplitude{};

    /// Where the solution puts it on the sky.
    an::SkyCoordinate direction{};

    /// Its Gaia G magnitude.
    float magnitude = 12.0f;

    /// Its Gaia BP minus RP colour, for the photometric field.
    double colorIndex = 0.0;

    /// Its catalog colour relative to green, red, green and blue, for the spectrophotometric field.
    std::array<double, 3u> catalogRatios{1.0, 1.0, 1.0};

    /// Its Gaia source identifier, which names its order-twelve tile.
    std::uint64_t sourceIdentifier = 0u;

    /// Its sampled spectrum, for the spectrophotometric field, as the catalog stores it.
    std::vector<float> spectrum;
};

/// A rendered RGB star field and the solution that places it.
struct StarField
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::optional<an::WcsSolution> solution;
    std::vector<PlantedStar> stars;

    /// The sky in each channel, red, green and blue, as a fraction of full scale.
    std::array<double, 3u> background{0.05, 0.06, 0.04};

    /// Planar samples, red then green then blue, as fractions of full scale.
    std::vector<float> samples;

    /// A sample of the field.
    [[nodiscard]] float value(std::uint32_t x, std::uint32_t y, std::uint32_t channel) const
    {
        return samples[(static_cast<std::size_t>(channel) * height + y) * width + x];
    }
};

/// The photometric field: sixty stars of colours from -0.3 to 2.1, their red and blue multiplied by the casts.
///
/// @param redCast what the red channel is multiplied by, beyond the star's colour.
/// @param blueCast what the blue channel is multiplied by.
/// @return the field.
[[nodiscard]] StarField makePhotometricField(double redCast, double blueCast);

/// Writes a Gaia chunk catalog of a field's stars - their positions, magnitudes and colours - into a directory, as the
/// downloader installs one.
///
/// @param directory the directory, which is created.
/// @param field the field.
/// @param starCount how many of the field's stars the catalog holds, or all of them.
/// @return whether it was written.
[[nodiscard]] bool writeGaiaCatalog(const QString& directory, const StarField& field,
                                    std::optional<std::size_t> starCount = std::nullopt);

/// The response profile identifier the repository's test camera answers to.
inline constexpr const char kSpccProfileId[] = "afternight.spcc.profile.osc_sensor.testcam.default";

/// The white reference the repository anchors the profile on.
inline constexpr const char kSpccWhiteReferenceId[] = "afternight.spcc.white_reference.flat";

/// A second white reference the repository offers: twice as bright at the red end as at the blue.
inline constexpr const char kSpccWarmWhiteReferenceId[] = "afternight.spcc.white_reference.warm";

/// The camera model the repository's alias names.
inline constexpr const char kSpccCameraModel[] = "Test Camera";

/// The SPCC catalog's manifest identity.
inline constexpr const char kSpccManifestId[] = "spcc-test-field-v1";

/// The spectrophotometric field: a grid of stars of sampled spectra whose colours the response predicts, their red and
/// blue multiplied by the casts.
///
/// @param redCast what the red channel is multiplied by, beyond the star's colour.
/// @param blueCast what the blue channel is multiplied by.
/// @return the field.
[[nodiscard]] StarField makeSpectrophotometricField(double redCast, double blueCast);

/// The gains a correct spectrophotometric calibration of a field with these casts finds: the casts and the white
/// reference's colour undone, the largest one.
[[nodiscard]] std::array<double, 3u> expectedSpectrophotometricGains(double redCast, double blueCast);

/// Writes the SPCC spectrum catalog of a field's stars into a directory, as the managed download installs a local full
/// catalog: the manifest, the chunk, the four sidecars and the marker of a finished install.
///
/// @param directory the directory, which is created.
/// @param field the spectrophotometric field.
/// @return whether it was written.
[[nodiscard]] bool writeSpectrumCatalog(const QString& directory, const StarField& field);

/// Writes the response repository the spectrophotometric field's colours are integrated through: three bandpass
/// components, a flat white reference the profile is anchored on, a warm one beside it, and one colour-sensor profile
/// answering to the test camera with no filter named.
///
/// @param directory the directory, which is created.
/// @param reviewed whether the profile's rights review is approved.
/// @return whether it was written.
[[nodiscard]] bool writeResponseRepository(const QString& directory, bool reviewed = true);

/// What a field's image states about itself.
struct FieldMetadataOptions
{
    /// Whether the cards state the field's solution.
    bool solved = true;

    /// The camera the acquisition record names; empty for none.
    std::string camera = kSpccCameraModel;

    /// The filter the acquisition record names; empty for none.
    std::string filter;

    /// The processing state the cards record; nothing for no state cards.
    std::optional<an::ProcessingStateRecord> state = an::ProcessingStateRecord{
        .linearity = an::RecordedLinearity::Linear, .channelBasis = an::RecordedChannelBasis::LinearRgb};
};

/// The metadata a field's image carries.
[[nodiscard]] an::ImageMetadata fieldMetadata(const StarField& field, const FieldMetadataOptions& options = {});

/// A field as a new asset of a session in the representation `SampleT` - integers as fractions of full scale - and
/// carrying metadata; fails the case when it cannot be built.
template <typename SampleT>
[[nodiscard]] std::optional<an::consumer::ImageAsset> fieldAsset(const an::consumer::EngineSession& session,
                                                                 const StarField& field, an::ImageMetadata metadata);

/// A view over a field's image in a representation, its history begun.
template <typename SampleT>
[[nodiscard]] ViewHandle fieldView(const an::consumer::EngineSession& session, const StarField& field,
                                   const an::ImageMetadata& metadata, const std::string& name = "target",
                                   const QString& sourcePath = QString())
{
    std::optional<an::consumer::ImageAsset> asset = fieldAsset<SampleT>(session, field, metadata);
    if (!asset.has_value())
    {
        return ViewHandle();
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset),
        an::ui::DocumentNames{.displayName = QString::fromStdString(name), .sourcePath = sourcePath});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return ViewHandle();
    }
    return an::tests::processes::makeTargetView(name, std::move(*document));
}

/// Writes a SER recording of a field's frames at `path` - each the field, a little brighter than the one before - as a
/// capture program writes one: samples only, no plate solution; fails the case when it cannot.
void writeFieldRecording(const an::consumer::EngineSession& session, const QString& path, const StarField& field,
                         std::uint32_t frameCount);

/// A view over the recording at `path`, showing its first frame as a still image carrying `metadata`.
[[nodiscard]] ViewHandle fieldSequenceView(const an::consumer::EngineSession& session, const QString& path,
                                           const StarField& field, const an::ImageMetadata& metadata,
                                           std::uint32_t frameCount);

/// A view over a field's green channel as a grey Float32 image carrying `metadata` - the field's solution - so only
/// its channels keep it from being calibrated.
[[nodiscard]] ViewHandle greyFieldView(const an::consumer::EngineSession& session, const StarField& field,
                                       const an::ImageMetadata& metadata, const std::string& name);

/// Writes what a document holds now to a FITS file at `path` as a save writes it - Float32, its cards carried - and
/// reads the file back as an open reads it; the metadata the file gave back, or nothing when either step failed, which
/// fails the case.
[[nodiscard]] std::optional<an::ImageMetadata> writtenAndReadBack(const an::consumer::EngineSession& session,
                                                                  const an::ui::ImageDocument& document,
                                                                  const QString& path);

/// Takes a view out of the context's views and deletes it, as closing it does, so a window opened after it never lists
/// it.
void dropView(ProcessTestContext& context, ViewHandle& view);

/// What a document holds now, as planar doubles, red then green then blue, in the unit interval each representation is
/// read in; empty when it cannot be read.
[[nodiscard]] std::vector<double> documentValues(const an::ui::ImageDocument& document);

/// The metadata a document holds now; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::ImageMetadata> documentMetadata(const an::ui::ImageDocument& document);

/// The value a list of cards states for a FITS keyword, or nothing.
[[nodiscard]] std::optional<std::string> cardValue(const an::CodecMetadata& codec, std::string_view keyword);

/// The median of one channel of planar values outside every star, the field's sky.
[[nodiscard]] double skyMedian(const StarField& field, const std::vector<double>& values, std::uint32_t channel);

/// A window of a process, closed and deleted with the case once its runs have landed.
template <typename WindowT> struct WindowDeleter
{
    void operator()(WindowT* window) const;
};
template <typename WindowT> using WindowHandle = std::unique_ptr<WindowT, WindowDeleter<WindowT>>;

/// The directories a case installs its catalogs and repository in, and the settings that name them.
class InstalledCatalogs
{
public:
    /// Writes nothing; the case writes what it needs and states it.
    explicit InstalledCatalogs(ProcessTestContext& context);

    [[nodiscard]] QString gaiaDirectory() const;
    [[nodiscard]] QString spectrumDirectory() const;
    [[nodiscard]] QString responseDirectory() const;

    /// Names the Gaia catalog in the settings the photometric window reads.
    void useGaiaCatalog();

    /// Names the spectrum catalog and the response repository in the settings the spectrophotometric window reads.
    void useSpectrumCatalogAndRepository();

private:
    ProcessTestContext& m_context;
    QTemporaryDir m_root;
};

/// Opens the photometric window over a target as the entry point opens it, without the delete-on-close.
[[nodiscard]] WindowHandle<PhotometricColorCalibrationProcess> openPhotometric(ImageViewControl& target);

/// Opens the spectrophotometric window over a target, without the delete-on-close, and waits for its readiness.
[[nodiscard]] WindowHandle<SpectrophotometricColorCalibrationProcess> openSpectrophotometric(ImageViewControl& target);

/// Turns the event loop until a photometric window's run has landed and the view has settled.
[[nodiscard]] bool settlePhotometric(const PhotometricColorCalibrationProcess& window, const ImageViewControl& target);

/// Turns the event loop until a spectrophotometric window's readiness decision has landed.
[[nodiscard]] bool settleReadiness(const SpectrophotometricColorCalibrationProcess& window);

/// Runs the photometric window's Apply and waits for it to land.
[[nodiscard]] bool applyPhotometric(PhotometricColorCalibrationProcess& window, const ImageViewControl& target);

/// Runs the spectrophotometric window's Apply and waits for it and its readiness to land.
[[nodiscard]] bool applySpectrophotometric(SpectrophotometricColorCalibrationProcess& window,
                                           const ImageViewControl& target);

/// The sources an integration's registration recorded for a group, which a case writes into the registration database
/// the hand-off reads.
struct RecordedHandoff
{
    QString metricsDatabasePath;
    QString registrationGroupKey;
    int settingsVersion = 3;
    std::vector<an::MatchedSourceReference> references;
};

/// Writes `recorded`'s registration database through the processes library's own writer, as an integration's
/// registration records it: the group's settings version, and the sources it matched recorded for one of its frames.
/// Fails the case when the database cannot be written.
void writeHandoffDatabase(const RecordedHandoff& recorded);

/// Writes a workflow archive in a directory whose one integration group names an output file and a registration group;
/// the registration database beside it is the case's to write.
void writeWorkflowArchive(const QString& directory, const QString& outputFilename, const QString& registrationGroupKey);

} // namespace an::tests::processes::photometric
