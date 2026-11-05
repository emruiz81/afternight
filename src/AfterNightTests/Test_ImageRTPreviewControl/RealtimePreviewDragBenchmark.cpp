// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The real-time preview's drag benchmarks, run only when asked for: the brightness and contrast dialog over a frame,
/// its preview on, its brightness slider pressed, dragged a step at a time at a steady rate and released; the gamma
/// correction's, the LCh adjustment's, the Gaussian blur's and the median filter's sliders dragged the same way; the
/// histogram transformation's midtones and a point of the curves transformation's curve, which no slider drives,
/// stepped at the same rate - and what each drag cost reported: the
/// previews shown per second and by kind of frame, how long they took and how long each spent in its one operation and
/// waiting to be admitted, how many ran over what the view paints, how their histograms were made, what they converted
/// and cut of what they read and what they read of what earlier frames had, the GUI thread's pump lengths, the
/// processor time, the page faults and the memory, and what the preview view's presentations cost.
///
/// It runs when AN_BENCH_IMAGE names a frame: a file the viewer opens, read as the viewer reads it, a camera RAW frame
/// demosaiced by RCD; or `synthetic`, a 6000 by 4000 sixteen-bit colour frame of a starry sky the case makes. The
/// other knobs, each optional:
///
///   AN_BENCH_AUTOSTRETCH   1 shows the preview in autostretch; 0, the default, linear
///   AN_BENCH_ZOOM100       1 zooms the preview view to 1:1 before the drag; 0, the default, fits it to the window
///   AN_BENCH_HISTOGRAM     0 hides the dialog's histogram before the drag; 1, the default, shows it
///   AN_BENCH_MASK          1 assigns a Float32 grey gradient mask to the frame, 2 a UInt16 one; 0, the default, none
///   AN_BENCH_CHANNEL       the channel selector's entry the dialog runs on, by its label - "CIE L* (Lightness)" -
///                          the whole image when not set
///   AN_BENCH_AREA          1 turns the preview area on, at the size the dialog opens it with; 0, the default, off,
///                          a dialog that opens with one included
///   AN_BENCH_APPLY         1 applies the dialog once the drag has settled and reports whether the settled preview's
///                          samples are the Apply's - within the preview area when one is on, since outside it the
///                          preview shows the frame itself; 0, the default, applies nothing
///   AN_BENCH_STEPS         the slider's steps, 60 by default
///   AN_BENCH_STEP_MS       the milliseconds between steps, 16 by default
///   AN_BENCH_FPS           the previews a second the Processing settings state; the product's default, thirty, when
///                          not set
///   AN_BENCH_WIDTH         the session's width; the host's by default
///   AN_BENCH_TRACE         a file the case appends its results to as one tab-separated row, the header first when
///                          the file is new
///
/// The report is printed and recorded as the case's properties; the case asserts only that the drag ran and the
/// dialog went idle after it. A second case presents the frame in a standard view, fitted and at 1:1, linear and
/// stretched, bare and under a multiply mask, and pans it. docs/dev/guides/BENCHMARKS.md says how to run them.

#include "Test_ImageRTPreviewControl/ProcessDialogSupport.h"

#include "app/FilePolicies.h"
#include "controls/ImageViewFiles.h"
#include "controls/settings/ProcessingLimits.h"
#include "controls/settings/SettingsKeys.h"
#include "processes/AdjustBrightnessAndContrastProcess.h"
#include "processes/AdjustLChProcess.h"
#include "processes/CurvesTransformationProcess.h"
#include "processes/GammaCorrectionProcess.h"
#include "processes/GaussianBlurProcess.h"
#include "processes/HistogramTransformationProcess.h"
#include "processes/MedianFilterProcess.h"
#include "widgets/CurvesWidget.h"
#include "widgets/ImageViewWidget.h"
#include "widgets/ToneCurveModel.h"

#include <AfterNightEngine/Algorithms/Demosaic/Demosaic.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QScrollBar>
#include <QSlider>
#include <QToolButton>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <thread>
#include <memory>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <ctime>
#endif

namespace
{

using namespace an::tests::processes;
using namespace an::tests::processes::dialogs;

// ---- What the process spent, from the operating system.

/// The process's processor time so far, user and kernel, in milliseconds.
[[nodiscard]] double processorMilliseconds()
{
#if defined(_WIN32)
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto ticks = [](const FILETIME& value) {
        ULARGE_INTEGER combined{};
        combined.LowPart = value.dwLowDateTime;
        combined.HighPart = value.dwHighDateTime;
        return combined.QuadPart;
    };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.0;
#else
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(value.tv_sec) * 1000.0 + static_cast<double>(value.tv_nsec) / 1000000.0;
#endif
}

/// The process's page faults so far and its memory now.
struct ProcessMemory
{
    /// Page faults so far, soft and hard.
    double pageFaults = 0.0;

    /// The working set now and at its peak, and the private bytes now, in MiB.
    double workingSetMiB = 0.0;
    double peakWorkingSetMiB = 0.0;
    double privateMiB = 0.0;
};

[[nodiscard]] ProcessMemory processMemory()
{
    constexpr double kMiB = 1024.0 * 1024.0;
    ProcessMemory memory;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters)) != 0)
    {
        memory.pageFaults = static_cast<double>(counters.PageFaultCount);
        memory.workingSetMiB = static_cast<double>(counters.WorkingSetSize) / kMiB;
        memory.peakWorkingSetMiB = static_cast<double>(counters.PeakWorkingSetSize) / kMiB;
        memory.privateMiB = static_cast<double>(counters.PrivateUsage) / kMiB;
    }
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        memory.pageFaults = static_cast<double>(usage.ru_minflt) + static_cast<double>(usage.ru_majflt);
    }
    // The resident and peak resident sets, and the anonymous resident pages as what the process alone holds.
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        const auto kibibytes = [&line]() {
            std::istringstream fields(line.substr(line.find(':') + 1u));
            double value = 0.0;
            fields >> value;
            return value * 1024.0 / kMiB;
        };
        if (line.rfind("VmRSS:", 0) == 0)
        {
            memory.workingSetMiB = kibibytes();
        }
        else if (line.rfind("VmHWM:", 0) == 0)
        {
            memory.peakWorkingSetMiB = kibibytes();
        }
        else if (line.rfind("RssAnon:", 0) == 0)
        {
            memory.privateMiB = kibibytes();
        }
    }
#endif
    return memory;
}

