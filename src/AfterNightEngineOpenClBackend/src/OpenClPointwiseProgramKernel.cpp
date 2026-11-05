// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

// AfterNightEngineOpenClBackend - a fused pointwise program, lowered into one OpenCL kernel. See
// OpenClPointwiseProgramKernel.h.

#include "OpenClPointwiseProgramKernel.h"

#include "OpenClRuntime.h"

#include <AfterNightEngine/Algorithms/Kernel/MathKernel.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseOp.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgram.h>
#include <AfterNightEngine/Algorithms/Pointwise/PointwiseProgramKernel.h>
#include <AfterNightEngine/Core/ErrorCode.h>
#include <AfterNightEngine/Core/ImageLayout.h>
#include <AfterNightEngine/Image/ChannelSemantics.h>
#include <AfterNightEngine/Image/ComputePrecision.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace an::opencl
{

namespace
{

/// The bits the host executor writes for every not-a-number it narrows to Float32.
constexpr const char* kQuietNotANumberBits = "0x7FC00000u";

/// How a program is built: against OpenCL 1.2, so a later built-in does not compile.
constexpr const char* kBuildOptions = "-cl-std=CL1.2";

/// The functions a program with a sampled curve divides through.
///
/// The RTX 5080's OpenCL division of one Float32 by another is not the nearest quotient. Over a curve 1.75
/// wide it gave the product with the divisor's rounded reciprocal in 972 of 2,257 samples, whether the
/// divisor was a constant or an argument and whether the driver's optimizer was on or off, while every
/// multiply and add in the same kernels matched the evaluator bit for bit. So the curve's quotient is not
/// taken from the division: a double-precision quotient is only an estimate within one unit in the last
/// place, and the nearest Float32 is chosen from it and its two neighbours by their residuals `|a - c * b|`,
/// which double precision holds exactly because each product has at most 48 significant bits. An exact tie
/// goes to the even neighbour, and past the largest finite Float32 the quotient is infinite exactly when it
/// reaches the threshold IEEE 754 rounds to infinity from.
constexpr const char* kQuotientSource = R"CLC(
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

int isEvenFloat(float value)
{
    union
    {
        float real;
        uint bits;
    } view;
    view.real = value;
    return (view.bits & 1u) == 0u;
}

float nearerQuotient(double numerator, double divisor, float below, float above)
{
    const double belowProduct = (double)below * divisor;
    const double aboveProduct = (double)above * divisor;
    const double belowError = fabs(numerator - belowProduct);
    const double aboveError = fabs(numerator - aboveProduct);
    if (belowError < aboveError)
    {
        return below;
    }
    if (aboveError < belowError)
    {
        return above;
    }
    return isEvenFloat(below) ? below : above;
}

float nearestQuotient(float dividend, float divisor)
{
    if (!isfinite(dividend))
    {
        return dividend / divisor;
    }
    const double magnitude = fabs((double)dividend);
    const double wideDivisor = (double)divisor;
    float estimate = (float)(magnitude / wideDivisor);
    if (isinf(estimate))
    {
        estimate = FLT_MAX;
    }
    const float below = nextafter(estimate, 0.0f);
    const float above = nextafter(estimate, (float)INFINITY);
    float chosen = nearerQuotient(magnitude, wideDivisor, below, estimate);
    if (isinf(above))
    {
        const double threshold = 0x1.ffffffp+127 * wideDivisor;
        if (chosen == estimate && magnitude >= threshold)
        {
            chosen = (float)INFINITY;
        }
    }
    else
    {
        chosen = nearerQuotient(magnitude, wideDivisor, chosen, above);
    }
    return copysign(chosen, dividend);
}
)CLC";

/// Where one binding's samples sit, in Float32 elements: plane `c`, row `y`, sample `x` is at
/// `c * planeStride + y * rowStride + x`.
struct BindingPlacement
{
    std::uint64_t planeStride = 0u;
    std::uint64_t rowStride = 0u;
};

[[nodiscard]] std::optional<BindingPlacement> placementOf(const ImageLayout& layout) noexcept
{
    const std::optional<std::uint64_t> first = layout.rowByteOffset(0u, 0u);
    if (!first.has_value() || *first != 0u || layout.rowStrideBytes() % sizeof(float) != 0u ||
        layout.planeStrideBytes() % sizeof(float) != 0u)
    {
        return std::nullopt;
    }
    return BindingPlacement{.planeStride = layout.planeStrideBytes() / sizeof(float),
                            .rowStride = layout.rowStrideBytes() / sizeof(float)};
}

/// An exact OpenCL C spelling of one Float32 value: a hexadecimal constant, or an infinity by name.
[[nodiscard]] std::string floatLiteral(float value)
{
    if (std::isinf(value))
    {
        return value > 0.0f ? "(INFINITY)" : "(-INFINITY)";
    }
    std::array<char, 32u> digits{};
    const std::to_chars_result written =
        std::to_chars(digits.data(), digits.data() + digits.size(), std::fabs(value), std::chars_format::hex);
    std::string literal = std::signbit(value) ? "(-0x" : "(0x";
    literal.append(digits.data(), written.ptr);
    literal += "f)";
    return literal;
}

[[nodiscard]] std::string elementLiteral(std::uint64_t value)
{
    return "(ulong)" + std::to_string(value) + "u";
}

[[nodiscard]] Status notLowered(const std::string& what)
{
    return backendFailure(ErrorCode::UnsupportedBackend,
                          "this OpenCL device's pointwise program kernel does not lower " + what +
                              "; run the program on the host",
                          {StatusContextField::ofText(ContextKey::Parameter, what)});
}

/// Success when every step and property of the program is one this kernel lowers.
[[nodiscard]] Status checkLowered(const PointwiseProgram& program)
{
    if (program.sourceSampleType() != SampleType::Float32 || program.precision() != ComputePrecision::Float32)
    {
        return notLowered("a program that is not Float32 throughout");
    }
    if (program.contractionAllowed())
    {
        return notLowered("a contracted scale and bias");
    }
    for (const PointwiseOp& step : program.steps())
    {
        switch (step.kind)
        {
        case PointwiseOpKind::Scale:
        case PointwiseOpKind::Bias:
        case PointwiseOpKind::Clamp:
        case PointwiseOpKind::Gamma:
        case PointwiseOpKind::Mask:
        case PointwiseOpKind::Blend:
            break;
        case PointwiseOpKind::SampledCurve:
            if (step.sampling == PointwiseCurveSampling::Lower)
            {
                return notLowered("a lower-neighbour curve");
            }
            if (step.domainPolicy == PointwiseCurveDomainPolicy::Reject)
            {
                return notLowered("a curve that rejects values outside its domain");
            }
            if (const float span = static_cast<float>(step.high) - static_cast<float>(step.low);
                !(span > 0.0f) || !std::isfinite(span))
            {
                return notLowered("a curve whose domain has no positive finite width in Float32");
            }
            break;
        case PointwiseOpKind::Unknown:
        case PointwiseOpKind::Lut:
        case PointwiseOpKind::ChannelMatrix:
        case PointwiseOpKind::Add:
        case PointwiseOpKind::Subtract:
        case PointwiseOpKind::Multiply:
        case PointwiseOpKind::Divide:
        case PointwiseOpKind::Minimum:
        case PointwiseOpKind::Maximum:
        case PointwiseOpKind::Compare:
            return notLowered(std::string(pointwiseOpKindName(step.kind)) + " steps");
        }
    }
    return Status::success();
}

/// One sampled-curve step, statement for statement as the evaluator computes it.
void appendSampledCurve(std::string& source, const PointwiseProgram& program, const PointwiseOp& step)
{
    const std::size_t entries = program.table(step.table).size();
    const std::string table = "kTable" + std::to_string(step.table);
    const auto low = static_cast<float>(step.low);
    const float span = static_cast<float>(step.high) - low;
    const auto last = static_cast<float>(entries - 1u);
    const std::string lastLiteral = floatLiteral(last);

    source += "        const float shifted = v - " + floatLiteral(low) + ";\n";
    source += "        const float ratio = nearestQuotient(shifted, " + floatLiteral(span) + ");\n";
    source += "        float position = ratio * " + lastLiteral + ";\n";
    source += "        if (isnan(position))\n        {\n            v = NAN;\n        }\n        else\n        {\n";
    if (step.domainPolicy == PointwiseCurveDomainPolicy::Clamp)
    {
        source += "            if (!(position >= 0.0f) || position > " + lastLiteral + ")\n            {\n";
        source += "                position = position < 0.0f ? 0.0f : " + lastLiteral + ";\n            }\n";
    }
    if (step.sampling == PointwiseCurveSampling::Nearest)
    {
        source += "            const float halfUp = position + 0.5f;\n";
        source += "            const float rounded = floor(halfUp);\n";
        source += "            const float lifted = rounded < 0.0f ? 0.0f : rounded;\n";
        source += "            const float bounded = " + lastLiteral + " < lifted ? " + lastLiteral + " : lifted;\n";
        source += "            v = " + table + "[(uint)bounded];\n";
    }
    else
    {
        source += "            uint lower = 0u;\n";
        source += "            if (position <= 0.0f)\n            {\n                lower = 0u;\n            }\n";
        source += "            else if (position >= " + lastLiteral +
                  ")\n            {\n                lower = " + std::to_string(entries - 2u) + "u;\n            }\n";
        source += "            else\n            {\n                lower = (uint)floor(position);\n            }\n";
        source += "            const float fraction = position - (float)lower;\n";
        source += "            const float first = " + table + "[lower];\n";
        source += "            const float second = " + table + "[lower + 1u];\n";
        source += "            if (first == second)\n            {\n                v = first;\n            }\n";
        source += "            else\n            {\n                const float rise = second - first;\n";
        source += "                const float moved = rise * fraction;\n                v = first + moved;\n";
        source += "            }\n";
    }
    source += "        }\n";
}

/// The OpenCL C source of one program over its bindings' placements.
[[nodiscard]] std::string programSource(const PointwiseProgram& program, std::span<const BindingPlacement> placements)
{
    const std::uint32_t sources = program.sourceCount();
    std::string source = "#pragma OPENCL FP_CONTRACT OFF\n";
    if (std::ranges::any_of(program.steps(),
                            [](const PointwiseOp& step) { return step.kind == PointwiseOpKind::SampledCurve; }))
    {
        source += kQuotientSource;
    }
    source += "\n";

    for (std::uint32_t index = 0u; index < program.tableCount(); ++index)
    {
        const std::span<const double> entries = program.table(index);
        source += "constant float kTable" + std::to_string(index) + "[" + std::to_string(entries.size()) + "] = {";
        for (std::size_t entry = 0u; entry < entries.size(); ++entry)
        {
            source += entry % 4u == 0u ? "\n    " : " ";
            source += floatLiteral(static_cast<float>(entries[entry]));
            source += ",";
        }
        source += "\n};\n\n";
    }

    source += "kernel void pointwiseProgram(";
    for (std::uint32_t index = 0u; index < sources; ++index)
    {
        source += "global const float* source" + std::to_string(index) + ", ";
    }
    source += "global float* destination)\n{\n";
    source += "    const ulong x = get_global_id(0);\n";
    source += "    const ulong y = get_global_id(1);\n";
    source += "    const ulong c = get_global_id(2);\n";

    std::array<bool, kMaxPointwiseSources> read{};
    read[0] = true;
    for (const PointwiseOp& step : program.steps())
    {
        if (step.kind == PointwiseOpKind::Mask || step.kind == PointwiseOpKind::Blend)
        {
            read[step.operand] = true;
        }
    }
    for (std::uint32_t index = 0u; index < sources; ++index)
    {
        if (read[index])
        {
            const std::string name = std::to_string(index);
            source += "    const float s" + name + " = source" + name + "[c * " +
                      elementLiteral(placements[index].planeStride) + " + y * " +
                      elementLiteral(placements[index].rowStride) + " + x];\n";
        }
    }
    source += "    float v = s0;\n";

    for (std::size_t index = 0u; index < program.steps().size(); ++index)
    {
        const PointwiseOp& step = program.steps()[index];
        source += "    {\n";
        switch (step.kind)
        {
        case PointwiseOpKind::Scale:
            source += "        v = v * " + floatLiteral(static_cast<float>(step.factor)) + ";\n";
            break;
        case PointwiseOpKind::Bias:
            source += "        v = v + " + floatLiteral(static_cast<float>(step.offset)) + ";\n";
            break;
        case PointwiseOpKind::Clamp: {
            // Two one-sided comparisons, so a not-a-number satisfies neither and stays.
            const std::string low = floatLiteral(static_cast<float>(step.low));
            const std::string high = floatLiteral(static_cast<float>(step.high));
            source += "        if (v < " + low + ")\n        {\n            v = " + low + ";\n        }\n";
            source += "        else if (v > " + high + ")\n        {\n            v = " + high + ";\n        }\n";
            break;
        }
        case PointwiseOpKind::Gamma:
            source += "        v = pow(v, " + floatLiteral(static_cast<float>(step.factor)) + ");\n";
            break;
        case PointwiseOpKind::Mask:
            source += "        v = v * s" + std::to_string(step.operand) + ";\n";
            break;
        case PointwiseOpKind::Blend: {
            const auto weight = static_cast<float>(step.factor);
            const float kept = 1.0f - weight;
            source += "        const float kept = v * " + floatLiteral(kept) + ";\n";
            source +=
                "        const float moved = s" + std::to_string(step.operand) + " * " + floatLiteral(weight) + ";\n";
            source += "        v = kept + moved;\n";
            break;
        }
        case PointwiseOpKind::SampledCurve:
            appendSampledCurve(source, program, step);
            break;
        case PointwiseOpKind::Unknown:
        case PointwiseOpKind::Lut:
        case PointwiseOpKind::ChannelMatrix:
        case PointwiseOpKind::Add:
        case PointwiseOpKind::Subtract:
        case PointwiseOpKind::Multiply:
        case PointwiseOpKind::Divide:
        case PointwiseOpKind::Minimum:
        case PointwiseOpKind::Maximum:
        case PointwiseOpKind::Compare:
            // Refused before the source is written.
            break;
        }
        source += "    }\n";
    }

    const BindingPlacement& destination = placements[sources];
    source += "    const ulong at = c * " + elementLiteral(destination.planeStride) + " + y * " +
              elementLiteral(destination.rowStride) + " + x;\n";
    source += "    if (isnan(v))\n    {\n        ((global uint*)destination)[at] = ";
    source += kQuietNotANumberBits;
    source += ";\n    }\n    else\n    {\n        destination[at] = v;\n    }\n}\n";
    return source;
}

} // namespace

