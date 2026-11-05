/// @file
/// NB Color Mapper: narrowband layers - each a grey image given a hue and a saturation, or a colour image as it is,
/// through its own black point and midtones, optionally modulated by another layer - screened into one colour image,
/// with an optional background neutralization and output stretch: one engine operation, `mapNarrowbandColor`.
///
/// Based on NBColorMapper 2.2, a PixInsight script by Mike Cranfield of Cosmic Photons -
/// Copyright (C) 2023, 2024 Mike Cranfield, GNU General Public License version 3: the window offers the script's
/// controls, and the mapping is a derived work of the script that `mapNarrowbandColor`'s header attributes. No
/// PixInsight, PJSR or Pleiades code, interface or asset is used; `docs/dev/NBCOLORMAPPER_PROVENANCE.md` records
/// the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/MaskProcessUtils.h"
#include "processes/NBColorMapperProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/NarrowbandColorMap.h>

#include <QRect>
#include <QString>

#include <memory>
#include <string>
#include <vector>

class ImageViewControl;

/// One layer as the window keeps it: the view it reads and the layer's choices, whose samples are bound on the worker.
struct NBColorMapperProcessLayerState
{
    std::string imageName;
    an::NarrowbandColorLayer layer;
};

/// What one run of the mapper reads: the layers with the images the window snapshotted when the run was asked for,
/// and the frame-level choices.
struct NBColorMapperProcessParams
{
    /// One layer per row of the table.
    struct Layer
    {
        std::string imageName;

        /// The layer's image, taken from its view on the GUI thread; null when the view is gone.
        std::shared_ptr<const an::consumer::ImageAsset> image;

        an::NarrowbandColorLayer layer;
    };

    std::vector<Layer> layers;

    /// The background neutralization and the target background; whether the output is stretched is decided per run.
    an::NarrowbandColorMapRequest request;

    /// Whether a preview is stretched to the target background.
    bool applyPreviewStretch = true;

    /// Whether an Apply's result is stretched to the target background.
    bool applyStretchToOutput = false;

    /// Whether an Apply opens each included layer, colourized on its own, beside the result.
    bool returnColorizedLayers = false;

    std::string outputBaseName;

    /// The preview's area in the target's pixels, which every layer is cropped to as the framework crops the target;
    /// empty for the whole image.
    QRect previewArea;
};

/// NB Color Mapper: maps the layers of the table into one colour image through the engine's `mapNarrowbandColor` in
/// fractions of full scale, as a new Float32 image recorded as a combination of the layers' images, and opens each
/// layer colourized on its own beside it when asked.
class AFTERNIGHTPROCESSES_API NBColorMapperProcess : public an::ui::TypedProcessControl<NBColorMapperProcessParams>
{
public:
    explicit NBColorMapperProcess(ImageViewControl* control);
    ~NBColorMapperProcess() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    /// The layers of the table, for a case that reads them.
    [[nodiscard]] const std::vector<NBColorMapperProcessLayerState>& layers() const noexcept;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const NBColorMapperProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep describeTypedProcessStep(const NBColorMapperProcessParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const NBColorMapperProcessParams& params) const override;
    void executeProcessStarted(RTPreviewMode previewMode, bool sequenceMode) override;
    void executeProcessCompletedImpl(RTPreviewMode previewMode, bool sequenceMode) override;

private:
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void enableProcessControlsImpl(bool enabled) override;
    void refreshTheme() override;
    [[nodiscard]] NBColorMapperProcessParams getParams() const override;

    /// A still grey or colour image: the target, and a layer.
    [[nodiscard]] bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void connectUi();
    void rebuildImageSelectors(const QString& preferredLayer = {});
    void refreshLayerTable();
    void refreshLayerTableRow(int row);
    void syncSelectedLayerFromEditor();
    void refreshLayerEditorFromSelection();
    void refreshLayerColorPicker();
    void refreshModificationLayerSelector();
    void updateModificationControlsEnabled();
    void autoAddActiveLayer();
    void addLayerFromEditor();
    void removeSelectedLayer();
    [[nodiscard]] NBColorMapperProcessLayerState layerFromEditor() const;
    [[nodiscard]] NBColorMapperProcessLayerState defaultLayerForImageName(const std::string& imageName) const;
    [[nodiscard]] std::string defaultOutputBaseName() const;
    [[nodiscard]] bool selectedLayerUsesMonoColorControls() const;
    [[nodiscard]] bool isLayerViewSupported(const ImageViewControl* ctrl) const;

    an::ui::processes::NBColorMapperProcessUiElements m_ui;
    std::vector<NBColorMapperProcessLayerState> m_layers;
    bool m_loadingLayerEditor = false;

    /// The colourized layers an Apply made, waiting to be opened once the result lands.
    process_utils::StagedOutputs m_stagedOutputs;
};

namespace an::ui::processes::nbcolormapper_detail
{

/// Whether the layers name what a map needs: at least one included layer, every image present, grey or colour and of
/// the first's size, every number finite, and every modifier another layer of the table.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool combinationReady(const NBColorMapperProcessParams& params);

} // namespace an::ui::processes::nbcolormapper_detail
