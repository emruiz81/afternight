// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The stand-in program: the product's probes answered, the handed image read through the engine's codecs, and the
/// answers written as stated functions of it.

#include "Test_ProcessesExternalTools/StandInTool.h"

#include "controls/ImageViewFiles.h"

#include "fixtures/engine/EngineFixtureAdapter.h"

#include <AfterNightConsumerRuntime/EngineSession.h>
#include <AfterNightConsumerRuntime/ImageAsset.h>
#include <AfterNightConsumerRuntime/OperationScope.h>

#include <AfterNightEngine/Core/EngineLimits.h>
#include <AfterNightEngine/Core/ImageExtent.h>
#include <AfterNightEngine/IO/CodecFormat.h>
#include <AfterNightEngine/IO/CodecIO.h>
#include <AfterNightEngine/IO/CodecRequests.h>
#include <AfterNightEngine/IO/OutputTransaction.h>
#include <AfterNightEngine/Image/Image.h>
#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/OutputIntent.h>
#include <AfterNightEngine/Image/SampleTraits.h>
#include <AfterNightEngine/Image/StorageArrangement.h>
#include <AfterNightEngine/Runtime/EngineContext.h>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#endif

namespace an::tests::processes::externaltools
{

namespace
{

namespace adapter = an::tests::fixtures::engine;

/// What one answer is of the handed image.
enum class Answer
{
    Identity,
    Half,
    Complement,
    WrongSize
};

[[nodiscard]] std::string environment(const char* name)
{
    return qgetenv(name).toStdString();
}

/// The arguments as the StarNet CLI family spells them.
struct Arguments
{
    std::string input;
    std::string output;
    std::string mask;
    std::string stars;
    bool version = false;
    bool machineInfo = false;
    bool machineProgress = false;
};

[[nodiscard]] Arguments parse(int argc, char** argv)
{
    Arguments arguments;
    for (int index = 1; index < argc; ++index)
    {
        const std::string option = argv[index];
        const auto value = [&]() -> std::string {
            return index + 1 < argc ? std::string(argv[++index]) : std::string();
        };
        if (option == "--version")
        {
            arguments.version = true;
        }
        else if (option == "--machine-info")
        {
            arguments.machineInfo = true;
        }
        else if (option == "--machine-progress")
        {
            arguments.machineProgress = true;
        }
        else if (option == "--input" || option == "-i")
        {
            arguments.input = value();
        }
        else if (option == "--output" || option == "-o")
        {
            arguments.output = value();
        }
        else if (option == "--mask" || option == "-m")
        {
            arguments.mask = value();
        }
        else if (option == "--unscreen" || option == "-n")
        {
            arguments.stars = value();
        }
        else if (option == "--stride" || option == "-s" || option == "--weights" || option == "-w")
        {
            (void)value();
        }
    }
    return arguments;
}

void record(int argc, char** argv)
{
    const std::string path = environment(kStandInRecordVariable);
    if (path.empty())
    {
        return;
    }
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::Append | QIODevice::Text))
    {
        return;
    }
    QStringList line;
    for (int index = 1; index < argc; ++index)
    {
        line << QString::fromUtf8(argv[index]);
    }
    file.write(line.join(QLatin1Char(' ')).toUtf8());
    file.write("\n");
}

[[nodiscard]] const char* productVersion(const std::string& product)
{
    return product == "deepsnr" ? "1.2.2" : "2.5.3";
}

void printProgress(const std::string& product, bool machine, int percent)
{
    if (machine)
    {
        std::fprintf(stderr,
                     "{\"schema\":\"starnetastro.cli.progress.v1\",\"event\":\"progress\",\"stage\":\"inference\","
                     "\"percent\":%d.0}\n",
                     percent);
        std::fflush(stderr);
        return;
    }
    std::printf("%s %d%% finished\n", product.c_str(), percent);
    std::fflush(stdout);
}

