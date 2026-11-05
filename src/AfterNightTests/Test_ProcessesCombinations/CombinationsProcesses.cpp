// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The combination processes as the family's cases see them: how each window is opened - as the launcher's entry
/// point opens it, or with a body that stops its own run - the views it reads beside its target, how its widgets are
/// set to a mode, how its run is started and known to have landed, where its result lands, and the helpers that make
/// a target from a fixture scene and run an Apply through a window.

#include "Test_ProcessesCombinations/CombinationsTestSupport.h"

#include "processes/ChannelCompositionProcess.h"
#include "processes/ImageBlendProcess.h"
#include "processes/ImageCompareProcess.h"
#include "processes/LRGBCombinationProcess.h"
#include "processes/NBColorMapperProcess.h"
#include "processes/NBRGBCombinationProcess.h"
#include "processes/NarrowbandPaletteComposerPreviewGridWidget.h"
#include "processes/NarrowbandPaletteComposerProcess.h"
#include "processes/ProfiledStarmaskRecombinationProcess.h"
#include "processes/StarmaskRecombinationProcess.h"

#include "controls/ImageViewControl.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QTableWidget>

#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace an::tests::processes::combinations
{

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;
using an::tests::viewer::Channels;

/// A window whose body asks for its run's stop before the process's own body runs.
template <typename ProcessT, typename ParamsT> class StoppingWindow final : public ProcessT
{
public:
    using ProcessT::ProcessT;

protected:
    an::Status executeTypedProcessImpl(const an::ui::ProcessSource& source, an::ui::ProcessDestination& destination,
                                       const an::ui::ProcessRun& run, const ParamsT& params,
                                       an::ui::ExecutionScope& scope) override
    {
        scope.requestStop();
        return ProcessT::executeTypedProcessImpl(source, destination, run, params, scope);
    }
};

/// A window that says which history step an Apply of its current values records.
template <typename ProcessT, typename ParamsT> class DescribedWindow final : public ProcessT
{
public:
    using ProcessT::ProcessT;

    [[nodiscard]] an::ui::ProcessStep describeStep() const
    {
        const auto& typed = static_cast<const an::ui::TypedProcessControl<ParamsT>&>(*this);
        return this->describeTypedProcessStep(typed.getParams());
    }
};

/// A channel composition window whose run asks for its own stop once its operation is admitted.
class StoppingComposition final : public ChannelCompositionProcess
{
public:
    using ChannelCompositionProcess::ChannelCompositionProcess;

protected:
    [[nodiscard]] std::function<void(an::ui::ExecutionScope&)> admissionHook() const override
    {
        return [](an::ui::ExecutionScope& scope) { scope.requestStop(); };
    }
};

/// Turns a window's preview off, whichever name its builder gave the box.
void turnPreviewOff(QWidget& window)
{
    for (const char* name : {"preview", "previewCheckbox"})
    {
        if (auto* preview = window.findChild<QCheckBox*>(QString::fromLatin1(name)))
        {
            preview->setChecked(false);
        }
    }
}

/// A window opened over a target as the entry point opens it, kept by the case, with its preview off.
template <typename WindowT> [[nodiscard]] WindowHandle openOver(ImageViewControl* target)
{
    WindowHandle window(new WindowT(target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    turnPreviewOff(*window);
    return window;
}

/// A top-level window, opened over no view.
template <typename WindowT> [[nodiscard]] WindowHandle openTopLevel(ImageViewControl*)
{
    WindowHandle window(new WindowT(nullptr));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    turnPreviewOff(*window);
    return window;
}

void failUnknownMode(const char* process, std::string_view mode)
{
    ADD_FAILURE() << process << " has no mode " << std::string(mode);
}

[[nodiscard]] ImageRTPreviewControl* previewWindow(QWidget& window)
{
    auto* preview = dynamic_cast<ImageRTPreviewControl*>(&window);
    EXPECT_NE(preview, nullptr) << "the window is a real-time preview window";
    return preview;
}

[[nodiscard]] const ImageRTPreviewControl* previewWindow(const QWidget& window)
{
    return dynamic_cast<const ImageRTPreviewControl*>(&window);
}

// ---- The side views each process reads.

/// A view added to the context beside the target.
[[nodiscard]] SideView addSide(ProcessTestContext& context, const std::string& name, an::consumer::ImageAsset&& asset)
{
    SideView side{.name = name, .view = assetView(std::move(asset), name)};
    if (side.view != nullptr)
    {
        context.addViewControl(side.view.get());
    }
    return side;
}

/// The samples of a fixture scene beside a scene - `<scene>-second`, `<scene>-starless` - when it exists and is the
/// scene's size; nothing otherwise.
[[nodiscard]] std::optional<fixtures::FixtureSamples> besideScene(const std::string& sceneName, const char* suffix,
                                                                  const fixtures::FixtureSamples& scene)
{
    if (sceneName.empty() || fixtures::fixtureNamed(sceneName + suffix) == nullptr)
    {
        return std::nullopt;
    }
    std::optional<fixtures::FixtureSamples> beside = sceneSamples(sceneName + suffix);
    if (beside.has_value() && (beside->width() != scene.width() || beside->height() != scene.height()))
    {
        return std::nullopt;
    }
    return beside;
}

/// A second image of the scene's size and channels: the corpus's second scene, or the scene's values turned round.
[[nodiscard]] std::optional<an::consumer::ImageAsset> secondImage(const an::consumer::EngineSession& session,
                                                                  const fixtures::FixtureSamples& scene,
                                                                  const std::string& sceneName)
{
    if (const std::optional<fixtures::FixtureSamples> second = besideScene(sceneName, "-second", scene);
        second.has_value() && second->channelCount() == scene.channelCount())
    {
        return sceneAsset(session, *second);
    }
    return an::tests::viewer::makeAsset<float>(
        session, scene.width(), scene.height(), scene.channelCount() == 1u ? Channels::Grey : Channels::Rgb,
        [&scene](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(0.9 - 0.8 * scene.value(scene.width() - 1u - x, scene.height() - 1u - y, c));
        });
}

/// A grey image of the scene's size: the corpus's mono scene when it is the scene's size, or the scene's first plane.
[[nodiscard]] std::optional<an::consumer::ImageAsset> greyImage(const an::consumer::EngineSession& session,
                                                                const fixtures::FixtureSamples& scene)
{
    if (const fixtures::FixtureImage* mono = fixtures::fixtureNamed("scene-mono-float32"); mono != nullptr)
    {
        std::optional<fixtures::FixtureSamples> samples = sceneSamples("scene-mono-float32");
        if (samples.has_value() && samples->width() == scene.width() && samples->height() == scene.height())
        {
            return sceneAsset(session, *samples);
        }
    }
    return planeAsset(session, scene, 0u);
}

/// A starless image of the scene: the corpus's starless scene, or the scene's values scaled down.
[[nodiscard]] std::optional<an::consumer::ImageAsset> starlessImage(const an::consumer::EngineSession& session,
                                                                    const fixtures::FixtureSamples& scene,
                                                                    const std::string& sceneName)
{
    if (const std::optional<fixtures::FixtureSamples> starless = besideScene(sceneName, "-starless", scene);
        starless.has_value() && starless->channelCount() == scene.channelCount())
    {
        return sceneAsset(session, *starless);
    }
    return an::tests::viewer::makeAsset<float>(session, scene.width(), scene.height(),
                                               scene.channelCount() == 1u ? Channels::Grey : Channels::Rgb,
                                               [&scene](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                                   return static_cast<float>(0.8 * scene.value(x, y, c));
                                               });
}

std::vector<SideView> sidesSecond(ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                  const std::string& sceneName)
{
    std::vector<SideView> sides;
    std::optional<an::consumer::ImageAsset> second = secondImage(context.engineSession(), scene, sceneName);
    EXPECT_TRUE(second.has_value());
    if (second.has_value())
    {
        sides.push_back(addSide(context, "second", std::move(*second)));
    }
    return sides;
}

std::vector<SideView> sidesLuminance(ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                     const std::string&)
{
    std::vector<SideView> sides;
    std::optional<an::consumer::ImageAsset> luminance = greyImage(context.engineSession(), scene);
    EXPECT_TRUE(luminance.has_value());
    if (luminance.has_value())
    {
        sides.push_back(addSide(context, "luminance", std::move(*luminance)));
    }
    return sides;
}

/// The second scene's first plane as the red narrowband channel.
std::vector<SideView> sidesNarrowbandRed(ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                         const std::string& sceneName)
{
    std::vector<SideView> sides;
    const std::optional<fixtures::FixtureSamples> second = besideScene(sceneName, "-second", scene);
    std::optional<an::consumer::ImageAsset> red =
        planeAsset(context.engineSession(), second.has_value() ? *second : scene, 0u);
    EXPECT_TRUE(red.has_value());
    if (red.has_value())
    {
        sides.push_back(addSide(context, "red", std::move(*red)));
    }
    return sides;
}

/// The scene's planes, each a grey view named after its order.
std::vector<SideView> sidesPlanes(ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                  const std::vector<std::string>& names)
{
    std::vector<SideView> sides;
    for (std::size_t index = 0u; index < names.size(); ++index)
    {
        const std::uint32_t plane = static_cast<std::uint32_t>(index) < scene.channelCount()
                                        ? static_cast<std::uint32_t>(index)
                                        : scene.channelCount() - 1u;
        std::optional<an::consumer::ImageAsset> asset = planeAsset(context.engineSession(), scene, plane);
        EXPECT_TRUE(asset.has_value());
        if (asset.has_value())
        {
            sides.push_back(addSide(context, names[index], std::move(*asset)));
        }
    }
    return sides;
}

std::vector<SideView> sidesStarless(ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                    const std::string& sceneName)
{
    std::vector<SideView> sides;
    std::optional<an::consumer::ImageAsset> starless = starlessImage(context.engineSession(), scene, sceneName);
    EXPECT_TRUE(starless.has_value());
    if (starless.has_value())
    {
        sides.push_back(addSide(context, "starless", std::move(*starless)));
    }
    return sides;
}

// ---- The modes.

struct BlendModeLabel
{
    const char* mode;
    const char* label;
};

constexpr BlendModeLabel kBlendModes[] = {
    {"replace", "Replace"},
    {"darken-min", "Darken/Min"},
    {"multiply", "Multiply"},
    {"color-burn", "Color Burn"},
    {"linear-burn", "Linear Burn"},
    {"darker-color", "Darker Color"},
    {"lighten-max", "Lighten/Max"},
    {"screen", "Screen"},
    {"color-dodge", "Color Dodge"},
    {"linear-dodge-add", "Linear Dodge/Add"},
    {"lighter-color", "Lighter Color"},
    {"overlay", "Overlay"},
    {"soft-light", "Soft Light"},
    {"hard-light", "Hard Light"},
    {"vivid-light", "Vivid Light"},
    {"linear-light", "Linear Light"},
    {"pin-light", "Pin Light"},
    {"difference", "Difference"},
    {"exclusion", "Exclusion"},
    {"subtract", "Subtract"},
    {"divide", "Divide"},
    {"power", "Power"},
    {"arctan", "Arctan"},
    {"hue", "Hue"},
    {"saturation", "Saturation"},
    {"color", "Color"},
    {"lightness", "Lightness"},
};

void configureBlend(QWidget& window, const Targets& targets, std::string_view mode)
{
    // Every mode starts from the dialog's own values over the second image.
    selectView(window, "secondImageSelector", targets.side("second") ? "second" : "");
    setCurrentText(window, "filterSelector", QStringLiteral("None"));
    setDouble(window, "opacityValue", 1.0);
    setChecked(window, "preserveLightness", false);
    for (const char* name : {"srcImageBlackPointValue", "imageBlendBlackPointValue"})
    {
        setDouble(window, name, 0.0);
    }
    for (const char* name : {"srcImageHighlightsValue", "imageBlendHighlightsValue"})
    {
        setDouble(window, name, 1.0);
    }
    for (const char* name : {"srcImageMidtonesValue", "imageBlendMidtonesValue"})
    {
        setDouble(window, name, 0.5);
    }
    if (mode == "-" || mode == "replace")
    {
        setCurrentText(window, "blendMode", QStringLiteral("Replace"));
        return;
    }
    if (mode == "overlay-opacity-0.5")
    {
        setCurrentText(window, "blendMode", QStringLiteral("Overlay"));
        setDouble(window, "opacityValue", 0.5);
        return;
    }
    if (mode == "multiply-preserve-lightness")
    {
        setCurrentText(window, "blendMode", QStringLiteral("Multiply"));
        setChecked(window, "preserveLightness", true);
        return;
    }
    for (const BlendModeLabel& entry : kBlendModes)
    {
        if (mode == entry.mode)
        {
            setCurrentText(window, "blendMode", QString::fromLatin1(entry.label));
            return;
        }
    }
    failUnknownMode("the image blend", mode);
}

void configureCompare(QWidget& window, const Targets& targets, std::string_view mode)
{
    selectView(window, "secondImageSelector", targets.side("second") ? "second" : "");
    if (mode != "-")
    {
        failUnknownMode("the image comparison", mode);
    }
}

void configureLrgb(QWidget& window, const Targets& targets, std::string_view mode)
{
    selectView(window, "luminanceImageSelector", targets.side("luminance") ? "luminance" : "");
    if (mode == "-")
    {
        setDouble(window, "lightnessWeightValue", 1.0);
        setDouble(window, "redWeightValue", 1.0);
        setDouble(window, "greenWeightValue", 1.0);
        setDouble(window, "blueWeightValue", 1.0);
        setDouble(window, "lightnessMTFValue", 0.5);
        setDouble(window, "saturationMTFValue", 0.5);
        return;
    }
    if (mode == "lightness-midtones-0.3-saturation-midtones-0.4-weights")
    {
        setDouble(window, "lightnessWeightValue", 0.6);
        setDouble(window, "redWeightValue", 0.9);
        setDouble(window, "greenWeightValue", 1.0);
        setDouble(window, "blueWeightValue", 0.8);
        setDouble(window, "lightnessMTFValue", 0.3);
        setDouble(window, "saturationMTFValue", 0.4);
        return;
    }
    failUnknownMode("the LRGB combination", mode);
}

void configureNbrgb(QWidget& window, const Targets& targets, std::string_view mode)
{
    // The dialog's values: RGB bandwidth 100, every narrowband channel 7 nm at scale 1.2, the fit rejecting nothing
    // below 0 and above 0.92, no nonlinear rescale, the combination shown.
    setDouble(window, "rgbBandwidth", 100.0);
    for (const char* name : {"redBandwidth", "greenBandwidth", "blueBandwidth"})
    {
        setDouble(window, name, 7.0);
    }
    for (const char* name : {"redScale", "greenScale", "blueScale"})
    {
        setDouble(window, name, 1.2);
    }
    setDouble(window, "rejectLow", 0.0);
    setDouble(window, "rejectHigh", 0.92);
    setChecked(window, "nonLinearRescale", false);
    setIndex(window, "previewDisplayMode", 0);
    selectView(window, "redImageSelector", targets.side("red") ? "red" : "");
    selectView(window, "greenImageSelector", "");
    selectView(window, "blueImageSelector", "");
    if (mode != "-")
    {
        failUnknownMode("the NBRGB combination", mode);
    }
}

/// Removes every layer a mapper window holds, as the Remove button does.
void clearMapperLayers(QWidget& window)
{
    auto* table = widgetOf<QTableWidget>(window, "layersTable");
    auto* remove = widgetOf<QPushButton>(window, "removeLayerButton");
    if (table == nullptr || remove == nullptr)
    {
        return;
    }
    while (table->rowCount() > 0)
    {
        table->selectRow(0);
        remove->click();
    }
}

/// Adds a layer of a view at a hue, as the editor and the Add button do.
void addMapperLayer(QWidget& window, const std::string& view, double hueDegrees)
{
    // The editor edits the selected row as it changes; a new layer is composed with no row selected.
    if (auto* table = widgetOf<QTableWidget>(window, "layersTable"))
    {
        table->clearSelection();
        table->setCurrentCell(-1, -1);
    }
    selectView(window, "layerImageSelector", view);
    setChecked(window, "layerInclude", true);
    setDouble(window, "layerHue", hueDegrees);
    setDouble(window, "layerSaturation", 0.5);
    setDouble(window, "layerMidtones", 0.5);
    setDouble(window, "layerBlackpoint", 0.0);
    setIndex(window, "modificationMode", 0);
    if (auto* add = widgetOf<QPushButton>(window, "addLayerButton"))
    {
        add->click();
    }
}

void configureMapper(QWidget& window, const Targets& targets, std::string_view mode)
{
    // The target's first two planes at hues 0 and 180, saturation and midtones 0.5, nothing screened onto the output,
    // no background neutralization, no colourized layers.
    setChecked(window, "applyStfToCombinedOutput", false);
    setChecked(window, "applyPreviewStf", false);
    setChecked(window, "returnColorizedLayers", false);
    setChecked(window, "backgroundEnabled", false);
    clearMapperLayers(window);
    if (targets.side("first") != nullptr)
    {
        addMapperLayer(window, "first", 0.0);
    }
    if (targets.side("second") != nullptr)
    {
        addMapperLayer(window, "second", 180.0);
    }
    if (mode != "-")
    {
        failUnknownMode("the narrowband colour mapper", mode);
    }
}

void configurePalette(QWidget& window, const Targets& targets, std::string_view mode)
{
    selectView(window, "haSourceSelector", targets.side("ha") ? "ha" : "");
    selectView(window, "oiiiSourceSelector", targets.side("oiii") ? "oiii" : "");
    selectView(window, "siiSourceSelector", targets.side("sii") ? "sii" : "");
    selectView(window, "osc1SourceSelector", "");
    selectView(window, "osc2SourceSelector", "");
    setChecked(window, "linearInputStretch", true);
    setChecked(window, "resizeMismatched", true);
    setInt(window, "haIntensitySpin", 100);
    setInt(window, "oiiiIntensitySpin", 100);
    setInt(window, "siiIntensitySpin", 100);
    an::NarrowbandPalette palette = an::NarrowbandPalette::Sho;
    if (mode == "hoo")
    {
        palette = an::NarrowbandPalette::Hoo;
    }
    else if (mode == "foraxx")
    {
        palette = an::NarrowbandPalette::Foraxx;
    }
    else if (mode != "-")
    {
        failUnknownMode("the narrowband palette composer", mode);
    }
    // The grid widget is the palette scroll area's; it is no QObject of its own to be found by name.
    auto* scrollArea = widgetOf<QScrollArea>(window, "paletteGridScrollArea");
    auto* grid =
        scrollArea != nullptr
            ? dynamic_cast<an::ui::processes::NarrowbandPaletteComposerPreviewGridWidget*>(scrollArea->widget())
            : nullptr;
    ASSERT_NE(grid, nullptr) << "the window has a palette grid";
    grid->setSelectedPalette(palette);
}

void configureComposition(QWidget& window, const Targets& targets, std::string_view mode)
{
    selectView(window, "redChannel", targets.side("red") ? "red" : "");
    selectView(window, "greenChannel", targets.side("green") ? "green" : "");
    selectView(window, "blueChannel", targets.side("blue") ? "blue" : "");
    setChecked(window, "linearMatchCheckBox", mode == "linear-match");
    const char* space = "RGB";
    if (mode == "cie-lab")
    {
        space = "CIE L*a*b*";
    }
    else if (mode == "cie-lch")
    {
        space = "CIE L*c*h*";
    }
    else if (mode == "cie-xyz")
    {
        space = "CIE XYZ";
    }
    else if (mode == "hsl")
    {
        space = "HSL";
    }
    else if (mode == "hsv")
    {
        space = "HSV";
    }
    else if (mode == "hsi")
    {
        space = "HSI";
    }
    else if (mode == "ycbcr")
    {
        space = "YCbCr";
    }
    else if (mode != "-" && mode != "linear-match")
    {
        failUnknownMode("the channel composition", mode);
    }
    setCurrentText(window, "colorSpace", QString::fromLatin1(space));
}

void configureRecombination(QWidget& window, const Targets& targets, std::string_view mode)
{
    // The starless image becomes the window's target; the stars are the case's target.
    selectView(window, "starlessImage", targets.side("starless") ? "starless" : "");
    selectView(window, "starmaskImage", targets.target ? targets.target->getUniqueName() : "");
    selectView(window, "maskImage", "");
    setChecked(window, "invertMask", false);
    if (mode != "-")
    {
        failUnknownMode("the starmask recombination", mode);
    }
}

void configureProfiledRecombination(QWidget& window, const Targets& targets, std::string_view mode)
{
    selectView(window, "starlessImage", targets.side("starless") ? "starless" : "");
    selectView(window, "starmaskImage", targets.target ? targets.target->getUniqueName() : "");
    if (auto* profile = widgetOf<QLineEdit>(window, "starsProfile"))
    {
        profile->setText(QString());
    }
    setDouble(window, "scaleFactorValue", 1.0);
    setDouble(window, "tightenEdgesValue", 0.0);
    setChecked(window, "invertSelection", false);
    setCurrentText(window, "shapeSelector", QStringLiteral("Binary Areas"));
    if (mode != "-")
    {
        failUnknownMode("the profiled starmask recombination", mode);
    }
}

// ---- The table.

template <typename ProcessT, typename ParamsT>
CombinationsProcess typedProcess(const char* name, const char* action, const char* descriptorId, const char* className,
                                 const char* processName, const char* records, const char* stateOperation,
                                 bool topLevel,
                                 std::function<void(QWidget&, const Targets&, std::string_view)> configure)
{
    CombinationsProcess process;
    process.name = name;
    process.action = action;
    process.descriptorId = descriptorId;
    process.className = className;
    process.processName = processName;
    process.records = records;
    process.stateOperation = stateOperation;
    process.topLevel = topLevel;
    process.realtimePreview = true;
    process.open = topLevel ? &openTopLevel<ProcessT> : &openOver<ProcessT>;
    process.openStopping =
        topLevel ? &openTopLevel<StoppingWindow<ProcessT, ParamsT>> : &openOver<StoppingWindow<ProcessT, ParamsT>>;
    process.configure = configure;
    process.apply = [](QWidget& window) {
        if (ImageRTPreviewControl* preview = previewWindow(window))
        {
            preview->applyProcess();
        }
    };
    process.idle = [](const QWidget& window) {
        const ImageRTPreviewControl* preview = previewWindow(window);
        return preview == nullptr || preview->isProcessIdle();
    };
    process.committed = [](const QWidget& window) {
        const ImageRTPreviewControl* preview = previewWindow(window);
        return preview == nullptr ? 0u : preview->runTelemetry().runsCommitted;
    };
    process.describe = [configure, topLevel](const Targets& targets, std::string_view mode) {
        WindowHandle opened = topLevel ? openTopLevel<DescribedWindow<ProcessT, ParamsT>>(nullptr)
                                       : openOver<DescribedWindow<ProcessT, ParamsT>>(targets.target.get());
        configure(*opened, targets, mode);
        return static_cast<const DescribedWindow<ProcessT, ParamsT>&>(*opened).describeStep();
    };
    return process;
}

std::vector<CombinationsProcess> makeProcesses()
{
    std::vector<CombinationsProcess> processes;

    processes.push_back(typedProcess<ImageBlendProcess, ImageBlendParams>(
        "ImageBlend", "actionImage_Blend", "multi_image_stacking.actionImage_Blend", "ImageBlendProcess", "Image Blend",
        "image-blend", "image-blend", false, configureBlend));
    processes.back().scene = "scene-rgb-float32";
    processes.back().previewArea = true;
    processes.back().smooth = true;
    processes.back().keepsRepresentation = true;
    processes.back().openSides = sidesSecond;
    processes.back().changingMode = "multiply";
    processes.back().changingStep = "Multiply with second";

    processes.push_back(typedProcess<LRGBCombinationProcess, LRGBCombinationParams>(
        "LRGBCombination", "actionLRGB_Combination", "color.actionLRGB_Combination", "LRGBCombinationProcess",
        "LRGB Combination", "lrgb-combination", "lrgb-combination", false, configureLrgb));
    processes.back().scene = "scene-rgb-float32";
    processes.back().previewArea = true;
    processes.back().smooth = true;
    processes.back().keepsRepresentation = true;
    processes.back().openSides = sidesLuminance;
    processes.back().changingMode = "-";
    processes.back().changingStep = "Luminance: 'luminance', lightness weight 1.00, channel weights R 1.00 G 1.00 B "
                                    "1.00, lightness midtones 0.50, saturation midtones 0.50";

    processes.push_back(typedProcess<NBRGBCombinationProcess, NBRGBCombinationProcessParams>(
        "NBRGBCombination", "actionNBRGB_Combination", "color.actionNBRGB_Combination", "NBRGBCombinationProcess",
        "NBRGB Combination", "nbrgb-combination", "", false, configureNbrgb));
    processes.back().scene = "scene-rgb-float32";
    processes.back().landing = ResultLanding::NewView;
    processes.back().smooth = true;
    processes.back().stillImageOnly = true;
    processes.back().keepsRepresentation = true;
    processes.back().openSides = sidesNarrowbandRed;
    processes.back().changingMode = "-";
    processes.back().changingStep =
        "Created NBRGB combination from RGB (100.0 nm) and narrowband channels R: 'red' 7.0 nm x1.20; fit 0.000 to "
        "0.920";

    processes.push_back(typedProcess<NBColorMapperProcess, NBColorMapperProcessParams>(
        "NBColorMapper", "actionNBColorMapper", "color.actionNBColorMapper", "NBColorMapperProcess", "NB Color Mapper",
        "nbcolor-mapper", "nb-color-mapper", false, configureMapper));
    processes.back().scene = "scene-rgb-float32";
    processes.back().landing = ResultLanding::NewView;
    processes.back().stillImageOnly = true;
    processes.back().openSides = [](ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                    const std::string&) { return sidesPlanes(context, scene, {"first", "second"}); };
    processes.back().changingMode = "-";

    processes.push_back(typedProcess<NarrowbandPaletteComposerProcess, NarrowbandPaletteComposerProcessParams>(
        "NarrowbandPaletteComposer", "actionNarrowbandPaletteComposer", "color.actionNarrowbandPaletteComposer",
        "NarrowbandPaletteComposerProcess", "Narrowband Palette Composer", "narrowband-palette-composer",
        "narrowband-palette-composer", true, configurePalette));
    processes.back().scene = "scene-rgb-float32";
    processes.back().landing = ResultLanding::NewView;
    processes.back().previewBox = false;
    processes.back().openSides = [](ProcessTestContext& context, const fixtures::FixtureSamples& scene,
                                    const std::string&) { return sidesPlanes(context, scene, {"ha", "oiii", "sii"}); };
    processes.back().changingMode = "hoo";

    processes.push_back(typedProcess<StarmaskRecombinationProcess, StarmaskRecombinationParams>(
        "StarmaskRecombination", "actionStarmask_Recombination", "multi_image_stacking.actionStarmask_Recombination",
        "StarmaskRecombinationProcess", "Starmask Recombination", "starmask-recombination", "starmask-recombination",
        true, configureRecombination));
    processes.back().scene = "stars-rgb-float32";
    processes.back().landing = ResultLanding::NewView;
    processes.back().smooth = true;
    processes.back().openSides = sidesStarless;
    processes.back().changingMode = "-";

    processes.push_back(typedProcess<ProfiledStarmaskRecombinationProcess, ProfiledStarmaskRecombinationParams>(
        "ProfiledStarmaskRecombination", "actionProfiled_Starmask_Recombination",
        "multi_image_stacking.actionProfiled_Starmask_Recombination", "ProfiledStarmaskRecombinationProcess",
        "Profiled Starmask Recombination", "", "profiled-starmask-recombination", true,
        configureProfiledRecombination));
    processes.back().scene = "stars-rgb-float32";
    processes.back().landing = ResultLanding::NewView;
    processes.back().smooth = true;
    processes.back().openSides = sidesStarless;
    processes.back().changingMode = "-";

    processes.push_back(typedProcess<ImageCompareProcess, std::monostate>(
        "ImageCompare", "actionImage_Compare", "multi_image_stacking.actionImage_Compare", "ImageCompareProcess",
        "Image Compare", "", "", false, configureCompare));
    processes.back().scene = "scene-rgb-float32";
    processes.back().leavesTarget = true;
    processes.back().openSides = sidesSecond;
    processes.back().changingMode = "-";
    processes.back().changingStep = "";

    // The channel composition is a top-level window of its own, not a preview window: Ok runs it to a new image.
    CombinationsProcess composition;
    composition.name = "ChannelComposition";
    composition.action = "actionChannelComposition";
    composition.descriptorId = "color.actionChannelComposition";
    composition.className = "ChannelCompositionProcess";
    composition.processName = "Channel Composition";
    composition.records = "channel-composition";
    composition.stateOperation = "";
    composition.landing = ResultLanding::NewView;
    composition.topLevel = true;
    composition.realtimePreview = false;
    composition.scene = "scene-rgb-float32";
    composition.changingMode = "-";
    composition.openSides = [](ProcessTestContext& context, const fixtures::FixtureSamples& scene, const std::string&) {
        return sidesPlanes(context, scene, {"red", "green", "blue"});
    };
    composition.open = &openTopLevel<ChannelCompositionProcess>;
    composition.openStopping = &openTopLevel<StoppingComposition>;
    composition.previewBox = false;
    composition.configure = configureComposition;
    composition.apply = [](QWidget& window) {
        if (auto* process = dynamic_cast<ChannelCompositionProcess*>(&window))
        {
            process->executeChannelComposition();
        }
    };
    composition.idle = [](const QWidget& window) {
        const auto* process = dynamic_cast<const ChannelCompositionProcess*>(&window);
        return process == nullptr || !process->isRunning();
    };
    composition.committed = [](const QWidget& window) {
        const auto* process = dynamic_cast<const ChannelCompositionProcess*>(&window);
        return process == nullptr ? 0u : process->runsPublished();
    };
    processes.push_back(std::move(composition));

    return processes;
}

} // namespace

void WindowDeleter::operator()(QWidget* window) const
{
    if (window == nullptr)
    {
        return;
    }
    if (auto* preview = dynamic_cast<ImageRTPreviewControl*>(window))
    {
        (void)pumpUntil([preview]() { return preview->isProcessIdle(); });
    }
    else if (auto* composition = dynamic_cast<ChannelCompositionProcess*>(window))
    {
        composition->requestStop();
        (void)pumpUntil([composition]() { return !composition->isRunning(); });
    }
    delete window;
    QCoreApplication::processEvents();
}

ImageViewControl* Targets::side(std::string_view name) const
{
    for (const SideView& view : sides)
    {
        if (view.name == name)
        {
            return view.view.get();
        }
    }
    return nullptr;
}

const std::vector<CombinationsProcess>& combinationsProcesses()
{
    static const std::vector<CombinationsProcess> processes = makeProcesses();
    return processes;
}

const CombinationsProcess* processOfAction(std::string_view action)
{
    for (const CombinationsProcess& process : combinationsProcesses())
    {
        if (action == process.action)
        {
            return &process;
        }
    }
    return nullptr;
}

const CombinationsProcess& processNamed(std::string_view name)
{
    for (const CombinationsProcess& candidate : combinationsProcesses())
    {
        if (std::string_view(candidate.name) == name)
        {
            return candidate;
        }
    }
    ADD_FAILURE() << "no process named " << std::string(name);
    return combinationsProcesses().front();
}

std::vector<std::size_t> processesWithRecords()
{
    return processesWhere(
        [](const CombinationsProcess& process) { return std::string_view(process.records).size() > 0u; });
}

std::vector<std::size_t> processesWhere(const std::function<bool(const CombinationsProcess&)>& holds)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < combinationsProcesses().size(); ++index)
    {
        if (holds(combinationsProcesses()[index]))
        {
            indices.push_back(index);
        }
    }
    return indices;
}

