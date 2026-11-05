// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The NB Color Mapper window: the layer table edited on the GUI thread and copied into each run, and one engine
/// operation on the run's worker over unit-fraction copies of the layers' images, the colourized layers an Apply
/// asks for staged and opened beside the result once it lands.

#include "stdafx.hpp"
#include "NBColorMapperProcess.h"

#include "controls/ImageSelectorTooltipBuilder.h"
#include "controls/ImageViewControl.h"
#include "controls/ProcessButtonIcons.h"
#include "processes/CombinationProcessSupport.h"
#include "processes/PointwiseProcessUtils.h"
#include "processes/ProcessCommonUtils.h"
#include "widgets/VectorSlopeWidget.h"

#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidgetItem>

#include <cmath>
#include <utility>

namespace
{

constexpr int kModificationNone = 0;
constexpr int kModificationAdd = 1;
constexpr int kModificationMultiply = 2;
constexpr int kModificationBoost = 3;

[[nodiscard]] an::Status refusal(an::ErrorCode code, std::string message)
{
    return an::Status::failure(code, std::move(message));
}

void addNoImageItem(QComboBox* selector)
{
    if (selector)
    {
        selector->addItem(NO_IMAGE_STR);
    }
}

void addImageItem(QComboBox* selector, const ImageViewControl* control)
{
    if (!selector || !control)
    {
        return;
    }
    selector->addItem(QString::fromUtf8(control->getUniqueName()));
    selector->setItemData(selector->count() - 1, an::ui::buildImageSelectorTooltip(control), Qt::ToolTipRole);
}

void restoreSelection(QComboBox* selector, const QString& preferred)
{
    if (!selector)
    {
        return;
    }
    const QString selection = preferred.isEmpty() ? NO_IMAGE_STR : preferred;
    const int index = selector->findText(selection);
    selector->setCurrentText(index >= 0 ? selection : NO_IMAGE_STR);
}

void setTableText(QTableWidget* table, const int row, const int column, const QString& text)
{
    if (!table)
    {
        return;
    }
    if (auto* item = table->item(row, column))
    {
        item->setText(text);
        return;
    }
    table->setItem(row, column, new QTableWidgetItem(text));
}

[[nodiscard]] bool supportedLayerView(const ImageViewControl* view)
{
    if (view == nullptr || view->isSequence() || !view->hasImage())
    {
        return false;
    }
    const std::optional<an::ImageDescriptor> described = view->document().asset()->descriptor();
    return described.has_value() &&
           (described->extent().channelCount() == 1u || process_utils::describesColourImage(*described));
}

[[nodiscard]] QString colorSummary(const NBColorMapperProcessLayerState& state)
{
    if (!state.layer.included)
    {
        return QStringLiteral("Excluded");
    }
    if (auto* control = process_utils::findViewControlByName(QString::fromStdString(state.imageName));
        control != nullptr && process_utils::holdsColourImage(control))
    {
        return QStringLiteral("RGB");
    }
    return QStringLiteral("H %1 / S %2").arg(state.layer.hueDegrees, 0, 'f', 0).arg(state.layer.saturation, 0, 'f', 2);
}

[[nodiscard]] QString modificationSummary(const an::NarrowbandColorLayer& layer)
{
    const int source = layer.modificationSource.has_value() ? static_cast<int>(*layer.modificationSource) + 1 : 0;
    switch (layer.modification)
    {
    case an::NarrowbandModificationMode::Add:
        return QStringLiteral("Add #%1").arg(source);
    case an::NarrowbandModificationMode::Multiply:
        return QStringLiteral("Multiply #%1").arg(source);
    case an::NarrowbandModificationMode::Boost:
        return QStringLiteral("Boost #%1").arg(source);
    case an::NarrowbandModificationMode::None:
    default:
        break;
    }
    return QStringLiteral("None");
}

[[nodiscard]] an::NarrowbandModificationMode modificationModeFromIndex(const int index)
{
    switch (index)
    {
    case kModificationAdd:
        return an::NarrowbandModificationMode::Add;
    case kModificationMultiply:
        return an::NarrowbandModificationMode::Multiply;
    case kModificationBoost:
        return an::NarrowbandModificationMode::Boost;
    default:
        return an::NarrowbandModificationMode::None;
    }
}

[[nodiscard]] int modificationModeToIndex(const an::NarrowbandModificationMode mode)
{
    switch (mode)
    {
    case an::NarrowbandModificationMode::Add:
        return kModificationAdd;
    case an::NarrowbandModificationMode::Multiply:
        return kModificationMultiply;
    case an::NarrowbandModificationMode::Boost:
        return kModificationBoost;
    case an::NarrowbandModificationMode::None:
    default:
        break;
    }
    return kModificationNone;
}

[[nodiscard]] std::string outputBaseNameForLayers(const std::vector<NBColorMapperProcessLayerState>& layers,
                                                  const ImageViewControl* fallback)
{
    for (const auto& layer : layers)
    {
        if (!layer.imageName.empty() && QString::fromStdString(layer.imageName) != NO_IMAGE_STR)
        {
            return layer.imageName;
        }
    }
    return fallback ? fallback->getUniqueName() : "NB Color Mapper";
}

/// One layer read for a run: the lease on the window's snapshot, the unit-fraction copy in the working
/// representation and the lease and binding on that.
struct LayerInput
{
    process_utils::LeasedImage leased;
    an::consumer::ImageAsset copy;
    an::consumer::AssetReadLease copyLease;
    std::unique_ptr<process_utils::BoundImage> bound;
};

/// The working representation the layers share: Float64 when any layer's own representation is worked in it.
[[nodiscard]] an::SampleType sharedWorkingType(const NBColorMapperProcessParams& params)
{
    an::SampleType working = an::SampleType::Float32;
    for (const auto& layer : params.layers)
    {
        if (!layer.image)
        {
            continue;
        }
        const std::optional<an::ImageDescriptor> described = layer.image->descriptor();
        if (described.has_value() &&
            process_utils::normalizedWorkingType(described->sampleType()) == an::SampleType::Float64)
        {
            working = an::SampleType::Float64;
        }
    }
    return working;
}

} // namespace

