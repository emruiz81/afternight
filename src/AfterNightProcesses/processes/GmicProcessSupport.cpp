// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"

#include "processes/GmicProcessSupport.h"

#include "processes/SpatialProcessSupport.h"

#include "controls/ProgressDialogControl.h"

#include <AfterNightEngineGmicAdapter/GmicAdapterCapabilities.h>
#include <AfterNightEngineGmicAdapter/GmicImageBridge.h>
#include <AfterNightEngineGmicAdapter/GmicRuntimeAdapter.h>

#include <AfterNightEngine/Algorithms/Pointwise/Arithmetic.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseExecution.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageView.h>
#include <AfterNightEngine/Interop/ExternalBuffer.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace process_utils
{

const std::string_view kLocalContrastEnhancementDefinitions = "_fx_LCE :\n"
                                                              "  SpatialRadius=$1\n"
                                                              "  Amount=$2\n"
                                                              "  DarknessLevel=$3\n"
                                                              "  LightnessLevel=$4\n"
                                                              "  Threshold=0\n"
                                                              "  +b. $SpatialRadius\n"
                                                              "  -. .. *. -$Amount\n"
                                                              "  +norm. ge. $Threshold% ri. .. *[-2,-1]\n"
                                                              "  +c. 0,100% c.. -100%,0 *.. $DarknessLevel *. "
                                                              "$LightnessLevel +[-2,-1]\n"
                                                              "  +[-2,-1] c. 0,255\n"
                                                              "\n"
                                                              "fx_LCE :\n"
                                                              "  ac \"_fx_LCE $1,$2,$3,$4\",$5,0\n";

bool gmicAvailable() noexcept
{
    return an::gmicAdapterCapabilities().libraryCompiledIn;
}

void refuseWithoutGmic(QCheckBox* preview, QDialogButtonBox* buttons)
{
    if (gmicAvailable())
    {
        return;
    }
    if (preview != nullptr)
    {
        preview->setChecked(false);
        preview->setEnabled(false);
        preview->setToolTip(QString::fromLatin1(kGmicUnavailableText));
    }
    if (buttons != nullptr)
    {
        if (QPushButton* apply = buttons->button(QDialogButtonBox::Apply))
        {
            apply->setEnabled(false);
            apply->setToolTip(QString::fromLatin1(kGmicUnavailableText));
        }
    }
}

ProgressDialogControl* gmicProgressDialog(QWidget* parent, const QString& title, const bool forSequence)
{
    auto* progress = new ProgressDialogControl(parent, title, forSequence);
    progress->setProgressBarRange(0, 100);
    if (!forSequence)
    {
        progress->setBusy(true);
    }
    return progress;
}

an::Status runGmicCommand(an::ui::ExecutionScope& scope, const an::ChannelScopeInput& input,
                          const an::ChannelScopeOutput& output, const GmicCommand& command)
{
    an::Result<an::GmicRuntimeAdapter> session =
        an::GmicRuntimeAdapter::create(scope.engine(), an::GmicRuntimeOptions{.customCommands = command.definitions});
    if (!session.hasValue())
    {
        return session.status();
    }
    const an::ExecutionContext& execution = scope.execution();
    return runOverUnitFloat(
        scope.engine(), execution, input.descriptor->sampleType(), output.samples, *output.layout,
        [&](float* samples, const an::ImageLayout& layout) -> an::Status {
            const std::span<float> all(samples, static_cast<std::size_t>(layout.totalBytes() / sizeof(float)));
            // The library reads a sample that is not a finite number as a number, and some of its commands fault on
            // one, so an image holding one is not handed over.
            if (std::ranges::any_of(all, [](float sample) { return !std::isfinite(sample); }))
            {
                return an::Status::failure(an::ErrorCode::InvalidData,
                                           "the image holds a sample that is not a finite number, which G'MIC cannot "
                                           "process");
            }
            const std::optional<an::ImageView<float>> view = an::ImageView<float>::create(all, layout);
            if (!view.has_value())
            {
                return an::Status::failure(an::ErrorCode::IncompatibleArguments,
                                           "the working samples cannot be viewed as Float32");
            }
            // The library works on fractions of full scale, as the engine's working copy states them.
            const an::ExternalBufferContract contract{.engineDomain = an::NumericDomain::NormalizedLinear,
                                                      .libraryDomain = an::NumericDomain::NormalizedLinear,
                                                      .ownership = an::BufferOwnership::EngineOwned,
                                                      .scope = an::BorrowScope::SingleCall,
                                                      .mutability = an::BufferMutability::ReadWrite};
            const std::optional<an::GmicImageDescription> described = an::describeAsGmicImage(*view, contract);
            if (!described.has_value())
            {
                return an::Status::failure(an::ErrorCode::InvalidArgument,
                                           std::string("the G'MIC bridge refuses the samples: ") +
                                               an::bridgeRefusalName(an::checkGmicImage(*view, contract)));
            }
            an::Result<an::RawFrame> ran = session->run(
                execution,
                an::GmicImageCall{.command = command.text,
                                  .source = *described,
                                  .sharing = an::GmicSourceSharing::Share,
                                  .intent = an::GmicResultIntent{.channelSchema = an::ChannelSchema::Named}});
            if (!ran.hasValue())
            {
                return ran.status();
            }
            const std::optional<an::ImageLayout> produced = ran->layout();
            if (!produced.has_value() || produced->extent() != layout.extent())
            {
                return an::Status::failure(an::ErrorCode::ExternalFailure,
                                           "the G'MIC command left an image of another shape");
            }
            // A command that worked in place left its answer in the samples; one that made another image is copied
            // back.
            if (ran->bytes().data() != reinterpret_cast<const std::byte*>(samples))
            {
                an::ConversionCounters counters;
                if (an::Status copied = an::convertImageSamples(execution, an::SampleType::Float32, ran->bytes().data(),
                                                                *produced, an::SampleType::Float32, samples, layout,
                                                                an::ConversionPolicy{}, counters);
                    copied.failed())
                {
                    return copied;
                }
            }
            const an::PointwisePlanShape shape{.channelCount = layout.planeCount()};
            const an::Result<an::PointwiseProgram> clamp =
                an::buildClampProgram(shape, an::ClampInterval{.low = 0.0, .high = 1.0});
            if (!clamp.hasValue())
            {
                return clamp.status();
            }
            const std::array<an::PointwiseImageView, 1u> sources{
                an::PointwiseImageView{.samples = samples, .layout = &layout}};
            return statusOf(an::executePointwiseProgram(
                execution, *clamp, sources, an::PointwiseMutableImageView{.samples = samples, .layout = &layout},
                an::KernelValidityView{}));
        });
}

} // namespace process_utils
