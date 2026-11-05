// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "RestorationOracle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace an::bench
{
namespace
{

/// How many sigma-clipping passes the noise estimate makes over a row of first differences.
constexpr std::uint32_t kClipPasses = 3u;

/// How many standard deviations a first difference may stand from the mean and be kept.
constexpr double kClipSigma = 5.0;

/// How many samples apart the digest lattice is.
constexpr std::uint32_t kDigestStride = 37u;

/// One coordinate reflected back inside an extent without repeating the edge.
///
/// @param position where the read wants to be.
/// @param extent how many samples there are.
/// @return the sample index inside the extent.
[[nodiscard]] std::int64_t reflect(std::int64_t position, std::int64_t extent)
{
    const std::int64_t last = extent - 1;
    while (position < 0 || position > last)
    {
        position = position < 0 ? -position : 2 * last - position;
    }
    return position;
}

/// The sigma-clipped standard deviation of one row's first differences.
///
/// @param row the samples.
/// @param measured where the standard deviation goes.
/// @return true when the row produced a measurement.
[[nodiscard]] bool clippedDeviation(std::span<const float> row, double& measured)
{
    if (row.size() < 3u)
    {
        return false;
    }
    std::vector<double> differences;
    differences.reserve(row.size() - 1u);
    for (std::size_t i = 1u; i < row.size(); ++i)
    {
        differences.push_back(static_cast<double>(row[i]) - static_cast<double>(row[i - 1u]));
    }
    double deviation = 0.0;
    for (std::uint32_t pass = 0u; pass <= kClipPasses; ++pass)
    {
        if (differences.empty())
        {
            return false;
        }
        double mean = 0.0;
        for (const double value : differences)
        {
            mean += value;
        }
        mean /= static_cast<double>(differences.size());
        double variance = 0.0;
        for (const double value : differences)
        {
            const double centred = value - mean;
            variance += centred * centred;
        }
        deviation = std::sqrt(variance / static_cast<double>(differences.size()));
        if (pass == kClipPasses || deviation < 1.0e-10)
        {
            break;
        }
        std::vector<double> kept;
        kept.reserve(differences.size());
        for (const double value : differences)
        {
            if (std::abs(value - mean) <= kClipSigma * deviation)
            {
                kept.push_back(value);
            }
        }
        differences.swap(kept);
    }
    measured = deviation;
    return true;
}

} // namespace

std::vector<float> restorationBlurKernel(double fwhm, std::uint32_t& side)
{
    const double sigma = fwhm / kRestorationFwhmPerSigma;
    const auto radius = static_cast<std::uint32_t>((std::max)(2.0, std::ceil(sigma * 4.0)));
    side = radius * 2u + 1u;
    std::vector<float> kernel(static_cast<std::size_t>(side) * side, 0.0f);
    const double centre = static_cast<double>(side / 2u);
    const double inverse = 1.0 / (2.0 * sigma * sigma);
    double total = 0.0;
    for (std::uint32_t y = 0u; y < side; ++y)
    {
        const double dy = static_cast<double>(y) - centre;
        for (std::uint32_t x = 0u; x < side; ++x)
        {
            const double dx = static_cast<double>(x) - centre;
            const double value = std::exp(-(dx * dx + dy * dy) * inverse);
            kernel[static_cast<std::size_t>(y) * side + x] = static_cast<float>(value);
            total += value;
        }
    }
    const auto normalization = static_cast<float>(1.0 / total);
    for (float& sample : kernel)
    {
        sample *= normalization;
    }
    return kernel;
}

std::vector<float> restorationBlur(std::span<const float> samples, std::uint32_t width, std::uint32_t height,
                                   std::span<const float> kernel, std::uint32_t side)
{
    std::vector<float> answer(samples.size(), 0.0f);
    const auto radius = static_cast<std::int64_t>(side / 2u);
    const auto signedWidth = static_cast<std::int64_t>(width);
    const auto signedHeight = static_cast<std::int64_t>(height);
    for (std::int64_t y = 0; y < signedHeight; ++y)
    {
        for (std::int64_t x = 0; x < signedWidth; ++x)
        {
            double sum = 0.0;
            for (std::int64_t ky = 0; ky < static_cast<std::int64_t>(side); ++ky)
            {
                const std::int64_t sourceY = reflect(y + radius - ky, signedHeight);
                for (std::int64_t kx = 0; kx < static_cast<std::int64_t>(side); ++kx)
                {
                    const std::int64_t sourceX = reflect(x + radius - kx, signedWidth);
                    sum += static_cast<double>(kernel[static_cast<std::size_t>(ky) * side + kx]) *
                           static_cast<double>(samples[static_cast<std::size_t>(sourceY) * width + sourceX]);
                }
            }
            answer[static_cast<std::size_t>(y) * width + x] = static_cast<float>(sum);
        }
    }
    return answer;
}

double restorationResidual(std::span<const float> restored, std::span<const float> original)
{
    if (restored.size() != original.size() || restored.empty())
    {
        return (std::numeric_limits<double>::max)();
    }
    double total = 0.0;
    for (std::size_t i = 0u; i < restored.size(); ++i)
    {
        if (!std::isfinite(restored[i]))
        {
            return (std::numeric_limits<double>::max)();
        }
        const double difference = static_cast<double>(restored[i]) - static_cast<double>(original[i]);
        total += difference * difference;
    }
    return std::sqrt(total / static_cast<double>(restored.size()));
}

double restorationResidualRatio(std::span<const float> restored, std::span<const float> blurredInput,
                                std::span<const float> original)
{
    const double before = restorationResidual(blurredInput, original);
    if (!(before > 0.0) || !std::isfinite(before))
    {
        return (std::numeric_limits<double>::max)();
    }
    const double after = restorationResidual(restored, original);
    const double ratio = after / before;
    return std::isfinite(ratio) ? ratio : (std::numeric_limits<double>::max)();
}

std::string restorationText(std::span<const float> restored, std::span<const float> original, std::uint32_t width)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "residual=" << restorationResidual(restored, original) << '\n';
    stream << "width=" << width << '\n';
    for (std::size_t i = 0u; i < restored.size(); i += kDigestStride)
    {
        stream << i << '=' << static_cast<double>(restored[i]) << '\n';
    }
    return stream.str();
}

