// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include <AfterNightPlatform/Logging.h>

#include <QCborMap>
#include <QCborValue>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QString>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace an::ui::extensions::schema_utils
{

struct SchemaConditionTerm
{
    QString left_id;
    QString op;
    QCborValue right_value;
};

struct SchemaCondition
{
    std::vector<SchemaConditionTerm> terms;
};

inline QString normalizedParamType(QString type)
{
    type = type.trimmed().toLower();
    if (type == QStringLiteral("double"))
    {
        return QStringLiteral("float");
    }
    return type;
}

inline QCborValue jsonValueToCbor(const QJsonValue& value)
{
    return QCborValue::fromVariant(value.toVariant());
}

inline bool isFiniteNumber(const double value)
{
    return std::isfinite(value);
}

inline std::optional<double> cborToDouble(const QCborValue& value)
{
    if (value.isDouble())
    {
        const double number = value.toDouble();
        return isFiniteNumber(number) ? std::optional<double>(number) : std::nullopt;
    }
    if (value.isInteger())
    {
        return static_cast<double>(value.toInteger());
    }
    if (value.isString())
    {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok && isFiniteNumber(parsed))
        {
            return parsed;
        }
    }
    return std::nullopt;
}

inline std::optional<bool> cborToBool(const QCborValue& value)
{
    if (value.isBool())
    {
        return value.toBool();
    }
    if (value.isInteger())
    {
        return value.toInteger() != 0;
    }
    if (value.isDouble())
    {
        return std::fabs(value.toDouble()) > std::numeric_limits<double>::epsilon();
    }
    if (value.isString())
    {
        const QString text = value.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("1") || text == QStringLiteral("yes"))
        {
            return true;
        }
        if (text == QStringLiteral("false") || text == QStringLiteral("0") || text == QStringLiteral("no"))
        {
            return false;
        }
    }
    return std::nullopt;
}

inline QString cborToString(const QCborValue& value)
{
    if (value.isString())
    {
        return value.toString();
    }
    if (value.isBool())
    {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isInteger())
    {
        return QString::number(value.toInteger());
    }
    if (value.isDouble())
    {
        return QString::number(value.toDouble(), 'g', 17);
    }
    if (value.isArray() || value.isMap())
    {
        const QJsonDocument document = value.isArray() ? QJsonDocument::fromVariant(value.toVariant().toList())
                                                       : QJsonDocument::fromVariant(value.toVariant().toMap());
        return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
    }
    return value.toVariant().toString();
}

// Coerce a CBOR value according to the normalized type of a schema field.
// FieldT must provide: .type (QString), .default_value (QCborValue).
// When used_default is non-null, it is set to true if the default was used.
template <typename FieldT>
QCborValue coerceSchemaFieldValue(const FieldT& field, const QCborValue& input, bool* used_default = nullptr)
{
    auto use_default = [&]() {
        if (used_default)
        {
            *used_default = true;
        }
        return field.default_value;
    };

    if (used_default)
    {
        *used_default = false;
    }

    const QString type = normalizedParamType(field.type);
    if (input.isNull() || input.isUndefined())
    {
        return use_default();
    }
    if (type == QStringLiteral("bool"))
    {
        const auto value = cborToBool(input);
        return value.has_value() ? QCborValue(*value) : use_default();
    }
    if (type == QStringLiteral("int") || type == QStringLiteral("int_slider"))
    {
        const auto value = cborToDouble(input);
        return value.has_value() ? QCborValue(static_cast<qint64>(std::llround(*value))) : use_default();
    }
    if (type == QStringLiteral("float") || type == QStringLiteral("slider"))
    {
        const auto value = cborToDouble(input);
        return value.has_value() ? QCborValue(*value) : use_default();
    }
    if (type == QStringLiteral("console_output"))
    {
        if (input.isArray() || input.isMap() || input.isString())
        {
            return input;
        }
        return QCborValue(cborToString(input));
    }
    if (type == QStringLiteral("string") || type == QStringLiteral("file_path") || type == QStringLiteral("dir_path") ||
        type == QStringLiteral("status"))
    {
        return QCborValue(cborToString(input));
    }
    if (type == QStringLiteral("choice"))
    {
        return input;
    }
    if (type == QStringLiteral("curve_editor"))
    {
        return input.isArray() ? input : use_default();
    }

    return use_default();
}

