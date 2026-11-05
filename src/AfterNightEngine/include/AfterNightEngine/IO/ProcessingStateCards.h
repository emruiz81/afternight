// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The processing-state cards a product carries, read into typed records and written back.
///
/// A product's own header states what is known about its samples as a family of `AN` cards: whether the
/// samples are linear, which channel basis they are in, whether and how they were colour calibrated, and
/// who said so - a loader, a process, or a person attesting to an imported file. A reader uses those cards
/// to decide what may be done next, which is how a frame that was already colour calibrated refuses to be
/// calibrated again.
///
/// This engine states what samples are in their descriptor and what was done to them in their semantics
/// and lineage, and nothing infers either from a card. These records are the bridge a caller crosses
/// deliberately: a typed reading of every card of the family, the cards a typed record writes, and the
/// rules that apply to the record, so that a file reads the same whoever wrote it.
///
/// **A long identity travels twice.** A card holds no more than one card's worth of value, so a
/// calibration's catalogue manifest or response profile cannot survive a save as a card alone. The whole
/// calibration record is therefore also written as a JSON document, hex-encoded across numbered cards with
/// its length and checksum, and that document is preferred when reading. Both halves are kept: the cards
/// for a reader that knows only cards, and the document for the whole text.

#pragma once

#include <AfterNightEngine/Core/Api.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/CodecMetadata.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/RawCameraMetadata.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace an
{

/// Whether a product's samples were recorded as standing in proportion to the light, as `ANLIN` states it.
enum class RecordedLinearity : std::uint8_t
{
    /// Nothing was recorded, or the card said something this engine does not recognise.
    Unknown,

    /// The samples were recorded as linear. Written as `linear`.
    Linear,

    /// The samples were recorded as having been through a nonlinear stretch. Written as `nonlinear`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Nonlinear
};

/// Whether a value is one of the recorded linearities this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param linearity the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRecordedLinearity(RecordedLinearity linearity) noexcept
{
    return static_cast<std::uint8_t>(linearity) <= static_cast<std::uint8_t>(RecordedLinearity::Nonlinear);
}

/// Which channel basis a product's samples were recorded in, as `ANBASIS` states it.
enum class RecordedChannelBasis : std::uint8_t
{
    /// Nothing was recorded, or the card said something this engine does not recognise.
    Unknown,

    /// One plane of sensor codes under a colour filter array. Written as `raw-cfa`.
    RawCfa,

    /// Linear red, green and blue in a stated colour space. Written as `linear-rgb`.
    LinearRgb,

    /// Linear red, green and blue in the camera's own primaries. Written as `camera-rgb`.
    CameraRgb,

    /// Red, green and blue prepared for display. Written as `display-rgb`.
    DisplayRgb,

    /// One plane of intensity. Written as `monochrome`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Monochrome
};

/// Whether a value is one of the recorded channel bases this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param basis the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRecordedChannelBasis(RecordedChannelBasis basis) noexcept
{
    return static_cast<std::uint8_t>(basis) <= static_cast<std::uint8_t>(RecordedChannelBasis::Monochrome);
}

/// Whether and how a product was colour calibrated, as `ANCCAL` states it.
enum class RecordedColorCalibration : std::uint8_t
{
    /// Not colour calibrated, which is also what an absent or empty card reads as. Written as `none`.
    None,

    /// Calibrated against catalogue photometry. Written as `pcc`.
    Photometric,

    /// Calibrated against catalogue spectra through a response profile. Written as `spcc`.
    Spectrophotometric,

    /// Calibrated by factors a person chose. Written as `manual`.
    Manual,

    /// The card said something this engine does not recognise. Written as `unknown`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Unknown
};

/// Whether a value is one of the recorded colour calibrations this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param calibration the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRecordedColorCalibration(RecordedColorCalibration calibration) noexcept
{
    return static_cast<std::uint8_t>(calibration) <= static_cast<std::uint8_t>(RecordedColorCalibration::Unknown);
}

/// Who stated a product's recorded state, as `ANASRC` states it.
enum class StateAssignmentSource : std::uint8_t
{
    /// Nobody, which is also what an absent or unrecognised card reads as. Written as `none`.
    None,

    /// The loader that decoded the file. Written as `loader`.
    Loader,

    /// The process that produced the samples. Written as `process`.
    Process,

    /// A person, attesting to a file this engine did not produce. Written as `user-attestation`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UserAttestation
};

/// Whether a value is one of the assignment sources this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param source the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isStateAssignmentSource(StateAssignmentSource source) noexcept
{
    return static_cast<std::uint8_t>(source) <= static_cast<std::uint8_t>(StateAssignmentSource::UserAttestation);
}

/// Which white balance a RAW loader applied, as `ANWBMOD` states it.
enum class RecordedWhiteBalance : std::uint8_t
{
    /// No channel gains were applied. Written as `none`.
    None,

    /// The decoder's daylight gains, which is also what an unrecognised card reads as. Written as
    /// `daylight`.
    Daylight,

    /// The gains the camera recorded when the frame was taken. Written as `camera-as-shot`.
    CameraAsShot,

    /// Gains measured from the frame itself. Written as `auto`.
    Automatic,

    /// Gains a person chose. Written as `custom`.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    Custom
};

/// Whether a value is one of the recorded white balances this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param balance the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isRecordedWhiteBalance(RecordedWhiteBalance balance) noexcept
{
    return static_cast<std::uint8_t>(balance) <= static_cast<std::uint8_t>(RecordedWhiteBalance::Custom);
}

/// The spelling a recorded linearity is written with.
///
/// Thread-safe and allocation-free.
///
/// @param linearity the value to spell. A value outside the enumeration is spelled `unknown`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stateCardText(RecordedLinearity linearity) noexcept;

/// The spelling a recorded channel basis is written with.
///
/// Thread-safe and allocation-free.
///
/// @param basis the value to spell. A value outside the enumeration is spelled `unknown`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stateCardText(RecordedChannelBasis basis) noexcept;

/// The spelling a recorded colour calibration is written with.
///
/// Thread-safe and allocation-free.
///
/// @param calibration the value to spell. A value outside the enumeration is spelled `unknown`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stateCardText(RecordedColorCalibration calibration) noexcept;

/// The spelling an assignment source is written with.
///
/// Thread-safe and allocation-free.
///
/// @param source the value to spell. A value outside the enumeration is spelled `none`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stateCardText(StateAssignmentSource source) noexcept;

/// The spelling a recorded white balance is written with.
///
/// Thread-safe and allocation-free.
///
/// @param balance the value to spell. A value outside the enumeration is spelled `daylight`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char* stateCardText(RecordedWhiteBalance balance) noexcept;

/// Who attested to a recorded state, and on what evidence, when a person rather than a loader or a
/// process stated it.
struct StateAttestation
{
    /// The file the person attested to, from `ANASFILE`.
    std::string sourcePath{};

    /// A digest of that file, from `ANASHSH`.
    std::string sourceHash{};

    /// Who attested, from `ANASBY`.
    std::string attestedBy{};

    /// When they attested, as the attesting tool wrote it, from `ANASUTC`.
    std::string timestampUtc{};

    /// Which tool recorded the attestation, from `ANASTOOL`.
    std::string toolVersion{};

    /// What the attesting tool warned about, from `ANASWARN`.
    std::string warnings{};

    /// Whether two attestations say the same thing.
    ///
    /// @param left one attestation.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const StateAttestation& left, const StateAttestation& right) = default;
};

/// The white balance a RAW loader recorded applying.
struct RecordedRawWhiteBalance
{
    /// Which gains were chosen, from `ANWBMOD`.
    RecordedWhiteBalance mode = RecordedWhiteBalance::Daylight;

    /// Whether they were applied to the samples, from `ANWBAPP`.
    bool applied = false;

    /// Whether two records say the same thing.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when both fields are equal.
    [[nodiscard]] friend bool operator==(const RecordedRawWhiteBalance& left,
                                         const RecordedRawWhiteBalance& right) = default;
};

/// Everything the processing-state cards record about a product, typed.
///
/// A plain aggregate whose defaults are what a product with none of the cards reads as.
struct ProcessingStateRecord
{
    /// Whether the samples are linear, from `ANLIN`.
    RecordedLinearity linearity = RecordedLinearity::Unknown;

    /// Which channel basis the samples are in, from `ANBASIS`.
    RecordedChannelBasis channelBasis = RecordedChannelBasis::Unknown;

    /// Whether and how the product was colour calibrated, from `ANCCAL`.
    RecordedColorCalibration colorCalibration = RecordedColorCalibration::None;

    /// Who stated this record, from `ANASRC`.
    StateAssignmentSource assignmentSource = StateAssignmentSource::None;

    /// The white balance a RAW loader applied, present exactly when `ANWBMOD` states one.
    std::optional<RecordedRawWhiteBalance> rawWhiteBalance{};

    /// The red, first green, blue and second green gains that white balance used, present exactly when
    /// `ANWBMUL` states them.
    std::optional<std::array<double, 4u>> rawWhiteBalanceMultipliers{};

    /// Where the camera's colour matrix came from, from `ANCMATP`.
    std::string cameraMatrixProvenance{};

    /// The operation or loader that stated this record, from `ANOP`.
    std::string operation{};

    /// Who attested to this record, when a person did.
    StateAttestation attestation{};

    /// Whether two records say the same thing.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const ProcessingStateRecord& left,
                                         const ProcessingStateRecord& right) = default;
};

