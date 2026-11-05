// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleGeometry.h>
#include <AfterNightEngine/Algorithms/Drizzle/DrizzlePixmap.h>

#include <AfterNightEngine/Runtime/EngineContext.h>
#include <AfterNightEngine/Runtime/ExecutionContext.h>
#include <AfterNightEngine/Runtime/MemoryResource.h>
#include <AfterNightEngine/Runtime/TraceSink.h>

#include <array>
#include <bit>
#include <cmath>
#include <chrono>
#include <future>
#include <limits>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{

an::DrizzleMapDescriptor geometryDescriptor(std::uint32_t width = 17u, std::uint32_t height = 13u, double scale = 2.0,
                                            const an::DrizzleMappingRequest& mapping = {})
{
    const auto extent = an::ImageExtent::create(width, height, 1u, an::defaultEngineLimits());
    const auto image = an::ImageDescriptor::create({.extent = *extent,
                                                    .sampleType = an::SampleType::Float32,
                                                    .channelSchema = an::ChannelSchema::Mono,
                                                    .colorSpace = an::ColorSpace::Unspecified,
                                                    .numericDomain = an::NumericDomain::NormalizedLinear,
                                                    .cfa = {},
                                                    .coordinateOrigin = an::CoordinateOrigin::TopLeft,
                                                    .pixelAspectRatio = 1.0});
    an::DrizzleRequest request{.input = *image, .frameCount = 1u};
    request.parameters.scale = scale;
    const auto model = an::DrizzleModel::create(request);
    return *an::DrizzleMapDescriptor::create(*model, mapping);
}

TEST(DrizzleGeometryTest, IdentityUsesPixelCentresAndInverseUndoesScaleAndTranslation)
{
    const auto descriptor = geometryDescriptor(17u, 13u, 1.5, {.postTranslation = {3.0, -2.0}});
    for (const an::PixelCoordinate point : {an::PixelCoordinate{0.0, 0.0}, {4.25, 8.75}, {-0.5, 12.5}})
    {
        const auto mapped = an::mapDrizzlePoint(descriptor, point);
        ASSERT_TRUE(mapped.hasValue());
        EXPECT_DOUBLE_EQ(mapped->x, (point.x + 0.5 + 3.0) * 1.5 - 0.5);
        EXPECT_DOUBLE_EQ(mapped->y, (point.y + 0.5 - 2.0) * 1.5 - 0.5);
        const auto inverse = an::mapDrizzlePointInverse(descriptor, *mapped);
        ASSERT_TRUE(inverse.hasValue());
        EXPECT_NEAR(inverse->x, point.x, 1.0e-12);
        EXPECT_NEAR(inverse->y, point.y, 1.0e-12);
    }
}

TEST(DrizzleGeometryTest, BoundedMapKeepsExactHaloAndMatchesDenseCoordinates)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto descriptor = geometryDescriptor();
    auto plan = an::DrizzleGeometryPlan::create(*execution, descriptor);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    auto dense = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
    ASSERT_TRUE(dense.hasValue());
    auto block = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 4u, .rowCount = 3u, .haloRows = 1u});
    ASSERT_TRUE(block.hasValue());
    auto view = block->view();
    auto full = dense->view();
    ASSERT_TRUE(view.hasValue());
    ASSERT_TRUE(full.hasValue());
    EXPECT_EQ(view->storedRowBegin(), 3u);
    EXPECT_EQ(view->storedRowEnd(), 8u);
    EXPECT_EQ(view->coreRowBegin(), 4u);
    EXPECT_EQ(view->coreRowEnd(), 7u);
    EXPECT_EQ(block->coordinateBytes(), 17u * 5u * 8u + 5u * 8u);
    EXPECT_FALSE(view->containsRow(8u));
    EXPECT_FALSE(view->isDense());
    for (std::uint32_t y = 3u; y < 8u; ++y)
        for (std::uint32_t x = 0u; x < 17u; ++x)
        {
            EXPECT_EQ(view->at(x, y)->x, full->at(x, y)->x);
            EXPECT_EQ(view->at(x, y)->y, full->at(x, y)->y);
        }
    EXPECT_EQ(view->at(0u, 2u).status().code(), an::ErrorCode::OutOfRange);
}

an::DrizzleMappingRequest curvedMapping(std::uint32_t polynomialOrder, std::uint32_t sipOrder)
{
    an::RegistrationTransformRequest transform;
    transform.linearKind = an::RegistrationTransformKind::Projective;
    transform.sourceToReference = {1.01, -0.03, 2.25, 0.02, 0.98, -1.75, 0.0001, -0.0002, 1.0};
    transform.polynomial.order = polynomialOrder;
    if (polynomialOrder != 0u)
    {
        transform.polynomial.normalizationOrigin = {23.5, 17.5};
        transform.polynomial.normalizationScale = {32.0, 24.0};
        transform.polynomial.x[1] = 1.0;
        transform.polynomial.y[2] = 1.0;
        for (std::uint32_t i = 3u; i < an::registrationPolynomialTermCount(polynomialOrder); ++i)
        {
            transform.polynomial.x[i] = 0.0002 * (i % 2u == 0u ? 1.0 : -1.0);
            transform.polynomial.y[i] = 0.0003 * (i % 3u == 0u ? 1.0 : -1.0);
        }
    }
    an::DrizzleMappingRequest mapping{.transform = an::RegistrationTransform::create(transform).value(),
                                      .postTranslation = {-0.25, 0.75}};
    if (sipOrder != 0u)
    {
        an::SipDistortionRequest sip;
        sip.referencePixel = {19.25, 12.75};
        sip.forwardX.order = sipOrder;
        sip.forwardY.order = sipOrder;
        for (std::uint32_t i = 0u; i <= sipOrder; ++i)
            for (std::uint32_t j = 0u; j <= sipOrder - i; ++j)
                if (i + j >= 2u)
                {
                    sip.forwardX.coefficients[i][j] = 0.01 / std::pow(40.0, i + j);
                    sip.forwardY.coefficients[i][j] = -0.02 / std::pow(40.0, i + j);
                }
        mapping.sourceDistortion = an::SipDistortion::create(sip).value();
    }
    return mapping;
}

