// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "PixelMathEditControl.h"
#include "app/UiStyleTokens.h"
#include "controls/IAppContext.h"
#include "ImageViewControl.h"
#include "controls/PixelMathVocabulary.h"
#include "session/ImageDocument.h"
#include "theme/AnDesignTokens.h"

#include <AfterNightEngine/Image/ImageDescriptor.h>
#include <AfterNightEngine/Image/SampleType.h>

#include <QGuiApplication>

namespace
{

bool resolveDarkModeFromAppPalette()
{
    if (!QGuiApplication::instance())
    {
        return true;
    }

    return an::ui::styling::isDarkPalette(QGuiApplication::palette());
}

} // anonymous namespace

std::unordered_map<QString, QString> PixelMathEditControl::m_functions_doc = {
    {"abs", "<h3>abs(x)</h3>Absolute value of x"},
    {"adev", "<h3>adev(image)</h3>Average absolute deviation from the median of the specified image."},
    {"acos", "<h3>acos(x)</h3>Arc cosine of x"},
    {"acosh", "<h3>acosh(x)</h3>Hyperbolic arc cosine of x"},
    {"asin", "<h3>asin(x)</h3>Arc sine of x"},
    {"asinh", "<h3>asinh(x)</h3>Hyperbolic arc sine of x"},
    {"asqr", "<h3>asqr(a,b,...)<br>asqr(image)</h3><ol><li>Mean of the squares of a set of two or more "
             "values.</li><li>Mean of the squared pixel sample values in the specified image.</li></ol>"},
    {"atan", "<h3>atan(x)</h3>Arc tangent of x"},
    {"atan2", "<h3>atan2(y,x)</h3>Arc tangent of y/x"},
    {"atanh", "<h3>atanh(x)</h3>Hyperbolic arc tangent of x"},
    {"bwmv", "<h3>bwmv(a,b,...)<br>bwmv(image)</h3><ol><li>Biweight midvariance (BWMV) of a set of two or more "
             "values.</li><li>Biweight midvariance (BWMV) of the specified image.</li></ol>"},
    {"ceil", "<h3>ceil(x)</h3>Lowest integer greater than or equal to x."},
    {"clamp", "<h3>clamp(x,a,b)</h3>x constrained to the [a,b] range:<br>Returns x if x &gt;= a and x &lt;= "
              "b<br>Returns a if x &lt; a<br>Returns b if x &gt; b"},
    {"cos", "<h3>cos(x)</h3>Cosine of x"},
    {"cosh", "<h3>cosh(x)</h3>Hyperbolic cosine of x"},
    {"e", "<h3>e()</h3>Constant value of e (2.71828)"},
    {"exp", "<h3>exp(x)</h3>Exponential function ex, where e=2.71828"},
    {"fac", "<h3>fac(x)</h3>Factorial function"},
    {"floor", "<h3>floor(x)</h3>Highest integer less than or equal to x"},
    {"ln", "<h3>ln(x)</h3>Natural logarithm of x, or base-e logarithm, where e=2.71828"},
    {"log", "<h3>log(x)</h3>Base-10 logarithm of x"},
    {"log10", "<h3>log10(x)</h3>Base-10 logarithm of x"},
    {"log2", "<h3>log2(x)</h3>Base-2 logarithm of x"},
    {"max", "<h3>max(a,b,...)<br>max(image)</h3><ol><li>Maximum value of a set of two or more values.</li><li>Maximum "
            "pixel sample value in the specified image</li></ol>"},
    {"mad", "<h3>mad(a,b, ...)<br>mad(image)</h3><p>Median absolute deviation from the median (MAD):</p><ol><li>MAD of "
            "a set of two or more values.</li><li>MAD of the specified image</li></ol>"},
    {"mean", "<h3>mean(a,b, ...)<br>mean(image)</h3><p>Arithmetic mean function:</p><ol><li>Arithmetic mean of a set "
             "of two or more values.</li><li>Arithmetic mean of the specified image</li></ol>"},
    {"mdev", "<h3>mdev(a,b, ...)<br>mdev(image)</h3><p>Median absolute deviation from the median (MAD):</p><ol><li>MAD "
             "of a set of two or more values.</li><li>MAD of the specified image</li></ol>"},
    {"med", "<h3>med(a,b,c, ...)<br>med(image)</h3><p>Median function:</p><ol><li>Median value of a set of three or "
            "more values.</li><li>Median pixel sample value of the specified image</li></ol>"},
    {"min", "<h3>min(a,b,...)<br>min(image)</h3><ol><li>Minimum value of a set of two or more values.</li><li>Minimum "
            "pixel sample value in the specified image</li></ol>"},
    {"mtf",
     "<h3>mtf(m,x)</h3>Midtones Transfer Function (MTF) of x for a midtones balance parameter m, 0 &lt;= m &lt;= 1"},
    {"ncr", "<h3>ncr(a,r)</h3>Combinations function"},
    {"noise", "<h3>noise(image)</h3>Estimation of gaussian noise of the image"},
    {"npr", "<h3>npr(a,r)</h3>Permutations function"},
    {"pi", "<b>pi()</b><br>Constant value of Pi (3.141592...)"},
    {"random", "<h3>random()<h3>Pseudo-random uniform deviate in the range [0,1)."},
    {"rand", "<h3>rand(a,b,[...])</h3>Pseudo-random uniform deviate in the range [0,1)."},
    {"range", "<h3>range(x,a,b)</h3>x constrained to the [a,b] range:<br>Returns x if x &gt;= a and x &lt;= "
              "b<br>Returns a if x &lt; a<br>Returns b if x &gt; b"},
    {"sdev", "<h3>sdev(a,b, ...)<br>sdev(image)</h3><p>Standard deviations:</p><ol><li>Standard deviation of a set of "
             "two or more values.</li><li>Standard deviation of the specified image</li></ol>"},
    {"sin", "<b>sin(x)</b><br>Sine of x"},
    {"sinh", "<b>sinh(x)</b><br>Hyperbolic sine of x"},
    {"sign", "<b>sign(x)</b><br>Sign of x"},
    {"sqrt", "<b>sqrt(x)</b><br>Square root of x"},
    {"ssqr", "<h3>ssqr(a,b, ...)<br>ssqr(image)</h3><p>Sum of squares:</p><ol><li>Sum of squares of a set of two or "
             "more values.</li><li>Sum of the squared pixel sample values in the specified image</li></ol>"},
    {"sum", "<b>sum(a,b,...)</b><br>Sum of a set of two or more values"},
    {"tan", "<b>tan(x)</b><br>Tangent of x"},
    {"tanh", "<b>tanh(x)</b><br>Hyperbolic tangent of x"},
    {"trunc", "<b>trunc(x)</b><br>Truncated integer part of x"},
    {"var", "<h3>var(a,b, ...)<br>var(image)</h3><ol><li>Variance of a set of two or more values.</li><li>Variance of "
            "the specified image</li></ol>"}};

