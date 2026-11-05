// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The mask, star and background processes as the family's cases see them: how each window is opened - as the
/// launcher's entry point opens it, or with a body that stops its own run - how its widgets are set to a mode, where
/// its result lands, and the helpers that make a target from a fixture scene and run an Apply through a window.

#include "Test_ProcessesMasks/MasksTestSupport.h"

#include "processes/AutoGradientRemovalProcess.h"
#include "processes/BackgroundExtractionProcess.h"
#include "processes/BinarizeProcess.h"
#include "processes/DarkStructureEnhanceProcess.h"
#include "processes/MaskDrawingProcess.h"
#include "processes/MultiscaleLinearProcess.h"
#include "processes/RangeSelectionProcess.h"
#include "processes/StarMaskProcess.h"
#include "processes/StarReductionProcess.h"

#include "controls/ImageViewFiles.h"
#include "controls/ProcessExecution.h"
#include "controls/drawing/DrawingDocument.h"
#include "session/IEngineSessionProvider.h"
#include "session/ExecutionScope.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>

namespace an::tests::processes::masks
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

/// A window opened over a target as the entry point opens it, kept by the case, with its preview off.
template <typename WindowT> [[nodiscard]] WindowHandle openWindow(ImageViewControl& target)
{
    WindowHandle window(new WindowT(&target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(900, 600);
    for (const char* name : {"preview", "previewCheckbox"})
    {
        if (auto* preview = window->template findChild<QCheckBox*>(QString::fromLatin1(name)))
        {
            preview->setChecked(false);
        }
    }
    return window;
}

void failUnknownMode(const char* process, std::string_view mode)
{
    ADD_FAILURE() << process << " has no mode " << std::string(mode);
}

void configureBinarize(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setIndex(window, "selectionMode", 0);
        setDouble(window, "rgbkValue", 0.5);
    }
    else if (mode == "individual-0.4-0.5-0.6")
    {
        setIndex(window, "selectionMode", 1);
        setDouble(window, "rgbkValue", 0.4);
        setDouble(window, "greenValue", 0.5);
        setDouble(window, "blueValue", 0.6);
    }
    else
    {
        failUnknownMode("binarize", mode);
    }
}

void configureRangeSelection(ImageRTPreviewControl& window, std::string_view mode)
{
    // Every mode starts from the dialog's own values.
    setChecked(window, "linkRangeLimits", false);
    setDouble(window, "lowerLimitValue", 0.0);
    setDouble(window, "upperLimitValue", 1.0);
    setDouble(window, "fuzzinessValue", 0.0);
    setDouble(window, "smoothnessValue", 0.0);
    setChecked(window, "screening", false);
    setChecked(window, "lightness", true);
    setChecked(window, "invert", false);
    if (mode == "-")
    {
        return;
    }
    if (mode == "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2" ||
        mode == "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2-without-lightness")
    {
        setDouble(window, "lowerLimitValue", 0.2);
        setDouble(window, "upperLimitValue", 0.7);
        setDouble(window, "fuzzinessValue", 0.1);
        setDouble(window, "smoothnessValue", 2.0);
        setChecked(window, "lightness", mode == "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2");
        return;
    }
    if (mode == "lower-0.2-upper-0.7-screening-inverted")
    {
        setDouble(window, "lowerLimitValue", 0.2);
        setDouble(window, "upperLimitValue", 0.7);
        setChecked(window, "screening", true);
        setChecked(window, "invert", true);
        setChecked(window, "lightness", false);
        return;
    }
    failUnknownMode("range selection", mode);
}

void configureStarReduction(ImageRTPreviewControl& window, std::string_view mode)
{
    // The starless image is the view the case opened beside the target, named so.
    if (auto* starless = widgetOf<QComboBox>(window, "starlessImageSelector"))
    {
        const int index = starless->findText(QStringLiteral("starless"));
        EXPECT_GE(index, 0) << "the selector offers the starless image";
        starless->setCurrentIndex(index);
    }
    setChecked(window, "reduceTargetImage", true);
    setChecked(window, "createStarsImage", false);
    setDouble(window, "stretchFactor", 0.2);
    auto pick = [&window](const char* name, const char* text) {
        if (auto* box = widgetOf<QComboBox>(window, name))
        {
            box->setCurrentText(QString::fromLatin1(text));
        }
    };
    if (mode == "-")
    {
        pick("method", "Star Method");
        pick("mode", "Strong");
        setInt(window, "iterations", 1);
    }
    else if (mode == "transfer" || mode == "halo" || mode == "lightness")
    {
        pick("method", mode == "transfer" ? "Transfer Method" : mode == "halo" ? "Halo Method" : "Lightness Method");
        pick("mode", "Strong");
        setInt(window, "iterations", 1);
    }
    else if (mode == "soft-three-iterations")
    {
        pick("method", "Star Method");
        pick("mode", "Soft");
        setInt(window, "iterations", 3);
    }
    else
    {
        failUnknownMode("star reduction", mode);
    }
}

void configureMultiscale(ImageRTPreviewControl& window, std::string_view mode)
{
    // Every mode starts from the dialog's own values: the multiscale linear transform over six dyadic layers.
    setIndex(window, "algorithmComboBox", 1);
    setIndex(window, "spacingComboBox", 0);
    setIndex(window, "scalingFunctionComboBox", 1);
    setInt(window, "numLayersSpinBox", 6);
    setDouble(window, "amountSpinBox", 1.0);
    if (mode == "-")
    {
        return;
    }
    if (mode == "linear-three-layers-finest-removed" || mode == "starlet-three-layers-finest-removed")
    {
        setIndex(window, "algorithmComboBox", mode == "linear-three-layers-finest-removed" ? 1 : 0);
        setInt(window, "numLayersSpinBox", 3);
        if (auto* tree = widgetOf<QTreeWidget>(window, "layerTreeWidget");
            tree != nullptr && tree->topLevelItemCount() > 0)
        {
            tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        }
        return;
    }
    failUnknownMode("the multiscale linear transform", mode);
}

void configureAutoGradientRemoval(ImageRTPreviewControl& window, std::string_view mode)
{
    // Every mode starts from the dialog's own values.
    setDouble(window, "scaleValue", 5.0);
    setDouble(window, "smoothnessValue", 1.0);
    if (auto* downsample = widgetOf<QComboBox>(window, "downsampleComboBox"))
    {
        downsample->setCurrentText(QStringLiteral("4"));
    }
    setChecked(window, "structureProtection", true);
    setDouble(window, "protectionThresholdValue", 0.05);
    setDouble(window, "protectionAmountValue", 0.5);
    setChecked(window, "simplifiedModel", false);
    setInt(window, "polynomialDegreeValue", 2);
    setIndex(window, "correctionModeComboBox", 0);
    if (mode == "-")
    {
        return;
    }
    if (mode == "polynomial-divide-downsample-2")
    {
        setChecked(window, "simplifiedModel", true);
        setIndex(window, "correctionModeComboBox", 1);
        if (auto* downsample = widgetOf<QComboBox>(window, "downsampleComboBox"))
        {
            downsample->setCurrentText(QStringLiteral("2"));
        }
        return;
    }
    failUnknownMode("automatic gradient removal", mode);
}

void configureBackgroundExtraction(ImageRTPreviewControl& window, std::string_view mode)
{
    const bool polynomial = mode == "grid-4-polynomial-4-divide";
    if (!polynomial && mode != "grid-4-rbf-subtract")
    {
        failUnknownMode("background extraction", mode);
        return;
    }
    setInt(window, "samplesPerLineValue", 5);
    setDouble(window, "gridToleranceValue", 2.0);
    widgetOf<QComboBox>(window, "interpolationMethod")
        ->setCurrentText(polynomial ? QStringLiteral("Polynomial") : QStringLiteral("RBF"));
    setDouble(window, "smoothingValue", 0.5);
    setInt(window, "degreeOrder", 4);
    widgetOf<QComboBox>(window, "correctionMode")
        ->setCurrentText(polynomial ? QStringLiteral("Division") : QStringLiteral("Subtraction"));
    auto* extraction = dynamic_cast<BackgroundExtractionProcess*>(&window);
    ASSERT_NE(extraction, nullptr);
    // The records hold four samples a line; the dialog's grid holds at least five, which do not fit across the
    // records' scene, so the records' four-a-line sites are placed by hand, as a click on the preview places one: the
    // eight the records' grid kept, the third row having fallen off the bottom of the picture.
    for (const int y : {35, 63})
    {
        for (const int x : {13, 41, 70, 98})
        {
            ImageSampleSelection site;
            site.pos = QPoint(x, y);
            site.size = 25u;
            extraction->addSampleSelection(site);
        }
    }
    ASSERT_EQ(extraction->samplePositions().size(), 8u);
}

void configureStarMask(ImageRTPreviewControl& window, std::string_view mode)
{
    // The dialog's values, but for the search box: the corpus's scene is too small for the dialog's radius of five,
    // whose box has to fit four times over, so every mode searches at three, the smallest the dialog offers.
    setInt(window, "radiusValue", 3);
    setDouble(window, "thresholdValue", 1.0);
    setDouble(window, "roundnessValue", 0.5);
    setGroupChecked(window, "amplitudeRange", false);
    setGroupChecked(window, "roundnessRange", false);
    setChecked(window, "useLightness", false);
    setDouble(window, "percentIncreaseValue", 0.5);
    setInt(window, "pixelIncreaseValue", 0);
    setInt(window, "minSizeValue", 2);
    setInt(window, "maxSizeValue", 25);
    if (mode == "-")
    {
        setGroupChecked(window, "shapedAreas", true);
        return;
    }
    if (mode == "unshaped")
    {
        setGroupChecked(window, "shapedAreas", false);
        return;
    }
    failUnknownMode("the star mask", mode);
}

/// Where the cases' imported drawings are written, for the life of the suite.
const QTemporaryDir& drawingsDirectory()
{
    static const QTemporaryDir directory;
    return directory;
}

void configureMaskDrawing(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        return;
    }
    if (mode != "imported")
    {
        failUnknownMode("mask drawing", mode);
        return;
    }
    // A grey ramp the size of the drawing, written as a file and imported as a person imports one.
    auto* drawing = dynamic_cast<MaskDrawingProcess*>(&window);
    ASSERT_NE(drawing, nullptr);
    ASSERT_NE(drawing->document(), nullptr);
    auto* context = an::ui::appContext();
    ASSERT_NE(context, nullptr);
    const an::consumer::EngineSession& session = context->getEngineSessionProvider()->engineSession();
    const auto width = static_cast<std::uint32_t>(drawing->document()->width());
    const auto height = static_cast<std::uint32_t>(drawing->document()->height());
    std::optional<an::consumer::ImageAsset> ramp =
        an::tests::viewer::makeAsset<float>(session, width, height, an::tests::viewer::Channels::Grey,
                                            [width](std::uint32_t x, std::uint32_t, std::uint32_t) {
                                                return static_cast<float>(x) / static_cast<float>(width - 1u);
                                            });
    ASSERT_TRUE(ramp.has_value());
    ASSERT_TRUE(drawingsDirectory().isValid());
    static int written = 0;
    const QString path = drawingsDirectory().filePath(QStringLiteral("drawing-%1.fits").arg(++written));
    const std::optional<an::ImageDescriptor> described = ramp->descriptor();
    ASSERT_TRUE(described.has_value());
    an::Result<an::OutputIntent> intent =
        context->getOutputIntentProvider()->buildOutputIntent(*described, an::CodecFormat::Fits);
    ASSERT_TRUE(intent.hasValue()) << intent.status().message();
    const an::Status saved = an::ui::writeViewImage(session, *ramp, path, *intent, std::stop_token{});
    ASSERT_TRUE(saved.ok()) << saved.message();
    drawing->importMaskFrom(path);
    ASSERT_TRUE(pumpUntil([drawing]() {
        return !drawing->isTransferring() && drawing->pendingEdits() == 0u && !drawing->document()->isEmpty();
    }));
}

