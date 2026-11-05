// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the instrument was doing when the data was recorded.
///
/// These are the facts a frame arrives with and that nothing downstream may invent: how long the
/// shutter was open, which filter was in the path, which camera and telescope produced it, when.
/// They are source facts and they are immutable, which is the point of separating them from
/// everything else in the metadata split: kept in the same mutable bag as the cards about to be
/// written, a source fact can become an output decision without anyone deciding.
///
/// **A fact that was not stated is absent, not zero.** Every numeric field is an optional and every
/// name is empty until something sets it, because an exposure of zero seconds and an exposure nobody
/// recorded are different things, and an engine that conflates them will one day divide by the
/// first. That is why the fields are typed rather than a map of strings: a card that says
/// `EXPTIME = ` is a card, and `std::optional<double>` is an answer.
///
/// **Nothing here affects a computation without being asked for.** An algorithm that genuinely
/// depends on an acquisition fact - a dark scaled by exposure ratio - reads it and declares that it
/// did, in its own parameters. Nothing branches on this object implicitly, and no metadata
/// transition may change it: a derived product with different acquisition facts is a new image built
/// from scratch rather than an old one with its history edited.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>

#include <cstdint>
#include <optional>
#include <string>

namespace an
{

/// A moment in time, as a count of nanoseconds since 1970-01-01T00:00:00Z.
///
/// Deliberately not a `std::chrono` type. The published surface of this engine carries no standard
/// header it does not need, and `<chrono>` is one of the expensive ones - it would land in every
/// translation unit that touches an image, to express a number this type expresses in eight bytes.
/// A caller that wants calendar arithmetic converts at its own boundary, where it is already paying
/// for the header.
///
/// Nanoseconds because a sub-second exposure start is a real thing in this domain and a
/// millisecond count would quietly lose it. Signed because instants before 1970 exist in archival
/// data and a count that wrapped would be worse than one that went negative.
struct UtcInstant
{
    /// Nanoseconds since the Unix epoch, negative before it.
    std::int64_t nanosecondsSinceEpoch;

    /// Whether two instants are the same moment.
    ///
    /// @param left one instant.
    /// @param right the other.
    /// @return true when the counts are equal.
    [[nodiscard]] friend constexpr bool operator==(const UtcInstant& left, const UtcInstant& right) noexcept = default;
};

/// How a sensor's pixels were combined on readout.
///
/// Carried because it changes what a pixel is: a two-by-two binned frame has pixels of four times
/// the area, and a calibration frame taken at a different binning does not apply to it.
struct PixelBinning
{
    /// How many sensor columns one pixel covers. At least one.
    std::uint32_t horizontal;

    /// How many sensor rows one pixel covers. At least one.
    std::uint32_t vertical;

    /// Whether two binnings are the same.
    ///
    /// @param left one binning.
    /// @param right the other.
    /// @return true when both factors are equal.
    [[nodiscard]] friend constexpr bool operator==(const PixelBinning& left,
                                                   const PixelBinning& right) noexcept = default;
};

/// What a caller states about how a frame was acquired, before any of it has been checked.
///
/// A plain aggregate whose every member has a default, so a caller names the facts it has and
/// leaves out the ones it does not. That is the opposite of the descriptor's rule, and deliberately
/// so: a descriptor with a field missing describes no image, while an acquisition record with a
/// field missing describes a frame whose file did not say.
struct AcquisitionMetadataRequest
{
    /// The camera or sensor, as the source named it.
    std::string instrumentName{};

    /// The telescope or lens, as the source named it.
    std::string telescopeName{};

    /// Whoever recorded it, as the source named them.
    std::string observerName{};

    /// What was being observed, as the source named it.
    std::string objectName{};

    /// The filter in the light path, as the source named it.
    std::string filterName{};

    /// How long the shutter was open, in seconds. Must be positive and finite when stated.
    std::optional<double> exposureSeconds{};

    /// The sensor temperature in degrees Celsius. Must be finite and above absolute zero when
    /// stated.
    std::optional<double> sensorTemperatureCelsius{};

    /// The conversion from recorded code values to electrons. Must be positive and finite when
    /// stated.
    std::optional<double> gainElectronsPerAdu{};

    /// How the sensor's pixels were combined on readout. Both factors must be at least one when
    /// stated.
    std::optional<PixelBinning> binning{};

    /// When the exposure began, in UTC.
    std::optional<UtcInstant> exposureStart{};

