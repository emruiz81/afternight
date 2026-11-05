// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// NBRGB Combination: a colour image's channels enhanced with continuum-corrected narrowband images - a bandwidth and
/// a scale per channel, a linear fit inside a stated interval, an optional rescale - one engine operation,
/// `combineNarrowbandRgb`, run in fractions of full scale over the target's own representation.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/NBRGBCombinationProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/NarrowbandRgbCombination.h>

#include <QRect>
#include <QString>

#include <array>
#include <memory>
#include <string>

class ImageViewControl;

/// What the preview shows: the combination, or one of its inputs as a colour image.
enum class NBRGBPreviewDisplay : int
{
    Combination = 0,
    Rgb = 1,
    NarrowbandRed = 2,
    NarrowbandGreen = 3,
    NarrowbandBlue = 4
};

/// What one run of the combination reads: the engine's request without its images, and the three narrowband images
/// the window snapshotted when the run was asked for.
struct NBRGBCombinationProcessParams
{
    /// The bandwidths, the scales, the fit interval and the rescale; its channels' samples are bound on the worker.
    an::NarrowbandRgbRequest engine;

    /// The narrowband image of each channel, red, green and blue; null where none is selected.
    std::array<std::shared_ptr<const an::consumer::ImageAsset>, 3u> narrowband{};

    std::array<std::string, 3> narrowbandNames;

    NBRGBPreviewDisplay previewDisplay = NBRGBPreviewDisplay::Combination;

    /// The preview's area in the target's pixels, which the narrowband images are cropped to as the framework crops
    /// the target; empty for the whole image.
    QRect previewArea;
};

/// NBRGB Combination: enhances a still colour target with up to three grey narrowband images of its size through the
/// engine's `combineNarrowbandRgb`, in place in the target's representation, as a new image; the result is recorded as
/// a combination of the images, linear when every one declares itself linear.
class AFTERNIGHTPROCESSES_API NBRGBCombinationProcess
    : public an::ui::TypedProcessControl<NBRGBCombinationProcessParams>
{
public:
    explicit NBRGBCombinationProcess(ImageViewControl* control);
    ~NBRGBCombinationProcess() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const NBRGBCombinationProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const NBRGBCombinationProcessParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const NBRGBCombinationProcessParams& params) const override;

private:
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void on_targetImageSelector_currentTextChangedInternal(const QString& str) override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void refreshTheme() override;
    [[nodiscard]] NBRGBCombinationProcessParams getParams() const override;

    /// A still colour image: the target.
    [[nodiscard]] bool isViewControlSupported(const ImageViewControl* ctrl) override;

    /// A still grey image of the target's size: a narrowband input.
    [[nodiscard]] bool isNarrowbandViewSupported(const ImageViewControl* ctrl) const;

    void rebuildNarrowbandSelectors(const QString& preferredRed = {}, const QString& preferredGreen = {},
                                    const QString& preferredBlue = {});
    void connectUi();

    an::ui::processes::NBRGBCombinationProcessUiElements m_ui;
    bool m_ignoreSelectorUpdates = false;
};

namespace an::ui::processes::nbrgb_detail
{

/// Whether a request names what a combination needs: at least one narrowband image, each the target's size and one
/// channel, every bandwidth and scale admissible and the fit interval ordered within the unit interval.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool combinationReady(const NBRGBCombinationProcessParams& params,
                                                            const an::ImageDescriptor& target);

} // namespace an::ui::processes::nbrgb_detail
