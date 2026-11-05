// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "MorphologicalTransformationProcess.h"
#include "controls/ImageViewControl.h"
#include "processes/ProcessCommonUtils.h"
#include "processes/SpatialProcessSupport.h"
#include "controls/ProcessButtonIcons.h"

#include <AfterNightEngine/Algorithms/Spatial/GeneralFilter.h>
#include <AfterNightEngine/Algorithms/Spatial/OrderFilter.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/ImageStorage.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{

/// The side of the element each entry of the size selector names, in its order.
constexpr std::array<std::size_t, 12u> kElementSides{3u, 5u, 7u, 9u, 11u, 13u, 15u, 17u, 19u, 21u, 23u, 25u};

/// One engine call of an operator's chain: an erosion, a dilation or a rank, repeated up to the engine's limit.
struct ChainStep
{
    MorphologyOperator primitive = MorphologyOperator::Erosion;
    std::uint32_t repeats = 1u;
};

/// The engine calls an operator is made of: each primitive repeated the dialog's number of times, split where the
/// count is more than one call may repeat.
[[nodiscard]] std::vector<ChainStep> chainOf(MorphologyOperator op, std::uint32_t iterations)
{
    std::vector<MorphologyOperator> primitives;
    switch (op)
    {
    case MorphologyOperator::Dilation:
        primitives = {MorphologyOperator::Dilation};
        break;
    case MorphologyOperator::Opening:
        primitives = {MorphologyOperator::Erosion, MorphologyOperator::Dilation};
        break;
    case MorphologyOperator::Closing:
        primitives = {MorphologyOperator::Dilation, MorphologyOperator::Erosion};
        break;
    case MorphologyOperator::Selection:
        primitives = {MorphologyOperator::Selection};
        break;
    case MorphologyOperator::Erosion:
    default:
        primitives = {MorphologyOperator::Erosion};
        break;
    }
    const std::uint32_t limit = (std::min)(an::kMaxMorphologyIterations, an::kMaxOrderFilterIterations);
    std::vector<ChainStep> chain;
    for (const MorphologyOperator primitive : primitives)
    {
        for (std::uint32_t left = iterations; left > 0u;)
        {
            const std::uint32_t repeats = (std::min)(left, limit);
            chain.push_back(ChainStep{.primitive = primitive, .repeats = repeats});
            left -= repeats;
        }
    }
    return chain;
}

/// One call of a chain, from one image to another of the same layout.
[[nodiscard]] an::Status runStep(const an::ExecutionContext& execution, an::SampleType type, const void* source,
                                 const an::ImageLayout& sourceLayout, void* destination,
                                 const an::ImageLayout& destinationLayout, const ChainStep& step, std::uint32_t radius,
                                 std::span<const std::uint8_t> footprint, double selection,
                                 const an::SpatialSelection& validity)
{
    if (step.primitive == MorphologyOperator::Selection)
    {
        return process_utils::statusOf(an::filterImageFootprintOrderStatistic(
            execution, type, source, sourceLayout, destination, destinationLayout,
            an::FootprintOrderFilterRequest{.radiusX = radius,
                                            .radiusY = radius,
                                            .footprint = footprint,
                                            .quantile = selection,
                                            .border = an::KernelBorderMode::NotApplicable,
                                            .iterations = step.repeats},
            validity));
    }
    // A neighbour past the edge, or one the image says means nothing, reads as the value neither a minimum nor a
    // maximum is moved by: left out.
    const bool erode = step.primitive == MorphologyOperator::Erosion;
    const an::Result<an::MorphologyPlan> plan = an::planMorphology(an::MorphologyRequest{
        .operation = erode ? an::MorphologyOperation::Erode : an::MorphologyOperation::Dilate,
        .shape = an::MorphologyShape::Custom,
        .radiusX = radius,
        .radiusY = radius,
        .iterations = step.repeats,
        .border = an::KernelBorderMode::Constant,
        .constantBorder = erode ? (std::numeric_limits<float>::max)() : -(std::numeric_limits<float>::max)(),
        .customFootprint = footprint});
    if (!plan.hasValue())
    {
        return plan.status();
    }
    return process_utils::statusOf(an::filterImageMorphology(execution, type, source, sourceLayout, destination,
                                                             destinationLayout, *plan, validity));
}

} // namespace

