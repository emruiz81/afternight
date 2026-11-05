// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The eight pointwise and tone processes as the family's cases see them: how each window is opened - as the launcher's
/// entry point opens it, or with a body that stops its own run - how its widgets are set to a mode its behaviour
/// records hold, and the helpers that make a target from a fixture scene and run an Apply through a window.

#include "Test_ProcessesPointwise/PointwiseTestSupport.h"

#include "processes/AdjustBrightnessAndContrastProcess.h"
#include "processes/AdjustHSLProcess.h"
#include "processes/AdjustLChProcess.h"
#include "processes/ArcSinhStretchProcess.h"
#include "processes/ColorSaturationProcess.h"
#include "processes/ColorizeProcess.h"
#include "processes/ExposureCorrectionProcess.h"
#include "processes/GammaCorrectionProcess.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <QCoreApplication>
#include <QString>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace an::tests::processes::pointwise
{

namespace
{

namespace fixtures = an::tests::fixtures;
namespace adapter = an::tests::fixtures::engine;

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

/// A window whose body a case runs itself, over destinations it makes.
template <typename ProcessT, typename ParamsT> class BodyWindow final : public ProcessT
{
public:
    using ProcessT::ProcessT;

    /// Runs the body with the window's values as an Apply runs it over the target: into a copy of the target's image,
    /// and into a destination of its shape that starts holding none of it, every byte of it a marker.
    [[nodiscard]] BodyOverDestinations runOverDestinations(ImageViewControl& target)
    {
        BodyOverDestinations result;
        const auto& typed = static_cast<const an::ui::TypedProcessControl<ParamsT>&>(*this);
        const ParamsT params = typed.getParams();
        result.coverage = this->typedProcessBodyCoverage(params);
        const an::consumer::EngineSession* session = this->engineSession();
        an::Result<an::consumer::AssetReadLease> lease = target.document().read();
        if (session == nullptr || !lease.hasValue())
        {
            ADD_FAILURE() << "the body has no session or cannot read its target";
            return result;
        }
        an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
            *session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply},
            an::ui::ExecutionObserver{});
        if (!scope.hasValue())
        {
            ADD_FAILURE() << scope.status().message();
            return result;
        }
        const an::ui::ProcessSource source = an::ui::ProcessSource::of(*lease);
        const an::ui::ProcessRun run{.kind = an::ui::ProcessRunKind::Apply,
                                     .channel = this->selectedChannelScope(),
                                     .coverage = result.coverage};
        const auto runInto = [&](an::Result<an::ui::ProcessDestination> made,
                                 bool marked) -> std::optional<fixtures::FixtureSamples> {
            if (!made.hasValue())
            {
                ADD_FAILURE() << made.status().message();
                return std::nullopt;
            }
            if (marked)
            {
                made->asset().visitWrite([](auto&& access) {
                    std::memset(access.view().data(), 0xA5,
                                static_cast<std::size_t>(access.view().layout().totalBytes()));
                });
            }
            const an::Status ran = this->executeTypedProcessImpl(source, *made, run, params, *scope);
            if (ran.failed())
            {
                ADD_FAILURE() << ran.message();
                return std::nullopt;
            }
            an::Result<an::consumer::ImageAsset> written = made->release();
            if (!written.hasValue())
            {
                ADD_FAILURE() << written.status().message();
                return std::nullopt;
            }
            std::string error;
            std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(*written, error);
            EXPECT_TRUE(samples.has_value()) << error;
            return samples;
        };
        result.overCopy =
            runInto(an::ui::ProcessDestination::copyOf(*session, scope->engine(), scope->execution(), source), false);
        result.overUnwritten =
            runInto(an::ui::ProcessDestination::unwrittenFor(*session, scope->engine(), source), true);
        scope->finish(an::Status::success());
        return result;
    }
};

