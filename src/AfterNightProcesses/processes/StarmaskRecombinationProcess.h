// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Starmask Recombination: a starless image and its stars put back together - the stars added to the starless image,
/// the sum blended over it through a mask when one is chosen - one engine composition, `recombineStarMask`, in the
/// higher of the two images' representations.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/StarmaskRecombinationProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <QRect>
#include <QString>

#include <memory>
#include <string>

class ImageViewControl;
class QAbstractButton;
class QWidget;

/// What one run of the recombination reads: the stars and the mask the window snapshotted when the run was asked
/// for; the starless image is the run's target.
struct StarmaskRecombinationParams
{
    /// The star image, taken from its view on the GUI thread; null when none is selected.
    std::shared_ptr<const an::consumer::ImageAsset> stars;
    QString starsName;

    /// The mask the sum is blended through, or null for none.
    std::shared_ptr<const an::consumer::ImageAsset> mask;
    QString maskName;
    bool invertMask = false;

    QString starlessName;

    /// The preview's area in the starless image's pixels, which the stars and the mask are cropped to as the
    /// framework crops the target; empty for the whole image.
    QRect previewArea;
};

/// Starmask Recombination: a window naming the starless image, the stars and an optional mask among the open views;
/// the starless view becomes its target, and Ok recombines them through the engine's `recombineStarMask` into a new
/// image beside it, recorded as the starless image with the stars put back.
class AFTERNIGHTPROCESSES_API StarmaskRecombinationProcess
    : public an::ui::TypedProcessControl<StarmaskRecombinationParams>
{
public:
    explicit StarmaskRecombinationProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void onApplicationClose() override;

    ~StarmaskRecombinationProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const StarmaskRecombinationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const StarmaskRecombinationParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const StarmaskRecombinationParams& params) const override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);

    void refreshTheme() override;
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void settingsUpdatedImpl() override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    [[nodiscard]] StarmaskRecombinationParams getParams() const override;

    void initialize();
    void validateSelectedImages();
    void addViewToSelectors(const ImageViewControl* ctrl);

    an::ui::processes::StarmaskRecombinationProcessUiElements m_ui;
};