double restorationNoiseLevel(std::span<const float> samples, std::uint32_t width, std::uint32_t height)
{
    std::vector<double> deviations;
    deviations.reserve(height);
    for (std::uint32_t y = 0u; y < height; ++y)
    {
        const std::span<const float> row = samples.subspan(static_cast<std::size_t>(y) * width, width);
        double deviation = 0.0;
        if (clippedDeviation(row, deviation))
        {
            deviations.push_back(deviation);
        }
    }
    if (deviations.empty())
    {
        return 0.0;
    }
    std::sort(deviations.begin(), deviations.end());
    return deviations[deviations.size() / 2u] * 0.70710678118654752440;
}

double restorationNoiseRatio(std::span<const float> denoised, std::span<const float> noisy, std::uint32_t width,
                             std::uint32_t height)
{
    for (const float sample : denoised)
    {
        if (!std::isfinite(sample))
        {
            return (std::numeric_limits<double>::max)();
        }
    }
    const double before = restorationNoiseLevel(noisy, width, height);
    if (!(before > 0.0))
    {
        return (std::numeric_limits<double>::max)();
    }
    const double after = restorationNoiseLevel(denoised, width, height);
    const double ratio = after / before;
    return std::isfinite(ratio) ? ratio : (std::numeric_limits<double>::max)();
}

std::string restorationDenoiseText(std::span<const float> denoised, std::span<const float> noisy, std::uint32_t width,
                                   std::uint32_t height)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << std::scientific;
    stream << "ratio=" << restorationNoiseRatio(denoised, noisy, width, height) << '\n';
    stream << "level=" << restorationNoiseLevel(denoised, width, height) << '\n';
    stream << "width=" << width << '\n';
    stream << "height=" << height << '\n';
    for (std::size_t i = 0u; i < denoised.size(); i += kDigestStride)
    {
        stream << i << '=' << static_cast<double>(denoised[i]) << '\n';
    }
    return stream.str();
}

} // namespace an::bench