/// How a product was colour calibrated, and against what.
///
/// A plain aggregate whose defaults are what a product with none of the cards reads as.
struct ColorCalibrationRecord
{
    /// Which calibration was applied, from `ANCCAL`.
    RecordedColorCalibration calibration = RecordedColorCalibration::None;

    /// The red, green and blue gains the calibration applied, from `ANCCGN`.
    std::array<double, 3u> gains{1.0, 1.0, 1.0};

    /// The red, green and blue offsets the calibration applied, from `ANCCOF`.
    std::array<double, 3u> offsets{0.0, 0.0, 0.0};

    /// The response profile the calibration used, from `ANRSPID`.
    std::string responseProfileId{};

    /// That profile's version, from `ANRSPVR`.
    std::string responseProfileVersion{};

    /// The white reference the calibration used, from `ANWREF`.
    std::string whiteReferenceId{};

    /// That reference's version, from `ANWREFV`.
    std::string whiteReferenceVersion{};

    /// The catalogue family the calibration read, from `ANCATF`.
    std::string catalogFamilyId{};

    /// The catalogue manifest the calibration read, from `ANCATM`.
    std::string catalogManifestId{};

    /// The access profile the catalogue was read under, from `ANCATP`.
    std::string catalogAccessProfileId{};

    /// The version of the algorithm that calibrated, from `ANALGOV`.
    std::string algorithmVersion{};

