// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNight benchmark system - deterministic synthetic fixture generation.
//
// A fixture is raw bytes plus the descriptor that explains them, and every run must be handed
// exactly the same bytes or a comparison of two runs proves nothing. Determinism is therefore the
// whole requirement here: the same generator, seed, generator version, and descriptor produce the
// same bytes on any machine, in any process, in any order.
//
// Generation is versioned for a related reason. A generator that quietly changes what it emits
// under the same name invalidates every archived digest taken with it, and the damage surfaces
// later as a correctness difference between two runs that are both behaving correctly. The
// version is mixed into the value stream rather than merely recorded beside it, so a bump is
// visible in the digest instead of being a claim about it.
//
// The generators use their own small integer mixer rather than the standard library's
// distributions: `std::mt19937` is specified exactly, but `std::uniform_real_distribution` and
// friends are not, so two standard libraries may map the same engine output to different values.
// A fixture that differs between platforms would make a cross-platform comparison meaningless.

#pragma once

#include "Fixture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace an::bench
{

/// Version of the synthetic generators as a set.
///
/// Bump it whenever any generator's output changes for a given seed and descriptor. Every fixture
/// records the version that produced it, so an archived digest states what it was taken with.
inline constexpr std::uint32_t kSyntheticGeneratorVersion = 2;

/// The deterministic synthetic generators.
///
/// Recognised source ids, all prefixed `synthetic.`:
///
/// | Id | Content |
/// |---|---|
/// | `synthetic.zero` | Every sample zero |
/// | `synthetic.constant` | Every sample at mid range |
/// | `synthetic.asinh_boundaries` | Unit ramp plus adjacent Float32 protection and inverse-domain boundaries |
/// | `synthetic.ghs_boundaries` | The same, for every generalized-hyperbolic bias regime and limit |
/// | `synthetic.mtf_boundaries` | The same, for every midtones transfer balance and endpoint limit |
/// | `synthetic.statistical_sky` | A black margin, normal-like background, cold pixels and stars |
/// | `synthetic.ramp` | Horizontal ramp across the row, so a tail or stride error shifts values |
/// | `synthetic.gradient` | Diagonal gradient, varying along both axes |
/// | `synthetic.checkerboard` | Alternating extremes in 8-sample squares |
/// | `synthetic.impulse` | Zero with one full-scale sample at the centre of each plane |
/// | `synthetic.extremes` | Alternating representable minimum and maximum |
/// | `synthetic.uniform_noise` | Uniform pseudo-random values |
/// | `synthetic.gaussian_noise` | Normally distributed values around mid range |
/// | `synthetic.stars` | Sparse Gaussian point sources over a low background |
/// | `synthetic.nonfinite` | Mostly finite, with NaN and both infinities placed deterministically |
///
/// Every generator fills the whole buffer the descriptor implies, padding bytes included, so a
/// descriptor with a wider stride than its rows still hashes reproducibly.
class SyntheticFixtureSource final : public FixtureSource
{
public:
    /// Construct a source at `generatorVersion`.
    ///
    /// The default is the current version; passing another is how a test proves that a version
    /// bump changes the bytes rather than only the label. It is not a compatibility mechanism:
    /// an older version number does not reproduce an older generator's output.
    explicit SyntheticFixtureSource(std::uint32_t generatorVersion = kSyntheticGeneratorVersion);

    /// Every generator id this source recognises.
    [[nodiscard]] static std::vector<std::string> generatorIds();

    /// True when `sourceId` names one of them.
    [[nodiscard]] static bool knows(const std::string& sourceId);

    /// Build the fixture `sourceId` asks for.
    ///
    /// Returns false and sets `error` when the id is unknown, the descriptor is invalid, the
    /// layout is one the generators do not produce, or the generator cannot express itself in the
    /// requested sample type - `synthetic.nonfinite` in an integer type has no meaning, and
    /// producing something else instead would be a fixture that silently does not test what its
    /// name says.
    bool acquire(const std::string& sourceId, std::uint64_t seed, const SampleDescriptor& descriptor, Fixture& out,
                 std::string& error) override;

private:
    std::uint32_t m_generatorVersion;
};

} // namespace an::bench
