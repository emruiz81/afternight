// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// Highway kernel bodies, shared by the static and the runtime-dispatch candidates.
//
// One source rather than two copies, for the same reason the scalar and auto-vectorization
// candidates share theirs: the only difference those two candidates may have is how the target is
// chosen. If the kernels could drift, the pair would stop measuring dispatch and start measuring
// whichever transcription was luckier.
//
// The includer arranges the surrounding namespace and target machinery.

namespace hn = hwy::HWY_NAMESPACE;

using namespace spike::constants;

void transfer(const std::uint16_t* src, float* dst, std::size_t count)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToUnsigned<decltype(df)> du32;
    const hn::Rebind<std::uint16_t, decltype(df)> du16;
    const std::size_t lanes = hn::Lanes(df);
    const auto scale = hn::Set(df, kU16Scale);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto widened = hn::PromoteTo(du32, hn::LoadU(du16, src + i));
        hn::StoreU(hn::Mul(hn::ConvertTo(df, widened), scale), df, dst + i);
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU16Scale;
    }
}

void clampScale(const float* src, float* dst, std::size_t count, float scaleValue, float biasValue, float lo, float hi)
{
    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);
    const auto scale = hn::Set(df, scaleValue);
    const auto bias = hn::Set(df, biasValue);
    const auto low = hn::Set(df, lo);
    const auto high = hn::Set(df, hi);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto value = hn::MulAdd(hn::LoadU(df, src + i), scale, bias);
        hn::StoreU(hn::Min(high, hn::Max(low, value)), df, dst + i);
    }
    for (; i < count; ++i)
    {
        const float value = src[i] * scaleValue + biasValue;
        dst[i] = value < lo ? lo : (value > hi ? hi : value);
    }
}

Reduction reduce(const float* src, std::size_t count)
{
    Reduction result;
    if (count == 0)
    {
        return result;
    }

    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);

    auto sum = hn::Zero(df);
    auto minimum = hn::Set(df, src[0]);
    auto maximum = hn::Set(df, src[0]);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto value = hn::LoadU(df, src + i);
        sum = hn::Add(sum, value);
        minimum = hn::Min(minimum, value);
        maximum = hn::Max(maximum, value);
    }

    double total = static_cast<double>(hn::ReduceSum(df, sum));
    float lowest = hn::ReduceMin(df, minimum);
    float highest = hn::ReduceMax(df, maximum);

    for (; i < count; ++i)
    {
        total += static_cast<double>(src[i]);
        lowest = src[i] < lowest ? src[i] : lowest;
        highest = src[i] > highest ? src[i] : highest;
    }

    result.sum = total;
    result.minimum = lowest;
    result.maximum = highest;
    return result;
}

void strided(const float* src, std::size_t srcStride, float* dst, std::size_t dstStride, std::size_t width,
             std::size_t height, float scale, float bias, float lo, float hi)
{
    for (std::size_t y = 0; y < height; ++y)
    {
        clampScale(src + y * srcStride, dst + y * dstStride, width, scale, bias, lo, hi);
    }
}

void gamma(const float* src, float* dst, std::size_t count, float exponent)
{
    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);
    const auto power = hn::Set(df, exponent);
    const auto zero = hn::Zero(df);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto value = hn::LoadU(df, src + i);
        // pow(x, g) as exp(g * log(x)). log(0) is -inf, so zero is restored explicitly rather than
        // left to propagate a NaN through the multiply.
        const auto powered = hn::Exp(df, hn::Mul(power, hn::Log(df, value)));
        hn::StoreU(hn::IfThenElse(hn::Eq(value, zero), zero, powered), df, dst + i);
    }
    for (; i < count; ++i)
    {
        dst[i] = src[i] == 0.0f ? 0.0f : std::pow(src[i], exponent);
    }
}

void u8ToF32(const std::uint8_t* src, float* dst, std::size_t count)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToUnsigned<decltype(df)> du32;
    const hn::Rebind<std::uint8_t, decltype(df)> du8;
    const std::size_t lanes = hn::Lanes(df);
    const auto scale = hn::Set(df, kU8Scale);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto widened = hn::PromoteTo(du32, hn::LoadU(du8, src + i));
        hn::StoreU(hn::Mul(hn::ConvertTo(df, widened), scale), df, dst + i);
    }
    for (; i < count; ++i)
    {
        dst[i] = static_cast<float>(src[i]) * kU8Scale;
    }
}

