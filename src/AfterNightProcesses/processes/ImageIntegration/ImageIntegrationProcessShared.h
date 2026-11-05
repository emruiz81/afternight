// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file ImageIntegrationProcessShared.h
 * @brief File-local helpers of ImageIntegrationProcess.cpp that its extracted units also need.
 *
 * Split out when the import session and the preset/menu helpers came out of what was a 5,469-line
 * ImageIntegrationProcess.cpp.
 * Those two units both reach for the same small set of helpers - icon lookup, remembered-directory
 * state, the non-blocking message boxes and the frame-type labels - so the helpers had to come out
 * first. Pure move: no call site, signature or behaviour changed.
 *
 * The message boxes are non-blocking on purpose: the process runs inside the extension host, whose
 * event loop is heartbeat-supervised and must never be blocked by a modal `exec()`.
 */
#pragma once

#include "processes/ImageIntegration/model/FrameCollection.h"
#include "processes/ImageIntegration/model/FrameMetadata.h"
#include "processes/ImageIntegration/model/FrameType.h"

#include <QIcon>
#include <QMessageBox>
#include <QSet>
#include <QString>
#include <QStyle>

#include <functional>
#include <map>
#include <vector>

class QWidget;

namespace an::consumer
{
class EngineSession;
} // namespace an::consumer

namespace ImageIntegration::detail
{

FrameMetadata remapMetadataType(const FrameMetadata& meta, FrameType type);
FrameInfo remapFrameInfoType(const FrameInfo& info, FrameType type);

QIcon integrationIcon(const QString& key);
QIcon integrationIconOrFallback(const QWidget* widget, const QString& key, const QStyle::StandardPixmap fallback);

/// Last directory the user picked for `contextKey`, if it still exists.
QString rememberedExistingPath(const QString& contextKey);
void rememberDirectoryForPath(const QString& contextKey, const QString& path);
void rememberDirectoryPath(const QString& contextKey, const QString& directory);

void openNonBlockingMessageBox(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                               const QString& detailedText = {});
void openNonBlockingQuestionBox(QWidget* parent, const QString& title, const QString& text,
                                std::function<void(QMessageBox::StandardButton, bool)> finished,
                                const QString& checkBoxText = {});

/// Asks for one line of text without blocking: `accepted` runs with the text once the person presses OK, and nothing
/// runs when they cancel. The dialog deletes itself when it closes.
///
/// @param parent the window the dialog belongs to.
/// @param title the dialog's title.
/// @param label the prompt above the text field.
/// @param accepted what to do with the text entered.
void openNonBlockingTextInput(QWidget* parent, const QString& title, const QString& label,
                              std::function<void(const QString&)> accepted);

/// Why a run into a project's output directory cannot start: its registration record database is of another format, or
/// its tables are not its format's (`stage_utils::registrationMetricsFormatRefusal`). Empty when the run may start.
///
/// @param outputDirectory the project's output directory.
/// @return the refusal, naming the database, or an empty string.
[[nodiscard]] QString outputDirectoryRefusal(const QString& outputDirectory);

/// Checks a project's output directory before a run starts. A registration record database of another format refuses
/// the run, said in a message that does not block and in the log, and is left as it is.
///
/// @param parent the window the message belongs to.
/// @param outputDirectory the project's output directory.
/// @return whether the run may start.
bool checkOutputDirectoryBeforeRun(QWidget* parent, const QString& outputDirectory);

void notifyWarnOrDialog(QWidget* parent, const QString& title, const QString& text);
void notifyInfoOrDialog(QWidget* parent, const QString& title, const QString& text);
void notifyErrorOrDialog(QWidget* parent, const QString& title, const QString& text);

QString frameTypeLabel(FrameType type);

/// The process's engine session, as the application context lends it; null where no session is lent.
const an::consumer::EngineSession* applicationEngineSession();

/// Whether a path names an image file the process's session can read: a container its name states and the session's
/// codecs read, or, where no session is lent, any container the engine knows by name.
bool isReadableImagePath(const QString& path);
QString importSummaryText(const std::map<FrameType, std::vector<FrameInfo>>& groupedInfos, int skipped);

} // namespace ImageIntegration::detail