void setInt(QWidget& window, const char* name, int value)
{
    if (auto* box = widgetOf<QSpinBox>(window, name))
    {
        box->setValue(value);
    }
}

void setDouble(QWidget& window, const char* name, double value)
{
    if (auto* box = widgetOf<QDoubleSpinBox>(window, name))
    {
        box->setValue(value);
    }
}

void setChecked(QWidget& window, const char* name, bool checked)
{
    if (auto* box = widgetOf<QCheckBox>(window, name))
    {
        box->setChecked(checked);
    }
}

void setIndex(QWidget& window, const char* name, int index)
{
    if (auto* box = widgetOf<QComboBox>(window, name))
    {
        box->setCurrentIndex(index);
    }
}

void setCurrentText(QWidget& window, const char* name, const QString& text)
{
    if (auto* box = widgetOf<QComboBox>(window, name))
    {
        const int index = box->findText(text);
        EXPECT_GE(index, 0) << name << " offers " << text.toStdString();
        if (index >= 0)
        {
            box->setCurrentIndex(index);
        }
    }
}

void selectView(QWidget& window, const char* name, const std::string& view)
{
    if (auto* box = widgetOf<QComboBox>(window, name))
    {
        const QString text = view.empty() ? NO_IMAGE_STR : QString::fromStdString(view);
        const int index = box->findText(text);
        EXPECT_GE(index, 0) << name << " offers " << text.toStdString();
        if (index >= 0)
        {
            box->setCurrentIndex(index);
        }
    }
}

