// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "BinarizeProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/MaskProcessUtils.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Masks/MaskOperations.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>

#include <array>
#include <cmath>
#include <type_traits>

namespace
{

/// The largest code of a representation, which a selected sample becomes; one for a floating one.
[[nodiscard]] double largestCode(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::UInt8:
        return 255.0;
    case an::SampleType::Int8:
        return 127.0;
    case an::SampleType::UInt16:
        return 65535.0;
    case an::SampleType::Int16:
        return 32767.0;
    case an::SampleType::UInt32:
        return 4294967295.0;
    case an::SampleType::Int32:
        return 2147483647.0;
    default:
        return 1.0;
    }
}

/// The smallest code of a representation, which an unselected sample becomes; zero for a floating one.
[[nodiscard]] double smallestCode(an::SampleType type) noexcept
{
    switch (type)
    {
    case an::SampleType::Int8:
        return -128.0;
    case an::SampleType::Int16:
        return -32768.0;
    case an::SampleType::Int32:
        return -2147483648.0;
    default:
        return 0.0;
    }
}

/// A threshold in the unit interval, stated in a representation's codes before comparing: an unsigned integer's
/// largest value times the threshold, truncated; a signed one's range from its smallest value, rounded; a
/// single-precision sample the threshold in single precision; a double-precision one the threshold.
[[nodiscard]] double thresholdCode(an::SampleType type, double value) noexcept
{
    switch (type)
    {
    case an::SampleType::UInt8:
    case an::SampleType::UInt16:
    case an::SampleType::UInt32:
        return std::trunc(largestCode(type) * value);
    case an::SampleType::Int8:
    case an::SampleType::Int16:
    case an::SampleType::Int32:
        return std::round(value * (largestCode(type) - smallestCode(type)) + smallestCode(type));
    case an::SampleType::Float32:
        return static_cast<double>(static_cast<float>(value));
    case an::SampleType::Float64:
        return value;
    }
    return value;
}

} // namespace

BinarizeProcess::BinarizeProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<BinarizeParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildBinarizeProcessUi(this);
    setProcessDescription(QStringLiteral("Generate binary masks from global or per-channel threshold values."));

    QObject::connect(m_ui.selectionMode, &QComboBox::currentTextChanged, this,
                     &BinarizeProcess::on_selectionMode_currentTextChanged);

    INIT_D_SPINBOX_CTRL(rgbkControl, m_ui.rgbkValue, m_ui.rgbkSlider, &BinarizeProcess::on_rgbkChanged);
    INIT_D_SPINBOX_CTRL(greenControl, m_ui.greenValue, m_ui.greenSlider, &BinarizeProcess::on_greenChanged);
    INIT_D_SPINBOX_CTRL(blueControl, m_ui.blueValue, m_ui.blueSlider, &BinarizeProcess::on_blueChanged);

    updateWindowTitle();

    m_CreatedViewControlsAreNeighbour = true;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, nullptr, false, false);

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);

    checkImageColorSpace();

    settingsUpdated();

    refreshTheme();
}

std::string BinarizeProcess::getProcessNameStatic()
{
    return "Binarize";
}

bool BinarizeProcess::requiresExclusiveAccessStatic()
{
    return false;
}

void BinarizeProcess::onImageLoaded(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageLoaded(ctrl);
    checkImageColorSpace();
}

void BinarizeProcess::onImageUpdated(ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onImageUpdated(ctrl);
    checkImageColorSpace();
}

void BinarizeProcess::onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl)
{
    ImageRTPreviewControl::onViewControlChanged(oldCtrl, newCtrl);
    checkImageColorSpace();
}

BinarizeProcess::~BinarizeProcess() {}

void BinarizeProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void BinarizeProcess::on_rgbkChanged(double value)
{
    // Joint channels move the green and blue thresholds with the first.
    if (m_ui.selectionMode->currentIndex() == 0)
    {
        greenControl->setValue(value);
        blueControl->setValue(value);
    }

    settingsUpdated();
}

void BinarizeProcess::on_greenChanged(double value)
{
    settingsUpdated();
}

void BinarizeProcess::on_blueChanged(double value)
{
    settingsUpdated();
}

void BinarizeProcess::on_selectionMode_currentTextChanged(const QString& text)
{
    if (text == QStringLiteral("Joint RGB/K channels"))
    {
        greenControl->setEnabled(false);
        blueControl->setEnabled(false);

        greenControl->setValue(rgbkControl->getValue());
        blueControl->setValue(rgbkControl->getValue());

        m_ui.rgbkLabel->setText(QStringLiteral("RGB/K"));
    }
    else
    {
        greenControl->setEnabled(true);
        blueControl->setEnabled(true);
        m_ui.rgbkLabel->setText(QStringLiteral("Red"));
    }

    settingsUpdated();
}