    /// Whether the input was linear when it was calibrated, from `ANINLIN`.
    RecordedLinearity inputLinearity = RecordedLinearity::Unknown;

    /// Which channel basis the input was in when it was calibrated, from `ANINBAS`.
    RecordedChannelBasis inputChannelBasis = RecordedChannelBasis::Unknown;

    /// Whether two records say the same thing.
    ///
    /// @param left one record.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const ColorCalibrationRecord& left,
                                         const ColorCalibrationRecord& right) = default;
};

/// Why a calibration document could not be read out of its cards.
enum class CalibrationPayloadProblem : std::uint8_t
{
    /// The document was read and verified.
    None,

    /// None of the document's cards is present.
    Absent,

    /// `ANCJVER` is missing or names a version other than 1.
    UnsupportedVersion,

    /// `ANCJCNT` is not a whole number from 1 to 10000.
    InvalidChunkCount,

    /// `ANCJLEN` is not a whole number.
    InvalidLength,

    /// `ANCJCHK` is missing or empty.
    MissingChecksum,

    /// A numbered chunk card is missing or empty; `CalibrationPayloadStatus::missingChunk` says which.
    MissingChunk,

    /// The chunks do not join into an even run of hexadecimal digits.
    InvalidHex,

    /// The decoded document's length is not the one `ANCJLEN` states.
    LengthMismatch,

    /// The decoded document's checksum is not the one `ANCJCHK` states.
    ChecksumMismatch,

