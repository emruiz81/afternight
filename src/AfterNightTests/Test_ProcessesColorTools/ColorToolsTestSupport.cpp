// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The colour calibration tools as one table and the runs of a window as a person runs it.

#include "Test_ProcessesColorTools/ColorToolsTestSupport.h"

#include "processes/BackgroundNeutralizationProcess.h"
#include "processes/ColorCalibrationProcess.h"
#include "processes/LinearMatchProcess.h"
#include "processes/SCNRProcess.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QPushButton>

#include <algorithm>
#include <cmath>
#include <limits>

namespace an::tests::processes::colortools
{

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;

/// A window whose run asks for its own stop once its operation is admitted.
template <typename WindowT> class Stopping final : public WindowT
{
public:
    using WindowT::WindowT;

protected:
    [[nodiscard]] an::ui::processes::AdmissionHook admissionHook() const override
    {
        return [](an::ui::ExecutionScope& scope) { scope.requestStop(); };
    }
};

/// A window opened over a target as the entry point opens it, kept by the case.
template <typename WindowT> [[nodiscard]] WindowHandle openOver(ImageViewControl* target)
{
    WindowHandle window(new WindowT(target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(700, 600);
    return window;
}

void unknownMode(std::string_view process, std::string_view mode)
{
    ADD_FAILURE() << process << " has no mode " << mode;
}

void configureColorCalibration(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        return;
    }
    if (mode == "manual-0.9-1-0.8")
    {
        clickGroup(window, "mwbGroupBox", true);
        setDouble(window, "mwbRedValue", 0.9);
        setDouble(window, "mwbGreenValue", 1.0);
        setDouble(window, "mwbBlueValue", 0.8);
        return;
    }
    unknownMode("Color Calibration", mode);
}

void configureNeutralization(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        return;
    }
    if (mode == "upper-0.5")
    {
        setDouble(window, "backgroundUpperLimitValue", 0.5);
        return;
    }
    unknownMode("Background Neutralization", mode);
}

void configureLinearMatch(QWidget&, std::string_view mode)
{
    if (mode != "-")
    {
        unknownMode("Linear Match", mode);
    }
}

void configureScnr(QWidget& window, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        const char* protection;
        double amount;
        bool preserve;
    };
    for (const Setting& setting :
         {Setting{"-", "Average Neutral", 1.0, true}, Setting{"maximum-neutral", "Maximum Neutral", 1.0, true},
          Setting{"maximum-mask-0.5", "Maximum Mask", 0.5, true},
          Setting{"additive-mask-0.5", "Additive Mask", 0.5, true},
          Setting{"average-neutral-lightness-free", "Average Neutral", 1.0, false}})
    {
        if (mode == setting.mode)
        {
            setComboText(window, "protectionType", QString::fromLatin1(setting.protection));
            setDouble(window, "amountValue", setting.amount);
            setChecked(window, "preserveLightness", setting.preserve);
            return;
        }
    }
    unknownMode("SCNR", mode);
}

[[nodiscard]] std::vector<ColorTool> makeTools()
{
    std::vector<ColorTool> table;
    table.push_back(ColorTool{.name = "ColorCalibration",
                              .action = "actionColor_Calibration_2",
                              .descriptorId = "color.actionColor_Calibration_2",
                              .className = "ColorCalibrationProcess",
                              .processName = "ColorCalibration",
                              .records = "color-calibration",
                              .scene = "scene-rgb-float32",
                              .exclusive = true,
                              .apply = QDialogButtonBox::Apply,
                              .closesWhenShown = true,
                              .errorTitle = "Error running color calibration",
                              .errorLead = "An error occurred when executing the color calibration process",
                              .open = openOver<ColorCalibrationProcess>,
                              .openStopping = openOver<Stopping<ColorCalibrationProcess>>,
                              .configure = configureColorCalibration,
                              .chooseReference = [](QWidget& window, const QString& view) {
                                  setComboText(window, "whiteReferenceImageSelector", view);
                                  setComboText(window, "backgroundReferenceImageSelector", view);
                              }});
    table.push_back(ColorTool{.name = "BackgroundNeutralization",
                              .action = "actionBackground_Neutralization",
                              .descriptorId = "color.actionBackground_Neutralization",
                              .className = "BackgroundNeutralizationProcess",
                              .processName = "Background Neutralization",
                              .records = "background-neutralization",
                              .scene = "stars-rgb-float32",
                              .exclusive = true,
                              .apply = QDialogButtonBox::Ok,
                              .closesWhenShown = true,
                              .errorTitle = "Error running background neutralization",
                              .errorLead = "An error occurred when executing the background neutralization process",
                              .open = openOver<BackgroundNeutralizationProcess>,
                              .openStopping = openOver<Stopping<BackgroundNeutralizationProcess>>,
                              .configure = configureNeutralization,
                              .chooseReference = [](QWidget& window, const QString& view) {
                                  setComboText(window, "backgroundReferenceImageSelector", view);
                              }});
    table.push_back(ColorTool{.name = "LinearMatch",
                              .action = "actionLinear_Match",
                              .descriptorId = "color.actionLinear_Match",
                              .className = "LinearMatchProcess",
                              .processName = "Linear Match",
                              .records = "linear-match",
                              .scene = "scene-rgb-float32",
                              .exclusive = false,
                              .needsReference = true,
                              .apply = QDialogButtonBox::Ok,
                              .closesWhenShown = true,
                              .errorTitle = "Error running linear match",
                              .errorLead = "An error occurred when executing the linear match process",
                              .open = openOver<LinearMatchProcess>,
                              .openStopping = openOver<Stopping<LinearMatchProcess>>,
                              .configure = configureLinearMatch,
                              .chooseReference = [](QWidget& window, const QString& view) {
                                  setComboText(window, "referenceImageSelector", view);
                              }});
    table.push_back(ColorTool{.name = "Scnr",
                              .action = "actionSCNR",
                              .descriptorId = "color.actionSCNR",
                              .className = "SCNRProcess",
                              .processName = "SCNR",
                              .records = "scnr",
                              .scene = "scene-rgb-float32",
                              .exclusive = false,
                              .apply = QDialogButtonBox::Apply,
                              .closesWhenShown = false,
                              .errorTitle = "Error running SCNR",
                              .errorLead = "An error occurred when executing the SCNR process",
                              .open = openOver<SCNRProcess>,
                              .openStopping = openOver<Stopping<SCNRProcess>>,
                              .configure = configureScnr,
                              .chooseReference = {}});
    return table;
}

