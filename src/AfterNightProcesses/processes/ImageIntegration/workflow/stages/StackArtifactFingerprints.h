// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The lights stacking stage's fingerprints: canonical JSON, its SHA-256, and the fingerprint of the files a stack
/// reads.
///
/// A stack is reused when its master's record names the fingerprint of the stack it would make now - its group's
/// settings, the files it reads as they are now, and the transforms and comet products that place them. These are the
/// pieces that fingerprint is assembled from; the planner computes the same one from its preparation snapshot.

#include "processes/ImageIntegration/model/FrameId.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <vector>

namespace ImageIntegration::detail
{

[[nodiscard]] QString sha256Hex(const QByteArray& bytes);
[[nodiscard]] QString sha256Hex(const QJsonObject& obj);

/// Recursively sort object keys so structurally equal JSON hashes identically.
[[nodiscard]] QJsonValue canonicalizeJson(const QJsonValue& value);

/// Canonicalize then hash - the fingerprint every artifact reuse check compares.
[[nodiscard]] QString fingerprintJson(const QJsonObject& obj);

[[nodiscard]] QJsonArray frameListToJson(const std::vector<FrameId>& frames);

/// What a stack's files are as they are now.
struct InputsFingerprint
{
    /// The fingerprint of every file's stamp - its path, size, modification time and platform identity.
    QString fingerprint;
    /// The latest modification time among them, in milliseconds since the epoch.
    qint64 maxMtimeMs = 0;
    /// How many of them are missing.
    int missingFiles = 0;
};

[[nodiscard]] InputsFingerprint fingerprintInputs(const std::vector<FrameId>& frames);

} // namespace ImageIntegration::detail