// Independent long-double algebra: no production mapping, power table or inverse is called.
an::PixelCoordinate geometryOracle(const an::DrizzleMapDescriptor& descriptor, an::PixelCoordinate point)
{
    long double x = static_cast<long double>(point.x) + 0.5L;
    long double y = static_cast<long double>(point.y) + 0.5L;
    const auto& mapping = descriptor.mapping();
    if (mapping.sourceDistortion.has_value())
    {
        const auto& sip = *mapping.sourceDistortion;
        const long double u = x - sip.referencePixel().x;
        const long double v = y - sip.referencePixel().y;
        const auto polynomial = [&](const an::SipPolynomial& component) {
            long double sum = 0.0L;
            for (std::uint32_t i = 0u; i <= component.order; ++i)
                for (std::uint32_t j = 0u; j <= component.order - i; ++j)
                    sum += static_cast<long double>(component.coefficients[i][j]) * std::pow(u, i) * std::pow(v, j);
            return sum;
        };
        x += polynomial(sip.forwardX());
        y += polynomial(sip.forwardY());
    }
    const auto& h = mapping.transform.matrix();
    const long double denominator = h[6] * x + h[7] * y + h[8];
    const long double rx = (h[0] * x + h[1] * y + h[2]) / denominator;
    const long double ry = (h[3] * x + h[4] * y + h[5]) / denominator;
    x = rx;
    y = ry;
    if (!mapping.transform.isLinear())
    {
        const auto& poly = mapping.transform.polynomial();
        const long double u = (x - poly.normalizationOrigin.x) / poly.normalizationScale.x;
        const long double v = (y - poly.normalizationOrigin.y) / poly.normalizationScale.y;
        x = y = 0.0L;
        std::size_t term = 0u;
        for (std::uint32_t degree = 0u; degree <= poly.order; ++degree)
            for (std::uint32_t j = 0u; j <= degree; ++j, ++term)
            {
                const long double basis = std::pow(u, degree - j) * std::pow(v, j);
                x += poly.x[term] * basis;
                y += poly.y[term] * basis;
            }
        x = x * poly.normalizationScale.x + poly.normalizationOrigin.x;
        y = y * poly.normalizationScale.y + poly.normalizationOrigin.y;
    }
    return {static_cast<double>((x + mapping.postTranslation.x) * descriptor.scale() - 0.5L),
            static_cast<double>((y + mapping.postTranslation.y) * descriptor.scale() - 0.5L)};
}

TEST(DrizzleGeometryTest, RowBoundsRecordEachStoredRowsFiniteDestinationSpan)
{
    // The accumulation partitions the destination into strips and asks, per strip and per source
    // row, whether the row can reach the strip. Answering that by scanning the row's coordinates
    // reads the whole vertical plane once per strip; a generated map records the answer per row
    // instead, and a view built from bare planes carries no bounds and is scanned as before.
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto descriptor = geometryDescriptor(17u, 13u, 1.5, {.postTranslation = {3.0, -2.0}});
    auto plan = an::DrizzleGeometryPlan::create(*execution, descriptor);
    ASSERT_TRUE(plan.hasValue()) << plan.status().message();
    for (const auto request : {an::DrizzlePixmapRequest{.storage = an::DrizzleMapStorage::Dense},
                               an::DrizzlePixmapRequest{.firstRow = 4u, .rowCount = 3u, .haloRows = 1u}})
    {
        auto map = an::generateDrizzlePixmap(*execution, *plan, request);
        ASSERT_TRUE(map.hasValue()) << map.status().message();
        auto view = map->view();
        ASSERT_TRUE(view.hasValue());
        const std::uint32_t stored = view->storedRowEnd() - view->storedRowBegin();
        EXPECT_TRUE(view->hasRowBounds());
        EXPECT_EQ(view->rowBounds().size(), 2u * stored);
        EXPECT_EQ(map->coordinateBytes(), 17u * stored * 8u + stored * 8u);
        for (std::uint32_t y = view->storedRowBegin(); y < view->storedRowEnd(); ++y)
        {
            const auto row = view->row(y);
            ASSERT_TRUE(row.hasValue());
            float low = std::numeric_limits<float>::infinity(), high = -low;
            for (const float value : row->y)
                if (std::isfinite(value))
                {
                    low = (std::min)(low, value);
                    high = (std::max)(high, value);
                }
            const auto bounds = view->rowBounds(y);
            ASSERT_TRUE(bounds.hasValue()) << bounds.status().message();
            EXPECT_EQ(bounds->low, low);
            EXPECT_EQ(bounds->high, high);
        }
        EXPECT_EQ(view->rowBounds(view->storedRowEnd()).status().code(), an::ErrorCode::OutOfRange);
        EXPECT_FALSE(view->rowBounds(view->storedRowBegin() > 0u ? view->storedRowBegin() - 1u : 13u).hasValue());
    }

    // A row with no finite destination has an empty span: the low bound above the high one.
    const auto extent = an::ImageExtent::create(3u, 2u, 1u, an::defaultEngineLimits());
    const auto output = an::ImageExtent::create(6u, 4u, 1u, an::defaultEngineLimits());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> x{0.0f, 1.0f, 2.0f, nan, nan, nan};
    const std::vector<float> y{0.5f, 1.5f, 3.0f, nan, nan, nan};
    const std::vector<float> bounds{0.5f, 3.0f, std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity()};
    auto bare = an::DrizzlePixmapView::create(*extent, *output, 0u, 2u, 0u, 2u, x, y);
    ASSERT_TRUE(bare.hasValue());
    EXPECT_FALSE(bare->hasRowBounds());
    EXPECT_TRUE(bare->rowBounds().empty());
    EXPECT_EQ(bare->rowBounds(0u).status().code(), an::ErrorCode::Unsupported);
    auto bounded = an::DrizzlePixmapView::create(*extent, *output, 0u, 2u, 0u, 2u, x, y, bounds);
    ASSERT_TRUE(bounded.hasValue()) << bounded.status().message();
    EXPECT_TRUE(bounded->hasRowBounds());
    EXPECT_EQ(bounded->rowBounds(0u)->low, 0.5f);
    EXPECT_EQ(bounded->rowBounds(0u)->high, 3.0f);
    EXPECT_GT(bounded->rowBounds(1u)->low, bounded->rowBounds(1u)->high);
    const std::vector<float> wrong{0.5f, 3.0f};
    EXPECT_EQ(an::DrizzlePixmapView::create(*extent, *output, 0u, 2u, 0u, 2u, x, y, wrong).status().code(),
              an::ErrorCode::IncompatibleArguments);
    const std::vector<float> inverted{3.0f, 0.5f, 0.0f, 0.0f};
    EXPECT_EQ(an::DrizzlePixmapView::create(*extent, *output, 0u, 2u, 0u, 2u, x, y, inverted).status().code(),
              an::ErrorCode::InvalidData);
}

