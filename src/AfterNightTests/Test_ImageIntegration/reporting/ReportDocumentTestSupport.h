// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

/// @file
/// What the suites of the report's document share: a document read back as the tree of its elements.
///
/// The document is well-formed XML as well as HTML, so a suite reads it with an XML parser and asserts on its
/// structure - an id that exists once, a link that resolves, a table's cells - and never on its bytes.

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <cstddef>
#include <vector>

namespace an::tests::integration::report
{
/// One element of a parsed document.
struct DocumentElement
{
    QString name;
    QHash<QString, QString> attributes;

    /// The text inside it, its descendants' included, in document order.
    QString text;

    /// The place of its parent among the document's elements; -1 for the root.
    int parent = -1;

    [[nodiscard]] QString attribute(const char* key) const
    {
        return attributes.value(QLatin1String(key));
    }
    [[nodiscard]] bool has(const char* key) const
    {
        return attributes.contains(QLatin1String(key));
    }
    [[nodiscard]] bool hasClass(const char* wanted) const
    {
        return attribute("class").split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(QLatin1String(wanted));
    }
};

/// One run of a document's text: the characters between two tags, and the element they stand in.
struct DocumentTextRun
{
    QString text;

    /// The place of the element among the document's elements.
    int element = -1;
};

/// A document as its elements, in document order.
struct ParsedDocument
{
    std::vector<DocumentElement> elements;

    /// The document's text run by run, in document order, the style's and the script's left out. A table's cells
    /// are runs of their own, where `DocumentElement::text` reads them as one line.
    std::vector<DocumentTextRun> runs;

    /// Why the document is not well-formed; empty when it is.
    QString error;

    /// The elements that carry an id, by that id; an id written twice is listed twice.
    [[nodiscard]] std::vector<const DocumentElement*> withId(const QString& id) const
    {
        std::vector<const DocumentElement*> found;
        for (const DocumentElement& element : elements)
        {
            if (element.attributes.value(QStringLiteral("id")) == id)
            {
                found.push_back(&element);
            }
        }
        return found;
    }

    /// The one element of an id; null when there is none or more than one.
    [[nodiscard]] const DocumentElement* byId(const char* id) const
    {
        const std::vector<const DocumentElement*> found = withId(QLatin1String(id));
        return found.size() == 1 ? found.front() : nullptr;
    }

    [[nodiscard]] std::vector<const DocumentElement*> ofClass(const char* wanted) const
    {
        std::vector<const DocumentElement*> found;
        for (const DocumentElement& element : elements)
        {
            if (element.hasClass(wanted))
            {
                found.push_back(&element);
            }
        }
        return found;
    }

    [[nodiscard]] std::vector<const DocumentElement*> named(const char* name) const
    {
        std::vector<const DocumentElement*> found;
        for (const DocumentElement& element : elements)
        {
            if (element.name == QLatin1String(name))
            {
                found.push_back(&element);
            }
        }
        return found;
    }

    /// Whether an element lies inside another, however deep.
    [[nodiscard]] bool isInside(const DocumentElement& element, const DocumentElement& ancestor) const
    {
        for (int at = element.parent; at >= 0; at = elements[static_cast<std::size_t>(at)].parent)
        {
            if (&elements[static_cast<std::size_t>(at)] == &ancestor)
            {
                return true;
            }
        }
        return false;
    }

    /// The elements of a class inside an element.
    [[nodiscard]] std::vector<const DocumentElement*> ofClassInside(const DocumentElement& ancestor,
                                                                    const char* wanted) const
    {
        std::vector<const DocumentElement*> found;
        for (const DocumentElement* element : ofClass(wanted))
        {
            if (isInside(*element, ancestor))
            {
                found.push_back(element);
            }
        }
        return found;
    }

    /// Every id of the document, each as often as it is written.
    [[nodiscard]] QStringList ids() const
    {
        QStringList found;
        for (const DocumentElement& element : elements)
        {
            if (element.attributes.contains(QStringLiteral("id")))
            {
                found << element.attributes.value(QStringLiteral("id"));
            }
        }
        return found;
    }

