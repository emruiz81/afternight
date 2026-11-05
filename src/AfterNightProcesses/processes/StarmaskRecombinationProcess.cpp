// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The Starmask Recombination window: three selectors on the GUI thread - the starless image becoming the window's
/// target, the stars and the mask snapshotted into each run - and one engine composition on the run's worker,
/// written in the higher of the two images' representations as a new image beside the starless one.

#include "stdafx.hpp"
#include "StarmaskRecombinationProcess.h"
#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/ProcessCommonUtils.h"

#include <AfterNightEngine/Algorithms/Stars/StarMaskRecombination.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QSignalBlocker>

#include <utility>

namespace
{

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

} // namespace

StarmaskRecombinationProcess::StarmaskRecombinationProcess(QWidget* parent)
    : an::ui::TypedProcessControl<StarmaskRecombinationParams>(nullptr, getProcessNameStatic())
{
    Q_UNUSED(parent);

    m_ui = an::ui::processes::buildStarmaskRecombinationProcessUi(this);
    setProcessDescription(
        QStringLiteral("Recombine starless and star-mask inputs with optional mask inversion control."));

    m_SupportTargetViewControlSelection = false;
    m_SupportPerChannelProcessing = false;
    m_ShowWaitingSpinner = true;
    m_SupportMasking = false;
    m_ImagePreviewLayout = m_ui.verticalLayout_2;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.verticalLayout_2, m_ui.preview, nullptr, nullptr, nullptr,
                              {.supportPerChannel = false, .showWaitingSpinner = true});

    QObject::connect(m_ui.buttonBox, &QDialogButtonBox::clicked, this,
                     &StarmaskRecombinationProcess::on_dialog_box_button_clicked);
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        QObject::connect(selector, &QComboBox::currentTextChanged, this, [this](const QString&) {
            validateSelectedImages();
            settingsUpdated();
        });
    }
    QObject::connect(m_ui.invertMask, &QCheckBox::checkStateChanged, this,
                     [this](Qt::CheckState) { settingsUpdated(); });

    setWindowTitle(QStringLiteral("Starmask Recombination"));

    setExecutionAutostretchSupported(true);
    refreshTheme();
    initialize();
}

StarmaskRecombinationProcess::~StarmaskRecombinationProcess() = default;

std::string StarmaskRecombinationProcess::getProcessNameStatic()
{
    return "Starmask Recombination";
}

void StarmaskRecombinationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void StarmaskRecombinationProcess::addViewToSelectors(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        if (selector->findText(imageName) == -1)
        {
            selector->addItem(imageName);
            selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
}

void StarmaskRecombinationProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    addViewToSelectors(ctrl);
}

void StarmaskRecombinationProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    const QString imageName = QString::fromUtf8(ctrl->getUniqueName());
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        const int idx = selector->findText(imageName);
        if (idx != -1)
        {
            if (selector->currentText() == imageName)
            {
                selector->setCurrentText(NO_IMAGE_STR);
            }
            selector->removeItem(idx);
        }
    }
    validateSelectedImages();
}

void StarmaskRecombinationProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                        const ImageViewControl* ctrl)
{
    const QString newImageName = QString::fromUtf8(newName);
    const QString oldImageName = QString::fromUtf8(oldName);
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        const int idx = selector->findText(oldImageName);
        if (idx != -1)
        {
            selector->setItemText(idx, newImageName);
            selector->setItemData(idx, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
        }
    }
    validateSelectedImages();
}

void StarmaskRecombinationProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    if (ctrl == m_image_view_control || m_ui.starmaskImage->currentText() == name ||
        m_ui.maskImage->currentText() == name)
    {
        settingsUpdated();
    }
}

void StarmaskRecombinationProcess::onApplicationClose()
{
    close();
}

void StarmaskRecombinationProcess::initialize()
{
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        const QSignalBlocker blocker(selector);
        selector->addItem(NO_IMAGE_STR);
    }
    for (auto* viewControl : process_utils::listViewControls())
    {
        if (viewControl != nullptr && viewControl->hasImage())
        {
            addViewToSelectors(viewControl);
        }
    }
    for (QComboBox* selector : {m_ui.starmaskImage, m_ui.starlessImage, m_ui.maskImage})
    {
        selector->setCurrentText(NO_IMAGE_STR);
    }
    validateSelectedImages();
    adjustSize();
    resize(m_DefaultSizeNoPreview);
}