TEST(DrizzleGeometryTest, PreparedAndReferenceMapsAgreeWithIndependentAlgebraAcrossOrdersAndWorkers)
{
    for (std::uint32_t sipOrder : {0u, 2u, 3u, 4u, 5u, 6u})
        for (std::uint32_t polynomialOrder : {0u, 2u, 3u, 4u})
        {
            const auto descriptor = geometryDescriptor(43u, 37u, 1.75, curvedMapping(polynomialOrder, sipOrder));
            std::vector<an::PixelCoordinate> baseline;
            for (std::uint32_t workers : {1u, 2u, 4u})
                for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
                {
                    auto engine = an::EngineContext::create({.maxConcurrency = workers});
                    auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
                    auto plan = an::DrizzleGeometryPlan::create(*execution, descriptor);
                    ASSERT_TRUE(plan.hasValue());
                    auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
                    ASSERT_TRUE(map.hasValue()) << map.status().message();
                    EXPECT_EQ(map->report().unmapped, 0u);
                    EXPECT_EQ(map->report().sipEvaluations, sipOrder == 0u ? 0u : 43u * 37u);
                    EXPECT_EQ(map->report().polynomialEvaluations, polynomialOrder == 0u ? 0u : 43u * 37u);
                    EXPECT_LE(map->report().workers, workers);
                    EXPECT_EQ(map->report().implementation, backend == an::ComputeBackend::Reference
                                                                ? an::DrizzleMapImplementation::Reference
                                                                : an::DrizzleMapImplementation::PreparedScalar);
                    auto view = map->view();
                    for (std::uint32_t y = 0u; y < 37u; ++y)
                        for (std::uint32_t x = 0u; x < 43u; ++x)
                        {
                            const auto point = view->at(x, y).value();
                            const auto expected =
                                geometryOracle(descriptor, {static_cast<double>(x), static_cast<double>(y)});
                            EXPECT_NEAR(point.x, expected.x, 8.0e-6);
                            EXPECT_NEAR(point.y, expected.y, 8.0e-6);
                            if (workers == 1u && backend == an::ComputeBackend::Reference)
                                baseline.push_back(point);
                            else
                                EXPECT_EQ(point, baseline[static_cast<std::size_t>(y) * 43u + x]);
                        }
                }
        }
}

TEST(DrizzleGeometryTest, ForwardInverseRoundTripsFractionalCornersWithSipNewtonAndPolynomial)
{
    for (std::uint32_t polynomialOrder : {0u, 2u, 3u, 4u})
    {
        const auto descriptor = geometryDescriptor(43u, 37u, 2.5, curvedMapping(polynomialOrder, 3u));
        for (double x : {-0.5, 0.0, 2.375, 21.75, 42.5})
            for (double y : {-0.5, 0.0, 8.25, 36.5})
            {
                auto mapped = an::mapDrizzlePoint(descriptor, {x, y});
                ASSERT_TRUE(mapped.hasValue());
                const auto oracle = geometryOracle(descriptor, {x, y});
                EXPECT_NEAR(mapped->x, oracle.x, 1.0e-10);
                EXPECT_NEAR(mapped->y, oracle.y, 1.0e-10);
                auto inverse = an::mapDrizzlePointInverse(descriptor, *mapped);
                ASSERT_TRUE(inverse.hasValue()) << inverse.status().message();
                EXPECT_NEAR(inverse->x, x, 0.001);
                EXPECT_NEAR(inverse->y, y, 0.001);
            }
    }
}

TEST(DrizzleGeometryTest, InaccurateExplicitSipInverseIsRefusedInsteadOfReturningWrongCoordinates)
{
    auto mapping = curvedMapping(0u, 2u);
    auto sip = mapping.sourceDistortion->toRequest();
    sip.inverseX.order = 2u;
    sip.inverseX.coefficients[0][0] = 12.0;
    mapping.sourceDistortion = an::SipDistortion::create(sip).value();
    const auto descriptor = geometryDescriptor(43u, 37u, 2.0, mapping);
    auto mapped = an::mapDrizzlePoint(descriptor, {3.0, 5.0});
    ASSERT_TRUE(mapped.hasValue());
    EXPECT_EQ(an::mapDrizzlePointInverse(descriptor, *mapped).status().code(), an::ErrorCode::InvalidData);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(an::mapDrizzlePoint(descriptor, {nan, 0.0}).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::mapDrizzlePointInverse(descriptor, {0.0, nan}).status().code(), an::ErrorCode::InvalidArgument);
}

TEST(DrizzleGeometryTest, PolesAndUnrepresentableCoordinatesBecomePairedNanWithoutClippingCanvas)
{
    an::RegistrationTransformRequest transform;
    transform.linearKind = an::RegistrationTransformKind::Projective;
    transform.sourceToReference = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, -2.5};
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const std::array<an::DrizzleMappingRequest, 3u> mappings{
        {{.transform = an::RegistrationTransform::create(transform).value()},
         {.postTranslation = {1.0e100, 0.0}},
         {.postTranslation = {-1000.0, 1000.0}}}};
    for (std::size_t i = 0u; i < mappings.size(); ++i)
    {
        auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(5u, 3u, 2.0, mappings[i]));
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue());
        auto view = map->view();
        EXPECT_EQ(map->report().unmapped, i == 0u ? 3u : (i == 1u ? 15u : 0u));
        for (std::uint32_t y = 0u; y < 3u; ++y)
            for (std::uint32_t x = 0u; x < 5u; ++x)
            {
                auto point = view->at(x, y);
                EXPECT_EQ(std::isnan(point->x), std::isnan(point->y));
                if (i == 2u)
                {
                    EXPECT_LT(point->x, 0.0);
                    EXPECT_GT(point->y, 6.0);
                }
            }
    }
}

TEST(DrizzleGeometryTest, NearlySingularProjectiveColumnsAreHolesInBothImplementations)
{
    // A projective denominator that comes within 1e-12 of zero without reaching it maps its column
    // to a finite position a Float32 holds, which the transform does not honestly define; the
    // checked composition refuses it, and the prepared row must leave the same hole. Column 8's
    // centre gives a denominator of exactly 2^-42; every other column is well inside the plane.
    constexpr double kSlope = 0x1.0p-10;
    an::RegistrationTransformRequest transform;
    transform.linearKind = an::RegistrationTransformKind::Projective;
    transform.sourceToReference = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, kSlope, 0.0, -kSlope * 8.5 + 0x1.0p-42};
    const auto descriptor =
        geometryDescriptor(17u, 13u, 1.5, {.transform = an::RegistrationTransform::create(transform).value()});
    std::vector<an::PixelCoordinate> reference;
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
        auto plan = an::DrizzleGeometryPlan::create(*execution, descriptor);
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue()) << map.status().message();
        EXPECT_EQ(map->report().unmapped, 13u);
        auto view = map->view();
        for (std::uint32_t y = 0u; y < 13u; ++y)
            for (std::uint32_t x = 0u; x < 17u; ++x)
            {
                const auto point = view->at(x, y);
                ASSERT_TRUE(point.hasValue());
                EXPECT_EQ(std::isnan(point->x), x == 8u) << x << ',' << y;
                EXPECT_EQ(std::isnan(point->y), x == 8u) << x << ',' << y;
                if (backend == an::ComputeBackend::Reference)
                    reference.push_back(*point);
                else if (x != 8u)
                    EXPECT_EQ(*point, reference[static_cast<std::size_t>(y) * 17u + x]);
            }
    }
}

