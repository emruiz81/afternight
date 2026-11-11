// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace ImageIntegration::Reporting
{
namespace
{
constexpr QChar kReplacementCharacter{0xFFFD};

/// The elements HTML gives no content, which are closed in their own tag.
constexpr std::array<std::string_view, 7> kVoidElements = {"br", "hr", "img", "input", "meta", "link", "col"};

[[nodiscard]] bool isVoidElement(const char* element)
{
    return std::find(kVoidElements.begin(), kVoidElements.end(), std::string_view(element)) != kVoidElements.end();
}

/// Whether a name is lower-case letters and digits, with hyphens inside where `hyphens` allows them.
[[nodiscard]] bool isPlainName(const char* name, const bool hyphens)
{
    if (name == nullptr || *name == '\0' || !(*name >= 'a' && *name <= 'z'))
    {
        return false;
    }
    for (const char* at = name; *at != '\0'; ++at)
    {
        const bool letter = *at >= 'a' && *at <= 'z';
        const bool digit = *at >= '0' && *at <= '9';
        if (!letter && !digit && !(hyphens && *at == '-'))
        {
            return false;
        }
    }
    return true;
}

/// One of the report's icons: its name and the paths drawn in a square of sixteen.
struct Icon
{
    std::string_view name;
    std::array<const char*, 2> paths;
};

constexpr std::array<Icon, 11> kIcons = {{
    {"good", {"M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Z", "m5.2 8.2 2 2 3.6-4"}},
    {"warning", {"M8 2 1.6 13.2h12.8L8 2Z", "M8 6.4v3.2M8 11.4v.2"}},
    {"critical", {"M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Z", "m5.8 5.8 4.4 4.4M10.2 5.8l-4.4 4.4"}},
    {"note", {"M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Z", "M8 7.2v4M8 4.8v.2"}},
    {"reused", {"M13.5 8a5.5 5.5 0 1 1-1.7-4", "M13.6 2.2v3h-3"}},
    {"skipped", {"M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Z", "M5 8h6"}},
    {"theme", {"M13.2 9.6A5.6 5.6 0 0 1 6.4 2.8a5.6 5.6 0 1 0 6.8 6.8Z", nullptr}},
    {"file", {"M4 1.8h5l3 3v9.4H4V1.8Z", "M9 1.8v3h3"}},
    {"clock", {"M8 1.5a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13Z", "M8 4.6V8l2.3 1.5"}},
    {"frames", {"M2 4.2h9.6v8.6H2V4.2Z", "M4.4 4.2V2.4H14v8.6h-2.4"}},
    {"chevron", {"m6 3.5 4.5 4.5L6 12.5", nullptr}},
}};

/// One of the report's two assets, with the line ends a document is written with.
[[nodiscard]] QString readAsset(const QString& name)
{
    QFile file(QStringLiteral(":/ImageIntegrationReport/") + name);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text;
}

/// What the document is of: its one target, or how many it has.
[[nodiscard]] QString subjectOf(const ReportModel& model)
{
    if (model.run.targets.size() == 1)
    {
        return model.run.targets.front();
    }
    if (model.run.targets.size() > 1)
    {
        return counted(model.run.targets.size(), QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 target"),
                       QT_TRANSLATE_NOOP("ImageIntegrationReport", "%1 targets"));
    }
    return {};
}

/// The contents: the report's name, the overview, then each part's heading and entries.
void writeContents(ReportMarkup& out, const ReportDocumentContext& context, const std::vector<ReportPart>& parts)
{
    const ReportModel& model = context.model;
    out.open("nav", {{"class", QStringLiteral("toc noprint")}, {"aria-label", documentText("Contents")}});
    out.open("div", {{"class", QStringLiteral("brand")}});
    out.element("b", documentText("Integration report"));
    QStringList under;
    if (const QString subject = subjectOf(model); !subject.isEmpty())
    {
        under << subject;
    }
    if (model.run.archivedAt.has_value())
    {
        under << formatDate(model.run.archivedAt->toTimeZone(context.zone.resolved()).date());
    }
    if (!under.isEmpty())
    {
        out.element("span", under.join(dotSeparator()));
    }
    out.close("div");

    out.element("a", {{"href", QStringLiteral("#top")}}, documentText("Overview"));
    for (const ReportPart& part : parts)
    {
        if (!part.isPresent())
        {
            continue;
        }
        out.element("p", {{"class", QStringLiteral("toc-h")}}, part.heading);
        for (const ReportContentsEntry& entry : part.entries)
        {
            out.open("a", {{"href", QStringLiteral("#") + entry.anchor}});
            out.element("span", entry.label);
            if (entry.attention)
            {
                out.icon("warning");
            }
            else if (!entry.count.isEmpty())
            {
                out.element("em", entry.count);
            }
            out.close("a");
        }
    }
    out.close("nav");
}

/// The document's last line: which application wrote it, of which run, and when.
void writeFooter(ReportMarkup& out, const ReportDocumentContext& context)
{
    const ReportRun& run = context.model.run;
    QStringList parts;
    parts << (run.applicationVersion.trimmed().isEmpty()
                  ? QStringLiteral("AfterNight")
                  : QStringLiteral("AfterNight %1").arg(run.applicationVersion.trimmed()));
    if (!run.executionId.trimmed().isEmpty())
    {
        parts << documentText("run %1").arg(run.executionId.trimmed());
    }
    if (run.archivedAt.has_value())
    {
        parts << documentText("archived %1 (%2)")
                     .arg(formatDateTime(*run.archivedAt, context.zone), formatUtcOffset(run.utcOffsetSeconds));
    }
    out.element("footer", parts.join(dotSeparator()));
}
} // namespace

// ---- The markup.

QString ReportMarkup::escaped(const QString& text, const bool attribute)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        const QChar character = text.at(index);
        const char16_t unit = character.unicode();
        switch (unit)
        {
        case u'&':
            out += QLatin1String("&amp;");
            continue;
        case u'<':
            out += QLatin1String("&lt;");
            continue;
        case u'>':
            out += QLatin1String("&gt;");
            continue;
        case u'"':
            out += QLatin1String("&quot;");
            continue;
        case u'\'':
            out += QLatin1String("&#39;");
            continue;
        case u'\t':
            out += attribute ? QString(QLatin1String("&#9;")) : QString(character);
            continue;
        case u'\n':
            out += attribute ? QString(QLatin1String("&#10;")) : QString(character);
            continue;
        case u'\r':
            // A carriage return is a line break to both parsers; in text it is left to the line feed beside it.
            if (attribute)
            {
                out += QLatin1String("&#13;");
            }
            else if (index + 1 >= text.size() || text.at(index + 1) != QLatin1Char('\n'))
            {
                out += QLatin1Char('\n');
            }
            continue;
        default:
            break;
        }
        if (unit < 0x20 || unit == 0xFFFE || unit == 0xFFFF)
        {
            out += kReplacementCharacter;
        }
        else if (character.isHighSurrogate())
        {
            if (index + 1 < text.size() && text.at(index + 1).isLowSurrogate())
            {
                out += character;
                out += text.at(index + 1);
                ++index;
            }
            else
            {
                out += kReplacementCharacter;
            }
        }
        else if (character.isLowSurrogate())
        {
            out += kReplacementCharacter;
        }
        else
        {
            out += character;
        }
    }
    return out;
}