std::unordered_map<QString, QString> PixelMathEditControl::m_global_functions_doc = {
    {"c", "<h3>c()</h3>Current channel"},
    {"channel", "<h3>channel()</h3>Current channel"},
    {"x", "<h3>x()</h3>Current horizontal (X-axis) coordinate in integer pixels"},
    {"xpos", "<h3>xpos()</h3>Current horizontal (X-axis) coordinate in integer pixels"},
    {"X", "<h3>X()</h3>Current normalized horizontal (X-axis) coordinate in the [0,1] range, where 1 corresponds to "
          "width() - 1."},
    {"y", "<h3>y()</h3>Current vertical (Y-axis) coordinate in integer pixels."},
    {"ypos", "<h3>ypos()</h3>Current vertical (Y-axis) coordinate in integer pixels."},
    {"Y", "<h3>Y()</h3>Current normalized vertical (Y-axis) coordinate in the [0,1] range, where 1 corresponds to "
          "height() - 1."},
    {"w", "<h3>w(image=$T)</h3>Width in pixels of the specified image.<br>If no image is speficied, then use the "
          "target image $T"},
    {"width", "<h3>width(image=$T)</h3>Width in pixels of the specified image.<br>If no image is speficied, then use "
              "the target image $T"},
    {"h", "<h3>h(image=$T)</h3>Height in pixels of the specified image.<br>If no image is speficied, then use the "
          "target image $T"},
    {"height", "<h3>height(image=$T)</h3>Height in pixels of the specified image.<br>If no image is speficied, then "
               "use the target image $T"},
    {"n", "<h3>n(image=$T)</h3>Number of channels in the specified image.<br>If no image is speficied, then use the "
          "target image $T"},
    {"channels", "<h3>channels(image=$T)</h3>Number of channels in the specified image.<br>If no image is speficied, "
                 "then use the target image $T"},
    {"iscolor", "<h3>iscolor(image=$T)</h3>Returns one if the specified image is in the RGB color space. Returns zero "
                "for a grayscale image.<br>If no image is speficied, then use the target image $T"},
    {"CIEL", "<h3>CIEL(image=$T)</h3>Normalized CIE L* component of the current pixel in the specified image"},
    {"CIEa", "<h3>CIEa(image=$T)</h3>Normalized CIE a* component of the current pixel in the specified image"},
    {"CIEb", "<h3>CIEb(image=$T)</h3>Normalized CIE b* component of the current pixel in the specified image"},
    {"CIEc", "<h3>CIEc(image=$T)</h3>Normalized CIE c* component of the current pixel in the specified image"},
    {"CIEh", "<h3>CIEh(image=$T)</h3>Normalized CIE h* component of the current pixel in the specified image"},
    {"CIEX", "<h3>CIEX(image=$T)</h3>Normalized CIE X component of the current pixel in the specified image"},
    {"CIEY", "<h3>CIEY(image=$T)</h3>Normalized CIE Y component of the current pixel in the specified image"},
    {"CIEZ", "<h3>CIEZ(image=$T)</h3>Normalized CIE Z component of the current pixel in the specified image"},
    {"HSLh", "<h3>HSLh(image=$T)</h3>Normalized HSL hue of the current pixel in the specified image"},
    {"HSLs", "<h3>HSLs(image=$T)</h3>Normalized HSL saturation of the current pixel in the specified image"},
    {"HSLl", "<h3>HSLl(image=$T)</h3>Normalized HSL lightness of the current pixel in the specified image"},
    {"HSVh", "<h3>HSVh(image=$T)</h3>Normalized HSV hue of the current pixel in the specified image"},
    {"HSVs", "<h3>HSVs(image=$T)</h3>Normalized HSV saturation of the current pixel in the specified image"},
    {"HSVv", "<h3>HSVv(image=$T)</h3>Normalized HSV value of the current pixel in the specified image"},
    {"YCbCrY", "<h3>YCbCrY(image=$T)</h3>Normalized YCbCr luma of the current pixel in the specified image"},
    {"YCbCrCb", "<h3>YCbCrCb(image=$T)</h3>Normalized YCbCr blue chroma of the current pixel in the specified image"},
    {"YCbCrCr", "<h3>YCbCrCr(image=$T)</h3>Normalized YCbCr red chroma of the current pixel in the specified image"}};