std::optional<fixtures::FixtureSamples> sceneSamples(std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << "no fixture scene " << std::string(scene);
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session,
                                                   const fixtures::FixtureSamples& scene)
{
    return an::tests::viewer::makeAsset<float>(session, scene.width(), scene.height(),
                                               scene.channelCount() == 1u ? Channels::Grey : Channels::Rgb,
                                               [&scene](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
                                                   return static_cast<float>(scene.value(x, y, c));
                                               });
}

std::optional<an::consumer::ImageAsset> planeAsset(const an::consumer::EngineSession& session,
                                                   const fixtures::FixtureSamples& scene, std::uint32_t plane)
{
    const std::uint32_t chosen = (std::min)(plane, scene.channelCount() - 1u);
    return an::tests::viewer::makeAsset<float>(session, scene.width(), scene.height(), Channels::Grey,
                                               [&scene, chosen](std::uint32_t x, std::uint32_t y, std::uint32_t) {
                                                   return static_cast<float>(scene.value(x, y, chosen));
                                               });
}

ViewHandle sceneView(const an::consumer::EngineSession& session, std::string_view scene, const std::string& name)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << "no fixture scene " << std::string(scene);
    if (image == nullptr)
    {
        return ViewHandle();
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return ViewHandle();
    }
    an::Result<an::consumer::ImageAsset> asset = adapter::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return ViewHandle();
    }
    return assetView(std::move(*asset), name);
}

