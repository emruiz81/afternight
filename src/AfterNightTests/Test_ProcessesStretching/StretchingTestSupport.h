// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the stretching processes' cases share: the process context of the framework's cases, a process window of any
/// of the four dialogs opened over a view as the launcher opens one, documents made from the fixture layer's images,
/// the behaviour records the dialogs are held to, and the widgets a case drives by the names the dialogs' layouts give
/// them.
#pragma once

#include "Test_ControlsProcessFramework/ProcessFrameworkTestSupport.h"

#include "fixtures/common/FixtureCatalogue.h"
#include "fixtures/common/FixtureExpectation.h"
#include "fixtures/engine/EngineFixtureAdapter.h"

#include "app/FileCompanions.h"
#include "controls/ImageRTPreviewControl.h"
#include "controls/SequenceContainers.h"
#include "processes/StretchProcessSupport.h"
#include "session/ImageDocument.h"
#include "widgets/CurvesWidget.h"

#include <AfterNightEngine/Algorithms/Pointwise/Stretch.h>
#include <AfterNightEngine/IO/SequenceDescriptor.h>
#include <AfterNightEngine/Image/ConversionPolicy.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageConversion.h>
#include <AfterNightEngine/Image/StorageArrangement.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QRect>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::stretching
{

namespace fixtures = an::tests::fixtures;

using an::tests::processes::InstalledContext;
using an::tests::processes::openProcessContext;
using an::tests::processes::ProcessTestContext;
using an::tests::processes::settleView;
using an::tests::viewer::pumpQuietly;
using an::tests::viewer::pumpUntil;

/// A view, let go by the context that registered it and deleted with the case: a case that makes several views over one
/// context leaves none of them behind in the context's list, which every process window reads when it opens.
struct ViewDeleter
{
    void operator()(ImageViewControl* view) const
    {
        if (view == nullptr)
        {
            return;
        }
        if (an::ui::IAppContext* context = an::ui::appContext())
        {
            if (an::ui::IViewControlManager* manager = context->getViewControlManager())
            {
                manager->removeViewControl(view, false);
            }
        }
        delete view;
    }
};
using ViewHandle = std::unique_ptr<ImageViewControl, ViewDeleter>;

/// A view of a document, as the application makes one: its history begins with the state it was made with.
[[nodiscard]] inline ViewHandle makeTargetView(const std::string& name, an::ui::ImageDocument&& document)
{
    ViewHandle view(new ImageViewControl());
    view->initialize(name, ViewControlMode::standard);
    view->resize(640, 480);
    view->setDocument(std::move(document));
    view->initializeNewImageHistory("Image opened");
    return view;
}

/// A process window, closed and deleted with the case once everything it started has landed.
template <typename ProcessT> struct WindowDeleter
{
    void operator()(ProcessT* process) const
    {
        if (process == nullptr)
        {
            return;
        }
        (void)pumpUntil([process]() { return process->isProcessIdle(); });
        delete process;
        QCoreApplication::processEvents();
    }
};

template <typename ProcessT> using WindowHandle = std::unique_ptr<ProcessT, WindowDeleter<ProcessT>>;

/// A process window over a target, as the launcher opens one, without the delete-on-close the framework sets so the
/// case decides when it goes.
template <typename ProcessT> [[nodiscard]] WindowHandle<ProcessT> openWindow(ImageViewControl& target)
{
    WindowHandle<ProcessT> process(new ProcessT(&target));
    process->setAttribute(Qt::WA_DeleteOnClose, false);
    process->resize(900, 640);
    return process;
}

/// Turns the event loop until a window's runs, its measurements and its preview view's work have all landed.
[[nodiscard]] inline bool settleWindow(const ImageRTPreviewControl& process)
{
    if (!pumpUntil([&process]() { return process.isProcessIdle(); }))
    {
        return false;
    }
    pumpQuietly();
    return process.isProcessIdle();
}

/// A fixture image as a document of the session.
[[nodiscard]] inline std::optional<an::ui::ImageDocument> documentOf(const an::consumer::EngineSession& session,
                                                                     const fixtures::FixtureImage& fixture)
{
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(fixture, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, fixture, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QString::fromStdString(fixture.name)});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return std::nullopt;
    }
    return std::move(*document);
}