    /// The decoded text is not a JSON object whose fields have the types the schema gives them.
    InvalidDocument,

    /// The document names a schema other than the calibration record's.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    UnsupportedSchema
};

/// Whether a value is one of the payload problems this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param problem the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isCalibrationPayloadProblem(CalibrationPayloadProblem problem) noexcept
{
    return static_cast<std::uint8_t>(problem) <=
           static_cast<std::uint8_t>(CalibrationPayloadProblem::UnsupportedSchema);
}

/// The problem's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param problem the problem to name. A value outside the enumeration is named
///        `"UnknownCalibrationPayloadProblem"`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
calibrationPayloadProblemName(CalibrationPayloadProblem problem) noexcept;

/// What reading the calibration document out of its cards found.
struct CalibrationPayloadStatus
{
    /// Whether any of the document's cards is present.
    bool present = false;

    /// Whether the document was read and verified.
    bool valid = false;

    /// Whether the calibration record was read from the document rather than from the cards.
    bool usedPayload = false;

    /// Whether a document was present but could not be used, so the record was read from the cards.
    bool fellBackToCards = false;

    /// Why the document could not be read, or `CalibrationPayloadProblem::None`.
    CalibrationPayloadProblem problem = CalibrationPayloadProblem::Absent;

    /// The first missing chunk's number when the problem is `CalibrationPayloadProblem::MissingChunk`;
    /// zero otherwise.
    std::uint32_t missingChunk = 0u;

    /// Whether two statuses report the same thing.
    ///
    /// @param left one status.
    /// @param right the other.
    /// @return true when every field is equal.
    [[nodiscard]] friend bool operator==(const CalibrationPayloadStatus& left,
                                         const CalibrationPayloadStatus& right) = default;
};

/// The calibration document's verified text, and what reading it found.
struct CalibrationPayloadReading
{
    /// The document exactly as it was written, when it was read and verified; empty otherwise.
    std::string document{};

    /// What reading it found.
    CalibrationPayloadStatus status{};
};

/// A product's calibration record, and where it was read from.
struct ColorCalibrationReading
{
    /// The record, from the document when it was usable and from the cards otherwise.
    ColorCalibrationRecord record{};

    /// What reading the document found.
    CalibrationPayloadStatus status{};
};

/// Why a product may not be calibrated against catalogue spectra.
enum class SpectrophotometricInputProblem : std::uint8_t
{
    /// Nothing recorded stands in the way.
    None,

    /// The product's linearity is not recorded.
    UnknownLinearity,

    /// The product is recorded as nonlinear.
    Nonlinear,

    /// The product's channel basis is neither linear RGB nor camera RGB.
    IncompatibleChannelBasis,

    /// A person attested to the state without naming the file, its digest and themselves.
    IncompleteAttestation,

    /// The product is recorded as colour calibrated already.
    ///
    /// Deliberately the last enumerator, so a membership test needs no table.
    AlreadyColorCalibrated
};

/// Whether a value is one of the input problems this build knows.
///
/// Thread-safe, allocation-free, and usable in a constant expression.
///
/// @param problem the value to test.
/// @return true when the value is one of the enumerators.
[[nodiscard]] constexpr bool isSpectrophotometricInputProblem(SpectrophotometricInputProblem problem) noexcept
{
    return static_cast<std::uint8_t>(problem) <=
           static_cast<std::uint8_t>(SpectrophotometricInputProblem::AlreadyColorCalibrated);
}

/// The problem's own name, for a log line, a diagnostic field or a report.
///
/// Thread-safe and allocation-free.
///
/// @param problem the problem to name. A value outside the enumeration is named
///        `"UnknownSpectrophotometricInputProblem"`.
/// @return a null-terminated string literal compiled into this library, which is never freed.
[[nodiscard]] AFTERNIGHTENGINE_API const char*
spectrophotometricInputProblemName(SpectrophotometricInputProblem problem) noexcept;