    /// Camera calibration facts, absent for sources that do not supply them. Must pass
    /// `validateRawCameraMetadata` when stated.
    std::optional<RawCameraMetadata> rawCamera{};
};

/// What the instrument was doing when the data was recorded.
///
/// There is no way to build one whose stated facts contradict themselves: the constructor is
/// private and `create` is the only way in. A default-constructed one states nothing at all, which
/// is the honest description of an image the engine generated rather than recorded.
///
/// Immutable. An acquisition record is corrected by building a new one, and never by a metadata
/// transition - see `Image/ImageMetadata.h` for why that door is closed rather than guarded.
class AcquisitionMetadata
{
public:
    /// A record that states nothing.
    ///
    /// What a synthetic image has, and what a frame whose header carried none of these fields has.
    AcquisitionMetadata() = default;

    /// A record of these facts, when they are consistent.
    ///
    /// @param request the facts to record. Fields left out are simply absent.
    /// @return the record, or `ErrorCode::InvalidArgument` naming the first field whose stated value
    ///         is one no instrument produced. A stated `rawCamera` record is checked last, and its
    ///         refusal is `validateRawCameraMetadata`'s own `ErrorCode::InvalidArgument`, naming
    ///         the rejected camera field.
    [[nodiscard]] static AFTERNIGHTENGINE_API Result<AcquisitionMetadata>
    create(const AcquisitionMetadataRequest& request);

    /// The camera or sensor.
    ///
    /// @return the name, or an empty string when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& instrumentName() const noexcept;

    /// The telescope or lens.
    ///
    /// @return the name, or an empty string when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& telescopeName() const noexcept;

    /// Whoever recorded the frame.
    ///
    /// @return the name, or an empty string when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& observerName() const noexcept;

    /// What was being observed.
    ///
    /// @return the name, or an empty string when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& objectName() const noexcept;

    /// The filter in the light path.
    ///
    /// @return the name, or an empty string when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::string& filterName() const noexcept;

    /// How long the shutter was open.
    ///
    /// @return the exposure in seconds, or an empty optional when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> exposureSeconds() const noexcept;

    /// The sensor temperature.
    ///
    /// @return degrees Celsius, or an empty optional when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> sensorTemperatureCelsius() const noexcept;

    /// The conversion from recorded code values to electrons.
    ///
    /// @return electrons per code value, or an empty optional when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<double> gainElectronsPerAdu() const noexcept;

    /// How the sensor's pixels were combined on readout.
    ///
    /// @return the binning factors, or an empty optional when the source did not state them.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<PixelBinning> binning() const noexcept;

    /// When the exposure began.
    ///
    /// @return the instant, or an empty optional when the source did not state one.
    [[nodiscard]] AFTERNIGHTENGINE_API std::optional<UtcInstant> exposureStart() const noexcept;

    /// Camera calibration facts, as the source reported them before any decode processing.
    ///
    /// @return the record, or an empty optional when the source did not supply one. Borrowed from
    ///         this object and valid for as long as it is.
    [[nodiscard]] AFTERNIGHTENGINE_API const std::optional<RawCameraMetadata>& rawCamera() const noexcept;

    /// The request this record was built from.
    ///
    /// There is no setter, for the reason the descriptors have none: a correction is stated in full
    /// and rebuilt, so a value that breaks a rule is refused where it is made.
    ///
    /// @return every field, in a form `create` accepts.
    [[nodiscard]] AFTERNIGHTENGINE_API AcquisitionMetadataRequest toRequest() const;

    /// Whether two records state the same facts.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every field is equal, an absent field matching only another absent one.
    [[nodiscard]] friend bool operator==(const AcquisitionMetadata& left, const AcquisitionMetadata& right) = default;

private:
    /// Builds from a request whose values have already been checked.
    ///
    /// @param request the validated facts.
    explicit AcquisitionMetadata(const AcquisitionMetadataRequest& request);

    /// The camera or sensor.
    std::string m_instrumentName{};

    /// The telescope or lens.
    std::string m_telescopeName{};

    /// Whoever recorded the frame.
    std::string m_observerName{};

    /// What was being observed.
    std::string m_objectName{};

    /// The filter in the light path.
    std::string m_filterName{};

    /// How long the shutter was open, in seconds.
    std::optional<double> m_exposureSeconds{};

    /// The sensor temperature in degrees Celsius.
    std::optional<double> m_sensorTemperatureCelsius{};

    /// Electrons per recorded code value.
    std::optional<double> m_gainElectronsPerAdu{};

    /// How the sensor's pixels were combined on readout.
    std::optional<PixelBinning> m_binning{};

    /// When the exposure began.
    std::optional<UtcInstant> m_exposureStart{};

    /// Owned camera calibration facts.
    std::optional<RawCameraMetadata> m_rawCamera{};
};

} // namespace an