ViewHandle assetView(an::consumer::ImageAsset&& asset, const std::string& name)
{
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(asset), an::ui::DocumentNames{.displayName = QString::fromStdString(name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return ViewHandle();
    }
    ViewHandle view = makeTargetView(name, std::move(*document));
    // The view's opening work - its presentation and the copies it admits - lands before the next image is made:
    // a session held to a few images refuses an input made while that work still holds its share.
    if (view != nullptr)
    {
        EXPECT_TRUE(settleView(*view)) << name << " settles";
    }
    return view;
}

Targets openTargets(ProcessTestContext& context, const CombinationsProcess& process, an::consumer::ImageAsset&& asset,
                    const std::string& name, const std::string& sceneName)
{
    Targets made;
    made.target = assetView(std::move(asset), name);
    EXPECT_NE(made.target, nullptr);
    if (made.target == nullptr)
    {
        return made;
    }
    // A top-level window finds its target among the context's views; a single-image one is handed it.
    if (process.topLevel)
    {
        context.addViewControl(made.target.get());
    }
    if (process.openSides)
    {
        const std::optional<fixtures::FixtureSamples> samples = documentSamples(made.target->document());
        EXPECT_TRUE(samples.has_value());
        if (samples.has_value())
        {
            made.sides = process.openSides(context, unitSamples(*samples), sceneName);
        }
    }
    return made;
}

Targets openScene(ProcessTestContext& context, const CombinationsProcess& process)
{
    const std::optional<fixtures::FixtureSamples> scene = sceneSamples(process.scene);
    if (!scene.has_value())
    {
        return {};
    }
    std::optional<an::consumer::ImageAsset> asset = sceneAsset(context.engineSession(), *scene);
    EXPECT_TRUE(asset.has_value());
    return asset.has_value() ? openTargets(context, process, std::move(*asset), "scene", process.scene) : Targets{};
}

void dropView(ProcessTestContext& context, ViewHandle& view)
{
    if (view == nullptr)
    {
        return;
    }
    context.removeViewControl(view.get(), false);
    view.reset();
    QCoreApplication::processEvents();
}

void dropTargets(ProcessTestContext& context, Targets& targets)
{
    for (SideView& side : targets.sides)
    {
        dropView(context, side.view);
    }
    targets.sides.clear();
    dropView(context, targets.target);
}

double unitValueOf(double code, fixtures::FixtureSampleType type)
{
    switch (type)
    {
    case fixtures::FixtureSampleType::UInt8:
        return code / 255.0;
    case fixtures::FixtureSampleType::Int8:
        return (code + 128.0) / 255.0;
    case fixtures::FixtureSampleType::UInt16:
        return code / 65535.0;
    case fixtures::FixtureSampleType::Int16:
        return (code + 32768.0) / 65535.0;
    case fixtures::FixtureSampleType::UInt32:
        return code / 4294967295.0;
    case fixtures::FixtureSampleType::Int32:
        return (code + 2147483648.0) / 4294967295.0;
    default:
        return code;
    }
}

fixtures::FixtureSamples unitSamples(const fixtures::FixtureSamples& samples)
{
    fixtures::FixtureSamples unit(samples.width(), samples.height(), samples.channelCount(),
                                  fixtures::FixtureSampleType::Float32);
    for (std::uint32_t c = 0u; c < samples.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                unit.setValue(x, y, c, unitValueOf(samples.value(x, y, c), samples.sampleType()));
            }
        }
    }
    return unit;
}