/// A window opened over a target as the entry point opens it, kept by the case, with its preview off.
template <typename WindowT> [[nodiscard]] WindowHandle openWindow(ImageViewControl& target)
{
    WindowHandle window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    if (auto* preview = window->template findChild<QCheckBox*>(QStringLiteral("preview")))
    {
        preview->setChecked(false);
    }
    return window;
}

/// A widget of a window by its object name; fails the case when the window has none.
template <typename WidgetT> [[nodiscard]] WidgetT* widgetOf(QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << "the window has no " << name;
    return widget;
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

/// The channel choice a gamma record's mode names, as the corpus spells it after `channel-`.
struct ChannelMode
{
    const char* mode;
    an::ChannelScope scope;
};

constexpr ChannelMode kChannelModes[] = {
    {"channel-rgb", an::ChannelScope::Whole},
    {"channel-grayscale", an::ChannelScope::Whole},
    {"channel-red", an::ChannelScope::Red},
    {"channel-green", an::ChannelScope::Green},
    {"channel-blue", an::ChannelScope::Blue},
    {"channel-cie-l", an::ChannelScope::CieLightness},
    {"channel-cie-a", an::ChannelScope::CieA},
    {"channel-cie-b", an::ChannelScope::CieB},
    {"channel-cie-c", an::ChannelScope::CieChroma},
    {"channel-cie-h", an::ChannelScope::CieHue},
    {"channel-cie-x", an::ChannelScope::CieX},
    {"channel-cie-y", an::ChannelScope::CieY},
    {"channel-cie-z", an::ChannelScope::CieZ},
    {"channel-hsl-hue", an::ChannelScope::HslHue},
    {"channel-hsl-saturation", an::ChannelScope::HslSaturation},
    {"channel-hsl-lightness", an::ChannelScope::HslLightness},
    {"channel-hsv-hue", an::ChannelScope::HsvHue},
    {"channel-hsv-saturation", an::ChannelScope::HsvSaturation},
    {"channel-hsv-value", an::ChannelScope::HsvValue},
    {"channel-ycbcr-luma", an::ChannelScope::YCbCrLuma},
    {"channel-ycbcr-blue-chroma", an::ChannelScope::YCbCrBlueChroma},
    {"channel-ycbcr-red-chroma", an::ChannelScope::YCbCrRedChroma},
};

/// Selects the channel a mode names in a window's selector, by the scope its item carries.
void selectChannel(QWidget& window, an::ChannelScope scope)
{
    auto* selector = widgetOf<QComboBox>(window, "channelSelector");
    if (selector == nullptr || selector->count() <= 1)
    {
        // A grey target's selector offers the whole image alone.
        return;
    }
    const int index = selector->findText(an::ui::processChannelLabel(scope));
    ASSERT_GE(index, 0) << "the selector does not offer " << an::ui::processChannelLabel(scope).toStdString();
    selector->setCurrentIndex(index);
}

void failUnknownMode(const char* process, std::string_view mode)
{
    ADD_FAILURE() << process << " has no mode " << std::string(mode);
}

void configureBrightness(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setInt(window, "brightnessValue", 0);
        setInt(window, "contrastValue", 0);
    }
    else if (mode == "brightness-40-contrast-25")
    {
        setInt(window, "brightnessValue", 40);
        setInt(window, "contrastValue", 25);
    }
    else if (mode == "brightness-minus-60-contrast-minus-30")
    {
        setInt(window, "brightnessValue", -60);
        setInt(window, "contrastValue", -30);
    }
    else
    {
        failUnknownMode("brightness and contrast", mode);
    }
}

void configureGamma(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setDouble(window, "gammaValue", 1.0);
        return;
    }
    if (mode == "gamma-2.2")
    {
        setDouble(window, "gammaValue", 2.2);
        return;
    }
    if (mode == "gamma-0.5")
    {
        setDouble(window, "gammaValue", 0.5);
        return;
    }
    for (const ChannelMode& channel : kChannelModes)
    {
        if (mode == channel.mode)
        {
            setDouble(window, "gammaValue", 2.2);
            selectChannel(window, channel.scope);
            return;
        }
    }
    failUnknownMode("gamma correction", mode);
}

