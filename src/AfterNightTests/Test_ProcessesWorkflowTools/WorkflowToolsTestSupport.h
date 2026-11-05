// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the workflow tools' cases share: frames written to disk as the tools read them - planted star fields, flat
/// frames of a stated value, calibration masters - read back as the engine reads them, the dialogs the integration's
/// companions show over a tool's window, and the waiting a case does for a window's work to land.
#pragma once

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"

#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QApplication>
#include <QString>
#include <QStringList>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace an::tests::processes::workflow
{

using an::tests::processes::ProcessTestContext;
using an::tests::processes::stars::PlantedStar;
using an::tests::processes::stars::ProfilerCase;
using an::tests::processes::stars::StarField;
using an::tests::viewer::pumpUntil;

/// The shown dialog of a class - one the integration's companions opened over a tool's window - or null. GUI thread
/// only.
template <typename DialogT> [[nodiscard]] DialogT* shownDialog()
{
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        if (auto* dialog = dynamic_cast<DialogT*>(widget); dialog != nullptr && dialog->isVisible())
        {
            return dialog;
        }
    }
    return nullptr;
}

/// A flat frame of one value, in a representation, written to a file through the output transaction.
///
/// @param session the case's session.
/// @param path the file.
/// @param width its width.
/// @param height its height.
/// @param channels its channels.
/// @param fraction every sample, as a fraction of full scale.
/// @param type the representation.
/// @param exposureSeconds the exposure its cards state, or nothing.
/// @param filter the filter its cards state, or empty.
/// @return whether it was written.
[[nodiscard]] bool writeFlatFrame(const an::consumer::EngineSession& session, const QString& path, std::uint32_t width,
                                  std::uint32_t height, std::uint32_t channels, double fraction, an::SampleType type,
                                  std::optional<double> exposureSeconds = std::nullopt, const QString& filter = {});

/// A frame whose samples a function states, in fractions of full scale, written in a representation; a grey frame
/// under a filter array when one is given.
[[nodiscard]] bool writeFrame(const an::consumer::EngineSession& session, const QString& path, std::uint32_t width,
                              std::uint32_t height, std::uint32_t channels, an::SampleType type,
                              const std::function<double(std::uint32_t x, std::uint32_t y, std::uint32_t c)>& sample,
                              const an::ImageMetadata& metadata = {},
                              const std::optional<an::CfaDescriptor>& cfa = std::nullopt);

/// A star field, its stars moved by a translation, written as a Float32 FITS file with its solution's cards.
[[nodiscard]] bool writeShiftedField(const an::consumer::EngineSession& session, const QString& path,
                                     const StarField& field, double dx, double dy, bool solved = true);

/// What a file holds, as the engine reads it.
struct ReadBack
{
    an::SampleType type = an::SampleType::Float32;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t channels = 0u;

    /// Every sample, planar, as a fraction of full scale.
    std::vector<double> fractions;

    /// The cards the file states, as keyword and value.
    std::vector<std::pair<std::string, std::string>> cards;

    [[nodiscard]] double at(std::uint32_t x, std::uint32_t y, std::uint32_t c = 0u) const
    {
        return fractions[(static_cast<std::size_t>(c) * height + y) * width + x];
    }

    /// A card's value, or nothing.
    [[nodiscard]] std::optional<std::string> card(const std::string& keyword) const;

    /// A card's value as a number read in the C locale, or nothing when it is absent or not a number: std::stod
    /// follows the process locale, whose decimal comma stops it at the point.
    [[nodiscard]] std::optional<double> number(const std::string& keyword) const;
};

/// Reads a file back.
[[nodiscard]] std::optional<ReadBack> readBack(const an::consumer::EngineSession& session, const QString& path);

/// The records the process logger passes while it lives, kept, and a way to act on the thread that logs one: to run
/// something at a point of a worker's run that nothing else marks, or to hold the worker there until the case lets it
/// go.
class LogCapture
{
public:
    LogCapture();
    ~LogCapture();
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    /// Every record's text so far, oldest first.
    [[nodiscard]] std::vector<std::string> messages() const;

    /// How many records so far hold the fragment.
    [[nodiscard]] std::size_t count(const std::string& fragment) const;

    /// Runs the action on the logging thread, before that thread's call returns, each time a record holds the fragment.
    /// The action must not log.
    void onRecord(std::string fragment, std::function<void()> action);

    /// Holds the first thread that logs a record holding the fragment until `release`, or for ten seconds. Nothing may
    /// log on the case's thread between this hold and `release`.
    void holdAt(std::string fragment);

    /// Waits until a thread is held.
    [[nodiscard]] bool waitHeld(std::chrono::milliseconds timeout = std::chrono::seconds(10)) const;

    /// Lets the held thread go.
    void release();

private:
    class Sink;
    std::shared_ptr<Sink> m_sink;
    int m_previousLevel = 0;
};

/// A workflow case: the profilers' context over the session, and a clean companion record.
class WorkflowCase : public ProfilerCase
{
protected:
    void SetUp() override;
    void TearDown() override;
};

} // namespace an::tests::processes::workflow