void ReportMarkup::fault(const QString& what)
{
    m_faults << what;
}

void ReportMarkup::writeAttributes(const char* element, const Attributes attributes)
{
    for (const Attribute& attribute : attributes)
    {
        if (attribute.second.isNull())
        {
            continue;
        }
        if (!isPlainName(attribute.first, true))
        {
            fault(QStringLiteral("the attribute \"%1\" of <%2> is not a plain name")
                      .arg(QString::fromLatin1(attribute.first != nullptr ? attribute.first : ""),
                           QString::fromLatin1(element)));
            continue;
        }
        m_markup += QLatin1Char(' ');
        m_markup += QLatin1String(attribute.first);
        m_markup += QLatin1String("=\"");
        m_markup += escaped(attribute.second, true);
        m_markup += QLatin1Char('"');
    }
}

ReportMarkup& ReportMarkup::open(const char* element, const Attributes attributes)
{
    if (!isPlainName(element, false))
    {
        fault(QStringLiteral("\"%1\" is not an element's name")
                  .arg(QString::fromLatin1(element != nullptr ? element : "")));
        return *this;
    }
    if (isVoidElement(element))
    {
        fault(QStringLiteral("<%1> has no content and cannot be opened").arg(QString::fromLatin1(element)));
        return *this;
    }
    m_markup += QLatin1Char('<');
    m_markup += QLatin1String(element);
    writeAttributes(element, attributes);
    m_markup += QLatin1Char('>');
    m_open.push_back(element);
    return *this;
}

