// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Image/AcquisitionMetadata.h>

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Core/Status.h>

#include <limits>
#include <optional>
#include <string>

// The whole file is one validation and a set of accessors, and the validation is the part with a
// decision in it.
//
// A field that was not stated is never checked, because there is nothing to check: an absent
// exposure is not a wrong exposure. A field that *was* stated is checked against what an instrument
// can produce rather than against what the arithmetic can hold, which is why absolute zero appears
// here - a sensor at -400 degrees Celsius is a misread header, and admitting it would let a
// temperature-matched calibration frame be chosen by a number nobody could have measured.
//
// The order is the order the fields are declared, so a request with two problems reports the same
// one on every build.

namespace an
{
namespace
{

/// The coldest temperature there is, in degrees Celsius.
///
/// A stated sensor temperature at or below it is a field that was misread rather than measured.
constexpr double kAbsoluteZeroCelsius = -273.15;

/// Whether a stated value is a number an instrument could have produced.
///
/// @param value the number to test.
/// @return true when it is positive and finite.
[[nodiscard]] bool isPositiveFinite(double value) noexcept
{
    return value > 0.0 && value < std::numeric_limits<double>::infinity();
}

/// The refusal for a stated fact no instrument produced.
///
/// @param message what is wrong, for a person.
/// @param parameter which field, spelled as the caller wrote it.
/// @return an `ErrorCode::InvalidArgument` failure naming the field.
[[nodiscard]] Status refuseField(const char* message, const char* parameter)
{
    return Status::failure(ErrorCode::InvalidArgument,
                           StatusDetail{.message = message,
                                        .operation = "AcquisitionMetadata::create",
                                        .domain = ErrorDomain::Image,
                                        .context = {StatusContextField::ofText(ContextKey::Parameter, parameter)}});
}

} // namespace

AcquisitionMetadata::AcquisitionMetadata(const AcquisitionMetadataRequest& request)
    : m_instrumentName(request.instrumentName), m_telescopeName(request.telescopeName),
      m_observerName(request.observerName), m_objectName(request.objectName), m_filterName(request.filterName),
      m_exposureSeconds(request.exposureSeconds), m_sensorTemperatureCelsius(request.sensorTemperatureCelsius),
      m_gainElectronsPerAdu(request.gainElectronsPerAdu), m_binning(request.binning),
      m_exposureStart(request.exposureStart), m_rawCamera(request.rawCamera)
{
}

Result<AcquisitionMetadata> AcquisitionMetadata::create(const AcquisitionMetadataRequest& request)
{
    if (request.exposureSeconds.has_value() && !isPositiveFinite(*request.exposureSeconds))
    {
        return refuseField("an exposure is a positive finite number of seconds", "exposureSeconds");
    }
    if (request.sensorTemperatureCelsius.has_value() &&
        !(*request.sensorTemperatureCelsius > kAbsoluteZeroCelsius &&
          *request.sensorTemperatureCelsius < std::numeric_limits<double>::infinity()))
    {
        return refuseField("a sensor temperature is a finite number of degrees Celsius above absolute zero",
                           "sensorTemperatureCelsius");
    }
    if (request.gainElectronsPerAdu.has_value() && !isPositiveFinite(*request.gainElectronsPerAdu))
    {
        return refuseField("a gain is a positive finite number of electrons per code value", "gainElectronsPerAdu");
    }
    if (request.binning.has_value() && (request.binning->horizontal == 0u || request.binning->vertical == 0u))
    {
        return refuseField("a binning factor of zero covers no sensor pixels", "binning");
    }
    if (request.rawCamera)
        if (const auto checked = validateRawCameraMetadata(*request.rawCamera); checked.failed())
            return checked;
    return AcquisitionMetadata{request};
}

const std::string& AcquisitionMetadata::instrumentName() const noexcept
{
    return m_instrumentName;
}

const std::string& AcquisitionMetadata::telescopeName() const noexcept
{
    return m_telescopeName;
}

const std::string& AcquisitionMetadata::observerName() const noexcept
{
    return m_observerName;
}

const std::string& AcquisitionMetadata::objectName() const noexcept
{
    return m_objectName;
}

const std::string& AcquisitionMetadata::filterName() const noexcept
{
    return m_filterName;
}

std::optional<double> AcquisitionMetadata::exposureSeconds() const noexcept
{
    return m_exposureSeconds;
}

std::optional<double> AcquisitionMetadata::sensorTemperatureCelsius() const noexcept
{
    return m_sensorTemperatureCelsius;
}

std::optional<double> AcquisitionMetadata::gainElectronsPerAdu() const noexcept
{
    return m_gainElectronsPerAdu;
}

std::optional<PixelBinning> AcquisitionMetadata::binning() const noexcept
{
    return m_binning;
}

std::optional<UtcInstant> AcquisitionMetadata::exposureStart() const noexcept
{
    return m_exposureStart;
}

const std::optional<RawCameraMetadata>& AcquisitionMetadata::rawCamera() const noexcept
{
    return m_rawCamera;
}

AcquisitionMetadataRequest AcquisitionMetadata::toRequest() const
{
    return AcquisitionMetadataRequest{.instrumentName = m_instrumentName,
                                      .telescopeName = m_telescopeName,
                                      .observerName = m_observerName,
                                      .objectName = m_objectName,
                                      .filterName = m_filterName,
                                      .exposureSeconds = m_exposureSeconds,
                                      .sensorTemperatureCelsius = m_sensorTemperatureCelsius,
                                      .gainElectronsPerAdu = m_gainElectronsPerAdu,
                                      .binning = m_binning,
                                      .exposureStart = m_exposureStart,
                                      .rawCamera = m_rawCamera};
}

} // namespace an