std::unordered_map<QString, QString> PixelMathEditControl::m_special_functions_doc = {
    {"iif", "<h3>iff(cond_expr, expr_true, expr_false)</h3><p>Conditional function (or inline if "
            "function):</p><ul><li>Returns expr_true if cond evaluates to nonzero.</li><li>Returns expr_false if cond "
            "evaluates to zero.</li></ul>"},
    {"inellipse", "<h3>inellipse(xc, yc, rx, ry)</h3>Returns one if the current coordinates are included in the "
                  "specified ellipse given by its center coordinates (xc,yc), horizontal and vertical radii rx and ry, "
                  "respectively. Returns zero otherwise."},
    {"inrect", "<h3>inrect(x0, y0, width, height)</h3>Returns one if the current coordinates are inside the specified "
               "rectangular region given by its top-left corner coordinates (x0,y0), width and height in pixels. "
               "Returns zero otherwise."},
    {"p",
     "<h3>p(image, x, y[, c])</h3>Pixel sample value of the specified image at (x,y) coordinates of channel c.<br>If "
     "not specified, the default channel is the current channel corresponding to the running expression."},
    {"pixel",
     "<h3>pixel(image, x, y[, c])</h3>Pixel sample value of the specified image at (x,y) coordinates of channel "
     "c.<br>If not specified, the default channel is the current channel corresponding to the running expression."}};