namespace an::ui::processes::nbcolormapper_detail
{

bool combinationReady(const NBColorMapperProcessParams& params)
{
    if (params.layers.empty())
    {
        return false;
    }
    const an::NarrowbandColorMapRequest& request = params.request;
    if (!std::isfinite(request.targetBackground) || request.targetBackground <= 0.0 || request.targetBackground >= 1.0)
    {
        return false;
    }
    if (request.background.enabled &&
        (!std::isfinite(request.background.lowerSigmas) || !std::isfinite(request.background.upperSigmas) ||
         request.background.upperSigmas <= request.background.lowerSigmas))
    {
        return false;
    }
    std::optional<an::ImageDescriptor> first;
    bool included = false;
    for (std::size_t index = 0u; index < params.layers.size(); ++index)
    {
        const NBColorMapperProcessParams::Layer& layer = params.layers[index];
        if (!layer.image)
        {
            return false;
        }
        const std::optional<an::ImageDescriptor> described = layer.image->descriptor();
        if (!described.has_value() ||
            (described->extent().channelCount() != 1u && !process_utils::describesColourImage(*described)))
        {
            return false;
        }
        if (!first.has_value())
        {
            first = described;
        }
        else if (!process_utils::sameSize(*first, *described))
        {
            return false;
        }
        if (!std::isfinite(layer.layer.hueDegrees) || !std::isfinite(layer.layer.saturation) ||
            !std::isfinite(layer.layer.midtones) || !std::isfinite(layer.layer.blackPoint) ||
            !std::isfinite(layer.layer.modificationFactor))
        {
            return false;
        }
        if (layer.layer.modification != an::NarrowbandModificationMode::None)
        {
            if (!layer.layer.modificationSource.has_value() ||
                *layer.layer.modificationSource >= params.layers.size() || *layer.layer.modificationSource == index)
            {
                return false;
            }
        }
        included = included || layer.layer.included;
    }
    return included;
}

} // namespace an::ui::processes::nbcolormapper_detail

NBColorMapperProcess::NBColorMapperProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<NBColorMapperProcessParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildNBColorMapperProcessUi(this);
    setProcessDescription(QStringLiteral("Map mono and RGB narrowband layers into a combined color image."));

    m_SupportTargetViewControlSelection = false;
    m_SupportPerChannelProcessing = false;
    m_FixedLayout = false;
    m_ShowWaitingSpinner = false;
    m_CreatedViewControlsAreNeighbour = true;
    m_ImagePreviewLayout = m_ui.previewLayout;
    m_MainHorizontalLayout = m_ui.horizontalLayoutMain;

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, nullptr, m_ui.buttonBox, nullptr, nullptr,
                              ImageRTPreviewControl::ProcessInitOptions{});

    connectUi();
    updateWindowTitle();

    setupHistogramPreview(m_ui.chartScrollArea, m_ui.plotResolution, m_ui.zoomIn, m_ui.zoomOut, m_ui.zoomReset,
                          m_ui.gridButton, m_ui.localChannelNormalization, m_ui.showClippingInfo, m_ui.showHistogram,
                          m_ui.showHistogramAreas);

    rebuildImageSelectors();
    setExecutionAutostretchSupported(true);
    setupPreviewControl(false, QSize(512, 512), QStringLiteral("NB Color Mapper preview"));
    resetSettings();
    autoAddActiveLayer();
    refreshLayerTable();
    settingsUpdated();
    refreshTheme();
}

