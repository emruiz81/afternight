// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The external tools as one table, the stand-in program the product is pointed at, the real programs where they are
/// installed, and the runs of a window as a person runs them.

#include "Test_ProcessesExternalTools/ExternalToolsTestSupport.h"

#include "controls/ImageViewFiles.h"
#include "controls/settings/DeepSNRCliTools.h"
#include "controls/settings/SettingsKeys.h"
#include "controls/settings/StarNetCliTools.h"
#include "processes/DeepSNRProcess.h"
#include "processes/StarnetProcess.h"
#include "processes/StarnetStarMaskProcess.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <thread>

namespace an::tests::processes::externaltools
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

/// DeepSNR's window with its program's floor lowered, so the fixture scenes reach the program.
class SmallDeepSnr : public DeepSNRProcess
{
public:
    using DeepSNRProcess::DeepSNRProcess;

protected:
    [[nodiscard]] std::uint32_t minimumImageSize() const override
    {
        return 0u;
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

void configureStarnet(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        setChecked(window, "generateStarMask", false);
        setChecked(window, "preStretchLinearImage", false);
        return;
    }
    if (mode == "prestretch")
    {
        setChecked(window, "generateStarMask", false);
        setChecked(window, "preStretchLinearImage", true);
        return;
    }
    if (mode == "stars")
    {
        setChecked(window, "preStretchLinearImage", false);
        setChecked(window, "generateStarMask", true);
        setComboData(window, "starOutputMode", QStringLiteral("both"));
        return;
    }
    if (mode == "stars-prestretch")
    {
        setChecked(window, "preStretchLinearImage", true);
        setChecked(window, "generateStarMask", true);
        setComboData(window, "starOutputMode", QStringLiteral("both"));
        return;
    }
    unknownMode("Starnet++", mode);
}

void configureStarMask(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        setChecked(window, "preStretchLinearImage", false);
        setChecked(window, "lightness", false);
        return;
    }
    if (mode == "prestretch")
    {
        setChecked(window, "preStretchLinearImage", true);
        setChecked(window, "lightness", false);
        return;
    }
    if (mode == "lightness")
    {
        setChecked(window, "preStretchLinearImage", false);
        setChecked(window, "lightness", true);
        setChecked(window, "binaryMask", false);
        return;
    }
    if (mode == "binary")
    {
        setChecked(window, "preStretchLinearImage", false);
        setChecked(window, "lightness", true);
        setChecked(window, "binaryMask", true);
        setDouble(window, "lowerLimitValue", 0.25);
        setDouble(window, "upperLimitValue", 0.75);
        setDouble(window, "smoothnessValue", 0.0);
        return;
    }
    unknownMode("Starnet++ Star Mask", mode);
}

void configureDeepSnr(QWidget& window, std::string_view mode)
{
    if (mode == "-")
    {
        setChecked(window, "preStretchLinearImage", false);
        return;
    }
    if (mode == "prestretch")
    {
        setChecked(window, "preStretchLinearImage", true);
        return;
    }
    unknownMode("DeepSNR", mode);
}

