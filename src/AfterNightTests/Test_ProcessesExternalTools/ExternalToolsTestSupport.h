// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the external tools' cases share: the three processes as one table - the action and the registry descriptor
/// each is launched through, how its window is opened and set to a mode, the button that runs it and what it makes -
/// the stand-in program the product is pointed at with a chosen behaviour, the real programs where they are installed,
/// and the run of one OK through a window as a person runs it.
#pragma once

#include "Test_ProcessesChannels/ChannelsTestSupport.h"
#include "Test_ProcessesExternalTools/StandInTool.h"

#include "processes/ExternalToolSupport.h"

#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QWidget>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace an::tests::processes::externaltools
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::ViewHandle;

/// A window of one of the processes, closed and deleted with the case once its runs have landed.
struct WindowDeleter
{
    void operator()(QWidget* window) const;
};
using WindowHandle = std::unique_ptr<QWidget, WindowDeleter>;

/// One of the processes this family holds.
struct ExternalTool
{
    /// A name for a case's parameter: "Starnet".
    const char* name = "";

    /// The registered action that launches it.
    const char* action = "";

    /// The id of the registry descriptor the launcher acts for.
    const char* descriptorId = "";

    /// The class the launcher opens.
    const char* className = "";

    /// The name it attaches to a view under, and titles its steps with.
    const char* processName = "";

    /// The product the stand-in imitates for it: "starnet2" or "deepsnr".
    const char* product = "";

    /// The program's name in the product's words: "StarNet2", "DeepSNR".
    const char* programName = "";

    /// The global setting that names the program.
    const char* settingsKey = "";

    /// The scene its window cases open on.
    const char* scene = "";

    /// Whether a run replaces the view's image, as a history step, rather than opening new views.
    bool inPlace = false;

    /// The suffixes of the views a run opens beside the target, in order; none for a run in place.
    std::vector<std::string> outputSuffixes;

    /// The title a failure is reported under.
    const char* errorTitle = "";

    /// The words a failure's message opens with, before what failed.
    const char* errorLead = "";

    /// Opens its window over a target, as the launcher's entry point does, without the delete-on-close the framework
    /// sets.
    std::function<WindowHandle(ImageViewControl* target)> open;

    /// Opens a window whose run asks for its own stop once it is admitted.
    std::function<WindowHandle(ImageViewControl* target)> openStopping;

    /// Sets an open window's widgets to a mode: `-` for the dialog's own values, `prestretch` for the automatic
    /// stretch, `stars` for StarNet's star outputs, `lightness` and `binary` for the star mask's shaping. Fails the
    /// case for a mode the window does not know.
    std::function<void(QWidget& window, std::string_view mode)> configure;

    /// Runs the window as OK does.
    std::function<void(QWidget& window)> run;
};

/// The processes, in a stable order: StarNet, StarNet's star mask, DeepSNR.
[[nodiscard]] const std::vector<ExternalTool>& externalTools();

/// The process of a name, or null.
[[nodiscard]] const ExternalTool* toolNamed(std::string_view name);

/// Points the product at the stand-in program for one tool: the tool's global setting names this executable, and the
/// environment the product's program inherits names the product it stands in for and its behaviour. Keeps the file
/// the stand-in records its arguments in and the copy of the image it was handed, and clears everything when the
/// case ends.
class StandInTools
{
public:
    /// @param context the case's context, whose settings name the program.
    /// @param tool the tool the stand-in imitates.
    /// @param behaviour the stand-in's behaviour: "identity", "half", "machine", "exit-code", "crash", "no-output",
    ///        "wrong-size" or "slow".
    StandInTools(ProcessTestContext& context, const ExternalTool& tool, std::string_view behaviour = "identity");
    ~StandInTools();
    StandInTools(const StandInTools&) = delete;
    StandInTools& operator=(const StandInTools&) = delete;

    /// Changes the stand-in's behaviour for the runs that follow.
    void setBehaviour(std::string_view behaviour);

    /// The arguments of every run the stand-in recorded, one per line, in order.
    [[nodiscard]] QStringList recordedRuns() const;

    /// Where the stand-in copies the image it is handed.
    [[nodiscard]] QString keptInputPath() const;

    /// This executable's path, which the settings name.
    [[nodiscard]] static QString executablePath();

private:
    QTemporaryDir m_directory;
};

