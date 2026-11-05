// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Profiled Starmask Recombination: a starless image and its stars put back together through a mask painted from a
/// star profile a person chose - every profiled star a filled ellipse of its fit, scaled and softened, or a shaped
/// area of a stated shape and size - the stars' edges optionally tightened; one engine composition,
/// `recombineStarMask`, over a mask the engine paints.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ImageStarSelectionController.h"
#include "controls/QSliderSpinBox.h"
#include "controls/StarFilter.h"
#include "controls/StarsFilterControlHandler.h"
#include "controls/TypedProcessControl.h"
#include "processes/ProfiledStarmaskRecombinationProcessUiBuilder.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Algorithms/Stars/StarMask.h>

#include <QRect>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class ImageViewControl;
class QAbstractButton;
class QWidget;

/// What one run of the recombination reads: the stars the window snapshotted, the profile's stars as the filter
/// left them, and how the mask is painted from them.
struct ProfiledStarmaskRecombinationParams
{
    /// The star image, taken from its view on the GUI thread; null when none is selected.
    std::shared_ptr<const an::consumer::ImageAsset> stars;
    QString starsName;
    QString starlessName;

    /// The profile's stars, as the filter left them; empty for no profile, in which case the stars are put back
    /// unmasked.
    an::ui::StarListPtr profileStars;

    /// Whether the mask is the filled ellipses of the stars' fits, or shaped areas.
    bool binaryAreas = true;

    /// The factor the ellipses' axes are multiplied by.
    double scaleFactor = 1.0;

    /// The standard deviation, in pixels, the ellipses' edges are softened by; zero for none.
    double softenEdges = 2.0;

    /// The shaped areas' request: the shape, the size window, the radius's factor and the pixels added to it.
    an::StarMaskShapeRequest shape;

    /// The shape's label, as the selector shows it.
    QString shapeName;

    /// How strongly the stars' edges are tightened before they are put back; zero for not at all.
    double tightenEdges = 0.0;

    /// Whether the mask is read inverted.
    bool invertSelection = false;

    /// The preview's area in the starless image's pixels; empty for the whole image.
    QRect previewArea;
};

namespace an::ui::processes::profiled_recombination_detail
{

/// The shape a selector label names, or nothing for "Binary Areas" and a label the selector does not have.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<an::StarMaskShape> starMaskShapeFromLabel(const QString& label);

} // namespace an::ui::processes::profiled_recombination_detail

/// Profiled Starmask Recombination: a window naming the starless image and the stars among the open views and a star
/// profile file; the profile is read on the thread pool, its stars filtered through the star filter window and drawn
/// over the preview, and Ok paints the mask from them and recombines through the engine's `recombineStarMask` into a
/// new image beside the starless one, recorded as the starless image with the stars put back.
class AFTERNIGHTPROCESSES_API ProfiledStarmaskRecombinationProcess
    : public an::ui::TypedProcessControl<ProfiledStarmaskRecombinationParams>,
      public StarsFilterControlHandler,
      public ImageStarSelectorControl
{
public:
    explicit ProfiledStarmaskRecombinationProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    [[nodiscard]] an::ui::StarListPtr getCurrentStarList() override;
    void setCurrentStarList(const an::ui::StarListPtr& starList, bool fullUpdate = true) override;
    [[nodiscard]] ImageViewControl* getViewControl() override;
    void updateStarSelection(const an::ui::StarListPtr& starSelection) override;
    [[nodiscard]] bool isForProfiledStarmaskRecombination() override;
    [[nodiscard]] bool isPreviewEnabled() override;
    void onStarSelected(size_t idx) override;
    [[nodiscard]] double getCurrentScaleFactor() const override;

    void onApplicationClose() override;

    /// Loads a profile file as the Open button's choice does: the file is read on the thread pool and its stars
    /// land on the GUI thread, the latest request winning.
    void loadProfile(const QString& path);

    /// Whether a profile read is on its way.
    [[nodiscard]] bool isProfileLoading() const noexcept;

    /// What a profile load shares with the worker that reads it, which the worker holds while it runs: a case checks
    /// that the window's going frees nothing under a worker on its way.
    [[nodiscard]] std::weak_ptr<const void> profileLoadHandle() const noexcept;

    /// The stars the profile holds now, as the filter left them.
    [[nodiscard]] std::size_t profileStarCount() const;

    ~ProfiledStarmaskRecombinationProcess() override;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ProfiledStarmaskRecombinationParams& params,
                                       an::ui::ExecutionScope& scope) override;
    [[nodiscard]] an::ui::ProcessStep
    describeTypedProcessStep(const ProfiledStarmaskRecombinationParams& params) const override;
    [[nodiscard]] an::ui::PreviewScaling previewScaling() const override;
    [[nodiscard]] std::uint32_t
    typedPreviewRegionMargin(const ProfiledStarmaskRecombinationParams& params) const override;
    [[nodiscard]] std::vector<std::shared_ptr<const an::consumer::ImageAsset>>
    typedImagesRead(const ProfiledStarmaskRecombinationParams& params) const override;

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_starsProfile_Changed(const QString& text);
    void on_openStarsProfileButtonPushed();
    void on_starsFilterButtonPushed();
    void on_shapeSelector_currentTextChanged(const QString& text);

    void refreshTheme() override;
    [[nodiscard]] bool isPreviewEnabled() const override;
    void enablePreview(bool enabled) override;
    void settingsUpdatedImpl() override;
    void getBatchExecutionParameters(bool& createNewImage, bool& closeAtEnd, bool& executeInBackground,
                                     std::string& newImageName) override;
    void previewControlStarted() override;
    void previewControlClosed() override;
    [[nodiscard]] ProfiledStarmaskRecombinationParams getParams() const override;

    void initialize();
    void validateSelectedImages();
    void addViewToSelectors(const ImageViewControl* ctrl);
    void profileChanged();
    void showProfileStars();

    QSliderDoubleSpinBoxUPtr scaleFactorControl;
    QSliderDoubleSpinBoxUPtr tightenEdgesControl;
    QSliderDoubleSpinBoxUPtr softenEdgesControl;
    QSliderDoubleSpinBoxUPtr percentPixelIncreaseControl;
    QSliderSpinBoxUPtr pixelIncreaseControl;

    an::ui::StarListPtr m_profileStars = std::make_shared<const an::ui::StarList>();
    std::unique_ptr<ImageStarSelectionController> m_starSelectionController;

    struct ProfileLoad;
    std::shared_ptr<ProfileLoad> m_load;

    an::ui::processes::ProfiledStarmaskRecombinationProcessUiElements m_ui;
};