std::vector<KernelContract> pointwiseProgramContracts()
{
    constexpr std::array kDomains{NumericDomain::NormalizedLinear, NumericDomain::EncodedNonlinear,
                                  NumericDomain::RawSensorCode, NumericDomain::PhysicalScaled};
    std::vector<KernelContract> contracts;
    for (const NumericDomain input : kDomains)
    {
        for (const NumericDomain output : kDomains)
        {
            contracts.push_back(pointwiseProgramKernelContract(
                PointwiseProgramKernelShape{.inputDomain = input, .outputDomain = output}));
            // A power step is built over these two input domains only.
            if (input == NumericDomain::NormalizedLinear || input == NumericDomain::EncodedNonlinear)
            {
                contracts.push_back(pointwiseProgramKernelContract(
                    PointwiseProgramKernelShape{.inputDomain = input,
                                                .outputDomain = output,
                                                .accuracy = mathFunctionAccuracy(MathFunction::Pow)}));
            }
        }
    }
    return contracts;
}

namespace
{

/// The device memory a lowered program's tables occupy: one Float32 per entry, in the constant arrays
/// `programSource` declares.
[[nodiscard]] std::uint64_t pointwiseProgramTableBytes(const PointwiseProgram& program) noexcept
{
    std::uint64_t bytes = 0u;
    for (std::uint32_t index = 0u; index < program.tableCount(); ++index)
    {
        bytes += std::uint64_t{program.table(index).size()} * sizeof(float);
    }
    return bytes;
}

} // namespace