/// Reads the processing-state record a card set states.
///
/// Each field is read from the first `Fits` card of its keyword, which is also where an XISF file's FITS
/// keywords are preserved. A value is read without its surrounding white space and one pair of single
/// quotes, a doubled quote inside them reading as one. The spellings each enumeration documents are
/// matched without regard to ASCII case; any other text reads as `Unknown`, as `None` for the assignment
/// source and an empty colour calibration, and as `Daylight` for a white balance. `rawWhiteBalance` is
/// present when `ANWBMOD` is not empty, and applied when `ANWBAPP` reads `t`, `true`, `1` or `yes`.
/// `rawWhiteBalanceMultipliers` is present when `ANWBMUL` is not empty, and takes its numbers in order
/// from the comma-separated fields that begin with one, a gain with no such field left at 1.
///
/// Thread-safe; allocates the returned strings.
///
/// @param codec the preserved cards.
/// @return the record, whose defaults stand for every card that is absent.
[[nodiscard]] AFTERNIGHTENGINE_API ProcessingStateRecord readProcessingStateCards(const CodecMetadata& codec);

/// States a processing-state record as cards.
///
/// Writes `ANSTVER` as 1 and then `ANLIN`, `ANBASIS`, `ANASRC` and `ANCCAL`; `ANWBMOD` and `ANWBAPP` when
/// the record has a raw white balance and `ANWBMUL` when it has multipliers; and each text field's card
/// when the field is not empty. The card of an absent or empty field is removed. Enumerations are written
/// in the spellings they document and text as quoted FITS strings, `ANWBAPP` is written as the logical `T`
/// or `F`, and the multipliers as four numbers with six decimals, separated by commas. Writing a keyword
/// replaces the value of its first `Fits` card in place, keeping its comment, and removes the rest; a
/// keyword the cards lack is appended with no comment. Cards of other keywords and namespaces are kept.
///
/// Thread-safe with respect to both arguments, which are only read.
///
/// @param codec the cards to state the record on.
/// @param record what to state.
/// @return the cards with the record stated; `ErrorCode::InvalidArgument` for an enumeration outside its
///         values or a multiplier that is not finite; or `ErrorCode::LimitExceeded` carrying the keyword,
///         the size and the bound when a value is longer than one card holds or the cards would be more
///         than one image preserves.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> withProcessingStateCards(const CodecMetadata& codec,
                                                                                  const ProcessingStateRecord& record);

/// Removes every card of the processing-state and colour-calibration family.
///
/// Every `Fits` card whose keyword is one this header documents, and every numbered chunk of the
/// calibration document, is removed; every other card is kept in its order.
///
/// Thread-safe with respect to the argument, which is only read.
///
/// @param codec the cards to clear.
/// @return the remaining cards.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> withoutProcessingStateCards(const CodecMetadata& codec);

/// The channel basis linear samples with this description are in.
///
/// A mosaic is `RawCfa`; one intensity plane is `Monochrome`; red, green and blue are `CameraRgb` when that
/// is what was recorded and `LinearRgb` otherwise; anything else is `Unknown`.
///
/// Thread-safe, allocation-free.
///
/// @param descriptor what the samples are.
/// @param recorded the basis recorded for them before.
/// @return the basis.
[[nodiscard]] AFTERNIGHTENGINE_API RecordedChannelBasis linearChannelBasisOf(const ImageDescriptor& descriptor,
                                                                             RecordedChannelBasis recorded) noexcept;

/// The channel basis samples with this description are in after a nonlinear stretch.
///
/// Recorded linear, camera or display RGB becomes `DisplayRgb` and recorded monochrome stays `Monochrome`;
/// otherwise red, green and blue are `DisplayRgb`, one intensity plane is `Monochrome`, and anything else
/// keeps what was recorded.
///
/// Thread-safe, allocation-free.
///
/// @param descriptor what the samples are.
/// @param recorded the basis recorded for them before.
/// @return the basis.
[[nodiscard]] AFTERNIGHTENGINE_API RecordedChannelBasis nonlinearChannelBasisOf(const ImageDescriptor& descriptor,
                                                                                RecordedChannelBasis recorded) noexcept;