NBColorMapperProcess::~NBColorMapperProcess() = default;

std::string NBColorMapperProcess::getProcessNameStatic()
{
    return "NB Color Mapper";
}

bool NBColorMapperProcess::requiresExclusiveAccessStatic()
{
    return false;
}

const std::vector<NBColorMapperProcessLayerState>& NBColorMapperProcess::layers() const noexcept
{
    return m_layers;
}

void NBColorMapperProcess::connectUi()
{
    const auto update = [this]() {
        if (!m_loadingLayerEditor)
        {
            syncSelectedLayerFromEditor();
            refreshLayerColorPicker();
            updateModificationControlsEnabled();
            settingsUpdated();
        }
    };
    for (auto* selector : {m_ui.layerImageSelector, m_ui.modificationMode, m_ui.modificationLayerSelector})
    {
        QObject::connect(selector, qOverload<int>(&QComboBox::currentIndexChanged), this, update);
    }
    for (auto* spin :
         {m_ui.layerHue, m_ui.layerSaturation, m_ui.layerMidtones, m_ui.layerBlackpoint, m_ui.modificationFactor,
          m_ui.backgroundLowerSigma, m_ui.backgroundUpperSigma, m_ui.stfTargetBackground})
    {
        QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, update);
    }
    for (auto* check :
         {m_ui.layerInclude, m_ui.modificationInvert, m_ui.modificationMedianOffset, m_ui.backgroundEnabled,
          m_ui.backgroundRescaleHigh, m_ui.applyPreviewStf, m_ui.returnColorizedLayers, m_ui.applyStfToCombinedOutput})
    {
        QObject::connect(check, &QCheckBox::checkStateChanged, this, update);
    }
    QObject::connect(m_ui.layerColorPicker, &an::ui::VectorSlopeWidget::layerColorChanged, this,
                     [this](double hueDegrees, double saturation) {
                         const QSignalBlocker hueBlocker(m_ui.layerHue);
                         const QSignalBlocker saturationBlocker(m_ui.layerSaturation);
                         m_ui.layerHue->setValue(hueDegrees);
                         m_ui.layerSaturation->setValue(saturation);
                         if (!m_loadingLayerEditor)
                         {
                             syncSelectedLayerFromEditor();
                             settingsUpdated();
                         }
                     });
    QObject::connect(m_ui.layersTable, &QTableWidget::itemSelectionChanged, this,
                     [this]() { refreshLayerEditorFromSelection(); });
    QObject::connect(m_ui.addLayerButton, &QPushButton::clicked, this, [this]() { addLayerFromEditor(); });
    QObject::connect(m_ui.removeLayerButton, &QPushButton::clicked, this, [this]() { removeSelectedLayer(); });
}

void NBColorMapperProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
    }
}

void NBColorMapperProcess::onViewControlCreated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlCreated(ctrl);
    if (isLayerViewSupported(ctrl))
    {
        addImageItem(m_ui.layerImageSelector, ctrl);
    }
}

void NBColorMapperProcess::onViewControlDestroyed(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlDestroyed(ctrl);
    if (!ctrl)
    {
        return;
    }
    const QString name = QString::fromUtf8(ctrl->getUniqueName());
    const int index = m_ui.layerImageSelector->findText(name);
    if (index >= 0)
    {
        if (m_ui.layerImageSelector->currentText() == name)
        {
            m_ui.layerImageSelector->setCurrentText(NO_IMAGE_STR);
        }
        m_ui.layerImageSelector->removeItem(index);
    }
    settingsUpdated();
}

void NBColorMapperProcess::onViewControlRenamed(const std::string& newName, const std::string& oldName,
                                                const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlRenamed(newName, oldName, ctrl);
    const QString oldNameQt = QString::fromUtf8(oldName);
    const QString newNameQt = QString::fromUtf8(newName);
    const int selectorIndex = m_ui.layerImageSelector->findText(oldNameQt);
    if (selectorIndex >= 0)
    {
        m_ui.layerImageSelector->setItemText(selectorIndex, newNameQt);
        m_ui.layerImageSelector->setItemData(selectorIndex, an::ui::buildImageSelectorTooltip(ctrl), Qt::ToolTipRole);
    }
    for (auto& layer : m_layers)
    {
        if (layer.imageName == oldName)
        {
            layer.imageName = newName;
        }
    }
    refreshLayerTable();
    settingsUpdated();
}

void NBColorMapperProcess::onViewControlUpdated(const ImageViewControl* ctrl)
{
    ImageRTPreviewControl::onViewControlUpdated(ctrl);
    const QString selectedLayer = m_ui.layerImageSelector->currentText();
    rebuildImageSelectors(selectedLayer);
    refreshLayerTable();
    settingsUpdated();
}

bool NBColorMapperProcess::isPreviewEnabled() const
{
    return true;
}