/// A view of a fixture image, as the application makes one.
[[nodiscard]] inline ViewHandle viewOf(const an::consumer::EngineSession& session,
                                       const fixtures::FixtureImage& fixture)
{
    std::optional<an::ui::ImageDocument> document = documentOf(session, fixture);
    if (!document.has_value())
    {
        return nullptr;
    }
    ViewHandle view = makeTargetView(fixture.name, std::move(*document));
    EXPECT_TRUE(settleView(*view));
    return view;
}

/// A view of a fixture image of the catalogue, by its name.
[[nodiscard]] inline ViewHandle fixtureView(const an::consumer::EngineSession& session, const std::string& name)
{
    const fixtures::FixtureImage* fixture = fixtures::fixtureNamed(name);
    EXPECT_NE(fixture, nullptr) << name;
    if (fixture == nullptr)
    {
        return nullptr;
    }
    return viewOf(session, *fixture);
}

/// A scene larger than the preview area the framework centres in an image, so the area is a rectangle inside it: 900
/// by 700 pixels of a gradient under noise, its levels fractions of full scale for a floating representation and of
/// the sixteen-bit range for an integer one.
[[nodiscard]] inline fixtures::FixtureImage
largeScene(fixtures::FixtureSampleType sampleType, fixtures::FixtureChannels channels = fixtures::FixtureChannels::Rgb)
{
    const bool floating = fixtures::isFloating(sampleType);
    const double scale = floating ? 1.0 : 65535.0;
    fixtures::FixtureImage image;
    image.name = std::string("large-scene-") + std::string(fixtures::sampleTypeName(sampleType)) + "-" +
                 std::string(fixtures::channelsName(channels));
    image.width = 900u;
    image.height = 700u;
    image.channels = channels;
    image.sampleType = sampleType;
    image.domain = floating ? fixtures::FixtureDomain::NormalizedLinear : fixtures::FixtureDomain::SensorCode;
    image.pattern = fixtures::FixturePattern::GradientNoise;
    image.seed = 41u;
    const std::vector<fixtures::FixtureChannelLevels> levels{
        {.base = 0.04 * scale, .perColumn = 0.0005 * scale, .perRow = 0.0002 * scale, .amplitude = 0.03 * scale},
        {.base = 0.08 * scale, .perColumn = 0.0003 * scale, .perRow = 0.0004 * scale, .amplitude = 0.03 * scale},
        {.base = 0.55 * scale, .perColumn = -0.0002 * scale, .perRow = 0.0001 * scale, .amplitude = 0.03 * scale}};
    for (std::uint32_t channel = 0u; channel < fixtures::channelCountOf(channels); ++channel)
    {
        image.levels.push_back(levels[channel]);
    }
    return image;
}

/// What a document holds now, as the fixture layer reads samples.
[[nodiscard]] inline std::optional<fixtures::FixtureSamples> samplesOf(const an::ui::ImageDocument& document)
{
    const std::shared_ptr<const an::consumer::ImageAsset> asset = document.asset();
    EXPECT_TRUE(asset != nullptr);
    if (!asset)
    {
        return std::nullopt;
    }
    std::string error;
    std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readAssetSamples(*asset, error);
    EXPECT_TRUE(samples.has_value()) << error;
    return samples;
}

/// Every record of one of the behaviour record files, by its base name.
[[nodiscard]] inline std::vector<fixtures::Expectation> behaviorRecords(const std::string& file)
{
    std::string error;
    const std::optional<std::filesystem::path> directory = fixtures::expectationDirectory(error);
    EXPECT_TRUE(directory.has_value()) << error;
    if (!directory.has_value())
    {
        return {};
    }
    std::optional<std::vector<fixtures::Expectation>> records =
        fixtures::readExpectationFile(*directory / "behavior" / (file + ".expectations"), error);
    EXPECT_TRUE(records.has_value()) << error;
    return records.has_value() ? std::move(*records) : std::vector<fixtures::Expectation>{};
}

/// The record of an id, or null.
[[nodiscard]] inline const fixtures::Expectation* recordNamed(const std::vector<fixtures::Expectation>& records,
                                                              const std::string& id)
{
    for (const fixtures::Expectation& record : records)
    {
        if (record.id == id)
        {
            return &record;
        }
    }
    ADD_FAILURE() << "no record " << id;
    return nullptr;
}