void StarmaskRecombinationProcess::validateSelectedImages()
{
    const QString starlessStr = m_ui.starlessImage->currentText();
    const QString starmaskStr = m_ui.starmaskImage->currentText();
    const QString maskStr = m_ui.maskImage->currentText();

    // The starless view is the window's target: the result is a new image beside it, carrying its record.
    ImageViewControl* starless = starlessStr == NO_IMAGE_STR || starlessStr.isEmpty()
                                     ? nullptr
                                     : process_utils::findViewControlByName(starlessStr);
    if (starless != nullptr && starless != m_image_view_control)
    {
        changeTargetViewControl(starless);
    }
    ImageViewControl* stars = starmaskStr == NO_IMAGE_STR || starmaskStr.isEmpty()
                                  ? nullptr
                                  : process_utils::findViewControlByName(starmaskStr);
    const bool hasMask =
        maskStr != NO_IMAGE_STR && !maskStr.isEmpty() && process_utils::findViewControlByName(maskStr) != nullptr;
    m_ui.invertMask->setEnabled(hasMask);

    const bool ready = starless != nullptr && starless == m_image_view_control && stars != nullptr && stars->hasImage();
    m_ui.preview->setEnabled(ready);
    if (ready)
    {
        if (isPreviewEnabled())
        {
            createPreviewControl();
        }
    }
    else if (isPreviewEnabled())
    {
        destroyPreviewControl();
    }
    m_ui.buttonBox->button(QDialogButtonBox::Ok)->setEnabled(ready);
}

void StarmaskRecombinationProcess::on_dialog_box_button_clicked(QAbstractButton* button)
{
    QDialogButtonBox* buttonBox = qobject_cast<QDialogButtonBox*>(sender());
    if (!buttonBox)
    {
        return;
    }
    const QDialogButtonBox::StandardButton sb = buttonBox->standardButton(button);
    if (sb == QDialogButtonBox::Ok)
    {
        m_IgnoreLastImageUpdatedEvent = true;
        applyProcess();
    }
    else if (sb == QDialogButtonBox::Close)
    {
        this->close();
    }
}

bool StarmaskRecombinationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void StarmaskRecombinationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void StarmaskRecombinationProcess::settingsUpdatedImpl() {}

void StarmaskRecombinationProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                               bool& executeInBackground, std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = true;
    executeInBackground = true;
    newImageName = fmt::format("{} - {} - MERGED", m_image_view_control ? m_image_view_control->getUniqueName() : "",
                               m_ui.starmaskImage->currentText().toStdString());
}

