// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "widgets/ToneCurveModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace an::ui
{
namespace
{

/// How close an inner point may come to its neighbours.
constexpr double kMinimumPointSpacing = 0.0001;

/// How many points Akima and Steffen need before they are used; fewer fall back to the natural cubic.
constexpr std::size_t kMinimumLocalRulePoints = 5u;

} // namespace

ToneCurveModel::ToneCurveModel() : m_points{{0.0, 0.0}, {1.0, 1.0}} {}

ToneCurveModel::ToneCurveModel(std::vector<an::CurveControlPoint> points, const an::CurveInterpolation interpolation)
    : m_points(std::move(points)), m_interpolation(interpolation)
{
}

const std::vector<an::CurveControlPoint>& ToneCurveModel::points() const noexcept
{
    return m_points;
}

std::size_t ToneCurveModel::size() const noexcept
{
    return m_points.size();
}

bool ToneCurveModel::isEmpty() const noexcept
{
    return m_points.empty();
}

an::CurveInterpolation ToneCurveModel::interpolation() const noexcept
{
    return m_interpolation;
}

void ToneCurveModel::setInterpolation(const an::CurveInterpolation interpolation) noexcept
{
    m_interpolation = interpolation;
}

an::CurveInterpolation ToneCurveModel::effectiveInterpolation() const noexcept
{
    if (m_points.size() <= 2u)
    {
        return an::CurveInterpolation::Linear;
    }
    if ((m_interpolation == an::CurveInterpolation::Akima || m_interpolation == an::CurveInterpolation::Steffen) &&
        m_points.size() < kMinimumLocalRulePoints)
    {
        return an::CurveInterpolation::NaturalCubic;
    }
    return m_interpolation;
}

std::size_t ToneCurveModel::insertPoint(const double input, const double output)
{
    std::size_t index = 0u;
    while (index < m_points.size() && input > m_points[index].input)
    {
        ++index;
    }
    const double held = std::clamp(output, 0.0, 1.0);
    if (index < m_points.size() && m_points[index].input == input)
    {
        m_points[index].output = held;
        return index;
    }
    m_points.insert(m_points.begin() + static_cast<std::ptrdiff_t>(index), an::CurveControlPoint{input, held});
    return index;
}

void ToneCurveModel::updatePoint(const std::size_t index, double input, const double output)
{
    if (index >= m_points.size())
    {
        return;
    }
    const std::size_t last = m_points.size() - 1u;
    if (input <= 0.0 && index > 0u)
    {
        input = kMinimumPointSpacing;
    }
    if (input >= 1.0 && index < last)
    {
        input = 1.0 - kMinimumPointSpacing;
    }
    if (index > 0u && m_points[index - 1u].input >= input)
    {
        input = m_points[index - 1u].input + kMinimumPointSpacing;
    }
    if (index < last && m_points[index + 1u].input <= input)
    {
        input = m_points[index + 1u].input - kMinimumPointSpacing;
    }
    m_points[index] = an::CurveControlPoint{input, std::clamp(output, 0.0, 1.0)};
}

void ToneCurveModel::removePoint(const std::size_t index)
{
    if (index > 0u && index + 1u < m_points.size())
    {
        m_points.erase(m_points.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

bool ToneCurveModel::isIdentity() const noexcept
{
    return m_points.size() == 2u && m_points[0].input == 0.0 && m_points[0].output == 0.0 && m_points[1].input == 1.0 &&
           m_points[1].output == 1.0;
}

double ToneCurveModel::evaluate(const double input) const
{
    if (m_points.size() < 2u)
    {
        return 0.0;
    }
    const an::Result<double> value =
        an::evaluateCurve(m_points, effectiveInterpolation(), an::CurveDomainPolicy::Clamp, input);
    return value.hasValue() ? *value : 0.0;
}

bool operator==(const ToneCurveModel& left, const ToneCurveModel& right) noexcept
{
    return left.m_interpolation == right.m_interpolation &&
           std::equal(left.m_points.begin(), left.m_points.end(), right.m_points.begin(), right.m_points.end(),
                      [](const an::CurveControlPoint& a, const an::CurveControlPoint& b) {
                          return a.input == b.input && a.output == b.output;
                      });
}

} // namespace an::ui