/// The largest absolute difference between two sample sets of one shape and representation, read as doubles; infinity
/// when their shapes or representations differ.
[[nodiscard]] inline double largestDifference(const fixtures::FixtureSamples& left,
                                              const fixtures::FixtureSamples& right)
{
    if (left.width() != right.width() || left.height() != right.height() ||
        left.channelCount() != right.channelCount() || left.sampleType() != right.sampleType())
    {
        return std::numeric_limits<double>::infinity();
    }
    double largest = 0.0;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (std::uint32_t y = 0u; y < left.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < left.width(); ++x)
            {
                largest = (std::max)(largest, std::abs(left.value(x, y, channel) - right.value(x, y, channel)));
            }
        }
    }
    return largest;
}

/// How many samples of a rectangle of two sample sets differ at all, the rectangle in both sets' own pixels.
[[nodiscard]] inline std::uint64_t differingSamplesIn(const fixtures::FixtureSamples& left,
                                                      const fixtures::FixtureSamples& right, const QRect& area)
{
    std::uint64_t differing = 0u;
    for (std::uint32_t channel = 0u; channel < left.channelCount(); ++channel)
    {
        for (int y = area.top(); y <= area.bottom(); ++y)
        {
            for (int x = area.left(); x <= area.right(); ++x)
            {
                const double a = left.value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), channel);
                const double b = right.value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), channel);
                if (a != b)
                {
                    ++differing;
                }
            }
        }
    }
    return differing;
}

/// A child widget of a window by the name its layout gives it; fails the case when there is none.
template <typename WidgetT> [[nodiscard]] WidgetT* child(const QWidget& window, const char* name)
{
    auto* widget = window.findChild<WidgetT*>(QString::fromLatin1(name));
    EXPECT_NE(widget, nullptr) << name;
    return widget;
}

/// A dialog's curve editor; its class declares no meta-object of its own, so it is found by its type.
[[nodiscard]] inline CurvesWidget* curvesWidgetOf(const QWidget& process)
{
    for (QWidget* widget : process.findChildren<QWidget*>())
    {
        if (auto* curves = dynamic_cast<CurvesWidget*>(widget))
        {
            return curves;
        }
    }
    return nullptr;
}

/// Sets a spin box of a window, as a person typing a value does.
inline void setValue(const QWidget& window, const char* name, double value)
{
    if (auto* box = child<QDoubleSpinBox>(window, name))
    {
        box->setValue(value);
    }
}

/// Checks or clears a check box of a window, as a click does.
inline void setChecked(const QWidget& window, const char* name, bool checked)
{
    if (auto* box = child<QAbstractButton>(window, name))
    {
        box->setChecked(checked);
    }
}

/// Chooses an entry of a combo box of a window by its text, as a person picking it does.
inline void choose(const QWidget& window, const char* name, const QString& text)
{
    if (auto* box = child<QComboBox>(window, name))
    {
        const int index = box->findText(text);
        EXPECT_GE(index, 0) << name << " offers no " << text.toStdString();
        box->setCurrentIndex(index);
    }
}

/// Turns a window's preview on and waits until its first preview is shown.
inline void openPreview(ImageRTPreviewControl& process)
{
    setChecked(process, "preview", true);
    ASSERT_NE(process.previewView(), nullptr);
    ASSERT_TRUE(settleWindow(process));
}

/// Turns a window's preview area on - the area centred in the image, as wide and as tall as the framework makes it -
/// and waits until its preview is shown.
inline void openPreviewArea(ImageRTPreviewControl& process)
{
    ImageViewControl* preview = process.previewView();
    ASSERT_NE(preview, nullptr);
    QCheckBox* area = preview->getRTPreviewAreaCheckBox();
    ASSERT_NE(area, nullptr);
    area->setChecked(true);
    ASSERT_TRUE(settleWindow(process));
    ASSERT_FALSE(process.previewArea().isEmpty());
}