TEST(DrizzleGeometryTest, PreparedLinearRowsMatchTheReferenceAcrossRunsTilesAndPoles)
{
    // A prepared linear row is mapped a run of columns at a time and records its row's bounds as it
    // writes them, and a source wider than one tile splits every row between tiles. A pole on either
    // side of a run's or a tile's edge, a last run of one column, and an affine transform beside the
    // projective ones must all leave the reference composition's exact bits, holes and bounds. The
    // denominator at column c is exactly 2^-12 (c - pole) + 2^-42, so only the pole is a hole.
    //
    // The order of every sum is part of that answer. In a cancelling case the row's products,
    // 2^40 per source row, cancel the constant terms, -2^39, on the first row: summed in the
    // composition's order the column's product is rounded to 2^39's precision first, and a sum in
    // any other order moves the Float32 result. That case is compared with the reference alone; the
    // independent algebra keeps precision the transform itself throws away.
    struct Case
    {
        std::uint32_t width;
        std::uint32_t pole;
        bool projective;
        bool cancelling;
    };
    constexpr std::uint32_t kHeight = 3u;
    constexpr double kSlope = 0x1.0p-12;
    for (const Case c : {Case{513u, 255u, true, false}, Case{513u, 256u, true, false}, Case{513u, 512u, true, false},
                         Case{4353u, 4095u, true, false}, Case{4353u, 4096u, true, false}, Case{513u, 0u, false, false},
                         Case{4353u, 0u, false, false}, Case{513u, 0u, false, true}, Case{513u, 256u, true, true}})
    {
        an::RegistrationTransformRequest transform;
        transform.linearKind =
            c.projective ? an::RegistrationTransformKind::Projective : an::RegistrationTransformKind::Affine;
        transform.sourceToReference = {1.001, -0.03, 2.5, 0.025, 0.999, -1.75, 0.0, 0.0, 1.0};
        if (c.projective)
        {
            transform.sourceToReference[6] = kSlope;
            transform.sourceToReference[8] = -kSlope * (static_cast<double>(c.pole) + 0.5) + 0x1.0p-42;
        }
        if (c.cancelling)
        {
            transform.sourceToReference[1] = transform.sourceToReference[4] = 0x1.0p40;
            transform.sourceToReference[2] = transform.sourceToReference[5] = -0x1.0p39;
        }
        const auto descriptor = geometryDescriptor(
            c.width, kHeight, 1.75,
            {.transform = an::RegistrationTransform::create(transform).value(), .postTranslation = {-0.25, 0.75}});
        std::vector<float> xs, ys, bounds;
        for (const auto [backend, workers] :
             {std::pair{an::ComputeBackend::Reference, 1u}, std::pair{an::ComputeBackend::Automatic, 1u},
              std::pair{an::ComputeBackend::Automatic, 4u}})
        {
            auto engine = an::EngineContext::create({.maxConcurrency = workers});
            auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
            auto plan = an::DrizzleGeometryPlan::create(*execution, descriptor);
            ASSERT_TRUE(plan.hasValue()) << plan.status().message();
            auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
            ASSERT_TRUE(map.hasValue()) << map.status().message();
            EXPECT_EQ(map->report().unmapped, c.projective ? kHeight : 0u) << c.width << ' ' << c.pole;
            const auto view = map->view().value();
            const bool baseline = backend == an::ComputeBackend::Reference;
            for (std::uint32_t y = 0u; y < kHeight; ++y)
            {
                const auto row = view.row(y).value();
                const auto rowBounds = view.rowBounds(y).value();
                if (baseline)
                {
                    bounds.push_back(rowBounds.low);
                    bounds.push_back(rowBounds.high);
                }
                else
                {
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(rowBounds.low), std::bit_cast<std::uint32_t>(bounds[2u * y]))
                        << c.width << ' ' << c.pole << " row " << y;
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(rowBounds.high),
                              std::bit_cast<std::uint32_t>(bounds[2u * y + 1u]))
                        << c.width << ' ' << c.pole << " row " << y;
                }
                for (std::uint32_t x = 0u; x < c.width; ++x)
                {
                    const bool hole = c.projective && x == c.pole;
                    EXPECT_EQ(std::isnan(row.x[x]), hole) << c.width << ' ' << c.pole << ' ' << x;
                    EXPECT_EQ(std::isnan(row.y[x]), hole) << c.width << ' ' << c.pole << ' ' << x;
                    if (baseline)
                    {
                        xs.push_back(row.x[x]);
                        ys.push_back(row.y[x]);
                        if (!hole && !c.cancelling)
                        {
                            const auto expected =
                                geometryOracle(descriptor, {static_cast<double>(x), static_cast<double>(y)});
                            EXPECT_NEAR(row.x[x], expected.x, 8.0e-6 + std::abs(expected.x) * 0x1.0p-23);
                            EXPECT_NEAR(row.y[x], expected.y, 8.0e-6 + std::abs(expected.y) * 0x1.0p-23);
                        }
                        continue;
                    }
                    const std::size_t index = static_cast<std::size_t>(y) * c.width + x;
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(row.x[x]), std::bit_cast<std::uint32_t>(xs[index]))
                        << c.width << ' ' << c.pole << ' ' << x << ',' << y << " workers " << workers;
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(row.y[x]), std::bit_cast<std::uint32_t>(ys[index]))
                        << c.width << ' ' << c.pole << ' ' << x << ',' << y << " workers " << workers;
                }
            }
        }
    }
}

TEST(DrizzleGeometryTest, InterpolationExtrapolatesAtEdgesAndRequiresStoredHalo)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(5u, 7u));
    auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
    const auto view = map->view().value();
    for (std::uint32_t y : {0u, 1u, 5u, 6u})
        for (std::uint32_t x : {0u, 1u, 3u, 4u})
        {
            const auto corners = an::interpolateDrizzleCorners(view, x, y, 0.5);
            ASSERT_TRUE(corners.hasValue());
            EXPECT_FLOAT_EQ((*corners)[0].x, static_cast<float>(x * 2.0 - 0.5));
            EXPECT_FLOAT_EQ((*corners)[0].y, static_cast<float>(y * 2.0 + 1.5));
            EXPECT_FLOAT_EQ((*corners)[1].x, static_cast<float>(x * 2.0 + 1.5));
            EXPECT_FLOAT_EQ((*corners)[1].y, static_cast<float>(y * 2.0 + 1.5));
            EXPECT_FLOAT_EQ((*corners)[2].x, static_cast<float>(x * 2.0 + 1.5));
            EXPECT_FLOAT_EQ((*corners)[2].y, static_cast<float>(y * 2.0 - 0.5));
            EXPECT_FLOAT_EQ((*corners)[3].x, static_cast<float>(x * 2.0 - 0.5));
            EXPECT_FLOAT_EQ((*corners)[3].y, static_cast<float>(y * 2.0 - 0.5));
        }
    auto bare = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 3u, .rowCount = 1u});
    EXPECT_EQ(an::interpolateDrizzleCorners(*bare->view(), 2u, 3u, 0.5).status().code(), an::ErrorCode::OutOfRange);
    auto halo = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 3u, .rowCount = 1u, .haloRows = 1u});
    EXPECT_EQ(an::interpolateDrizzleCorners(*halo->view(), 2u, 3u, 0.5).value(),
              an::interpolateDrizzleCorners(view, 2u, 3u, 0.5).value());
    for (double bad : {0.0, -0.5, 1.01, std::numeric_limits<double>::infinity()})
        EXPECT_EQ(an::interpolateDrizzleCorners(view, 2u, 3u, bad).status().code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::interpolateDrizzlePoint(view, {-1.001, 0.0}).status().code(), an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::interpolateDrizzlePoint(view, {0.0, 7.001}).status().code(), an::ErrorCode::OutOfRange);
}