inline std::optional<SchemaConditionTerm> parseSchemaConditionTerm(const QString& expression)
{
    static const QRegularExpression kPattern(
        QStringLiteral("^\\s*([A-Za-z0-9_.-]+)\\s*(==|!=|>=|<=|>|<)\\s*(.+?)\\s*$"));

    const QRegularExpressionMatch match = kPattern.match(expression);
    if (!match.hasMatch())
    {
        return std::nullopt;
    }

    const QString raw_value = match.captured(3).trimmed();
    QCborValue value;
    if ((raw_value.startsWith(QLatin1Char('\'')) && raw_value.endsWith(QLatin1Char('\''))) ||
        (raw_value.startsWith(QLatin1Char('"')) && raw_value.endsWith(QLatin1Char('"'))))
    {
        value = raw_value.mid(1, raw_value.size() - 2);
    }
    else if (raw_value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
    {
        value = true;
    }
    else if (raw_value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
    {
        value = false;
    }
    else
    {
        bool ok = false;
        const double numeric = raw_value.toDouble(&ok);
        value = ok ? QCborValue(numeric) : QCborValue(raw_value);
    }

    return SchemaConditionTerm{match.captured(1), match.captured(2), value};
}

inline std::optional<SchemaCondition> parseSchemaCondition(const QString& expression)
{
    auto is_word_char = [](const QChar ch) {
        return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('.') || ch == QLatin1Char('-');
    };

    std::vector<SchemaConditionTerm> terms;
    bool in_single_quote = false;
    bool in_double_quote = false;
    int start = 0;

    auto append_term = [&](const int end) -> bool {
        const QString segment = expression.mid(start, end - start).trimmed();
        if (segment.isEmpty())
        {
            return false;
        }
        const auto term = parseSchemaConditionTerm(segment);
        if (!term.has_value())
        {
            return false;
        }
        terms.push_back(*term);
        return true;
    };

    for (int i = 0; i < expression.size(); ++i)
    {
        const QChar ch = expression.at(i);
        if (ch == QLatin1Char('\'') && !in_double_quote)
        {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (ch == QLatin1Char('"') && !in_single_quote)
        {
            in_double_quote = !in_double_quote;
            continue;
        }
        if (in_single_quote || in_double_quote)
        {
            continue;
        }

        int delimiter_length = 0;
        if (expression.mid(i, 2) == QStringLiteral("&&"))
        {
            delimiter_length = 2;
        }
        else if (expression.mid(i, 3).compare(QStringLiteral("and"), Qt::CaseInsensitive) == 0)
        {
            const bool left_boundary = i == 0 || !is_word_char(expression.at(i - 1));
            const bool right_boundary = i + 3 >= expression.size() || !is_word_char(expression.at(i + 3));
            if (left_boundary && right_boundary)
            {
                delimiter_length = 3;
            }
        }

        if (delimiter_length > 0)
        {
            if (!append_term(i))
            {
                return std::nullopt;
            }
            i += delimiter_length - 1;
            start = i + 1;
        }
    }

    if (!append_term(expression.size()))
    {
        return std::nullopt;
    }

    return SchemaCondition{std::move(terms)};
}

inline bool evaluateSchemaConditionTerm(const SchemaConditionTerm& condition, const QCborMap& params)
{
    const QCborValue left = params.value(condition.left_id);
    if (left.isUndefined())
    {
        LOG_DEBUG("Schema condition references undefined field '{}'; treating as met", condition.left_id.toStdString());
        return true;
    }

    const auto left_number = cborToDouble(left);
    const auto right_number = cborToDouble(condition.right_value);
    if (left_number.has_value() && right_number.has_value())
    {
        if (condition.op == QStringLiteral("=="))
        {
            return std::fabs(*left_number - *right_number) <= 1e-9;
        }
        if (condition.op == QStringLiteral("!="))
        {
            return std::fabs(*left_number - *right_number) > 1e-9;
        }
        if (condition.op == QStringLiteral(">"))
        {
            return *left_number > *right_number;
        }
        if (condition.op == QStringLiteral(">="))
        {
            return *left_number >= *right_number;
        }
        if (condition.op == QStringLiteral("<"))
        {
            return *left_number < *right_number;
        }
        if (condition.op == QStringLiteral("<="))
        {
            return *left_number <= *right_number;
        }
    }

    const auto left_bool = cborToBool(left);
    const auto right_bool = cborToBool(condition.right_value);
    if (left_bool.has_value() && right_bool.has_value())
    {
        if (condition.op == QStringLiteral("=="))
        {
            return *left_bool == *right_bool;
        }
        if (condition.op == QStringLiteral("!="))
        {
            return *left_bool != *right_bool;
        }
        return true;
    }

    const QString left_text = cborToString(left);
    const QString right_text = cborToString(condition.right_value);
    if (condition.op == QStringLiteral("=="))
    {
        return left_text == right_text;
    }
    if (condition.op == QStringLiteral("!="))
    {
        return left_text != right_text;
    }

    return true;
}

inline bool evaluateSchemaCondition(const SchemaCondition& condition, const QCborMap& params)
{
    for (const auto& term : condition.terms)
    {
        if (!evaluateSchemaConditionTerm(term, params))
        {
            return false;
        }
    }
    return true;
}

} // namespace an::ui::extensions::schema_utils
