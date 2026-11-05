// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stars-plus-comet composite.

#include "stdafx.hpp"
#include "processes/ImageIntegration/workflow/stages/StackCometProducts.h"

#include "processes/ImageIntegration/core/CalibrationMasterSet.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationFrames.h"
#include "processes/ImageIntegration/workflow/stages/RegistrationOutputPlane.h"
#include "processes/ImageIntegration/workflow/stages/StackArtifactFingerprints.h"
#include "processes/ImageIntegration/workflow/stages/StackDrizzlePixmapIndex.h"
#include "processes/ImageIntegration/workflow/stages/StageParsingUtils.h"
#include "processes/core/RegistrationMetricsStateLoader.h"

#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Algorithms/Registration/CometTracking.h>
#include <AfterNightEngine/Algorithms/Stacking/CometComposite.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityMask.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <unordered_map>

namespace ImageIntegration::detail
{

namespace
{

[[nodiscard]] an::Status refusal(const an::ErrorCode code, const QString& message)
{
    return an::Status::failure(code, message.toUtf8().toStdString());
}

[[nodiscard]] an::Status aboutLight(an::Status cause, const FrameId& light, const char* what)
{
    if (cause.code() == an::ErrorCode::Cancelled || cause.code() == an::ErrorCode::DeadlineExceeded)
    {
        return cause;
    }
    const an::ErrorCode code = cause.code();
    std::string message =
        "'" + QFileInfo(light.canonicalPath()).fileName().toUtf8().toStdString() + "' " + what + " " + cause.message();
    return an::Status::failure(code, an::StatusDetail{.message = std::move(message)}, std::move(cause));
}

/// What one light contributes to the composite.
struct CompositeFrame
{
    FrameId light;
    QString starPath;
    QString cometPath;
    an::RegistrationTransform starTransform;
    std::optional<an::SipDistortion> starDistortion;
    an::RegistrationTransform cometTransform;
    std::optional<an::SipDistortion> cometDistortion;
    an::PixelCoordinate cometSourcePosition{};
    an::CometApertureMeasurement measurement;
    double radius = 0.0;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    an::CometApertureCircle starAperture{};
    an::CometApertureCircle cometAperture{};
};

[[nodiscard]] std::optional<double> positive(const double value)
{
    return std::isfinite(value) && value > 0.0 ? std::optional<double>(value) : std::nullopt;
}

/// A registered image read as fractions, with the pixels it does not reach - every plane an exact zero - and the
/// pixels holding a sample that is not a number - where a calibration's flat was zero - stated as no data in a shared
/// mask the split reads.
///
/// The split admits or refuses a whole pixel, so a pixel with such a sample in one plane is left out whole: restated as
/// a zero beside planes that hold numbers, the sample would be admitted and split into both layers as data.
struct CoveredFrame
{
    an::Image<float> image;
    an::ValidityMask mask;
};

[[nodiscard]] an::Result<CoveredFrame> readCovered(const an::consumer::OperationScope& scope,
                                                   const an::CodecRegistry* registry, const QString& path)
{
    an::Result<an::consumer::AnyImage> read = core::readWorkflowImage(scope, registry, path);
    if (!read.hasValue())
    {
        return read.status();
    }
    an::Result<an::Image<float>> image = core::workingFractions(scope, *read);
    if (!image.hasValue())
    {
        return image.status();
    }
    const std::optional<an::ImageDescriptor> described = image->descriptor();
    const auto view = std::as_const(*image).view();
    if (!described.has_value() || !view.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the frame holds nothing"));
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(described->extent().width(), described->extent().height(), 1u, scope.engine().limits());
    if (!extent.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, QStringLiteral("the frame's extent cannot hold a mask"));
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::create(scope.engine(), *extent);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        const std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
        if (!bits.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the frame's mask holds nothing"));
        }
        for (std::uint32_t y = 0u; y < view->height(); ++y)
        {
            for (std::uint32_t x = 0u; x < view->width(); ++x)
            {
                bool reached = false;
                bool finite = true;
                for (std::uint32_t plane = 0u; plane < view->planeCount() && finite; ++plane)
                {
                    const std::optional<std::span<const float>> row = view->row(plane, y);
                    const float sample = row.has_value() ? (*row)[x * view->samplesPerPixel()] : 0.0f;
                    finite = std::isfinite(sample);
                    reached = reached || sample != 0.0f;
                }
                (void)bits->setValid(0u, x, y, reached && finite);
            }
        }
    }
    return CoveredFrame{.image = std::move(*image), .mask = std::move(*mask)};
}

/// One layer of a registered image split at an aperture: the star layer when `cometLayer` is false.
[[nodiscard]] an::Result<an::Image<float>> splitLayer(const an::consumer::OperationScope& scope,
                                                      const an::CodecRegistry* registry, const QString& path,
                                                      const an::CometApertureCircle& aperture, const bool cometLayer)
{
    an::Result<CoveredFrame> frame = readCovered(scope, registry, path);
    if (!frame.hasValue())
    {
        return frame.status();
    }
    const std::optional<an::ImageDescriptor> described = frame->image.descriptor();
    const auto view = std::as_const(frame->image).view();
    const std::optional<an::ValidityMaskView<const std::uint8_t>> bits = std::as_const(frame->mask).view();
    const std::optional<an::ValidityDescriptor> explicitMask =
        an::ValidityDescriptor::create({.rule = an::ValidityRule::ExplicitMask});
    if (!described.has_value() || !view.has_value() || !bits.has_value() || !explicitMask.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the frame holds nothing"));
    }
    const an::Result<an::CometApertureSplitPlan> plan =
        an::planCometApertureSplit(an::CometApertureSplitRequest{.aperture = aperture});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    an::Result<an::Image<float>> layer =
        an::Image<float>::create(scope.engine(), *described, an::StorageArrangement::Planar,
                                 an::StorageInitialization::Zeroed, frame->image.metadata());
    if (!layer.hasValue())
    {
        return layer.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = layer->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the layer holds nothing"));
        }
        an::KernelValidityView validity;
        validity.descriptor = *explicitMask;
        validity.mask = &*bits;
        const an::CometCompositeSource source{
            .descriptor = &*described, .samples = view->data(), .layout = &view->layout(), .validity = validity};
        void* const destination = access->view().data();
        const an::ImageLayout& layout = access->view().layout();
        const an::Result<an::CometApertureSplitResult> split =
            cometLayer
                ? an::splitCometAperture(scope.execution(), *plan, source, nullptr, nullptr, destination, &layout)
                : an::splitCometAperture(scope.execution(), *plan, source, destination, &layout, nullptr, nullptr);
        if (!split.hasValue())
        {
            return split.status();
        }
    }
    return std::move(*layer);
}

/// Where a light's comet aperture lies on a plane its registration and the plane's origin place it on.
[[nodiscard]] an::Result<an::CometApertureCircle>
apertureOn(const an::ExecutionContext& execution, const CompositeFrame& frame,
           const an::RegistrationTransform& transform, const std::optional<an::SipDistortion>& distortion,
           const an::PixelCoordinate& origin, const std::uint32_t planeWidth, const std::uint32_t planeHeight)
{
    an::CometApertureProjectionRequest request;
    request.sourceCenter = frame.cometSourcePosition;
    request.sourceRadiusPixels = frame.radius;
    request.sourceWidth = frame.width;
    request.sourceHeight = frame.height;
    request.outputWidth = planeWidth;
    request.outputHeight = planeHeight;
    request.boundarySamples = 48u;
    request.sourceToReference = transform;
    request.sourceDistortion = distortion.value_or(an::SipDistortion{});
    request.postTranslation = origin;
    const an::Result<an::CometApertureProjection> projection = an::projectCometAperture(execution, request);
    if (!projection.hasValue())
    {
        return projection.status();
    }
    return an::cometApertureCircleOf(*projection, frame.radius, 1.0);
}

[[nodiscard]] QString starBaseDescriptorIdFor(const std::vector<an::ui::RegistrationMetricRecord>& products)
{
    QJsonArray array;
    for (const auto& product : products)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("frameId"), product.frameId.canonicalPath());
        entry.insert(QStringLiteral("outputPath"), product.outputPath);
        entry.insert(QStringLiteral("transformJson"), product.transformJson);
        entry.insert(QStringLiteral("updatedAt"), QString::number(product.updatedAt));
        array.append(entry);
    }
    return sha256Hex(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

[[nodiscard]] an::Result<an::Image<float>> imageLike(const an::EngineContext& engine, const an::Image<float>& model,
                                                     const std::uint32_t channels)
{
    const std::optional<an::ImageDescriptor> described = model.descriptor();
    if (!described.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the stack holds nothing"));
    }
    an::ImageDescriptorRequest request = described->toRequest();
    if (channels != 0u && channels != described->extent().channelCount())
    {
        const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
            described->extent().width(), described->extent().height(), channels, engine.limits());
        if (!extent.has_value())
        {
            return refusal(an::ErrorCode::InvalidArgument, QStringLiteral("the mask's extent cannot be stated"));
        }
        request.extent = *extent;
        request.channelSchema = an::ChannelSchema::Mono;
        request.colorSpace = an::ColorSpace::Unspecified;
    }
    const std::optional<an::ImageDescriptor> result = an::ImageDescriptor::create(request);
    if (!result.has_value())
    {
        return refusal(an::ErrorCode::InvalidArgument, QStringLiteral("the composite's image cannot be described"));
    }
    return an::Image<float>::create(engine, *result, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed,
                                    model.metadata());
}

} // namespace