std::optional<fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    EXPECT_NE(asset, nullptr);
    if (asset == nullptr)
    {
        return std::nullopt;
    }
    return samplesOf(*asset);
}

std::optional<fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

bool settleWindow(const CombinationsProcess& process, const QWidget& window)
{
    if (!pumpUntil([&process, &window]() { return process.idle(window); }))
    {
        return false;
    }
    pumpQuietly();
    return process.idle(window);
}

bool applyAndSettle(const CombinationsProcess& process, QWidget& window, const Targets& targets)
{
    process.apply(window);
    if (!settleWindow(process, window))
    {
        return false;
    }
    if (targets.target != nullptr && !settleView(*targets.target))
    {
        return false;
    }
    for (const SideView& side : targets.sides)
    {
        if (side.view != nullptr && !settleView(*side.view))
        {
            return false;
        }
    }
    return true;
}

std::vector<ImageViewControl*> viewsMadeSince(const ProcessTestContext& context, std::size_t count)
{
    std::vector<ImageViewControl*> views;
    const std::vector<ImageViewControl*>& made = context.madeViews;
    for (std::size_t index = count; index < made.size(); ++index)
    {
        views.push_back(made[index]);
    }
    return views;
}

const an::ui::ImageDocument* resultDocument(const ProcessTestContext& context, const CombinationsProcess& process,
                                            const Targets& targets, std::size_t viewsBefore)
{
    if (process.landing == ResultLanding::InPlace)
    {
        return targets.target != nullptr ? &targets.target->document() : nullptr;
    }
    const std::vector<ImageViewControl*> made = viewsMadeSince(context, viewsBefore);
    return made.size() == 1u ? &made.front()->document() : nullptr;
}