std::unordered_map<QString, QString> PixelMathEditControl::m_operators_doc = {
    {"+", "<h3>+x</h3><p>Unary Plus operator.</p><h3>x + y</h3><p>Addition operator.</p>"},
    {"-", "<h3>-x</h3><p>Unary Minus operator.</p><h3>x - y</h3><p>Subtraction operator.</p>"},
    {"--", "<h3>x -- y</h3><p>Absolute Difference operator.</p>"},
    {"*", "<h3>x * y</h3><p>Multiplication operator.</p>"},
    {"/", "<h3>x / y</h3><p>Division operator.</p>"},
    {"%", "<h3>x % y</h3><p>Modulus operator.</p>"},
    {"^", "<h3>x ^ y</h3><p>Exponentiation operator.</p>"},
    {"==", "<h3>x == y</h3><p>Equal To relational operator.</p>"},
    {"!=", "<h3>x != y</h3><p>Not Equal To relational operator.</p>"},
    {"<", "<h3>x &lt; y</h3><p>Less Than relational operator.</p>"},
    {"<=", "<h3>x &lt;= y</h3><p>Less Or Equal Than relational operator.</p>"},
    {">", "<h3>x &gt; y</h3><p>Greater Than relational operator.</p>"},
    {">=", "<h3>x &gt;= y</h3><p>Greater Or Equal Than relational operator.</p>"},
    {"&&", "<h3>x && y</h3><p>Logical AND operator.</p>"},
    {"||", "<h3>x || y</h3><p>Logical OR operator.</p>"},
    {"~", "<h3>~x</h3><p>Pixel Inversion operator.</p>"},
    {"!", "<h3>!x</h3><p>Logical NOT operator.</p>"},
    {"=", "<h3>x = y</h3><p>Assignment operator.</p>"},
    {"+=", "<h3>x += y</h3><p>Assignment / Addition operator.</p>"},
    {"-=", "<h3>x -= y</h3><p>Assignment / Subtraction operator.</p>"},
    {"--=", "<h3>x --= y</h3><p>Assignment / Absolute difference operator.</p>"},
    {"*=", "<h3>x *= y</h3><p>Assignment / Multiplication operator.</p>"},
    {"/=", "<h3>x / y</h3><p>Assignment / Division operator.</p>"},
    {"^=", "<h3>x ^ y</h3><p>Assignment / Exponentiation operator.</p>"},
    {"%=", "<h3>x %= y</h3><p>Assignment / Modulus operator.</p>"},
    {"~", "<h3>~x</h3><p>Pixel Inversion operator.</p>"},
    {";", "<h3>expr1 ; expr2; </h3><p>Expresson separator.</p>"},
    {"/**/", "<h3>/* &lt;comment&gt; */</h3><p>Block comment</p>"},
    {"//", "<h3>// &lt;comment&gt; </h3><p>Multiline comment</p>"}};

PixelMathEditControl::PixelMathEditControl(QWidget* parent) : QPlainTextEdit(parent)
{
    m_Highlighter = new PixelMathCodeHighlighter(document());

    setFont(an::ui::tokens().typography.monospace);

    const bool darkMode = an::ui::styling::isDarkPalette(palette());
    const auto syntaxPalette = an::ui::pixelMathSyntaxPalette(darkMode);
    QPalette editorPalette = palette();
    editorPalette.setColor(QPalette::Base, syntaxPalette.editorBackground);
    setPalette(editorPalette);

    setMouseTracking(true);

    m_TooltipTimer = new QTimer(this);
    m_TooltipTimer->setSingleShot(true);
    connect(m_TooltipTimer, &QTimer::timeout, this, &PixelMathEditControl::showTooltip);
}

PixelMathCodeHighlighter* PixelMathEditControl::getHighlighter() const
{
    return m_Highlighter;
}

QSet<QString> PixelMathEditControl::m_formats_allowed_for_tooltips = {"function", "spec_function", "symbol", "image"};

void PixelMathEditControl::setTargetImageRealName(const std::string& realName)
{
    m_target_image_real_name = realName;
    // TODO: Update internal caches here, including the potential mask $M
}

QString PixelMathEditControl::getDocForFunctionOrOperator(const QString& func)
{
    auto it = m_functions_doc.find(func);
    if (it != m_functions_doc.end())
    {
        return it->second;
    }

    auto it2 = m_global_functions_doc.find(func);
    if (it2 != m_global_functions_doc.end())
    {
        return it2->second;
    }

    auto it3 = m_special_functions_doc.find(func);
    if (it3 != m_special_functions_doc.end())
    {
        return it3->second;
    }

    auto it4 = m_operators_doc.find(func);
    if (it4 != m_operators_doc.end())
    {
        return it4->second;
    }

    return QString();
}

PixelMathEditControl::~PixelMathEditControl() = default;
// m_TooltipTimer has 'this' as Qt parent and m_Highlighter has document() as
// Qt parent, so both are automatically destroyed by Qt's parent-child mechanism.