PointwiseProgramKernel::PointwiseProgramKernel(std::shared_ptr<BackendCore> core, cl_program program, cl_kernel handle,
                                               const KernelPlanKey& plan,
                                               std::vector<DeviceBindingRequirement> bindings, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t channels,
                                               std::uint64_t tableBytes) noexcept
    : m_core(std::move(core)), m_program(program), m_handle(handle), m_plan(plan), m_bindings(std::move(bindings)),
      m_width(width), m_height(height), m_channels(channels), m_tableBytes(tableBytes)
{
}

PointwiseProgramKernel::~PointwiseProgramKernel()
{
    if (m_handle != nullptr)
    {
        (void)clReleaseKernel(m_handle);
    }
    if (m_program != nullptr)
    {
        (void)clReleaseProgram(m_program);
    }
}

KernelPlanKey PointwiseProgramKernel::plan() const noexcept
{
    return m_plan;
}

std::span<const DeviceBindingRequirement> PointwiseProgramKernel::bindings() const noexcept
{
    return m_bindings;
}

std::uint64_t PointwiseProgramKernel::scratchBytes() const noexcept
{
    return 0u;
}

std::uint64_t PointwiseProgramKernel::residentBytes() const noexcept
{
    return m_tableBytes;
}

Result<std::uint64_t> PointwiseProgramKernel::run(std::span<const DeviceBindingStep> bindings,
                                                  std::optional<DeviceAllocation> scratch) const
{
    (void)scratch;
    if (bindings.size() != m_bindings.size())
    {
        return backendFailure(ErrorCode::Internal,
                              "the pointwise program kernel was handed a binding count it did not declare");
    }
    for (std::size_t index = 0u; index < bindings.size(); ++index)
    {
        const std::optional<Allocation> allocation = m_core->findAllocation(bindings[index].allocation);
        if (!allocation.has_value() || allocation->buffer == nullptr)
        {
            return backendFailure(ErrorCode::Internal,
                                  "the pointwise program kernel was bound to memory this device holds no buffer for");
        }
        const cl_mem buffer = allocation->buffer;
        const cl_int status = clSetKernelArg(m_handle, static_cast<cl_uint>(index), sizeof(cl_mem), &buffer);
        if (status != CL_SUCCESS)
        {
            return callFailure(ErrorCode::ExternalFailure, "clSetKernelArg", status);
        }
    }

    const Clock::time_point started = Clock::now();
    const std::array<std::size_t, 3u> global{m_width, m_height, m_channels};
    cl_event event = nullptr;
    const cl_int status =
        clEnqueueNDRangeKernel(m_core->workQueue, m_handle, 3u, nullptr, global.data(), nullptr, 0u, nullptr, &event);
    if (status != CL_SUCCESS)
    {
        return callFailure(ErrorCode::ExternalFailure, "clEnqueueNDRangeKernel", status);
    }
    return finishCommand(event, "clEnqueueNDRangeKernel", started);
}

