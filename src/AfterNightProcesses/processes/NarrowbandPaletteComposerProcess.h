/// @file
/// Narrowband Palette Composer: H-alpha, OIII and SII images - or one-shot-colour images they are synthesized from -
/// prepared as aligned unit-fraction planes, optionally stretched, and mapped into one of the engine's twelve palettes
/// through `composeNarrowbandPalette`; every palette of the grid composed at once by `composeNarrowbandPaletteGrid`
/// on the thread pool, never on the GUI thread.
///
/// Based on the narrowband palette process of Seti Astro Suite Pro by Franklin Marek, GPL-3.0: the window offers
/// that process's sources and synthesizes the emission lines from one-shot-colour images as it does, and the
/// palettes are a derived work of it that `composeNarrowbandPalette`'s header attributes. No Python, PyQt or SASPro
/// asset is used; `docs/dev/NARROWBAND_PALETTE_COMPOSER_PROVENANCE.md` records the snapshot that was reviewed.

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/TypedProcessControl.h"
#include "processes/NarrowbandPaletteComposerProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Color/NarrowbandPalette.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

class ImageViewControl;
class QBoxLayout;
class QWidget;

/// One source of the composer: the image the window snapshotted when the run was asked for, and its name.
struct NarrowbandPaletteSource
{
    std::shared_ptr<const an::consumer::ImageAsset> image;
    std::string name;
};

/// What one run of the composer reads: the five sources, the palette, the intensities and the preparation choices.
struct NarrowbandPaletteComposerProcessParams
{
    NarrowbandPaletteSource ha;
    NarrowbandPaletteSource oiii;
    NarrowbandPaletteSource sii;
    NarrowbandPaletteSource osc1;
    NarrowbandPaletteSource osc2;

    an::NarrowbandPalette palette = an::NarrowbandPalette::Sho;
    an::NarrowbandPaletteIntensities intensities{};

    /// Whether each prepared plane is stretched by the automatic screen transfer before the palette reads it.
    bool stretchInputs = true;

    /// Whether a plane of another size than the reference's is resized to it; refused by name otherwise.
    bool resizeMismatched = true;

    std::string outputName;
};

/// Narrowband Palette Composer: prepares its sources on the run's worker - a grey image as it is, a colour one's red
/// channel, a one-shot-colour image's red as H-alpha or SII and its green and blue mean as OIII, each stretched when
/// asked and resized to the reference when their sizes differ - and composes the chosen palette as a new Float32
/// colour image recorded as a combination of the sources; the palette grid is composed the same way on the thread pool
/// and shown when it lands, the latest request winning.
class AFTERNIGHTPROCESSES_API NarrowbandPaletteComposerProcess
    : public an::ui::TypedProcessControl<NarrowbandPaletteComposerProcessParams>
{
public:
    explicit NarrowbandPaletteComposerProcess(QWidget* parent = nullptr);
    ~NarrowbandPaletteComposerProcess() override;

    static std::string getProcessNameStatic();
    static bool requiresExclusiveAccessStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    /// Whether a grid build is on its way or waiting to start.
    [[nodiscard]] bool isGridBuilding() const noexcept;

    /// How many grids landed in the widgets since the window opened.
    [[nodiscard]] std::uint64_t gridsShown() const noexcept;

    /// Whether the widgets show a grid now.
    [[nodiscard]] bool hasGrid() const;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run,
                                       const NarrowbandPaletteComposerProcessParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const NarrowbandPaletteComposerProcessParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const NarrowbandPaletteComposerProcessParams& params) const override;

private:
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    void onContentLayoutReplaced(QBoxLayout* contentLayout) override;
    void resetSettings() override;
    void settingsUpdatedImpl() override;
    void enableProcessControls(bool enabled) override;
    void refreshTheme() override;
    [[nodiscard]] NarrowbandPaletteComposerProcessParams getParams() const override;

    /// A still grey or colour image: a source, and the target the result is opened beside.
    [[nodiscard]] bool isViewControlSupported(const ImageViewControl* ctrl) override;

    void connectUi();
    void rebuildImageSelectors();
    void validateSelections();
    void refreshPaletteGridItems();
    void clearPaletteGridImages();
    void syncIntensityPair(QSlider* slider, QSpinBox* spin);
    void handleSettingsChanged();
    void handlePreviewModeChanged(int index);
    void handlePaletteSelectionChanged(an::NarrowbandPalette palette);
    void requestGridBuild();
    void startGridBuild();
    void setPaletteGridBusy(bool busy);
    [[nodiscard]] an::NarrowbandPalette selectedPalette() const;
    [[nodiscard]] bool hasRequiredSourceSelection() const;
    [[nodiscard]] std::string defaultOutputBaseName() const;
    [[nodiscard]] NarrowbandPaletteSource sourceFromSelector(const QComboBox* selector) const;

    an::ui::processes::NarrowbandPaletteComposerProcessUiElements m_ui;
    bool m_rebuildingSelectors = false;
    bool m_syncingPaletteSelection = false;

    struct Grid;
    // Shared with the grid's worker, which reads the generation after the window may be gone.
    std::shared_ptr<Grid> m_grid;
};

namespace an::ui::processes::narrowband_palette_detail
{

/// Whether the sources name what a palette needs: OIII, or a one-shot-colour image that synthesizes it, and H-alpha
/// or SII, or a one-shot-colour image that synthesizes them.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool hasRequiredInputs(const NarrowbandPaletteComposerProcessParams& params);

} // namespace an::ui::processes::narrowband_palette_detail
