// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightProcessesExport.h"

/// @file
/// What kind of frame an imported file is: the kind its own cards state, and only when they state none, the kind the
/// folder it sits in suggests, and only when neither says, the kind the caller imports it as.
///
/// The file is the authority. A capture program writes the kind into a card - `IMAGETYP`, INDI's `FRAME`, an XISF
/// image's `imageType` - and the engine reads all of them through `readFrameTypeCards`. A folder named `darks` is a
/// person's filing, which a light frame copied into it does not change, so the folder is a hint consulted after the
/// cards rather than before them. Reading the file is the caller's: detection is a function of the cards a probe
/// returned and the path, and reads nothing itself.

#include "processes/ImageIntegration/model/FrameType.h"

#include <QString>

#include <optional>

namespace an
{
class CodecMetadata;
} // namespace an

namespace ImageIntegration
{

/// Which statement decided a frame's kind.
enum class FrameTypeEvidence
{
    /// A card of the file stated it.
    Card,

    /// No card stated a kind, and a folder on the file's path is named after one.
    Folder,

    /// Neither said; the frame is the kind the caller imports it as.
    Fallback
};

/// A frame's kind and what decided it.
struct FrameTypeDetection
{
    /// The kind.
    FrameType type = FrameType::Light;

    /// What decided it.
    FrameTypeEvidence evidence = FrameTypeEvidence::Fallback;

    /// The deciding card's keyword (`IMAGETYP`, `FRAME` or `ImageType`) or the deciding folder's name; empty for a
    /// fallback.
    QString source;

    /// The deciding card's value as the file wrote it; empty unless a card decided.
    QString spelling;
};

/**
 * @class FrameDetection
 * @brief Detects a frame's kind from the cards a probe read and from its path.
 */
class AFTERNIGHTPROCESSES_API FrameDetection
{
public:
    /**
     * @brief The kind the folders on a path are named after, the folder nearest the file first.
     *
     * `bias`, `biases`, `offset` and `offsets` name a bias; `dark` and `darks` a dark; `flat` and `flats` a flat;
     * `light` and `lights` a light; the comparison ignores case. Both separators are accepted on every platform. The
     * file's own name is not read.
     *
     * @param filePath the file's path; a relative path is read from the current directory, as a probe would open it.
     * @param folderName where to write the name of the folder that named the kind, when not null.
     * @return the kind; nothing when no folder is named after a kind.
     */
    [[nodiscard]] static std::optional<FrameType> folderHint(const QString& filePath, QString* folderName = nullptr);

    /**
     * @brief A frame's kind: its cards first, then its folders, then the caller's fallback.
     *
     * @param filePath the file's path.
     * @param cards the cards a probe of the file read; null when the file could not be probed.
     * @param fallback the kind the caller imports a frame as when nothing states one.
     * @return the kind and what decided it.
     */
    [[nodiscard]] static FrameTypeDetection detect(const QString& filePath, const an::CodecMetadata* cards,
                                                   FrameType fallback);

    /**
     * @brief The kind `detect` answers, without what decided it.
     */
    [[nodiscard]] static FrameType detectType(const QString& filePath, const an::CodecMetadata* cards,
                                              FrameType fallback);
};

} // namespace ImageIntegration
