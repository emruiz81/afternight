// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

/// @file
/// The words the PixelMath editor highlights come from the engine's language: every name of its function tables, the
/// special functions apart, and every operator its syntax defines, beside the editor's own markers.

#include "Test_ControlsDialogs/DialogsTestSupport.h"

#include "controls/PixelMathEditControl.h"
#include "controls/PixelMathVocabulary.h"

#include <AfterNightEngine/Algorithms/PixelMath/PixelMathFunctions.h>

#include <gtest/gtest.h>

#include <set>

namespace
{

using an::tests::dialogs::card;
using an::tests::dialogs::DialogsTestContext;
using an::tests::dialogs::InstalledContext;
using an::tests::dialogs::makeCardDocument;
using an::tests::dialogs::makeView;
using an::tests::dialogs::settle;

std::set<QString> asSet(const QStringList& words)
{
    return std::set<QString>(words.begin(), words.end());
}

TEST(PixelMathVocabularyTest, EveryNameOfTheEnginesFunctionTablesIsHighlightedOnce)
{
    const an::ui::PixelMathVocabulary vocabulary = an::ui::pixelMathVocabulary();

    std::set<QString> engine;
    for (const auto table : {an::pixelMathFunctionRegistry(), an::pixelMathImageStatisticRegistry()})
    {
        for (const an::PixelMathFunctionEntry& entry : table)
        {
            engine.insert(QString::fromLatin1(entry.name.data(), static_cast<qsizetype>(entry.name.size())));
        }
    }
    std::set<QString> highlighted = asSet(vocabulary.functions);
    const std::set<QString> special = asSet(vocabulary.specialFunctions);
    for (const QString& word : special)
    {
        EXPECT_EQ(highlighted.count(word), 0u) << word.toStdString();
        highlighted.insert(word);
    }
    EXPECT_EQ(highlighted, engine);
    EXPECT_EQ(static_cast<std::size_t>(vocabulary.functions.size()), asSet(vocabulary.functions).size());
    EXPECT_TRUE(std::is_sorted(vocabulary.functions.begin(), vocabulary.functions.end()));
}

TEST(PixelMathVocabularyTest, TheSpecialFunctionsAreTheConditionalTheRegionTestsAndTheSample)
{
    const std::set<QString> special = asSet(an::ui::pixelMathVocabulary().specialFunctions);
    for (const char* word : {"iif", "inrect", "inellipse", "pixel", "p"})
    {
        EXPECT_EQ(special.count(QString::fromLatin1(word)), 1u) << word;
    }
    for (const QString& word : special)
    {
        const an::PixelMathFunctionSignature* signature = an::findPixelMathFunction(word.toStdString());
        ASSERT_NE(signature, nullptr) << word.toStdString();
        EXPECT_TRUE(signature->functionClass == an::PixelMathFunctionClass::Branch ||
                    signature->functionClass == an::PixelMathFunctionClass::ImageSample ||
                    signature->id == an::PixelMathFunctionId::InRectangle ||
                    signature->id == an::PixelMathFunctionId::InEllipse)
            << word.toStdString();
    }
}

TEST(PixelMathVocabularyTest, TheOperatorsAreEverySpellingTheSyntaxDefinesAndTheEditorsMarkers)
{
    // Exactly these: every operator spelling, and three markers of the editor's own - the block and line comments
    // and the statement separator.
    const std::set<QString> expected{
        "!",    "!=", "%",  "%=", "&&", "*",  "*=", "+",  "+=", "-",  "--", "--=", "-=", "/",
        "/**/", "//", "/=", ";",  "<",  "<=", "=",  "==", ">",  ">=", "^",  "^=",  "||", "~"};
    EXPECT_EQ(asSet(an::ui::pixelMathVocabulary().operators), expected);

    EXPECT_EQ(an::ui::pixelMathOperatorSpelling(an::PixelMathBinaryOperator::AbsoluteDifference), QStringLiteral("--"));
    EXPECT_EQ(an::ui::pixelMathOperatorSpelling(an::PixelMathUnaryOperator::Complement), QStringLiteral("~"));
    EXPECT_EQ(an::ui::pixelMathOperatorSpelling(an::PixelMathAssignmentOperator::PowerAssign), QStringLiteral("^="));
}

class PixelMathEditorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context = an::tests::dialogs::openDialogsContext();
        ASSERT_NE(context, nullptr);
        installed = std::make_unique<InstalledContext>(*context);
    }

    void TearDown() override
    {
        installed.reset();
        context.reset();
    }

    std::unique_ptr<DialogsTestContext> context;
    std::unique_ptr<InstalledContext> installed;
};

TEST_F(PixelMathEditorTest, TheEditorHighlightsAnExpressionWithTheEnginesVocabulary)
{
    PixelMathEditControl editor;
    editor.setPlainText(QStringLiteral("iif($T > 0.5, sqrt($T), med($T)) -- 0.1; // a note"));

    PixelMathCodeHighlighter* highlighter = editor.getHighlighter();
    ASSERT_NE(highlighter, nullptr);
    highlighter->rehighlight();
    const QString text = editor.toPlainText();
    const auto formatAt = [&](const QString& word) { return highlighter->formatForPosition(text.indexOf(word)); };
    // A function, a special function, an operator and a comment are each coloured; a function and a special function
    // apart, and a comment apart from both.
    for (const QString& word :
         {QStringLiteral("sqrt"), QStringLiteral("iif"), QStringLiteral("--"), QStringLiteral("//")})
    {
        EXPECT_NE(formatAt(word).foreground().style(), Qt::NoBrush) << word.toStdString();
    }
    EXPECT_NE(formatAt(QStringLiteral("sqrt")).foreground(), formatAt(QStringLiteral("iif")).foreground());
    EXPECT_NE(formatAt(QStringLiteral("//")).foreground(), formatAt(QStringLiteral("sqrt")).foreground());
    EXPECT_FALSE(PixelMathEditControl::getDocForFunctionOrOperator(QStringLiteral("sqrt")).isEmpty());
}

} // namespace
