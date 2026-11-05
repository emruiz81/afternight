// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Every behaviour record of the geometry and filter-array processes and the image menu's mirrors and turns,
/// reproduced through the window or the operation as a person runs it: the record's scene opened in a view made active,
/// the window set to the record's mode and applied, and what it publishes - the view's image, or the new view the mode
/// names - held to the record, by the record's own bound or by the bound an accepted difference states.

#include "Test_ProcessesGeometry/GeometryTestSupport.h"

#include "processes/CfaProcessSupport.h"
#include "processes/GeometryProcessSupport.h"
#include "processes/MergeCFAProcess.h"

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>
#include <AfterNightEngine/Algorithms/Geometry/Geometry.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace fixtures = an::tests::fixtures;
namespace shared = an::tests::processes::shared;
using namespace an::tests::processes;
using namespace an::tests::processes::channels;
using namespace an::tests::processes::geometry;

/// Whether a sample of a record's result is one an accepted difference explains a larger departure at: the position,
/// the channel, what the record holds, what the process made, and what the difference's reference holds there, in the
/// record's units.
using Explanation = std::function<bool(const fixtures::FixtureSamples& recorded, std::uint32_t x, std::uint32_t y,
                                       std::uint32_t channel, double held, double made, double reference)>;

/// What an explanation compares against, made from the record's input by the engine: nothing for none.
using ReferenceMaker = std::function<std::optional<fixtures::FixtureSamples>(const an::consumer::EngineSession& session,
                                                                             const fixtures::Expectation& record)>;

/// A difference from the record that a case accepts: the records it applies to, how far a result may be from the
/// record, and why.
struct AcceptedDifference
{
    /// The start of the ids of the records it applies to.
    const char* records = "";

    /// How far every sample may be from the record, in the record's units.
    double bound = 0.0;

    /// How far a sample the explanation names may be, beyond the bound.
    double explainedBound = 0.0;

    /// Which samples it names; empty for none.
    Explanation explains;

    /// What the explanation compares against; empty for none.
    ReferenceMaker reference;

    /// Why, for the failure message.
    const char* why = "";
};

/// Whether a position lies within a band at a picture's edges.
[[nodiscard]] bool inEdgeBand(const fixtures::FixtureSamples& picture, std::uint32_t x, std::uint32_t y,
                              std::uint32_t band)
{
    return x < band || y < band || x + band >= picture.width() || y + band >= picture.height();
}

/// An edge band explanation: a sample within the band the method's border rule fills.
[[nodiscard]] Explanation edgeBand(std::uint32_t band)
{
    return [band](const fixtures::FixtureSamples& recorded, std::uint32_t x, std::uint32_t y, std::uint32_t, double,
                  double, double) { return inEdgeBand(recorded, x, y, band); };
}