bool BinarizeProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void BinarizeProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

an::Status BinarizeProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                    an::ui::ProcessDestination& destination,
                                                    const an::ui::ProcessRun& run, const BinarizeParams& params,
                                                    an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> described = source.descriptor();
    if (!described.has_value())
    {
        return an::Status::failure(an::ErrorCode::NotInitialized, "the image holds nothing");
    }

    // Each of red, green and blue has its threshold, and an alpha channel the lowest, in the individual mode; every
    // channel the first threshold otherwise.
    const bool individual = params.selectionMode == 1 && process_utils::describesColourImage(*described);
    const std::array<double, 4u> thresholds =
        individual ? std::array<double, 4u>{params.rgbk, params.green, params.blue, 0.0}
                   : std::array<double, 4u>{params.rgbk, params.rgbk, params.rgbk, params.rgbk};
    const an::SampleType type = described->sampleType();

    const an::Status ran = destination.asset().visitWrite([&](auto&& access) -> an::Status {
        const auto view = access.view();
        const std::uint32_t channels = view.layout().extent().channelCount();
        for (std::uint32_t channel = 0u; channel < channels; ++channel)
        {
            const auto plane = view.plane(channel);
            if (!plane.has_value())
            {
                return an::Status::failure(an::ErrorCode::NotInitialized, "a channel of the result cannot be written");
            }
            // The stored sample is compared with the threshold and with the largest value it can hold, and the
            // representation's largest or smallest value is written.
            an::BinarizeRequest request;
            request.threshold.predicate = an::MaskPredicate::InsideClosedRange;
            request.threshold.lowerBound = thresholdCode(type, thresholds[(std::min)(channel, 3u)]);
            request.threshold.upperBound = thresholdCode(type, 1.0);
            request.threshold.nonFiniteSelected = false;
            request.selectedValue = largestCode(type);
            request.unselectedValue = smallestCode(type);
            request.conversion.domain = an::DomainMapping::CodeValue;
            const an::Result<an::BinarizeResult> binarized = an::binarizeImage(
                scope.execution(), type, plane->data(), plane->layout(), request, plane->data(), plane->layout());
            if (!binarized.hasValue())
            {
                return binarized.status();
            }
        }
        return an::Status::success();
    });
    if (ran.failed() || run.isForPreview())
    {
        return ran;
    }

    // Every sample holds one of two numbers now, so every one of them means something.
    return process_utils::recordNonlinearResult(
        scope, destination,
        process_utils::NonlinearResultRecord{
            .stateOperation = "binarize",
            .engineOperation = "an::binarizeImage",
            .detail = individual ? fmt::format("thresholds {}, {}, {}", params.rgbk, params.green, params.blue)
                                 : fmt::format("threshold {}", params.rgbk),
            .restatedValidity = an::ValidityDescriptor::allValid()});
}

an::ui::ProcessStep BinarizeProcess::describeTypedProcessStep(const BinarizeParams& params) const
{
    std::string description;
    if (process_utils::holdsColourImage(m_image_view_control))
    {
        description = params.selectionMode == 0
                          ? fmt::format("R: {}, G: {}, B: {} ", params.rgbk, params.rgbk, params.rgbk)
                          : fmt::format("R: {}, G: {}, B: {} ", params.rgbk, params.green, params.blue);
    }
    else
    {
        description = fmt::format("K: {} ", params.rgbk);
    }
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::move(description)};
}

void BinarizeProcess::resetSettings()
{
    rgbkControl->setValue(0.5);
    greenControl->setValue(0.5);
    blueControl->setValue(0.5);

    m_ui.selectionMode->setCurrentIndex(0);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void BinarizeProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(true);
    }
}

void BinarizeProcess::checkImageColorSpace()
{
    if (process_utils::holdsColourImage(m_image_view_control))
    {
        m_ui.selectionMode->setEnabled(true);
    }
    else
    {
        m_ui.selectionMode->setCurrentIndex(0);
        m_ui.selectionMode->setEnabled(false);
    }
}

BinarizeParams BinarizeProcess::getParams() const
{
    BinarizeParams params;
    params.rgbk = rgbkControl->getValue();
    params.green = greenControl->getValue();
    params.blue = blueControl->getValue();
    params.selectionMode = m_ui.selectionMode->currentIndex();

    return params;
}