/// The records' median of a set of values: a histogram of as many bins as values, at most 65,536, between their
/// extremes, the middle count interpolated between two bins and carried back to the values' range, in Float32 - the
/// estimator the records take every median and median absolute deviation from, restated so the records can be
/// explained.
[[nodiscard]] inline float histogramEstimatedMedian(const std::vector<float>& values)
{
    if (values.empty())
    {
        return 0.0f;
    }
    const auto [lowest, highest] = std::minmax_element(values.begin(), values.end());
    const float minVal = *lowest;
    const float maxVal = *highest;
    if (std::fabs(maxVal - minVal) == 0.0f)
    {
        return minVal;
    }
    const std::size_t histoSize = (std::min<std::size_t>)(65536u, values.size());
    const float scale = static_cast<float>(histoSize - 1u) / (maxVal - minVal);
    std::vector<std::uint32_t> histo(histoSize, 0u);
    for (const float value : values)
    {
        ++histo[static_cast<std::uint16_t>(scale * (value - minVal))];
    }
    std::size_t k = 0u;
    std::size_t count = 0u;
    const float threshold = 0.5f * static_cast<float>(values.size());
    while (static_cast<float>(count) < threshold)
    {
        count += histo[k++];
    }
    float median = static_cast<float>(k);
    if (k > 0u)
    {
        const std::size_t before = count - histo[k - 1u];
        const float c0 = static_cast<float>(count) - threshold;
        const float c1 = threshold - static_cast<float>(before);
        median = (c1 * static_cast<float>(k) + c0 * static_cast<float>(k - 1u)) / (c0 + c1);
    }
    median /= scale;
    median += minVal;
    return std::clamp(median, minVal, maxVal);
}

/// The records' median and median absolute deviation of every channel of an image, each through
/// `histogramEstimatedMedian` over the channel's samples as fractions of full scale: a float as it is, an unsigned
/// integer divided by its largest code in Float32.
[[nodiscard]] inline std::vector<an::StretchChannelStatistics>
estimatedStatistics(const fixtures::FixtureSamples& samples)
{
    const double fullScale =
        fixtures::isFloating(samples.sampleType()) ? 1.0 : fixtures::highestOf(samples.sampleType());
    std::vector<an::StretchChannelStatistics> statistics;
    for (std::uint32_t channel = 0u; channel < samples.channelCount(); ++channel)
    {
        std::vector<float> values;
        values.reserve(static_cast<std::size_t>(samples.width()) * samples.height());
        for (std::uint32_t y = 0u; y < samples.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < samples.width(); ++x)
            {
                values.push_back(static_cast<float>(samples.value(x, y, channel)) / static_cast<float>(fullScale));
            }
        }
        const float median = histogramEstimatedMedian(values);
        std::vector<float> deviations;
        deviations.reserve(values.size());
        for (const float value : values)
        {
            deviations.push_back(std::fabs(value - median));
        }
        statistics.push_back(an::StretchChannelStatistics{
            .median = static_cast<double>(median),
            .medianAbsoluteDeviation = static_cast<double>(histogramEstimatedMedian(deviations))});
    }
    return statistics;
}