void NBColorMapperProcess::enablePreview(bool /*enabled*/) {}

void NBColorMapperProcess::executeProcessStarted(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (previewMode == RTPreviewMode::NoPreview && !sequenceMode)
    {
        m_stagedOutputs.clear();
    }
}

void NBColorMapperProcess::executeProcessCompletedImpl(const RTPreviewMode previewMode, const bool sequenceMode)
{
    if (sequenceMode || previewMode != RTPreviewMode::NoPreview)
    {
        return;
    }
    process_utils::openStagedOutputs(m_stagedOutputs.take(), m_image_view_control);
}

an::Status NBColorMapperProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                         an::ui::ProcessDestination& destination,
                                                         const an::ui::ProcessRun& run,
                                                         const NBColorMapperProcessParams& params,
                                                         an::ui::ExecutionScope& scope)
{
    const std::optional<an::ImageDescriptor> targetDescribed = source.descriptor();
    if (!targetDescribed.has_value())
    {
        return refusal(an::ErrorCode::NotInitialized, "the target holds nothing");
    }
    if (run.isForSequence())
    {
        return refusal(an::ErrorCode::Unsupported, "the narrowband colour mapper needs a still image");
    }
    if (!an::ui::processes::nbcolormapper_detail::combinationReady(params))
    {
        return run.isForPreview()
                   ? an::Status::success()
                   : refusal(an::ErrorCode::InvalidArgument,
                             "add at least one included layer, every layer a grey or colour image of the first's "
                             "size, and name another layer as a modifier");
    }

    // Every layer as a unit-fraction copy in one working representation, bound for the map.
    const an::SampleType working = sharedWorkingType(params);
    std::vector<std::unique_ptr<LayerInput>> inputs;
    std::vector<an::NarrowbandColorLayer> layers;
    std::vector<const an::ImageMetadata*> sources;
    for (const NBColorMapperProcessParams::Layer& layer : params.layers)
    {
        an::Result<process_utils::LeasedImage> leased =
            process_utils::LeasedImage::read(scope, *layer.image, run, params.previewArea, *targetDescribed);
        if (!leased.hasValue())
        {
            return leased.status();
        }
        an::Result<an::consumer::ImageAsset> copy = process_utils::unitFloatCopy(scope, leased->source(), working);
        if (!copy.hasValue())
        {
            return copy.status();
        }
        an::Result<an::consumer::AssetReadLease> copyLease = copy->read(scope.execution().stopToken());
        if (!copyLease.hasValue())
        {
            return copyLease.status();
        }
        auto input = std::make_unique<LayerInput>(LayerInput{.leased = std::move(*leased),
                                                             .copy = std::move(*copy),
                                                             .copyLease = std::move(*copyLease),
                                                             .bound = nullptr});
        an::Result<std::unique_ptr<process_utils::BoundImage>> bound =
            process_utils::BoundImage::bind(an::ui::ProcessSource::of(input->copyLease));
        if (!bound.hasValue())
        {
            return bound.status();
        }
        input->bound = std::move(*bound);
        an::NarrowbandColorLayer engineLayer = layer.layer;
        engineLayer.samples = input->bound->samples();
        engineLayer.layout = &input->bound->layout();
        layers.push_back(engineLayer);
        sources.push_back(&input->leased.source().metadata());
        inputs.push_back(std::move(input));
    }
    const an::Result<an::ImageDescriptor> described =
        an::narrowbandColorMapOutputDescriptor(inputs.front()->bound->descriptor(), layers);
    if (!described.hasValue())
    {
        return described.status();
    }
    an::NarrowbandColorMapRequest request = params.request;
    request.applyOutputStretch = run.isForPreview() ? params.applyPreviewStretch : params.applyStretchToOutput;
    an::Result<an::ImageMetadata> metadata = process_utils::combinedResultMetadata(
        sources, an::kNarrowbandColorMapEffect, *described,
        process_utils::CombinationRecord{.stateOperation = "nb-color-mapper",
                                         .engineOperation = "an::mapNarrowbandColor",
                                         .detail = describeTypedProcessStep(params).description,
                                         .linearity = process_utils::CombinationLinearity::Nonlinear});
    if (!metadata.hasValue())
    {
        return metadata.status();
    }
    const an::ExecutionContext& execution = scope.execution();
    an::Result<an::consumer::ImageAsset> composed = process_utils::composeImage(
        scope, *described, std::move(*metadata), [&](void* samples, const an::ImageLayout& layout) -> an::Status {
            const an::Result<an::NarrowbandColorMapResult> ran =
                an::mapNarrowbandColor(execution, working, layers, samples, layout, request);
            return ran.hasValue() ? an::Status::success() : ran.status();
        });
    if (!composed.hasValue())
    {
        return composed.status();
    }
    if (an::Status replaced = destination.replace(std::move(*composed)); replaced.failed())
    {
        return replaced;
    }
    if (run.isForPreview() || run.isForSequence() || !params.returnColorizedLayers)
    {
        return an::Status::success();
    }

    // Each included layer on its own, its modifier read but not shown, opened beside the result once it lands.
    for (std::size_t index = 0u; index < layers.size(); ++index)
    {
        if (!params.layers[index].layer.included)
        {
            continue;
        }
        std::vector<an::NarrowbandColorLayer> alone = layers;
        for (std::size_t other = 0u; other < alone.size(); ++other)
        {
            alone[other].included = other == index;
        }
        const an::Result<an::ImageDescriptor> layerDescribed =
            an::narrowbandColorMapOutputDescriptor(inputs.front()->bound->descriptor(), alone);
        if (!layerDescribed.hasValue())
        {
            return layerDescribed.status();
        }
        an::Result<an::ImageMetadata> layerMetadata = process_utils::recombinedResultMetadata(
            *sources[index], *layerDescribed,
            process_utils::CombinationRecord{
                .stateOperation = "nb-color-mapper-layer",
                .engineOperation = "an::mapNarrowbandColor",
                .detail = fmt::format("layer '{}' colourized alone", params.layers[index].imageName),
                .linearity = process_utils::CombinationLinearity::Nonlinear});
        if (!layerMetadata.hasValue())
        {
            return layerMetadata.status();
        }
        an::NarrowbandColorMapRequest alongRequest = request;
        alongRequest.background.enabled = false;
        alongRequest.applyOutputStretch = false;
        an::Result<an::consumer::ImageAsset> colourized =
            process_utils::composeImage(scope, *layerDescribed, std::move(*layerMetadata),
                                        [&](void* samples, const an::ImageLayout& layout) -> an::Status {
                                            const an::Result<an::NarrowbandColorMapResult> ran = an::mapNarrowbandColor(
                                                execution, working, alone, samples, layout, alongRequest);
                                            return ran.hasValue() ? an::Status::success() : ran.status();
                                        });
        if (!colourized.hasValue())
        {
            return an::Status::failure(colourized.status().code(),
                                       fmt::format("colourizing the layer '{}' alone: {}",
                                                   params.layers[index].imageName, colourized.status().message()));
        }
        m_stagedOutputs.stage(process_utils::StagedOutput{
            .asset = std::move(*colourized),
            .name =
                QString::fromStdString(params.outputBaseName + " - " + params.layers[index].imageName + " colorized"),
            .history = "NB Color Mapper colorized layer"});
    }
    return an::Status::success();
}