an::Status StarmaskRecombinationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                 an::ui::ProcessDestination& destination,
                                                                 const an::ui::ProcessRun& run,
                                                                 const StarmaskRecombinationParams& params,
                                                                 an::ui::ExecutionScope& scope)
{
    if (!params.stars)
    {
        return run.isForPreview() ? an::Status::success()
                                  : refusal(an::ErrorCode::InvalidArgument, "select the star image to put back");
    }
    const std::optional<an::ImageDescriptor> starlessDescribed = source.descriptor();
    if (!starlessDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the starless image holds nothing");
    }
    an::Result<process_utils::LeasedImage> stars =
        process_utils::LeasedImage::read(scope, *params.stars, run, params.previewArea, *starlessDescribed);
    if (!stars.hasValue())
    {
        return stars.status();
    }
    const an::ui::ProcessSource starsSource = stars->source();
    const std::optional<an::ImageDescriptor> starsDescribed = starsSource.descriptor();
    if (!starsDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the star image holds nothing");
    }
    if (!process_utils::sameSize(*starlessDescribed, *starsDescribed) ||
        starlessDescribed->extent().channelCount() != starsDescribed->extent().channelCount())
    {
        return refusal(an::ErrorCode::IncompatibleArguments,
                       "the star image does not have the starless image's size and channels");
    }
    std::optional<process_utils::LeasedImage> mask;
    std::optional<an::ui::ProcessSource> maskSource;
    if (params.mask)
    {
        an::Result<process_utils::LeasedImage> leased =
            process_utils::LeasedImage::read(scope, *params.mask, run, params.previewArea, *starlessDescribed);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        mask.emplace(std::move(*leased));
        maskSource.emplace(mask->source());
    }

    const an::Result<an::StarMaskRecombinationPlan> plan =
        an::planStarMaskRecombination(an::StarMaskRecombinationRequest{.invertMask = params.invertMask});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    // The result is in the higher of the two images' representations; an integer image is recombined in fractions of
    // full scale through a working copy, and the result narrowed back.
    const an::SampleType resultType = an::starMaskRecombinationSampleType(*plan, *starlessDescribed, *starsDescribed);
    const an::SampleType working = process_utils::compositionWorkingType(resultType);
    // Two physical images keep their sum above one; any other pair is added as fractions of full scale.
    const process_utils::PhysicalReading reading =
        process_utils::recombinationReading(*starlessDescribed, *starsDescribed);
    an::Result<std::unique_ptr<process_utils::CompositionSource>> starless =
        process_utils::bindForComposition(scope, source, working, reading);
    if (!starless.hasValue())
    {
        return starless.status();
    }
    an::Result<std::unique_ptr<process_utils::CompositionSource>> starsBound =
        process_utils::bindForComposition(scope, starsSource, working, reading);
    if (!starsBound.hasValue())
    {
        return starsBound.status();
    }
    std::unique_ptr<process_utils::CompositionSource> maskBound;
    if (maskSource.has_value())
    {
        an::Result<std::unique_ptr<process_utils::CompositionSource>> bound =
            process_utils::bindForComposition(scope, *maskSource, working, process_utils::PhysicalReading::AsFractions);
        if (!bound.hasValue())
        {
            return bound.status();
        }
        maskBound = std::move(*bound);
    }
    const an::Result<an::ImageDescriptor> described = process_utils::compositionDescriptor(
        *starlessDescribed, starlessDescribed->extent().channelCount(), resultType, scope.engine().limits());
    if (!described.hasValue())
    {
        return described.status();
    }
    an::Result<an::ImageMetadata> metadata = process_utils::recombinedResultMetadata(
        source.metadata(), *described,
        process_utils::CombinationRecord{.stateOperation = "starmask-recombination",
                                         .engineOperation = "an::recombineStarMask",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::StarMaskRecombinationSource starlessBound{.descriptor = &(*starless)->bound->descriptor(),
                                                        .samples = (*starless)->bound->samples(),
                                                        .layout = &(*starless)->bound->layout(),
                                                        .validity = (*starless)->bound->validity()};
    const an::StarMaskRecombinationSource starsSourceBound{.descriptor = &(*starsBound)->bound->descriptor(),
                                                           .samples = (*starsBound)->bound->samples(),
                                                           .layout = &(*starsBound)->bound->layout(),
                                                           .validity = (*starsBound)->bound->validity()};
    std::optional<an::StarMaskRecombinationSource> maskSourceBound;
    if (maskBound)
    {
        maskSourceBound.emplace(an::StarMaskRecombinationSource{.descriptor = &maskBound->bound->descriptor(),
                                                                .samples = maskBound->bound->samples(),
                                                                .layout = &maskBound->bound->layout(),
                                                                .validity = maskBound->bound->validity()});
    }
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeNarrowed(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::StarMaskRecombinationResult> ran =
                an::recombineStarMask(execution, *plan, starlessBound, starsSourceBound,
                                      maskSourceBound.has_value() ? &*maskSourceBound : nullptr, samples, layout);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    return destination.replace(std::move(*composed));
}

an::ui::PreviewScaling StarmaskRecombinationProcess::previewScaling() const
{
    // The stars are put back pixel by pixel, weighed by the mask at the same place: an interactive frame runs at full
    // scale over the part of the target the preview view shows, the stars and the mask cut to the same part, with no
    // reach around it.
    return an::ui::PreviewScaling::NativeRegion;
}

an::ui::ProcessStep
StarmaskRecombinationProcess::describeTypedProcessStep(const StarmaskRecombinationParams& params) const
{
    return an::ui::ProcessStep{
        .name = getProcessName(),
        .description = fmt::format("StarmaskRecombinationProcess: Image created from starless image '{}' and starmask "
                                   "image '{}' using operation add{}",
                                   params.starlessName.toStdString(), params.starsName.toStdString(),
                                   params.mask ? fmt::format(", through mask '{}'{}", params.maskName.toStdString(),
                                                             params.invertMask ? " inverted" : "")
                                               : std::string())};
}

// The stars and the mask: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
StarmaskRecombinationProcess::typedImagesRead(const StarmaskRecombinationParams& params) const
{
    return {params.stars, params.mask};
}

StarmaskRecombinationParams StarmaskRecombinationProcess::getParams() const
{
    StarmaskRecombinationParams params;
    params.starlessName = m_ui.starlessImage->currentText();
    params.starsName = m_ui.starmaskImage->currentText();
    if (params.starsName != NO_IMAGE_STR && !params.starsName.isEmpty())
    {
        if (auto* stars = process_utils::findViewControlByName(params.starsName); stars != nullptr && stars->hasImage())
        {
            params.stars = stars->document().asset();
        }
    }
    params.maskName = m_ui.maskImage->currentText();
    if (params.maskName != NO_IMAGE_STR && !params.maskName.isEmpty())
    {
        if (auto* mask = process_utils::findViewControlByName(params.maskName); mask != nullptr && mask->hasImage())
        {
            params.mask = mask->document().asset();
        }
    }
    params.invertMask = m_ui.invertMask->isChecked();
    params.previewArea = previewArea();
    return params;
}