ReportMarkup& ReportMarkup::close(const char* element)
{
    if (m_open.empty())
    {
        fault(QStringLiteral("</%1> closes nothing").arg(QString::fromLatin1(element != nullptr ? element : "")));
        return *this;
    }
    if (element == nullptr || std::strcmp(m_open.back(), element) != 0)
    {
        fault(QStringLiteral("</%1> met where </%2> was due")
                  .arg(QString::fromLatin1(element != nullptr ? element : ""), QString::fromLatin1(m_open.back())));
        return *this;
    }
    m_markup += QLatin1String("</");
    m_markup += QLatin1String(element);
    m_markup += QLatin1Char('>');
    m_open.pop_back();
    return *this;
}

ReportMarkup& ReportMarkup::leaf(const char* element, const Attributes attributes)
{
    if (element == nullptr || !isVoidElement(element))
    {
        fault(QStringLiteral("<%1> is not a void element and cannot be closed in its own tag")
                  .arg(QString::fromLatin1(element != nullptr ? element : "")));
        return *this;
    }
    m_markup += QLatin1Char('<');
    m_markup += QLatin1String(element);
    writeAttributes(element, attributes);
    m_markup += QLatin1String("/>");
    return *this;
}

ReportMarkup& ReportMarkup::text(const QString& text)
{
    m_markup += escaped(text, false);
    return *this;
}

ReportMarkup& ReportMarkup::element(const char* element, const Attributes attributes, const QString& text)
{
    const std::size_t depth = m_open.size();
    open(element, attributes);
    if (m_open.size() > depth)
    {
        this->text(text);
        close(element);
    }
    return *this;
}

ReportMarkup& ReportMarkup::element(const char* element, const QString& text)
{
    return this->element(element, {}, text);
}

ReportMarkup& ReportMarkup::icon(const char* name, const QString& cssClass)
{
    const auto found = std::find_if(kIcons.begin(), kIcons.end(), [name](const Icon& candidate) {
        return name != nullptr && candidate.name == std::string_view(name);
    });
    if (found == kIcons.end())
    {
        fault(QStringLiteral("the report has no icon \"%1\"").arg(QString::fromLatin1(name != nullptr ? name : "")));
        return *this;
    }
    m_markup += QStringLiteral("<svg class=\"%1\" viewBox=\"0 0 16 16\" width=\"16\" height=\"16\" "
                               "aria-hidden=\"true\">")
                    .arg(cssClass.isEmpty() ? QStringLiteral("ic") : QStringLiteral("ic ") + escaped(cssClass, true));
    for (const char* path : found->paths)
    {
        if (path != nullptr)
        {
            m_markup += QStringLiteral("<path d=\"%1\"/>").arg(QLatin1String(path));
        }
    }
    m_markup += QLatin1String("</svg>");
    return *this;
}

ReportMarkup& ReportMarkup::chart(const QString& markup)
{
    m_markup += markup;
    return *this;
}

ReportMarkup& ReportMarkup::append(const ReportMarkup& other)
{
    if (!other.isBalanced())
    {
        fault(QStringLiteral("markup with <%1> still open was appended").arg(QString::fromLatin1(other.m_open.back())));
    }
    m_markup += other.m_markup;
    m_faults << other.m_faults;
    return *this;
}

// ---- Fragments the parts share.

void writePill(ReportMarkup& out, const char* kind, const QString& text)
{
    out.open("span", {{"class", QStringLiteral("pill is-") + QLatin1String(kind)}});
    out.icon(kind);
    out.element("span", text);
    out.close("span");
}

void writeTerm(ReportMarkup& out, const QString& term, const QString& meaning)
{
    out.element("span",
                {{"class", QStringLiteral("term")},
                 {"tabindex", QStringLiteral("0")},
                 {"data-tip", term + QLatin1Char('\n') + meaning}},
                term);
}

void writeScrollingChart(ReportMarkup& out, const QString& chart, const QString& label)
{
    out.open("div", {{"class", QStringLiteral("scrollx")},
                     {"tabindex", QStringLiteral("0")},
                     {"role", QStringLiteral("group")},
                     {"aria-label", label}});
    out.chart(chart);
    out.close("div");
}

void writeMasterLink(ReportMarkup& out, const ReportDocumentContext& context, const ReportMaster& master,
                     const QString& text)
{
    const QString anchor = QStringLiteral("file-") + master.slug;
    if (context.hasAnchor(anchor))
    {
        out.element("a", {{"href", QStringLiteral("#") + anchor}}, text);
    }
    else
    {
        out.text(text);
    }
}

