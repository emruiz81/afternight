// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The PixelMath editor's vocabulary from the engine's function table and operator enumerations.

#include "stdafx.hpp"
#include "controls/PixelMathVocabulary.h"

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>

#include <algorithm>
#include <set>
#include <span>

namespace an::ui
{

namespace
{

/// Whether a function is one the editor colours as special.
[[nodiscard]] bool isSpecialFunction(const an::PixelMathFunctionSignature& signature) noexcept
{
    return signature.functionClass == an::PixelMathFunctionClass::Branch ||
           signature.functionClass == an::PixelMathFunctionClass::ImageSample ||
           signature.id == an::PixelMathFunctionId::InRectangle || signature.id == an::PixelMathFunctionId::InEllipse;
}

[[nodiscard]] QStringList sortedList(const std::set<QString>& words)
{
    return QStringList(words.begin(), words.end());
}

} // namespace

QString pixelMathOperatorSpelling(const an::PixelMathUnaryOperator op)
{
    switch (op)
    {
    case an::PixelMathUnaryOperator::Plus:
        return QStringLiteral("+");
    case an::PixelMathUnaryOperator::Negate:
        return QStringLiteral("-");
    case an::PixelMathUnaryOperator::Complement:
        return QStringLiteral("~");
    case an::PixelMathUnaryOperator::Not:
        return QStringLiteral("!");
    }
    return {};
}

QString pixelMathOperatorSpelling(const an::PixelMathBinaryOperator op)
{
    switch (op)
    {
    case an::PixelMathBinaryOperator::Add:
        return QStringLiteral("+");
    case an::PixelMathBinaryOperator::Subtract:
        return QStringLiteral("-");
    case an::PixelMathBinaryOperator::AbsoluteDifference:
        return QStringLiteral("--");
    case an::PixelMathBinaryOperator::Multiply:
        return QStringLiteral("*");
    case an::PixelMathBinaryOperator::Divide:
        return QStringLiteral("/");
    case an::PixelMathBinaryOperator::Modulus:
        return QStringLiteral("%");
    case an::PixelMathBinaryOperator::Power:
        return QStringLiteral("^");
    case an::PixelMathBinaryOperator::Equal:
        return QStringLiteral("==");
    case an::PixelMathBinaryOperator::NotEqual:
        return QStringLiteral("!=");
    case an::PixelMathBinaryOperator::Less:
        return QStringLiteral("<");
    case an::PixelMathBinaryOperator::LessOrEqual:
        return QStringLiteral("<=");
    case an::PixelMathBinaryOperator::Greater:
        return QStringLiteral(">");
    case an::PixelMathBinaryOperator::GreaterOrEqual:
        return QStringLiteral(">=");
    case an::PixelMathBinaryOperator::And:
        return QStringLiteral("&&");
    case an::PixelMathBinaryOperator::Or:
        return QStringLiteral("||");
    }
    return {};
}

QString pixelMathOperatorSpelling(const an::PixelMathAssignmentOperator op)
{
    switch (op)
    {
    case an::PixelMathAssignmentOperator::Assign:
        return QStringLiteral("=");
    case an::PixelMathAssignmentOperator::AddAssign:
        return QStringLiteral("+=");
    case an::PixelMathAssignmentOperator::SubtractAssign:
        return QStringLiteral("-=");
    case an::PixelMathAssignmentOperator::AbsoluteDifferenceAssign:
        return QStringLiteral("--=");
    case an::PixelMathAssignmentOperator::MultiplyAssign:
        return QStringLiteral("*=");
    case an::PixelMathAssignmentOperator::DivideAssign:
        return QStringLiteral("/=");
    case an::PixelMathAssignmentOperator::ModulusAssign:
        return QStringLiteral("%=");
    case an::PixelMathAssignmentOperator::PowerAssign:
        return QStringLiteral("^=");
    }
    return {};
}

PixelMathVocabulary pixelMathVocabulary()
{
    std::set<QString> functions;
    std::set<QString> special;
    for (const std::span<const an::PixelMathFunctionEntry> table :
         {an::pixelMathFunctionRegistry(), an::pixelMathImageStatisticRegistry()})
    {
        for (const an::PixelMathFunctionEntry& entry : table)
        {
            const QString name = QString::fromLatin1(entry.name.data(), static_cast<qsizetype>(entry.name.size()));
            (isSpecialFunction(entry.signature) ? special : functions).insert(name);
        }
    }

    std::set<QString> operators;
    for (std::uint8_t value = 0u; an::isPixelMathUnaryOperator(static_cast<an::PixelMathUnaryOperator>(value)); ++value)
    {
        operators.insert(pixelMathOperatorSpelling(static_cast<an::PixelMathUnaryOperator>(value)));
    }
    for (std::uint8_t value = 0u; an::isPixelMathBinaryOperator(static_cast<an::PixelMathBinaryOperator>(value));
         ++value)
    {
        operators.insert(pixelMathOperatorSpelling(static_cast<an::PixelMathBinaryOperator>(value)));
    }
    for (std::uint8_t value = 0u;
         an::isPixelMathAssignmentOperator(static_cast<an::PixelMathAssignmentOperator>(value)); ++value)
    {
        operators.insert(pixelMathOperatorSpelling(static_cast<an::PixelMathAssignmentOperator>(value)));
    }
    operators.insert(QStringLiteral(";"));
    operators.insert(QStringLiteral("/**/"));
    operators.insert(QStringLiteral("//"));

    return PixelMathVocabulary{sortedList(functions), sortedList(special), sortedList(operators)};
}

} // namespace an::ui
