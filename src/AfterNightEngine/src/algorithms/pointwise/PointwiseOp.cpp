// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - names for the element-wise steps.

#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>

namespace an
{

const char* pointwiseOpKindName(PointwiseOpKind kind) noexcept
{
    switch (kind)
    {
    case PointwiseOpKind::Unknown:
        return "unknown";
    case PointwiseOpKind::Scale:
        return "scale";
    case PointwiseOpKind::Bias:
        return "bias";
    case PointwiseOpKind::Clamp:
        return "clamp";
    case PointwiseOpKind::Gamma:
        return "gamma";
    case PointwiseOpKind::Lut:
        return "lut";
    case PointwiseOpKind::ChannelMatrix:
        return "channelMatrix";
    case PointwiseOpKind::Mask:
        return "mask";
    case PointwiseOpKind::Blend:
        return "blend";
    case PointwiseOpKind::Add:
        return "add";
    case PointwiseOpKind::Subtract:
        return "subtract";
    case PointwiseOpKind::Multiply:
        return "multiply";
    case PointwiseOpKind::Divide:
        return "divide";
    case PointwiseOpKind::Minimum:
        return "minimum";
    case PointwiseOpKind::Maximum:
        return "maximum";
    case PointwiseOpKind::Compare:
        return "compare";
    case PointwiseOpKind::SampledCurve:
        return "sampledCurve";
    }
    return "unknown";
}

} // namespace an
