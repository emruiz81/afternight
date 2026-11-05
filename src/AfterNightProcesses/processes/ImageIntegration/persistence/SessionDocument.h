// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/**
 * @file SessionDocument.h
 * @brief The integration session's stored document: format 3, every engine-facing choice by its stable key.
 *
 * The model keeps each choice as the text its dialog shows or the integer its enumerator states. The stored document
 * states its format - `sessionSchema: 3` - and stores each such choice as the key `PersistedChoiceMaps.h`'s fixed
 * tables give it, so a renamed label or a reordered enumerator cannot change a saved session, and each set of image
 * points in engine pixel coordinates with its convention stated.
 *
 * Reading is strict. A document that states another format, or none, is refused with the format found and the one this
 * version reads; a value no table knows, or a set of image points that states no convention, is refused with the field
 * named, so a caller keeps the stored document as it is rather than guessing. The same document is the session
 * database's row, a preset's body and a project file.
 */
#pragma once

#include "AfterNightProcessesExport.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace ImageIntegration
{

/// The key a stored document states its format under.
inline constexpr char kSessionSchemaKey[] = "sessionSchema";

/// What reading or writing a document gave.
struct SessionDocumentResult
{
    /// The document - the model's own JSON for a read, the stored document for a write - or nothing when refused.
    std::optional<QJsonObject> document;

    /// Why it was refused, naming the format found or the field and the value; empty when it was not.
    QString refusal;

    /// The format the stored document states: zero for one that states none.
    int storedSchema = 0;

    [[nodiscard]] bool ok() const noexcept
    {
        return document.has_value();
    }
};

/// The format a stored document states.
///
/// @param stored the stored document.
/// @return the format, zero when it states none, or -1 when the stated format is not a positive integer.
[[nodiscard]] AFTERNIGHTPROCESSES_API int storedSessionSchema(const QJsonObject& stored);

/// The sentence a refusal of another format's document says: "The <context> was written in format 2; this version of
/// AfterNight reads format 3." or that it states no format version.
///
/// @param context what the document is: "session 'name'", "preset 'name'" or "project 'name'".
/// @param storedSchema the format the document states, zero for none or -1 for one that cannot be read.
/// @return the sentence.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString sessionFormatRefusal(const QString& context, int storedSchema);

/// A stored format 3 document read into the model's JSON: each key the dialog's text or the enumerator's integer again.
///
/// @param stored the stored document.
/// @param context what the document is, for a refusal: "session 'name'" or "preset 'name'".
/// @return the model's JSON, or the refusal.
[[nodiscard]] AFTERNIGHTPROCESSES_API SessionDocumentResult readSessionDocument(const QJsonObject& stored,
                                                                                const QString& context);

/// The model's JSON as a format 3 document.
///
/// @param model the model's JSON.
/// @param context what the document is, for a refusal.
/// @return the stored document, or the refusal of a value no table knows.
[[nodiscard]] AFTERNIGHTPROCESSES_API SessionDocumentResult writeSessionDocument(const QJsonObject& model,
                                                                                 const QString& context);

} // namespace ImageIntegration