/// Milliseconds on the steady clock.
[[nodiscard]] double nowMilliseconds()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// A duration in milliseconds.
[[nodiscard]] double milliseconds(std::chrono::nanoseconds duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

/// A whole-number knob, or its default when it is not set or not a number.
[[nodiscard]] int knob(const char* name, int fallback)
{
    bool parsed = false;
    const int value = qEnvironmentVariableIntValue(name, &parsed);
    return parsed ? value : fallback;
}

// ---- The frame.

/// A 64-bit mix of three coordinates, for the synthetic frame's noise and stars.
[[nodiscard]] std::uint64_t mixed(std::uint64_t a, std::uint64_t b, std::uint64_t c)
{
    std::uint64_t value = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull ^
                          (c + 0x85EBCA77C2B2AE63ull) * 0x94D049BB133111EBull;
    value ^= value >> 31u;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 29u;
    return value;
}

/// One sample of the synthetic frame in sixteen-bit codes, before any rounding: a sky of about 2,000 codes on a gentle
/// gradient, noise of about 120 codes, and a star in most cells of a 160-pixel grid, each channel at a gain of its own.
[[nodiscard]] double syntheticCodes(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    constexpr double kGains[3] = {1.0, 1.08, 0.93};
    constexpr std::uint32_t kCell = 160u;
    double value = 2000.0 + 0.05 * static_cast<double>(x) + 0.03 * static_cast<double>(y);
    const std::uint64_t noise = mixed(x, y, channel);
    const double first = static_cast<double>(noise & 0xFFFFFFFFull) / 4294967296.0;
    const double second = static_cast<double>(noise >> 32u) / 4294967296.0;
    value += 294.0 * (first + second - 1.0);
    const std::uint32_t cellX = x / kCell;
    const std::uint32_t cellY = y / kCell;
    const std::uint64_t star = mixed(cellX, cellY, 7u);
    if ((star & 3u) != 0u)
    {
        const double centreX =
            static_cast<double>(cellX * kCell + 20u + static_cast<std::uint32_t>((star >> 8u) % 120u));
        const double centreY =
            static_cast<double>(cellY * kCell + 20u + static_cast<std::uint32_t>((star >> 20u) % 120u));
        const double width = 1.2 + static_cast<double>((star >> 32u) % 100u) / 50.0;
        const double peak = 3000.0 + static_cast<double>((star >> 40u) % 40000u);
        const double dx = static_cast<double>(x) - centreX;
        const double dy = static_cast<double>(y) - centreY;
        const double spread = 1.0 + (dx * dx + dy * dy) / (width * width);
        value += peak / (spread * spread);
    }
    return value * kGains[channel];
}

/// One sample of the synthetic frame in a representation: the codes rounded for a sixteen-bit one, the same fraction of
/// full scale rounded to a 32-bit one's codes, and that fraction unrounded, as a stack holds it, for a floating one.
template <typename SampleT>
[[nodiscard]] SampleT syntheticSample(std::uint32_t x, std::uint32_t y, std::uint32_t channel)
{
    const double codes = syntheticCodes(x, y, channel);
    if constexpr (std::is_same_v<SampleT, std::uint16_t>)
    {
        return static_cast<std::uint16_t>(std::clamp(std::lround(codes), 0L, 65535L));
    }
    else if constexpr (std::is_floating_point_v<SampleT>)
    {
        return static_cast<SampleT>(codes / 65535.0);
    }
    else
    {
        const double top = static_cast<double>((std::numeric_limits<SampleT>::max)());
        return static_cast<SampleT>(std::clamp(std::round(codes / 65535.0 * top), 0.0, top));
    }
}

/// Whether a knob names a synthetic frame rather than a file.
[[nodiscard]] bool namesSyntheticFrame(const QString& named)
{
    return named == QStringLiteral("synthetic") || named.startsWith(QStringLiteral("synthetic:"));
}

/// The synthetic frame a knob names: `synthetic` is 6000 by 4000 sixteen-bit colour; `synthetic:<representation>` -
/// uint16, uint32, int32, float32 or float64 - makes it in that representation, and
/// `synthetic:<representation>:<width>x<height>` at that size too.
[[nodiscard]] std::optional<an::consumer::ImageAsset> syntheticFrame(const an::consumer::EngineSession& session,
                                                                     const QString& named)
{
    const QStringList parts = named.split(QLatin1Char(':'));
    const QString representation = parts.size() > 1 ? parts[1] : QStringLiteral("uint16");
    std::uint32_t width = 6000u;
    std::uint32_t height = 4000u;
    if (parts.size() > 2)
    {
        const QStringList size = parts[2].split(QLatin1Char('x'));
        bool wide = false;
        bool high = false;
        width = size.size() == 2 ? size[0].toUInt(&wide) : 0u;
        height = size.size() == 2 ? size[1].toUInt(&high) : 0u;
        EXPECT_TRUE(wide && high && width > 0u && height > 0u) << "not a size: " << parts[2].toStdString();
        if (!wide || !high || width == 0u || height == 0u)
        {
            return std::nullopt;
        }
    }
    const auto make = [&]<typename SampleT>() {
        return an::tests::viewer::makeAsset<SampleT>(session, width, height, an::tests::viewer::Channels::Rgb,
                                                     syntheticSample<SampleT>);
    };
    if (representation == QStringLiteral("uint16"))
    {
        return make.template operator()<std::uint16_t>();
    }
    if (representation == QStringLiteral("uint32"))
    {
        return make.template operator()<std::uint32_t>();
    }
    if (representation == QStringLiteral("int32"))
    {
        return make.template operator()<std::int32_t>();
    }
    if (representation == QStringLiteral("float32"))
    {
        return make.template operator()<float>();
    }
    if (representation == QStringLiteral("float64"))
    {
        return make.template operator()<double>();
    }
    ADD_FAILURE() << "no synthetic representation " << representation.toStdString();
    return std::nullopt;
}

/// The frame a knob names: read as the viewer reads it, or made.
[[nodiscard]] std::optional<an::consumer::ImageAsset> benchFrame(const an::consumer::EngineSession& session,
                                                                 const QString& named)
{
    if (namesSyntheticFrame(named))
    {
        return syntheticFrame(session, named);
    }
    an::Result<std::shared_ptr<const an::ui::RawOpenPolicy>> policy =
        an::ui::RawOpenPolicy::create(an::ui::RawOpenSettings{});
    EXPECT_TRUE(policy.hasValue()) << policy.status().message();
    if (!policy.hasValue())
    {
        return std::nullopt;
    }
    an::ui::ViewReadRequest request = (*policy)->readRequest(named);
    request.demosaic = an::DemosaicMethod::Rcd;
    an::Result<an::ui::ViewReadResult> read = an::ui::readViewImage(session, request, std::stop_token{}, nullptr);
    EXPECT_TRUE(read.hasValue()) << named.toStdString() << ": " << read.status().message();
    if (!read.hasValue())
    {
        return std::nullopt;
    }
    return std::move(read->asset);
}

/// A grey gradient mask of a frame's size, rising from the left edge to the right: Float32, or UInt16 for `kind` 2.
[[nodiscard]] std::optional<an::consumer::ImageAsset> benchMask(const an::consumer::EngineSession& session, int kind,
                                                                std::uint32_t width, std::uint32_t height)
{
    if (kind == 2)
    {
        return an::tests::viewer::makeAsset<std::uint16_t>(
            session, width, height, an::tests::viewer::Channels::Grey,
            [width](std::uint32_t x, std::uint32_t, std::uint32_t) {
                return static_cast<std::uint16_t>((static_cast<std::uint64_t>(x) * 65535u) / (width - 1u));
            });
    }
    return an::tests::viewer::makeAsset<float>(session, width, height, an::tests::viewer::Channels::Grey,
                                               [width](std::uint32_t x, std::uint32_t, std::uint32_t) {
                                                   return static_cast<float>(x) / static_cast<float>(width - 1u);
                                               });
}

// ---- The report.

/// The GUI thread's pumps during the drag: every slider step and every turn of the event loop between steps.
struct PumpCounts
{
    int pumps = 0;
    double longest = 0.0;
    int over4 = 0;
    int over8 = 0;
    int over16 = 0;
    int over33 = 0;

    /// The time spent in pumps over 16 ms, the ones a person sees as a stall.
    double blocked = 0.0;

    void note(double pumped)
    {
        ++pumps;
        longest = (std::max)(longest, pumped);
        over4 += pumped > 4.0 ? 1 : 0;
        over8 += pumped > 8.0 ? 1 : 0;
        over33 += pumped > 33.0 ? 1 : 0;
        if (pumped > 16.0)
        {
            ++over16;
            blocked += pumped;
        }
    }
};

/// One named result: its column in the trace and its value as text.
struct ReportField
{
    std::string name;
    std::string value;
};

/// A number with `decimals` places and a decimal point, whatever the locale: the QApplication sets the C library's
/// locale from the environment, so `printf` would write a decimal comma into the report and the trace on a host whose
/// numeric locale uses one.
[[nodiscard]] std::string fixed(double value, int decimals = 1)
{
    if (!std::isfinite(value))
    {
        return "nan";
    }
    char text[64];
    const std::to_chars_result written =
        std::to_chars(text, text + sizeof(text), value, std::chars_format::fixed, decimals);
    if (written.ec != std::errc{})
    {
        return "nan";
    }
    return std::string(text, written.ptr);
}

/// Appends one row to the trace file, the header first when the file is new.
void appendTrace(const std::string& path, const std::vector<ReportField>& fields)
{
    std::error_code ignored;
    const bool fresh = !std::filesystem::exists(path, ignored) || std::filesystem::file_size(path, ignored) == 0u;
    std::ofstream trace(path, std::ios::app | std::ios::binary);
    ASSERT_TRUE(trace.good()) << "cannot append to " << path;
    std::string header;
    std::string row;
    for (const ReportField& field : fields)
    {
        header += (header.empty() ? "" : "\t") + field.name;
        row += (row.empty() ? "" : "\t") + field.value;
    }
    if (fresh)
    {
        trace << header << "\r\n";
    }
    trace << row << "\r\n";
}

/// What a drag moves: the dialog it opens and the control it steps.
struct DragSubject
{
    /// What the report calls it.
    std::string name;

    /// Opens the dialog over the target.
    std::function<DialogHandle(ImageViewControl&)> open;

    /// Moves the control to a step of the drag, the first being one; false when the dialog has no such control.
    std::function<bool(QWidget&, int)> step;

    /// Presses the control before the drag and lets it go after, as a person drags a slider; nothing for a control
    /// that no slider drives.
    std::function<void(QWidget&, bool)> press;

    /// Whether letting the control go asks for a frame: false for a control no slider drives, whose last frame may
    /// have been shown before the drag ended.
    bool releases = true;
};

/// The brightness slider of the brightness and contrast dialog, from zero up a code a step.
[[nodiscard]] DragSubject brightnessDrag()
{
    return DragSubject{.name = "brightness-contrast",
                       .open =
                           [](ImageViewControl& target) {
                               return openDialog<AdjustBrightnessAndContrastProcess>(target, QSize(1400, 900));
                           },
                       .step =
                           [](QWidget& dialog, int step) {
                               auto* slider = child<QSlider>(dialog, "brightnessSlider");
                               if (slider == nullptr)
                               {
                                   return false;
                               }
                               slider->setValue(step);
                               return true;
                           },
                       .press =
                           [](QWidget& dialog, bool down) {
                               if (auto* slider = child<QSlider>(dialog, "brightnessSlider"); slider != nullptr)
                               {
                                   slider->setSliderDown(down);
                               }
                           }};
}

/// The gamma correction's slider, its value from one up a hundredth a step.
[[nodiscard]] DragSubject gammaDrag()
{
    return DragSubject{
        .name = "gamma-correction",
        .open = [](ImageViewControl& target) { return openDialog<GammaCorrectionProcess>(target, QSize(1400, 900)); },
        .step =
            [](QWidget& dialog, int step) {
                auto* gamma = child<QDoubleSpinBox>(dialog, "gammaValue");
                if (gamma == nullptr)
                {
                    return false;
                }
                gamma->setValue(1.0 + 0.01 * static_cast<double>(step));
                return true;
            },
        .press =
            [](QWidget& dialog, bool down) {
                if (auto* slider = child<QSlider>(dialog, "gammaSlider"); slider != nullptr)
                {
                    slider->setSliderDown(down);
                }
            }};
}

/// The lightness of the LCh adjustment, from zero up one a step: every pixel of what the preview view paints is taken
/// to CIE LCh and back through the exact conversions in every frame.
[[nodiscard]] DragSubject lchDrag()
{
    return DragSubject{
        .name = "adjust-lch",
        .open = [](ImageViewControl& target) { return openDialog<AdjustLChProcess>(target, QSize(1400, 900)); },
        .step =
            [](QWidget& dialog, int step) {
                auto* slider = child<QSlider>(dialog, "lightnessSlider");
                if (slider == nullptr)
                {
                    return false;
                }
                slider->setValue(step);
                return true;
            },
        .press =
            [](QWidget& dialog, bool down) {
                if (auto* slider = child<QSlider>(dialog, "lightnessSlider"); slider != nullptr)
                {
                    slider->setSliderDown(down);
                }
            }};
}

/// The sigma of the Gaussian blur, from two pixels up a tenth a step: a body that reads its neighbours, run over what
/// the preview view paints while the slider is held.
[[nodiscard]] DragSubject gaussianDrag()
{
    return DragSubject{
        .name = "gaussian-blur",
        .open = [](ImageViewControl& target) { return openDialog<GaussianBlurProcess>(target, QSize(1400, 900)); },
        .step =
            [](QWidget& dialog, int step) {
                auto* sigma = child<QDoubleSpinBox>(dialog, "sigmaValue");
                if (sigma == nullptr)
                {
                    return false;
                }
                sigma->setValue(2.0 + 0.1 * static_cast<double>(step));
                return true;
            },
        .press =
            [](QWidget& dialog, bool down) {
                if (auto* slider = child<QSlider>(dialog, "sigmaSlider"); slider != nullptr)
                {
                    slider->setSliderDown(down);
                }
            }};
}

/// The modulation of the median filter over its five by five window, from four tenths up a hundredth a step: a body
/// that reads the pixel grid at full scale, run over the part of the target the preview view shows while the slider is
/// held, and over the whole image where the view shows all of it.
[[nodiscard]] DragSubject medianDrag()
{
    return DragSubject{.name = "median-filter",
                       .open =
                           [](ImageViewControl& target) {
                               DialogHandle dialog = openDialog<MedianFilterProcess>(target, QSize(1400, 900));
                               if (auto* kernel = child<QComboBox>(*dialog, "kernelSize"); kernel != nullptr)
                               {
                                   kernel->setCurrentText(QStringLiteral("5x5"));
                               }
                               return dialog;
                           },
                       .step =
                           [](QWidget& dialog, int step) {
                               auto* modulation = child<QDoubleSpinBox>(dialog, "modulationValue");
                               if (modulation == nullptr)
                               {
                                   return false;
                               }
                               modulation->setValue(0.4 + 0.01 * static_cast<double>(step));
                               return true;
                           },
                       .press =
                           [](QWidget& dialog, bool down) {
                               if (auto* slider = child<QSlider>(dialog, "modulationSlider"); slider != nullptr)
                               {
                                   slider->setSliderDown(down);
                               }
                           }};
}

/// The midtones of the histogram transformation, from its half down a thousandth a step: the dialog stretches every
/// frame over the whole image, and no slider drives the value, so every frame is settled.
[[nodiscard]] DragSubject midtonesDrag()
{
    return DragSubject{.name = "histogram-transformation",
                       .open =
                           [](ImageViewControl& target) {
                               return openDialog<HistogramTransformationProcess>(target, QSize(1400, 900));
                           },
                       .step =
                           [](QWidget& dialog, int step) {
                               auto* midtones = child<QDoubleSpinBox>(dialog, "midtonesValue");
                               if (midtones == nullptr)
                               {
                                   return false;
                               }
                               midtones->setValue(0.5 - 0.001 * static_cast<double>(step));
                               return true;
                           },
                       .press = [](QWidget&, bool) {},
                       .releases = false};
}

/// A dialog's curve editor; its class declares no meta-object of its own, so it is found by its type.
[[nodiscard]] CurvesWidget* curvesWidgetOf(const QWidget& dialog)
{
    for (QWidget* widget : dialog.findChildren<QWidget*>())
    {
        if (auto* curves = dynamic_cast<CurvesWidget*>(widget))
        {
            return curves;
        }
    }
    return nullptr;
}

/// The middle point of the curves transformation's curve, from the identity up two thousandths a step: no slider drives
/// it, so every frame is settled, and the dialog runs every frame over the whole image.
[[nodiscard]] DragSubject curvesDrag()
{
    return DragSubject{
        .name = "curves-transformation",
        .open =
            [](ImageViewControl& target) { return openDialog<CurvesTransformationProcess>(target, QSize(1400, 900)); },
        .step =
            [](QWidget& dialog, int step) {
                CurvesWidget* curves = curvesWidgetOf(dialog);
                if (curves == nullptr)
                {
                    return false;
                }
                const double output = 0.5 + 0.002 * static_cast<double>(step);
                curves->setCurve(an::ui::ToneCurveModel({an::CurveControlPoint{.input = 0.0, .output = 0.0},
                                                         an::CurveControlPoint{.input = 0.5, .output = output},
                                                         an::CurveControlPoint{.input = 1.0, .output = 1.0}},
                                                        curves->getCurve().interpolation()));
                return true;
            },
        .press = [](QWidget&, bool) {},
        .releases = false};
}

/// A digest of the samples a document holds within a part of it, or within all of it when the part is empty, every
/// channel's in row order: two documents of one digest over one part hold the same samples there.
[[nodiscard]] std::uint64_t digestOf(const an::ui::ImageDocument& document, const QRect& part = QRect())
{
    an::Result<an::consumer::AssetReadLease> lease = document.read();
    if (!lease.hasValue())
    {
        return 0u;
    }
    std::uint64_t digest = 0xCBF29CE484222325ull;
    lease->visit([&digest, &part](const auto& typed) {
        const auto view = typed.view();
        const std::optional<an::ImageDescriptor> described = typed.descriptor();
        if (!view.has_value() || !described.has_value())
        {
            return;
        }
        const QRect whole(0, 0, static_cast<int>(described->extent().width()),
                          static_cast<int>(described->extent().height()));
        const QRect within = part.isEmpty() ? whole : part.intersected(whole);
        for (std::uint32_t channel = 0u; channel < described->extent().channelCount(); ++channel)
        {
            for (int y = within.top(); y <= within.bottom(); ++y)
            {
                const auto row = view->row(channel, static_cast<std::uint32_t>(y));
                if (!row.has_value() || row->empty())
                {
                    continue;
                }
                const std::size_t sampleBytes = row->size_bytes() / row->size();
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(row->data());
                const std::size_t end = static_cast<std::size_t>(within.right() + 1) * sampleBytes;
                for (std::size_t index = static_cast<std::size_t>(within.left()) * sampleBytes; index < end; ++index)
                {
                    digest = (digest ^ bytes[index]) * 0x100000001B3ull;
                }
            }
        }
    });
    return digest;
}

/// A digest as sixteen hexadecimal digits.
[[nodiscard]] std::string hexOf(std::uint64_t value)
{
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

/// Drags a subject over the frame AN_BENCH_IMAGE names, under the knobs this file opens with, and reports the drag.
void runDrag(const DragSubject& subject)
{
    const QString named = qEnvironmentVariable("AN_BENCH_IMAGE");
    if (named.isEmpty())
    {
        GTEST_SKIP() << "set AN_BENCH_IMAGE to a frame's path, or to synthetic, to run the drag benchmark";
    }
    const bool autostretch = knob("AN_BENCH_AUTOSTRETCH", 0) != 0;
    const bool zoom100 = knob("AN_BENCH_ZOOM100", 0) != 0;
    const bool histogram = knob("AN_BENCH_HISTOGRAM", 1) != 0;
    const int maskKind = knob("AN_BENCH_MASK", 0);
    const QString channel = qEnvironmentVariable("AN_BENCH_CHANNEL");
    const bool previewArea = knob("AN_BENCH_AREA", 0) != 0;
    const bool applyAfter = knob("AN_BENCH_APPLY", 0) != 0;
    const int steps = (std::max)(1, knob("AN_BENCH_STEPS", 60));
    const int stepMs = (std::max)(1, knob("AN_BENCH_STEP_MS", 16));
    const int frameRate = knob("AN_BENCH_FPS", 0);
    const int width = knob("AN_BENCH_WIDTH", 0);

    an::consumer::EngineSessionSettings settings;
    settings.codecRegistry = []() { return an::ui::createProductCodecRegistry(); };
    if (width > 0)
    {
        settings.maxConcurrency = static_cast<std::uint32_t>(width);
    }
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    auto context = std::make_unique<ProcessTestContext>(std::move(*opened));
    std::optional<InstalledContext> installed;
    installed.emplace(*context);
    const an::consumer::EngineSession& session = context->engineSession();
    if (frameRate > 0)
    {
        (void)context->upsert(QString::fromLatin1(SettingsKeys::PROCESSING_PREVIEW_FRAME_RATE),
                              QString::number(frameRate));
    }
    const std::uint32_t frameRateInUse = an::ui::readPreviewFrameRate(
        [&context](const QString& key, const QString& fallback) { return context->select(key, fallback); });

    std::optional<an::consumer::ImageAsset> frame = benchFrame(session, named);
    ASSERT_TRUE(frame.has_value());
    const std::optional<an::ImageDescriptor> described = frame->descriptor();
    ASSERT_TRUE(described.has_value());
    const std::uint32_t frameWidth = described->extent().width();
    const std::uint32_t frameHeight = described->extent().height();
    SceneView target = assetView(std::move(*frame), "target");
    ASSERT_TRUE(target);
    target->resize(1600, 1000);
    target->show();
    if (autostretch)
    {
        target->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
    }
    ASSERT_TRUE(settleView(*target));

    SceneView mask;
    if (maskKind != 0)
    {
        std::optional<an::consumer::ImageAsset> maskAsset = benchMask(session, maskKind, frameWidth, frameHeight);
        ASSERT_TRUE(maskAsset.has_value());
        mask = assetView(std::move(*maskAsset), "mask");
        ASSERT_TRUE(mask);
        target->setCurrentMask(mask.get());
        ASSERT_TRUE(target->isMasked() && target->isMaskEnabled());
        ASSERT_TRUE(settleView(*target));
    }

    DialogHandle dialog = subject.open(*target);
    dialog->show();
    auto* preview = child<QCheckBox>(*dialog, "preview");
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    // The dialog opens at its identity, which shows the frame itself and runs nothing.
    ASSERT_TRUE(pumpUntil([&dialog]() {
        const ProcessRunTelemetry shown = dialog->runTelemetry();
        return shown.previewsCommitted + shown.identityPreviews >= 1u;
    }));
    ASSERT_TRUE(settleDialog(*dialog));
    ImageViewControl* previewView = dialog->previewView();
    ASSERT_NE(previewView, nullptr);
    ImageViewWidget* previewWidget = previewView->getViewWidget();
    ASSERT_NE(previewWidget, nullptr);
    if (autostretch)
    {
        previewView->setViewMode(ImageViewMode::AUTOSTRETCH_MODE);
        ASSERT_TRUE(settleDialog(*dialog));
    }
    if (zoom100)
    {
        previewView->zoom100();
        ASSERT_TRUE(settleDialog(*dialog));
    }
    if (!histogram)
    {
        auto* showHistogram = child<QToolButton>(*dialog, "showHistogram");
        ASSERT_NE(showHistogram, nullptr);
        if (showHistogram->isChecked())
        {
            showHistogram->click();
        }
        ASSERT_TRUE(settleDialog(*dialog));
    }
    if (!channel.isEmpty())
    {
        // The dialog's own channel selector: the process dialogs' and the curves transformation's.
        QComboBox* selector = dialog->findChild<QComboBox*>(QStringLiteral("channelSelector"));
        if (selector == nullptr || selector->findText(channel) < 0)
        {
            selector = dialog->findChild<QComboBox*>(QStringLiteral("channelSelector2"));
        }
        ASSERT_NE(selector, nullptr) << subject.name << ": the dialog has no channel selector";
        const int index = selector->findText(channel);
        ASSERT_GE(index, 0) << subject.name << ": the channel selector offers no " << channel.toStdString();
        selector->setCurrentIndex(index);
        ASSERT_TRUE(settleDialog(*dialog));
    }
    if (previewArea)
    {
        QCheckBox* area = previewView->getRTPreviewAreaCheckBox();
        ASSERT_NE(area, nullptr) << subject.name << ": the dialog offers no preview area";
        area->setChecked(true);
        ASSERT_TRUE(settleDialog(*dialog));
        ASSERT_FALSE(dialog->previewArea().isEmpty());
    }
    else if (QCheckBox* area = previewView->getRTPreviewAreaCheckBox(); area != nullptr && area->isChecked())
    {
        // A dialog that opens with a preview area is dragged over the whole image, as one that opens with none is.
        area->setChecked(false);
        ASSERT_TRUE(settleDialog(*dialog));
        ASSERT_TRUE(dialog->previewArea().isEmpty());
    }
    ASSERT_TRUE(subject.step(*dialog, 0)) << subject.name << ": the dialog has no control to drag";
    ASSERT_TRUE(settleDialog(*dialog));
    // The part of the image the preview view shows: what a frame at full scale over what the view shows runs over.
    const std::optional<an::ui::ViewRegion> shownPart = previewWidget->shownImageRegion();

    const ProcessRunTelemetry runsBefore = dialog->runTelemetry();
    const ImageViewPresentationTelemetry presentedBefore = previewWidget->presentationTelemetry();
    const ProcessMemory memoryBefore = processMemory();
    const double processorStart = processorMilliseconds();

    // The first previews of a drag take memory that the ones after it reuse - the preview shown before the drag still
    // holds its result when the first is made - so the steady state is counted from the third preview shown on.
    constexpr std::uint64_t kWarmUpPreviews = 3u;
    std::optional<ProcessMemory> memoryAfterWarmUp;
    std::uint64_t shownInWarmUp = 0u;

    // The control is pressed for the whole drag and released after its last step, as a person drags a slider.
    PumpCounts pumps;
    QElapsedTimer wall;
    wall.start();
    subject.press(*dialog, true);
    for (int step = 1; step <= steps; ++step)
    {
        const double stepStart = nowMilliseconds();
        (void)subject.step(*dialog, step);
        pumps.note(nowMilliseconds() - stepStart);
        const qint64 until = static_cast<qint64>(step) * stepMs;
        while (wall.elapsed() < until)
        {
            const double pumpStart = nowMilliseconds();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            pumps.note(nowMilliseconds() - pumpStart);
            const std::uint64_t shown = dialog->runTelemetry().previewsCommitted - runsBefore.previewsCommitted;
            if (!memoryAfterWarmUp.has_value() && shown >= kWarmUpPreviews)
            {
                memoryAfterWarmUp = processMemory();
                shownInWarmUp = shown;
            }
            // A turn of the loop with nothing to serve returns at once; sleeping between turns keeps the harness's own
            // thread from counting as a core busy with the drag, and from competing with the frames' workers.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const ProcessRunTelemetry runsAtDragEnd = dialog->runTelemetry();
    const double releasedAt = nowMilliseconds();
    subject.press(*dialog, false);
    const double dragMs = static_cast<double>(wall.elapsed());
    const double processorAtDragEnd = processorMilliseconds();
    const ProcessMemory memoryAtDragEnd = processMemory();
    const ImageViewPresentationTelemetry presentedAtDragEnd = previewWidget->presentationTelemetry();

    // The release asks for the settled frame: when it was handed to the preview view, and when the view had presented
    // it. A control no slider drives asks for nothing when it is let go, and the frame of its last step may have been
    // shown before the drag ended.
    const auto settledAfterDrag = [&dialog, &runsAtDragEnd]() {
        return dialog->runTelemetry().settledPreviewsCommitted > runsAtDragEnd.settledPreviewsCommitted;
    };
    const bool settledShown =
        pumpUntil([&]() { return settledAfterDrag() || (!subject.releases && dialog->isProcessIdle()); }) &&
        settledAfterDrag();
    const double settledCommittedMs = nowMilliseconds() - releasedAt;
    (void)pumpUntil([previewWidget]() { return previewWidget->isPresentationIdle(); });
    const double settledPresentedMs = nowMilliseconds() - releasedAt;
    EXPECT_TRUE(settledShown || !subject.releases) << "a settled frame followed the release";

    EXPECT_TRUE(settleDialog(*dialog)) << "the dialog goes idle after the drag";
    const double settledMs = static_cast<double>(wall.elapsed());
    const double processorEnd = processorMilliseconds();
    const ProcessMemory memoryEnd = processMemory();
    const ProcessRunTelemetry runsEnd = dialog->runTelemetry();
    const ImageViewPresentationTelemetry presentedEnd = previewWidget->presentationTelemetry();
    EXPECT_GT(runsAtDragEnd.previewsCommitted, runsBefore.previewsCommitted) << "the drag showed previews";
    const QRect areaShown = dialog->previewArea();

    // The settled preview's samples, and the Apply's, which runs the production path over the real image; within the
    // preview area when one is on, outside which the preview shows the frame itself.
    const std::uint64_t settledDigest = digestOf(previewView->document(), areaShown);
    std::optional<std::uint64_t> appliedDigest;
    if (applyAfter)
    {
        dialog->applyProcess();
        EXPECT_TRUE(settleDialog(*dialog)) << "the dialog goes idle after the Apply";
        EXPECT_TRUE(settleView(*target));
        appliedDigest = digestOf(target->document(), areaShown);
    }

    // During the drag.
    const double dragSeconds = dragMs / 1000.0;
    const auto shownInDrag = static_cast<double>(runsAtDragEnd.previewsCommitted - runsBefore.previewsCommitted);
    const double processorInDrag = processorAtDragEnd - processorStart;
    const double faultsInDrag = memoryAtDragEnd.pageFaults - memoryBefore.pageFaults;
    const double frameTimeInDrag = milliseconds(runsAtDragEnd.previewTime - runsBefore.previewTime);
    const auto presentedInDrag = static_cast<double>(presentedAtDragEnd.presented - presentedBefore.presented);
    const auto committedInDrag = static_cast<double>(presentedAtDragEnd.committed - presentedBefore.committed);
    const auto adoptedInDrag = static_cast<double>(presentedAtDragEnd.adopted - presentedBefore.adopted);
    const auto pixmapsInDrag = static_cast<double>(presentedAtDragEnd.pixmapsMade - presentedBefore.pixmapsMade);
    const auto reducedInDrag = static_cast<double>(presentedAtDragEnd.proxiesReduced - presentedBefore.proxiesReduced);
    const double perShown = (std::max)(1.0, shownInDrag);
    // From the third preview shown in the drag until its end; nothing when the drag showed no more than three.
    const double shownAfterWarmUp = shownInDrag - static_cast<double>(shownInWarmUp);
    const std::string faultsAfterWarmUp =
        memoryAfterWarmUp.has_value() && shownAfterWarmUp > 0.0
            ? fixed((memoryAtDragEnd.pageFaults - memoryAfterWarmUp->pageFaults) / shownAfterWarmUp, 0)
            : std::string("none");
    // From the drag's start until the dialog was idle again.
    const auto shownInAll = static_cast<double>(runsEnd.previewsCommitted - runsBefore.previewsCommitted);

    const auto meanMs = [](std::chrono::nanoseconds total, std::uint64_t count) {
        return fixed(milliseconds(total) / (std::max)(1.0, static_cast<double>(count)));
    };
    const std::uint64_t committedInRun = runsEnd.previewsCommitted - runsBefore.previewsCommitted;
    const std::vector<ReportField> fields = {
        {"date", QDateTime::currentDateTime().toString(Qt::ISODate).toStdString()},
        {"process", subject.name},
        {"frame", namesSyntheticFrame(named) ? named.toStdString() : QFileInfo(named).fileName().toStdString()},
        {"frame_size", std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + "x" +
                           std::to_string(described->extent().channelCount())},
        {"session_width", std::to_string(session.width())},
        {"mode", previewView->isAutoStretchEnabled() ? "autostretch" : "linear"},
        {"preview_scale", fixed(previewWidget->getCurrentScaleFactor(), 4)},
        {"preview_widget", std::to_string(previewWidget->width()) + "x" + std::to_string(previewWidget->height())},
        {"preview_shown", shownPart.has_value()
                              ? std::to_string(shownPart->width) + "x" + std::to_string(shownPart->height)
                              : std::string("none")},
        {"histogram", histogram ? "shown" : "hidden"},
        {"mask", maskKind == 0 ? "none" : (maskKind == 2 ? "UInt16" : "Float32")},
        {"channel", channel.isEmpty() ? std::string("whole") : channel.toStdString()},
        {"preview_area", areaShown.isEmpty()
                             ? std::string("none")
                             : std::to_string(areaShown.width()) + "x" + std::to_string(areaShown.height())},
        {"steps", std::to_string(steps)},
        {"step_ms", std::to_string(stepMs)},
        {"frame_rate", std::to_string(frameRateInUse)},
        {"drag_ms", fixed(dragMs, 0)},
        {"settle_ms", fixed(settledMs - dragMs, 0)},
        {"previews_shown_in_drag", fixed(shownInDrag, 0)},
        {"previews_per_second", fixed(shownInDrag / dragSeconds)},
        {"previews_shown_in_all", fixed(shownInAll, 0)},
        {"settled_frames_in_drag",
         std::to_string(runsAtDragEnd.settledPreviewsCommitted - runsBefore.settledPreviewsCommitted)},
        {"interactive_frames_in_drag",
         std::to_string(runsAtDragEnd.interactivePreviewsCommitted - runsBefore.interactivePreviewsCommitted)},
        {"settled_frame_after_release_ms", settledShown ? fixed(settledCommittedMs) : std::string("none")},
        {"settled_presented_after_release_ms", settledShown ? fixed(settledPresentedMs) : std::string("none")},
        {"previews_requested", std::to_string(runsEnd.previewsRequested - runsBefore.previewsRequested)},
        {"previews_started", std::to_string(runsEnd.previewsStarted - runsBefore.previewsStarted)},
        {"previews_discarded", std::to_string(runsEnd.previewsDiscarded - runsBefore.previewsDiscarded)},
        {"previews_failed", std::to_string(runsEnd.previewsFailed - runsBefore.previewsFailed)},
        {"frame_mean_ms", fixed(frameTimeInDrag / perShown)},
        {"frame_longest_ms", fixed(milliseconds(runsEnd.longestPreview))},
        {"frame_work_mean_ms", meanMs(runsEnd.frameWorkTime - runsBefore.frameWorkTime, committedInRun)},
        {"frame_work_longest_ms", fixed(milliseconds(runsEnd.longestFrameWork))},
        {"admission_wait_mean_ms", meanMs(runsEnd.admissionWaitTime - runsBefore.admissionWaitTime, committedInRun)},
        {"admission_wait_longest_ms", fixed(milliseconds(runsEnd.longestAdmissionWait))},
        {"view_frames_in_drag", std::to_string(runsAtDragEnd.viewFramesCommitted - runsBefore.viewFramesCommitted)},
        {"view_frames_layered_in_drag", std::to_string(runsAtDragEnd.viewFramesLayered - runsBefore.viewFramesLayered)},
        {"view_frame_factor", std::to_string(runsEnd.viewFrameFactor)},
        {"presentations_adopted_in_drag",
         std::to_string(runsAtDragEnd.presentationsAdopted - runsBefore.presentationsAdopted)},
        {"histograms_counted_in_drag", std::to_string(runsAtDragEnd.histogramsCounted - runsBefore.histogramsCounted)},
        {"histograms_remapped_in_drag",
         std::to_string(runsAtDragEnd.histogramsRemapped - runsBefore.histogramsRemapped)},
        {"histograms_sampled_in_drag", std::to_string(runsAtDragEnd.histogramsSampled - runsBefore.histogramsSampled)},
        {"analyses_measured", std::to_string(runsEnd.analysesMeasured - runsBefore.analysesMeasured)},
        {"scopes_prepared", std::to_string(runsEnd.scopesPrepared - runsBefore.scopesPrepared)},
        {"scopes_reused", std::to_string(runsEnd.scopesReused - runsBefore.scopesReused)},
        {"planar_conversions_made", std::to_string(runsEnd.planarConversionsMade - runsBefore.planarConversionsMade)},
        {"planar_conversions_reused",
         std::to_string(runsEnd.planarConversionsReused - runsBefore.planarConversionsReused)},
        {"crops_made", std::to_string(runsEnd.cropsMade - runsBefore.cropsMade)},
        {"crops_reused", std::to_string(runsEnd.cropsReused - runsBefore.cropsReused)},
        {"composite_bases_built", std::to_string(runsEnd.compositeBasesBuilt - runsBefore.compositeBasesBuilt)},
        {"patch_frames_in_drag", std::to_string(runsAtDragEnd.patchFramesCommitted - runsBefore.patchFramesCommitted)},
        {"histograms_derived_in_drag", std::to_string(runsAtDragEnd.histogramsDerived - runsBefore.histogramsDerived)},
        {"layers_adopted_in_drag", std::to_string(presentedAtDragEnd.layersAdopted - presentedBefore.layersAdopted)},
        {"settled_digest", hexOf(settledDigest)},
        {"applied_digest", appliedDigest.has_value() ? hexOf(*appliedDigest) : std::string("none")},
        {"settled_is_applied",
         appliedDigest.has_value() ? (*appliedDigest == settledDigest ? "yes" : "no") : std::string("none")},
        {"cores_busy_in_drag", fixed(processorInDrag / dragMs, 2)},
        {"processor_ms_in_drag", fixed(processorInDrag, 0)},
        {"processor_ms_in_all", fixed(processorEnd - processorStart, 0)},
        {"gui_pumps", std::to_string(pumps.pumps)},
        {"gui_pump_longest_ms", fixed(pumps.longest)},
        {"gui_pumps_over_4ms", std::to_string(pumps.over4)},
        {"gui_pumps_over_8ms", std::to_string(pumps.over8)},
        {"gui_pumps_over_16ms", std::to_string(pumps.over16)},
        {"gui_pumps_over_33ms", std::to_string(pumps.over33)},
        {"gui_blocked_ms", fixed(pumps.blocked, 0)},
        {"gui_blocked_ms_per_second", fixed(pumps.blocked / dragSeconds, 0)},
        {"page_faults_in_drag", fixed(faultsInDrag, 0)},
        {"page_faults_per_preview", fixed(faultsInDrag / perShown, 0)},
        {"page_faults_per_preview_after_warm_up", faultsAfterWarmUp},
        {"working_set_mib_before", fixed(memoryBefore.workingSetMiB, 0)},
        {"working_set_mib_after", fixed(memoryEnd.workingSetMiB, 0)},
        {"working_set_mib_peak", fixed(memoryEnd.peakWorkingSetMiB, 0)},
        {"private_mib_before", fixed(memoryBefore.privateMiB, 0)},
        {"private_mib_after", fixed(memoryEnd.privateMiB, 0)},
        {"presentations_requested", std::to_string(presentedEnd.requested - presentedBefore.requested)},
        {"presentations_committed", std::to_string(presentedEnd.committed - presentedBefore.committed)},
        {"presentations_discarded", std::to_string(presentedEnd.discarded - presentedBefore.discarded)},
        {"presentations_coalesced", std::to_string(presentedEnd.coalesced - presentedBefore.coalesced)},
        {"presentations_failed", std::to_string(presentedEnd.failed - presentedBefore.failed)},
        {"presentations_run_in_drag", fixed(presentedInDrag, 0)},
        {"present_mean_ms", fixed(milliseconds(presentedAtDragEnd.presentTime - presentedBefore.presentTime) /
                                  (std::max)(1.0, presentedInDrag))},
        {"present_longest_ms", fixed(milliseconds(presentedEnd.longestPresent))},
        {"pixmaps_made_in_drag", fixed(pixmapsInDrag, 0)},
        {"pixmap_mean_ms", fixed(milliseconds(presentedAtDragEnd.pixmapTime - presentedBefore.pixmapTime) /
                                 (std::max)(1.0, pixmapsInDrag))},
        {"adopt_mean_ms", fixed(milliseconds(presentedAtDragEnd.adoptTime - presentedBefore.adoptTime) /
                                    (std::max)(1.0, committedInDrag + adoptedInDrag),
                                2)},
        {"adopt_longest_ms", fixed(milliseconds(presentedEnd.longestAdopt), 2)},
        {"presented_factor", std::to_string(presentedEnd.presentedFactor)},
        {"proxies_reduced_in_drag", fixed(reducedInDrag, 0)},
        {"reduce_mean_ms", fixed(milliseconds(presentedAtDragEnd.reduceTime - presentedBefore.reduceTime) /
                                 (std::max)(1.0, reducedInDrag))},
        {"reduce_longest_ms", fixed(milliseconds(presentedEnd.longestReduce))},
        {"whole_image_measured", std::to_string(presentedEnd.wholeImageMeasured - presentedBefore.wholeImageMeasured)},
        {"whole_measure_mean_ms",
         fixed(milliseconds(presentedAtDragEnd.wholeImageMeasureTime - presentedBefore.wholeImageMeasureTime) /
               (std::max)(1.0, static_cast<double>(presentedAtDragEnd.wholeImageMeasured -
                                                   presentedBefore.wholeImageMeasured)))},
    };

    std::printf("[ drag ] %s %s, session width %u, preview %s at scale %s in a %s widget, histogram %s, mask %s\n",
                fields[1].value.c_str(), fields[2].value.c_str(), session.width(), fields[4].value.c_str(),
                fields[5].value.c_str(), fields[6].value.c_str(), fields[7].value.c_str(), fields[8].value.c_str());
    for (const ReportField& field : fields)
    {
        std::printf("  %-28s %s\n", field.name.c_str(), field.value.c_str());
        ::testing::Test::RecordProperty(field.name, field.value);
    }
    std::fflush(stdout);
    const QString trace = qEnvironmentVariable("AN_BENCH_TRACE");
    if (!trace.isEmpty())
    {
        appendTrace(trace.toStdString(), fields);
    }

    dialog.reset();
    if (mask)
    {
        target->removeCurrentMask();
        EXPECT_TRUE(settleView(*target));
        mask.reset();
    }
    target.reset();
    installed.reset();
}

} // namespace

TEST(RealtimePreviewDragBenchmark, BrightnessAndContrastSliderDrag)
{
    runDrag(brightnessDrag());
}

TEST(RealtimePreviewDragBenchmark, GammaCorrectionSliderDrag)
{
    runDrag(gammaDrag());
}

TEST(RealtimePreviewDragBenchmark, AdjustLChLightnessDrag)
{
    runDrag(lchDrag());
}

TEST(RealtimePreviewDragBenchmark, GaussianBlurSigmaDrag)
{
    runDrag(gaussianDrag());
}

TEST(RealtimePreviewDragBenchmark, MedianFilterModulationDrag)
{
    runDrag(medianDrag());
}

TEST(RealtimePreviewDragBenchmark, HistogramTransformationMidtonesDrag)
{
    runDrag(midtonesDrag());
}

TEST(RealtimePreviewDragBenchmark, CurvesTransformationPointDrag)
{
    runDrag(curvesDrag());
}

// A standard view of the frame, without a dialog: what each presentation costs fitted and at 1:1, linear and stretched,
// bare and under a multiply mask - the engine's time on a worker, the proxies it reduced, the GUI thread's adoption
// and one synchronous paint - and what a pan at 1:1 costs the GUI thread. Runs when AN_BENCH_IMAGE names a frame, as
// the drag does; AN_BENCH_WIDTH sets the session's width.
TEST(RealtimePreviewDragBenchmark, StandardViewPresentations)
{
    const QString named = qEnvironmentVariable("AN_BENCH_IMAGE");
    if (named.isEmpty())
    {
        GTEST_SKIP() << "set AN_BENCH_IMAGE to a frame's path, or to synthetic, to run the view benchmark";
    }
    const int width = knob("AN_BENCH_WIDTH", 0);
    an::consumer::EngineSessionSettings settings;
    settings.codecRegistry = []() { return an::ui::createProductCodecRegistry(); };
    if (width > 0)
    {
        settings.maxConcurrency = static_cast<std::uint32_t>(width);
    }
    an::Result<an::consumer::EngineSession> opened = an::consumer::EngineSession::open(settings);
    ASSERT_TRUE(opened.hasValue()) << opened.status().message();
    auto context = std::make_unique<ProcessTestContext>(std::move(*opened));
    std::optional<InstalledContext> installed;
    installed.emplace(*context);
    const an::consumer::EngineSession& session = context->engineSession();

    std::optional<an::consumer::ImageAsset> frame = benchFrame(session, named);
    ASSERT_TRUE(frame.has_value());
    const std::optional<an::ImageDescriptor> described = frame->descriptor();
    ASSERT_TRUE(described.has_value());
    const std::uint32_t frameWidth = described->extent().width();
    const std::uint32_t frameHeight = described->extent().height();
    std::optional<an::consumer::ImageAsset> maskAsset = benchMask(session, 2, frameWidth, frameHeight);
    ASSERT_TRUE(maskAsset.has_value());
    SceneView mask = assetView(std::move(*maskAsset), "mask");
    SceneView target = assetView(std::move(*frame), "target");
    ASSERT_TRUE(target && mask);
    target->resize(1600, 1000);
    target->show();
    ASSERT_TRUE(settleView(*target));
    ImageViewWidget* widget = target->getViewWidget();

    const std::string frameName =
        namesSyntheticFrame(named) ? named.toStdString() : QFileInfo(named).fileName().toStdString();
    std::printf("[ view ] %s, session width %u, view %dx%d\n", frameName.c_str(), session.width(), widget->width(),
                widget->height());
    for (const bool masked : {false, true})
    {
        if (masked)
        {
            target->setCurrentMask(mask.get());
            target->setMaskRenderingMode(MaskRenderingMode::multiply_mode);
            ASSERT_TRUE(settleView(*target));
        }
        for (const bool stretched : {false, true})
        {
            for (const bool full : {false, true})
            {
                target->setViewMode(stretched ? ImageViewMode::AUTOSTRETCH_MODE : ImageViewMode::LINEAR_MODE);
                if (full)
                {
                    widget->zoom100();
                }
                else
                {
                    widget->fitInView();
                }
                ASSERT_TRUE(settleView(*target));
                // The image is presented again for the revision now shown, so the cost is a presentation's, not a
                // cached one's.
                const ImageViewPresentationTelemetry before = widget->presentationTelemetry();
                std::optional<an::consumer::ImageAsset> again = benchFrame(session, named);
                ASSERT_TRUE(again.has_value());
                target->replaceImage(std::move(*again));
                ASSERT_TRUE(settleView(*target));
                const ImageViewPresentationTelemetry after = widget->presentationTelemetry();
                QElapsedTimer painting;
                painting.start();
                widget->repaint();
                const double paintMs = static_cast<double>(painting.nsecsElapsed()) / 1.0e6;
                const auto presented = static_cast<double>(after.presented - before.presented);
                const std::shared_ptr<const an::ui::PresentedImage> bytes = widget->presentedImage();
                std::printf(
                    "[ view ] %-11s %-3s %-8s present %7s ms, of which reduce %6s ms (%llu) and "
                    "whole measure %6s ms (%llu); adopt %5s ms, paint %5s ms, pixmaps %llu, factor %u, "
                    "presented %ux%u\n",
                    stretched ? "autostretch" : "linear", full ? "1:1" : "fit", masked ? "multiply" : "bare",
                    fixed(milliseconds(after.presentTime - before.presentTime) / (std::max)(1.0, presented)).c_str(),
                    fixed(milliseconds(after.reduceTime - before.reduceTime)).c_str(),
                    static_cast<unsigned long long>(after.proxiesReduced - before.proxiesReduced),
                    fixed(milliseconds(after.wholeImageMeasureTime - before.wholeImageMeasureTime)).c_str(),
                    static_cast<unsigned long long>(after.wholeImageMeasured - before.wholeImageMeasured),
                    fixed(milliseconds(after.adoptTime - before.adoptTime) /
                              (std::max)(1.0, static_cast<double>(after.committed - before.committed)),
                          2)
                        .c_str(),
                    fixed(paintMs, 2).c_str(), static_cast<unsigned long long>(after.pixmapsMade - before.pixmapsMade),
                    after.presentedFactor, bytes ? bytes->width() : 0u, bytes ? bytes->height() : 0u);
            }
        }
    }

    // A pan across the frame at 1:1, a step every 16 ms: the GUI thread's pumps and the presentations it asked for.
    widget->zoom100();
    ASSERT_TRUE(settleView(*target));
    QScrollBar* across = target->getScrollArea()->horizontalScrollBar();
    across->setValue(0);
    ASSERT_TRUE(settleView(*target));
    const ImageViewPresentationTelemetry beforePan = widget->presentationTelemetry();
    PumpCounts pumps;
    const int panSteps = 60;
    for (int step = 0; step < panSteps; ++step)
    {
        const double started = nowMilliseconds();
        across->setValue(across->value() + 40);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        pumps.note(nowMilliseconds() - started);
        QElapsedTimer idle;
        idle.start();
        while (idle.elapsed() < 16)
        {
            const double turned = nowMilliseconds();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            pumps.note(nowMilliseconds() - turned);
        }
    }
    ASSERT_TRUE(settleView(*target));
    const ImageViewPresentationTelemetry afterPan = widget->presentationTelemetry();
    std::printf("[ view ] pan at 1:1 (multiply, autostretch): %d pumps, longest %s ms, %d over 16 ms, blocked %s ms; "
                "%llu presentations asked, %llu run\n",
                pumps.pumps, fixed(pumps.longest).c_str(), pumps.over16, fixed(pumps.blocked, 0).c_str(),
                static_cast<unsigned long long>(afterPan.requested - beforePan.requested),
                static_cast<unsigned long long>(afterPan.presented - beforePan.presented));
    target->removeCurrentMask();
}