/// The record of a product a process left linear.
///
/// The linearity becomes `Linear`, the basis the one given, the source `Process` and the operation the one
/// named; every other field is kept.
///
/// Thread-safe; allocates the returned strings.
///
/// @param record the record before the process.
/// @param basis the channel basis of the product, typically `linearChannelBasisOf` its description.
/// @param operation the process, as the record names it.
/// @return the product's record.
[[nodiscard]] AFTERNIGHTENGINE_API ProcessingStateRecord linearState(const ProcessingStateRecord& record,
                                                                     RecordedChannelBasis basis,
                                                                     std::string_view operation);

/// The record of a product a process stretched.
///
/// The linearity becomes `Nonlinear`, the basis `nonlinearChannelBasisOf` the description and the recorded
/// basis, the source `Process` and the operation the one named; every other field is kept.
///
/// Thread-safe; allocates the returned strings.
///
/// @param record the record before the process.
/// @param descriptor what the product's samples are.
/// @param operation the process, as the record names it.
/// @return the product's record.
[[nodiscard]] AFTERNIGHTENGINE_API ProcessingStateRecord nonlinearState(const ProcessingStateRecord& record,
                                                                        const ImageDescriptor& descriptor,
                                                                        std::string_view operation);

/// The record of a product whose state a person attested to.
///
/// The linearity and basis become the ones given, the source `UserAttestation`, the operation empty and
/// the attestation the one given; every other field is kept.
///
/// Thread-safe; allocates the returned strings.
///
/// @param record the record before the attestation.
/// @param linearity what the person attested the linearity is.
/// @param basis what the person attested the channel basis is.
/// @param attestation who attested, and on what evidence.
/// @return the product's record.
[[nodiscard]] AFTERNIGHTENGINE_API ProcessingStateRecord attestedState(const ProcessingStateRecord& record,
                                                                       RecordedLinearity linearity,
                                                                       RecordedChannelBasis basis,
                                                                       const StateAttestation& attestation);

/// The record of a frame as its loader decoded it.
///
/// The linearity becomes `Linear`, the source `Loader` and the operation the format's name. A mosaic is
/// `RawCfa`, red, green and blue are `LinearRgb`, one intensity plane is `Monochrome` and anything else is
/// `Unknown`. When a RAW decoder's camera record is given, the raw white balance becomes the mode given,
/// applied unless that mode is `None`; the multipliers become the decoder's daylight gains when its red,
/// green and blue daylight gains are all positive, and otherwise its as-shot gains under the same rule;
/// and the camera matrix provenance becomes `libraw:rgb_cam` when any entry of the camera-to-RGB matrix is
/// not zero. Every other field is kept.
///
/// Thread-safe; allocates the returned strings.
///
/// @param record the record before decoding.
/// @param descriptor what the decoded samples are.
/// @param formatName the format, as the record names it: `FITS`, `TIFF`, `PNG`, `JPEG`, `RAW` or `XISF`.
/// @param camera the RAW decoder's camera record, or null for any other format.
/// @param applied which white balance the caller applied to a RAW frame's samples.
/// @return the frame's record.
[[nodiscard]] AFTERNIGHTENGINE_API ProcessingStateRecord
loaderState(const ProcessingStateRecord& record, const ImageDescriptor& descriptor, std::string_view formatName,
            const RawCameraMetadata* camera = nullptr, RecordedWhiteBalance applied = RecordedWhiteBalance::None);

/// Reads and verifies the calibration document a card set carries.
///
/// The document is present when any of `ANCJVER`, `ANCJCNT`, `ANCJLEN`, `ANCJCHK` or a chunk card `ANCJ`
/// followed by four digits is. It is valid when `ANCJVER` is 1; `ANCJCNT` is a whole number from 1 to
/// 10000 and every chunk from `ANCJ0000` up to it is present and not empty; the chunks join into
/// hexadecimal digits that decode to exactly `ANCJLEN` bytes; the bytes' 64-bit FNV-1a checksum, written as
/// sixteen lower-case hexadecimal digits, is `ANCJCHK` read without regard to case; and the bytes are a
/// JSON object naming the schema `afternight.color_calibration_provenance.v1` whose fields have the types
/// the schema gives them. The checks run in that order and the first to fail is the problem reported.
///
/// Thread-safe; allocates the returned document.
///
/// @param codec the preserved cards.
/// @return the verified document text, and what reading it found. Never a failure: a document that cannot
///         be read is reported, not refused.
[[nodiscard]] AFTERNIGHTENGINE_API CalibrationPayloadReading readColorCalibrationPayload(const CodecMetadata& codec);