void i16ToF32(const std::int16_t* src, float* dst, std::size_t count, float offset, float scaleValue)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToSigned<decltype(df)> di32;
    const hn::Rebind<std::int16_t, decltype(df)> di16;
    const std::size_t lanes = hn::Lanes(df);
    const auto bias = hn::Set(df, offset);
    const auto scale = hn::Set(df, scaleValue);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto widened = hn::PromoteTo(di32, hn::LoadU(di16, src + i));
        hn::StoreU(hn::Mul(hn::Add(hn::ConvertTo(df, widened), bias), scale), df, dst + i);
    }
    for (; i < count; ++i)
    {
        dst[i] = (static_cast<float>(src[i]) + offset) * scaleValue;
    }
}

void f32ToU16(const float* src, std::uint16_t* dst, std::size_t count)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToSigned<decltype(df)> di32;
    const hn::Rebind<std::uint16_t, decltype(df)> du16;
    const std::size_t lanes = hn::Lanes(df);
    const auto scale = hn::Set(df, kU16Max);
    const auto low = hn::Zero(df);
    const auto high = hn::Set(df, kU16Max);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto scaled = hn::Min(high, hn::Max(low, hn::Mul(hn::LoadU(df, src + i), scale)));
        const auto rounded = hn::ConvertTo(di32, hn::Round(scaled));
        hn::StoreU(hn::DemoteTo(du16, rounded), du16, dst + i);
    }
    for (; i < count; ++i)
    {
        const float scaled = src[i] * kU16Max;
        const float clamped = scaled < 0.0f ? 0.0f : (scaled > kU16Max ? kU16Max : scaled);
        dst[i] = static_cast<std::uint16_t>(std::nearbyintf(clamped));
    }
}

void f32ToU8(const float* src, std::uint8_t* dst, std::size_t count)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToSigned<decltype(df)> di32;
    const hn::Rebind<std::uint8_t, decltype(df)> du8;
    const std::size_t lanes = hn::Lanes(df);
    const auto scale = hn::Set(df, kU8Max);
    const auto low = hn::Zero(df);
    const auto high = hn::Set(df, kU8Max);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto scaled = hn::Min(high, hn::Max(low, hn::Mul(hn::LoadU(df, src + i), scale)));
        const auto rounded = hn::ConvertTo(di32, hn::Round(scaled));
        hn::StoreU(hn::DemoteTo(du8, rounded), du8, dst + i);
    }
    for (; i < count; ++i)
    {
        const float scaled = src[i] * kU8Max;
        const float clamped = scaled < 0.0f ? 0.0f : (scaled > kU8Max ? kU8Max : scaled);
        dst[i] = static_cast<std::uint8_t>(std::nearbyintf(clamped));
    }
}

void brightnessContrast(const float* src, float* dst, std::size_t count, float brightness, float contrast)
{
    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);
    const auto pivot = hn::Set(df, kPivot);
    const auto gain = hn::Set(df, contrast);
    const auto offset = hn::Set(df, kPivot + brightness);
    const auto low = hn::Zero(df);
    const auto high = hn::Set(df, 1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto value = hn::MulAdd(hn::Sub(hn::LoadU(df, src + i), pivot), gain, offset);
        hn::StoreU(hn::Min(high, hn::Max(low, value)), df, dst + i);
    }
    for (; i < count; ++i)
    {
        const float value = (src[i] - kPivot) * contrast + kPivot + brightness;
        dst[i] = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    }
}

void mtf(const float* src, float* dst, std::size_t count, float midtones, float lo, float hi)
{
    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);
    const auto vLo = hn::Set(df, lo);
    const auto vHi = hn::Set(df, hi);
    const auto vSpan = hn::Set(df, hi - lo);
    const auto vNum = hn::Set(df, midtones - 1.0f);
    const auto vDen = hn::Set(df, 2.0f * midtones - 1.0f);
    const auto vM = hn::Set(df, midtones);
    const auto zero = hn::Zero(df);
    const auto one = hn::Set(df, 1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto x = hn::LoadU(df, src + i);
        const auto xp = hn::Div(hn::Sub(x, vLo), vSpan);
        const auto curve = hn::Div(hn::Mul(vNum, xp), hn::Sub(hn::Mul(vDen, xp), vM));

        auto result = hn::IfThenElse(hn::Le(x, vLo), zero, curve);
        result = hn::IfThenElse(hn::Ge(x, vHi), one, result);
        hn::StoreU(result, df, dst + i);
    }
    for (; i < count; ++i)
    {
        const float x = src[i];
        if (x <= lo)
        {
            dst[i] = 0.0f;
            continue;
        }
        if (x >= hi)
        {
            dst[i] = 1.0f;
            continue;
        }
        const float xp = (x - lo) / (hi - lo);
        dst[i] = ((midtones - 1.0f) * xp) / (((2.0f * midtones - 1.0f) * xp) - midtones);
    }
}