void configureDarkStructure(ImageRTPreviewControl& window, std::string_view mode)
{
    if (mode == "-")
    {
        setInt(window, "layersValue", 8);
        setIndex(window, "scalingFunctionComboBox", 1);
        setDouble(window, "amountValue", 0.4);
        setInt(window, "iterationsValue", 1);
    }
    else if (mode == "layers-7-amount-0.8-two-iterations")
    {
        setInt(window, "layersValue", 7);
        setIndex(window, "scalingFunctionComboBox", 1);
        setDouble(window, "amountValue", 0.8);
        setInt(window, "iterationsValue", 2);
    }
    else
    {
        failUnknownMode("dark structure enhancement", mode);
    }
}

template <typename ProcessT, typename ParamsT>
MasksProcess describe(const char* name, const char* action, const char* descriptorId, const char* className,
                      const char* processName, const char* records, const char* stateOperation,
                      std::function<void(ImageRTPreviewControl&, std::string_view)> configure)
{
    MasksProcess process;
    process.name = name;
    process.action = action;
    process.descriptorId = descriptorId;
    process.className = className;
    process.processName = processName;
    process.records = records;
    process.stateOperation = stateOperation;
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
    return process;
}

std::vector<MasksProcess> makeProcesses()
{
    std::vector<MasksProcess> processes;

    processes.push_back(describe<RangeSelectionProcess, RangeSelectionProcessParams>(
        "RangeSelection", "actionRange_Selection", "sensor_calibration.actionRange_Selection", "RangeSelectionProcess",
        "Range Selection", "range-selection", "range-selection", configureRangeSelection));
    processes.back().scene = "scene-rgb-float32";
    processes.back().smooth = true;
    processes.back().landing = ResultLanding::NewView;
    processes.back().newViewSuffix = " - MASK";
    processes.back().changingMode = "lower-0.2-upper-0.7-fuzziness-0.1-smoothness-2";
    processes.back().changingStep = "";

    processes.push_back(describe<BinarizeProcess, BinarizeParams>(
        "Binarize", "actionBinarize", "sensor_calibration.actionBinarize", "BinarizeProcess", "Binarize", "binarize",
        "binarize", configureBinarize));
    processes.back().scene = "scene-rgb-float32";
    processes.back().previewArea = true;
    processes.back().changingMode = "individual-0.4-0.5-0.6";
    processes.back().changingStep = "R: 0.4, G: 0.5, B: 0.6 ";

    processes.push_back(describe<StarMaskProcess, StarMaskParams>(
        "StarMask", "actionStar_Mask", "star_object.actionStar_Mask", "StarMaskProcess", "Star Mask", "star-mask",
        "star-mask", configureStarMask));
    processes.back().scene = "stars-mono-float32-wide";
    processes.back().landing = ResultLanding::NewView;
    processes.back().newViewSuffix = " - STARMASK";
    processes.back().changingMode = "-";
    processes.back().changingStep =
        "Created in shaped areas mode. Radius: 3, Threshold: 1.00, Profile: Gaussian. Percent "
        "Increase: 0.50, Pixel Increase: 0, Min Size: 2, Max Size: 25, Star Shape: "
        "Truncated Bell";

    processes.push_back(describe<StarReductionProcess, StarReductionProcessParams>(
        "StarReduction", "actionStar_Reduction", "star_object.actionStar_Reduction", "StarReductionProcess",
        "Star Reduction", "star-reduction", "star-reduction", configureStarReduction));
    processes.back().scene = "stars-rgb-float32";
    processes.back().smooth = true;
    processes.back().needsStarless = true;
    processes.back().changingMode = "-";
    processes.back().changingStep = "Star reduction using starless";

    processes.push_back(describe<MultiscaleLinearProcess, MultiscaleParams>(
        "MultiscaleLinear", "actionMultiscale_Linear_Transform", "transforms.actionMultiscale_Linear_Transform",
        "MultiscaleLinearProcess", "Multiscale Linear Transform", "multiscale-linear", "", configureMultiscale));
    processes.back().scene = "stars-rgb-float32";
    processes.back().previewArea = true;
    processes.back().smooth = true;
    processes.back().perChannel = true;
    processes.back().changingMode = "linear-three-layers-finest-removed";
    processes.back().changingStep =
        "Algorithm: Multiscale Linear Transform, Layers: 3, Spacing: Dyadic in channel: RGB";

    processes.push_back(describe<AutoGradientRemovalProcess, AutoGradientRemovalProcessParams>(
        "AutoGradientRemoval", "actionAuto_Gradient_Removal", "background_extraction.actionAuto_Gradient_Removal",
        "AutoGradientRemovalProcess", "Auto Gradient Removal", "auto-gradient-removal", "",
        configureAutoGradientRemoval));
    processes.back().scene = "stars-rgb-float32";
    processes.back().previewOnButton = true;
    processes.back().smooth = true;
    processes.back().stillImageOnly = true;
    processes.back().changingMode = "-";
    processes.back().changingStep = "Scale: 5.00, Smoothness: 1.00, Downsample: 4, Correction: Subtraction, Model: "
                                    "Multiscale, Structure protection: enabled (threshold: 0.050, amount: 0.50)";

    processes.push_back(describe<BackgroundExtractionProcess, BackgroundExtractionParams>(
        "BackgroundExtraction", "actionBackground_Extraction", "background_extraction.actionBackground_Extraction",
        "BackgroundExtractionProcess", "Background Extraction", "background-extraction", "",
        configureBackgroundExtraction));
    processes.back().scene = "stars-mono-float32-wide";
    processes.back().previewOnButton = true;
    processes.back().smooth = true;
    processes.back().changingMode = "grid-4-rbf-subtract";
    processes.back().changingStep =
        "Samples per Line: 5, Grid Tolerance: 2, Correction: Subtraction, Interpolation: RBF, Smoothing: 0.5";

    processes.push_back(describe<MaskDrawingProcess, MaskDrawingProcessParams>(
        "MaskDrawing", "actionMask_Drawing", "mask_generation.actionMask_Drawing", "MaskDrawingProcess", "Mask Drawing",
        "", "", configureMaskDrawing));
    processes.back().scene = "scene-rgb-float32";
    processes.back().previewShowsTarget = true;
    processes.back().landing = ResultLanding::NewView;
    processes.back().newViewSuffix = " - MASK";
    processes.back().changingMode = "imported";
    processes.back().changingStep = "Create painted grayscale mask";

    processes.push_back(describe<DarkStructureEnhanceProcess, DarkStructureEnhanceParams>(
        "DarkStructure", "actionDark_Structure_Enhance", "sharpening_enhancement.actionDark_Structure_Enhance",
        "DarkStructureEnhanceProcess", "DarkStructureEnhance", "", "dark-structure-enhance", configureDarkStructure));
    processes.back().scene = "stars-rgb-float32";
    processes.back().previewArea = true;
    processes.back().smooth = true;
    processes.back().perChannel = true;
    processes.back().changingMode = "layers-7-amount-0.8-two-iterations";
    processes.back().changingStep = "Layers: 7, Scaling: 5x5 B3 Spline, Amount: 0.80 (midtones 0.90), Iterations: 2";

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

const std::vector<MasksProcess>& masksProcesses()
{
    static const std::vector<MasksProcess> processes = makeProcesses();
    return processes;
}

const MasksProcess* processOfAction(std::string_view action)
{
    for (const MasksProcess& process : masksProcesses())
    {
        if (action == process.action)
        {
            return &process;
        }
    }
    return nullptr;
}

const MasksProcess* processOfRecords(std::string_view records)
{
    for (const MasksProcess& process : masksProcesses())
    {
        if (!records.empty() && records == process.records)
        {
            return &process;
        }
    }
    return nullptr;
}

std::vector<std::size_t> processesWithRecords()
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < masksProcesses().size(); ++index)
    {
        if (std::string_view(masksProcesses()[index].records).size() > 0u)
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

void setGroupChecked(QWidget& window, const char* name, bool checked)
{
    if (auto* box = widgetOf<QGroupBox>(window, name))
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

void setText(QWidget& window, const char* name, const QString& text)
{
    if (auto* edit = widgetOf<QLineEdit>(window, name))
    {
        edit->setText(text);
    }
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
    return samplesOf(*asset);
}

std::optional<fixtures::FixtureSamples> samplesOf(const an::consumer::ImageAsset& asset)
{
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = adapter::readAssetSamples(asset, error);
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

std::optional<fixtures::FixtureSamples> applyAndRead(ProcessTestContext& context, const MasksProcess& process,
                                                     ImageRTPreviewControl& window, ImageViewControl& target)
{
    const std::size_t before = context.madeViews.size();
    EXPECT_TRUE(applyAndSettle(window, target));
    if (process.landing == ResultLanding::InPlace)
    {
        return documentSamples(target.document());
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

} // namespace an::tests::processes::masks
