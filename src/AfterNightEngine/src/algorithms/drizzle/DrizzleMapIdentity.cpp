// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngine - the drizzle map identity's wire encoding and its exact comparison.
//
// The record is written field by field in a fixed order: the signature "ANDMAP" and CR LF, the
// version, the normalization grid, both conventions, both extents, the scale, the registration's
// linear kind, matrix, polynomial order, coefficients and normalization, the post-translation, and
// the source SIP's presence, reference pixel and each of its four polynomials' order and full
// coefficient table. Integers are written least significant byte first and doubles as their IEEE-754
// bits, so the bytes do not depend on the host; an absent SIP is written as a default one, and
// every byte after the last field stays zero. Validation re-encodes and compares every byte.

#include <AfterNightEngine/Algorithms/Drizzle/DrizzleMapIdentity.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <new>

namespace an
{
Result<DrizzleMapIdentity> encodeDrizzleMapIdentity(const DrizzleMapDescriptor& descriptor,
                                                    DrizzleNormalizationSpace normalization) noexcept
try
{
    if (normalization != DrizzleNormalizationSpace::SourcePixels &&
        normalization != DrizzleNormalizationSpace::OutputPixels)
        return Status::failure(ErrorCode::InvalidArgument,
                               {.message = "the normalization grid is unknown",
                                .operation = "an::encodeDrizzleMapIdentity",
                                .domain = ErrorDomain::Algorithms,
                                .context = {StatusContextField::ofText(ContextKey::Parameter, "normalization")}});
    DrizzleMapIdentity identity;
    std::size_t offset = 0u;
    auto integer = [&](std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0u; i < bytes; ++i)
            identity.bytes[offset++] = static_cast<std::byte>(value >> (i * 8u));
    };
    auto real = [&](double value) { integer(std::bit_cast<std::uint64_t>(value), 8u); };
    for (const char c : std::array{'A', 'N', 'D', 'M', 'A', 'P', '\r', '\n'})
        integer(static_cast<unsigned char>(c), 1u);
    integer(1u, 4u);
    integer(static_cast<std::uint8_t>(normalization), 4u);
    integer(static_cast<std::uint8_t>(descriptor.coordinateConvention()), 4u);
    integer(static_cast<std::uint8_t>(descriptor.invalidCoordinate()), 4u);
    for (const auto& extent : {descriptor.sourceExtent(), descriptor.outputExtent()})
    {
        integer(extent.width(), 4u);
        integer(extent.height(), 4u);
        integer(extent.channelCount(), 4u);
    }
    real(descriptor.scale());
    const auto transform = descriptor.mapping().transform.toRequest();
    integer(static_cast<std::uint8_t>(transform.linearKind), 4u);
    for (auto value : transform.sourceToReference)
        real(value);
    integer(transform.polynomial.order, 4u);
    for (auto value : transform.polynomial.x)
        real(value);
    for (auto value : transform.polynomial.y)
        real(value);
    real(transform.polynomial.normalizationOrigin.x);
    real(transform.polynomial.normalizationOrigin.y);
    real(transform.polynomial.normalizationScale.x);
    real(transform.polynomial.normalizationScale.y);
    real(descriptor.mapping().postTranslation.x);
    real(descriptor.mapping().postTranslation.y);
    integer(descriptor.mapping().sourceDistortion.has_value(), 4u);
    const auto sip = descriptor.mapping().sourceDistortion.value_or(SipDistortion{});
    real(sip.referencePixel().x);
    real(sip.referencePixel().y);
    for (const auto& polynomial : {sip.forwardX(), sip.forwardY(), sip.inverseX(), sip.inverseY()})
    {
        integer(polynomial.order, 4u);
        for (const auto& row : polynomial.coefficients)
            for (const auto value : row)
                real(value);
    }
    // The fixed record has space for every coefficient in these published tables. A future table
    // size change must choose a new wire version rather than silently overrun or truncate this one.
    static_assert(kRegistrationPolynomialTerms == 15u && kSipCoefficientRowCount == 7u);
    return identity;
}
catch (const std::bad_alloc&)
{
    return Status::failure(ErrorCode::AllocationFailed);
}
Status validateDrizzleMapIdentity(const DrizzleMapDescriptor& descriptor, DrizzleNormalizationSpace normalization,
                                  std::span<const std::byte> stored) noexcept
try
{
    auto expected = encodeDrizzleMapIdentity(descriptor, normalization);
    if (!expected)
        return expected.status();
    if (stored.size() != expected->bytes.size() || !std::equal(stored.begin(), stored.end(), expected->bytes.begin()))
        return Status::failure(
            stored.size() != expected->bytes.size() ? ErrorCode::InvalidData : ErrorCode::IncompatibleArguments,
            {.message = "the stored drizzle identity does not match the requested geometry",
             .operation = "an::validateDrizzleMapIdentity",
             .domain = ErrorDomain::Algorithms,
             .context = {StatusContextField::ofText(ContextKey::Parameter, "stored"),
                         StatusContextField::ofCount(ContextKey::Requested, stored.size()),
                         StatusContextField::ofCount(ContextKey::Available, expected->bytes.size())}});
    return Status::success();
}
catch (const std::bad_alloc&)
{
    return Status::failure(ErrorCode::AllocationFailed);
}
} // namespace an
