// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "Descriptors.h"

#include <array>
#include <limits>

namespace an::bench
{
namespace
{

/// One table per enum, so a name and its value can never drift apart the way two parallel
/// switch statements do.
template <typename EnumT, std::size_t N>
std::string_view lookupName(const std::array<std::pair<EnumT, std::string_view>, N>& table, EnumT value)
{
    for (const auto& entry : table)
    {
        if (entry.first == value)
        {
            return entry.second;
        }
    }
    return "Unknown";
}

template <typename EnumT, std::size_t N>
bool lookupValue(const std::array<std::pair<EnumT, std::string_view>, N>& table, std::string_view text, EnumT& out)
{
    for (const auto& entry : table)
    {
        if (entry.second == text)
        {
            out = entry.first;
            return true;
        }
    }
    return false;
}

constexpr std::array<std::pair<SampleType, std::string_view>, 10> kSampleTypes{{
    {SampleType::UInt8, "UInt8"},
    {SampleType::Int8, "Int8"},
    {SampleType::UInt16, "UInt16"},
    {SampleType::Int16, "Int16"},
    {SampleType::UInt32, "UInt32"},
    {SampleType::Int32, "Int32"},
    {SampleType::Int64, "Int64"},
    {SampleType::UInt64, "UInt64"},
    {SampleType::Float32, "Float32"},
    {SampleType::Float64, "Float64"},
}};

constexpr std::array<std::pair<Layout, std::string_view>, 4> kLayouts{{
    {Layout::Planar, "Planar"},
    {Layout::Interleaved, "Interleaved"},
    {Layout::SemiPlanar, "SemiPlanar"},
    {Layout::PackedCfa, "PackedCfa"},
}};

constexpr std::array<std::pair<ByteOrder, std::string_view>, 3> kByteOrders{{
    {ByteOrder::Native, "Native"},
    {ByteOrder::Little, "Little"},
    {ByteOrder::Big, "Big"},
}};

constexpr std::array<std::pair<CorrectnessClass, std::string_view>, 9> kCorrectnessClasses{{
    {CorrectnessClass::BitExact, "BitExact"},
    {CorrectnessClass::ExactAfterMapping, "ExactAfterMapping"},
    {CorrectnessClass::MaxUlp, "MaxUlp"},
    {CorrectnessClass::AbsoluteRelative, "AbsoluteRelative"},
    {CorrectnessClass::LsbDistribution, "LsbDistribution"},
    {CorrectnessClass::RmsPsnr, "RmsPsnr"},
    {CorrectnessClass::GeometricResidual, "GeometricResidual"},
    {CorrectnessClass::EstimatorTolerance, "EstimatorTolerance"},
    {CorrectnessClass::MetadataExact, "MetadataExact"},
}};

constexpr std::array<std::pair<BackendId, std::string_view>, 11> kBackends{{
    {BackendId::Unspecified, "Unspecified"},
    {BackendId::Scalar, "Scalar"},
    {BackendId::CompilerVectorized, "CompilerVectorized"},
    {BackendId::PortableSimd, "PortableSimd"},
    {BackendId::Sse2, "Sse2"},
    {BackendId::Sse42, "Sse42"},
    {BackendId::Avx, "Avx"},
    {BackendId::Avx2, "Avx2"},
    {BackendId::Avx512, "Avx512"},
    {BackendId::Neon, "Neon"},
    {BackendId::Gpu, "Gpu"},
}};

std::uint64_t minimumRowBytes(const SampleDescriptor& descriptor)
{
    constexpr std::uint64_t kMax = (std::numeric_limits<std::uint64_t>::max)();
    std::uint64_t samples = descriptor.width;
    if (descriptor.layout != Layout::Planar && descriptor.layout != Layout::PackedCfa)
    {
        if (descriptor.channels != 0 && samples > kMax / descriptor.channels)
        {
            return 0;
        }
        samples *= descriptor.channels;
    }
    const std::uint64_t sampleBytes = descriptor.sampleSizeBytes();
    return sampleBytes != 0 && samples <= kMax / sampleBytes ? samples * sampleBytes : 0;
}

} // namespace

std::uint32_t SampleDescriptor::sampleSizeBytes() const
{
    switch (sampleType)
    {
    case SampleType::UInt8:
    case SampleType::Int8:
        return 1;
    case SampleType::UInt16:
    case SampleType::Int16:
        return 2;
    case SampleType::UInt32:
    case SampleType::Int32:
    case SampleType::Float32:
        return 4;
    case SampleType::Int64:
    case SampleType::UInt64:
    case SampleType::Float64:
        return 8;
    }
    return 0;
}

std::uint64_t SampleDescriptor::rowBytes() const
{
    const std::uint64_t minimum = minimumRowBytes(*this);
    if (minimum == 0)
    {
        return 0;
    }
    return rowStrideBytes != 0 ? rowStrideBytes : minimum;
}

std::uint64_t SampleDescriptor::totalBytes() const
{
    if (width == 0 || height == 0 || channels == 0 || sampleSizeBytes() == 0)
    {
        return 0;
    }

    // Checked throughout: a scenario file is untrusted input, and a wrapped size would allocate a
    // small buffer for a large image - a silent heap overflow rather than a rejected descriptor.
    constexpr std::uint64_t kMax = (std::numeric_limits<std::uint64_t>::max)();

    const std::uint64_t row = rowBytes();
    if (row == 0)
    {
        return 0;
    }

    const std::uint64_t planes = layout == Layout::Planar ? channels : 1u;

    if (row > kMax / height)
    {
        return 0;
    }
    const std::uint64_t plane = planeStrideBytes != 0 ? planeStrideBytes : row * height;
    if (planeStrideBytes != 0 && planeStrideBytes < row * height)
    {
        return 0;
    }
    if (plane > kMax / planes)
    {
        return 0;
    }
    return plane * planes;
}

bool SampleDescriptor::isValid() const
{
    if (width == 0 || height == 0 || channels == 0 || sampleSizeBytes() == 0)
    {
        return false;
    }

    if (layout == Layout::SemiPlanar)
    {
        // The descriptor does not yet carry the per-plane subsampling needed to size this layout.
        return false;
    }
    if (layout == Layout::PackedCfa && (channels != 1 || cfaPattern.empty()))
    {
        return false;
    }
    if (layout != Layout::Planar && planeStrideBytes != 0)
    {
        return false;
    }

    const std::uint64_t minimumRow = minimumRowBytes(*this);
    if (minimumRow == 0)
    {
        return false;
    }
    if (rowStrideBytes != 0 && rowStrideBytes < minimumRow)
    {
        return false;
    }
    return totalBytes() != 0;
}

bool operator==(const SampleDescriptor& lhs, const SampleDescriptor& rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.channels == rhs.channels &&
           lhs.sampleType == rhs.sampleType && lhs.layout == rhs.layout && lhs.byteOrder == rhs.byteOrder &&
           lhs.rowStrideBytes == rhs.rowStrideBytes && lhs.planeStrideBytes == rhs.planeStrideBytes &&
           lhs.cfaPattern == rhs.cfaPattern;
}

std::string_view toString(SampleType value)
{
    return lookupName(kSampleTypes, value);
}
std::string_view toString(Layout value)
{
    return lookupName(kLayouts, value);
}
std::string_view toString(ByteOrder value)
{
    return lookupName(kByteOrders, value);
}
std::string_view toString(CorrectnessClass value)
{
    return lookupName(kCorrectnessClasses, value);
}
std::string_view toString(BackendId value)
{
    return lookupName(kBackends, value);
}

bool fromString(std::string_view text, SampleType& out)
{
    return lookupValue(kSampleTypes, text, out);
}
bool fromString(std::string_view text, Layout& out)
{
    return lookupValue(kLayouts, text, out);
}
bool fromString(std::string_view text, ByteOrder& out)
{
    return lookupValue(kByteOrders, text, out);
}
bool fromString(std::string_view text, CorrectnessClass& out)
{
    return lookupValue(kCorrectnessClasses, text, out);
}
bool fromString(std::string_view text, BackendId& out)
{
    return lookupValue(kBackends, text, out);
}

} // namespace an::bench