TEST(DrizzleGeometryTest, DegenerateAndMalformedViewsNeverReadMissingNeighbours)
{
    const auto source = *an::ImageExtent::create(2u, 2u, 1u, an::defaultEngineLimits());
    std::array<float, 8u> data{0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    auto x = std::span<const float>(data).first(4u);
    auto y = std::span<const float>(data).last(4u);
    EXPECT_EQ(an::DrizzlePixmapView::create(source, source, 0u, 2u, 0u, 2u, x, x).status().code(),
              an::ErrorCode::Aliasing);
    EXPECT_EQ(an::DrizzlePixmapView::create(source, source, 0u, 2u, 0u, 2u, x.first(3u), y).status().code(),
              an::ErrorCode::IncompatibleArguments);
    EXPECT_EQ(an::DrizzlePixmapView::create(source, source, 1u, 2u, 0u, 2u, x, y).status().code(),
              an::ErrorCode::OutOfRange);
    const auto view = an::DrizzlePixmapView::create(source, source, 0u, 2u, 0u, 2u, x, y).value();
    EXPECT_TRUE(an::interpolateDrizzlePoint(view, {0.5, 0.5}).hasValue());
    data[3] = std::numeric_limits<float>::quiet_NaN();
    // Even a zero-weight invalid tap poisons interpolation.
    EXPECT_EQ(an::interpolateDrizzlePoint(view, {0.0, 0.0}).status().code(), an::ErrorCode::InvalidData);
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    for (auto size : {std::array{1u, 1u}, std::array{1u, 7u}, std::array{7u, 1u}})
    {
        auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(size[0], size[1]));
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue());
        EXPECT_EQ(map->report().mapped, size[0] * size[1]);
        EXPECT_EQ(an::interpolateDrizzlePoint(*map->view(), {0.0, 0.0}).status().code(),
                  an::ErrorCode::InvalidArgument);
    }
}

TEST(DrizzleGeometryTest, CacheIdentityIncludesEveryGeometryFactAndEvictionKeepsBorrowedPlansAlive)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    const auto mapping = curvedMapping(2u, 2u);
    const auto first = geometryDescriptor(43u, 37u, 2.0, mapping);
    auto cache = an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 2u});
    ASSERT_TRUE(cache.hasValue());
    auto held = cache->get(*execution, first);
    auto again = cache->get(*execution, first);
    ASSERT_TRUE(held.hasValue());
    ASSERT_TRUE(again.hasValue());
    EXPECT_EQ(held->descriptor(), again->descriptor());
    EXPECT_EQ(cache->counters().hits, 1u);
    std::vector<an::DrizzleMapDescriptor> variants;
    variants.push_back(geometryDescriptor(44u, 37u, 2.0, mapping));
    variants.push_back(geometryDescriptor(43u, 38u, 2.0, mapping));
    variants.push_back(geometryDescriptor(43u, 37u, 2.5, mapping));
    auto changed = mapping;
    changed.postTranslation.x += 0.25;
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    for (std::uint32_t field = 0u; field < 5u; ++field)
    {
        changed = mapping;
        auto transform = changed.transform.toRequest();
        if (field == 0u)
            transform.sourceToReference[2] += 0.25;
        else if (field == 1u)
            transform.polynomial.x[3] += 0.001;
        else if (field == 2u)
            transform.polynomial.y[4] += 0.001;
        else if (field == 3u)
            transform.polynomial.normalizationOrigin.y += 0.25;
        else
            transform.polynomial.normalizationScale.x += 1.0;
        changed.transform = an::RegistrationTransform::create(transform).value();
        variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    }
    changed = mapping;
    auto sip = changed.sourceDistortion->toRequest();
    sip.referencePixel.x += 1.0;
    changed.sourceDistortion = an::SipDistortion::create(sip).value();
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    sip = mapping.sourceDistortion->toRequest();
    sip.forwardX.coefficients[2][0] *= 2.0;
    changed.sourceDistortion = an::SipDistortion::create(sip).value();
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    sip = mapping.sourceDistortion->toRequest();
    sip.inverseX.order = 2u;
    changed.sourceDistortion = an::SipDistortion::create(sip).value();
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    sip.inverseY.order = 2u;
    sip.inverseY.coefficients[1][1] = 0.0001;
    changed.sourceDistortion = an::SipDistortion::create(sip).value();
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    changed.sourceDistortion.reset();
    variants.push_back(geometryDescriptor(43u, 37u, 2.0, changed));
    for (const auto& variant : variants)
    {
        EXPECT_FALSE(an::sameDrizzleGeometry(first, variant));
        EXPECT_TRUE(cache->get(*execution, variant).hasValue());
        EXPECT_LE(cache->counters().entries, 2u);
        EXPECT_LE(cache->counters().retainedBytes, 1024u * 1024u);
    }
    EXPECT_EQ(cache->counters().evictions, variants.size() - 1u);
    EXPECT_TRUE(an::sameDrizzleGeometry(first, *held->descriptor()));
    EXPECT_TRUE(cache->clear().ok());
    EXPECT_EQ(cache->counters().entries, 0u);
    EXPECT_TRUE(an::generateDrizzlePixmap(*execution, *held, {.rowCount = 1u}).hasValue());
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, cache->counters().retainedBytes + held->residentBytes());
}

