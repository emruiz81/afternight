// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// What the six processes that run a G'MIC command share: whether this build carries the library, what a dialog says
/// when it does not, and one command run over the plane a channel scope hands a process, through the optional adapter.
///
/// **The library is optional, and a build without it says so.** The adapter is linked by every build; a build whose
/// adapter carries no library refuses every command by name. A dialog over such a build keeps its preview and Apply off
/// and says why, and a run asked for anyway answers the adapter's refusal rather than a missing symbol.
///
/// **A command sees fractions of full scale.** The plane is handed over as tightly packed Float32 fractions of full
/// scale - a working copy where the plane is another representation or padded - and the command's result is held to
/// the unit interval here; the adapter itself clamps nothing.
#pragma once

#include "AfterNightProcessesExport.h"

#include "controls/ProcessExecution.h"
#include "session/ExecutionScope.h"

#include <AfterNightEngine/Algorithms/Color/ChannelScope.h>
#include <AfterNightEngine/Core/Status.h>

#include <QString>

#include <string_view>

class ProgressDialogControl;
class QCheckBox;
class QDialogButtonBox;
class QWidget;

namespace process_utils
{

/// Whether this build of the G'MIC adapter carries the library. Asks nothing of the library and cannot fail.
[[nodiscard]] AFTERNIGHTPROCESSES_API bool gmicAvailable() noexcept;

/// What a G'MIC process's dialog says in a build without the library.
inline constexpr const char* kGmicUnavailableText =
    "This build of AfterNight carries no G'MIC library, so this process cannot run.";

/// Keeps a G'MIC process's preview and Apply off in a build without the library. GUI thread.
///
/// @param preview the dialog's preview check box, or null.
/// @param buttons the dialog's buttons, or null.
void refuseWithoutGmic(QCheckBox* preview, QDialogButtonBox* buttons);

/// One G'MIC command and what the run asks of it.
struct GmicCommand
{
    /// The command, in the library's own language.
    std::string_view text;

    /// Definitions the library parses before the command, for one the standard library does not define.
    std::string_view definitions{};
};

/// Runs a G'MIC command over the plane a channel scope hands a process, writing the output.
///
/// A session of the adapter is opened for the run and let go after it. The output, which starts as the input, is
/// handed to the library as Float32 fractions of full scale - itself where it already is one, tightly packed, and
/// otherwise a working copy - for the command to work on in place; what the command leaves is held to the unit interval
/// and written back, converted to the plane's representation. A plane holding a sample that is not a finite number is
/// never handed over: the library reads one as a number, and some of its commands fault on it.
///
/// @param scope the run's admitted execution; its stop reaches the running command.
/// @param input what the scope handed the process.
/// @param output where it writes; it starts as a copy of the input, planar.
/// @param command the command.
/// @return success; `ErrorCode::Unsupported` naming `GmicAdapterReason::LibraryNotCompiledIn` in a build without the
///         library; `ErrorCode::InvalidData` for a plane holding a sample that is not a finite number;
///         `ErrorCode::Cancelled` for a stopped run; or what the adapter, the library or the working copy refused.
[[nodiscard]] an::Status runGmicCommand(an::ui::ExecutionScope& scope, const an::ChannelScopeInput& input,
                                        const an::ChannelScopeOutput& output, const GmicCommand& command);

/// The definitions the local contrast enhancement's command needs, which the standard library lacks: Arto Huotari's
/// `fx_LCE` and the helper it applies.
extern const std::string_view kLocalContrastEnhancementDefinitions;

/// The progress dialog a G'MIC process shows. The library reports a share done only when the command's own script
/// states one, which the standard library's filters do not, so a still image's run is shown busy - not as 0% - until a
/// share arrives; a sequence's dialog counts its frames as every sequence run does.
///
/// @param parent the process window, which owns the dialog.
/// @param title the dialog's title.
/// @param forSequence whether the run is over a sequence's frames.
/// @return the dialog.
[[nodiscard]] AFTERNIGHTPROCESSES_API ProgressDialogControl* gmicProgressDialog(QWidget* parent, const QString& title,
                                                                                bool forSequence);

} // namespace process_utils