void configureExposure(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setDouble(window, "exposureValue", 0.0);
    }
    else if (mode == "exposure-1.5")
    {
        setDouble(window, "exposureValue", 1.5);
    }
    else if (mode == "exposure-minus-1")
    {
        setDouble(window, "exposureValue", -1.0);
    }
    else
    {
        failUnknownMode("exposure correction", mode);
    }
}

void configureHsl(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setInt(window, "hueValue", 0);
        setInt(window, "saturationValue", 0);
        setInt(window, "lightnessValue", 0);
    }
    else if (mode == "hue-30-saturation-40-lightness-minus-10")
    {
        setInt(window, "hueValue", 30);
        setInt(window, "saturationValue", 40);
        setInt(window, "lightnessValue", -10);
    }
    else if (mode == "hue-minus-90-saturation-minus-50-lightness-20")
    {
        setInt(window, "hueValue", -90);
        setInt(window, "saturationValue", -50);
        setInt(window, "lightnessValue", 20);
    }
    else
    {
        failUnknownMode("the HSL adjustment", mode);
    }
}

void configureLch(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setInt(window, "lightnessValue", 0);
        setInt(window, "chromaValue", 0);
        setInt(window, "hueValue", 0);
    }
    else if (mode == "lightness-10-chroma-30-hue-minus-20")
    {
        setInt(window, "lightnessValue", 10);
        setInt(window, "chromaValue", 30);
        setInt(window, "hueValue", -20);
    }
    else
    {
        failUnknownMode("the LCh adjustment", mode);
    }
}

void configureArcsinh(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setDouble(window, "stretchFactor", 0.0);
        setDouble(window, "blackPoint", 0.0);
        setChecked(window, "humanWeightedLuminance", true);
    }
    else if (mode == "stretch-0-black-point-0.001")
    {
        setDouble(window, "stretchFactor", 0.0);
        setDouble(window, "blackPoint", 0.001);
        setChecked(window, "humanWeightedLuminance", true);
    }
    else if (mode == "stretch-50")
    {
        setDouble(window, "stretchFactor", 50.0);
        setDouble(window, "blackPoint", 0.001);
        setChecked(window, "humanWeightedLuminance", true);
    }
    else if (mode == "stretch-50-even-luminance")
    {
        setDouble(window, "stretchFactor", 50.0);
        setDouble(window, "blackPoint", 0.001);
        setChecked(window, "humanWeightedLuminance", false);
    }
    else
    {
        failUnknownMode("the arcsinh stretch", mode);
    }
}

void configureSaturation(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setDouble(window, "amountValue", 0.0);
        setDouble(window, "bgFactorValue", 1.0);
        setIndex(window, "hueSelector", 0);
    }
    else if (mode == "amount-0.5")
    {
        setDouble(window, "amountValue", 0.5);
        setDouble(window, "bgFactorValue", 1.0);
        setIndex(window, "hueSelector", 0);
    }
    else if (mode == "amount-0.5-yellow-green-to-green-cyan")
    {
        setDouble(window, "amountValue", 0.5);
        setDouble(window, "bgFactorValue", 1.0);
        setIndex(window, "hueSelector", 3);
    }
    else if (mode == "amount-0.5-background-factor-2")
    {
        setDouble(window, "amountValue", 0.5);
        setDouble(window, "bgFactorValue", 2.0);
        setIndex(window, "hueSelector", 0);
    }
    else
    {
        failUnknownMode("the colour saturation", mode);
    }
}

