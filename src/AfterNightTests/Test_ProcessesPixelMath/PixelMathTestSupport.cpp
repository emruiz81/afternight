// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The PixelMath window as the cases open and run it, and a run of the support unit made directly over an asset.

#include "Test_ProcessesPixelMath/PixelMathTestSupport.h"

#include "session/ExecutionScope.h"

#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/ValidityDescriptor.h>
#include <AfterNightEngine/Image/ValidityMask.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>
#include <type_traits>
#include <variant>

namespace an::tests::processes::pixelmath
{

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;

/// The window with a fixed seed, so a case can state what `random()` draws.
class SeededWindow final : public PixelMathProcess
{
public:
    SeededWindow(ImageViewControl* target, std::uint64_t seed) : PixelMathProcess(target), m_seed(seed) {}

protected:
    [[nodiscard]] std::uint64_t drawRandomSeed() const override
    {
        return m_seed;
    }

private:
    std::uint64_t m_seed;
};

/// The window whose run asks for its own stop once its operation is admitted.
class StoppingWindow final : public PixelMathProcess
{
public:
    using PixelMathProcess::PixelMathProcess;

protected:
    [[nodiscard]] an::ui::processes::AdmissionHook admissionHook() const override
    {
        return [](an::ui::ExecutionScope& scope) { scope.requestStop(); };
    }
};

[[nodiscard]] WindowHandle kept(PixelMathProcess* window)
{
    WindowHandle handle(window);
    handle->setAttribute(Qt::WA_DeleteOnClose, false);
    handle->resize(900, 700);
    return handle;
}

} // namespace

void WindowDeleter::operator()(QWidget* window) const
{
    if (window == nullptr)
    {
        return;
    }
    (void)pumpUntil([window]() {
        const auto* process = dynamic_cast<const ImageProcessControl*>(window);
        return process == nullptr || process->isRunIdle();
    });
    delete window;
    QCoreApplication::processEvents();
}

WindowHandle openWindow(ImageViewControl* target, std::uint64_t seed)
{
    return kept(new SeededWindow(target, seed));
}

WindowHandle openStoppingWindow(ImageViewControl* target)
{
    return kept(new StoppingWindow(target));
}

void setExpression(QWidget& window, an::ui::processes::PixelMathExpressionTab tab, const QString& text)
{
    static constexpr const char* kEditors[] = {"rgbkTextEdit", "greenTextEdit", "blueTextEdit", "alphaTextEdit"};
    setPlainText(window, kEditors[static_cast<std::size_t>(tab)], text);
}

void setPlainText(QWidget& window, const char* name, const QString& text)
{
    if (auto* edit = widgetOf<QPlainTextEdit>(window, name))
    {
        edit->setPlainText(text);
    }
}