/// Reads the colour-calibration record a card set states.
///
/// From the calibration document when `readColorCalibrationPayload` verifies it, and from the cards
/// otherwise, each read as `readProcessingStateCards` reads a card: `ANCCAL`; `ANCCGN` and `ANCCOF` as up to
/// three comma-separated numbers each, taken as the white balance multipliers are; the eight identities as
/// text; and `ANINLIN` and `ANINBAS`. A document field that is absent leaves the record's default, and a
/// gain or offset that is not a number leaves that entry's default.
///
/// Thread-safe; allocates the returned strings.
///
/// @param codec the preserved cards.
/// @return the record, and what reading the document found.
[[nodiscard]] AFTERNIGHTENGINE_API ColorCalibrationReading readColorCalibrationCards(const CodecMetadata& codec);

/// States a colour calibration as cards, and records it in the processing state.
///
/// The record's input linearity and basis, where they are `Unknown`, are taken from the processing state
/// the cards record. The processing state is written as `withProcessingStateCards` writes it, with its
/// colour calibration set to the record's. Then `ANCCAL`, `ANCCGN` and `ANCCOF`, each identity's card when
/// the identity is not empty, and `ANINLIN` and `ANINBAS` are written, removing the card of an empty
/// identity; gains and offsets are written as three numbers with six decimals separated by commas. Last,
/// the calibration document is written: every chunk card and the four cards that describe the document are
/// removed, and `ANCJVER`, `ANCJCNT`, `ANCJLEN`, `ANCJCHK` and the chunks are appended, each chunk carrying
/// thirty-two hexadecimal digits of the document as a quoted string.
///
/// Thread-safe with respect to both arguments, which are only read.
///
/// @param codec the cards to state the calibration on.
/// @param record what to state.
/// @return the cards with the calibration stated; `ErrorCode::InvalidArgument` for an enumeration outside
///         its values, a gain or offset that is not finite, or text that is not UTF-8; or
///         `ErrorCode::LimitExceeded` carrying the keyword, the size and the bound when a value is longer
///         than one card holds, the document needs more than 10000 chunks, or the cards would be more
///         than one image preserves.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata>
withColorCalibrationCards(const CodecMetadata& codec, const ColorCalibrationRecord& record);

/// States a calibration document, given as its JSON text, as cards.
///
/// The document is read as `readColorCalibrationPayload` reads a decoded one. Its record is written into
/// the processing state and as cards the way `withColorCalibrationCards` writes them, except that the
/// input linearity and basis are written as the document states them, and the document is written as the
/// text given rather than as one rebuilt from the record.
///
/// Thread-safe with respect to both arguments, which are only read.
///
/// @param codec the cards to state the document on.
/// @param document the document's JSON text.
/// @return the cards with the document stated; `ErrorCode::InvalidData` when the text is not a document of
///         the calibration schema; or `ErrorCode::LimitExceeded` carrying the keyword, the size and the
///         bound when a value is longer than one card holds, the document needs more than 10000 chunks, or
///         the cards would be more than one image preserves.
[[nodiscard]] AFTERNIGHTENGINE_API Result<CodecMetadata> withColorCalibrationPayload(const CodecMetadata& codec,
                                                                                     std::string_view document);

/// Why a product with this record may not be calibrated against catalogue spectra.
///
/// The checks run in the order the enumeration lists them, and the first to fail is the one reported: an
/// unknown or nonlinear linearity, a basis other than `LinearRgb` or `CameraRgb`, a person's attestation
/// missing its file, its digest or its author, and a colour calibration other than `None`.
///
/// Thread-safe, allocation-free.
///
/// @param record the product's processing state.
/// @return the first problem, or `SpectrophotometricInputProblem::None`.
[[nodiscard]] AFTERNIGHTENGINE_API SpectrophotometricInputProblem
spectrophotometricInputProblem(const ProcessingStateRecord& record) noexcept;

} // namespace an
