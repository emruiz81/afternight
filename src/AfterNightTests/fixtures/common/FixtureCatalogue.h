// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

// The standard fixture descriptions: the inputs the adapter is held to and the captured expectations name.
//
// Between them they cover every one of the eight representations, mono and colour, signed samples, integer
// saturation, floats outside zero to one, a filter array, non-finite values and a negative zero - the inputs a test
// of an image consumer has to reach, stated once so every suite is held to the same set.

#include "FixtureImage.h"

#include <string_view>
#include <vector>

namespace an::tests::fixtures
{

/// Every standard description, in a stable order. Names are unique.
[[nodiscard]] const std::vector<FixtureImage>& standardFixtures();

/// The standard description of this name, or null.
[[nodiscard]] const FixtureImage* standardFixture(std::string_view name);

/// The inputs the behavior corpus runs the product's actions over: small colour and mono scenes in the
/// representations the actions meet - normalized floats, floats outside zero to one, sixteen-bit and eight-bit
/// sensor codes, signed sixteen-bit samples - a Bayer mosaic large enough for the filter-array actions, and star
/// fields for the actions that detect or mask stars. Names are unique and none is a standard fixture's.
[[nodiscard]] const std::vector<FixtureImage>& behaviorFixtures();

/// The behavior description of this name, or null.
[[nodiscard]] const FixtureImage* behaviorFixture(std::string_view name);

/// The standard or behavior description of this name, or null: how a reader finds the input a record names.
[[nodiscard]] const FixtureImage* fixtureNamed(std::string_view name);

} // namespace an::tests::fixtures