void setChecked(QWidget& window, const char* name, bool checked)
{
    if (auto* box = widgetOf<QCheckBox>(window, name))
    {
        box->setChecked(checked);
    }
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

void setLineText(QWidget& window, const char* name, const QString& text)
{
    if (auto* edit = widgetOf<QLineEdit>(window, name))
    {
        edit->setText(text);
    }
}

void pressApply(QWidget& window)
{
    auto* box = window.findChild<QDialogButtonBox*>(QStringLiteral("buttonBox"));
    ASSERT_NE(box, nullptr) << "the window has no buttons";
    QPushButton* apply = box->button(QDialogButtonBox::Apply);
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(apply->isEnabled()) << "Apply is disabled";
    apply->click();
}

bool settleWindow(ProcessTestContext& context, const PixelMathProcess& window)
{
    if (!pumpUntil([&window]() { return window.isRunIdle(); }))
    {
        return false;
    }
    return channels::settleAll(context) && window.isRunIdle();
}

bool applyAndSettle(ProcessTestContext& context, PixelMathProcess& window)
{
    pressApply(window);
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

std::optional<an::ValidityMask>
explicitMask(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
             std::uint32_t planes,
             const std::function<bool(std::uint32_t plane, std::uint32_t x, std::uint32_t y)>& valid)
{
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    EXPECT_TRUE(scope.has_value());
    if (!scope.has_value())
    {
        return std::nullopt;
    }
    const std::optional<an::ImageExtent> extent =
        an::ImageExtent::create(width, height, planes, scope->engine().limits());
    EXPECT_TRUE(extent.has_value());
    if (!extent.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::ValidityMask> mask = an::ValidityMask::createAllValid(scope->engine(), *extent);
    EXPECT_TRUE(mask.hasValue()) << mask.status().message();
    if (!mask.hasValue())
    {
        return std::nullopt;
    }
    const std::optional<an::ValidityMaskView<std::uint8_t>> bits = mask->view();
    EXPECT_TRUE(bits.has_value());
    if (!bits.has_value())
    {
        return std::nullopt;
    }
    for (std::uint32_t plane = 0u; plane < planes; ++plane)
    {
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                EXPECT_TRUE(bits->setValid(plane, x, y, valid(plane, x, y)));
            }
        }
    }
    return std::optional<an::ValidityMask>(std::move(*mask));
}

an::ImageMetadata explicitMaskMetadata()
{
    an::ImageSemanticsRequest meaning;
    meaning.validity =
        *an::ValidityDescriptor::create(an::ValidityDescriptorRequest{.rule = an::ValidityRule::ExplicitMask});
    const an::Result<an::ImageSemantics> semantics = an::ImageSemantics::create(meaning);
    EXPECT_TRUE(semantics.hasValue());
    return an::ImageMetadata::create(an::AcquisitionMetadata{}, *semantics, an::AstrometricSolution{}, an::Provenance{},
                                     an::CodecMetadata{}, std::nullopt);
}

std::optional<an::consumer::ImageAsset> sceneAsset(const an::consumer::EngineSession& session, std::string_view scene)
{
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    EXPECT_NE(image, nullptr) << "no fixture scene " << std::string(scene);
    if (image == nullptr)
    {
        return std::nullopt;
    }
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(*image, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    return asset.hasValue() ? std::optional<an::consumer::ImageAsset>(std::move(*asset)) : std::nullopt;
}

std::optional<an::consumer::ImageAsset>
filledAsset(const an::consumer::EngineSession& session, std::uint32_t width, std::uint32_t height,
            std::uint32_t channels, an::SampleType sampleType, an::NumericDomain domain,
            const std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t channel)>& value)
{
    const an::tests::viewer::Channels shape = channels == 1u   ? an::tests::viewer::Channels::Grey
                                              : channels == 3u ? an::tests::viewer::Channels::Rgb
                                                               : an::tests::viewer::Channels::Rgba;
    std::optional<an::consumer::ImageAsset> made;
    const bool dispatched = an::dispatchSampleType(sampleType, [&]<typename SampleT>() {
        made = an::tests::viewer::makeAsset<SampleT>(
            session, width, height, shape,
            [&value](std::uint32_t x, std::uint32_t y, std::uint32_t channel) {
                return static_cast<SampleT>(value(x, y, channel));
            },
            domain);
    });
    EXPECT_TRUE(dispatched);
    return made;
}

DirectRun runDirect(const an::consumer::EngineSession& session, const an::consumer::ImageAsset& target,
                    const an::ui::processes::PixelMathSettings& settings,
                    const an::ui::processes::PixelMathImageSet& images, std::uint64_t seed)
{
    DirectRun run;
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    if (!scope.hasValue())
    {
        run.status = scope.status();
        return run;
    }
    an::Result<an::consumer::AssetReadLease> lease = target.read();
    if (!lease.hasValue())
    {
        run.status = lease.status();
        scope->finish(run.status);
        return run;
    }
    an::Result<an::consumer::ImageAsset> made =
        an::ui::processes::runPixelMath(*scope, an::ui::ProcessSource::of(*lease), settings, images, seed, &run.report);
    lease->release();
    run.status = made.hasValue() ? an::Status::success() : made.status();
    scope->finish(run.status);
    if (made.hasValue())
    {
        run.result.emplace(std::move(*made));
    }
    return run;
}

std::optional<StoredSamples> storedSamples(const an::consumer::ImageAsset& asset)
{
    an::Result<an::consumer::AssetReadLease> lease = asset.read();
    EXPECT_TRUE(lease.hasValue()) << lease.status().message();
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    return std::visit(
        [](const auto& image) -> std::optional<StoredSamples> {
            const auto view = image.view();
            const std::optional<an::ImageDescriptor> described = image.descriptor();
            EXPECT_TRUE(view.has_value() && described.has_value());
            if (!view.has_value() || !described.has_value())
            {
                return std::nullopt;
            }
            StoredSamples stored;
            stored.width = described->extent().width();
            stored.height = described->extent().height();
            stored.channelCount = described->extent().channelCount();
            stored.values.reserve(static_cast<std::size_t>(stored.width) * stored.height * stored.channelCount);
            for (std::uint32_t channel = 0u; channel < stored.channelCount; ++channel)
            {
                for (std::uint32_t y = 0u; y < stored.height; ++y)
                {
                    const auto row = view->row(channel, y);
                    EXPECT_TRUE(row.has_value()) << "the image is not planar";
                    if (!row.has_value())
                    {
                        return std::nullopt;
                    }
                    for (const auto sample : *row)
                    {
                        stored.values.push_back(static_cast<double>(sample));
                    }
                }
            }
            return stored;
        },
        lease->image());
}

an::ui::processes::PixelMathSettings expressionSettings(const std::string& expression)
{
    an::ui::processes::PixelMathSettings settings;
    settings.expressions[0] = expression;
    settings.singleExpression = true;
    return settings;
}

} // namespace an::tests::processes::pixelmath
