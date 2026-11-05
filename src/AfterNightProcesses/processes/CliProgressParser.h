// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QStringView>
#include <QVector>

namespace an::ui::processes::cli_progress
{

[[nodiscard]] inline int clampPercent(const int value)
{
    return std::clamp(value, 0, 100);
}

[[nodiscard]] inline int mapPercentToRange(const int percent, const int rangeStart, const int rangeEnd)
{
    const int start = clampPercent(rangeStart);
    const int end = clampPercent(rangeEnd);
    if (end <= start)
    {
        return start;
    }

    return start + (((end - start) * clampPercent(percent)) + 50) / 100;
}

[[nodiscard]] inline std::optional<int> extractLatestProgressPercent(const QStringView text)
{
    if (text.isEmpty())
    {
        return std::nullopt;
    }

    static const QRegularExpression percentRegex(
        QStringLiteral(R"((?:^|[^\d.])(\d{1,3}(?:[\.,]\d+)?)\s*(?:%|percent\b))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fractionRegex(
        QStringLiteral(R"((?:^|[^\d])(\d{1,6})\s*(?:/|of)\s*(\d{1,6})(?=$|[^\d]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression finishedRegex(
        QStringLiteral(R"((?:^|[^\d])(\d{1,3})\s+(?:finished|done|completed|processed)\b)"),
        QRegularExpression::CaseInsensitiveOption);

    std::optional<int> result;
    qsizetype resultPosition = -1;

    const auto consider = [&](const qsizetype position, const int percent) {
        if (position >= resultPosition)
        {
            result = clampPercent(percent);
            resultPosition = position;
        }
    };

    auto percentMatches = percentRegex.globalMatchView(text);
    while (percentMatches.hasNext())
    {
        const auto match = percentMatches.next();
        QString percentText = match.captured(1);
        percentText.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double percent = percentText.toDouble(&ok);
        if (ok)
        {
            consider(match.capturedStart(1), static_cast<int>(std::round(percent)));
        }
    }

    auto fractionMatches = fractionRegex.globalMatchView(text);
    while (fractionMatches.hasNext())
    {
        const auto match = fractionMatches.next();
        const int current = match.captured(1).toInt();
        const int total = match.captured(2).toInt();
        if (total > 0)
        {
            consider(match.capturedStart(1), (current * 100 + (total / 2)) / total);
        }
    }

    auto finishedMatches = finishedRegex.globalMatchView(text);
    while (finishedMatches.hasNext())
    {
        const auto match = finishedMatches.next();
        const qsizetype start = match.capturedStart(1);
        const QStringView prefix = text.left(start).trimmed();
        if (prefix.endsWith(u'/') || prefix.endsWith(QStringLiteral("of"), Qt::CaseInsensitive))
        {
            continue;
        }
        consider(match.capturedStart(1), match.captured(1).toInt());
    }

    return result;
}

struct MachineProgressEvent
{
    QString event;
    QString stage;
    std::optional<int> percent;
    std::optional<bool> ok;
    QString message;
};

struct MachineProgressParseBatch
{
    QVector<MachineProgressEvent> events;
    QStringList diagnostics;
};

[[nodiscard]] inline std::optional<MachineProgressEvent> parseMachineProgressLine(const QStringView line)
{
    const QByteArray payload = line.trimmed().toUtf8();
    if (payload.isEmpty() || !payload.startsWith('{'))
    {
        return std::nullopt;
    }

    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("starnetastro.cli.progress.v1"))
    {
        return std::nullopt;
    }

    MachineProgressEvent event;
    event.event = object.value(QStringLiteral("event")).toString();
    if (event.event.isEmpty())
    {
        return std::nullopt;
    }

    event.stage = object.value(QStringLiteral("stage")).toString();
    event.message = object.value(QStringLiteral("message")).toString();

    const QJsonValue percent_value = object.value(QStringLiteral("percent"));
    if (percent_value.isDouble())
    {
        const double percent = std::clamp(percent_value.toDouble(), 0.0, 100.0);
        event.percent = static_cast<int>(std::round(percent));
    }

    const QJsonValue ok_value = object.value(QStringLiteral("ok"));
    if (ok_value.isBool())
    {
        event.ok = ok_value.toBool();
    }

    return event;
}

class MachineProgressStreamParser
{
public:
    [[nodiscard]] MachineProgressParseBatch append(const QByteArray& data)
    {
        m_pending += data;
        return consume(false);
    }

    [[nodiscard]] MachineProgressParseBatch finish()
    {
        return consume(true);
    }

private:
    [[nodiscard]] MachineProgressParseBatch consume(const bool flush)
    {
        MachineProgressParseBatch batch;

        while (true)
        {
            const qsizetype newline = m_pending.indexOf('\n');
            if (newline < 0)
            {
                break;
            }

            QByteArray line = m_pending.left(newline);
            m_pending.remove(0, newline + 1);
            consumeLine(std::move(line), batch);
        }

        if (flush && !m_pending.isEmpty())
        {
            QByteArray line = std::move(m_pending);
            m_pending.clear();
            consumeLine(std::move(line), batch);
        }

        return batch;
    }

    static void consumeLine(QByteArray line, MachineProgressParseBatch& batch)
    {
        if (line.endsWith('\r'))
        {
            line.chop(1);
        }

        const QString text = QString::fromUtf8(line).trimmed();
        if (text.isEmpty())
        {
            return;
        }

        if (auto event = parseMachineProgressLine(QStringView(text)))
        {
            batch.events.push_back(std::move(*event));
        }
        else
        {
            batch.diagnostics.push_back(text);
        }
    }

    QByteArray m_pending;
};

} // namespace an::ui::processes::cli_progress