    /// The document's text as a reader meets it: everything but its style and its script.
    [[nodiscard]] QString bodyText() const
    {
        for (const DocumentElement& element : elements)
        {
            if (element.name == QStringLiteral("body"))
            {
                return element.text;
            }
        }
        return {};
    }
};

/// Reads a document back.
[[nodiscard]] inline ParsedDocument parseDocument(const QString& markup)
{
    ParsedDocument parsed;
    QXmlStreamReader reader(markup);
    std::vector<int> open;
    while (!reader.atEnd())
    {
        switch (reader.readNext())
        {
        case QXmlStreamReader::StartElement: {
            DocumentElement element;
            element.name = reader.name().toString();
            const QXmlStreamAttributes attributes = reader.attributes();
            for (const QXmlStreamAttribute& attribute : attributes)
            {
                element.attributes.insert(attribute.name().toString(), attribute.value().toString());
            }
            element.parent = open.empty() ? -1 : open.back();
            open.push_back(static_cast<int>(parsed.elements.size()));
            parsed.elements.push_back(element);
            break;
        }
        case QXmlStreamReader::Characters: {
            const QString text = reader.text().toString();
            for (const int index : open)
            {
                parsed.elements[static_cast<std::size_t>(index)].text += text;
            }
            if (!open.empty())
            {
                const QString& inside = parsed.elements[static_cast<std::size_t>(open.back())].name;
                if (inside != QStringLiteral("style") && inside != QStringLiteral("script"))
                {
                    parsed.runs.push_back(DocumentTextRun{.text = text, .element = open.back()});
                }
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            open.pop_back();
            break;
        default:
            break;
        }
    }
    if (reader.hasError())
    {
        parsed.error = QStringLiteral("%1 at line %2, column %3")
                           .arg(reader.errorString())
                           .arg(reader.lineNumber())
                           .arg(reader.columnNumber());
    }
    return parsed;
}

/// The elements a document closes in their own tag where the rules do not let it, and the void elements it does
/// not: each as `<name>` with what is wrong. HTML reads `<div/>` as an open `div`, so only a void element may be
/// closed so - `br`, `hr`, `img`, `input`, `meta`, `link`, `col` - and, inside an `svg`, an element with no content.
[[nodiscard]] inline QStringList selfClosingFaults(QString markup)
{
    // The style and the script are not markup.
    static const QRegularExpression kRaw(QStringLiteral("<(style|script)>.*?</\\1>"),
                                         QRegularExpression::DotMatchesEverythingOption);
    markup.remove(kRaw);
    static const QStringList kVoid = {QStringLiteral("br"),    QStringLiteral("hr"),   QStringLiteral("img"),
                                      QStringLiteral("input"), QStringLiteral("meta"), QStringLiteral("link"),
                                      QStringLiteral("col")};
    static const QRegularExpression kTag(
        QStringLiteral("<(/?)([A-Za-z][A-Za-z0-9]*)((?:\\s+[A-Za-z_:][-A-Za-z0-9_:.]*=\"[^\"]*\")*)\\s*(/?)>"));
    QStringList faults;
    int svgDepth = 0;
    QRegularExpressionMatchIterator tags = kTag.globalMatch(markup);
    while (tags.hasNext())
    {
        const QRegularExpressionMatch tag = tags.next();
        const bool closing = !tag.captured(1).isEmpty();
        const QString name = tag.captured(2);
        const bool selfClosed = !tag.captured(4).isEmpty();
        if (closing)
        {
            svgDepth -= name == QStringLiteral("svg") ? 1 : 0;
            continue;
        }
        const bool isVoid = kVoid.contains(name);
        if (selfClosed && !isVoid && svgDepth == 0)
        {
            faults << QStringLiteral("<%1/> is closed in its own tag outside an svg").arg(name);
        }
        if (!selfClosed && isVoid)
        {
            faults << QStringLiteral("<%1> is a void element left open").arg(name);
        }
        if (!selfClosed && name == QStringLiteral("svg"))
        {
            ++svgDepth;
        }
    }
    return faults;
}
} // namespace an::tests::integration::report
