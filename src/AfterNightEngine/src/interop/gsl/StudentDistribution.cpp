// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the generalized ESD critical value computed with GSL, and GSL's version
// report.
//
// The one translation unit of the engine that includes GSL. It takes the Student t quantile from
// `gsl_cdf_tdist_Pinv` after checking that the probability lies strictly between zero and one and
// that there is at least one degree of freedom, and it installs no error handler: GSL's handler is
// process-wide, so changing it would change every other GSL caller in the process.
//
// The version report states both versions GSL can: the one its headers declare, `GSL_VERSION`, and
// the one the loaded library exports as `gsl_version`.

#include "interop/gsl/StudentDistribution.h"

#include "runtime/DependencyVersions.h"

#include <cmath>

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_version.h>

namespace an
{

Result<float> studentizedDeviateCritical(std::uint32_t samples, double significance)
{
    // Alpha is rounded to float before the probability is formed in double, as the adopted deviate
    // test rounds it.
    const float alpha = static_cast<float>(significance);
    if (samples < 3u || !(alpha > 0.0f && alpha < 1.0f))
    {
        return Status::failure(ErrorCode::InvalidArgument);
    }
    const double n = samples;
    const double probability = 1.0 - static_cast<double>(alpha) / (2.0 * n);
    if (!(probability > 0.0 && probability < 1.0))
    {
        return Status::failure(ErrorCode::InvalidArgument);
    }
    const double quantile = gsl_cdf_tdist_Pinv(probability, n - 2.0);
    const float critical =
        static_cast<float>((n - 1.0) * quantile / (std::sqrt(n) * std::sqrt(n - 2.0 + quantile * quantile)));
    if (!std::isfinite(critical))
    {
        return Status::failure(ErrorCode::InvalidData);
    }
    return critical;
}

namespace dependency_detail
{

void reportGsl(DependencyVersion& entry) noexcept
{
    entry.name = "gsl";
    entry.linkage = DependencyLinkage::SharedLibrary;
    setVersionTextFromBanner(entry.compiled, GSL_VERSION, nullptr);

    // The loaded version is an exported variable rather than a function, and this library's own
    // header declares it as an import on Windows, so it is read the way any other symbol is. A
    // report is not a reason to load a module, and nothing here does.
    setVersionTextFromBanner(entry.loaded, gsl_version, nullptr);
}

} // namespace dependency_detail

} // namespace an