/// The samples of an asset an admitted operation made from a scene.
[[nodiscard]] std::optional<fixtures::FixtureSamples>
madeFromScene(const an::consumer::EngineSession& session, std::string_view scene,
              const std::function<an::Result<an::consumer::ImageAsset>(const an::ui::ExecutionScope&,
                                                                       const an::ui::ProcessSource&)>& operation)
{
    // An asset, not a view: a view registers itself with the context, which a case would have to take it out of.
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
    if (image == nullptr || !samples.has_value())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> asset = an::tests::fixtures::engine::makeImageAsset(session, *image, *samples);
    EXPECT_TRUE(asset.hasValue()) << asset.status().message();
    if (!asset.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        session, an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    an::Result<an::consumer::AssetReadLease> lease = asset->read();
    EXPECT_TRUE(scope.hasValue() && lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return std::nullopt;
    }
    an::Result<an::consumer::ImageAsset> made = operation(*scope, an::ui::ProcessSource::of(*lease));
    EXPECT_TRUE(made.hasValue()) << made.status().message();
    lease->release();
    scope->finish(an::Status::success());
    if (!made.hasValue())
    {
        return std::nullopt;
    }
    return assetSamples(*made);
}

/// The resize a record's mode asked for, with the dark-ringing guard off: the plain interpolation both guards start
/// from.
[[nodiscard]] std::optional<fixtures::FixtureSamples> unguardedResize(const an::consumer::EngineSession& session,
                                                                      const fixtures::Expectation& record)
{
    const std::string mode = modeOf(record);
    const an::KernelInterpolation interpolation =
        mode == "percent-150-bicubic" ? an::KernelInterpolation::Cubic : an::KernelInterpolation::Lanczos4;
    const an::ResizeRequest resize{.width = record.result.width(),
                                   .height = record.result.height(),
                                   .sampling = {.interpolation = interpolation,
                                                .border = an::KernelBorderMode::Clamp,
                                                .invalid = an::SamplingInvalidPolicy::RequireAll},
                                   .ringing = an::ResizeRingingPolicy::Keep};
    return madeFromScene(session, record.inputName,
                         [resize](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
                             const std::array<an::ui::processes::GeometryStep, 1u> steps{resize};
                             return an::ui::processes::transformImage(scope, source, steps, "reference");
                         });
}

/// Where either dark-ringing guard replaced the plain interpolation: the record or this result departs from it there.
[[nodiscard]] bool guarded(const fixtures::FixtureSamples&, std::uint32_t, std::uint32_t, std::uint32_t, double held,
                           double made, double reference)
{
    return std::fabs(held - reference) > 1.0e-4 || std::fabs(made - reference) > 1.0e-4;
}

/// The accepted differences.
[[nodiscard]] const std::vector<AcceptedDifference>& acceptedDifferences()
{
    static const std::vector<AcceptedDifference> differences{
        // The dark-ringing guard compares a bicubic or Lanczos result with a guide: in the record an imaging library's
        // pixel-area resize, which repeats samples when it enlarges, and in the engine a bilinear sample. Where neither
        // guard replaced the interpolation, the two agree.
        AcceptedDifference{.records = "resize.percent-200-auto/",
                           .bound = 1.0e-4,
                           .explainedBound = 0.05,
                           .explains = guarded,
                           .reference = unguardedResize,
                           .why = "the guard's guide when enlarging"},
        AcceptedDifference{.records = "resize.percent-150-bicubic/",
                           .bound = 1.0e-4,
                           .explainedBound = 0.05,
                           .explains = guarded,
                           .reference = unguardedResize,
                           .why = "the guard's guide when enlarging"},
        // The demosaic family fills the band its method reaches from the edge with one shared rule; inside it the
        // methods agree with the record within a code. A 24 by 16 mosaic is all band for LMMSE.
        AcceptedDifference{.records = "debayer.vng4/",
                           .bound = 1.0,
                           .explainedBound = 256.0,
                           .explains = edgeBand(an::demosaicMethodReach(an::DemosaicMethod::Vng4)),
                           .why = "the shared border rule over VNG4's reach"},
        AcceptedDifference{.records = "debayer.amaze/",
                           .bound = 1.0,
                           .explainedBound = 700.0,
                           .explains = edgeBand(an::demosaicMethodReach(an::DemosaicMethod::Amaze)),
                           .why = "the shared border rule over AMaZE's reach"},
        AcceptedDifference{.records = "debayer.lmmse/",
                           .bound = 1.0,
                           .explainedBound = 17000.0,
                           .explains = edgeBand(an::demosaicMethodReach(an::DemosaicMethod::Lmmse)),
                           .why = "the shared border rule over LMMSE's reach"},
        // The compatibility pair's resampled product uses the engine's stated Lanczos definition, edge-clamped, where
        // the record used an imaging library's.
        AcceptedDifference{.records = "split-cfa.ha-oiii-upsample-ha-ha/",
                           .bound = 1.0,
                           .explainedBound = 800.0,
                           .explains = [](const fixtures::FixtureSamples&, std::uint32_t, std::uint32_t, std::uint32_t,
                                          double, double, double) { return true; },
                           .why = "the stated Lanczos definition of the upsampled H-alpha"},
        AcceptedDifference{.records = "split-cfa.ha-oiii-downsample-oiii-oiii/",
                           .bound = 1.0,
                           .explainedBound = 160.0,
                           .explains = edgeBand(2u),
                           .why = "the stated Lanczos definition's edge clamp on the downsampled OIII"}};
    return differences;
}

[[nodiscard]] const AcceptedDifference* acceptedFor(const std::string& id)
{
    for (const AcceptedDifference& difference : acceptedDifferences())
    {
        if (id.rfind(difference.records, 0u) == 0u)
        {
            return &difference;
        }
    }
    return nullptr;
}

/// Holds what a process made to a record: by the record's own classification, or by the accepted difference that
/// applies to it.
void expectRecord(const an::consumer::EngineSession& session, const fixtures::Expectation& record,
                  const fixtures::FixtureSamples& made)
{
    const fixtures::ExpectationComparison compared = fixtures::compareWithExpectation(record, made);
    if (compared.matched)
    {
        return;
    }
    const AcceptedDifference* accepted = acceptedFor(record.id);
    const fixtures::FixtureSamples& held = record.result;
    ASSERT_EQ(made.width(), held.width()) << record.id;
    ASSERT_EQ(made.height(), held.height()) << record.id;
    ASSERT_EQ(made.channelCount(), held.channelCount()) << record.id;
    std::optional<fixtures::FixtureSamples> reference;
    if (accepted != nullptr && accepted->reference)
    {
        reference = accepted->reference(session, record);
        ASSERT_TRUE(reference.has_value()) << record.id;
    }
    double largest = 0.0;
    double largestExplained = 0.0;
    std::uint64_t outside = 0u;
    for (std::uint32_t c = 0u; c < held.channelCount(); ++c)
    {
        for (std::uint32_t y = 0u; y < held.height(); ++y)
        {
            for (std::uint32_t x = 0u; x < held.width(); ++x)
            {
                const double a = held.value(x, y, c);
                const double b = made.value(x, y, c);
                const double difference = std::isnan(a) && std::isnan(b)   ? 0.0
                                          : std::isnan(a) || std::isnan(b) ? std::numeric_limits<double>::infinity()
                                                                           : std::fabs(a - b);
                const double r = reference.has_value() ? reference->value(x, y, c) : 0.0;
                const bool explained =
                    accepted != nullptr && accepted->explains && accepted->explains(held, x, y, c, a, b, r);
                if (explained)
                {
                    largestExplained = (std::max)(largestExplained, difference);
                }
                else
                {
                    largest = (std::max)(largest, difference);
                }
                const double bound = accepted == nullptr ? 0.0 : explained ? accepted->explainedBound : accepted->bound;
                if (difference > bound)
                {
                    ++outside;
                }
            }
        }
    }
    std::cout << "[ record ] " << record.id << ": largest " << largest << ", largest explained " << largestExplained
              << ", " << compared.differingSamples << " samples differ" << std::endl;
    if (accepted == nullptr)
    {
        ADD_FAILURE() << record.id << " differs from the record and no accepted difference applies: " << compared.reason
                      << " (largest " << largest << ")";
        return;
    }
    EXPECT_EQ(outside, 0u) << record.id << ": " << outside << " samples beyond the accepted bound " << accepted->bound
                           << " (largest " << largest << ", explained " << largestExplained << ") - " << accepted->why;
}

/// Which of a new-view process's outputs a mode's record holds.
[[nodiscard]] std::size_t outputOf(std::string_view mode)
{
    const std::string text(mode);
    if (text.rfind("plane-", 0u) == 0u)
    {
        return static_cast<std::size_t>(std::stoul(text.substr(6u)));
    }
    if (text.size() > 5u && text.substr(text.size() - 5u) == "-oiii")
    {
        return 1u;
    }
    return 0u;
}

/// The modes the records hold for demosaic methods the engine does not implement - DCB, HPHD and IGV - which the dialog
/// does not offer.
[[nodiscard]] bool unofferedMode(std::string_view records, std::string_view mode)
{
    return records == "debayer" && (mode == "dcb" || mode == "hphd" || mode == "igv");
}

class GeometryRecordTest : public ::testing::TestWithParam<const char*>
{
};

/// The views four planes a mosaic splits into, as Split CFA makes them, under names a Merge CFA window picks.
[[nodiscard]] std::vector<ViewHandle> planeViews(ProcessTestContext& context, std::string_view scene)
{
    std::vector<ViewHandle> views;
    // An asset, not a view: a view registers itself with the context, which the case would have to take it out of.
    const fixtures::FixtureImage* image = fixtures::fixtureNamed(scene);
    const std::optional<fixtures::FixtureSamples> samples = sceneSamples(scene);
    EXPECT_TRUE(image != nullptr && samples.has_value());
    if (image == nullptr || !samples.has_value())
    {
        return views;
    }
    an::Result<an::consumer::ImageAsset> mosaic =
        an::tests::fixtures::engine::makeImageAsset(context.engineSession(), *image, *samples);
    EXPECT_TRUE(mosaic.hasValue());
    if (!mosaic.hasValue())
    {
        return views;
    }
    an::Result<an::ui::ExecutionScope> scope = an::ui::ExecutionScope::admit(
        context.engineSession(), an::ui::ExecutionRequest{.purpose = an::consumer::OperationPurpose::Apply}, {});
    EXPECT_TRUE(scope.hasValue());
    an::Result<an::consumer::AssetReadLease> lease = mosaic->read();
    EXPECT_TRUE(lease.hasValue());
    if (!scope.hasValue() || !lease.hasValue())
    {
        return views;
    }
    an::Result<std::vector<an::consumer::ImageAsset>> planes =
        an::ui::processes::splitMosaicPlanes(*scope, an::ui::ProcessSource::of(*lease));
    EXPECT_TRUE(planes.hasValue()) << planes.status().message();
    lease->release();
    scope->finish(an::Status::success());
    if (!planes.hasValue())
    {
        return views;
    }
    for (std::size_t plane = 0u; plane < planes->size(); ++plane)
    {
        views.push_back(assetView(std::move((*planes)[plane]), "cfa" + std::to_string(plane)));
        activate(context, *views.back());
    }
    return views;
}

} // namespace

