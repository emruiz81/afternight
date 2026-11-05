// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QWidget>
#include "processes/GHTransformationProcessUiBuilder.h"
#include "processes/StretchProcessSupport.h"
#include "controls/TypedProcessControl.h"
#include "controls/ImagePointSelectionController.h"
#include "widgets/HistogramGHTWidget.h"
#include "controls/QSliderSpinBox.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>

#include <QRect>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class ImageViewControl;

/// The curve the dialog's transformation list chooses.
enum class GhtTransformation : std::uint8_t
{
    GeneralizedHyperbolic,
    Histogram,
    Arcsinh,
    Linear,
    Inversion,
    ScreenTransfer
};

/// What the dialog's channel list chooses: the stored channels, one of them, or a colour component every channel is
/// stretched through.
enum class GhtChannel : std::uint8_t
{
    Rgb,
    Red,
    Green,
    Blue,
    Lightness,
    Saturation,
    Colour
};

/// Where the colour mode's luminance weights come from.
enum class GhtLuminanceSource : std::uint8_t
{
    Default,
    Image,
    Manual
};

/// The shadows clipping, in sigmas, and the target background the screen transfer is derived from.
inline constexpr double kGhtScreenTransferShadowsClipping = -2.8;
inline constexpr double kGhtScreenTransferTargetBackground = 0.25;

struct GHTransformationParams
{
    GhtTransformation transformation = GhtTransformation::GeneralizedHyperbolic;
    bool invert = false;
    GhtChannel channel = GhtChannel::Rgb;

    /// The stretch factor the dialog shows; the curve's amount is its exponential less one.
    double stretchFactor = 0.0;
    double localIntensity = 0.0;
    double symmetryPoint = 0.0;
    double protectShadows = 0.0;
    double protectHighlights = 1.0;
    double blackPoint = 0.0;
    double whitePoint = 1.0;

    bool linkedStf = false;
    an::StretchOverflowPolicy colourClip = an::StretchOverflowPolicy::Clip;
    GhtLuminanceSource luminanceSource = GhtLuminanceSource::Default;
    std::array<double, 3u> luminanceCoefficients{1.0, 1.0, 1.0};

    /// The image the dialog targeted when the run was issued, whose statistics derive a preview area's screen transfer.
    std::shared_ptr<const an::consumer::ImageAsset> target;

    /// The preview area shown when the run was issued; empty for the whole image.
    QRect area;

    /// The dialog's measurements of its target, shared with its runs.
    std::shared_ptr<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>> statistics;
};

/// The one-dimensional curve the dialog's parameters ask for: its function, its amount, bias and points, and for the
/// screen transfer the triplets derived from the statistics, linked or channel by channel.
///
/// @param params the dialog's parameters.
/// @param statistics the whole image's statistics; read only by the screen transfer.
/// @return the curve; or what the derivation refused, `ErrorCode::NotInitialized` for a screen transfer with no
///         statistics.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::GeneralizedStretchParameters>
ghtStretchParametersOf(const GHTransformationParams& params,
                       const an::ui::processes::ScreenTransferStatistics* statistics);

/// The engine's request for the dialog's stretch of an image: the curve, and how a colour image's channels take part -
/// the channels chosen, or the lightness, the saturation or a luminance of stated weights, read in the image's own
/// encoding.
///
/// @param params the dialog's parameters.
/// @param image what the image is.
/// @param statistics the whole image's statistics; read only by the screen transfer.
/// @return the request; or what `ghtStretchParametersOf` refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::StretchImageRequest>
ghtStretchRequestOf(const GHTransformationParams& params, const an::ImageDescriptor& image,
                    const an::ui::processes::ScreenTransferStatistics* statistics);

/// The colour mode's luminance weights, normalized: equal thirds by default and from the image, the stated
/// coefficients held at zero and divided by their sum otherwise, equal thirds when they sum to nothing.
///
/// @param params the dialog's parameters.
/// @return the weights, summing to one.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::array<double, 3u>
ghtLuminanceWeightsOf(const GHTransformationParams& params);