an::ui::ProcessStep NBColorMapperProcess::describeTypedProcessStep(const NBColorMapperProcessParams& params) const
{
    std::size_t included = 0u;
    for (const auto& layer : params.layers)
    {
        included += layer.layer.included ? 1u : 0u;
    }
    return an::ui::ProcessStep{.name = getProcessName(),
                               .description =
                                   fmt::format("Created NB Color Mapper combination from {} layers, {} included{}{}",
                                               params.layers.size(), included,
                                               params.request.background.enabled ? ", background neutralized" : "",
                                               params.applyStretchToOutput ? ", stretched" : "")};
}

// Every layer's image: other views' images the body reads, leased before the run is admitted.
std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
NBColorMapperProcess::typedImagesRead(const NBColorMapperProcessParams& params) const
{
    std::vector<std::shared_ptr<const an::consumer::ImageAsset>> images;
    for (const NBColorMapperProcessParams::Layer& layer : params.layers)
    {
        images.push_back(layer.image);
    }
    return images;
}

void NBColorMapperProcess::getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd,
                                                       bool& executeInBackground, std::string& newImageName)
{
    createNewImage = true;
    closeAtEnd = false;
    executeInBackground = true;
    newImageName = defaultOutputBaseName() + " - NB Color Mapper";
}

void NBColorMapperProcess::resetSettings()
{
    ImageRTPreviewControl::resetSettings();
    m_ui.layerInclude->setChecked(true);
    m_ui.layerHue->setValue(0.0);
    m_ui.layerSaturation->setValue(0.5);
    m_ui.layerMidtones->setValue(0.5);
    m_ui.layerBlackpoint->setValue(0.0);
    m_ui.modificationMode->setCurrentIndex(kModificationNone);
    m_ui.modificationFactor->setValue(1.0);
    m_ui.modificationInvert->setChecked(false);
    m_ui.modificationMedianOffset->setChecked(false);
    m_ui.backgroundEnabled->setChecked(false);
    m_ui.backgroundRescaleHigh->setChecked(false);
    m_ui.backgroundLowerSigma->setValue(-2.8);
    m_ui.backgroundUpperSigma->setValue(2.0);
    m_ui.applyPreviewStf->setChecked(true);
    m_ui.stfTargetBackground->setValue(0.25);
    m_ui.returnColorizedLayers->setChecked(false);
    m_ui.applyStfToCombinedOutput->setChecked(false);
    refreshLayerColorPicker();
    refreshModificationLayerSelector();
    updateModificationControlsEnabled();
}