/// Writes one answer of the handed image, in its own representation and arrangement.
[[nodiscard]] bool writeAnswer(const an::consumer::OperationScope& scope, const std::string& input,
                               const std::string& output, Answer answer)
{
    an::Result<an::CodecDecodeResult> decoded =
        an::decodeImage(scope.engine(), scope.execution(), an::CodecReadRequest{.path = input});
    if (!decoded.hasValue())
    {
        std::fprintf(stderr, "cannot read %s: %s\n", input.c_str(), decoded.status().message());
        return false;
    }
    an::Result<an::consumer::AnyImage> image = an::ui::importViewFrame(
        scope.engine(), scope.execution(), std::move(decoded->frame), std::move(decoded->metadata), false);
    if (!image.hasValue())
    {
        std::fprintf(stderr, "cannot import %s: %s\n", input.c_str(), image.status().message());
        return false;
    }
    const bool written = std::visit(
        [&](auto& typed) -> bool {
            using SampleT = typename std::remove_cvref_t<decltype(typed)>::Sample;
            const std::optional<an::ImageDescriptor> described = typed.descriptor();
            if (!described.has_value())
            {
                return false;
            }
            if (answer == Answer::WrongSize)
            {
                an::ImageDescriptorRequest request = described->toRequest();
                const std::optional<an::ImageExtent> extent = an::ImageExtent::create(
                    (std::max)(1u, described->extent().width() / 2u), described->extent().height(),
                    described->extent().channelCount(), an::defaultEngineLimits());
                if (!extent.has_value())
                {
                    return false;
                }
                request.extent = *extent;
                const std::optional<an::ImageDescriptor> smaller = an::ImageDescriptor::create(request);
                an::Result<an::Image<SampleT>> wrong = an::Image<SampleT>::create(
                    scope.engine(), *smaller, an::StorageArrangement::Planar, an::StorageInitialization::Zeroed);
                if (!wrong.hasValue())
                {
                    return false;
                }
                typed = std::move(*wrong);
            }
            else if (answer != Answer::Identity)
            {
                std::optional<an::ImageWriteAccess<SampleT>> access = typed.write();
                if (!access.has_value())
                {
                    return false;
                }
                const an::ImageLayout& layout = access->view().layout();
                const std::uint32_t perPixel = layout.samplesPerPixel();
                for (std::uint32_t channel = 0u; channel < layout.extent().channelCount(); ++channel)
                {
                    for (std::uint32_t y = 0u; y < layout.extent().height(); ++y)
                    {
                        for (std::uint32_t x = 0u; x < layout.extent().width(); ++x)
                        {
                            SampleT* sample = access->view().sampleAt(channel / perPixel, x, y, channel % perPixel);
                            if constexpr (std::is_floating_point_v<SampleT>)
                            {
                                *sample = answer == Answer::Half ? *sample / SampleT{2} : SampleT{1} - *sample;
                            }
                            else
                            {
                                *sample = answer == Answer::Half
                                              ? static_cast<SampleT>(*sample / 2)
                                              : static_cast<SampleT>((std::numeric_limits<SampleT>::max)() - *sample);
                            }
                        }
                    }
                }
            }
            const an::Result<an::OutputIntent> intent = an::OutputIntent::create(
                an::OutputIntentRequest{.sampleType = an::kSampleTypeOf<SampleT>, .preserveSourceCards = false});
            an::Result<an::RawFrame> frame = an::borrowImageFrame(typed);
            if (!intent.hasValue() || !frame.hasValue())
            {
                return false;
            }
            an::OutputRequest request;
            request.destinationPath = output;
            request.format = an::CodecFormat::Tiff;
            request.intent = &*intent;
            request.replacePolicy = an::OutputReplacePolicy::ReplaceExisting;
            const an::Result<an::OutputReport> report =
                an::writeImage(scope.engine(), scope.execution(), request, *frame, typed.metadata());
            if (!report.hasValue())
            {
                std::fprintf(stderr, "cannot write %s: %s\n", output.c_str(), report.status().message());
            }
            return report.hasValue();
        },
        *image);
    return written;
}

} // namespace

