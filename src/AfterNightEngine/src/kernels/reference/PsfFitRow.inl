// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the scalar point-spread-function evaluation, included where it is compiled.
//
// This file is not a header. It defines the two evaluations a star fit iterates over - the residual
// sum of a trial step, and the residual sum with the normal-equation matrix and gradient of an
// accepted one - as one scalar text, and it is included twice: once by the fit itself, inside an
// unnamed namespace, as the scalar authority, and once by the CPU kernel tables inside the vector
// library's per-target namespace, where the vector bodies call it for the samples past their last
// full vector. So the tail of a compiled run and the reference it is compared against are the same
// expressions in the same order, and the only difference a conformance case can find is the one the
// vector exponential introduces.
//
// The includer supplies `PsfSampleRun`, `PsfModelTerms` and `PsfNormalEquations` from
// `kernels/reference/AlgorithmKernels.h`, and `<cmath>`, `<cstddef>` and `<cstdint>`.
//
// **The Jacobian is never materialised.** Seven or eight partial derivatives are formed per sample
// and consumed immediately into the upper triangle of the normal-equation matrix and into the
// gradient, so a fit over a four-hundred-sample box holds at most thirty-six matrix entries and
// eight gradient terms rather than a four-hundred-by-eight matrix. That is the whole reason this
// evaluation is one function rather than a Jacobian builder and a matrix product.

/// One sample's rotated offsets from the model centre.
struct PsfSampleOffsets
{
    /// The offset along the model's major axis.
    double alongMajor;

    /// The offset along the model's minor axis.
    double alongMinor;
};

/// Rotates one sample's offset from the model centre into the model's own axes.
///
/// @param terms the model.
/// @param x the sample's horizontal coordinate.
/// @param y the sample's vertical coordinate.
/// @return the offsets along the two axes.
[[nodiscard]] inline PsfSampleOffsets psfSampleOffsetsOf(const PsfModelTerms& terms, double x, double y) noexcept
{
    const double dx = x - terms.centreX;
    const double dy = y - terms.centreY;
    return PsfSampleOffsets{.alongMajor = terms.angleCosine * dx - terms.angleSine * dy,
                            .alongMinor = terms.angleSine * dx + terms.angleCosine * dy};
}

/// The model's value at one sample, minus its background.
///
/// The Gaussian's exponential of the negated quadratic form, or the Moffat's power of one plus it.
/// The Moffat is evaluated as an exponential of a logarithm rather than through the platform's
/// power, because that is what a vector body can do and the two must agree.
///
/// @param terms the model.
/// @param quadratic the quadratic form at the sample.
/// @param logarithm receives the logarithm of one plus the quadratic form, for a Moffat only.
/// @return the shape factor the amplitude multiplies.
[[nodiscard]] inline double psfShapeOf(const PsfModelTerms& terms, double quadratic, double& logarithm) noexcept
{
    if (!terms.moffat)
    {
        logarithm = 0.0;
        return std::exp(-quadratic);
    }
    logarithm = std::log(1.0 + quadratic);
    return std::exp(-terms.beta * logarithm);
}

/// The residual sum of squares over the samples of a run from one index onwards.
///
/// @param samples the compacted run.
/// @param begin the first sample to read.
/// @param terms the model.
/// @return the sum of squared residuals over `[begin, samples.count)`.
[[nodiscard]] inline double psfResidualScalarRun(const PsfSampleRun& samples, std::uint64_t begin,
                                                 const PsfModelTerms& terms) noexcept
{
    double sum = 0.0;
    for (std::uint64_t index = begin; index < samples.count; ++index)
    {
        const PsfSampleOffsets offsets = psfSampleOffsetsOf(terms, samples.x[index], samples.y[index]);
        const double quadratic = offsets.alongMajor * offsets.alongMajor * terms.inverseScaleX +
                                 offsets.alongMinor * offsets.alongMinor * terms.inverseScaleY;
        double logarithm = 0.0;
        const double shape = psfShapeOf(terms, quadratic, logarithm);
        const double residual = terms.background + terms.amplitude * shape - samples.values[index];
        sum += residual * residual;
    }
    return sum;
}

/// Adds the residual sum, normal-equation matrix and gradient of a run's tail into an accumulation.
///
/// It adds rather than assigns, so a vector body reduces its own lanes into `out` first and then
/// calls this for what is left. A caller with nothing already accumulated passes a cleared `out`.
///
/// @param samples the compacted run.
/// @param begin the first sample to read.
/// @param terms the model.
/// @param out the accumulation to add into.
inline void psfNormalEquationsScalarAccumulate(const PsfSampleRun& samples, std::uint64_t begin,
                                               const PsfModelTerms& terms, PsfNormalEquations& out) noexcept
{
    const std::size_t parameters = static_cast<std::size_t>(terms.parameterCount);
    for (std::uint64_t index = begin; index < samples.count; ++index)
    {
        const PsfSampleOffsets offsets = psfSampleOffsetsOf(terms, samples.x[index], samples.y[index]);
        const double scaledMajor = offsets.alongMajor * terms.inverseScaleX;
        const double scaledMinor = offsets.alongMinor * terms.inverseScaleY;
        const double quadratic = offsets.alongMajor * scaledMajor + offsets.alongMinor * scaledMinor;
        double logarithm = 0.0;
        const double shape = psfShapeOf(terms, quadratic, logarithm);
        const double residual = terms.background + terms.amplitude * shape - samples.values[index];
        out.residualSumOfSquares += residual * residual;

        // The Gaussian differentiates the exponential and the Moffat differentiates the power. Once
        // the common factor is named - twice the amplitude times the shape for one, and twice the
        // amplitude times the exponent times the shape over its base for the other - the six
        // geometric derivatives are the same six expressions.
        const double common = terms.moffat ? 2.0 * terms.amplitude * terms.beta * shape / (1.0 + quadratic)
                                           : 2.0 * terms.amplitude * shape;
        const double ratioMinor = offsets.alongMinor * terms.inverseScaleX * terms.inverseRatio;
        double jacobian[kPsfMaxFitParameters];
        jacobian[0] = 1.0;
        jacobian[1] = shape;
        jacobian[2] = common * (scaledMajor * terms.angleCosine + scaledMinor * terms.angleSine);
        jacobian[3] = common * (scaledMinor * terms.angleCosine - scaledMajor * terms.angleSine);
        jacobian[4] = common * 0.5 * (scaledMajor * scaledMajor + ratioMinor * ratioMinor);
        jacobian[5] = common * 0.5 * (-terms.ratioSine * terms.inverseScaleY * terms.inverseRatio) *
                      (offsets.alongMinor * offsets.alongMinor);
        jacobian[6] = common * offsets.alongMajor * offsets.alongMinor * (terms.inverseScaleX - terms.inverseScaleY);
        if (parameters > 7u)
        {
            jacobian[7] = terms.amplitude * terms.betaDerivativeScale * logarithm * shape;
        }
        for (std::size_t row = 0u; row < parameters; ++row)
        {
            out.gradient[row] += jacobian[row] * residual;
            for (std::size_t column = row; column < parameters; ++column)
            {
                out.matrix[row][column] += jacobian[row] * jacobian[column];
            }
        }
    }
}