void NBColorMapperProcess::settingsUpdatedImpl()
{
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        const bool hasStillImage = m_image_view_control && !m_image_view_control->isSequence();
        applyButton->setEnabled(hasStillImage &&
                                an::ui::processes::nbcolormapper_detail::combinationReady(getParams()));
    }
}

void NBColorMapperProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.controlsScrollArea->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Apply)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    if (enabled)
    {
        updateModificationControlsEnabled();
        settingsUpdatedImpl();
    }
}

NBColorMapperProcessParams NBColorMapperProcess::getParams() const
{
    NBColorMapperProcessParams params;
    params.applyPreviewStretch = m_ui.applyPreviewStf->isChecked();
    params.request.targetBackground = m_ui.stfTargetBackground->value();
    params.request.background.enabled = m_ui.backgroundEnabled->isChecked();
    params.request.background.rescaleHighlights = m_ui.backgroundRescaleHigh->isChecked();
    params.request.background.lowerSigmas = m_ui.backgroundLowerSigma->value();
    params.request.background.upperSigmas = m_ui.backgroundUpperSigma->value();
    params.returnColorizedLayers = m_ui.returnColorizedLayers->isChecked();
    params.applyStretchToOutput = m_ui.applyStfToCombinedOutput->isChecked();
    params.previewArea = previewArea();

    std::vector<NBColorMapperProcessLayerState> layerStates = m_layers;
    const int editedRow = m_ui.layersTable ? m_ui.layersTable->currentRow() : -1;
    if (!m_loadingLayerEditor && editedRow >= 0 && editedRow < static_cast<int>(layerStates.size()))
    {
        layerStates[static_cast<std::size_t>(editedRow)] = layerFromEditor();
    }
    params.outputBaseName = outputBaseNameForLayers(layerStates, m_image_view_control);
    params.layers.reserve(layerStates.size());
    for (const auto& state : layerStates)
    {
        NBColorMapperProcessParams::Layer layer;
        layer.imageName = state.imageName;
        layer.layer = state.layer;
        if (auto* control = process_utils::findViewControlByName(QString::fromStdString(state.imageName));
            control != nullptr && control->hasImage())
        {
            layer.image = control->document().asset();
        }
        params.layers.push_back(std::move(layer));
    }
    return params;
}

bool NBColorMapperProcess::isViewControlSupported(const ImageViewControl* ctrl)
{
    return supportedLayerView(ctrl);
}

bool NBColorMapperProcess::isLayerViewSupported(const ImageViewControl* ctrl) const
{
    if (!supportedLayerView(ctrl))
    {
        return false;
    }
    if (m_layers.empty())
    {
        return true;
    }
    auto* referenceControl = process_utils::findViewControlByName(QString::fromStdString(m_layers.front().imageName));
    if (referenceControl == nullptr || !referenceControl->hasImage())
    {
        return true;
    }
    const std::optional<an::ImageDescriptor> candidate = ctrl->document().asset()->descriptor();
    const std::optional<an::ImageDescriptor> reference = referenceControl->document().asset()->descriptor();
    return candidate.has_value() && reference.has_value() && process_utils::sameSize(*candidate, *reference);
}

void NBColorMapperProcess::rebuildImageSelectors(const QString& preferredLayer)
{
    const QSignalBlocker layerBlocker(m_ui.layerImageSelector);
    m_ui.layerImageSelector->clear();
    addNoImageItem(m_ui.layerImageSelector);
    for (auto* control : process_utils::listViewControls())
    {
        if (isLayerViewSupported(control))
        {
            addImageItem(m_ui.layerImageSelector, control);
        }
    }
    restoreSelection(m_ui.layerImageSelector, preferredLayer);
}

void NBColorMapperProcess::refreshLayerTable()
{
    {
        process_utils::TableBatchUpdateGuard guard(m_ui.layersTable);
        const int selectedRow = m_ui.layersTable->currentRow();
        m_ui.layersTable->setRowCount(static_cast<int>(m_layers.size()));
        for (int row = 0; row < static_cast<int>(m_layers.size()); ++row)
        {
            const auto& layer = m_layers[static_cast<std::size_t>(row)];
            setTableText(m_ui.layersTable, row, 0, layer.layer.included ? QStringLiteral("Yes") : QStringLiteral("No"));
            setTableText(m_ui.layersTable, row, 1, QStringLiteral("#%1").arg(row + 1));
            setTableText(m_ui.layersTable, row, 2, QString::fromStdString(layer.imageName));
            setTableText(m_ui.layersTable, row, 3, colorSummary(layer));
            setTableText(m_ui.layersTable, row, 4, modificationSummary(layer.layer));
        }
        process_utils::applyStandardFileTableColumnWidths(m_ui.layersTable, 2);
        if (!m_layers.empty())
        {
            const int rowToSelect =
                selectedRow >= 0 && selectedRow < static_cast<int>(m_layers.size()) ? selectedRow : 0;
            m_ui.layersTable->selectRow(rowToSelect);
        }
    }
    refreshLayerEditorFromSelection();
}

