// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The pictures and the paths of a report's document, in each of its two forms.
///
/// A report is written in one of two forms from one writer. The **linked** form is the one a run keeps: it sits in the
/// run's folder beside `report_previews/` and names each picture by its path under that folder. The **embedded** form
/// is the one a person sends: every picture is inside the document, bounded so the file stays small enough to send,
/// and a path is printed as a file's name without its folders, since a folder is noise to a stranger and says more
/// than its owner may want. A document asks `ReportAssets` for every picture and every path it prints, so no part of
/// it decides the form for itself.

#include "AfterNightProcessesExport.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace ImageIntegration::Reporting
{
struct ReportModel;

/// The form a report's document is written in.
enum class ReportForm
{
    /// Pictures are files beside the document, named by their paths under the run's folder.
    Linked,

    /// Pictures are inside the document, and paths are printed as names.
    Embedded
};

/// The longest side, in pixels, of a picture inside an embedded document.
inline constexpr int kEmbeddedImageLongestSide = 1280;

/// The longest side, in pixels, of a picture an embedded document shows small.
inline constexpr int kEmbeddedThumbnailLongestSide = 480;

/// The JPEG quality a picture inside an embedded document is written at.
inline constexpr int kEmbeddedImageQuality = 82;

/// A picture read from a file, reduced so that neither side is longer than `longestSide`, and written again as a
/// JPEG. A picture already within the bound keeps its size; it is still written again, so the result's quality is
/// the one asked for.
///
/// @param file the picture's file.
/// @param longestSide the bound, in pixels.
/// @param quality the JPEG quality, 0 to 100.
/// @return the JPEG's bytes; empty when the file is not there or is no picture.
[[nodiscard]] AFTERNIGHTPROCESSES_API QByteArray boundedJpeg(const QString& file, int longestSide, int quality);

/// What a document of one form writes for a picture, a path and a message of the run.
class AFTERNIGHTPROCESSES_API ReportAssets
{
public:
    /// @param form the document's form.
    /// @param runDirectory the run's folder under `history/`, which the model's picture paths are relative to.
    ReportAssets(ReportForm form, QString runDirectory);

    /// Names the folders an embedded document must not print: the run's output folder, and the folder of every file
    /// the model names. A linked document prints paths as they are and needs none.
    void hideFoldersOf(const ReportModel& model);

    [[nodiscard]] ReportForm form() const noexcept
    {
        return m_form;
    }

    /// What an `img` element takes as its source.
    ///
    /// Linked: the path as given, with forward slashes. Embedded: a `data:` address holding the picture bounded to
    /// `kEmbeddedImageLongestSide` and written as a JPEG of `kEmbeddedImageQuality`; a picture asked for twice is
    /// read once.
    ///
    /// @param relativePath the picture's path under the run's folder, as the model states it.
    /// @return the source; empty when the model states no picture, or when an embedded document cannot read it -
    ///     an embedded document never names a file outside itself.
    [[nodiscard]] QString image(const QString& relativePath);

    /// What an `img` element takes as its source where the picture is shown small: on a card of the gallery, in
    /// the heading of a master's file, in a row of a table.
    ///
    /// Linked: the same file `image` names, which the browser reduces. Embedded: a `data:` address holding the
    /// picture bounded to `kEmbeddedThumbnailLongestSide`, so that a picture shown small and whole in one document
    /// is not carried in it twice at its whole size.
    ///
    /// @param relativePath the picture's path under the run's folder, as the model states it.
    /// @return the source; empty when the model states no picture or an embedded document cannot read it.
    [[nodiscard]] QString thumbnail(const QString& relativePath);

    /// A file's path as the document prints it: as given in a linked document, its name alone in an embedded one.
    [[nodiscard]] QString path(const QString& path) const;

    /// A message of the run as the document prints it. In an embedded document the output folder's path is replaced
    /// by the folder's name, and the folder of every file the model names is dropped, in either spelling of the
    /// separator.
    [[nodiscard]] QString message(const QString& text) const;

private:
    ReportForm m_form;
    QString m_runDirectory;

    /// The output folder's path and what stands in its place.
    QString m_outputFolder;
    QString m_outputFolderName;

    /// The folders dropped from a message, longest first.
    QStringList m_hiddenFolders;

    /// A picture's source inside an embedded document, bounded to a longest side; a picture is read once per bound.
    [[nodiscard]] QString embedded(const QString& relative, int longestSide);

    QHash<QString, QString> m_embedded;
};
} // namespace ImageIntegration::Reporting