void calibrate(const float* light, const float* dark, const float* flat, float* dst, std::size_t count, double factor)
{
    // Double lanes, so half the throughput of the float version by construction. That is the point:
    // the engine's calibration promotes to double and this prices it.
    const hn::ScalableTag<double> dd;
    const hn::Rebind<float, decltype(dd)> df;
    const std::size_t lanes = hn::Lanes(dd);
    const auto vFactor = hn::Set(dd, factor);
    const auto zero = hn::Zero(df);
    const auto nan = hn::Set(df, std::numeric_limits<float>::quiet_NaN());

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto vLight = hn::LoadU(df, light + i);
        const auto vDark = hn::LoadU(df, dark + i);
        const auto vFlat = hn::LoadU(df, flat + i);

        const auto wide = hn::Mul(
            vFactor, hn::Div(hn::Sub(hn::PromoteTo(dd, vLight), hn::PromoteTo(dd, vDark)), hn::PromoteTo(dd, vFlat)));
        const auto narrowed = hn::DemoteTo(df, wide);
        hn::StoreU(hn::IfThenElse(hn::Eq(vFlat, zero), nan, narrowed), df, dst + i);
    }
    for (; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        dst[i] = static_cast<float>(
            factor * ((static_cast<double>(light[i]) - static_cast<double>(dark[i])) / static_cast<double>(flat[i])));
    }
}

void calibrateFloat(const float* light, const float* dark, const float* flat, float* dst, std::size_t count,
                    float factor)
{
    const hn::ScalableTag<float> df;
    const std::size_t lanes = hn::Lanes(df);
    const auto vFactor = hn::Set(df, factor);
    const auto zero = hn::Zero(df);
    const auto nan = hn::Set(df, std::numeric_limits<float>::quiet_NaN());

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto vLight = hn::LoadU(df, light + i);
        const auto vDark = hn::LoadU(df, dark + i);
        const auto vFlat = hn::LoadU(df, flat + i);

        const auto value = hn::Mul(vFactor, hn::Div(hn::Sub(vLight, vDark), vFlat));
        hn::StoreU(hn::IfThenElse(hn::Eq(vFlat, zero), nan, value), df, dst + i);
    }
    for (; i < count; ++i)
    {
        if (flat[i] == 0.0f)
        {
            dst[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        dst[i] = factor * ((light[i] - dark[i]) / flat[i]);
    }
}

void u16BrightnessContrastFused(const std::uint16_t* src, std::uint16_t* dst, std::size_t count, float brightness,
                                float contrast)
{
    const hn::ScalableTag<float> df;
    const hn::RebindToUnsigned<decltype(df)> du32;
    const hn::RebindToSigned<decltype(df)> di32;
    const hn::Rebind<std::uint16_t, decltype(df)> du16;
    const std::size_t lanes = hn::Lanes(df);

    const auto inScale = hn::Set(df, kU16Scale);
    const auto outScale = hn::Set(df, kU16Max);
    const auto pivot = hn::Set(df, kPivot);
    const auto gain = hn::Set(df, contrast);
    const auto bias = hn::Set(df, kPivot + brightness);
    const auto low = hn::Zero(df);
    const auto high = hn::Set(df, 1.0f);

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        const auto widened = hn::PromoteTo(du32, hn::LoadU(du16, src + i));
        const auto x = hn::Mul(hn::ConvertTo(df, widened), inScale);
        const auto value = hn::Min(high, hn::Max(low, hn::MulAdd(hn::Sub(x, pivot), gain, bias)));
        const auto rounded = hn::ConvertTo(di32, hn::Round(hn::Mul(value, outScale)));
        hn::StoreU(hn::DemoteTo(du16, rounded), du16, dst + i);
    }
    for (; i < count; ++i)
    {
        const float x = static_cast<float>(src[i]) * kU16Scale;
        float value = (x - kPivot) * contrast + kPivot + brightness;
        value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        dst[i] = static_cast<std::uint16_t>(std::nearbyintf(value * kU16Max));
    }
}