void PixelMathEditControl::showTooltip()
{
    if (m_LastTooltip.word.isEmpty())
    {
        return;
    }

    if (m_LastTooltip.property == "function")
    {
        QToolTip::showText(m_LastTooltip.mousePos, getDocForFunction(m_LastTooltip.word), this);
    }
    else if (m_LastTooltip.property == "spec_function")
    {
        QToolTip::showText(m_LastTooltip.mousePos, getDocForSpecFunction(m_LastTooltip.word), this);
    }
    else if (m_LastTooltip.property == "symbol")
    {
        QToolTip::showText(m_LastTooltip.mousePos, getDocForSymbol(m_LastTooltip.word), this);
    }
    else if (m_LastTooltip.property == "image")
    {
        QToolTip::showText(m_LastTooltip.mousePos, getDocForImage(m_LastTooltip.word), this);
    }
}

void PixelMathEditControl::mouseMoveEvent(QMouseEvent* event)
{
    QTextCursor cursor = cursorForPosition(event->pos());

    // Get the position BEFORE selecting the word under
    int pos = cursor.position();

    cursor.select(QTextCursor::WordUnderCursor);

    QString prop = m_Highlighter->getSelectedPositionFormatProperty(pos);
    if (prop == "comment")
    {
        // The word is in a comment section, so do nothing
        return;
    }

    QString word = cursor.selectedText();

    if (!word.isEmpty() && m_formats_allowed_for_tooltips.contains(prop))
    {
        // Workaround for image macros, the cursor can't detect $T as a word... so
        // we need to capture the remaining part of the word and validate it
        if (prop == "image")
        {
            if (word == "$")
            {
                // Move cursor one position forward
                cursor.movePosition(QTextCursor::NextCharacter);

                // Check if the cursor is still within valid range
                if (cursor.atEnd())
                {
                    return;
                }

                QChar nextChar = cursor.document()->characterAt(cursor.position());

                word.append(nextChar);
            }
            else if (!cursor.atStart() && (word == "T" || word == "A" || word == "M"))
            {
                cursor.movePosition(QTextCursor::PreviousCharacter);
                cursor.movePosition(QTextCursor::PreviousCharacter);

                QChar prevChar = cursor.document()->characterAt(cursor.position());

                word = prevChar + word;
            }
        }

        m_LastTooltip.mousePos = event->globalPosition().toPoint();
        m_LastTooltip.word = word;
        m_LastTooltip.property = prop;

        m_TooltipTimer->start(500); // Set a 500 ms delay before showing the tooltip
    }
    else
    {
        m_TooltipTimer->stop(); // Stop the timer if not hovering over special text
        QToolTip::hideText();
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

QString PixelMathEditControl::getDocForFunction(const QString& func)
{
    auto it = m_functions_doc.find(func);
    if (it != m_functions_doc.end())
    {
        return it->second;
    }

    auto it2 = m_global_functions_doc.find(func);
    if (it2 != m_global_functions_doc.end())
    {
        return it2->second;
    }

    return QString();
}

QString PixelMathEditControl::getDocForSpecFunction(const QString& func)
{
    auto it = m_special_functions_doc.find(func);
    if (it != m_special_functions_doc.end())
    {
        return it->second;
    }

    return QString();
}

QString PixelMathEditControl::getDocForSymbol(const QString& func)
{
    return m_Highlighter->getDocForSymbol(func);
}

QString PixelMathEditControl::getDocForImage(const QString& imgname)
{
    auto* context = an::ui::appContext();
    auto* viewRegistry = context ? context->getViewRegistry() : nullptr;
    auto* viewManager = context ? context->getViewControlManager() : nullptr;
    ImageViewControl* vControl = nullptr;
    if (imgname == "$A")
    {
        vControl = viewRegistry ? viewRegistry->getActiveImageViewControl() : nullptr;
    }
    else if (imgname == "$T")
    {
        vControl = viewManager ? viewManager->getViewControlByName(m_target_image_real_name) : nullptr;
    }
    else if (imgname == "$M")
    {
        vControl = viewManager ? viewManager->getViewControlByName(m_target_image_real_name) : nullptr;
        if (vControl && vControl->isMasked())
        {
            vControl = vControl->getCurrentMask();
        }
        else
        {
            return QString("<p><strong>WARNING: Target image doesn't have any mask applied</strong></p>");
        }
    }
    else
    {
        vControl = viewManager ? viewManager->getViewControlByName(imgname.toUtf8().toStdString()) : nullptr;
    }

    if (vControl)
    {
        // The description is the document's own record: reading it takes no lease and waits for nothing.
        if (const std::optional<an::ImageDescriptor> described = vControl->document().descriptor();
            described.has_value())
        {
            std::string formattedText =
                fmt::format("<h4>Image: {}</h4>Width: {}<br>Height: {}<br>Channels: {}<br>Format: {}",
                            vControl->getUniqueName(), described->extent().width(), described->extent().height(),
                            described->extent().channelCount(), an::sampleTypeName(described->sampleType()));

            return QString::fromUtf8(formattedText);
        }
    }

    return QString();
}

PixelMathCodeHighlighter::PixelMathCodeHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent)
{
    const auto syntaxPalette = an::ui::pixelMathSyntaxPalette(resolveDarkModeFromAppPalette());

    m_FunctionFormat.setForeground(syntaxPalette.functionColor);
    m_FunctionFormat.setFontWeight(QFont::Bold);
    m_FunctionFormat.setProperty(QTextFormat::UserProperty, QVariant("function"));

    m_SpecialFunctionFormat.setForeground(syntaxPalette.specialFunctionColor);
    m_SpecialFunctionFormat.setFontWeight(QFont::Bold);
    m_SpecialFunctionFormat.setProperty(QTextFormat::UserProperty, QVariant("spec_function"));

    m_ImageFormat.setForeground(syntaxPalette.imageColor);
    m_ImageFormat.setFontWeight(QFont::Bold);
    m_ImageFormat.setProperty(QTextFormat::UserProperty, QVariant("image"));

    m_LocalSymbolFormat.setForeground(syntaxPalette.symbolColor);
    m_LocalSymbolFormat.setFontWeight(QFont::Bold);
    m_LocalSymbolFormat.setProperty(QTextFormat::UserProperty, QVariant("symbol"));

    // TODO: Set a different color for global symbols
    m_GlobalSymbolFormat.setForeground(syntaxPalette.symbolColor);
    m_GlobalSymbolFormat.setFontWeight(QFont::Bold);
    m_GlobalSymbolFormat.setProperty(QTextFormat::UserProperty, QVariant("symbol"));

    m_CommentFormat.setForeground(syntaxPalette.commentColor);
    m_CommentFormat.setFontItalic(true);
    m_CommentFormat.setProperty(QTextFormat::UserProperty, QVariant("comment"));

    m_ParenFormat.setForeground(syntaxPalette.punctuationColor);
    m_ParenFormat.setFontWeight(QFont::Bold);
    m_ParenFormat.setProperty(QTextFormat::UserProperty, QVariant("paren"));

    m_SemiColonFormat.setForeground(syntaxPalette.punctuationColor);
    m_SemiColonFormat.setProperty(QTextFormat::UserProperty, QVariant("semicolon"));

    m_ComaFormat.setForeground(syntaxPalette.punctuationColor);
    m_ComaFormat.setProperty(QTextFormat::UserProperty, QVariant("coma"));

    m_BracketFormat.setForeground(syntaxPalette.bracketColor);
    m_BracketFormat.setProperty(QTextFormat::UserProperty, QVariant("bracket"));

    m_OperatorFormat.setForeground(syntaxPalette.operatorColor);
    m_OperatorFormat.setProperty(QTextFormat::UserProperty, QVariant("operator"));

    m_DigitFormat.setForeground(syntaxPalette.digitColor);
    m_DigitFormat.setProperty(QTextFormat::UserProperty, QVariant("digit"));

    initialize();
}

QTextCharFormat PixelMathCodeHighlighter::formatForPosition(int position) const
{
    auto it = m_PositionFormats.find(position);

    if (it != m_PositionFormats.end())
    {
        return it->second;
    }

    return QTextCharFormat(); // Return an invalid format if not found
}

QString PixelMathCodeHighlighter::getSelectedPositionFormatProperty(int position) const
{
    auto it = m_PositionFormats.find(position);

    if (it != m_PositionFormats.end())
    {
        return it->second.property(QTextFormat::UserProperty).toString();
    }

    return QString();
}

void PixelMathCodeHighlighter::setFunctionFormat(const QTextCharFormat& format)
{
    m_FunctionFormat = format;
}

void PixelMathCodeHighlighter::setSpecialFunctionFormat(const QTextCharFormat& format)
{
    m_SpecialFunctionFormat = format;
}

void PixelMathCodeHighlighter::setComaFormat(const QTextCharFormat& format)
{
    m_ComaFormat = format;
}

void PixelMathCodeHighlighter::setSemiColonFormat(const QTextCharFormat& format)
{
    m_SemiColonFormat = format;
}

void PixelMathCodeHighlighter::setParenFormat(const QTextCharFormat& format)
{
    m_ParenFormat = format;
}

void PixelMathCodeHighlighter::setBracketFormat(const QTextCharFormat& format)
{
    m_BracketFormat = format;
}

void PixelMathCodeHighlighter::setOperatorFormat(const QTextCharFormat& format)
{
    m_OperatorFormat = format;
}

void PixelMathCodeHighlighter::setLocalSymbolFormat(const QTextCharFormat& format)
{
    m_LocalSymbolFormat = format;
}

void PixelMathCodeHighlighter::setGlobalSymbolFormat(const QTextCharFormat& format)
{
    m_GlobalSymbolFormat = format;
}

void PixelMathCodeHighlighter::setImageFormat(const QTextCharFormat& format)
{
    m_ImageFormat = format;
}

void PixelMathCodeHighlighter::setCommentFormat(const QTextCharFormat& format)
{
    m_CommentFormat = format;
}

void PixelMathCodeHighlighter::setDigitFormat(const QTextCharFormat& format)
{
    m_DigitFormat = format;
}

static const std::string escapeForRegex(const std::string& str)
{
    static std::string patterns(".^$|()[]{}*+?\\");

    std::string result;
    result.reserve(str.length() * 2); // Reserve more space to avoid reallocations

    for (wchar_t ch : str)
    {
        // Check if the character is a special regex character
        if (patterns.find(ch) != std::string::npos)
        {
            result += '\\'; // Add a backslash before the special character
        }
        result += ch;
    }

    return result;
}

void PixelMathCodeHighlighter::setImageList(const std::vector<std::string>& image_list)
{
    m_ImagesRules.clear();

    for (auto& _image : image_list)
    {
        auto pattern = fmt::format("(?<!\\w){}(?!\\w)", escapeForRegex(_image));
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_ImageFormat;
        m_ImagesRules.push_back(rule);
    }
}

void PixelMathCodeHighlighter::setSymbolMap(const std::map<std::string, double>& local_symbol_map,
                                            const std::map<std::string, double>& global_symbol_map)
{
    m_SymbolsRules.clear();

    for (auto& _symbol : local_symbol_map)
    {
        auto pattern = fmt::format("(?<!\\w){}(?!\\w)", escapeForRegex(_symbol.first));
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_LocalSymbolFormat;
        m_SymbolsRules.push_back(rule);
        m_SymbolsDoc[QString::fromUtf8(_symbol.first)] = QString("Local symbol. Initial value: %1").arg(_symbol.second);
    }

    for (auto& _symbol : global_symbol_map)
    {
        auto pattern = fmt::format("(?<!\\w){}(?!\\w)", escapeForRegex(_symbol.first));
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_GlobalSymbolFormat;
        m_SymbolsRules.push_back(rule);
        m_SymbolsDoc[QString::fromUtf8(_symbol.first)] =
            QString("Global symbol. Initial value: %1").arg(_symbol.second);
    }
}

QString PixelMathCodeHighlighter::getDocForSymbol(const QString& symbol)
{
    auto it = m_SymbolsDoc.find(symbol);
    if (it != m_SymbolsDoc.end())
        return it->second;

    return "User defined symbol";
}

void PixelMathCodeHighlighter::initialize()
{
    m_HighlightingRules.clear();
    m_SymbolsRules.clear();
    m_ImagesRules.clear();

    // The words come from the engine's function table and syntax, so the editor colours what the language has.
    const an::ui::PixelMathVocabulary vocabulary = an::ui::pixelMathVocabulary();

    // Functions
    for (const QString& _func : vocabulary.functions)
    {
        auto pattern = fmt::format("(?<!\\w){}(?!\\w)", escapeForRegex(_func.toStdString()));
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_FunctionFormat;
        m_HighlightingRules.push_back(rule);
    }

    // Special Functions
    for (const QString& _func : vocabulary.specialFunctions)
    {
        auto pattern = fmt::format("(?<!\\w){}(?!\\w)", escapeForRegex(_func.toStdString()));
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_SpecialFunctionFormat;
        m_HighlightingRules.push_back(rule);
    }

    // Operators
    for (const QString& _op : vocabulary.operators)
    {
        auto pattern = escapeForRegex(_op.toStdString());
        HighlightingRuleSPtr rule = std::make_shared<HighlightingRule>();
        rule->pattern = QRegularExpression(QString::fromUtf8(pattern));
        rule->format = m_OperatorFormat;
        m_HighlightingRules.push_back(rule);
    }

    // Digits
    HighlightingRuleSPtr digitRule = std::make_shared<HighlightingRule>();
    digitRule->pattern = QRegularExpression("\\b[0-9\\.]+[^a-zA-Z]+");
    digitRule->format = m_DigitFormat;
    m_HighlightingRules.push_back(digitRule);

    // Parentheses
    HighlightingRuleSPtr parenRule = std::make_shared<HighlightingRule>();
    parenRule->pattern = QRegularExpression("[()]");
    parenRule->format = m_ParenFormat;
    m_HighlightingRules.push_back(parenRule);

    // Coma
    HighlightingRuleSPtr comaRule = std::make_shared<HighlightingRule>();
    comaRule->pattern = QRegularExpression(",");
    comaRule->format = m_ComaFormat;
    m_HighlightingRules.push_back(comaRule);

    // Semicolon
    HighlightingRuleSPtr semiColonRule = std::make_shared<HighlightingRule>();
    semiColonRule->pattern = QRegularExpression(";");
    semiColonRule->format = m_SemiColonFormat;
    m_HighlightingRules.push_back(semiColonRule);

    // Brackets
    HighlightingRuleSPtr bracketRule = std::make_shared<HighlightingRule>();
    bracketRule->pattern = QRegularExpression("[\\[\\]]");
    bracketRule->format = m_BracketFormat;
    m_HighlightingRules.push_back(bracketRule);

    // Single-line comments
    HighlightingRuleSPtr commentRule = std::make_shared<HighlightingRule>();
    commentRule->pattern = QRegularExpression("//[^\n]*");
    commentRule->format = m_CommentFormat;
    m_HighlightingRules.push_back(commentRule);

    // Multi-line comments start and end patterns
    m_MultiLineCommentStartPattern = QRegularExpression("/\\*");
    m_MultiLineCommentEndPattern = QRegularExpression("\\*/");
}

void PixelMathCodeHighlighter::highlightBlock(const QString& text)
{
    int blockPosition = currentBlock().position();

    // Apply symbols rules
    for (auto& rule : m_SymbolsRules)
    {
        QRegularExpressionMatchIterator matchIterator = rule->pattern.globalMatch(text);
        while (matchIterator.hasNext())
        {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule->format);
            persistFormat(blockPosition, match.capturedStart(), match.capturedLength(), rule->format);
        }
    }

    // Apply images rules
    for (auto& rule : m_ImagesRules)
    {
        QRegularExpressionMatchIterator matchIterator = rule->pattern.globalMatch(text);
        while (matchIterator.hasNext())
        {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule->format);
            persistFormat(blockPosition, match.capturedStart(), match.capturedLength(), rule->format);
        }
    }

    // Apply highlighting rules
    for (auto& rule : m_HighlightingRules)
    {
        QRegularExpressionMatchIterator matchIterator = rule->pattern.globalMatch(text);
        while (matchIterator.hasNext())
        {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule->format);
            persistFormat(blockPosition, match.capturedStart(), match.capturedLength(), rule->format);
        }
    }

    // Handle multi-line comments and parenthesis
    setCurrentBlockState(0);

    int startIndex = 0;

    if (previousBlockState() != 1)
    {
        startIndex = text.indexOf(m_MultiLineCommentStartPattern);
    }

    while (startIndex >= 0)
    {
        QRegularExpressionMatch endMatch = m_MultiLineCommentEndPattern.match(text, startIndex);
        int endIndex = endMatch.capturedStart();
        int commentLength;

        if (endIndex == -1)
        {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        }
        else
        {
            commentLength = endIndex - startIndex + endMatch.capturedLength();
        }

        setFormat(startIndex, commentLength, m_CommentFormat);
        persistFormat(blockPosition, startIndex, commentLength, m_CommentFormat);
        startIndex = text.indexOf(m_MultiLineCommentStartPattern, startIndex + commentLength);
    }
}

void PixelMathCodeHighlighter::persistFormat(int blockPosition, int start, int count, const QTextCharFormat& format)
{
    // Store the format for each character position in the block
    for (int i = start; i < start + count; ++i)
    {
        m_PositionFormats[blockPosition + i] = format;
    }
}
