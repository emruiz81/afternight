// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibrations' target facts, working image, published result and recorded state.

#include "stdafx.hpp"

#include "processes/PhotometricCalibrationSupport.h"

#include "app/PlateSolveTypes.h"
#include "processes/ProcessMetadataUtils.h"
#include "processes/StretchProcessSupport.h"

#include <AfterNightEngine/Algorithms/Photometry/PhotometricColorCalibration.h>
#include <AfterNightEngine/Algorithms/Statistics/BasicStatistics.h>
#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageSemantics.h>
#include <AfterNightEngine/Image/Provenance.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace an::ui::processes
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)});
}

template <typename ImageT> using SampleOf = typename std::remove_cvref_t<ImageT>::Sample;

/// The working copy of a typed image, in `WorkT`.
template <typename WorkT, typename SampleT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
workingCopyAs(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::Image<SampleT>& image)
{
    const std::optional<an::ImageDescriptor> descriptor = image.descriptor();
    const auto view = image.view();
    if (!descriptor.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the image holds nothing");
    }
    if (!isCalibrationRgb(*descriptor))
    {
        return refusal(an::ErrorCode::Unsupported, "a colour calibration reads three-channel RGB images");
    }
    // The samples are fractions of full scale in a linear RGB basis: the window decided from what the image declares
    // that they may be calibrated, which a decoded file's description rarely states itself.
    an::ImageDescriptorRequest request = descriptor->toRequest();
    request.sampleType = an::Image<WorkT>::kSampleType;
    request.numericDomain = an::NumericDomain::NormalizedLinear;
    request.colorSpace = an::ColorSpace::LinearRgb;
    const std::optional<an::ImageDescriptor> working = an::ImageDescriptor::create(request);
    if (!working.has_value())
    {
        return refusal(an::ErrorCode::IncompatibleArguments, "the image has no description as linear RGB fractions");
    }
    an::Result<an::Image<WorkT>> copy = an::Image<WorkT>::createDestination(
        engine, *working, an::storageArrangementOf(view->layout()), an::StorageInitialization::Uninitialized, image);
    if (!copy.hasValue())
    {
        return copy.status();
    }
    {
        const std::optional<an::ImageWriteAccess<WorkT>> access = copy->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
        }
        const an::Status converted =
            readAsFractions(execution, an::Image<SampleT>::kSampleType, view->data(), view->layout(),
                            an::Image<WorkT>::kSampleType, access->view().data(), access->view().layout());
        if (converted.failed())
        {
            return converted;
        }
    }
    return an::consumer::AnyImage(std::move(*copy));
}

/// The result in the source's representation `ResultT`, made from a working image.
template <typename ResultT>
[[nodiscard]] an::Result<an::consumer::AnyImage>
resultFrom(const an::EngineContext& engine, const an::ExecutionContext& execution, const an::ImageDescriptor& described,
           an::ImageMetadata metadata, const an::consumer::AnyImage& working)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using WorkT = SampleOf<decltype(typed)>;
            const auto read = typed.view();
            if (!read.has_value())
            {
                return refusal(an::ErrorCode::NotInitialized, "the calibrated image holds nothing");
            }
            if (read->width() != described.extent().width() || read->height() != described.extent().height() ||
                read->channelCount() != described.extent().channelCount())
            {
                return refusal(an::ErrorCode::IncompatibleArguments,
                               "the calibrated samples are not of the source's size");
            }
            an::Result<an::Image<ResultT>> result =
                an::Image<ResultT>::create(engine, described, an::storageArrangementOf(read->layout()),
                                           an::StorageInitialization::Uninitialized, std::move(metadata));
            if (!result.hasValue())
            {
                return result.status();
            }
            {
                const std::optional<an::ImageWriteAccess<ResultT>> access = result->write();
                if (!access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the result holds nothing");
                }
                const an::Status converted =
                    writeFromFractions(engine, execution, described, an::Image<WorkT>::kSampleType, read->data(),
                                       read->layout(), access->view().data(), access->view().layout());
                if (converted.failed())
                {
                    return converted;
                }
            }
            return an::consumer::AnyImage(std::move(*result));
        },
        working);
}

[[nodiscard]] an::RecordedLinearity linearityOfTransfer(const an::TransferFunction transfer) noexcept
{
    switch (transfer)
    {
    case an::TransferFunction::Linear:
        return an::RecordedLinearity::Linear;
    case an::TransferFunction::Srgb:
    case an::TransferFunction::Rec709:
    case an::TransferFunction::PureGamma:
        return an::RecordedLinearity::Nonlinear;
    case an::TransferFunction::Unspecified:
    default:
        return an::RecordedLinearity::Unknown;
    }
}

} // namespace

bool isCalibrationRgb(const an::ImageDescriptor& descriptor) noexcept
{
    return descriptor.extent().channelCount() == 3u && descriptor.channelSchema() == an::ChannelSchema::Rgb;
}