class AFTERNIGHTPROCESSES_API GHTransformationProcess : public an::ui::TypedProcessControl<GHTransformationParams>,
                                                        public HistogramGHTWidgetListener,
                                                        public ImagePointSelectorControl
{
public:
    GHTransformationProcess(ImageViewControl* control);

    void refreshTheme() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onImageLoaded(ImageViewControl* ctrl) override;

    void onImageUpdated(ImageViewControl* ctrl) override;

    void onViewControlChanged(ImageViewControl* oldCtrl, ImageViewControl* newCtrl) override;

    void onProtectShadowsChanged(double shadows) override;
    void onSymmetryPointChanged(double midtones) override;
    void onProtectHightlightsChanged(double highlights) override;
    void onSettingsUpdated() override;
    void onSPPickerUpdated(double value) override;

    void onAlternateImageEnabled(ImageViewControl* ctrl) override;
    void onAlternateImageDisabled(ImageViewControl* ctrl) override;
    void onImageDisplayChannelChanged(ImageViewControl* ctrl, int mode) override;

    an::GeneralizedStretchParameters stretchParameters() override;
    std::uint32_t transferCurveChannel() const override;

    void onPointSelected(const QPoint& point) override;

    void selectionAreaUpdated() override;
    void selectionAreaChanged() override;

    void newSelectionFinished() override;

    ~GHTransformationProcess() override;

private:
    void on_channelSelector2_currentTextChanged(const QString& str);
    void on_plotResolution_currentTextChanged(const QString& str);

    void on_transformationTypeSelector_currentTextChanged(const QString& str);
    void on_HumanWeightedLuminanceClicked(Qt::CheckState state);
    void on_colourClipSelector_currentTextChanged(const QString& str);
    void on_lumCoeffSourceSelector_currentTextChanged(const QString& str);
    void on_lumCoeffChanged(double value);
    void on_linkedStfClicked(Qt::CheckState state);

    void on_InvertClicked(Qt::CheckState state);

    void on_stretchFactorChanged(double value);
    void on_localIntensityChanged(double value);
    void on_symmetryPointChanged(double value);
    void on_protectShadowsChanged(double value);
    void on_protectHighlightsChanged(double value);
    void on_blackPointChanged(double value);
    void on_whitePointChanged(double value);

    void on_gridClicked(bool checked);
    void on_transferCurveClicked(bool checked);
    void on_ZoomInButton();
    void on_ZoomOutButton();
    void on_ZoomResetButton();
    void on_LocalChannelNormalization(bool checked);
    void on_rt_showClippingInfo(bool checked);
    void on_showHistogramAreas(bool checked);

    void on_spFromImageClicked(bool checked);
    void on_spFromImageAreaClicked(bool checked);
    void on_spFromHistogramClicked(bool checked);

    void targetChanged();
    void updateSupportedImageChannels();
    void updateSupportedPlotResolutions();
    void previewControlStarted() override;
    void previewControlClosed() override;

    void resetSettings() override;

    bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;

    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const GHTransformationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessBodyCoverage
    typedProcessBodyCoverage(const GHTransformationParams& params) const override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const GHTransformationParams& params) const override;

    [[nodiscard]] GHTransformationParams getParams() const override;

    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;

    [[nodiscard]] GhtTransformation currentTransformation() const;
    [[nodiscard]] GhtChannel currentChannel() const;
    [[nodiscard]] bool isColourTarget() const;

    /// Counts the target, with the component its colour mode stretches, on a worker, and shows it as the original
    /// histogram.
    void recalculateOriginal();

    /// Shows what the stretch makes of the original histogram, computed on a worker: the original counts remapped
    /// through the curve channel by channel, or the stretched target counted again for a colour mode.
    void recalculateHistogramPreview();

    /// Sets the symmetry point to the mean of a rectangle of the target, measured on a worker.
    void pickSymmetryPoint(const QRect& region);
    void setPickedSymmetryPoint(double value);

    void updateControlVisibility();
    void updateHistogramDisplayChannels();
    [[nodiscard]] ImageViewControl* getSamplingViewControl() const;
    void clearPointSelectionController();
    void clearAreaSelectionController();
    void disarmSymmetryPointPickers();
    void rebuildPointSelectionController();
    void rebuildAreaSelectionController();
    void updateSpPickerButtonsStyle();

    std::unique_ptr<HistogramGHTWidget> m_original_histogram_widget;
    std::unique_ptr<HistogramGHTWidget> m_preview_histogram_widget;

    bool m_IgnoreLastValueChangeForRangeSelectors = false;

    QSliderDoubleSpinBoxUPtr stretchFactorControl;
    QSliderDoubleSpinBoxUPtr localIntensityControl;
    QSliderDoubleSpinBoxUPtr symmetryPointControl;
    QSliderDoubleSpinBoxUPtr protectShadowsControl;
    QSliderDoubleSpinBoxUPtr protectHighlightsControl;
    QSliderDoubleSpinBoxUPtr blackPointControl;
    QSliderDoubleSpinBoxUPtr whitePointControl;

    std::shared_ptr<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>>
        m_Statistics =
            std::make_shared<an::ui::processes::StretchStatisticsCache<an::ui::processes::ScreenTransferStatistics>>();

    /// The target's statistics the transfer curve draws a screen transfer with, as last measured.
    std::shared_ptr<const an::ui::processes::ScreenTransferStatistics> m_CurveStatistics;

    // The original and the preview counts, each counted on a worker one count at a time; a request while one runs is
    // taken up when it lands.
    std::shared_ptr<const an::ui::HistogramDisplayData> m_OriginalCounts;
    bool m_OriginalRunning = false;
    bool m_OriginalPending = false;
    bool m_PreviewRunning = false;
    bool m_PreviewPending = false;

    std::unique_ptr<ImagePointSelectionController> m_PointSelectionController;
    std::unique_ptr<ImageCropController> m_AreaSelectionController;

    an::ui::processes::GHTransformationProcessUiElements m_ui;
};