MorphologicalTransformationProcess::MorphologicalTransformationProcess(ImageViewControl* control)
    : an::ui::TypedProcessControl<MorphologicalTransformationParams>(control, getProcessNameStatic())
{
    m_ui = an::ui::processes::buildMorphologicalTransformationProcessUi(this);

    setProcessDescription(QStringLiteral("Apply morphological operations (Erosion, Dilation, Opening, Closing, "
                                         "Selection) to structural elements of the image."));

    initializeStandardProcess(m_ui.horizontalLayoutMain, m_ui.previewLayout, m_ui.preview, m_ui.buttonBox,
                              m_ui.targetImageSelector, m_ui.channelSelector, true, true);

    initializeTargetViewControlComboBox(m_ui.targetImageSelector);

    QObject::connect(m_ui.iterationsValue, &QSpinBox::valueChanged, this,
                     &MorphologicalTransformationProcess::on_iterationsValueChanged);
    QObject::connect(m_ui.operatorSelector, &QComboBox::currentTextChanged, this,
                     &MorphologicalTransformationProcess::on_operatorSelector_currentTextChanged);
    QObject::connect(m_ui.seSizeSelector, &QComboBox::currentTextChanged, this,
                     &MorphologicalTransformationProcess::on_seSelector_currentTextChanged);
    QObject::connect(m_ui.seClearButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seClearButton);
    QObject::connect(m_ui.seFillButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seFillButton);
    QObject::connect(m_ui.seInvertButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seInvertButton);
    QObject::connect(m_ui.seCircularButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seCircularButton);
    QObject::connect(m_ui.seDiamondButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seDiamondButton);
    QObject::connect(m_ui.seCrossButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seCrossButton);
    QObject::connect(m_ui.seDiagonalButton, &QPushButton::clicked, this,
                     &MorphologicalTransformationProcess::on_seDiagonalButton);

    INIT_D_SPINBOX_CTRL(amountControl, m_ui.amountValue, m_ui.amountSlider,
                        &MorphologicalTransformationProcess::on_amountChanged);
    INIT_D_SPINBOX_CTRL(selectionControl, m_ui.selectionValue, m_ui.selectionSlider,
                        &MorphologicalTransformationProcess::on_selectionChanged);

    updateWindowTitle();

    m_morph_struct_element = std::make_unique<StructuredElementWidget>(m_ui.seHost);
    m_morph_struct_element->resize(170, 170);
    m_morph_struct_element->setListener(this);

    an::ui::StructuringElementGrid element(3u);
    element.fill(true);
    m_morph_struct_element->setStructuringElement(element);

    initialize();

    setExecutionAutostretchSupported(true);
    setupPreviewControl(false);
    settingsUpdated();

    refreshTheme();
}

void MorphologicalTransformationProcess::refreshTheme()
{
    ImageProcessControl::refreshTheme();
    if (const auto iconProvider = process_utils::currentIconProvider())
    {
        process_utils::applyDialogButtonBoxIcons(m_ui.buttonBox, iconProvider.get());
        m_ui.seClearButton->setIcon(iconProvider->getIcon("delete"));
        m_ui.seFillButton->setIcon(iconProvider->getIcon("color_fill"));
        m_ui.seInvertButton->setIcon(iconProvider->getIcon("contrast"));
        m_ui.seCircularButton->setIcon(iconProvider->getIcon("circle"));
        m_ui.seDiamondButton->setIcon(iconProvider->getIcon("diamond"));
        m_ui.seCrossButton->setIcon(iconProvider->getIcon("add"));
        m_ui.seDiagonalButton->setIcon(iconProvider->getIcon("close"));
    }
}

std::string MorphologicalTransformationProcess::getProcessNameStatic()
{
    return "Morphological Transformation";
}

bool MorphologicalTransformationProcess::requiresExclusiveAccessStatic()
{
    return false;
}

an::ui::StructuringElementGrid MorphologicalTransformationProcess::circleElement(std::size_t size)
{
    an::ui::StructuringElementGrid element(size);
    const auto centre = static_cast<std::int64_t>((size - 1u) / 2u);
    for (std::size_t row = 0u; row < size; ++row)
    {
        for (std::size_t column = 0u; column < size; ++column)
        {
            const std::int64_t down = static_cast<std::int64_t>(row) - centre;
            const std::int64_t across = static_cast<std::int64_t>(column) - centre;
            element.set(row, column, down * down + across * across <= centre * centre + centre);
        }
    }
    return element;
}

