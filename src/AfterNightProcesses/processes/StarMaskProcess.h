// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"
#include "processes/StarMaskProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/QSliderSpinBox.h"

#include <AfterNightEngine/Algorithms/Stars/StarDetection.h>
#include <AfterNightEngine/Algorithms/Stars/StarMask.h>
#include <AfterNightEngine/Core/Result.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>

#include <string>

class ImageViewControl;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QSpinBox;

struct StarMaskParams
{
    /// How the stars are found: the dialog's detection values, in units where full scale is one.
    an::StarDetectionRequest detection{};

    /// Whether a colour image's stars are found in its CIE lightness rather than in its green channel.
    bool useLightness = false;

    /// Whether each star is painted as a shaped falloff; a solid ellipse when not.
    bool shapedAreas = true;

    /// The falloff, the size window and the growth a shaped star is painted with.
    an::StarMaskShapeRequest shape{};

    /// The shape selector's entry, which the history step names.
    std::string shapeName;
};

namespace an::ui::processes::star_mask_detail
{

/// The falloff a shape selector's entry names, in the selector's order.
///
/// @param index the entry's index.
/// @return the falloff; the truncated bell for an index that names none.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::StarMaskShape shapeFromIndex(int index) noexcept;

/// The description a star search reads its samples under: the image's, restated in a working representation and in
/// the normalized linear domain.
///
/// @param described what the image is.
/// @param working the working representation.
/// @return the description; or why the image cannot be described so.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::ImageDescriptor>
detectionDescriptor(const an::ImageDescriptor& described, an::SampleType working);

} // namespace an::ui::processes::star_mask_detail

/// Star Mask: finds an image's stars with the engine's fitted detector and paints them into a new grey mask - each star
/// a shaped falloff, or a solid ellipse - through the engine's `detectStars`, `paintShapedStarMask` and
/// `paintEllipticalStarMask`. The mask is a new image in the target's representation.
class AFTERNIGHTPROCESSES_API StarMaskProcess : public an::ui::TypedProcessControl<StarMaskParams>
{
public:
    StarMaskProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    ~StarMaskProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const StarMaskParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const StarMaskParams& params) const override;

private:
    void buildDetectionSettingsUi();
    void updateDetectionSettingsState();

    void refreshTheme() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;

    void resetSettings() override;

    ProgressDialogControl* getProgressDialogControl(bool forSequence) override;
    [[nodiscard]] StarMaskParams getParams() const override;

    QSliderDoubleSpinBoxUPtr percentIncreaseControl;
    QSliderSpinBoxUPtr pixelIncreaseControl;
    QSliderSpinBoxUPtr minSizeControl;
    QSliderSpinBoxUPtr maxSizeControl;
    QSliderSpinBoxUPtr radiusControl;
    QSliderDoubleSpinBoxUPtr thresholdControl;
    QSliderDoubleSpinBoxUPtr amplitudeMinimumControl;
    QSliderDoubleSpinBoxUPtr amplitudeMaximumControl;
    QSliderDoubleSpinBoxUPtr roundnessControl;
    QSliderDoubleSpinBoxUPtr roundnessMaximumControl;

    QGroupBox* m_detectionGroup = nullptr;
    QGroupBox* m_amplitudeRangeGroup = nullptr;
    QGroupBox* m_roundnessRangeGroup = nullptr;
    QGroupBox* m_shapedAreasGroup = nullptr;
    QSpinBox* m_convergenceValue = nullptr;
    QCheckBox* m_relaxChecks = nullptr;
    QComboBox* m_profileTypeSelector = nullptr;
    QDoubleSpinBox* m_minimumBetaValue = nullptr;
    QCheckBox* m_useLightness = nullptr;

    an::ui::processes::StarMaskProcessUiElements m_ui;
};