Result<std::shared_ptr<const DeviceKernel>> preparePointwiseProgramKernel(const std::shared_ptr<BackendCore>& core,
                                                                          const DeviceKernelRequest& request)
{
    const Status checked = checkPointwiseProgramKernelRequest(request);
    if (checked.failed())
    {
        return checked;
    }
    const PointwiseProgram& program = *pointwiseProgramOf(request.arguments);
    const Status lowered = checkLowered(program);
    if (lowered.failed())
    {
        return lowered;
    }

    std::vector<BindingPlacement> placements;
    std::vector<DeviceBindingRequirement> bindings;
    for (std::size_t index = 0u; index < request.bindingLayouts.size(); ++index)
    {
        const ImageLayout& layout = request.bindingLayouts[index];
        const std::optional<BindingPlacement> placed = placementOf(layout);
        if (!placed.has_value())
        {
            return backendFailure(ErrorCode::InvalidArgument,
                                  "the pointwise program kernel addresses layouts whose strides are whole Float32 "
                                  "samples",
                                  {StatusContextField::ofText(ContextKey::Parameter, "bindingLayouts")});
        }
        placements.push_back(*placed);
        bindings.push_back(DeviceBindingRequirement{
            .access = index + 1u < request.bindingLayouts.size() ? DeviceAccess::Read : DeviceAccess::Write,
            .minimumBytes = layout.totalBytes(),
            .alignment = alignof(float),
            .layout = layout});
    }
    const std::string source = programSource(program, placements);

    cl_int status = CL_SUCCESS;
    const char* text = source.c_str();
    const std::size_t length = source.size();
    const cl_program built = clCreateProgramWithSource(core->context, 1u, &text, &length, &status);
    if (status != CL_SUCCESS || built == nullptr)
    {
        return callFailure(ErrorCode::ExternalFailure, "clCreateProgramWithSource", status);
    }
    status = clBuildProgram(built, 1u, &core->device, kBuildOptions, nullptr, nullptr);
    if (status != CL_SUCCESS)
    {
        std::string log = programBuildLog(built, core->device);
        (void)clReleaseProgram(built);
        return backendFailure(ErrorCode::ExternalFailure, "the pointwise program kernel did not build: " + log,
                              {StatusContextField::ofNumber(ContextKey::LibraryCode, status)});
    }
    const cl_kernel handle = clCreateKernel(built, "pointwiseProgram", &status);
    if (status != CL_SUCCESS || handle == nullptr)
    {
        (void)clReleaseProgram(built);
        return callFailure(ErrorCode::ExternalFailure, "clCreateKernel", status);
    }

    const ImageLayout& destination = request.bindingLayouts.back();
    try
    {
        return std::shared_ptr<const DeviceKernel>(std::make_shared<const PointwiseProgramKernel>(
            core, built, handle, request.plan, std::move(bindings), destination.extent().width(),
            destination.extent().height(), program.channelCount(), pointwiseProgramTableBytes(program)));
    }
    catch (...)
    {
        (void)clReleaseKernel(handle);
        (void)clReleaseProgram(built);
        throw;
    }
}

} // namespace an::opencl
