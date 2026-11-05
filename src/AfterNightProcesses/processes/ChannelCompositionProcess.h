// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Channel Composition: three grey images composed as the components of a colour space - red, green and blue, CIE XYZ,
/// L*a*b* or L*c*h*, HSL, HSV, HSI or YCbCr - into one colour image through the engine's `composeImageChannels` and
/// `convertImageColor`, the second and third optionally matched to the first by the normalization domain's scaling
/// match; three sequences composed frame by frame the same way.
#pragma once

#include "AfterNightProcessesExport.h"

#include "app/AfterNightAppListener.h"
#include "controls/ProcessExecution.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "processes/ChannelCompositionProcessUiBuilder.h"
#include "session/ExecutionScope.h"
#include "session/ImageDocument.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Core/Result.h>

#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class ImageViewControl;
class QAbstractButton;
class QWidget;

namespace an::ui::processes::channel_composition_detail
{

/// The colour spaces the dialog composes in, in the selector's order.
enum class ColourSpace : std::uint8_t
{
    Rgb,
    CieXyz,
    CieLab,
    CieLch,
    Hsl,
    Hsv,
    Hsi,
    YCbCr
};

/// The space a selector label names, or nothing for a label the selector does not have.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::optional<ColourSpace> colourSpaceFromLabel(const QString& label);

/// The selector's label for a space.
[[nodiscard]] AFTERNIGHTPROCESSES_API const char* colourSpaceLabel(ColourSpace space) noexcept;

/// The name a composed image's view is given: "RGB", "CIE L*a*b" and so on.
[[nodiscard]] AFTERNIGHTPROCESSES_API const char* composedImageName(ColourSpace space) noexcept;

/// The one-letter component names the dialog labels its selectors with.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::array<const char*, 3u> componentNames(ColourSpace space) noexcept;

/// What one composition reads: up to three grey images, the first required, and the choices.
struct CompositionRequest
{
    /// The components, the first sharing its grid with the result; a null source is a zero plane.
    std::array<const an::ui::ProcessSource*, 3u> components{};

    /// The names the components' views have, for the record.
    std::array<std::string, 3u> names{"-", "-", "-"};

    ColourSpace space = ColourSpace::Rgb;

    /// Whether the second and third components are matched to the first by the scaling match.
    bool linearMatch = false;
};

/// Composes the components into a colour image of the run's session: the planes read in fractions of full scale and
/// held in one working representation, matched when asked, composed as the space's components, converted to red, green
/// and blue, and written in the highest of the components' representations. The result is recorded as a combination of
/// the components, linear under the process's name when the space is RGB and every component declares itself linear,
/// nonlinear in any other space or when any component declares itself nonlinear.
///
/// @param scope the run's admitted execution.
/// @param request what to compose.
/// @return the image; `ErrorCode::InvalidArgument` without a first component; `ErrorCode::IncompatibleArguments` for
///         a component that is not one channel or not the first's size; or what the engine refused.
[[nodiscard]] AFTERNIGHTPROCESSES_API an::Result<an::consumer::ImageAsset>
composeChannelImages(const an::ui::ExecutionScope& scope, const CompositionRequest& request);

/// The history description a composition records: the components' views and the space's component names.
[[nodiscard]] AFTERNIGHTPROCESSES_API std::string compositionDescription(const CompositionRequest& request);

} // namespace an::ui::processes::channel_composition_detail

/// Channel Composition: a top-level window naming three grey views and a colour space; Ok composes them on the thread
/// pool under one admitted operation - a still image into a new view beside the first component, three sequences
/// frame by frame into a new sequence - and the window closes when the result is shown.
class AFTERNIGHTPROCESSES_API ChannelCompositionProcess : public TopLevelProcessBaseControl,
                                                          public AfterNightAppListener
{
public:
    explicit ChannelCompositionProcess(QWidget* parent = nullptr);

    static std::string getProcessNameStatic();

    void onViewControlCreated(const ImageViewControl* ctrl) override;
    void onViewControlDestroyed(const ImageViewControl* ctrl) override;
    void onViewControlRenamed(const std::string& newName, const std::string& oldName,
                              const ImageViewControl* ctrl) override;
    void onViewControlUpdated(const ImageViewControl* ctrl) override;

    void refreshTheme() override;

    /// Whether a composition is on its way.
    [[nodiscard]] bool isRunning() const noexcept;

    /// How many compositions published a view since the window opened.
    [[nodiscard]] std::uint64_t runsPublished() const noexcept;

    /// How many compositions were refused, stopped or failed since the window opened.
    [[nodiscard]] std::uint64_t runsFailed() const noexcept;

    /// Asks the composition on its way to stop; nothing is published.
    void requestStop();

    /// Composes as the Ok button does.
    void executeChannelComposition();

    ~ChannelCompositionProcess() override;

protected:
    /// What a run does on its worker once its operation is admitted and before it reads anything: nothing, unless a
    /// window of a suite says otherwise. Built on the GUI thread before the worker starts.
    [[nodiscard]] virtual std::function<void(an::ui::ExecutionScope&)> admissionHook() const
    {
        return {};
    }

private:
    void on_dialog_box_button_clicked(QAbstractButton* button);
    void on_colorSpace_currentTextChanged(const QString& text);

    void initialize();
    void validateSelectedImages();
    void addViewToSelectors(const ImageViewControl* ctrl);
    void removeViewFromSelectors(const QString& name);
    [[nodiscard]] bool isComponentViewSupported(const ImageViewControl* ctrl) const;
    void finishRun(an::Status outcome, bool published);

    an::ui::processes::ChannelCompositionProcessUiElements m_ui;

    struct Run;
    std::unique_ptr<Run> m_run;
};
