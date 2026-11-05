// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Image Blend: the target laid under a second image by one of the product's twenty-seven blend modes, each image
/// first stretched by a midtones transfer, the blend image blurred, high-passed or colourized, the target's lightness
/// optionally kept, and the composite laid over the target at an opacity - one engine composition, `composeBlend`.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Pointwise/BlendComposition.h>

#include <QRect>
#include <QString>

#include <memory>
#include <optional>
#include <string>

class ImageViewControl;

/// What one run of the blend reads: the mode, the transfers, the filter, the opacity, and the blend image the window
/// snapshotted when the run was asked for.
class ImageBlendParams
{
public:
    /// The mode's label, as the selector shows it.
    QString blendMode;

    /// The mode the label names.
    an::BlendCompositionMode mode = an::BlendCompositionMode::Replace;

    QString targetImageName;
    QString secondImageName;

    /// The blend image, taken from its view on the GUI thread; null when none is selected.
    std::shared_ptr<const an::consumer::ImageAsset> second;

    /// The filter's label, as the selector shows it.
    QString filterType;

    /// The filter the label names.
    an::BlendPreFilter filter = an::BlendPreFilter::None;

    bool preserveLightness = false;

    /// Laid over the target by the composition itself, so the framework's modulation stays one.
    double opacity = 1.0;

    double srcImageBlackPoint = 0.0;
    double srcImageHighlights = 1.0;
    double srcImageMidtones = 0.5;
    double blendImageBlackPoint = 0.0;
    double blendImageHighlights = 1.0;
    double blendImageMidtones = 0.5;

    double filterRadius = 10.0;
    double filterHue = 0.0;
    double filterSaturation = 1.0;

    /// The preview's area in the target's pixels, which the blend image is cropped to as the framework crops the
    /// target; empty for the whole image.
    QRect previewArea;
};

namespace an::ui::processes::image_blend_detail
{

/// The mode a selector label names, or nothing for a label the menu does not have.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::BlendCompositionMode> blendModeFromLabel(const QString& label);

/// The filter a selector label names, or nothing for a label the menu does not have.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::BlendPreFilter> blendFilterFromLabel(const QString& label);

} // namespace an::ui::processes::image_blend_detail

/// Image Blend: blends a second image of the target's size over the target through the engine's `composeBlend`, in
/// the target's representation, and records the result as a combination of the two images.
class AFTERNIGHTPROCESSES_API ImageBlendProcess : public an::ui::TypedProcessControl<ImageBlendParams>
{
public:
    explicit ImageBlendProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void refreshTheme() override;

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    ~ImageBlendProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ImageBlendParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const ImageBlendParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const ImageBlendParams& params) const override;

private:
    void on_targetImageSelector_currentTextChangedInternal(const QString& str) override;
    void on_secondImageSelector_currentTextChanged(const QString& str);
    void on_blendMode_currentTextChanged(const QString& str);
    void on_filterType_currentTextChanged(const QString& str);
    void on_preserve_lightness_clicked(Qt::CheckState state);

    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void settingsUpdatedImpl() override;
    void resetSettings() override;
    void previewControlStarted() override;

    [[nodiscard]] bool isViewControlSupportedCustom(const ImageViewControl* ctrl) const;
    void loadImage2List();
    void initialize();
    void validateParameters();
    void enableBlendParameters(bool enabled);
    void enableSlidersTracking(bool enabled);

    [[nodiscard]] ImageBlendParams getParams() const override;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
