// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// LRGB Combination: a colour image's chrominance combined with the lightness of a separate luminance image - the
/// luminance and the chroma each optionally stretched by a midtones transfer, the lightness replaced or blended by a
/// weight, and each recomposed channel weighed - one engine composition, `combineLrgb`.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/QSliderSpinBox.h"
#include "controls/TypedProcessControl.h"
#include "processes/LRGBCombinationProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QRect>
#include <QString>

#include <memory>
#include <string>

class ImageViewControl;

/// What one run of the combination reads: the luminance image the window snapshotted when the run was asked for,
/// the weights and the midtones balances.
struct LRGBCombinationParams
{
    /// The luminance image, taken from its view on the GUI thread; null when none is selected.
    std::shared_ptr<const an::consumer::ImageAsset> luminance;

    QString luminanceImageName;

    double lightnessWeight = 1.0;
    double redWeight = 1.0;
    double greenWeight = 1.0;
    double blueWeight = 1.0;

    double lightnessMTF = 0.5;
    double saturationMTF = 0.5;

    /// The preview's area in the target's pixels, which the luminance image is cropped to as the framework crops the
    /// target; empty for the whole image.
    QRect previewArea;
};

/// LRGB Combination: combines a colour target with a grey luminance image of its size through the engine's
/// `combineLrgb`, in the target's representation, and records the result as a combination of the two images.
class AFTERNIGHTPROCESSES_API LRGBCombinationProcess : public an::ui::TypedProcessControl<LRGBCombinationParams>
{
public:
    explicit LRGBCombinationProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    ~LRGBCombinationProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const LRGBCombinationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const LRGBCombinationParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const LRGBCombinationParams& params) const override;

private:
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void on_targetImageSelector_currentTextChangedInternal(const QString& str) override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void refreshTheme() override;
    [[nodiscard]] LRGBCombinationParams getParams() const override;

    /// A colour image: the target.
    [[nodiscard]] bool isViewControlSupported(const ImageViewControl* ctrl) override;

    /// A grey image of the target's size: a luminance.
    [[nodiscard]] bool isLuminanceViewSupported(const ImageViewControl* ctrl) const;

    void loadImage2List();

    QSliderDoubleSpinBoxUPtr lightnessWeightControl;
    QSliderDoubleSpinBoxUPtr redWeightControl;
    QSliderDoubleSpinBoxUPtr greenWeightControl;
    QSliderDoubleSpinBoxUPtr blueWeightControl;
    QSliderDoubleSpinBoxUPtr lightnessMTFControl;
    QSliderDoubleSpinBoxUPtr saturationMTFControl;

    bool m_ignoreLuminanceSelector = false;

    an::ui::processes::LRGBCombinationProcessUiElements m_ui;
};