void u16BrightnessContrastNative(const std::uint16_t* src, std::uint16_t* dst, std::size_t count, float brightness,
                                 float contrast)
{
    // Sixteen lanes per vector instead of eight, and the frame never becomes floats. The cost is in
    // the arithmetic: Q15 with a halved coefficient, saturating wherever it can overflow.
    const hn::ScalableTag<std::int16_t> d16;
    const hn::RebindToUnsigned<decltype(d16)> du16;
    const std::size_t lanes = hn::Lanes(d16);

    const long rawCoefficient = std::lroundf(contrast * 0.5f * 32768.0f);
    const int clampedCoefficient =
        static_cast<int>(rawCoefficient < -32768 ? -32768 : (rawCoefficient > 32767 ? 32767 : rawCoefficient));
    const auto coefficient = hn::Set(d16, static_cast<std::int16_t>(clampedCoefficient));

    // The brightness offset is added at half scale, inside the halved-coefficient domain, so the
    // whole expression saturates exactly once at the end. Saturating the contrast product first and
    // adding brightness afterwards - the obvious order - clips a shadow to zero and then lifts it
    // back up, which disagrees with the float path by the full offset. Fixed point rewards reading
    // the order twice.
    const long rawOffset = std::lroundf(brightness * kU16Max);
    const long halfOffset = std::lroundf(brightness * kU16Max * 0.5f);
    const auto sign = hn::Set(du16, static_cast<std::uint16_t>(0x8000));
    const auto offset = hn::Set(d16, static_cast<std::int16_t>(halfOffset));

    std::size_t i = 0;
    for (; i + lanes <= count; i += lanes)
    {
        // XOR with the sign bit is exactly "subtract 32768" once reinterpreted as signed.
        const auto centred = hn::BitCast(d16, hn::Xor(hn::LoadU(du16, src + i), sign));
        const auto scaled = hn::SaturatedAdd(hn::MulFixedPoint15(centred, coefficient), offset);
        const auto doubled = hn::SaturatedAdd(scaled, scaled);
        hn::StoreU(hn::Xor(hn::BitCast(du16, doubled), sign), du16, dst + i);
    }
    for (; i < count; ++i)
    {
        const int c = static_cast<int>(src[i]) - 32768;
        int v = (c * clampedCoefficient + 16384) >> 15;
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        v += static_cast<int>(halfOffset);
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        int d = v + v;
        d = d < -32768 ? -32768 : (d > 32767 ? 32767 : d);
        dst[i] = static_cast<std::uint16_t>(d + 32768);
    }
    (void)rawOffset;
}

Reduction u16Reduce(const std::uint16_t* src, std::size_t count)
{
    Reduction result;
    if (count == 0)
    {
        return result;
    }

    const hn::ScalableTag<std::uint16_t> du16;
    const hn::Repartition<std::uint32_t, decltype(du16)> du32;
    const std::size_t lanes = hn::Lanes(du16);

    auto minimum = hn::Set(du16, src[0]);
    auto maximum = hn::Set(du16, src[0]);
    std::uint64_t total = 0;

    // The accumulator has to be widened and drained. Sixteen-bit lanes overflow after two
    // iterations, and even a 32-bit lane sum across a whole frame exceeds its range - so the u32
    // accumulator is drained to a 64-bit total every 2048 vectors, which is the largest block that
    // cannot overflow the horizontal sum.
    constexpr std::size_t kBlock = 2048;

    std::size_t i = 0;
    while (i + lanes <= count)
    {
        const std::size_t blockEnd = (std::min)(count, i + lanes * kBlock);
        auto accumulator = hn::Zero(du32);
        for (; i + lanes <= blockEnd; i += lanes)
        {
            const auto value = hn::LoadU(du16, src + i);
            minimum = hn::Min(minimum, value);
            maximum = hn::Max(maximum, value);
            accumulator = hn::Add(accumulator, hn::PromoteLowerTo(du32, value));
            accumulator = hn::Add(accumulator, hn::PromoteUpperTo(du32, value));
        }
        total += hn::ReduceSum(du32, accumulator);
    }

    std::uint16_t lowest = hn::ReduceMin(du16, minimum);
    std::uint16_t highest = hn::ReduceMax(du16, maximum);
    for (; i < count; ++i)
    {
        total += src[i];
        lowest = src[i] < lowest ? src[i] : lowest;
        highest = src[i] > highest ? src[i] : highest;
    }

    result.sum = static_cast<double>(total) * static_cast<double>(kU16Scale);
    result.minimum = static_cast<float>(lowest) * kU16Scale;
    result.maximum = static_cast<float>(highest) * kU16Scale;
    return result;
}