void NBColorMapperProcess::refreshLayerTableRow(const int row)
{
    if (!m_ui.layersTable || row < 0 || row >= static_cast<int>(m_layers.size()))
    {
        return;
    }
    const auto& layer = m_layers[static_cast<std::size_t>(row)];
    setTableText(m_ui.layersTable, row, 0, layer.layer.included ? QStringLiteral("Yes") : QStringLiteral("No"));
    setTableText(m_ui.layersTable, row, 1, QStringLiteral("#%1").arg(row + 1));
    setTableText(m_ui.layersTable, row, 2, QString::fromStdString(layer.imageName));
    setTableText(m_ui.layersTable, row, 3, colorSummary(layer));
    setTableText(m_ui.layersTable, row, 4, modificationSummary(layer.layer));
    process_utils::applyStandardFileTableColumnWidths(m_ui.layersTable, 2);
}

void NBColorMapperProcess::syncSelectedLayerFromEditor()
{
    const int row = m_ui.layersTable ? m_ui.layersTable->currentRow() : -1;
    if (row < 0 || row >= static_cast<int>(m_layers.size()))
    {
        return;
    }
    m_layers[static_cast<std::size_t>(row)] = layerFromEditor();
    refreshLayerTableRow(row);
}

void NBColorMapperProcess::refreshLayerEditorFromSelection()
{
    const int row = m_ui.layersTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_layers.size()))
    {
        refreshModificationLayerSelector();
        updateModificationControlsEnabled();
        return;
    }
    const auto& state = m_layers[static_cast<std::size_t>(row)];
    m_loadingLayerEditor = true;
    restoreSelection(m_ui.layerImageSelector, QString::fromStdString(state.imageName));
    m_ui.layerInclude->setChecked(state.layer.included);
    m_ui.layerHue->setValue(state.layer.hueDegrees);
    m_ui.layerSaturation->setValue(state.layer.saturation);
    m_ui.layerMidtones->setValue(state.layer.midtones);
    m_ui.layerBlackpoint->setValue(state.layer.blackPoint);
    m_ui.modificationMode->setCurrentIndex(modificationModeToIndex(state.layer.modification));
    refreshModificationLayerSelector();
    const int modifierIndex = m_ui.modificationLayerSelector->findData(
        state.layer.modificationSource.has_value() ? static_cast<int>(*state.layer.modificationSource) : -1);
    m_ui.modificationLayerSelector->setCurrentIndex(modifierIndex >= 0 ? modifierIndex : 0);
    m_ui.modificationFactor->setValue(state.layer.modificationFactor);
    m_ui.modificationInvert->setChecked(state.layer.invertModification);
    m_ui.modificationMedianOffset->setChecked(state.layer.subtractModificationMedian);
    m_loadingLayerEditor = false;
    refreshLayerColorPicker();
    updateModificationControlsEnabled();
}

void NBColorMapperProcess::refreshLayerColorPicker()
{
    if (m_ui.layerColorPicker)
    {
        m_ui.layerColorPicker->setLayerColor(m_ui.layerHue->value(), m_ui.layerSaturation->value());
    }
}

void NBColorMapperProcess::refreshModificationLayerSelector()
{
    const QSignalBlocker blocker(m_ui.modificationLayerSelector);
    const QVariant previous = m_ui.modificationLayerSelector->currentData();
    const int currentRow = m_ui.layersTable ? m_ui.layersTable->currentRow() : -1;
    m_ui.modificationLayerSelector->clear();
    m_ui.modificationLayerSelector->addItem(QStringLiteral("None"), -1);
    for (int row = 0; row < static_cast<int>(m_layers.size()); ++row)
    {
        if (row == currentRow)
        {
            continue;
        }
        const QString label =
            QStringLiteral("#%1 %2").arg(row + 1).arg(QString::fromStdString(m_layers[row].imageName));
        m_ui.modificationLayerSelector->addItem(label, row);
    }
    const int restored = m_ui.modificationLayerSelector->findData(previous);
    m_ui.modificationLayerSelector->setCurrentIndex(restored >= 0 ? restored : 0);
}