[[nodiscard]] std::vector<ExternalTool> makeTools()
{
    std::vector<ExternalTool> table;
    table.push_back(ExternalTool{.name = "Starnet",
                                 .action = "actionStarnet",
                                 .descriptorId = "star_object.actionStarnet",
                                 .className = "StarnetProcess",
                                 .processName = "Starnet++",
                                 .product = "starnet2",
                                 .programName = "StarNet2",
                                 .settingsKey = SettingsKeys::TOOLS_STARNET_PATH,
                                 .scene = "stars-rgb-float32",
                                 .inPlace = false,
                                 .outputSuffixes = {" - starless"},
                                 .errorTitle = "Error running StarNet2",
                                 .errorLead = "An error occurred when running StarNet2 process",
                                 .open = openOver<StarnetProcess>,
                                 .openStopping = openOver<Stopping<StarnetProcess>>,
                                 .configure = configureStarnet,
                                 .run = [](QWidget& window) { pressOk(window); }});
    table.push_back(ExternalTool{.name = "StarnetStarMask",
                                 .action = "actionStarnet_Star_Mask",
                                 .descriptorId = "star_object.actionStarnet_Star_Mask",
                                 .className = "StarnetStarMaskProcess",
                                 .processName = "Starnet++ Star Mask",
                                 .product = "starnet2",
                                 .programName = "StarNet2",
                                 .settingsKey = SettingsKeys::TOOLS_STARNET_PATH,
                                 .scene = "stars-rgb-float32",
                                 .inPlace = false,
                                 .outputSuffixes = {" - STARMASK"},
                                 .errorTitle = "Error running StarNet2",
                                 .errorLead = "An error occurred when running StarNet2 process",
                                 .open = openOver<StarnetStarMaskProcess>,
                                 .openStopping = openOver<Stopping<StarnetStarMaskProcess>>,
                                 .configure = configureStarMask,
                                 .run = [](QWidget& window) { pressOk(window); }});
    table.push_back(ExternalTool{.name = "DeepSnr",
                                 .action = "actionDeepSNR",
                                 .descriptorId = "denoising.actionDeepSNR",
                                 .className = "DeepSNRProcess",
                                 .processName = "DeepSNR",
                                 .product = "deepsnr",
                                 .programName = "DeepSNR",
                                 .settingsKey = SettingsKeys::TOOLS_DEEPSNR_PATH,
                                 .scene = "stars-rgb-float32",
                                 .inPlace = true,
                                 .outputSuffixes = {},
                                 .errorTitle = "Error running DeepSNR",
                                 .errorLead = "An error occurred when running DeepSNR process",
                                 .open = openOver<SmallDeepSnr>,
                                 .openStopping = openOver<Stopping<SmallDeepSnr>>,
                                 .configure = configureDeepSnr,
                                 .run = [](QWidget& window) { pressOk(window); }});
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

const std::vector<ExternalTool>& externalTools()
{
    static const std::vector<ExternalTool> tools = makeTools();
    return tools;
}

const ExternalTool* toolNamed(std::string_view name)
{
    for (const ExternalTool& tool : externalTools())
    {
        if (name == tool.name)
        {
            return &tool;
        }
    }
    return nullptr;
}

StandInTools::StandInTools(ProcessTestContext& context, const ExternalTool& tool, std::string_view behaviour)
{
    EXPECT_TRUE(m_directory.isValid());
    qputenv(kStandInToolVariable, QByteArray(tool.product));
    qputenv(kStandInRecordVariable, m_directory.filePath(QStringLiteral("runs.txt")).toUtf8());
    qputenv(kStandInKeepInputVariable, keptInputPath().toUtf8());
    setBehaviour(behaviour);
    for (const ExternalTool& each : externalTools())
    {
        (void)context.upsert(QString::fromLatin1(each.settingsKey),
                             each.product == std::string_view(tool.product) ? executablePath() : QString());
    }
}

StandInTools::~StandInTools()
{
    qunsetenv(kStandInToolVariable);
    qunsetenv(kStandInBehaviourVariable);
    qunsetenv(kStandInRecordVariable);
    qunsetenv(kStandInKeepInputVariable);
    qunsetenv(kStandInSlowMsVariable);
}

void StandInTools::setBehaviour(std::string_view behaviour)
{
    qputenv(kStandInBehaviourVariable, QByteArray(behaviour.data(), static_cast<qsizetype>(behaviour.size())));
}

QStringList StandInTools::recordedRuns() const
{
    QFile file(m_directory.filePath(QStringLiteral("runs.txt")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString StandInTools::keptInputPath() const
{
    return m_directory.filePath(QStringLiteral("handed.tiff"));
}

QString StandInTools::executablePath()
{
    return QCoreApplication::applicationFilePath();
}

QString realToolPath(const ExternalTool& tool)
{
    const QByteArray variable =
        QByteArray("AFTERNIGHT_TEST_") + QByteArray(tool.product).toUpper() + QByteArray("_PATH");
    if (const QByteArray named = qgetenv(variable.constData()); !named.isEmpty())
    {
        return QString::fromUtf8(named);
    }
    const bool starnet = std::string_view(tool.product) == "starnet2";
    const QStringList directories =
        starnet ? an::ui::starnet::commonInstallDirectories() : an::ui::deepsnr::commonInstallDirectories();
    const QStringList names = starnet ? an::ui::starnet::executableNames() : an::ui::deepsnr::executableNames();
    for (const QString& directory : directories)
    {
        for (const QString& name : names)
        {
            const QString candidate = QDir(directory).filePath(name);
            if (QFileInfo(candidate).isExecutable())
            {
                return candidate;
            }
        }
    }
    return QString();
}

bool realToolsRequired()
{
    return qgetenv("AFTERNIGHT_TEST_REQUIRE_EXTERNAL_TOOLS") == QByteArray("1");
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

void setComboData(QWidget& window, const char* name, const QString& data)
{
    if (auto* combo = widgetOf<QComboBox>(window, name))
    {
        const int index = combo->findData(data);
        EXPECT_GE(index, 0) << name << " offers no " << data.toStdString();
        if (index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    }
}

QPushButton* okButtonOf(QWidget& window)
{
    QDialogButtonBox* box = buttonBoxOf(window);
    return box != nullptr ? box->button(QDialogButtonBox::Ok) : nullptr;
}

void pressOk(QWidget& window)
{
    QPushButton* ok = okButtonOf(window);
    ASSERT_NE(ok, nullptr) << "the window has no OK button";
    ASSERT_TRUE(ok->isEnabled()) << "OK is disabled";
    ok->click();
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

bool runAndSettle(ProcessTestContext& context, QWidget& window, const ExternalTool& tool)
{
    tool.run(window);
    return settleWindow(context, window);
}

bool runInstalledAndSettle(ProcessTestContext& context, QWidget& window, const ExternalTool& tool)
{
    tool.run(window);
    const auto until = std::chrono::steady_clock::now() + kInstalledProgramPatience;
    while (!windowIdle(window) && std::chrono::steady_clock::now() < until)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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

std::optional<an::consumer::ImageAsset>
madeAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height, std::uint32_t channels,
          an::SampleType sampleType,
          const std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t channel)>& value)
{
    const an::tests::viewer::Channels schema = channels == 1u   ? an::tests::viewer::Channels::Grey
                                               : channels == 4u ? an::tests::viewer::Channels::Rgba
                                                                : an::tests::viewer::Channels::Rgb;
    std::optional<an::consumer::ImageAsset> made;
    const bool dispatched = an::dispatchSampleType(sampleType, [&]<typename SampleT>() {
        made = an::tests::viewer::makeAsset<SampleT>(
            session, width, height, schema, [&](std::uint32_t x, std::uint32_t y, std::uint32_t channel) -> SampleT {
                const double fraction = value(x, y, channel);
                if constexpr (std::is_floating_point_v<SampleT>)
                {
                    return static_cast<SampleT>(fraction);
                }
                else
                {
                    return an::convertSample<double, SampleT>(fraction, an::ConversionPolicy{}).value;
                }
            });
    });
    EXPECT_TRUE(dispatched);
    return made;
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

std::optional<an::consumer::ImageAsset> withMetadata(const an::consumer::EngineSession& session,
                                                     const an::consumer::ImageAsset& source,
                                                     const an::ImageMetadata& metadata)
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
    an::Result<an::consumer::ImageAsset> asset = an::consumer::ImageAsset::adopt(session, std::move(*copy));
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

std::optional<an::ImageMetadata> assetMetadata(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit([](const auto& typed) { return typed.metadata(); }, lease->image());
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

an::Result<an::ui::processes::ExternalToolAnswers> runSupport(const an::consumer::EngineSession& session,
                                                              const an::consumer::ImageAsset& source,
                                                              const an::ui::processes::ExternalToolRequest& request,
                                                              bool stopAtOnce)
{
    an::Result<an::consumer::AssetReadLease> lease = source.read();
    if (!lease.hasValue())
    {
        return lease.status();
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    if (!scope.hasValue())
    {
        return scope.status();
    }
    if (stopAtOnce)
    {
        scope->requestStop();
    }
    an::Result<an::ui::processes::ExternalToolAnswers> answers =
        an::ui::processes::runExternalTool(*scope, an::ui::ProcessSource::of(*lease), request);
    scope->finish(answers.hasValue() ? an::Status::success() : answers.status());
    return answers;
}

} // namespace an::tests::processes::externaltools