QString calibrationFilterName(const an::ImageMetadata& metadata)
{
    return QString::fromStdString(metadata.acquisition().filterName()).trimmed();
}

QString calibrationCameraModel(const an::ImageMetadata& metadata)
{
    return QString::fromStdString(metadata.acquisition().instrumentName()).trimmed();
}

std::optional<an::WcsSolution> calibrationSolutionOf(const an::ImageMetadata& metadata,
                                                     const an::ImageDescriptor& descriptor)
{
    an::Result<an::WcsSolution> solution = an::ui::readImageWcsSolution(
        metadata.codec().cards(), descriptor.extent().width(), descriptor.extent().height());
    if (!solution.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*solution);
}

CalibrationTargetProblem photometricTargetProblem(const std::optional<an::ImageDescriptor>& descriptor,
                                                  const an::ImageMetadata& metadata)
{
    if (!descriptor.has_value())
    {
        return CalibrationTargetProblem::NoImage;
    }
    if (!isCalibrationRgb(*descriptor))
    {
        return CalibrationTargetProblem::NotRgb;
    }
    if (an::isNarrowbandFilterName(metadata.acquisition().filterName()))
    {
        return CalibrationTargetProblem::Narrowband;
    }
    if (!calibrationSolutionOf(metadata, *descriptor).has_value())
    {
        return CalibrationTargetProblem::NotPlateSolved;
    }
    if (descriptor->numericDomain() == an::NumericDomain::EncodedNonlinear ||
        process_utils::declaredLinearity(metadata) == an::RecordedLinearity::Nonlinear)
    {
        return CalibrationTargetProblem::DeclaredNonlinear;
    }
    return CalibrationTargetProblem::None;
}

an::ProcessingStateRecord declaredCalibrationState(const an::ImageDescriptor& descriptor,
                                                   const an::ImageMetadata& metadata)
{
    an::ProcessingStateRecord record = an::readProcessingStateCards(metadata.codec());
    const an::RecordedLinearity transfer = linearityOfTransfer(metadata.semantics().transferFunction());
    if (transfer != an::RecordedLinearity::Unknown)
    {
        // A transfer function the meaning states is what the samples are, whatever an older card says.
        record.linearity = transfer;
    }
    else if (record.linearity == an::RecordedLinearity::Unknown &&
             descriptor.numericDomain() == an::NumericDomain::EncodedNonlinear)
    {
        record.linearity = an::RecordedLinearity::Nonlinear;
    }
    if (record.channelBasis == an::RecordedChannelBasis::Unknown && isCalibrationRgb(descriptor) &&
        descriptor.colorSpace() == an::ColorSpace::LinearRgb)
    {
        record.channelBasis = an::RecordedChannelBasis::LinearRgb;
    }
    if (record.colorCalibration == an::RecordedColorCalibration::None &&
        metadata.semantics().processingState().colorCalibrated)
    {
        record.colorCalibration = an::RecordedColorCalibration::Unknown;
    }
    return record;
}

an::Result<an::consumer::AnyImage> makeCalibrationWorkingImage(const an::EngineContext& engine,
                                                               const an::ExecutionContext& execution,
                                                               const an::consumer::AnyImage& image)
{
    return std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using SampleT = SampleOf<decltype(typed)>;
            // A 32-bit integer holds more digits than Float32 does, as a Float64 image does.
            if constexpr (std::is_same_v<SampleT, double> || std::is_same_v<SampleT, std::uint32_t> ||
                          std::is_same_v<SampleT, std::int32_t>)
            {
                return workingCopyAs<double>(engine, execution, typed);
            }
            else
            {
                return workingCopyAs<float>(engine, execution, typed);
            }
        },
        image);
}

an::Status calibrateWorkingImage(an::consumer::AnyImage& working, const an::ValidityMask* mask,
                                 const CalibrationBody& body)
{
    return std::visit(
        [&](auto& typed) -> an::Status {
            using WorkT = SampleOf<decltype(typed)>;
            if constexpr (!std::is_floating_point_v<WorkT>)
            {
                return refusal(an::ErrorCode::IncompatibleArguments, "a working image holds floating samples");
            }
            else
            {
                const std::optional<an::ImageDescriptor> descriptor = typed.descriptor();
                std::optional<an::ImageWriteAccess<WorkT>> access = typed.write();
                if (!descriptor.has_value() || !access.has_value())
                {
                    return refusal(an::ErrorCode::NotInitialized, "the working image holds nothing");
                }
                std::optional<an::ValidityMaskView<const std::uint8_t>> maskView;
                const an::KernelValidityView validity = stretchValidity(typed.metadata(), mask, maskView);
                const an::ImageLayout layout = access->view().layout();
                void* samples = access->view().data();
                const an::Result<an::StarFrameView> frame =
                    an::StarFrameView::create(samples, layout.totalBytes(), layout, *descriptor, validity);
                if (!frame.hasValue())
                {
                    return frame.status();
                }
                return body(*frame, samples, layout.totalBytes());
            }
        },
        working);
}