/// What the engine makes of a fixture image when called directly, the dialog out of the way: the image's working copy
/// handed to `operate`, then brought back to the image's representation at full scale.
[[nodiscard]] inline std::optional<fixtures::FixtureSamples>
engineDirectly(const an::consumer::EngineSession& session, const fixtures::FixtureImage& fixture,
               const std::function<an::Status(const an::ExecutionContext&, an::consumer::AnyImage&)>& operate)
{
    std::string error;
    const std::optional<fixtures::FixtureSamples> samples = fixtures::materializeFixture(fixture, error);
    EXPECT_TRUE(samples.has_value()) << error;
    if (!samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = fixtures::engine::makeImageAsset(session, fixture, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!asset.hasValue() || !scope.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    EXPECT_TRUE(lease.hasValue());
    if (!lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::AnyImage> working =
        an::ui::processes::makeStretchWorkingCopy(scope->engine(), scope->execution(), lease->image());
    EXPECT_TRUE(working.hasValue()) << working.status().message();
    if (!working.hasValue())
    {
        return std::nullopt;
    }
    const an::Status operated = operate(scope->execution(), *working);
    EXPECT_TRUE(operated.ok()) << operated.message();
    if (operated.failed())
    {
        return std::nullopt;
    }
    std::optional<fixtures::FixtureSamples> result = std::visit(
        [&](const auto& original) -> std::optional<fixtures::FixtureSamples> {
            using ResultT = typename std::remove_cvref_t<decltype(original)>::Sample;
            const std::optional<an::ImageDescriptor> described = original.descriptor();
            if (!described.has_value())
            {
                return std::nullopt;
            }
            an::Result<an::Image<ResultT>> converted = an::Image<ResultT>::create(
                scope->engine(), *described, an::StorageArrangement::Planar, an::StorageInitialization::Uninitialized);
            if (!converted.hasValue())
            {
                return std::nullopt;
            }
            {
                const std::optional<an::ImageWriteAccess<ResultT>> access = converted->write();
                const an::Status done = std::visit(
                    [&](const auto& typed) -> an::Status {
                        using WorkT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                        const auto view = typed.view();
                        an::ConversionCounters counters;
                        return an::convertImageSamples(scope->execution(), an::Image<WorkT>::kSampleType, view->data(),
                                                       view->layout(), an::Image<ResultT>::kSampleType,
                                                       access->view().data(), access->view().layout(),
                                                       an::ConversionPolicy{}, counters);
                    },
                    *working);
                if (done.failed())
                {
                    return std::nullopt;
                }
            }
            std::string readError;
            return fixtures::engine::readImageSamples(an::consumer::AnyImage(std::move(*converted)), readError);
        },
        lease->image());
    EXPECT_TRUE(result.has_value());
    return result;
}

/// The engine's `stretchImage` over a working image in place.
[[nodiscard]] inline an::Status stretchInPlace(const an::ExecutionContext& execution, an::consumer::AnyImage& working,
                                               const an::StretchImageRequest& request)
{
    return std::visit(
        [&](auto& typed) -> an::Status {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            if constexpr (!std::is_floating_point_v<SampleT>)
            {
                return an::Status::failure(an::ErrorCode::Internal, "the working image is not floating");
            }
            else
            {
                const std::optional<an::ImageWriteAccess<SampleT>> access = typed.write();
                return an::stretchImage(execution, an::Image<SampleT>::kSampleType, access->view().data(),
                                        access->view().layout(), access->view().data(), access->view().layout(),
                                        request)
                    .status();
            }
        },
        working);
}

/// A sequence view of a three-frame sixteen-bit grey SER written below the context's temporary directory, made by the
/// context's view factory as the application makes one; its frames are a gradient under three different gains, so each
/// frame's statistics differ.
[[nodiscard]] inline ImageViewControl* sequenceView(ProcessTestContext& context, const QString& path)
{
    an::tests::viewer::writeSerFile(
        context.engineSession(), path, 24u, 16u, 3u, [](std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
            return (0.05f + 0.01f * static_cast<float>(x + y)) * (1.0f + 0.5f * static_cast<float>(frame));
        });
    an::Result<an::ui::ImageDocument> document =
        an::ui::openSequenceDocument(context.engineSession(), path, an::SequenceFormat::Ser, {});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    ImageViewControl* view = context.createNewImageViewControlFromSequence(
        nullptr, std::move(*document), nullptr, false, "frames", ViewControlMode::standard, nullptr, false, false);
    EXPECT_NE(view, nullptr);
    if (view != nullptr)
    {
        EXPECT_TRUE(settleView(*view));
    }
    return view;
}

/// Every frame of a SER file, as the fixture layer reads samples.
[[nodiscard]] inline std::vector<fixtures::FixtureSamples> framesOf(const an::consumer::EngineSession& session,
                                                                    const QString& path)
{
    std::vector<fixtures::FixtureSamples> frames;
    std::optional<an::consumer::OperationScope> scope = an::tests::viewer::admitted(session);
    if (!scope.has_value())
    {
        return frames;
    }
    an::Result<an::ui::OpenedSequence> opened =
        an::ui::openSequenceSource(scope->engine(), scope->execution(), path, an::SequenceFormat::Ser);
    EXPECT_TRUE(opened.hasValue()) << opened.status().message();
    if (!opened.hasValue())
    {
        return frames;
    }
    for (;;)
    {
        an::Result<std::optional<std::pair<std::uint64_t, an::consumer::AnyImage>>> read =
            an::ui::readSequenceFrame(scope->engine(), scope->execution(), opened->source, std::nullopt);
        EXPECT_TRUE(read.hasValue()) << read.status().message();
        if (!read.hasValue() || !read->has_value())
        {
            return frames;
        }
        std::string error;
        std::optional<fixtures::FixtureSamples> samples = fixtures::engine::readImageSamples((*read)->second, error);
        EXPECT_TRUE(samples.has_value()) << error;
        if (samples.has_value())
        {
            frames.push_back(std::move(*samples));
        }
    }
}

/// The pixel of the channel-keeping cases whose blue sample holds no number.
inline constexpr std::uint32_t kNoNumberX = 3u;
/// The row of that pixel.
inline constexpr std::uint32_t kNoNumberY = 2u;

/// A floating colour view whose red is inside the unit interval and whose green and blue are not: green above one and
/// blue below zero, one blue sample holding no number - what a stretch of the red channel alone has to leave as it is.
[[nodiscard]] inline ViewHandle outOfRangeColourView(const an::consumer::EngineSession& session)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<float>(
        session, 24u, 16u, an::tests::viewer::Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            if (c == 0u)
            {
                return 0.05f + 0.03f * static_cast<float>(x) + 0.02f * static_cast<float>(y);
            }
            if (c == 1u)
            {
                return 1.25f + 0.01f * static_cast<float>(x);
            }
            return x == kNoNumberX && y == kNoNumberY ? std::numeric_limits<float>::quiet_NaN()
                                                      : -0.2f + 0.001f * static_cast<float>(y);
        });
    if (!asset.has_value())
    {
        return nullptr;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("out-of-range")});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    ViewHandle view = makeTargetView("out-of-range", std::move(*document));
    EXPECT_TRUE(settleView(*view));
    return view;
}