an::ui::StructuringElementGrid MorphologicalTransformationProcess::diamondElement(std::size_t size)
{
    an::ui::StructuringElementGrid element(size);
    const auto centre = static_cast<std::int64_t>((size - 1u) / 2u);
    for (std::size_t row = 0u; row < size; ++row)
    {
        for (std::size_t column = 0u; column < size; ++column)
        {
            element.set(row, column,
                        std::llabs(centre - static_cast<std::int64_t>(row)) +
                                std::llabs(centre - static_cast<std::int64_t>(column)) <=
                            centre);
        }
    }
    return element;
}

an::ui::StructuringElementGrid MorphologicalTransformationProcess::crossElement(std::size_t size)
{
    an::ui::StructuringElementGrid element(size);
    const std::size_t centre = (size - 1u) / 2u;
    for (std::size_t index = 0u; index < size; ++index)
    {
        element.set(index, centre, true);
        element.set(centre, index, true);
    }
    return element;
}

an::ui::StructuringElementGrid MorphologicalTransformationProcess::diagonalElement(std::size_t size)
{
    an::ui::StructuringElementGrid element(size);
    for (std::size_t index = 0u; index < size; ++index)
    {
        element.set(index, index, true);
        element.set(size - 1u - index, index, true);
    }
    return element;
}

void MorphologicalTransformationProcess::onGridUpdated()
{
    settingsUpdated();
}

MorphologicalTransformationProcess::~MorphologicalTransformationProcess() {}

void MorphologicalTransformationProcess::on_amountChanged(double value)
{
    settingsUpdated();
}

void MorphologicalTransformationProcess::on_iterationsValueChanged(int value)
{
    settingsUpdated();
}

void MorphologicalTransformationProcess::on_selectionChanged(double value)
{
    settingsUpdated();
}

void MorphologicalTransformationProcess::on_operatorSelector_currentTextChanged(const QString& str)
{
    const bool selection = m_ui.operatorSelector->currentIndex() == static_cast<int>(MorphologyOperator::Selection);
    selectionControl->setEnabled(selection);

    settingsUpdated();
}

void MorphologicalTransformationProcess::showElement(const an::ui::StructuringElementGrid& element)
{
    m_morph_struct_element->setStructuringElement(element);
    settingsUpdated();
}

void MorphologicalTransformationProcess::on_seSelector_currentTextChanged(const QString& str)
{
    const int index = m_ui.seSizeSelector->currentIndex();
    if (index < 0 || index >= static_cast<int>(kElementSides.size()))
    {
        return;
    }
    an::ui::StructuringElementGrid element = m_morph_struct_element->structuringElement();
    element.resize(kElementSides[static_cast<std::size_t>(index)]);
    showElement(element);
}

void MorphologicalTransformationProcess::on_seClearButton()
{
    an::ui::StructuringElementGrid element = m_morph_struct_element->structuringElement();
    element.fill(false);
    showElement(element);
}

void MorphologicalTransformationProcess::on_seFillButton()
{
    an::ui::StructuringElementGrid element = m_morph_struct_element->structuringElement();
    element.fill(true);
    showElement(element);
}

void MorphologicalTransformationProcess::on_seInvertButton()
{
    an::ui::StructuringElementGrid element = m_morph_struct_element->structuringElement();
    element.invert();
    showElement(element);
}

void MorphologicalTransformationProcess::on_seCircularButton()
{
    showElement(circleElement(m_morph_struct_element->structuringElement().size()));
}

void MorphologicalTransformationProcess::on_seDiamondButton()
{
    showElement(diamondElement(m_morph_struct_element->structuringElement().size()));
}

void MorphologicalTransformationProcess::on_seCrossButton()
{
    showElement(crossElement(m_morph_struct_element->structuringElement().size()));
}

void MorphologicalTransformationProcess::on_seDiagonalButton()
{
    showElement(diagonalElement(m_morph_struct_element->structuringElement().size()));
}