std::optional<an::StatisticsRegion> calibrationRegion(const QRect& rectangle)
{
    if (rectangle.isEmpty() || rectangle.x() < 0 || rectangle.y() < 0)
    {
        return std::nullopt;
    }
    return an::StatisticsRegion{.originX = static_cast<std::uint32_t>(rectangle.x()),
                                .originY = static_cast<std::uint32_t>(rectangle.y()),
                                .width = static_cast<std::uint32_t>(rectangle.width()),
                                .height = static_cast<std::uint32_t>(rectangle.height())};
}

an::Result<an::ImageMetadata> calibratedMetadata(const an::ImageMetadata& source, const an::MetadataTransition& change,
                                                 const an::ColorCalibrationRecord& record)
{
    an::Result<an::ImageMetadata> changed = an::applyMetadataTransition(source, change);
    if (!changed.hasValue())
    {
        return changed.status();
    }
    // The colour-calibration cards: the calibration, its identities and numbers, the state of the image it was run on -
    // which the card bridge reads from the cards where the record states none - and the state that records it.
    an::Result<an::CodecMetadata> codec = an::withColorCalibrationCards(changed->codec(), record);
    if (!codec.hasValue())
    {
        return codec.status();
    }
    return an::ImageMetadata::create(changed->acquisition(), changed->semantics(), changed->astrometry(),
                                     changed->provenance(), std::move(*codec), changed->outputIntent());
}

an::Result<an::consumer::ImageAsset> calibratedAsset(const an::consumer::EngineSession& session,
                                                     const an::EngineContext& engine,
                                                     const an::ExecutionContext& execution, const ProcessSource& source,
                                                     const an::consumer::AnyImage& working, an::ImageMetadata metadata)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the source holds nothing");
    }
    an::Result<an::consumer::AnyImage> result = std::visit(
        [&](const auto& typed) -> an::Result<an::consumer::AnyImage> {
            using ResultT = SampleOf<decltype(typed)>;
            return resultFrom<ResultT>(engine, execution, *described, std::move(metadata), working);
        },
        source.image());
    if (!result.hasValue())
    {
        return result.status();
    }
    std::optional<an::ValidityMask> mask;
    if (const an::ValidityMask* validity = source.validity(); validity != nullptr)
    {
        an::Result<an::ValidityMask> copied = validity->clone();
        if (!copied.hasValue())
        {
            return copied.status();
        }
        mask = std::move(*copied);
    }
    return an::consumer::ImageAsset::adopt(session, std::move(*result), std::move(mask));
}

QString attestedRevisionText(const an::consumer::AssetRevision& revision)
{
    return QStringLiteral("asset:%1;pixels:%2;meaning:%3;validity:%4")
        .arg(revision.assetIdentity)
        .arg(revision.pixelGeneration)
        .arg(revision.semanticGeneration)
        .arg(revision.validityGeneration);
}

an::Result<an::ImageMetadata> attestedLinearMetadata(const an::ImageMetadata& metadata, const QString& sourcePath,
                                                     const an::consumer::AssetRevision& revision,
                                                     const QString& timestampUtc)
{
    const an::StateAttestation attestation{
        .sourcePath = sourcePath.toStdString(),
        .sourceHash = attestedRevisionText(revision).toStdString(),
        .attestedBy = "AfterNight user",
        .timestampUtc = timestampUtc.toStdString(),
        .toolVersion = "AfterNight",
        .warnings = "User confirmed imported image is linear RGB for SPCC.",
    };
    const an::ProcessingStateRecord state =
        an::attestedState(an::readProcessingStateCards(metadata.codec()), an::RecordedLinearity::Linear,
                          an::RecordedChannelBasis::LinearRgb, attestation);
    an::Result<an::CodecMetadata> codec = an::withProcessingStateCards(metadata.codec(), state);
    if (!codec.hasValue())
    {
        return codec.status();
    }
    an::ImageSemanticsRequest meaning = metadata.semantics().toRequest();
    meaning.transferFunction = an::TransferFunction::Linear;
    meaning.gammaExponent.reset();
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    if (!semantics.hasValue())
    {
        return semantics.status();
    }
    const an::Result<an::ProvenanceEntry> entry =
        an::ProvenanceEntry::create("attestLinearState", "a person attested the samples are linear RGB");
    if (!entry.hasValue())
    {
        return entry.status();
    }
    an::Result<an::Provenance> provenance = metadata.provenance().appended(*entry);
    if (!provenance.hasValue())
    {
        return provenance.status();
    }
    return an::ImageMetadata::create(metadata.acquisition(), std::move(*semantics), metadata.astrometry(),
                                     std::move(*provenance), std::move(*codec), metadata.outputIntent());
}

} // namespace an::ui::processes