TEST(DrizzleGeometryTest, CacheSerializesConcurrentHitsAndUsesLeastRecentlyUsedEntry)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto cache = an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 2u});
    const auto a = geometryDescriptor();
    const auto b = geometryDescriptor(18u);
    const auto c = geometryDescriptor(19u);
    EXPECT_TRUE(cache->get(*execution, a).hasValue());
    EXPECT_TRUE(cache->get(*execution, b).hasValue());
    EXPECT_TRUE(cache->get(*execution, a).hasValue());
    EXPECT_TRUE(cache->get(*execution, c).hasValue());
    EXPECT_TRUE(cache->get(*execution, a).hasValue());
    EXPECT_EQ(cache->counters().misses, 3u);
    std::vector<std::future<bool>> readers;
    for (std::uint32_t i = 0u; i < 8u; ++i)
        readers.push_back(std::async(std::launch::async, [&] {
            for (std::uint32_t j = 0u; j < 32u; ++j)
                if (!cache->get(*execution, a).hasValue())
                    return false;
            return true;
        }));
    for (auto& reader : readers)
        EXPECT_TRUE(reader.get());
    EXPECT_EQ(cache->counters().hits, 258u);
    EXPECT_EQ(cache->counters().misses, 3u);
    EXPECT_TRUE(cache->get(*execution, b).hasValue());
    EXPECT_EQ(cache->counters().misses, 4u);
}

class StopAfterMapTile final : public an::TraceSink
{
public:
    std::stop_source stop;
    std::uint32_t calls = 0u;
    std::string_view counter = "coordinateTilePairs";
    void onTrace(const an::TraceEvent& event) noexcept override
    {
        if (std::string_view(event.name) == counter)
        {
            ++calls;
            stop.request_stop();
        }
    }
};

TEST(DrizzleGeometryTest, CancellationDuringGenerationReturnsAllUnpublishedBytes)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 2u});
    StopAfterMapTile sink;
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = sink.stop.get_token(), .traceSink = &sink});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(101u, 103u, 2.0, curvedMapping(2u, 3u)));
    ASSERT_TRUE(plan.hasValue());
    auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
    EXPECT_EQ(map.status().code(), an::ErrorCode::Cancelled);
    EXPECT_GT(sink.calls, 0u);
    EXPECT_EQ(engine->memoryResource()->counters().liveBytes, plan->residentBytes());
    const auto allocations = engine->memoryResource()->counters().totalAllocations;
    EXPECT_EQ(an::generateDrizzlePixmap(*execution, *plan).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, allocations);
}

TEST(DrizzleGeometryTest, MapBudgetBoundariesReleasePartialAllocationsAndRespectNarrowerLimits)
{
    auto owner = an::EngineContext::create({.maxConcurrency = 1u});
    auto preparing = an::ExecutionContext::create(*owner, {});
    const auto descriptor = geometryDescriptor(17u, 13u);
    auto plan = an::DrizzleGeometryPlan::create(*preparing, descriptor);
    const std::uint64_t payload = 17u * 3u * 8u + 3u * 8u;
    const auto exactBytes = payload + an::DrizzlePixmap::controlBytes();
    for (auto budget : {payload - 1u, payload, exactBytes - 1u, exactBytes})
    {
        auto limits = an::defaultEngineLimits();
        limits.memoryBudgetBytes = budget;
        auto engine = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
        auto execution = an::ExecutionContext::create(*engine, {});
        {
            auto map = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 3u, .rowCount = 1u, .haloRows = 1u});
            if (budget == exactBytes)
            {
                ASSERT_TRUE(map.hasValue()) << map.status().message();
                EXPECT_EQ(map->residentBytes(), exactBytes);
                EXPECT_EQ(engine->memoryResource()->counters().liveBytes, exactBytes);
            }
            else
                EXPECT_EQ(map.status().code(), an::ErrorCode::MemoryBudgetExceeded);
        }
        EXPECT_EQ(engine->memoryResource()->counters().liveBytes, 0u);
    }
    auto limits = an::defaultEngineLimits();
    limits.maxImageWidth = 16u;
    auto restricted = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto restrictedExecution = an::ExecutionContext::create(*restricted, {});
    EXPECT_EQ(an::generateDrizzlePixmap(*restrictedExecution, *plan).status().code(), an::ErrorCode::LimitExceeded);
    EXPECT_EQ(restricted->memoryResource()->counters().totalAllocations, 0u);
}

TEST(DrizzleGeometryTest, CacheRefusalsAndLeasesPreserveExistingEntries)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    EXPECT_EQ(an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 0u}).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::DrizzleGeometryCache::create(*execution, {.maximumBytes = 1u}).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    auto cache = an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 1u});
    auto held = cache->get(*execution, geometryDescriptor());
    const auto footprint = cache->counters().retainedBytes;
    auto limits = an::defaultEngineLimits();
    limits.memoryBudgetBytes = footprint;
    auto bounded = an::EngineContext::create({.maxConcurrency = 1u, .limits = limits});
    auto boundedExecution = an::ExecutionContext::create(*bounded, {});
    auto boundedCache = an::DrizzleGeometryCache::create(*boundedExecution, {.maximumEntries = 1u});
    ASSERT_TRUE(boundedCache.hasValue());
    EXPECT_TRUE(boundedCache->get(*boundedExecution, geometryDescriptor()).hasValue());
    EXPECT_EQ(boundedCache->get(*boundedExecution, geometryDescriptor(18u)).status().code(),
              an::ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(boundedCache->counters().evictions, 0u);
    EXPECT_TRUE(boundedCache->get(*boundedExecution, geometryDescriptor()).hasValue());
    EXPECT_EQ(cache->get(*boundedExecution, geometryDescriptor()).status().code(),
              an::ErrorCode::IncompatibleArguments);
    *execution = an::ExecutionContext{};
    EXPECT_FALSE(engine->shutdown().ok());
    *cache = an::DrizzleGeometryCache{};
    EXPECT_FALSE(engine->shutdown().ok());
    *held = an::DrizzleGeometryPlan{};
    EXPECT_TRUE(engine->shutdown().ok());
}

TEST(DrizzleGeometryTest, InvalidRequestsAndExpiredDeadlineAllocateNoMap)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor());
    for (const an::DrizzlePixmapRequest request : {an::DrizzlePixmapRequest{.rowCount = 0u},
                                                   {.rowCount = 257u},
                                                   {.haloRows = 2u},
                                                   {.storage = static_cast<an::DrizzleMapStorage>(255u)},
                                                   {.storage = an::DrizzleMapStorage::Dense, .firstRow = 1u}})
        EXPECT_EQ(an::generateDrizzlePixmap(*execution, *plan, request).status().code(),
                  an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 13u}).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, 1u);
    *execution = an::ExecutionContext{};
    auto expired =
        an::ExecutionContext::create(*engine, {.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1)});
    ASSERT_TRUE(expired.hasValue());
    EXPECT_EQ(an::generateDrizzlePixmap(*expired, *plan).status().code(), an::ErrorCode::DeadlineExceeded);
    an::DrizzlePixmap empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_EQ(empty.view().status().code(), an::ErrorCode::NotInitialized);
}