/// A thirty-two-bit colour view whose green and blue codes need every one of their bits: a working copy that held them
/// in single precision would bring them back off by up to a hundred and twenty-eight.
[[nodiscard]] inline ViewHandle wideCodeView(const an::consumer::EngineSession& session)
{
    std::optional<an::consumer::ImageAsset> asset = an::tests::viewer::makeAsset<std::uint32_t>(
        session, 24u, 16u, an::tests::viewer::Channels::Rgb, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            if (c == 0u)
            {
                return 200000000u + 90000000u * x + 7000000u * y;
            }
            return c == 1u ? 4294967295u - 7u * x - 13u * y : 123456789u + 97u * x + 101u * y;
        });
    if (!asset.has_value())
    {
        return nullptr;
    }
    an::Result<an::ui::ImageDocument> document = an::ui::ImageDocument::create(
        std::move(*asset), an::ui::DocumentNames{.displayName = QStringLiteral("wide-codes")});
    EXPECT_TRUE(document.hasValue()) << document.status().message();
    if (!document.hasValue())
    {
        return nullptr;
    }
    ViewHandle view = makeTargetView("wide-codes", std::move(*document));
    EXPECT_TRUE(settleView(*view));
    return view;
}

/// Holds that a result's green and blue are the source's sample for sample - one that held no number still holding
/// none - and that its red changed.
inline void expectRedAloneChanged(const fixtures::FixtureSamples& source, const fixtures::FixtureSamples& result)
{
    ASSERT_EQ(result.width(), source.width());
    ASSERT_EQ(result.height(), source.height());
    std::uint64_t redChanged = 0u;
    std::uint64_t othersChanged = 0u;
    for (std::uint32_t y = 0u; y < source.height(); ++y)
    {
        for (std::uint32_t x = 0u; x < source.width(); ++x)
        {
            redChanged += result.value(x, y, 0u) != source.value(x, y, 0u) ? 1u : 0u;
            for (std::uint32_t c = 1u; c < 3u; ++c)
            {
                const double before = source.value(x, y, c);
                const double after = result.value(x, y, c);
                const bool same = std::isnan(before) ? std::isnan(after) : after == before;
                othersChanged += same ? 0u : 1u;
                EXPECT_TRUE(same) << "channel " << c << " at " << x << ',' << y << ": " << before << " became "
                                  << after;
            }
        }
    }
    EXPECT_GT(redChanged, 0u) << "the red channel is stretched";
    EXPECT_EQ(othersChanged, 0u) << "the channels the stretch leaves alone are as they were";
}

} // namespace an::tests::processes::stretching
