// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The geometry and filter-array processes as one table and the runs of a window as a person runs it.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"

#include "app/FileCompanions.h"
#include "processes/CFAIntegerResampleProcess.h"
#include "processes/CropProcess.h"
#include "processes/DebayerProcess.h"
#include "processes/ExtractSequenceProcess.h"
#include "processes/IntegerResampleProcess.h"
#include "processes/MergeCFAProcess.h"
#include "processes/ResizeProcess.h"
#include "processes/RotateProcess.h"
#include "processes/SplitCFAProcess.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QThreadPool>

#include <cmath>
#include <limits>

namespace an::tests::processes::geometry
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

/// A single-image window opened over a target as the entry point opens it, kept by the case.
template <typename WindowT> [[nodiscard]] WindowHandle openOver(ImageViewControl* target)
{
    WindowHandle window(new WindowT(target));
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(700, 500);
    return window;
}

/// A top-level window, opened over no view.
template <typename WindowT> [[nodiscard]] WindowHandle openTopLevel(ImageViewControl*)
{
    WindowHandle window(new WindowT());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    return window;
}

void unknownMode(std::string_view process, std::string_view mode)
{
    ADD_FAILURE() << process << " has no mode " << mode;
}

void configureCrop(QWidget& window, std::string_view mode)
{
    if (mode != "-" && mode != "x-2-y-1-width-8-height-5")
    {
        unknownMode("Crop", mode);
        return;
    }
    setInt(window, "targetPosX", 2);
    setInt(window, "targetPosY", 1);
    setInt(window, "targetWidth", 8);
    setInt(window, "targetHeight", 5);
}

void configureResize(QWidget& window, std::string_view mode)
{
    struct Setting
    {
        const char* mode;
        double percent;
        const char* interpolation;
    };
    for (const Setting& setting :
         {Setting{"-", 50.0, "Auto"}, Setting{"percent-50-auto", 50.0, "Auto"},
          Setting{"percent-200-auto", 200.0, "Auto"}, Setting{"percent-150-bicubic", 150.0, "Bicubic"},
          Setting{"percent-150-nearest", 150.0, "Nearest Neighbor"}})
    {
        if (mode == setting.mode)
        {
            setChecked(window, "keepAspectRatio", true);
            setDouble(window, "targetWidthPercent", setting.percent);
            if (auto* combo = widgetOf<QComboBox>(window, "interpolation"))
            {
                an::ui::processes::selectInterpolationName(combo, QString::fromLatin1(setting.interpolation));
            }
            return;
        }
    }
    unknownMode("Resize", mode);
}

void configureRotate(QWidget& window, std::string_view mode)
{
    if (mode == "-" || mode == "angle-30-auto")
    {
        setDouble(window, "rotationAngle", 30.0);
        return;
    }
    if (mode == "angle-minus-17.5-resized-bilinear")
    {
        setChecked(window, "resizeImage", true);
        setDouble(window, "rotationAngle", -17.5);
        if (auto* combo = widgetOf<QComboBox>(window, "interpolation"))
        {
            an::ui::processes::selectInterpolationName(combo, QStringLiteral("Bilinear"));
        }
        return;
    }
    unknownMode("Rotate", mode);
}

void configureIntegerResample(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        return;
    }
    if (mode == "upsample-2")
    {
        clickRadio(window, "UpsampleRadioButton");
        return;
    }
    for (const auto& [name, label] : {std::pair<const char*, const char*>{"median", "Median"},
                                      std::pair<const char*, const char*>{"maximum", "Maximum"},
                                      std::pair<const char*, const char*>{"minimum", "Minimum"}})
    {
        if (mode == name)
        {
            setComboText(window, "downsampleMode", QString::fromLatin1(label));
            return;
        }
    }
    unknownMode("Integer Resample", mode);
}

void configureDebayer(QWidget& window, std::string_view mode)
{
    for (const auto& [name, label] :
         {std::pair<const char*, const char*>{"bilinear", "Bilinear"},
          std::pair<const char*, const char*>{"vng4", "VNG4"}, std::pair<const char*, const char*>{"ahd", "AHD"},
          std::pair<const char*, const char*>{"amaze", "AMaZE"}, std::pair<const char*, const char*>{"lmmse", "LMMSE"}})
    {
        if (mode == name)
        {
            setComboText(window, "demosaicingMethod", QString::fromLatin1(label));
            return;
        }
    }
    if (mode == "-")
    {
        return;
    }
    if (mode == "rcd-bggr-stated")
    {
        setComboText(window, "bayerPattern", QStringLiteral("BGGR"));
        return;
    }
    unknownMode("Debayer", mode);
}