TEST(DrizzleGeometryTest, CurvedBlockCornersMatchDenseMapsAcrossEveryBoundary)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 4u});
    auto execution = an::ExecutionContext::create(*engine, {});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(19u, 17u, 1.75, curvedMapping(4u, 6u)));
    auto dense = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
    ASSERT_TRUE(dense.hasValue());
    const auto full = dense->view().value();
    for (std::uint32_t first = 0u; first < 17u; first += 3u)
    {
        auto block = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = first, .rowCount = 3u, .haloRows = 1u});
        ASSERT_TRUE(block.hasValue());
        const auto view = block->view().value();
        for (std::uint32_t y = view.coreRowBegin(); y < view.coreRowEnd(); ++y)
            for (std::uint32_t x = 0u; x < 19u; ++x)
                for (double halfSize : {0.01, 0.5, 1.0})
                {
                    const auto corners = an::interpolateDrizzleCorners(view, x, y, halfSize);
                    ASSERT_TRUE(corners.hasValue());
                    EXPECT_EQ(*corners, an::interpolateDrizzleCorners(full, x, y, halfSize).value());
                }
    }
}

TEST(DrizzleGeometryTest, ByteCeilingNarrowsCacheAndCancelledHitsDoNotChangeRecency)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    std::stop_source stop;
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = stop.get_token()});
    auto one = an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 1u});
    const auto descriptor = geometryDescriptor();
    auto held = one->get(*execution, descriptor);
    ASSERT_TRUE(held.hasValue());
    const auto ceiling = one->counters().retainedBytes;
    auto narrowed = an::DrizzleGeometryCache::create(*execution, {.maximumEntries = 256u, .maximumBytes = ceiling});
    ASSERT_TRUE(narrowed.hasValue());
    EXPECT_EQ(narrowed->counters().capacity, 1u);
    EXPECT_TRUE(narrowed->get(*execution, descriptor).hasValue());
    EXPECT_EQ(narrowed->counters().retainedBytes, ceiling);
    stop.request_stop();
    EXPECT_EQ(narrowed->get(*execution, descriptor).status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(narrowed->counters().hits, 0u);
    EXPECT_EQ(narrowed->counters().misses, 1u);
    an::DrizzleGeometryCache moved = std::move(*narrowed);
    EXPECT_EQ(narrowed->counters().capacity, 0u);
    EXPECT_EQ(moved.counters().entries, 1u);
    *held = an::DrizzleGeometryPlan{};
    *one = an::DrizzleGeometryCache{};
    *execution = an::ExecutionContext{};
    EXPECT_FALSE(engine->shutdown().ok());
    moved = an::DrizzleGeometryCache{};
    EXPECT_TRUE(engine->shutdown().ok());
}

TEST(DrizzleGeometryTest, NonlinearOverflowProducesInvalidPairsInBothImplementations)
{
    auto mapping = curvedMapping(2u, 0u);
    auto transform = mapping.transform.toRequest();
    transform.polynomial.normalizationScale = {1.0e-100, 1.0e-100};
    transform.polynomial.x[3] = 1.0e300;
    mapping.transform = an::RegistrationTransform::create(transform).value();
    for (auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 1u});
        auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
        auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(3u, 3u, 2.0, mapping));
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue());
        EXPECT_EQ(map->report().unmapped, 9u);
        const auto point = map->view()->at(1u, 1u).value();
        EXPECT_TRUE(std::isnan(point.x));
        EXPECT_TRUE(std::isnan(point.y));
    }
}

TEST(DrizzleGeometryTest, SourceSipFailuresAreCountedAndHoledAlikeInBothImplementations)
{
    // A source distortion that overflows away from its reference column is refused pixel by pixel
    // before the curved registration runs. Both implementations count every SIP evaluation, count a
    // polynomial evaluation only where the SIP succeeded - the three central columns of five - and
    // leave the same holes.
    auto mapping = curvedMapping(2u, 0u);
    an::SipDistortionRequest sip;
    sip.referencePixel = {2.5, 2.5};
    sip.forwardX.order = 2u;
    sip.forwardY.order = 2u;
    sip.forwardX.coefficients[2][0] = 1.0e308;
    mapping.sourceDistortion = an::SipDistortion::create(sip).value();
    std::vector<an::PixelCoordinate> reference;
    for (const auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 2u});
        auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
        auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(5u, 5u, 1.5, mapping));
        ASSERT_TRUE(plan.hasValue()) << plan.status().message();
        auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
        ASSERT_TRUE(map.hasValue()) << map.status().message();
        EXPECT_EQ(map->report().sipEvaluations, 25u);
        EXPECT_EQ(map->report().polynomialEvaluations, 15u);
        EXPECT_GE(map->report().unmapped, 10u);
        auto view = map->view();
        for (std::uint32_t y = 0u; y < 5u; ++y)
            for (std::uint32_t x = 0u; x < 5u; ++x)
            {
                const auto point = view->at(x, y).value();
                if (x == 0u || x == 4u)
                    EXPECT_TRUE(std::isnan(point.x) && std::isnan(point.y)) << x << ',' << y;
                if (backend == an::ComputeBackend::Reference)
                    reference.push_back(point);
                else
                {
                    const auto& expected = reference[static_cast<std::size_t>(y) * 5u + x];
                    EXPECT_EQ(std::isnan(point.x), std::isnan(expected.x)) << x << ',' << y;
                    if (!std::isnan(expected.x))
                        EXPECT_EQ(point, expected) << x << ',' << y;
                }
            }
    }
}

TEST(DrizzleGeometryTest, UnavailableIsaIsRefusedOrReportsActualScalarFallback)
{
    for (auto fallback : {an::BackendFallback::Forbidden, an::BackendFallback::Allowed})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 1u});
        auto execution = an::ExecutionContext::create(*engine, {.backend = an::ComputeBackend::Cpu,
                                                                .instructionSet = an::InstructionSet::Avx2,
                                                                .backendFallback = fallback});
        ASSERT_TRUE(execution.hasValue());
        auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor());
        auto map = an::generateDrizzlePixmap(*execution, *plan);
        if (fallback == an::BackendFallback::Forbidden)
            EXPECT_EQ(map.status().code(), an::ErrorCode::UnsupportedBackend);
        else
        {
            ASSERT_TRUE(map.hasValue());
            EXPECT_EQ(map->report().implementation, an::DrizzleMapImplementation::PreparedScalar);
            an::DrizzlePixmap moved = std::move(*map);
            EXPECT_FALSE(map->valid());
            EXPECT_TRUE(moved.valid());
        }
    }
}