void writeSentence(ReportMarkup& out, const QString& pattern, const QStringList& values)
{
    static const QRegularExpression kPlace(QStringLiteral("%([1-9])"));
    qsizetype from = 0;
    QRegularExpressionMatchIterator places = kPlace.globalMatch(pattern);
    while (places.hasNext())
    {
        const QRegularExpressionMatch place = places.next();
        out.text(pattern.mid(from, place.capturedStart() - from));
        const int index = place.captured(1).toInt() - 1;
        if (index >= 0 && index < values.size())
        {
            out.element("b", values.at(index));
        }
        from = place.capturedEnd();
    }
    out.text(pattern.mid(from));
}

void openTable(ReportMarkup& out, const QString& cssClass, const QString& label,
               const std::vector<ReportColumn>& columns)
{
    out.open("div", {{"class", QStringLiteral("tablewrap")},
                     {"tabindex", QStringLiteral("0")},
                     {"role", QStringLiteral("group")},
                     {"aria-label", label}});
    out.open("table", {{"class", cssClass.isEmpty() ? QString() : cssClass}});
    out.open("thead");
    out.open("tr");
    for (const ReportColumn& column : columns)
    {
        out.open("th", {{"scope", QStringLiteral("col")}, {"class", column.right ? QStringLiteral("r") : QString()}});
        if (column.meaning.isEmpty())
        {
            out.text(column.heading);
        }
        else
        {
            writeTerm(out, column.heading, column.meaning);
        }
        out.close("th");
    }
    out.close("tr");
    out.close("thead");
    out.open("tbody");
}

void closeTable(ReportMarkup& out)
{
    out.close("tbody");
    out.close("table");
    out.close("div");
}

QString dotSeparator()
{
    return QStringLiteral(" · ");
}

// ---- The document.

QString reportStylesheet()
{
    return readAsset(QStringLiteral("report.css"));
}

QString reportScript()
{
    return readAsset(QStringLiteral("report.js"));
}

ReportDocument writeReportDocument(const ReportModel& model, const ReportDocumentOptions& options)
{
    ReportAssets assets(options.form, options.runDirectory);
    assets.hideFoldersOf(model);

    ReportDocumentContext context{
        .model = model,
        .assets = assets,
        .zone = ReportTimeZone{.id = model.run.timeZoneId, .utcOffsetSeconds = model.run.utcOffsetSeconds},
        .anchors = {}};
    context.anchors.insert(QStringLiteral("top"));
    for (const QString& anchor : summaryAnchors(model))
    {
        context.anchors.insert(anchor);
    }

    const ReportMarkup header = writeReportHeader(context);
    std::vector<ReportPart> parts;
    parts.push_back(writeReportSummary(context));

    ReportMarkup body;
    body.open("div", {{"class", QStringLiteral("shell")}});
    writeContents(body, context, parts);
    body.open("main");
    body.append(header);
    for (const ReportPart& part : parts)
    {
        if (part.isPresent())
        {
            body.append(part.body);
        }
    }
    writeFooter(body, context);
    body.close("main");
    body.close("div");

    QStringList faults = body.faults();
    if (!body.isBalanced())
    {
        faults << QStringLiteral("the document's body was left with an element open");
    }

    const QString subject = subjectOf(model);
    const QString title = subject.isEmpty() ? documentText("Integration report")
                                            : subject + dotSeparator() + documentText("Integration report");
    const QString generator = model.run.applicationVersion.trimmed().isEmpty()
                                  ? QStringLiteral("AfterNight")
                                  : QStringLiteral("AfterNight %1").arg(model.run.applicationVersion.trimmed());

    // The style and the script are the report's own assets, each inside a CDATA section a comment hides from the
    // other parser. Nothing of the run is written inside either.
    QString html;
    html.reserve(body.markup().size() + 65536);
    html += QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\"/>\n"
                           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
                           "<meta name=\"color-scheme\" content=\"dark light\"/>\n");
    html += QStringLiteral("<meta name=\"generator\" content=\"%1\"/>\n").arg(ReportMarkup::escaped(generator, true));
    html += QStringLiteral("<title>%1</title>\n").arg(ReportMarkup::escaped(title));
    html += QStringLiteral("<style>/*<![CDATA[*/\n");
    html += reportStylesheet();
    html += QStringLiteral("/*]]>*/</style>\n<script>//<![CDATA[\n");
    html += reportScript();
    html += QStringLiteral("//]]></script>\n</head>\n<body>\n");
    html += body.markup();
    html += QStringLiteral("\n</body>\n</html>\n");

    return ReportDocument{.html = html, .faults = faults};
}
} // namespace ImageIntegration::Reporting