QString compositeLayerPath(const QString& finalPath, const QString& kind)
{
    const QFileInfo info(finalPath);
    const QString suffix = info.suffix().isEmpty() ? QStringLiteral("fit") : info.suffix();
    return QDir(info.absolutePath()).filePath(QStringLiteral("%1.%2.%3").arg(info.completeBaseName(), kind, suffix));
}

QString starsPlusCometProductsFingerprint(const an::ui::StarsPlusCometExperimentalProductResolveResult& products)
{
    QJsonObject object;
    object.insert(QStringLiteral("cometProducts"), fingerprintCometProducts(products.cometProducts));
    object.insert(QStringLiteral("starBaseDescriptorId"), starBaseDescriptorIdFor(products.starBaseProducts));
    return fingerprintJson(object);
}

an::Result<StarsPlusCometCompositeOutput> buildStarsPlusCometComposite(const an::consumer::OperationScope& scope,
                                                                       const an::CodecRegistry* registry,
                                                                       const StarsPlusCometCompositeRequest& request,
                                                                       const CompositeLayerStacker& stack)
{
    if (request.products == nullptr || !request.products->available || request.frames.size() < 2u ||
        request.referenceIndex >= request.frames.size())
    {
        return refusal(an::ErrorCode::InvalidArgument,
                       QStringLiteral("The composite needs matching star base and comet products for two lights or "
                                      "more."));
    }
    const auto& products = *request.products;
    const auto& comet = products.cometProducts;
    if (!comet.outputPlane.has_value() || comet.outputPlane->canvasWidth <= 0 || comet.outputPlane->canvasHeight <= 0)
    {
        return refusal(an::ErrorCode::InvalidArgument, QStringLiteral("The comet products state no output plane."));
    }

    an::ui::RegistrationMetricsStateLoader loader(request.metricsPath, QStringLiteral("StarsPlusCometCompositeLoader"),
                                                  request.counters);
    QString error;
    if (!loader.open(&error))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("Failed to open registration metrics for composite stacking: %1").arg(error));
    }
    std::vector<an::ui::CometTrackingPointRecord> points;
    if (!loader.loadCometTrackingPoints(comet.cometRunId, points, &error))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("Failed to load persisted comet tracking points: %1").arg(error));
    }
    std::vector<an::ui::CometTransformRoleRecord> roles;
    if (!loader.loadCometTransformRoles(comet.cometRunId, comet.role, roles, &error))
    {
        return refusal(an::ErrorCode::ExternalFailure,
                       QStringLiteral("Failed to load comet transform roles for composite stacking: %1").arg(error));
    }

    std::unordered_map<QString, const an::ui::CometTrackingPointRecord*> pointByLight;
    for (const auto& point : points)
    {
        if (std::isfinite(point.sourcePosition.x()) && std::isfinite(point.sourcePosition.y()))
        {
            pointByLight[point.originalFrameId.identityKey()] = &point;
        }
    }
    std::unordered_map<QString, const an::ui::CometTransformRoleRecord*> roleByLight;
    for (const auto& role : roles)
    {
        if (role.succeeded)
        {
            roleByLight[role.frameId.identityKey()] = &role;
        }
    }
    std::unordered_map<QString, const an::ui::RegistrationMetricRecord*> starByLight;
    for (const auto& product : products.starBaseProducts)
    {
        starByLight[product.frameId.identityKey()] = &product;
    }
    std::unordered_map<QString, QString> cometOutputByLight;
    for (const auto& output : comet.registeredOutputs)
    {
        if (output.succeeded)
        {
            cometOutputByLight[output.frameId.identityKey()] = output.outputPath;
        }
    }

    std::vector<CompositeFrame> frames;
    frames.reserve(request.frames.size());
    std::vector<an::CometApertureMeasurement> measurements;
    std::vector<stage_utils::RegistrationOutputPlaneFrame> planeFrames;
    for (const FrameId& light : request.frames)
    {
        const QString key = light.identityKey();
        const auto star = starByLight.find(key);
        const auto role = roleByLight.find(key);
        const auto point = pointByLight.find(key);
        const auto cometOutput = cometOutputByLight.find(key);
        if (star == starByLight.end() || role == roleByLight.end() || point == pointByLight.end() ||
            cometOutput == cometOutputByLight.end())
        {
            return refusal(an::ErrorCode::InvalidArgument,
                           QStringLiteral("Composite stacking is missing a star base product, a comet transform, a "
                                          "comet-aligned image or the comet's aperture for '%1'.")
                               .arg(light.canonicalPath()));
        }
        CompositeFrame frame;
        frame.light = light;
        frame.starPath = star->second->outputPath;
        frame.cometPath = cometOutput->second;
        QString parseError;
        const std::optional<stage_utils::ParsedTransformRecord> starRecord =
            stage_utils::parseTransformRecord(star->second->transformJson, parseError);
        const std::optional<stage_utils::ParsedTransformRecord> cometRecord =
            stage_utils::parseTransformRecord(role->second->transformJson, parseError);
        if (!starRecord.has_value() || !cometRecord.has_value())
        {
            return refusal(
                an::ErrorCode::InvalidData,
                QStringLiteral("Failed to read the registrations of '%1': %2").arg(light.canonicalPath(), parseError));
        }
        frame.starTransform = starRecord->transform;
        frame.cometTransform = cometRecord->transform;
        const an::Result<stage_utils::RegistrationFrameFacts> facts =
            stage_utils::probeRegistrationFrame(scope, registry, light.canonicalPath());
        if (!facts.hasValue())
        {
            return aboutLight(facts.status(), light, "could not be probed:");
        }
        frame.width = facts->width;
        frame.height = facts->height;
        const auto resolve = [&facts](const stage_utils::SourceDistortionProvenance& provenance) {
            if (provenance.policy == stage_utils::SourceDistortionPolicy::ExplicitModel)
            {
                return provenance.model;
            }
            return provenance.policy == stage_utils::SourceDistortionPolicy::InheritMetadata ? facts->distortion
                                                                                             : std::nullopt;
        };
        frame.starDistortion = resolve(starRecord->provenance);
        frame.cometDistortion = resolve(cometRecord->provenance);
        if (!role->second->sourceDistortionJson.trimmed().isEmpty())
        {
            const QJsonObject json = QJsonDocument::fromJson(role->second->sourceDistortionJson.toUtf8()).object();
            an::SipDistortion distortion;
            if (!stage_utils::sipDistortionFromJson(json, distortion))
            {
                return refusal(
                    an::ErrorCode::InvalidData,
                    QStringLiteral("The comet source distortion of '%1' cannot be read.").arg(light.canonicalPath()));
            }
            frame.cometDistortion = distortion;
        }
        frame.cometSourcePosition =
            an::PixelCoordinate{point->second->sourcePosition.x(), point->second->sourcePosition.y()};
        frame.measurement =
            an::CometApertureMeasurement{.apertureRadius = positive(point->second->apertureRadiusPixels),
                                         .measuredDiameter = positive(point->second->measuredDiameterPixels),
                                         .measuredFullWidth = positive(point->second->measuredFwhmPixels)};
        measurements.push_back(frame.measurement);
        planeFrames.push_back({frame.starTransform, frame.width, frame.height, frame.starDistortion});
        frames.push_back(std::move(frame));
    }

    std::vector<double> radii(frames.size(), 0.0);
    const an::Result<std::uint32_t> resolved = an::resolveCometApertureRadii(measurements, radii);
    if (!resolved.hasValue())
    {
        return refusal(resolved.status().code(),
                       QStringLiteral("Persisted comet aperture data is missing for every light: %1")
                           .arg(QString::fromUtf8(resolved.status().message())));
    }
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        frames[index].radius = radii[index];
    }

    // The stars' plane: the canvas the star base was aligned onto, planned from its transforms as the alignment
    // planned it.
    const an::Result<stage_utils::PlannedRegistrationOutputPlane> starPlane =
        stage_utils::planRegistrationOutputPlane(planeFrames, request.referenceIndex, request.canvasMode);
    if (!starPlane.hasValue())
    {
        return starPlane.status();
    }
    const an::RegistrationCanvas& starCanvas = starPlane->canvas;
    const an::PixelCoordinate cometOrigin{comet.outputPlane->canvasOffsetX, comet.outputPlane->canvasOffsetY};
    const auto cometWidth = static_cast<std::uint32_t>(comet.outputPlane->canvasWidth);
    const auto cometHeight = static_cast<std::uint32_t>(comet.outputPlane->canvasHeight);
    QJsonArray maskSources;
    std::vector<an::CometApertureCircle> starApertures;
    for (CompositeFrame& frame : frames)
    {
        an::Result<an::CometApertureCircle> starAperture =
            apertureOn(scope.execution(), frame, frame.starTransform, frame.starDistortion, starCanvas.origin,
                       starCanvas.width, starCanvas.height);
        if (!starAperture.hasValue())
        {
            return aboutLight(starAperture.status(), frame.light, "has no comet aperture on the stars' plane:");
        }
        an::Result<an::CometApertureCircle> cometAperture =
            apertureOn(scope.execution(), frame, frame.cometTransform, frame.cometDistortion, cometOrigin, cometWidth,
                       cometHeight);
        if (!cometAperture.hasValue())
        {
            return aboutLight(cometAperture.status(), frame.light, "has no comet aperture on the comet's plane:");
        }
        frame.starAperture = *starAperture;
        frame.cometAperture = *cometAperture;
        starApertures.push_back(frame.starAperture);
        QJsonObject source;
        source.insert(QStringLiteral("sourceX"), frame.cometSourcePosition.x);
        source.insert(QStringLiteral("sourceY"), frame.cometSourcePosition.y);
        source.insert(QStringLiteral("outputX"), frame.starAperture.center.x);
        source.insert(QStringLiteral("outputY"), frame.starAperture.center.y);
        source.insert(QStringLiteral("radius"), frame.starAperture.radius);
        maskSources.append(source);
    }

    const auto nameOf = [&frames](const std::uint32_t index) {
        return index < frames.size() ? frames[index].light.canonicalPath() : QString();
    };
    stage_utils::WorkflowStackFrameSource starSource(
        [&](const std::uint32_t index) -> an::Result<an::Image<float>> {
            an::Result<an::Image<float>> layer =
                splitLayer(scope, registry, frames.at(index).starPath, frames.at(index).starAperture, false);
            return layer.hasValue() ? std::move(layer)
                                    : an::Result<an::Image<float>>(
                                          aboutLight(layer.status(), frames.at(index).light, "could not be split:"));
        },
        static_cast<std::uint32_t>(frames.size()), an::ValidityRule::ZeroIsNoData, nameOf, scope.engine().limits());
    an::Result<an::Image<float>> starStack = stack(starSource, false);
    if (!starStack.hasValue())
    {
        return starStack.status();
    }
    stage_utils::WorkflowStackFrameSource cometSource(
        [&](const std::uint32_t index) -> an::Result<an::Image<float>> {
            an::Result<an::Image<float>> layer =
                splitLayer(scope, registry, frames.at(index).cometPath, frames.at(index).cometAperture, true);
            return layer.hasValue() ? std::move(layer)
                                    : an::Result<an::Image<float>>(
                                          aboutLight(layer.status(), frames.at(index).light, "could not be split:"));
        },
        static_cast<std::uint32_t>(frames.size()), an::ValidityRule::ZeroIsNoData, nameOf, scope.engine().limits());
    an::Result<an::Image<float>> cometStack = stack(cometSource, true);
    if (!cometStack.hasValue())
    {
        return cometStack.status();
    }

    // The mask, over every light's aperture on the stars' plane.
    an::Result<an::Image<float>> mask = imageLike(scope.engine(), *starStack, 1u);
    if (!mask.hasValue())
    {
        return mask.status();
    }
    {
        std::optional<an::ImageWriteAccess<float>> access = mask->write();
        if (!access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, QStringLiteral("the mask holds nothing"));
        }
        const an::Result<an::CometMaskResult> drawn = an::renderCometMask(
            scope.execution(), starApertures, an::CometMaskRequest{}, access->view().data(), access->view().layout());
        if (!drawn.hasValue())
        {
            return drawn.status();
        }
    }

    // The blend, through the reference light's registrations and both planes' origins.
    const CompositeFrame& reference = frames[request.referenceIndex];
    const an::Result<an::CometCompositeBlendPlan> blend =
        an::planCometCompositeBlend(an::CometCompositeBlendRequest{.starTransform = reference.starTransform,
                                                                   .starPostTranslation = starCanvas.origin,
                                                                   .cometTransform = reference.cometTransform,
                                                                   .cometPostTranslation = cometOrigin,
                                                                   .scale = 1.0});
    if (!blend.hasValue())
    {
        return blend.status();
    }
    an::Result<an::Image<float>> composite = imageLike(scope.engine(), *starStack, 0u);
    if (!composite.hasValue())
    {
        return composite.status();
    }
    {
        const std::optional<an::ImageDescriptor> starDescriptor = starStack->descriptor();
        const std::optional<an::ImageDescriptor> cometDescriptor = cometStack->descriptor();
        const std::optional<an::ImageDescriptor> maskDescriptor = mask->descriptor();
        const auto starView = std::as_const(*starStack).view();
        const auto cometView = std::as_const(*cometStack).view();
        const auto maskView = std::as_const(*mask).view();
        std::optional<an::ImageWriteAccess<float>> access = composite->write();
        if (!starDescriptor.has_value() || !cometDescriptor.has_value() || !maskDescriptor.has_value() ||
            !starView.has_value() || !cometView.has_value() || !maskView.has_value() || !access.has_value())
        {
            return refusal(an::ErrorCode::NotInitialized, QStringLiteral("a stack of the composite holds nothing"));
        }
        const an::Result<an::CometCompositeBlendResult> blended = an::blendCometComposite(
            scope.execution(), *blend,
            an::CometCompositeSource{
                .descriptor = &*starDescriptor, .samples = starView->data(), .layout = &starView->layout()},
            an::CometCompositeSource{
                .descriptor = &*cometDescriptor, .samples = cometView->data(), .layout = &cometView->layout()},
            an::CometCompositeSource{
                .descriptor = &*maskDescriptor, .samples = maskView->data(), .layout = &maskView->layout()},
            access->view().data(), access->view().layout());
        if (!blended.hasValue())
        {
            return blended.status();
        }
    }

    QJsonObject fingerprint;
    fingerprint.insert(QStringLiteral("schemaVersion"), 2);
    fingerprint.insert(QStringLiteral("sources"), maskSources);
    StarsPlusCometCompositeOutput output{
        .composite = std::move(*composite),
        .cometLayer = std::move(*cometStack),
        .cometMask = std::move(*mask),
        .starBaseDescriptorId = starBaseDescriptorIdFor(products.starBaseProducts),
        .maskSettingsFingerprint = fingerprintJson(fingerprint),
        .diagnostic = QStringLiteral("Comet aperture replacement + aperture-difference star suppression")};
    return output;
}

} // namespace ImageIntegration::detail