void configureColorize(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setDouble(window, "hueValue", 0.0);
        setDouble(window, "saturationValue", 1.0);
        setIndex(window, "colorizeMethod", 0);
        setChecked(window, "useLightness", false);
    }
    else if (mode == "hue-200-saturation-0.6-hsi")
    {
        setDouble(window, "hueValue", 200.0);
        setDouble(window, "saturationValue", 0.6);
        setIndex(window, "colorizeMethod", 1);
        setChecked(window, "useLightness", false);
    }
    else if (mode == "hue-200-saturation-0.6-lightness")
    {
        setDouble(window, "hueValue", 200.0);
        setDouble(window, "saturationValue", 0.6);
        setIndex(window, "colorizeMethod", 0);
        setChecked(window, "useLightness", true);
    }
    else
    {
        failUnknownMode("the colorization", mode);
    }
}

template <typename ProcessT, typename ParamsT>
PointwiseProcess describe(const char* name, const char* action, const char* descriptorId, const char* className,
                          const char* records, const char* stateOperation, bool colourOnly, bool perChannel,
                          const char* changingMode, const char* changingStep,
                          std::function<void(ImageRTPreviewControl&, std::string_view)> configure)
{
    PointwiseProcess process;
    process.name = name;
    process.action = action;
    process.descriptorId = descriptorId;
    process.className = className;
    process.records = records;
    process.stateOperation = stateOperation;
    process.colourOnly = colourOnly;
    process.perChannel = perChannel;
    process.changingMode = changingMode;
    process.open = [](ImageViewControl& target) { return openWindow<ProcessT>(target); };
    process.openStopping = [](ImageViewControl& target) {
        return openWindow<StoppingWindow<ProcessT, ParamsT>>(target);
    };
    process.configure = configure;
    process.describe = [configure](ImageViewControl& target, std::string_view mode) {
        WindowHandle opened = openWindow<DescribedWindow<ProcessT, ParamsT>>(target);
        configure(*opened, mode);
        return static_cast<const DescribedWindow<ProcessT, ParamsT>&>(*opened).describeStep();
    };
    process.runBodyOverDestinations = [configure](ImageViewControl& target, std::string_view mode) {
        WindowHandle opened = openWindow<BodyWindow<ProcessT, ParamsT>>(target);
        configure(*opened, mode);
        return static_cast<BodyWindow<ProcessT, ParamsT>&>(*opened).runOverDestinations(target);
    };
    process.changingStep = changingStep;
    // The name is the class's own, which the registry's descriptor states too.
    process.processName = nullptr;
    return process;
}