// Every record of a process's corpus file, through its window as a person runs it.
TEST_P(GeometryRecordTest, EveryRecordOfTheProcessIsReproducedThroughItsWindow)
{
    const std::string file = GetParam();
    const GeometryProcess* under = processOfRecords(file);
    ASSERT_NE(under, nullptr) << file;
    const std::vector<fixtures::Expectation> records = recordsOf(file);
    ASSERT_FALSE(records.empty()) << file;
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const fixtures::Expectation& record : records)
    {
        SCOPED_TRACE(record.id);
        const fixtures::FixtureImage* image = fixtures::fixtureNamed(record.inputName);
        ASSERT_NE(image, nullptr);
        std::string error;
        const std::optional<fixtures::FixtureSamples> input = fixtures::materializeFixture(*image, error);
        ASSERT_TRUE(input.has_value()) << error;
        ASSERT_TRUE(fixtures::expectationDescribes(record, *image, *input))
            << "the record was captured from another input than this scene";
        const std::string mode = modeOf(record);
        if (unofferedMode(file, mode))
        {
            // The method is not offered; GeometryWindowTest holds the dialog to that.
            continue;
        }
        std::optional<fixtures::FixtureSamples> made;
        if (under->landing == Landing::TopLevel)
        {
            std::vector<ViewHandle> planes = planeViews(*context, record.inputName);
            ASSERT_EQ(planes.size(), 4u);
            WindowHandle window = under->open(nullptr);
            for (std::size_t plane = 0u; plane < 4u; ++plane)
            {
                const std::string selector = "cfa" + std::to_string(plane) + "Selector";
                setComboText(*window, selector.c_str(), QString::fromStdString(planes[plane]->getUniqueName()));
            }
            setComboText(*window, "patternSelector", QStringLiteral("RGGB"));
            const std::size_t before = context->madeViews.size();
            ASSERT_TRUE(applyAndSettle(*context, *window));
            ASSERT_EQ(context->madeViews.size(), before + 1u)
                << context->errorTexts.join(QStringLiteral("; ")).toStdString();
            made = documentSamples(context->madeViews.back()->document());
            window.reset();
            for (ViewHandle& plane : planes)
            {
                dropView(*context, plane);
            }
        }
        else
        {
            ViewHandle target = sceneView(context->engineSession(), record.inputName);
            ASSERT_NE(target, nullptr);
            activate(*context, *target);
            WindowHandle window = under->open(target.get());
            under->configure(*window, mode);
            const std::size_t before = context->madeViews.size();
            ASSERT_TRUE(applyAndSettle(*context, *window));
            if (under->landing == Landing::InPlace)
            {
                EXPECT_EQ(context->madeViews.size(), before);
                made = documentSamples(target->document());
            }
            else
            {
                const std::size_t output = outputOf(mode);
                ASSERT_GT(context->madeViews.size(), before + output)
                    << context->errorTexts.join(QStringLiteral("; ")).toStdString();
                made = documentSamples(context->madeViews[before + output]->document());
            }
            window.reset();
            dropView(*context, target);
        }
        ASSERT_TRUE(made.has_value());
        expectRecord(context->engineSession(), record, *made);
        if (file == "resize")
        {
            // The window's resize: the edge sample repeated and the dark-ringing guard on, stated here rather than
            // through the request the window builds.
            const std::string kernel = modeOf(record);
            const an::KernelInterpolation interpolation =
                kernel == "percent-150-bicubic"   ? an::KernelInterpolation::Cubic
                : kernel == "percent-150-nearest" ? an::KernelInterpolation::Nearest
                : kernel == "percent-50-auto"     ? an::KernelInterpolation::Area
                                                  : an::KernelInterpolation::Lanczos4;
            const an::ResizeRequest stated{.width = record.result.width(),
                                           .height = record.result.height(),
                                           .sampling = {.interpolation = interpolation,
                                                        .border = an::KernelBorderMode::Clamp,
                                                        .invalid = an::SamplingInvalidPolicy::RequireAll},
                                           .ringing = an::ResizeRingingPolicy::SuppressDarkRinging};
            const std::optional<fixtures::FixtureSamples> expected =
                madeFromScene(context->engineSession(), record.inputName,
                              [stated](const an::ui::ExecutionScope& scope, const an::ui::ProcessSource& source) {
                                  const std::array<an::ui::processes::GeometryStep, 1u> steps{stated};
                                  return an::ui::processes::transformImage(scope, source, steps, "reference");
                              });
            ASSERT_TRUE(expected.has_value());
            EXPECT_EQ(made->digest(), expected->digest()) << "the window's resize is not the stated one";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Processes, GeometryRecordTest,
                         ::testing::Values("crop", "resize", "integer-resample", "cfa-integer-resample", "debayer",
                                           "split-cfa", "merge-cfa"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name = info.param;
                             for (char& c : name)
                             {
                                 if (c == '-')
                                 {
                                     c = '_';
                                 }
                             }
                             return name;
                         });

// Every record of the image menu's mirrors and turns, through the image operations coordinator on the active view.
TEST(GeometryTurnRecordTest, EveryRecordOfTheMirrorsAndTurnsIsReproducedThroughTheMenu)
{
    struct Turn
    {
        const char* records;
        std::function<void(const an::ui::ImageOperationsCoordinator&)> run;
    };
    const std::vector<Turn> turns{
        {"horizontal-mirror", [](const an::ui::ImageOperationsCoordinator& o) { o.horizontalMirror(); }},
        {"vertical-mirror", [](const an::ui::ImageOperationsCoordinator& o) { o.verticalMirror(); }},
        {"rotate-180", [](const an::ui::ImageOperationsCoordinator& o) { o.rotate180Degree(); }},
        {"rotate-90-left", [](const an::ui::ImageOperationsCoordinator& o) { o.rotate90DegreeLeft(); }},
        {"rotate-90-right", [](const an::ui::ImageOperationsCoordinator& o) { o.rotate90DegreeRight(); }}};
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    for (const Turn& turn : turns)
    {
        const std::vector<fixtures::Expectation> records = recordsOf(turn.records);
        ASSERT_FALSE(records.empty()) << turn.records;
        for (const fixtures::Expectation& record : records)
        {
            SCOPED_TRACE(record.id);
            ViewHandle target = sceneView(context->engineSession(), record.inputName);
            ASSERT_NE(target, nullptr);
            activate(*context, *target);
            turn.run(imageOperations());
            ASSERT_TRUE(settleAll(*context));
            const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
            ASSERT_TRUE(made.has_value());
            expectRecord(context->engineSession(), record, *made);
            dropView(*context, target);
        }
    }
}

namespace
{

/// A rotation through the engine's affine warp under a stated destination-to-source matrix and canvas, held to the unit
/// interval as both generations hold a normalized floating result.
[[nodiscard]] std::optional<fixtures::FixtureSamples> warpedScene(const an::consumer::EngineSession& session,
                                                                  std::string_view scene, std::uint32_t width,
                                                                  std::uint32_t height,
                                                                  const std::array<double, 6>& destinationToSource,
                                                                  an::KernelInterpolation interpolation)
{
    return madeFromScene(
        session, scene,
        [&](const an::ui::ExecutionScope& scope,
            const an::ui::ProcessSource& source) -> an::Result<an::consumer::ImageAsset> {
            const an::AffineWarpRequest warp{.width = width,
                                             .height = height,
                                             .destinationToSource = destinationToSource,
                                             .sampling = {.interpolation = interpolation,
                                                          .border = an::KernelBorderMode::Constant,
                                                          .constantBorder = 0.0f,
                                                          .invalid = an::SamplingInvalidPolicy::RequireAll}};
            return std::visit(
                [&](const auto& typed) -> an::Result<an::consumer::ImageAsset> {
                    using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
                    const std::optional<an::ImageDescriptor> described = typed.descriptor();
                    const auto view = typed.view();
                    const an::Result<an::GeometryPlan> plan = an::planAffineWarp(*described, warp);
                    if (!plan.hasValue())
                    {
                        return plan.status();
                    }
                    an::Result<an::Image<SampleT>> result = an::Image<SampleT>::create(
                        scope.engine(), plan->destinationDescriptor(), an::StorageArrangement::Planar,
                        an::StorageInitialization::Zeroed, typed.metadata());
                    an::Result<an::ValidityMask> mask =
                        an::ValidityMask::create(scope.engine(), plan->destinationDescriptor().extent());
                    if (!result.hasValue() || !mask.hasValue())
                    {
                        return result.hasValue() ? mask.status() : result.status();
                    }
                    std::optional<an::ValidityMaskView<std::uint8_t>> maskView = mask->view();
                    {
                        std::optional<an::ImageWriteAccess<SampleT>> access = result->write();
                        const an::Result<an::GeometryResult> ran = an::transformImageGeometry(
                            scope.execution(), an::kSampleTypeOf<SampleT>, view->data(), view->layout(),
                            access->view().data(), access->view().layout(), *plan, an::GeometrySelection{}, *maskView);
                        if (!ran.hasValue())
                        {
                            return ran.status();
                        }
                        if constexpr (std::is_floating_point_v<SampleT>)
                        {
                            for (std::uint32_t c = 0u; c < plan->destinationDescriptor().extent().channelCount(); ++c)
                            {
                                for (std::uint32_t y = 0u; y < height; ++y)
                                {
                                    auto row = access->view().row(c, y);
                                    for (SampleT& sample : *row)
                                    {
                                        sample = (std::clamp)(sample, SampleT{0}, SampleT{1});
                                    }
                                }
                            }
                        }
                    }
                    return an::consumer::ImageAsset::adopt(scope.session(), an::consumer::AnyImage(std::move(*result)));
                },
                source.image());
        });
}

/// The destination-to-source matrix of a clockwise turn by an angle about a source centre, onto a canvas whose point
/// `destinationCentre` the source centre lands on.
[[nodiscard]] std::array<double, 6> turnAbout(double degrees, double sourceX, double sourceY, double destinationX,
                                              double destinationY)
{
    const double c = std::cos(degrees * std::numbers::pi / 180.0);
    const double s = std::sin(degrees * std::numbers::pi / 180.0);
    // source = R(-angle) (destination - destination centre) + source centre.
    return {c, s, sourceX - c * destinationX - s * destinationY, -s, c, sourceY + s * destinationX - c * destinationY};
}

} // namespace

// Every rotation record. A record turns the image about (width / 2, height / 2) in pixel-centre coordinates - half a
// pixel right of and below its centre - and rounds a grown canvas to the nearest whole pixel; the record is that turn,
// reproduced through the engine's affine warp with the record's matrix. The window turns about the image's centre onto
// a canvas holding every turned pixel, which the same warp with the true centre reproduces.
TEST(GeometryRotateRecordTest, EveryRotationRecordIsTheTurnAboutTheRecordsCentreAndTheWindowTurnsAboutTheImages)
{
    const std::vector<fixtures::Expectation> records = recordsOf("rotate");
    ASSERT_FALSE(records.empty());
    const shared::RegistrySnapshot snapshot;
    std::unique_ptr<ProcessTestContext> context = openProcessContext(4u);
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    const GeometryProcess* rotate = processOfRecords("rotate");
    ASSERT_NE(rotate, nullptr);
    for (const fixtures::Expectation& record : records)
    {
        SCOPED_TRACE(record.id);
        const std::string mode = modeOf(record);
        const bool expand = mode == "angle-minus-17.5-resized-bilinear";
        const double degrees = expand ? -17.5 : 30.0;
        const an::KernelInterpolation interpolation =
            expand ? an::KernelInterpolation::Linear : an::KernelInterpolation::Lanczos4;
        const std::optional<fixtures::FixtureSamples> input = sceneSamples(record.inputName);
        ASSERT_TRUE(input.has_value());
        const double w = input->width();
        const double h = input->height();
        const double c = std::fabs(std::cos(degrees * std::numbers::pi / 180.0));
        const double s = std::fabs(std::sin(degrees * std::numbers::pi / 180.0));

        // The record's turn: about (w / 2, h / 2), onto the bounding box rounded to whole pixels.
        const double boxWidth = expand ? w * c + h * s : w;
        const double boxHeight = expand ? w * s + h * c : h;
        const auto recordWidth = static_cast<std::uint32_t>(std::lround(boxWidth));
        const auto recordHeight = static_cast<std::uint32_t>(std::lround(boxHeight));
        ASSERT_EQ(recordWidth, record.result.width());
        ASSERT_EQ(recordHeight, record.result.height());
        const std::optional<fixtures::FixtureSamples> recordTurn =
            warpedScene(context->engineSession(), record.inputName, recordWidth, recordHeight,
                        turnAbout(degrees, w / 2.0, h / 2.0, boxWidth / 2.0, boxHeight / 2.0), interpolation);
        ASSERT_TRUE(recordTurn.has_value());
        const std::optional<fixtures::FixtureSamples> trueCentre = warpedScene(
            context->engineSession(), record.inputName, recordWidth, recordHeight,
            turnAbout(degrees, (w - 1.0) / 2.0, (h - 1.0) / 2.0, (boxWidth - 1.0) / 2.0, (boxHeight - 1.0) / 2.0),
            interpolation);
        ASSERT_TRUE(trueCentre.has_value());
        // The imaging library places its samples at a thirty-second of a pixel and interpolates from tables, which a
        // turn computed exactly departs from by about one per cent of full scale at the edges and less inside.
        const bool codes = record.result.sampleType() == fixtures::FixtureSampleType::UInt16;
        const double whole = largestInteriorDifference(*recordTurn, record.result, 0u);
        const double inside = largestInteriorDifference(*recordTurn, record.result, 2u);
        EXPECT_LE(whole, codes ? 900.0 : 0.015) << "the record is not the turn about (w / 2, h / 2)";
        EXPECT_LE(inside, codes ? 200.0 : 0.007) << "the record is not the turn about (w / 2, h / 2)";
        EXPECT_LT(10.0 * whole, largestInteriorDifference(*trueCentre, record.result, 0u))
            << "the record is as close to the turn about the image's centre";

        // The window's turn: about the image's centre, onto the engine's canvas.
        ViewHandle target = sceneView(context->engineSession(), record.inputName);
        ASSERT_NE(target, nullptr);
        activate(*context, *target);
        WindowHandle window = rotate->open(target.get());
        rotate->configure(*window, mode);
        ASSERT_TRUE(applyAndSettle(*context, *window));
        const std::optional<fixtures::FixtureSamples> made = documentSamples(target->document());
        ASSERT_TRUE(made.has_value());
        const double madeWidth = made->width();
        const double madeHeight = made->height();
        if (expand)
        {
            EXPECT_GE(madeWidth, boxWidth) << "the grown canvas holds every turned pixel";
            EXPECT_GE(madeHeight, boxHeight) << "the grown canvas holds every turned pixel";
            EXPECT_LT(madeWidth, boxWidth + 2.0);
            EXPECT_LT(madeHeight, boxHeight + 2.0);
        }
        else
        {
            EXPECT_EQ(made->width(), input->width());
            EXPECT_EQ(made->height(), input->height());
        }
        const std::optional<fixtures::FixtureSamples> centred = warpedScene(
            context->engineSession(), record.inputName, made->width(), made->height(),
            turnAbout(degrees, (w - 1.0) / 2.0, (h - 1.0) / 2.0, (madeWidth - 1.0) / 2.0, (madeHeight - 1.0) / 2.0),
            interpolation);
        ASSERT_TRUE(centred.has_value());
        const double exact = made->sampleType() == fixtures::FixtureSampleType::UInt16 ? 1.0 : 1.0e-5;
        EXPECT_LE(largestInteriorDifference(*centred, *made, 0u), exact) << "the window turns about the image's centre";
        window.reset();
        dropView(*context, target);
    }
}