void configureSplitCfa(QWidget& window, std::string_view mode)
{
    const std::string text(mode);
    if (mode == "-" || text.rfind("plane-", 0u) == 0u)
    {
        setComboText(window, "methodSelector", QStringLiteral("Split CFA Channels"));
        return;
    }
    if (mode == "ha")
    {
        setComboText(window, "methodSelector", QStringLiteral("Extract Ha"));
        return;
    }
    if (mode == "green")
    {
        setComboText(window, "methodSelector", QStringLiteral("Extract Green"));
        return;
    }
    for (const auto& [prefix, scaling] :
         {std::pair<const char*, const char*>{"ha-oiii-no-resampling", "No Resampling"},
          std::pair<const char*, const char*>{"ha-oiii-upsample-ha", "Upsample Ha"},
          std::pair<const char*, const char*>{"ha-oiii-downsample-oiii", "Downsample OIII"}})
    {
        if (text.rfind(prefix, 0u) == 0u)
        {
            setComboText(window, "methodSelector", QStringLiteral("Extract Ha/OIII"));
            setComboText(window, "haOIIIResamplingSelector", QString::fromLatin1(scaling));
            return;
        }
    }
    unknownMode("Split CFA", mode);
}

void configureMergeCfa(QWidget&, std::string_view mode)
{
    if (mode != "-")
    {
        unknownMode("Merge CFA", mode);
    }
}

void configureExtractSequence(QWidget&, std::string_view mode)
{
    if (mode != "-")
    {
        unknownMode("Extract Sequence", mode);
    }
}

