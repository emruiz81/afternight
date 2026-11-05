// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "processes/BackgroundExtractionProcessUiBuilder.h"
#include "controls/TypedProcessControl.h"
#include "controls/ImageSampleSelectionController.h"
#include "controls/QSliderSpinBox.h"
#include "processes/MaskProcessUtils.h"

#include <AfterNightEngine/Algorithms/Background/BackgroundModel.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ImageViewControl;

/// Where one background sample sits, in the target's pixels: the centre of its box.
struct BackgroundSamplePosition
{
    std::uint32_t x = 0u;
    std::uint32_t y = 0u;

    friend constexpr bool operator==(const BackgroundSamplePosition& left,
                                     const BackgroundSamplePosition& right) noexcept = default;
};

struct BackgroundExtractionParams
{
    enum class PreviewDisplayMode : uint8_t
    {
        corrected = 0,
        gradient_model
    };

    /// The samples, by position, as the window held them when the run was asked for.
    std::vector<BackgroundSamplePosition> samples;
    double smoothing = 0.5;
    int degreeOrder = 4;
    bool addDither = false;
    bool outputBackgroundModel = false;
    an::BackgroundModelKind interpolationType = an::BackgroundModelKind::RadialBasis;
    an::BackgroundCorrection correctionType = an::BackgroundCorrection::Subtract;
    PreviewDisplayMode previewDisplay = PreviewDisplayMode::corrected;

    /// Whether the preview computes at all: it does once its button asked for it and until a setting or a sample
    /// changes, and shows the target until then.
    bool previewComputed = false;

    /// What the history step says of the grid, and the target's name, read on the GUI thread.
    int samplesPerLine = 20;
    double gridTolerance = 2.0;
    std::string targetName;
};

namespace an::ui::processes::background_extraction_detail
{

/// The side, in pixels, of the box a background sample is measured over.
inline constexpr std::uint32_t kSampleExtent = 25u;

/// Whether a sample's box lies inside an image of a size.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool sampleFits(const BackgroundSamplePosition& position, std::uint32_t width,
                                                      std::uint32_t height) noexcept;

/// The index of the sample nearest a position, the first of those equally near; nothing for an empty list.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<std::size_t>
nearestSample(const std::vector<BackgroundSamplePosition>& samples, double x, double y);

/// Places the sample grid over an image's CIE lightness, or over its only channel, and keeps the samples the engine's
/// rejection rule accepts, as "Generate Samples" places them.
///
/// @param scope an admitted execution.
/// @param source the image.
/// @param samplesPerLine how many samples one row holds.
/// @param tolerance how many deviations above the sites' level a sample may be.
/// @return the positions of the accepted samples; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<std::vector<BackgroundSamplePosition>>
generateSamples(const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source, int samplesPerLine,
                double tolerance);

} // namespace an::ui::processes::background_extraction_detail

/// Background Extraction: removes a smooth background a person samples - a grid the engine places and filters, and
/// samples placed or removed by clicking on the preview - by fitting a polynomial or a thin-plate spline through each
/// channel's sample medians and subtracting or dividing it out, through the engine's `fitBackgroundModel` and
/// `applyImageBackground`. An Apply can open the fitted background as an image of its own; the preview, computed when
/// its button asks, can show it instead of the result.
class AFTERNIGHTPROCESSES_API BackgroundExtractionProcess
    : public an::ui::TypedProcessControl<BackgroundExtractionParams>,
      public ImageSampleSelectorControl
{
public:
    BackgroundExtractionProcess(ImageViewControl* control);

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    size_t getSampleRadius() const override;
    void addSampleSelection(const ImageSampleSelection& selection) override;
    void removeSampleSelection(const ImageSampleSelection& selection) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    /// The samples the window holds, by position.
    [[nodiscard]] const std::vector<BackgroundSamplePosition>& samplePositions() const noexcept;

    /// Whether a grid placement asked for is still on its way.
    [[nodiscard]] bool isGeneratingSamples() const noexcept;

    ~BackgroundExtractionProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const BackgroundExtractionParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const BackgroundExtractionParams& params) const override;

private:
    void on_samplesPerLineChanged(int value);
    void on_gridToleranceChanged(double value);
    void on_smoothingChanged(double value);

    void on_generateSamplesButtonPushed();
    void on_clearSamplesButtonPushed();
    void on_previewButtonPushed();
    void on_previewCheckboxChanged(Qt::CheckState state);

    void on_interpolationMethod_currentTextChanged(const QString& text);
    void on_correctionMode_currentTextChanged(const QString& text);
    void on_previewDisplay_currentTextChanged(const QString& text);

    void initialize();
    void clearSamples();
    void landGeneratedSamples(std::uint64_t generation, an::Result<std::vector<BackgroundSamplePosition>>&& samples);
    void syncSampleSelectionOverlay();
    void updateActionButtons(bool controlsEnabled = true);
    void invalidatePreview();
    void syncPreviewUiState(bool controlsEnabled);
    void refreshTheme() override;

    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void previewControlStarted() override;
    void previewControlClosed() override;

    [[nodiscard]] BackgroundExtractionParams getParams() const override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void resetSettings() override;
    ProgressDialogControl* getProgressDialogControl(bool forSequence) override;

    QSliderSpinBoxUPtr samplesPerLineControl;
    QSliderDoubleSpinBoxUPtr gridToleranceControl;
    QSliderDoubleSpinBoxUPtr smoothingControl;

    /// The samples, kept on the GUI thread and copied into every run.
    std::vector<BackgroundSamplePosition> m_samples;

    /// The grid placement on its way, by the generation of the request that asked for it; a later request, a cleared
    /// list or another target makes the one on its way stale.
    std::uint64_t m_generateGeneration = 0u;
    bool m_generating = false;

    std::unique_ptr<ImageSampleSelectionController> m_sampleSelectionController;

    bool m_previewEnabled = true;

    /// Whether the preview computes: set by its button, cleared by a change of a setting, a sample or the target.
    bool m_previewComputed = false;

    /// The background model an Apply made, waiting for the result to land.
    process_utils::StagedOutputs m_StagedOutputs;

    an::ui::processes::BackgroundExtractionProcessUiElements m_ui;
};
