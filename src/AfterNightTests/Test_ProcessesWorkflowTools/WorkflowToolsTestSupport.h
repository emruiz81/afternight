// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the workflow tools' cases share: frames written to disk as the tools read them - planted star fields, flat
/// frames of a stated value, calibration masters - read back as the engine reads them, the dialogs the integration's
/// companions show over a tool's window, a tool opened as a page of a shell's centre, and the waiting a case does for
/// a window's work to land.
#pragma once

#include "Test_ProcessesStarProfilers/StarProfilerTestSupport.h"

#include "app/ProcessRegistry.h"
#include "controls/ProcessPageLayout.h"
#include "controls/TopLevelProcessBaseControl.h"
#include "processes/core/WorkflowSettingsRecords.h"

#include <AfterNightConsumerRuntime/ImageAsset.h>

#include <AfterNightEngine/Image/CfaPattern.h>
#include <AfterNightEngine/Image/ImageMetadata.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QApplication>
#include <QCoreApplication>
#include <QSize>
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

/// The registry's ids of the four tools.
inline constexpr char kImageCalibrationTool[] = "sensor_calibration.actionImage_Calibration";
inline constexpr char kImageRegistrationTool[] = "multi_image_stacking.actionImage_Registration";
inline constexpr char kImageStackingTool[] = "multi_image_stacking.actionImage_Stacking";
inline constexpr char kImageListAnalyzerTool[] = "multi_image_stacking.actionImage_List_Analyzer";

/// A tool opened as its registered action opens it while a presenter takes processes into pages: the tool as a page of
/// a centre of its own, the floor display's unless another size is given. The page goes with it, once the tool's work
/// has landed. GUI thread only.
template <typename ToolT> class PagedTool
{
public:
    /// @param context the case's context, which lends the presenter while this lives.
    /// @param descriptorId the tool's id in the registry.
    /// @param centre the size of the centre the page is in.
    PagedTool(ProcessTestContext& context, const std::string& descriptorId, const QSize centre = PagePresenter::kCentre)
        : m_context(context), m_presenter(centre)
    {
        m_context.presenter = &m_presenter;
        const an::ui::process_registry::ProcessDescriptor* const descriptor = shared::registered(descriptorId);
        if (descriptor != nullptr && descriptor->launch_top_level)
        {
            descriptor->launch_top_level();
        }
        for (TopLevelProcessBaseControl* open : m_context.openProcesses)
        {
            if (auto* const made = dynamic_cast<ToolT*>(open))
            {
                tool = made;
            }
        }
        if (tool != nullptr)
        {
            tool->setAttribute(Qt::WA_DeleteOnClose, false);
        }
        QCoreApplication::processEvents();
    }

    ~PagedTool()
    {
        if (tool != nullptr)
        {
            (void)pumpUntil([this]() { return tool->isIdle(); });
            delete tool;
        }
        QCoreApplication::processEvents();
        m_context.presenter = nullptr;
    }

    PagedTool(const PagedTool&) = delete;
    PagedTool& operator=(const PagedTool&) = delete;

    /// The centre the page lies in; null when the tool was taken into none.
    [[nodiscard]] QWidget* host() const
    {
        return tool != nullptr ? m_presenter.hostOf(*tool) : nullptr;
    }

    /// The tool; null when its action opened none.
    ToolT* tool = nullptr;

private:
    ProcessTestContext& m_context;
    PagePresenter m_presenter;
};

/// A workflow case: the profilers' context over the session, and a clean companion record.
class WorkflowCase : public ProfilerCase
{
protected:
    void SetUp() override;
    void TearDown() override;
};

} // namespace an::tests::processes::workflow