int runStandInTool(int argc, char** argv)
{
    const std::string product = environment(kStandInToolVariable);
    const std::string behaviour = environment(kStandInBehaviourVariable);
    const bool machine = behaviour == "machine" || behaviour == "machine-slow";
    const Arguments arguments = parse(argc, argv);
    if (!arguments.version && !arguments.machineInfo)
    {
        // Only a run over an image is recorded; the product's probes of the program are not.
        record(argc, argv);
    }

    if (arguments.version)
    {
        std::printf("%s  version: %s\n", product.c_str(), productVersion(product));
        return 0;
    }
    if (arguments.machineInfo)
    {
        if (!machine)
        {
            std::printf("PARSE ERROR: Argument: --machine-info\n             Couldn't find match for argument\n");
            return 1;
        }
        std::printf("{\"schema\":\"starnetastro.cli.machine-info.v1\",\"product\":\"%s\",\"version\":\"%s\","
                    "\"io\":{\"progress\":{\"machine\":{\"supported\":true,\"stable\":true,\"stream\":\"stderr\","
                    "\"format\":\"json_lines\",\"schema\":\"starnetastro.cli.progress.v1\","
                    "\"option\":\"machine_progress\"}}}}\n",
                    product.c_str(), productVersion(product));
        return 0;
    }
    if (arguments.input.empty())
    {
        std::fprintf(stderr, "PARSE ERROR: Required argument missing: input\n");
        return 1;
    }
    if (const std::string keep = environment(kStandInKeepInputVariable); !keep.empty())
    {
        QFile::remove(QString::fromStdString(keep));
        (void)QFile::copy(QString::fromStdString(arguments.input), QString::fromStdString(keep));
    }

    std::printf("%s version %s, stand-in\n", product.c_str(), productVersion(product));
    std::fflush(stdout);
    if (behaviour == "exit-code")
    {
        std::printf("bad input: the stand-in was told to fail\n");
        return 3;
    }
    if (behaviour == "crash")
    {
#if defined(_WIN32)
        TerminateProcess(GetCurrentProcess(), 0xC0000005u);
#else
        std::raise(SIGKILL);
#endif
        return 1;
    }
    if (machine)
    {
        std::fprintf(stderr, "ONNX Runtime warning: stand-in\n");
        std::fflush(stderr);
    }
    const bool slow = behaviour == "slow" || behaviour == "machine-slow";
    const int steps = slow ? (std::max)(1, std::atoi(environment(kStandInSlowMsVariable).empty()
                                                         ? "6000"
                                                         : environment(kStandInSlowMsVariable).c_str()) /
                                               200)
                           : 4;
    for (int step = 1; step <= steps; ++step)
    {
        printProgress(product, machine && arguments.machineProgress, (step * 100) / steps);
        if (slow)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    if (behaviour == "no-output")
    {
        return 0;
    }

    an::Result<an::consumer::EngineSession> session = adapter::createFixtureSession(1u);
    if (!session.hasValue())
    {
        std::fprintf(stderr, "no engine: %s\n", session.status().message());
        return 4;
    }
    an::Result<an::consumer::OperationScope> scope = adapter::admitFixtureOperation(*session);
    if (!scope.hasValue())
    {
        std::fprintf(stderr, "no operation: %s\n", scope.status().message());
        return 4;
    }
    const Answer imageAnswer = behaviour == "wrong-size" ? Answer::WrongSize
                               : behaviour == "half"     ? Answer::Half
                                                         : Answer::Identity;
    if (!arguments.output.empty() && !writeAnswer(*scope, arguments.input, arguments.output, imageAnswer))
    {
        return 5;
    }
    if (!arguments.mask.empty() && !writeAnswer(*scope, arguments.input, arguments.mask, Answer::Complement))
    {
        return 5;
    }
    if (!arguments.stars.empty() && !writeAnswer(*scope, arguments.input, arguments.stars, Answer::Half))
    {
        return 5;
    }
    if (machine && arguments.machineProgress)
    {
        std::fprintf(stderr, "{\"schema\":\"starnetastro.cli.progress.v1\",\"event\":\"finish\",\"percent\":100.0}\n");
        std::fflush(stderr);
    }
    std::printf("done\n");
    return 0;
}

} // namespace an::tests::processes::externaltools
