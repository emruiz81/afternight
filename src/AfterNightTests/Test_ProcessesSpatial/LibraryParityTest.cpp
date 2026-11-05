// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// Two dialogs held to an imaging library's own answers: the Unsharp Mask dialog to the library's unsharp mask, and the
/// Total Variation dialog's Chambolle method to the library's Chambolle iteration, at the settings each dialog reaches.
/// The library's answers are the ones the engine's library parity suite records and compiles.

#include "Test_ProcessesSpatial/SpatialProcessesTestSupport.h"

#include "Test_EngineLibraryParity/SkimageGoldens.h"

#include "processes/TotalVariationDenoiseProcess.h"
#include "processes/UnsharpMaskProcess.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::spatial;
namespace goldens = parity_test::skimage_goldens;

using Golden = std::array<float, 432u>;

/// What a dialog's Apply shows over the library's recorded plane, set by `configure`, against the library's answer.
template <typename ProcessT>
void applyAgainstGolden(const std::function<void(QWidget&)>& configure, const Golden& golden, double tolerance)
{
    std::unique_ptr<ProcessTestContext> context = openProcessContext();
    ASSERT_NE(context, nullptr);
    const InstalledContext installed(*context);
    std::optional<an::ui::ImageDocument> document =
        greyDocument(context->engineSession(), goldens::kPlaneWidth, goldens::kPlaneHeight,
                     [](std::uint32_t x, std::uint32_t y) { return goldens::kPlane[y * goldens::kPlaneWidth + x]; });
    ASSERT_TRUE(document.has_value());
    const TargetView target(*context, "plane", std::move(*document));
    SpatialHandle<ProcessT> process = openSpatial<ProcessT>(target.view());
    setChecked(*process, "preview", false);
    configure(*process);
    process->applyProcess();
    ASSERT_TRUE(settleRun(*process, target.view()));
    ASSERT_EQ(process->runTelemetry().runsCommitted, 1u)
        << context->errorTexts.join(QStringLiteral("; ")).toStdString();
    const std::optional<fixtures::FixtureSamples> shown = documentSamples(target.view().document());
    ASSERT_TRUE(shown.has_value());
    double largest = 0.0;
    for (std::uint32_t y = 0u; y < goldens::kPlaneHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < goldens::kPlaneWidth; ++x)
        {
            largest = (std::max)(largest, std::abs(shown->value(x, y, 0u) -
                                                   static_cast<double>(golden[y * goldens::kPlaneWidth + x])));
        }
    }
    EXPECT_LE(largest, tolerance);
}

/// The Unsharp Mask dialog at a radius and an amount.
[[nodiscard]] std::function<void(QWidget&)> unsharp(double radius, double amount)
{
    return [radius, amount](QWidget& window) {
        setReal(window, "radiusValue", radius);
        setReal(window, "amountValue", amount);
    };
}

/// The Total Variation dialog's Chambolle method at a weight, running exactly a number of iterations.
[[nodiscard]] std::function<void(QWidget&)> chambolle(double weight, int iterations)
{
    return [weight, iterations](QWidget& window) {
        choose(window, "algorithmSelector", QStringLiteral("Chambolle"));
        setReal(window, "weightValue", weight);
        setReal(window, "epsValue", 0.0);
        setWhole(window, "maxIterationsValue", iterations);
    };
}

} // namespace

// The radius is the library's sigma and the amount its amount; what is left between the two is the order two separable
// passes accumulate in at single precision.
TEST(LibraryParityTest, TheUnsharpMaskIsTheLibrarysUnsharpMask)
{
    constexpr double kPassRounding = 4.0e-6;
    applyAgainstGolden<UnsharpMaskProcess>(unsharp(1.0, 1.0), goldens::kUnsharpR1A1, kPassRounding);
    applyAgainstGolden<UnsharpMaskProcess>(unsharp(1.0, 2.5), goldens::kUnsharpR1A25, kPassRounding);
    applyAgainstGolden<UnsharpMaskProcess>(unsharp(2.0, 1.0), goldens::kUnsharpR2A1, kPassRounding);
    applyAgainstGolden<UnsharpMaskProcess>(unsharp(2.0, 0.5), goldens::kUnsharpR2A05, kPassRounding);
}

// The dialog's weight is a tenth of the library's; a zero tolerance runs every iteration, and a fixed-point iteration
// carries one single-precision step of rounding per iteration.
TEST(LibraryParityTest, TheChambolleMethodIsTheLibrarysIteration)
{
    applyAgainstGolden<TotalVariationDenoiseProcess>(chambolle(0.5, 50), goldens::kChambolleW005I50, 6.0e-6);
    applyAgainstGolden<TotalVariationDenoiseProcess>(chambolle(0.5, 200), goldens::kChambolleW005I200, 2.4e-5);
}