std::vector<PointwiseProcess> makeProcesses()
{
    std::vector<PointwiseProcess> processes;
    processes.push_back(describe<AdjustBrightnessAndContrastProcess, AdjustBrightnessAndContrastParams>(
        "BrightnessContrast", "actionAdjustBrightness_Contrast", "tone_brightness.actionAdjustBrightness_Contrast",
        "AdjustBrightnessAndContrastProcess", "brightness-and-contrast", "brightness-contrast", false, true,
        "brightness-40-contrast-25", "Adjust brightness to 40 and contrast to 25 in channel: RGB",
        configureBrightness));
    processes.back().processName = "Adjust Brightness And Contrast";
    processes.push_back(describe<GammaCorrectionProcess, GammaCorrectionParams>(
        "Gamma", "actionGamma_Correction", "tone_brightness.actionGamma_Correction", "GammaCorrectionProcess",
        "gamma-correction", "gamma-correction", false, true, "gamma-2.2", "Adjust gamma to 2.2 in channel: RGB",
        configureGamma));
    processes.back().processName = "Gamma Correction";
    processes.push_back(describe<ExposureCorrectionProcess, ExposureCorrectionParams>(
        "Exposure", "actionExposure", "tone_brightness.actionExposure", "ExposureCorrectionProcess",
        "exposure-correction", "exposure-correction", false, true, "exposure-1.5",
        "Adjust exposure to 1.5 in channel: RGB", configureExposure));
    processes.back().processName = "Exposure Correction";
    processes.push_back(describe<ArcSinhStretchProcess, ArcSinhStretchParams>(
        "ArcSinh", "actionArcSinh_Stretch", "tone_brightness.actionArcSinh_Stretch", "ArcSinhStretchProcess",
        "arcsinh-stretch", "arcsinh-stretch", false, false, "stretch-50",
        "Stretch 50, Black Point 0.001, Human Weight true", configureArcsinh));
    processes.back().processName = "ArcSinhStretch";
    processes.push_back(describe<ColorizeProcess, ColorizeParams>(
        "Colorize", "actionColorize", "color.actionColorize", "ColorizeProcess", "colorize", "colorize", false, false,
        "hue-200-saturation-0.6-hsi", "Colorize hue to 200 and saturation to 0.6", configureColorize));
    processes.back().processName = "Colorize";
    processes.back().applyAtDefaults = true;
    processes.push_back(describe<ColorSaturationProcess, ColorSaturationParams>(
        "Saturation", "actionColor_Saturation", "color.actionColor_Saturation", "ColorSaturationProcess",
        "color-saturation", "saturation-enhancement", true, false, "amount-0.5",
        "Double coeff 0.5, background factor 1, hue min 0, hue max 360", configureSaturation));
    processes.back().processName = "Color Saturation";
    processes.back().applyAtDefaults = true;
    processes.push_back(describe<AdjustHSLProcess, AdjustHSLParams>(
        "Hsl", "actionAdjustHSL", "tone_brightness.actionAdjustHSL", "AdjustHSLProcess", "adjust-hsl", "hsl-adjustment",
        true, false, "hue-30-saturation-40-lightness-minus-10",
        "Adjust hue to 30 saturation to 40 and lightness to -10", configureHsl));
    processes.back().processName = "Adjust Hue/Saturation/Lightness";
    processes.push_back(describe<AdjustLChProcess, AdjustLChParams>(
        "Lch", "actionAdjustLightness_Chroma_Hue", "tone_brightness.actionAdjustLightness_Chroma_Hue",
        "AdjustLChProcess", "adjust-lch", "lch-adjustment", true, false, "lightness-10-chroma-30-hue-minus-20",
        "Adjust lightness to 10 chroma to 30 and hue to -20", configureLch));
    processes.back().processName = "Adjust Lightness/Chroma/Hue";
    return processes;
}

} // namespace

void WindowDeleter::operator()(ImageRTPreviewControl* window) const
{
    if (window == nullptr)
    {
        return;
    }
    (void)pumpUntil([window]() { return window->isProcessIdle(); });
    delete window;
    QCoreApplication::processEvents();
}

const std::vector<PointwiseProcess>& pointwiseProcesses()
{
    static const std::vector<PointwiseProcess> processes = makeProcesses();
    return processes;
}

const PointwiseProcess* processOfRecords(std::string_view records)
{
    for (const PointwiseProcess& process : pointwiseProcesses())
    {
        if (records == process.records)
        {
            return &process;
        }
    }
    return nullptr;
}

const PointwiseProcess* processOfAction(std::string_view action)
{
    for (const PointwiseProcess& process : pointwiseProcesses())
    {
        if (action == process.action)
        {
            return &process;
        }
    }
    return nullptr;
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
    return makeTargetView(name, std::move(*document));
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

std::optional<fixtures::FixtureSamples> documentSamples(const an::ui::ImageDocument& document)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    EXPECT_NE(asset, nullptr);
    if (asset == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(*asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

bool settleWindow(const ImageRTPreviewControl& window)
{
    if (!pumpUntil([&window]() { return window.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return window.isProcessIdle();
}

bool applyAndSettle(ImageRTPreviewControl& window, ImageViewControl& target)
{
    window.applyProcess();
    return settleWindow(window) && settleView(target);
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

} // namespace an::tests::processes::pointwise