an::Status MorphologicalTransformationProcess::executeTypedProcessIteration(
    const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output, const an::ui::ProcessRun& run,
    const MorphologicalTransformationParams& params, an::ui::ExecutionScope& scope)
{
    // An element with no cell on covers nothing: the plane stays as it starts.
    const std::span<const std::uint8_t> footprint(params.elementCells);
    if (params.elementSize == 0u || std::ranges::none_of(footprint, [](std::uint8_t cell) { return cell != 0u; }))
    {
        return an::Status::success();
    }
    const std::uint32_t radius = (params.elementSize - 1u) / 2u;
    const std::vector<ChainStep> chain = chainOf(static_cast<MorphologyOperator>(params._operator),
                                                 static_cast<std::uint32_t>((std::max)(1, params.iterations)));
    const an::SampleType type = input.descriptor->sampleType();
    const process_utils::PlaneSelection selection(input, process_utils::NonFiniteSamples::MeanNothing);
    const bool gatesWrites =
        selection.selection().mask != nullptr || selection.selection().validity.rule() != an::ValidityRule::AllValid;

    // A chain of more than one call alternates between working planes in the output's layout, and the last call writes
    // the output.
    std::array<std::optional<an::ImageStorageBytes>, 2u> between;
    const void* read = input.samples;
    const an::ImageLayout* readLayout = input.layout;
    for (std::size_t index = 0u; index < chain.size(); ++index)
    {
        const bool last = index + 1u == chain.size();
        void* write = output.samples;
        if (!last)
        {
            std::optional<an::ImageStorageBytes>& plane = between[index % 2u];
            if (!plane.has_value())
            {
                an::Result<an::ImageStorageBytes> made = an::ImageStorageBytes::create(
                    scope.engine(), *output.layout, an::StorageInitialization::Uninitialized);
                if (!made.hasValue())
                {
                    return made.status();
                }
                plane = std::move(*made);
            }
            write = plane->data();
            // A centre the image says means nothing is left untouched by every call, so a working plane starts as
            // what it replaces.
            if (gatesWrites)
            {
                an::ConversionCounters counters;
                if (an::Status seeded = an::convertImageSamples(scope.execution(), type, read, *readLayout, type, write,
                                                                *output.layout, an::ConversionPolicy{}, counters);
                    seeded.failed())
                {
                    return seeded;
                }
            }
        }
        // The first call reads the input where it lies; every later one reads a plane in the output's layout.
        if (an::Status ran = runStep(scope.execution(), type, read, *readLayout, write, *output.layout, chain[index],
                                     radius, footprint, params.selection, selection.selection());
            ran.failed())
        {
            return ran;
        }
        read = write;
        readLayout = output.layout;
    }
    return an::Status::success();
}

an::Status MorphologicalTransformationProcess::executeTypedProcessImpl(const an::ui::ProcessSource& source,
                                                                       an::ui::ProcessDestination& destination,
                                                                       const an::ui::ProcessRun& run,
                                                                       const MorphologicalTransformationParams& params,
                                                                       an::ui::ExecutionScope& scope)
{
    return executeTypedProcessUsingChannels(source, destination, run, params, scope);
}

an::ui::PreviewScaling MorphologicalTransformationProcess::previewScaling() const
{
    // The structuring element is cells of the pixel grid, which a reduction would change: an interactive frame runs at
    // full scale over the part of the target the preview view shows.
    return an::ui::PreviewScaling::NativeRegion;
}

std::uint32_t
MorphologicalTransformationProcess::typedPreviewRegionMargin(const MorphologicalTransformationParams& params) const
{
    // Every primitive pass of the operator's chain reads the element's half side around each pixel of the pass before:
    // an opening or a closing makes twice the dialog's iterations of them, every other operator as many.
    if (params.elementSize == 0u)
    {
        return 0u;
    }
    std::uint64_t passes = 0u;
    for (const ChainStep& step : chainOf(static_cast<MorphologyOperator>(params._operator),
                                         static_cast<std::uint32_t>((std::max)(1, params.iterations))))
    {
        passes += step.repeats;
    }
    return static_cast<std::uint32_t>(std::uint64_t{(params.elementSize - 1u) / 2u} * passes);
}

an::ui::ProcessStep
MorphologicalTransformationProcess::describeTypedProcessStep(const MorphologicalTransformationParams& params) const
{
    std::string description = fmt::format("Operator: {}, Iterations: {}, Amount: {:.2f}", params.operatorText,
                                          params.iterations, params.amount);
    if (params._operator == static_cast<int>(MorphologyOperator::Selection))
    {
        description += fmt::format(", Selection: {:.2f}", params.selection);
    }
    description += process_utils::channelStepSuffix(m_image_view_control, m_ui.channelSelector);
    return an::ui::ProcessStep{.name = getProcessName(), .description = std::move(description)};
}