TEST(DrizzleGeometryTest, InteriorSharedFootprintMatchesPointOracleAndRejectsEveryInvalidTap)
{
    const auto extent = *an::ImageExtent::create(5u, 5u, 1u, an::defaultEngineLimits());
    std::array<float, 25u> xs{}, ys{};
    for (std::size_t i = 0u; i < xs.size(); ++i)
    {
        xs[i] = static_cast<float>(i * i) * 0.031f;
        ys[i] = static_cast<float>(i) * -0.27f;
    }
    const auto view = an::DrizzlePixmapView::create(extent, extent, 0u, 5u, 0u, 5u, xs, ys).value();
    for (double h : {0.01, 0.37, 0.5, 0.91})
    {
        const auto corners = an::interpolateDrizzleCorners(view, 2u, 2u, h);
        ASSERT_TRUE(corners.hasValue());
        for (std::size_t i = 0u; i < 4u; ++i)
        {
            const auto expected =
                an::interpolateDrizzlePoint(view, {2.0 + (i == 0u || i == 3u ? -h : h), 2.0 + (i < 2u ? h : -h)})
                    .value();
            EXPECT_NEAR((*corners)[i].x, expected.x, 3.0e-6);
            EXPECT_NEAR((*corners)[i].y, expected.y, 3.0e-6);
        }
    }
    for (auto* plane : {&xs, &ys})
        for (std::size_t y = 1u; y <= 3u; ++y)
            for (std::size_t x = 1u; x <= 3u; ++x)
            {
                const auto index = y * 5u + x;
                const float saved = (*plane)[index];
                for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
                {
                    (*plane)[index] = invalid;
                    for (double h : {0.01, 0.5, 1.0})
                        EXPECT_EQ(an::interpolateDrizzleCorners(view, 2u, 2u, h).status().code(),
                                  an::ErrorCode::InvalidData);
                }
                (*plane)[index] = saved;
            }
}

TEST(DrizzleGeometryTest, FootprintRowsMatchIndividualQueriesAndCountInvalidGeometry)
{
    const auto extent = *an::ImageExtent::create(5u, 5u, 1u, an::defaultEngineLimits());
    std::array<float, 25u> xs{}, ys{};
    for (std::size_t i = 0u; i < xs.size(); ++i)
    {
        xs[i] = static_cast<float>(i % 5u);
        ys[i] = static_cast<float>(i / 5u);
    }
    xs[12] = std::numeric_limits<float>::quiet_NaN();
    const auto view = an::DrizzlePixmapView::create(extent, extent, 0u, 5u, 0u, 5u, xs, ys).value();
    for (auto backend : {an::ComputeBackend::Reference, an::ComputeBackend::Automatic})
    {
        auto engine = an::EngineContext::create({.maxConcurrency = 1u});
        auto execution = an::ExecutionContext::create(*engine, {.backend = backend});
        std::uint64_t invalid = 0u;
        for (std::uint32_t y = 0u; y < 5u; ++y)
        {
            std::array<an::DrizzleFootprint, 5u> output;
            auto report = an::interpolateDrizzleCornerRow(*execution, view, y, 0u, 0.5, output);
            ASSERT_TRUE(report.hasValue());
            EXPECT_EQ(report->mapped + report->unmapped, 5u);
            invalid += report->unmapped;
            for (std::uint32_t x = 0u; x < 5u; ++x)
            {
                const auto expected = an::interpolateDrizzleCorners(view, x, y, 0.5);
                if (expected.hasValue())
                    EXPECT_EQ(output[x], *expected);
                else
                    for (const auto& point : output[x])
                    {
                        EXPECT_TRUE(std::isnan(point.x));
                        EXPECT_TRUE(std::isnan(point.y));
                    }
            }
        }
        EXPECT_EQ(invalid, 9u);
        EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, 0u);
    }
}

TEST(DrizzleGeometryTest, FootprintRowValidationAndCancellationPrecedeWrites)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    std::stop_source stop;
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = stop.get_token()});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(5u, 7u));
    auto map = an::generateDrizzlePixmap(*execution, *plan, {.firstRow = 3u, .rowCount = 1u});
    std::array<an::DrizzleFootprint, 5u> output{};
    output[0][0].x = 42.0f;
    auto view = map->view().value();
    EXPECT_EQ(an::interpolateDrizzleCornerRow(*execution, view, 3u, 0u, 0.5, output).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::interpolateDrizzleCornerRow(*execution, view, 3u, 1u, 0.5, output).status().code(),
              an::ErrorCode::OutOfRange);
    EXPECT_EQ(an::interpolateDrizzleCornerRow(*execution, view, 3u, 0u, 0.0, output).status().code(),
              an::ErrorCode::InvalidArgument);
    EXPECT_TRUE(an::interpolateDrizzleCornerRow(*execution, view, 3u, 5u, 0.5, {}).hasValue());
    stop.request_stop();
    EXPECT_EQ(an::interpolateDrizzleCornerRow(*execution, view, 3u, 0u, 0.5, output).status().code(),
              an::ErrorCode::Cancelled);
    EXPECT_EQ(output[0][0].x, 42.0f);
}

TEST(DrizzleGeometryTest, FootprintRowCancellationLeavesOnlyCompletedTilePrefix)
{
    auto engine = an::EngineContext::create({.maxConcurrency = 1u});
    StopAfterMapTile sink;
    sink.counter = "footprintTileSamples";
    auto execution = an::ExecutionContext::create(*engine, {.stopToken = sink.stop.get_token(), .traceSink = &sink});
    auto plan = an::DrizzleGeometryPlan::create(*execution, geometryDescriptor(8193u, 3u));
    auto map = an::generateDrizzlePixmap(*execution, *plan, {.storage = an::DrizzleMapStorage::Dense});
    ASSERT_TRUE(map.hasValue());
    std::vector<an::DrizzleFootprint> output(8193u);
    output[4096][0].x = 123.0f;
    const auto before = engine->memoryResource()->counters().totalAllocations;
    auto row = an::interpolateDrizzleCornerRow(*execution, map->view().value(), 1u, 0u, 0.5, output);
    EXPECT_EQ(row.status().code(), an::ErrorCode::Cancelled);
    EXPECT_EQ(sink.calls, 1u);
    EXPECT_EQ(output[4095][0].x, 8190.0f - 0.5f);
    EXPECT_EQ(output[4096][0].x, 123.0f);
    EXPECT_EQ(engine->memoryResource()->counters().totalAllocations, before);
}

} // namespace

TEST(DrizzleGeometryTest, UndersizedMapIsAnInvalidArgumentForEveryInterpolation)
{
    const auto extent = *an::ImageExtent::create(1u, 1u, 1u, an::defaultEngineLimits());
    const std::array<float, 1u> xs{0.0f}, ys{0.0f};
    const auto map = an::DrizzlePixmapView::create(extent, extent, 0u, 1u, 0u, 1u, xs, ys);
    ASSERT_TRUE(map.hasValue());
    EXPECT_EQ(an::interpolateDrizzlePoint(*map, {0.0, 0.0}).code(), an::ErrorCode::InvalidArgument);
    EXPECT_EQ(an::interpolateDrizzleCorners(*map, 0u, 0u, 0.5).code(), an::ErrorCode::InvalidArgument);
}