[[nodiscard]] std::vector<GeometryProcess> makeProcesses()
{
    std::vector<GeometryProcess> table;
    table.push_back(GeometryProcess{.name = "Crop",
                                    .action = "actionCrop",
                                    .descriptorId = "geometry.actionCrop",
                                    .className = "CropProcess",
                                    .processName = "Crop",
                                    .records = "crop",
                                    .landing = Landing::InPlace,
                                    .scene = "scene-rgb-uint16",
                                    .exclusive = true,
                                    .errorTitle = "Error running crop",
                                    .open = openOver<CropProcess>,
                                    .openStopping = openOver<Stopping<CropProcess>>,
                                    .configure = configureCrop});
    table.push_back(GeometryProcess{.name = "Resize",
                                    .action = "actionResize",
                                    .descriptorId = "geometry.actionResize",
                                    .className = "ResizeProcess",
                                    .processName = "Resize",
                                    .records = "resize",
                                    .landing = Landing::InPlace,
                                    .scene = "scene-rgb-float32",
                                    .errorTitle = "Error running resize",
                                    .open = openOver<ResizeProcess>,
                                    .openStopping = openOver<Stopping<ResizeProcess>>,
                                    .configure = configureResize});
    table.push_back(GeometryProcess{.name = "Rotate",
                                    .action = "actionRotate",
                                    .descriptorId = "geometry.actionRotate",
                                    .className = "RotateProcess",
                                    .processName = "Rotate",
                                    .records = "rotate",
                                    .landing = Landing::InPlace,
                                    .scene = "scene-rgb-float32",
                                    .exclusive = true,
                                    .errorTitle = "Error running rotate",
                                    .open = openOver<RotateProcess>,
                                    .openStopping = openOver<Stopping<RotateProcess>>,
                                    .configure = configureRotate});
    table.push_back(GeometryProcess{.name = "IntegerResample",
                                    .action = "actionIntegerResample",
                                    .descriptorId = "geometry.actionIntegerResample",
                                    .className = "IntegerResampleProcess",
                                    .processName = "Integer Resample",
                                    .records = "integer-resample",
                                    .landing = Landing::InPlace,
                                    .scene = "scene-rgb-uint16",
                                    .errorTitle = "Error running integer resample",
                                    .open = openOver<IntegerResampleProcess>,
                                    .openStopping = openOver<Stopping<IntegerResampleProcess>>,
                                    .configure = configureIntegerResample});
    table.push_back(GeometryProcess{.name = "CfaIntegerResample",
                                    .action = "actionCFA_Integer_Resample",
                                    .descriptorId = "geometry.actionCFA_Integer_Resample",
                                    .className = "CFAIntegerResampleProcess",
                                    .processName = "CFA Integer Resample",
                                    .records = "cfa-integer-resample",
                                    .landing = Landing::InPlace,
                                    .scene = "mosaic-rggb-uint16",
                                    .errorTitle = "Error running integer resample",
                                    .open = openOver<CFAIntegerResampleProcess>,
                                    .openStopping = openOver<Stopping<CFAIntegerResampleProcess>>,
                                    .configure = configureIntegerResample});
    table.push_back(GeometryProcess{.name = "Debayer",
                                    .action = "actionDebayer",
                                    .descriptorId = "sensor_calibration.actionDebayer",
                                    .className = "DebayerProcess",
                                    .processName = "Debayer",
                                    .records = "debayer",
                                    .landing = Landing::NewViews,
                                    .scene = "mosaic-rggb-uint16",
                                    .errorTitle = "Error running Debayer",
                                    .open = openOver<DebayerProcess>,
                                    .openStopping = openOver<Stopping<DebayerProcess>>,
                                    .configure = configureDebayer});
    table.push_back(GeometryProcess{.name = "SplitCfa",
                                    .action = "actionSplit_CFA",
                                    .descriptorId = "sensor_calibration.actionSplit_CFA",
                                    .className = "SplitCFAProcess",
                                    .processName = "Split CFA",
                                    .records = "split-cfa",
                                    .landing = Landing::NewViews,
                                    .scene = "mosaic-rggb-uint16",
                                    .errorTitle = "Error running Split CFA",
                                    .open = openOver<SplitCFAProcess>,
                                    .openStopping = openOver<Stopping<SplitCFAProcess>>,
                                    .configure = configureSplitCfa});
    table.push_back(GeometryProcess{.name = "MergeCfa",
                                    .action = "actionMerge_CFA",
                                    .descriptorId = "sensor_calibration.actionMerge_CFA",
                                    .className = "MergeCFAProcess",
                                    .processName = "Merge CFA",
                                    .records = "merge-cfa",
                                    .landing = Landing::TopLevel,
                                    .scene = "mosaic-rggb-uint16",
                                    .errorTitle = "Error running Merge CFA",
                                    .open = openTopLevel<MergeCFAProcess>,
                                    .openStopping = openTopLevel<Stopping<MergeCFAProcess>>,
                                    .configure = configureMergeCfa});
    table.push_back(GeometryProcess{.name = "ExtractSequence",
                                    .action = "actionExtract_Sequence",
                                    .descriptorId = "multi_image_stacking.actionExtract_Sequence",
                                    .className = "ExtractSequenceProcess",
                                    .processName = "Extract Sequence",
                                    .records = "",
                                    .landing = Landing::NewViews,
                                    .scene = "scene-rgb-uint16",
                                    .errorTitle = "Error running Extract Sequence",
                                    .open = openOver<ExtractSequenceProcess>,
                                    .openStopping = openOver<Stopping<ExtractSequenceProcess>>,
                                    .configure = configureExtractSequence});
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

const std::vector<GeometryProcess>& geometryProcesses()
{
    static const std::vector<GeometryProcess> processes = makeProcesses();
    return processes;
}

const GeometryProcess* processOfAction(std::string_view action)
{
    for (const GeometryProcess& process : geometryProcesses())
    {
        if (action == process.action)
        {
            return &process;
        }
    }
    return nullptr;
}

const GeometryProcess* processOfRecords(std::string_view records)
{
    for (const GeometryProcess& process : geometryProcesses())
    {
        if (!records.empty() && records == process.records)
        {
            return &process;
        }
    }
    return nullptr;
}

std::vector<std::size_t> processesLanding(Landing landing)
{
    std::vector<std::size_t> indices;
    for (std::size_t index = 0u; index < geometryProcesses().size(); ++index)
    {
        if (geometryProcesses()[index].landing == landing)
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

void clickRadio(QWidget& window, const char* name)
{
    if (auto* button = widgetOf<QRadioButton>(window, name))
    {
        button->click();
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

void pressOk(QWidget& window)
{
    QDialogButtonBox* box = buttonBoxOf(window);
    ASSERT_NE(box, nullptr) << "the window has no buttons";
    QPushButton* ok = box->button(QDialogButtonBox::Ok);
    ASSERT_NE(ok, nullptr);
    ASSERT_TRUE(ok->isEnabled()) << "Ok is disabled: " << ok->toolTip().toStdString();
    ok->click();
}

bool windowIdle(const QWidget& window)
{
    if (const auto* process = dynamic_cast<const ImageProcessControl*>(&window))
    {
        return process->isRunIdle();
    }
    if (const auto* merge = dynamic_cast<const MergeCFAProcess*>(&window))
    {
        return !merge->isRunning();
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

bool applyAndSettle(ProcessTestContext& context, QWidget& window)
{
    pressOk(window);
    return settleWindow(context, window);
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

ImageViewControl* openDemosaicedSequenceView(ProcessTestContext& context, const QString& path,
                                             an::SequenceFormat format, const std::string& name)
{
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context.engineSession(), path, format, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    document->setNames(an::ui::DocumentNames{.displayName = QString::fromStdString(name), .sourcePath = path});
    ImageViewControl* view =
        context.createNewImageViewControlFromSequence(nullptr, std::move(*document), nullptr, true, "Sequence opened",
                                                      ViewControlMode::standard, nullptr, false, false);
    EXPECT_NE(view, nullptr);
    if (view == nullptr)
    {
        return nullptr;
    }
    EXPECT_TRUE(settleView(*view));
    channels::activate(context, *view);
    return view;
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

double largestInteriorDifference(const fixtures::FixtureSamples& left, const fixtures::FixtureSamples& right,
                                 std::uint32_t band)
{
    if (left.width() != right.width() || left.height() != right.height() || left.channelCount() != right.channelCount())
    {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::uint32_t c = 0u; c < left.channelCount(); ++c)
    {
        for (std::uint32_t y = band; y + band < left.height(); ++y)
        {
            for (std::uint32_t x = band; x + band < left.width(); ++x)
            {
                const double a = left.value(x, y, c);
                const double b = right.value(x, y, c);
                if (std::isnan(a) && std::isnan(b))
                {
                    continue;
                }
                const double difference =
                    std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity() : std::fabs(a - b);
                largest = (std::max)(largest, difference);
            }
        }
    }
    return largest;
}

} // namespace an::tests::processes::geometry
