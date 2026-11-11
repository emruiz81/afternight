// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "processes/ImageIntegration/reporting/ReportHtmlWriter.h"

#include "processes/ImageIntegration/reporting/ReportHtmlParts.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
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

void writeAnchorLink(ReportMarkup& out, const ReportDocumentContext& context, const QString& anchor,
                     const QString& text)
{
    if (!anchor.isEmpty() && context.hasAnchor(anchor))
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

void openPart(ReportMarkup& out, const char* anchor, const int number, const QString& title, const QString& lead)
{
    out.open("section", {{"class", QStringLiteral("part")}, {"id", QLatin1String(anchor)}});
    out.open("div", {{"class", QStringLiteral("parthead")}});
    out.element("span", {{"class", QStringLiteral("eyebrow")}}, documentText("Part %1").arg(number));
    out.element("h2", title);
    if (!lead.isEmpty())
    {
        out.element("p", lead);
    }
    out.close("div");
}

void openBlock(ReportMarkup& out, const char* anchor, const QString& title, const QString& aside, const QString& lead)
{
    out.open("section", {{"class", QStringLiteral("block")}, {"id", QLatin1String(anchor)}});
    out.open("h3");
    out.text(title);
    if (!aside.isEmpty())
    {
        out.text(QStringLiteral(" "));
        out.element("small", aside);
    }
    out.close("h3");
    if (!lead.isEmpty())
    {
        out.element("p", {{"class", QStringLiteral("lead")}}, lead);
    }
}

void openCard(ReportMarkup& out, const QString& title, const QString& aside)
{
    out.open("div", {{"class", QStringLiteral("card")}});
    if (title.isEmpty())
    {
        return;
    }
    out.open("h4");
    out.element("span", title);
    if (!aside.isEmpty())
    {
        out.element("small", aside);
    }
    out.close("h4");
}

void writePanelTitle(ReportMarkup& out, const char* heading, const QString& title, const QString& aside)
{
    out.open(heading, {{"class", QStringLiteral("ptitle")}});
    out.element("span", title);
    if (!aside.isEmpty())
    {
        out.element("small", aside);
    }
    out.close(heading);
}

void openPanel(ReportMarkup& out, const char* heading, const QString& title, const QString& aside)
{
    out.open("div", {{"class", QStringLiteral("inner")}});
    if (!title.isEmpty())
    {
        writePanelTitle(out, heading, title, aside);
    }
}

void writeCaption(ReportMarkup& out, const QString& text)
{
    out.element("p", {{"class", QStringLiteral("cap")}}, text);
}

void openFold(ReportMarkup& out, const QString& title, const QString& aside)
{
    out.open("details", {{"class", QStringLiteral("fold")}});
    out.open("summary");
    out.icon("chevron", QStringLiteral("chev"));
    out.element("span", title);
    if (!aside.isEmpty())
    {
        out.element("small", aside);
    }
    out.close("summary");
}

void closeFold(ReportMarkup& out)
{
    out.close("details");
}

void openFacts(ReportMarkup& out)
{
    out.open("dl", {{"class", QStringLiteral("facts")}});
}

void openFact(ReportMarkup& out, const QString& name, const QString& meaning)
{
    out.open("div");
    out.open("dt");
    if (meaning.isEmpty())
    {
        out.text(name);
    }
    else
    {
        writeTerm(out, name, meaning);
    }
    out.close("dt");
    out.open("dd");
}

void closeFact(ReportMarkup& out)
{
    out.close("dd");
    out.close("div");
}

void writeFact(ReportMarkup& out, const QString& name, const QString& value, const QString& meaning)
{
    if (value.trimmed().isEmpty())
    {
        return;
    }
    openFact(out, name, meaning);
    out.text(value);
    closeFact(out);
}

void closeFacts(ReportMarkup& out)
{
    out.close("dl");
}

void writeFacts(ReportMarkup& out, const std::vector<ReportFact>& facts)
{
    const bool any =
        std::any_of(facts.begin(), facts.end(), [](const ReportFact& fact) { return !fact.value.trimmed().isEmpty(); });
    if (!any)
    {
        return;
    }
    openFacts(out);
    for (const ReportFact& fact : facts)
    {
        writeFact(out, fact.name, fact.value, fact.meaning);
    }
    closeFacts(out);
}

// ---- Settings.

namespace
{
/// What a setting is counted under: its block's title and its own name.
[[nodiscard]] QString settingKey(const QString& block, const QString& setting)
{
    return block + QChar(0x001F) + setting;
}
} // namespace

void ReportSettingsMajority::add(const std::vector<ReportSettingsBlock>& blocks)
{
    ++m_groups;
    for (const ReportSettingsBlock& block : blocks)
    {
        for (const ReportSettingsRow& row : block.rows)
        {
            ++m_uses[settingKey(block.title, row.name)][row.value];
        }
    }
}

std::optional<QString> ReportSettingsMajority::common(const QString& block, const QString& setting) const
{
    const auto uses = m_uses.constFind(settingKey(block, setting));
    if (uses == m_uses.constEnd())
    {
        return std::nullopt;
    }
    for (auto value = uses->constBegin(); value != uses->constEnd(); ++value)
    {
        // More than half of the stage's groups, the ones that do not state the setting counted among them.
        if (value.value() * 2 > m_groups)
        {
            return value.key();
        }
    }
    return std::nullopt;
}

void openSettings(ReportMarkup& out, const QString& title, const char* heading)
{
    out.open("div", {{"class", QStringLiteral("setcard")}});
    out.open(heading, {{"class", QStringLiteral("ptitle")}});
    out.element("span", title);
    out.close(heading);
    out.open("dl");
}

void closeSettings(ReportMarkup& out)
{
    out.close("dl");
    out.close("div");
}

void writeSettingLink(ReportMarkup& out, const ReportDocumentContext& context, const QString& name,
                      const QString& anchor, const QString& text, const QString& note)
{
    out.open("div");
    out.element("dt", name);
    out.open("dd");
    if (text.trimmed().isEmpty())
    {
        out.element("span", {{"class", QStringLiteral("muted")}}, documentText("none"));
    }
    else
    {
        writeAnchorLink(out, context, anchor, text);
    }
    if (!note.isEmpty())
    {
        out.text(QStringLiteral(" "));
        out.element("span", {{"class", QStringLiteral("muted")}}, note);
    }
    out.close("dd");
    out.close("div");
}

void writeSettingsBlock(ReportMarkup& out, const ReportDocumentContext& context, const ReportSettingsBlock& block,
                        const QString& title, const char* heading, const ReportSettingsMajority* majority)
{
    openSettings(out, title.isEmpty() ? block.title : title, heading);
    writeSettingRows(out, context, block, majority);
    closeSettings(out);
}

void writeSettingRows(ReportMarkup& out, const ReportDocumentContext& context, const ReportSettingsBlock& block,
                      const ReportSettingsMajority* majority)
{
    for (const ReportSettingsRow& row : block.rows)
    {
        const QString value = context.said(row.value);
        const std::optional<QString> common =
            majority != nullptr ? majority->common(block.title, row.name) : std::nullopt;
        const bool differs = common.has_value() && *common != row.value;

        QStringList tip;
        if (differs)
        {
            tip << documentText("This group: %1").arg(value.isEmpty() ? noValueText() : value)
                << documentText("Most groups: %1").arg(common->isEmpty() ? noValueText() : context.said(*common));
        }
        if (!row.requested.trimmed().isEmpty())
        {
            tip << documentText("Asked for: %1").arg(context.said(row.requested));
            if (!row.reason.trimmed().isEmpty())
            {
                tip << context.said(row.reason);
            }
        }

        out.open("div");
        out.element("dt", row.name);
        out.open("dd", {{"class", differs ? QStringLiteral("diff") : QString()},
                        {"tabindex", tip.isEmpty() ? QString() : QStringLiteral("0")},
                        {"data-tip",
                         tip.isEmpty() ? QString() : row.name + QLatin1Char('\n') + tip.join(QLatin1Char('\n'))}});
        if (differs)
        {
            out.element("span", {{"class", QStringLiteral("ne")}}, QStringLiteral("≠"));
            out.text(QStringLiteral(" "));
        }
        out.text(value.isEmpty() ? noValueText() : value);
        out.close("dd");
        out.close("div");
    }
}

void writeMarkKey(ReportMarkup& out, const QString& text)
{
    out.open("p", {{"class", QStringLiteral("keyline")}});
    out.element("span", {{"class", QStringLiteral("ne")}}, QStringLiteral("≠"));
    out.text(QStringLiteral(" "));
    out.text(text);
    out.close("p");
}

// ---- The groups of a model, by key.

const ReportMaster* masterOfKey(const ReportModel& model, const QString& key)
{
    const auto found = std::find_if(model.masters.begin(), model.masters.end(),
                                    [&key](const ReportMaster& master) { return master.key == key; });
    return found == model.masters.end() ? nullptr : &*found;
}

const ReportLightGroup* lightGroupOfKey(const ReportModel& model, const QString& key)
{
    const auto found = std::find_if(model.lightGroups.begin(), model.lightGroups.end(),
                                    [&key](const ReportLightGroup& group) { return group.key == key; });
    return found == model.lightGroups.end() ? nullptr : &*found;
}

const ReportRegistrationGroup* registrationOfKey(const ReportModel& model, const QString& key)
{
    const auto found = std::find_if(model.registrationGroups.begin(), model.registrationGroups.end(),
                                    [&key](const ReportRegistrationGroup& group) { return group.key == key; });
    return found == model.registrationGroups.end() ? nullptr : &*found;
}

const ReportCalibrationMaster* calibrationOfSlug(const ReportModel& model, const QString& slug)
{
    if (slug.isEmpty())
    {
        return nullptr;
    }
    const auto found = std::find_if(model.calibrationMasters.begin(), model.calibrationMasters.end(),
                                    [&slug](const ReportCalibrationMaster& master) { return master.slug == slug; });
    return found == model.calibrationMasters.end() ? nullptr : &*found;
}

// ---- Lights and masters, in words.

QString ReportDocumentContext::said(const QString& text) const
{
    QString shown = assets.message(text);
    if (!shown.contains(QLatin1Char('|')))
    {
        return shown;
    }
    // A group's key reads as nothing to a person: where the run wrote one, the group's name stands. The longest key
    // first, since one group's key may begin another's.
    std::vector<std::pair<QString, QString>> named;
    for (const ReportMaster& master : model.masters)
    {
        named.emplace_back(master.key, master.name);
    }
    for (const ReportRegistrationGroup& group : model.registrationGroups)
    {
        named.emplace_back(group.key, group.name);
    }
    std::stable_sort(named.begin(), named.end(),
                     [](const auto& left, const auto& right) { return left.first.size() > right.first.size(); });
    for (const auto& [key, name] : named)
    {
        if (key.contains(QLatin1Char('|')) && !name.isEmpty())
        {
            shown.replace(key, name);
        }
    }
    return shown;
}

ChartState chartStateOf(const ReportFrame& frame)
{
    if (!frame.state.has_value())
    {
        return ChartState::Used;
    }
    switch (*frame.state)
    {
    case ReportFrameState::Used:
        return ChartState::Used;
    case ReportFrameState::ExcludedByUser:
    case ReportFrameState::StackExcludedByUser:
        return ChartState::ExcludedByUser;
    case ReportFrameState::RegistrationFailed:
        return ChartState::RegistrationFailed;
    case ReportFrameState::LeftOutByStack:
        return ChartState::LeftOutByStack;
    case ReportFrameState::ExcludedOnRestack:
        return ChartState::ExcludedOnRestack;
    case ReportFrameState::NotStacked:
        return ChartState::NotStacked;
    }
    return ChartState::Used;
}

QString stateName(const ChartState state)
{
    switch (state)
    {
    case ChartState::Used:
        return documentText("Used");
    case ChartState::ExcludedByUser:
        return documentText("Excluded by you");
    case ChartState::RegistrationFailed:
        return documentText("Registration failed");
    case ChartState::LeftOutByStack:
        return documentText("Left out by the stack");
    case ChartState::ExcludedOnRestack:
        return documentText("Excluded on the restack");
    case ChartState::NotStacked:
        return documentText("Not stacked");
    }
    return {};
}

QString stateClass(const ChartState state)
{
    switch (state)
    {
    case ChartState::Used:
        return QStringLiteral("s-used");
    case ChartState::ExcludedByUser:
        return QStringLiteral("s-user");
    case ChartState::RegistrationFailed:
        return QStringLiteral("s-registration");
    case ChartState::LeftOutByStack:
        return QStringLiteral("s-stack");
    case ChartState::ExcludedOnRestack:
        return QStringLiteral("s-restack");
    case ChartState::NotStacked:
        return QStringLiteral("s-notstacked");
    }
    return {};
}

void writeStateName(ReportMarkup& out, const ChartState state)
{
    out.element("i", {{"class", QStringLiteral("dot ") + stateClass(state)}}, QString());
    out.text(stateName(state));
}

QString fateLine(const ReportFrame& frame)
{
    if (!frame.state.has_value())
    {
        return documentText("Fate not recorded");
    }
    const QString name = stateName(chartStateOf(frame));
    return frame.stateDetail.trimmed().isEmpty() ? name : name + QStringLiteral(": ") + frame.stateDetail.trimmed();
}

ReportStateWords outcomeWords(const ReportOutcome outcome)
{
    switch (outcome)
    {
    case ReportOutcome::Completed:
        return {"good", documentText("Completed")};
    case ReportOutcome::CompletedWithWarnings:
        return {"warning", documentText("Completed with warnings")};
    case ReportOutcome::Partial:
        return {"critical", documentText("Partly completed")};
    case ReportOutcome::Failed:
        return {"critical", documentText("Failed")};
    case ReportOutcome::Cancelled:
        return {"critical", documentText("Cancelled")};
    }
    return {"note", {}};
}

ReportStateWords masterStateWords(const ReportMasterState state)
{
    switch (state)
    {
    case ReportMasterState::Made:
        return {};
    case ReportMasterState::Reused:
        return {"reused", documentText("Reused")};
    case ReportMasterState::Failed:
        return {"critical", documentText("Failed")};
    case ReportMasterState::NotReached:
        return {"skipped", documentText("Not reached")};
    case ReportMasterState::Cancelled:
        return {"skipped", documentText("Cancelled")};
    }
    return {};
}

ReportStateWords astrometryWords(const ReportAstrometryOutcome outcome)
{
    switch (outcome)
    {
    case ReportAstrometryOutcome::Inherited:
        return {"good", documentText("Inherited")};
    case ReportAstrometryOutcome::Solved:
        return {"good", documentText("Solved")};
    case ReportAstrometryOutcome::None:
        return {"warning", documentText("None")};
    case ReportAstrometryOutcome::NotRecorded:
        return {};
    }
    return {};
}

QString calibrationStateNote(const ReportCalibrationState state)
{
    switch (state)
    {
    case ReportCalibrationState::Reused:
        return documentText("reused");
    case ReportCalibrationState::Supplied:
        return documentText("supplied by you");
    case ReportCalibrationState::Failed:
        return documentText("failed");
    case ReportCalibrationState::Made:
        return {};
    }
    return {};
}

QHash<QString, QString> masterLabels(const ReportModel& model)
{
    const bool severalTargets = model.run.targets.size() > 1;
    QHash<QString, QString> labels;
    QHash<QString, int> uses;
    for (const ReportMaster& master : model.masters)
    {
        QString label = master.filter.trimmed();
        if (master.kind == ReportMasterKind::CombinedRgb)
        {
            label = documentText("RGB");
        }
        if (label.isEmpty())
        {
            label = master.name;
        }
        else if (severalTargets && !master.target.trimmed().isEmpty())
        {
            label = master.target.trimmed() + QLatin1Char(' ') + label;
        }
        labels.insert(master.key, label);
        ++uses[label];
    }
    for (const ReportMaster& master : model.masters)
    {
        if (uses.value(labels.value(master.key)) > 1)
        {
            labels.insert(master.key, master.name);
        }
    }
    return labels;
}

MasterUse useOf(const ReportMaster& master)
{
    MasterUse use;
    use.taken = static_cast<int>(master.frames.size());
    bool stated = false;
    int used = 0;
    double seconds = 0.0;
    for (const ReportFrame& frame : master.frames)
    {
        stated = stated || frame.state.has_value();
        if (frame.state == ReportFrameState::Used)
        {
            ++used;
            seconds += frame.exposureSeconds.value_or(0.0);
        }
    }
    if (stated)
    {
        use.used = used;
        use.integratedMs = static_cast<qint64>(std::llround(seconds * 1000.0));
    }
    return use;
}

bool isThere(const ReportMaster& master)
{
    return master.state == ReportMasterState::Made || master.state == ReportMasterState::Reused;
}

bool runStopped(const ReportModel& model)
{
    return model.run.outcome == ReportOutcome::Partial || model.run.outcome == ReportOutcome::Failed ||
           model.run.outcome == ReportOutcome::Cancelled;
}

double valueOf(const std::optional<double>& value)
{
    return value.has_value() ? *value : std::numeric_limits<double>::quiet_NaN();
}

std::optional<double> medianOfValues(std::vector<double> values)
{
    const std::optional<ChartRange> range = chartRange(std::move(values));
    return range.has_value() ? std::optional<double>(range->median) : std::nullopt;
}

QString percentText(const double fraction)
{
    return formatFigure(100.0 * fraction) + QStringLiteral(" %");
}

QString clockText(const QDateTime& instant, const ReportTimeZone& zone)
{
    return instant.toTimeZone(zone.resolved()).time().toString(QStringLiteral("HH:mm"));
}

double minutesIntoNight(const QDate& night, const QDateTime& instant, const ReportTimeZone& zone)
{
    const QDateTime local = instant.toTimeZone(zone.resolved());
    return static_cast<double>(night.daysTo(local.date())) * 1440.0 +
           static_cast<double>(local.time().msecsSinceStartOfDay()) / 60000.0;
}

std::optional<ReportDither> ditherOf(const ReportDocumentContext& context,
                                     const std::vector<const ReportMaster*>& masters, const int size)
{
    // The nights in order; the lights that state none are a series of their own, last.
    std::vector<QDate> nights;
    bool anyWithoutNight = false;
    for (const ReportMaster* master : masters)
    {
        for (const ReportFrame& frame : master->frames)
        {
            if (!frame.shiftX.has_value() || !frame.shiftY.has_value())
            {
                continue;
            }
            if (!frame.night.has_value())
            {
                anyWithoutNight = true;
            }
            else if (std::find(nights.begin(), nights.end(), *frame.night) == nights.end())
            {
                nights.push_back(*frame.night);
            }
        }
    }
    std::sort(nights.begin(), nights.end());
    if (nights.empty() && !anyWithoutNight)
    {
        return std::nullopt;
    }

    DitherChart chart;
    chart.description = {documentText("Where each light sat against the registration reference"),
                         documentText("No light states where it sat.")};
    chart.xAxis.title = documentText("Shift in x (px)");
    chart.yAxis.title = documentText("Shift in y (px)");
    chart.size = size;
    for (const QDate& night : nights)
    {
        chart.nights.push_back(ChartSeries{.name = formatDate(night), .points = {}});
    }
    if (anyWithoutNight)
    {
        chart.nights.push_back(ChartSeries{.name = documentText("No capture time"), .points = {}});
    }

    std::vector<ChartState> leftOut;
    for (const ReportMaster* master : masters)
    {
        const QString label = context.labelOf(*master);
        for (const ReportFrame& frame : master->frames)
        {
            if (!frame.shiftX.has_value() || !frame.shiftY.has_value())
            {
                continue;
            }
            const std::size_t series =
                frame.night.has_value()
                    ? static_cast<std::size_t>(std::find(nights.begin(), nights.end(), *frame.night) - nights.begin())
                    : nights.size();
            ChartPoint point;
            point.x = *frame.shiftX;
            point.y = *frame.shiftY;
            point.state = chartStateOf(frame);
            // Where several masters share the chart a light is named by its master and its time: its file's name
            // is in the master's own chart and table.
            point.tip.heading = masters.size() > 1 ? label : frame.fileName;
            if (frame.capturedAt.has_value())
            {
                point.tip.lines << formatDateTime(*frame.capturedAt, context.zone);
            }
            point.tip.lines
                << documentText("Shift %1, %2 px").arg(formatFigure(*frame.shiftX), formatFigure(*frame.shiftY));
            point.tip.lines << fateLine(frame);
            chart.nights[series].points.push_back(point);
            if (point.state != ChartState::Used &&
                std::find(leftOut.begin(), leftOut.end(), point.state) == leftOut.end())
            {
                leftOut.push_back(point.state);
            }
        }
    }

    std::vector<ChartLegendItem> legend;
    for (std::size_t night = 0; night < chart.nights.size(); ++night)
    {
        legend.push_back(ChartLegendItem{.mark = ChartLegendItem::Mark::Night,
                                         .state = ChartState::Used,
                                         .night = static_cast<int>(night),
                                         .label = chart.nights[night].name,
                                         .value = formatCount(static_cast<qint64>(chart.nights[night].points.size()))});
    }
    for (const ChartState state : leftOut)
    {
        legend.push_back(ChartLegendItem{
            .mark = ChartLegendItem::Mark::Cross, .state = state, .night = 0, .label = stateName(state), .value = {}});
    }
    return ReportDither{.chart = ditherChart(chart), .legend = chartLegend(legend)};
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
        .anchors = {},
        .labels = masterLabels(model)};
    // Every part states its anchors before any part is written, so a link is made only to a place that exists.
    context.anchors.insert(QStringLiteral("top"));
    for (const QStringList& anchors : {summaryAnchors(model), mastersAnchors(model), earlierAnchors(model),
                                       sharedAnchors(model), runAnchors(model), provenanceAnchors(model)})
    {
        for (const QString& anchor : anchors)
        {
            context.anchors.insert(anchor);
        }
    }

    const ReportMarkup header = writeReportHeader(context);
    std::vector<ReportPart> parts;
    parts.push_back(writeReportSummary(context));
    parts.push_back(writeReportMasters(context));
    parts.push_back(writeReportEarlier(context));
    parts.push_back(writeReportShared(context));
    parts.push_back(writeReportRun(context));
    parts.push_back(writeReportProvenance(context));

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