std::optional<fixtures::FixtureSamples> applyAndRead(ProcessTestContext& context, const CombinationsProcess& process,
                                                     QWidget& window, const Targets& targets)
{
    const std::size_t before = context.madeViews.size();
    EXPECT_TRUE(applyAndSettle(process, window, targets));
    if (process.landing == ResultLanding::InPlace)
    {
        return targets.target != nullptr ? documentSamples(targets.target->document()) : std::nullopt;
    }
    const std::vector<ImageViewControl*> made = viewsMadeSince(context, before);
    EXPECT_EQ(made.size(), 1u) << "the result lands in one new view";
    if (made.size() != 1u)
    {
        return std::nullopt;
    }
    EXPECT_TRUE(settleView(*made.front()));
    return documentSamples(made.front()->document());
}

std::vector<fixtures::Expectation> recordsOf(std::string_view records)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return {};
    }
    std::optional<std::vector<fixtures::Expectation>> read =
        fixtures::readExpectationFile(*directory / "behavior" / (std::string(records) + ".expectations"), error);
    EXPECT_TRUE(read.has_value()) << error;
    return read.has_value() ? std::move(*read) : std::vector<fixtures::Expectation>{};
}

std::string modeOf(const fixtures::Expectation& record)
{
    const std::size_t dot = record.id.find('.');
    const std::size_t slash = record.id.find('/');
    if (dot == std::string::npos || slash == std::string::npos || slash < dot)
    {
        return std::string();
    }
    const std::string mode = record.id.substr(dot + 1u, slash - dot - 1u);
    return mode == "default" ? std::string("-") : mode;
}

double largestDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = left.value(x, y, channel);
                const double b = right.value(x, y, channel);
                if (std::isnan(a) && std::isnan(b))
                {
                    continue;
                }
                largest = (std::max)(largest, std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity()
                                                                             : std::abs(a - b));
            }
        }
    }
    return largest;
}

} // namespace an::tests::processes::combinations