/// The real program a tool names, found through the tool's own install locations or
/// `AFTERNIGHT_TEST_<PRODUCT>_PATH`; empty when none is installed. `AFTERNIGHT_TEST_REQUIRE_EXTERNAL_TOOLS=1` makes a
/// case fail rather than skip without one.
[[nodiscard]] QString realToolPath(const ExternalTool& tool);

/// Whether a missing real program fails a case rather than skipping it.
[[nodiscard]] bool realToolsRequired();

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
}

/// Sets a double spin box, a check box, a spin box or a combo box's data by object name.
void setDouble(QWidget& window, const char* name, double value);
void setInt(QWidget& window, const char* name, int value);
void setChecked(QWidget& window, const char* name, bool checked);
void setComboData(QWidget& window, const char* name, const QString& data);

/// The OK button of a window, or null.
[[nodiscard]] QPushButton* okButtonOf(QWidget& window);

/// Presses a window's OK button; fails the case when it is disabled.
void pressOk(QWidget& window);

/// Whether a window's runs have all landed.
[[nodiscard]] bool windowIdle(const QWidget& window);

/// Turns the event loop until a window's runs, the views' work and the workers have all landed.
[[nodiscard]] bool settleWindow(ProcessTestContext& context, const QWidget& window);

/// Runs the window and waits for the run to land: true when it landed.
[[nodiscard]] bool runAndSettle(ProcessTestContext& context, QWidget& window, const ExternalTool& tool);

/// How long a case waits for an installed program's run: the program loads its model and evaluates it over the
/// image, which takes seconds alone and several times that on a machine busy with other suites - far past what is
/// given to something that should happen at once.
inline constexpr std::chrono::seconds kInstalledProgramPatience{300};

/// Runs the window over an installed program, waits up to `kInstalledProgramPatience` for its run to land and then for
/// the views' work and the workers: true when everything landed.
[[nodiscard]] bool runInstalledAndSettle(ProcessTestContext& context, QWidget& window, const ExternalTool& tool);

/// The asset of a fixture scene; fails the case when it cannot be made.
[[nodiscard]] std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                                 std::string_view scene);

/// The samples of a fixture scene, materialized; fails the case when they cannot be.
[[nodiscard]] std::optional<an::tests::fixtures::FixtureSamples> sceneSamples(std::string_view scene);

/// A new asset of a size and representation whose samples are `value(x, y, channel)` as fractions of full scale: a
/// star field for the real programs, a scene larger than DeepSNR's smallest.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
madeAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, std::uint32_t channels,
          an::SampleType sampleType,
          const std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t channel)>& value);

/// A copy of an asset's image in another representation, at corresponding full scales, under its own metadata.
[[nodiscard]] std::optional<an::consumer::ImageAsset>
convertedTo(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& source, an::SampleType target);

/// The metadata a well-described image carries: its object and exposure, a linear meaning, a solution, and the cards a
/// file stated.
[[nodiscard]] an::ImageMetadata describedMetadata();

/// A copy of an asset's image under other metadata.
[[nodiscard]] std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                                   const an::consumer::ImageAsset& source,
                                                                   const an::ImageMetadata& metadata);

/// The metadata an asset's image carries; fails the case when it cannot be read.
[[nodiscard]] std::optional<an::ImageMetadata> assetMetadata(const an::consumer::ImageAsset& asset);

/// A sample of a record or a result as a fraction of full scale: an unsigned integer of its largest value, a signed one
/// counted from its smallest, a floating sample as it is.
[[nodiscard]] double fractionOf(const an::tests::fixtures::FixtureSamples& samples, std::uint32_t x, std::uint32_t y,
                                std::uint32_t channel);

/// The largest absolute difference between two sets of samples of one shape, as fractions of full scale; infinite for
/// two shapes.
[[nodiscard]] double largestFractionDifference(const an::tests::fixtures::FixtureSamples& left,
                                               const an::tests::fixtures::FixtureSamples& right);

/// Runs the hand-off through the support directly - the stand-in as the program - over an asset, as a window's
/// operation does, under an operation the session admits; fails the case when the run is refused.
[[nodiscard]] an::Result<an::ui::processes::ExternalToolAnswers>
runSupport(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& source,
           const an::ui::processes::ExternalToolRequest& request, bool stopAtOnce = false);

} // namespace an::tests::processes::externaltools