void NBColorMapperProcess::updateModificationControlsEnabled()
{
    const bool canUseMonoLayerControls = selectedLayerUsesMonoColorControls();
    m_ui.colorControlsGroup->setEnabled(canUseMonoLayerControls);
    m_ui.modificationMode->setEnabled(canUseMonoLayerControls);
    const bool enabled = canUseMonoLayerControls && m_ui.modificationMode->currentIndex() != kModificationNone;
    m_ui.modificationLayerSelector->setEnabled(enabled);
    m_ui.modificationFactor->setEnabled(enabled);
    m_ui.modificationInvert->setEnabled(enabled);
    m_ui.modificationMedianOffset->setEnabled(enabled);
}

void NBColorMapperProcess::autoAddActiveLayer()
{
    if (!m_image_view_control || !isViewControlSupported(m_image_view_control))
    {
        return;
    }
    const std::string activeName = m_image_view_control->getUniqueName();
    for (const auto& layer : m_layers)
    {
        if (layer.imageName == activeName)
        {
            return;
        }
    }
    m_layers.push_back(defaultLayerForImageName(activeName));
}

void NBColorMapperProcess::addLayerFromEditor()
{
    auto layer = layerFromEditor();
    if (layer.imageName.empty() || QString::fromStdString(layer.imageName) == NO_IMAGE_STR)
    {
        return;
    }
    m_layers.push_back(std::move(layer));
    refreshLayerTable();
    m_ui.layersTable->selectRow(static_cast<int>(m_layers.size()) - 1);
    settingsUpdated();
}

void NBColorMapperProcess::removeSelectedLayer()
{
    const int row = m_ui.layersTable->currentRow();
    if (row < 0 || row >= static_cast<int>(m_layers.size()))
    {
        return;
    }
    m_layers.erase(m_layers.begin() + row);
    for (auto& layer : m_layers)
    {
        if (!layer.layer.modificationSource.has_value())
        {
            continue;
        }
        const int source = static_cast<int>(*layer.layer.modificationSource);
        if (source == row)
        {
            layer.layer.modificationSource.reset();
            layer.layer.modification = an::NarrowbandModificationMode::None;
        }
        else if (source > row)
        {
            layer.layer.modificationSource = static_cast<std::uint32_t>(source - 1);
        }
    }
    refreshLayerTable();
    settingsUpdated();
}

NBColorMapperProcessLayerState NBColorMapperProcess::layerFromEditor() const
{
    NBColorMapperProcessLayerState state;
    state.imageName = m_ui.layerImageSelector->currentText().toStdString();
    state.layer.included = m_ui.layerInclude->isChecked();
    state.layer.hueDegrees = m_ui.layerHue->value();
    state.layer.saturation = m_ui.layerSaturation->value();
    state.layer.midtones = m_ui.layerMidtones->value();
    state.layer.blackPoint = m_ui.layerBlackpoint->value();
    state.layer.modification = modificationModeFromIndex(m_ui.modificationMode->currentIndex());
    const int source = m_ui.modificationLayerSelector->currentData().toInt();
    if (state.layer.modification != an::NarrowbandModificationMode::None && source >= 0)
    {
        state.layer.modificationSource = static_cast<std::uint32_t>(source);
    }
    else
    {
        state.layer.modificationSource.reset();
        if (source < 0)
        {
            state.layer.modification = an::NarrowbandModificationMode::None;
        }
    }
    state.layer.modificationFactor = m_ui.modificationFactor->value();
    state.layer.invertModification = m_ui.modificationInvert->isChecked();
    state.layer.subtractModificationMedian = m_ui.modificationMedianOffset->isChecked();
    return state;
}

NBColorMapperProcessLayerState NBColorMapperProcess::defaultLayerForImageName(const std::string& imageName) const
{
    NBColorMapperProcessLayerState state;
    state.imageName = imageName;
    state.layer = an::NarrowbandColorLayer{};
    return state;
}

std::string NBColorMapperProcess::defaultOutputBaseName() const
{
    return outputBaseNameForLayers(m_layers, m_image_view_control);
}

bool NBColorMapperProcess::selectedLayerUsesMonoColorControls() const
{
    if (!m_ui.layerImageSelector)
    {
        return false;
    }
    const QString imageName = m_ui.layerImageSelector->currentText();
    if (imageName.isEmpty() || imageName == NO_IMAGE_STR)
    {
        return false;
    }
    auto* control = process_utils::findViewControlByName(imageName);
    if (control == nullptr || !control->hasImage() || process_utils::holdsColourImage(control))
    {
        return false;
    }
    const int currentRow = m_ui.layersTable ? m_ui.layersTable->currentRow() : -1;
    if (currentRow < 0)
    {
        return true;
    }
    for (int row = 0; row < static_cast<int>(m_layers.size()); ++row)
    {
        const auto& layer = m_layers[static_cast<std::size_t>(row)].layer;
        if (row != currentRow && layer.modification != an::NarrowbandModificationMode::None &&
            layer.modificationSource.has_value() && static_cast<int>(*layer.modificationSource) == currentRow)
        {
            return false;
        }
    }
    return true;
}