bool MorphologicalTransformationProcess::isPreviewEnabled() const
{
    return m_ui.preview->isChecked();
}

void MorphologicalTransformationProcess::enablePreview(bool enabled)
{
    m_ui.preview->setChecked(enabled);
}

void MorphologicalTransformationProcess::initialize()
{
    m_ui.operatorSelector->clear();
    m_ui.operatorSelector->addItem("Erosion (Minimum)");
    m_ui.operatorSelector->addItem("Dilation (Maximum)");
    m_ui.operatorSelector->addItem("Opening (Dilation + Erosion)");
    m_ui.operatorSelector->addItem("Closing (Erosion + Dilation)");
    m_ui.operatorSelector->addItem("Morphological Selection");

    m_ui.operatorSelector->setCurrentText("Erosion (Minimum)");

    m_ui.iterationsValue->setValue(1);
    amountControl->setValue(1.0);
    selectionControl->setValue(0.5);
    selectionControl->setEnabled(false);

    m_ui.seSizeSelector->clear();
    for (const std::size_t side : kElementSides)
    {
        m_ui.seSizeSelector->addItem(QStringLiteral("%1 (%2 elements)").arg(side).arg(side * side));
    }

    m_ui.seSizeSelector->setCurrentText("3 (9 elements)");
}

void MorphologicalTransformationProcess::resetSettings()
{
    m_ui.iterationsValue->setValue(1);

    amountControl->setValue(1.0);
    selectionControl->setValue(0.5);

    m_ui.operatorSelector->setCurrentText("Erosion (Minimum)");

    m_ui.seSizeSelector->setCurrentText("3 (9 elements)");
    an::ui::StructuringElementGrid element(3u);
    element.fill(true);
    m_morph_struct_element->setStructuringElement(element);

    if (m_ui.preview->isChecked())
    {
        schedulePreviewProcessExecution();
    }
}

void MorphologicalTransformationProcess::settingsUpdatedImpl()
{
    // An element with no cell on has nothing to apply.
    if (auto* applyButton = m_ui.buttonBox->button(QDialogButtonBox::Apply))
    {
        applyButton->setEnabled(m_morph_struct_element && m_morph_struct_element->structuringElement().onCount() > 0u);
    }
}

void MorphologicalTransformationProcess::enableProcessControlsImpl(bool enabled)
{
    m_ui.buttonBox->button(QDialogButtonBox::Apply)
        ->setEnabled(enabled && m_morph_struct_element->structuringElement().onCount() > 0u);
    m_ui.buttonBox->button(QDialogButtonBox::Close)->setEnabled(enabled);
    m_ui.buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enabled);

    m_ui.operatorSelector->setEnabled(enabled);
    m_ui.iterationsValue->setEnabled(enabled);
    m_ui.seSizeSelector->setEnabled(enabled);
    m_morph_struct_element->setEnabled(enabled);
    amountControl->setEnabled(enabled);
    selectionControl->setEnabled(enabled && m_ui.operatorSelector->currentIndex() ==
                                                static_cast<int>(MorphologyOperator::Selection));

    m_ui.seClearButton->setEnabled(enabled);
    m_ui.seFillButton->setEnabled(enabled);
    m_ui.seInvertButton->setEnabled(enabled);

    m_ui.seCircularButton->setEnabled(enabled);
    m_ui.seDiamondButton->setEnabled(enabled);
    m_ui.seCrossButton->setEnabled(enabled);
    m_ui.seDiagonalButton->setEnabled(enabled);
}

double MorphologicalTransformationProcess::getTypedModulationFromParams(const MorphologicalTransformationParams& params)
{
    return params.amount;
}

MorphologicalTransformationParams MorphologicalTransformationProcess::getParams() const
{
    MorphologicalTransformationParams params;
    params.iterations = m_ui.iterationsValue->value();
    params._operator = m_ui.operatorSelector->currentIndex();
    params.amount = amountControl->getValue();
    params.selection = selectionControl->getValue();
    params.operatorText = m_ui.operatorSelector->currentText().toUtf8().toStdString();

    if (m_morph_struct_element)
    {
        const an::ui::StructuringElementGrid& element = m_morph_struct_element->structuringElement();
        params.elementSize = static_cast<std::uint32_t>(element.size());
        params.elementCells = element.cells();
    }

    return params;
}
