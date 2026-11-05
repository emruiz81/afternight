// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The product's RAW load options, as neutral cases.
//
// The product's RAW options are one record - a white-balance mode, `none`, `daylight`, `camera_as_shot`, `auto_wb` or
// `custom` with three gains, an auto-rotate flag and an auto-brightness flag - which the settings page and the
// workflow write and a RAW open reads. A case states one open of a synthetic DNG under one such record, and its
// record is the reference result. The engine fixture executable opens the file as the product does - a full-colour
// frame through a registry whose camera RAW adapter carries the mapped balance, a mosaic decoded to its codes and
// balanced by the filter-array step - turns it by its recorded orientation when the record asks, and compares by the
// record's classification.
//
// An input is a description of the frame's stored codes and the camera facts a DNG states beside them;
// `writeRawPolicyDng` writes it as an uncompressed little-endian DNG from the specification's tags alone, so the
// engine never reads a file it wrote. The records live in `raw-policy.expectations`.

#include "FixtureExpectation.h"
#include "FixtureImage.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// A synthetic camera frame: its codes, and what its DNG states beside them.
struct RawPolicyInput
{
    /// The stored codes, black included: one UInt16 sensor-code channel under a named filter array, or three.
    FixtureImage image;
    /// The black level every code sits on, and the white level a code saturates at.
    std::uint32_t blackLevel = 0u;
    std::uint32_t whiteLevel = 0u;
    /// The as-shot neutral as three rationals, numerator then denominator; the camera's own multipliers are their
    /// reciprocals.
    std::array<std::uint32_t, 6> asShotNeutral{1u, 1u, 1u, 1u, 1u, 1u};
    /// The orientation tag, one through eight; six is a clockwise quarter turn.
    std::uint16_t orientation = 1u;
};

/// The product's RAW load record, in the spelling its settings persist.
struct RawLoadPolicy
{
    /// `none`, `daylight`, `camera_as_shot`, `auto_wb` or `custom`.
    std::string whiteBalance = "daylight";
    /// The red, green and blue gains `custom` applies.
    std::array<double, 3> customGains{1.0, 1.0, 1.0};
    bool autoRotate = false;
};

/// How a record relates to a correct open of its file, where the record's layout differs from it.
enum class RawRecordLayout : std::uint8_t
{
    /// The record holds the frame as a decoder hands it over.
    AsDecoded,
    /// The record holds a full-colour frame's interleaved samples written across its three planes, each held below
    /// 65535: a correct open written that way.
    Interleaved,
    /// As `Interleaved`, from a correct open left unturned: the record holds a full-colour frame unturned, whatever
    /// its options asked.
    InterleavedUnturned
};

/// One open of a synthetic DNG under one record.
struct RawPolicyCase
{
    /// The operation key, `raw-open.<white balance>[.rotated]`; the id is `<operation>/<input>`.
    std::string operation;
    /// The input's name.
    std::string input;
    RawLoadPolicy policy;

    /// What the product does, in words; the record states it.
    std::string text;
    /// The product source whose code the case follows, as `<library path>:<line>`.
    std::string source;
    /// How the record lays out what was decoded.
    RawRecordLayout recordLayout = RawRecordLayout::AsDecoded;
    /// How the engine's answer is held to the record.
    ExpectationClass classification = ExpectationClass::Exact;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    /// Why the answers differ, for an accepted case.
    std::string acceptedReason;
    /// For an accepted case, the largest difference the comparison allows, in codes.
    double acceptedBound = 0.0;
    /// For an accepted case, how many samples of the record the engine's answer differs in.
    std::uint64_t differing = 0u;
    /// Why a bound is what it is.
    std::string note;

    [[nodiscard]] std::string id() const
    {
        return operation + "/" + input;
    }
};

/// The record file every case's record lives in.
inline constexpr const char* kRawPolicyExpectationFile = "raw-policy.expectations";

/// The white-balance spellings the product persists, in its menu order.
inline constexpr std::string_view kRawWhiteBalanceModes[] = {"none", "daylight", "camera_as_shot", "auto_wb", "custom"};

/// The inputs the cases name, in a stable order; names are unique and none is a catalogue fixture's.
[[nodiscard]] const std::vector<RawPolicyInput>& rawPolicyInputs();

/// The input of this name, or null.
[[nodiscard]] const RawPolicyInput* rawPolicyInput(std::string_view name);

/// Every case, in a stable order; ids are unique.
[[nodiscard]] const std::vector<RawPolicyCase>& rawPolicyCases();

/// The case of this id, or null.
[[nodiscard]] const RawPolicyCase* rawPolicyCase(std::string_view id);

/// Writes an input's codes as an uncompressed little-endian DNG: a colour filter array image of one plane under the
/// description's filter array, or a linear raw image of three interleaved channels, with the input's black and white
/// levels, as-shot neutral and orientation, and an identity colour matrix. False, with the reason in `error`, when the
/// samples are not the input's or the file cannot be written.
[[nodiscard]] bool writeRawPolicyDng(const RawPolicyInput& input, const FixtureSamples& samples,
                                     const std::filesystem::path& path, std::string& error);

} // namespace an::tests::fixtures
