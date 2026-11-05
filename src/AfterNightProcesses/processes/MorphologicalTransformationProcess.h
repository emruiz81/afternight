// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include "processes/MorphologicalTransformationProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "widgets/StructuredElementWidget.h"
#include "widgets/StructuringElementGrid.h"
#include "controls/QSliderSpinBox.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ImageViewControl;

/// The operators the dialog offers, in the order its selector lists them.
enum class MorphologyOperator : int
{
    Erosion = 0,
    Dilation = 1,
    Opening = 2,
    Closing = 3,
    Selection = 4
};

struct MorphologicalTransformationParams
{
    int iterations = 0;
    int _operator = 0;
    double amount = 1.0;

    /// Morphological Selection's rank among the samples the element covers, as a fraction.
    double selection = 0.5;

    /// The structuring element's side and its cells, row by row, one byte each, one for on.
    std::uint32_t elementSize = 0u;
    std::vector<std::uint8_t> elementCells;

    std::string operatorText;
};

/// Erodes, dilates, opens or closes the image, or the channel the selector names, by the dialog's structuring element,
/// or replaces every sample by a rank of the samples the element covers.
///
/// Every operator leaves out a neighbour past the image's edge, and repeats its primitives the dialog's number of
/// times: an opening is that many erosions, then as many dilations.
class AFTERNIGHTPROCESSES_API MorphologicalTransformationProcess
    : public an::ui::TypedProcessControl<MorphologicalTransformationParams>,
      public StructuredElementWidgetListener
{
public:
    MorphologicalTransformationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onGridUpdated() override;

    void refreshTheme() override;

    ~MorphologicalTransformationProcess();

    /// A disc of a side: the cells within the side's half and a half of the centre.
    [[nodiscard]] static an::ui::StructuringElementGrid circleElement(std::size_t size);

    /// A diamond of a side: the cells whose distance from the centre along the rows and columns is at most half the
    /// side.
    [[nodiscard]] static an::ui::StructuringElementGrid diamondElement(std::size_t size);

    /// A cross of a side: the centre's row and column.
    [[nodiscard]] static an::ui::StructuringElementGrid crossElement(std::size_t size);

    /// The two diagonals of a side.
    [[nodiscard]] static an::ui::StructuringElementGrid diagonalElement(std::size_t size);

private:
    void on_amountChanged(double value);

    void on_iterationsValueChanged(int value);

    void on_selectionChanged(double value);

    void on_operatorSelector_currentTextChanged(const QString& str);

    void on_seSelector_currentTextChanged(const QString& str);

    void on_seClearButton();

    void on_seFillButton();

    void on_seInvertButton();

    void on_seCircularButton();

    void on_seDiamondButton();

    void on_seCrossButton();

    void on_seDiagonalButton();

    /// Shows a new element and asks for a preview of it.
    void showElement(const an::ui::StructuringElementGrid& element);

private:
    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    an::Status executeTypedProcessIteration(const an::ChannelScopeInput& input, const an::ChannelScopeOutput& output,
                                            const an::ui::ProcessRun& run,
                                            const MorphologicalTransformationParams& params,
                                            an::ui::ExecutionScope& scope) override;
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const MorphologicalTransformationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const MorphologicalTransformationParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t
    typedPreviewRegionMargin(const MorphologicalTransformationParams& params) const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void initialize();
    void resetSettings() override;
    [[nodiscard]] MorphologicalTransformationParams getParams() const override;
    double getTypedModulationFromParams(const MorphologicalTransformationParams& params) override;

    QSliderDoubleSpinBoxUPtr amountControl;
    QSliderDoubleSpinBoxUPtr selectionControl;

    std::unique_ptr<StructuredElementWidget> m_morph_struct_element;

    an::ui::processes::MorphologicalTransformationProcessUiElements m_ui;
};
