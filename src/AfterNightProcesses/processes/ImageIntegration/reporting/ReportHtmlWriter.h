// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// The integration report's document: one HTML file written from a report model.
///
/// The document is markup that is well-formed XML and valid HTML at once, so a suite reads it back with an XML
/// parser and asserts on its structure, and a browser shows it. `ReportMarkup` is the one way a part of the document
/// is written, and it keeps the rules that make that true, so no part can break them by hand:
///
/// - every element has an end tag, except the void ones (`br`, `hr`, `img`, `input`, `meta`, `link`, `col`), which
///   are closed in their own tag; no other element is, outside an `svg`;
/// - an attribute's value is quoted and escaped, and so is text;
/// - a character XML forbids - a control character other than a tab and a line break - is replaced by U+FFFD;
/// - no named entity is written but the five XML has.
///
/// Inside an `svg` an element with no content is closed in its own tag, which HTML allows there and nowhere else:
/// the charts are written that way (`ReportCharts.h`).
///
/// The document's style and script are the report's own two assets, put in `style` and `script` as they are. Nothing
/// a run wrote is ever put inside either: a run's words are text and attribute values only.
///
/// `writeReportDocument` writes the whole document: the shell, the contents made from the parts that are present,
/// and the parts. A part that has nothing to say is absent, and so is its entry in the contents.

#include "AfterNightProcessesExport.h"

#include "processes/ImageIntegration/reporting/ReportAssets.h"

#include <QString>
#include <QStringList>

#include <initializer_list>
#include <utility>
#include <vector>

namespace ImageIntegration::Reporting
{
struct ReportModel;

/// Markup under the document's rules. Elements are opened and closed by name; what the rules forbid is refused and
/// named among `faults()` rather than written.
class AFTERNIGHTPROCESSES_API ReportMarkup
{
public:
    /// An attribute and its value. An attribute whose value is a null string is left out, which is how a caller
    /// states one only sometimes; an empty value that is not null is written (`alt=""`).
    using Attribute = std::pair<const char*, QString>;
    using Attributes = std::initializer_list<Attribute>;

    /// Opens an element.
    ///
    /// @param element its name: lower-case letters and digits. A void element is refused: it has no content.
    /// @param attributes its attributes, each name lower-case letters, digits and hyphens.
    ReportMarkup& open(const char* element, Attributes attributes = {});

    /// Closes the innermost open element, which must be the one named.
    ReportMarkup& close(const char* element);

    /// Writes a void element - `br`, `hr`, `img`, `input`, `meta`, `link`, `col` - closed in its own tag. Any other
    /// name is refused.
    ReportMarkup& leaf(const char* element, Attributes attributes = {});

    /// Writes text, escaped.
    ReportMarkup& text(const QString& text);

    /// Opens an element, writes text into it and closes it.
    ReportMarkup& element(const char* element, Attributes attributes, const QString& text);
    ReportMarkup& element(const char* element, const QString& text);

    /// Writes one of the report's icons, by name: an `svg` of sixteen pixels hidden from a reader who cannot see it,
    /// since the words beside it say the same. An unknown name is refused.
    ///
    /// @param name `good`, `warning`, `critical`, `note`, `reused`, `skipped`, `theme`, `file`, `clock`, `frames`,
    ///     `chevron`.
    /// @param cssClass a class beside `ic`; may be empty.
    ReportMarkup& icon(const char* name, const QString& cssClass = {});

    /// Writes a chart or a legend as `ReportCharts` returned it. Those functions keep the same rules, so their
    /// markup is taken as it is; nothing else may be handed here.
    ReportMarkup& chart(const QString& markup);

    /// Writes other markup made under the same rules. The other markup must have every element closed.
    ReportMarkup& append(const ReportMarkup& other);

    /// Whether every element opened has been closed.
    [[nodiscard]] bool isBalanced() const noexcept
    {
        return m_open.empty();
    }

    /// Whether nothing was written.
    [[nodiscard]] bool isEmpty() const noexcept
    {
        return m_markup.isEmpty();
    }

    /// What was refused, each in a sentence; empty when the rules were kept.
    [[nodiscard]] const QStringList& faults() const noexcept
    {
        return m_faults;
    }

    /// The markup written so far.
    [[nodiscard]] const QString& markup() const noexcept
    {
        return m_markup;
    }

    /// Text as the markup holds it: the five characters XML reserves as references, a character XML forbids as
    /// U+FFFD, and in an attribute a tab and a line break as references too, since a parser reads each as a space.
    [[nodiscard]] static QString escaped(const QString& text, bool attribute = false);

private:
    void writeAttributes(const char* element, Attributes attributes);
    void fault(const QString& what);

    QString m_markup;
    std::vector<const char*> m_open;
    QStringList m_faults;
};

/// How a document is written.
struct ReportDocumentOptions
{
    ReportForm form = ReportForm::Linked;

    /// The run's folder under `history/`, which the model's picture paths are relative to. An embedded document
    /// reads its pictures from it; without it an embedded document has none.
    QString runDirectory;
};

/// A document, and what its writer refused.
struct ReportDocument
{
    /// The document, from its doctype to its last tag.
    QString html;

    /// What the markup rules refused while it was written; empty for a document written as meant.
    QStringList faults;
};

/// Writes a report's document from its model. The same model and the same options give the same document: nothing
/// reads a clock or a counter.
[[nodiscard]] AFTERNIGHTPROCESSES_API ReportDocument writeReportDocument(const ReportModel& model,
                                                                         const ReportDocumentOptions& options);

/// The report's stylesheet, as the document carries it in its `style` element.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString reportStylesheet();

/// The report's script, as the document carries it in its `script` element. It shows a tip, switches the theme,
/// sorts a table and opens a folded section; it computes no figure and reaches no network.
[[nodiscard]] AFTERNIGHTPROCESSES_API QString reportScript();
} // namespace ImageIntegration::Reporting