[[nodiscard]] QDialogButtonBox* buttonBoxOf(QWidget& window)
{
    return window.findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
}

} // namespace

void WindowDeleter::operator()(QWidget* window) const
{
    if (window == nullptr)
    {
        return;
    }
    (void)pumpUntil([window]() { return windowIdle(*window); });
    delete window;
    QCoreApplication::processEvents();
}

const std::vector<ColorTool>& colorTools()
{
    static const std::vector<ColorTool> tools = makeTools();
    return tools;
}

const ColorTool* toolOfRecords(std::string_view records)
{
    for (const ColorTool& tool : colorTools())
    {
        if (records == tool.records)
        {
            return &tool;
        }
    }
    return nullptr;
}

const ColorTool* toolNamed(std::string_view name)
{
    for (const ColorTool& tool : colorTools())
    {
        if (name == tool.name)
        {
            return &tool;
        }
    }
    return nullptr;
}

void setDouble(QWidget& window, const char* name, double value)
{
    if (auto* box = widgetOf<QDoubleSpinBox>(window, name))
    {
        box->setValue(value);
    }
}

void setInt(QWidget& window, const char* name, int value)
{
    if (auto* box = widgetOf<QSpinBox>(window, name))
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

void clickGroup(QWidget& window, const char* name, bool checked)
{
    if (auto* box = widgetOf<QGroupBox>(window, name))
    {
        box->setChecked(checked);
        emit box->clicked(checked);
    }
}

void setComboText(QWidget& window, const char* name, const QString& text)
{
    if (auto* combo = widgetOf<QComboBox>(window, name))
    {
        const int index = combo->findText(text);
        EXPECT_GE(index, 0) << name << " offers no " << text.toStdString();
        if (index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    }
}

QPushButton* applyButtonOf(QWidget& window, const ColorTool& tool)
{
    QDialogButtonBox* box = buttonBoxOf(window);
    return box != nullptr ? box->button(tool.apply) : nullptr;
}

void pressApply(QWidget& window, const ColorTool& tool)
{
    QPushButton* apply = applyButtonOf(window, tool);
    ASSERT_NE(apply, nullptr) << "the window has no apply button";
    ASSERT_TRUE(apply->isEnabled()) << "apply is disabled: " << apply->toolTip().toStdString();
    apply->click();
}

bool windowIdle(const QWidget& window)
{
    if (const auto* process = dynamic_cast<const ImageProcessControl*>(&window))
    {
        return process->isRunIdle();
    }
    return true;
}

bool settleWindow(ProcessTestContext& context, const QWidget& window)
{
    if (!pumpUntil([&window]() { return windowIdle(window); }))
    {
        return false;
    }
    return channels::settleAll(context) && windowIdle(window);
}

bool applyAndSettle(ProcessTestContext& context, QWidget& window, const ColorTool& tool)
{
    pressApply(window, tool);
    return settleWindow(context, window);
}

std::optional<fixtures::FixtureSamples> sceneSamples(std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << scene;
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session, std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
    if (image == nullptr || !samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                     const an::consumer::ImageAsset& source,
                                                     const an::ImageMetadata& metadata,
                                                     std::optional<an::ValidityMask> mask)
{
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    EXPECT_TRUE(lease.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!lease.hasValue() || !scope.has_value())
    {
        return std::nullopt;
    }
    std::optional<an::consumer::AnyImage> copy = std::visit(
        [&](const auto& typed) -> std::optional<an::consumer::AnyImage> {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const auto view = typed.view();
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!view.has_value() || !described.has_value())
            {
                return std::nullopt;
            }
            an::Result<an::Image<SampleT>> image =
                an::Image<SampleT>::create(scope->engine(), *described, an::StorageArrangement::Planar,
                                           an::StorageInitialization::Zeroed, metadata);
            EXPECT_TRUE(image.hasValue()) << image.status().message();
            if (!image.hasValue())
            {
                return std::nullopt;
            }
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = image->write();
                an::ConversionCounters counters;
                const an::Status copied =
                    an::convertImageSamples(scope->execution(), an::kSampleTypeOf<SampleT>, view->data(),
                                            view->layout(), an::kSampleTypeOf<SampleT>, access->view().data(),
                                            access->view().layout(), an::ConversionPolicy{}, counters);
                EXPECT_TRUE(copied.ok()) << copied.message();
            }
            return an::consumer::AnyImage(std::move(*image));
        },
        lease->image());
    lease->release();
    scope.reset();
    if (!copy.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset =
        an::consumer::ImageAsset::adopt(session, std::move(*copy), std::move(mask));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

std::optional<an::consumer::ImageAsset> convertedTo(const an::consumer::EngineSession& session,
                                                    const an::consumer::ImageAsset& source, an::SampleType target)
{
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    EXPECT_TRUE(lease.hasValue());
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!lease.hasValue() || !scope.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AnyImage> converted =
        an::ui::convertViewFrame(scope->engine(), scope->execution(), lease->image(), target);
    EXPECT_TRUE(converted.hasValue()) << converted.status().message();
    lease->release();
    scope.reset();
    if (!converted.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move(*converted));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

an::ImageMetadata describedMetadata()
{
    an::Result<an::AcquisitionMetadata> acquisition =
        an::AcquisitionMetadata::create(an::AcquisitionMetadataRequest{.objectName = "M 42", .exposureSeconds = 120.0});
    EXPECT_TRUE(acquisition.hasValue()) << acquisition.status().message();
    an::Result<an::AstrometricSolution> solution = an::AstrometricSolution::create(
        an::AstrometricSolutionRequest{.referencePoint = {.rightAscensionDegrees = 83.82, .declinationDegrees = -5.39},
                                       .referencePixelX = 6.0,
                                       .referencePixelY = 4.0,
                                       .pixelScaleArcsecPerPixel = 2.4,
                                       .orientationDegrees = 17.0,
                                       .mirrored = false,
                                       .hasDistortion = false});
    EXPECT_TRUE(solution.hasValue()) << solution.status().message();
    an::Result<an::CodecCard> object = an::CodecCard::create(an::CodecNamespace::Fits, "OBJECT", "'M 42'", "target");
    an::Result<an::CodecCard> linear = an::CodecCard::create(an::CodecNamespace::Fits, "ANLIN", "'linear'", "");
    EXPECT_TRUE(object.hasValue() && linear.hasValue());
    an::Result<an::CodecMetadata> cards =
        an::CodecMetadata::create(an::SourceEncoding{}, std::vector<an::CodecCard>{*object, *linear});
    EXPECT_TRUE(cards.hasValue()) << cards.status().message();
    an::ImageSemanticsRequest meaning;
    meaning.transferFunction = an::TransferFunction::Linear;
    an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    return an::ImageMetadata::create(*acquisition, *semantics, *solution, an::Provenance{}, *cards, std::nullopt);
}

double fractionOf(const fixtures::FixtureSamples& samples, std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    const double value = samples.value(x, y, channel);
    if (fixtures::isFloating(samples.sampleType()))
    {
        return value;
    }
    const double lowest = fixtures::lowestOf(samples.sampleType());
    const double highest = fixtures::highestOf(samples.sampleType());
    return (value - lowest) / (highest - lowest);
}

double largestFractionDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::uint32_t c = 0u; c < left.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                const double a = fractionOf(left, x, y, c);
                const double b = fractionOf(right, x, y, c);
                if (std::isnan(a) && std::isnan(b))
                {
                    continue;
                }
                largest = (std::max)(largest, std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity()
                                                                             : std::fabs(a - b));
            }
        }
    }
    return largest;
}

double medianOf(std::vector<double> values)
{
    if (values.empty())
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(values.begin(), values.end());
    const std::size_t half = values.size() / 2u;
    return values.size() % 2u == 1u ? values[half] : 0.5 * (values[half - 1u] + values[half]);
}

double deviationOf(const std::vector<double>& values)
{
    const double centre = medianOf(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (const double value : values)
    {
        deviations.push_back(std::fabs(value - centre));
    }
    return medianOf(deviations);
}

} // namespace an::tests::processes::colortools
