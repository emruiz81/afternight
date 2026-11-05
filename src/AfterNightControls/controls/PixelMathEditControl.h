// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QColor>
#include <QFont>
#include <QToolTip>
#include <QMouseEvent>
#include <QTimer>

#include "PixelMathThemeTokens.h"

class PixelMathEditControl;

class AFTERNIGHTCONTROLS_API PixelMathCodeHighlighter : public QSyntaxHighlighter
{
public:
    PixelMathCodeHighlighter(QTextDocument* parent = nullptr);

    // Method to get the format at a specific position
    QTextCharFormat formatForPosition(int position) const;

    QString getSelectedPositionFormatProperty(int position) const;

    void setFunctionFormat(const QTextCharFormat& format);
    void setSpecialFunctionFormat(const QTextCharFormat& format);
    void setComaFormat(const QTextCharFormat& format);
    void setSemiColonFormat(const QTextCharFormat& format);
    void setParenFormat(const QTextCharFormat& format);
    void setBracketFormat(const QTextCharFormat& format);
    void setOperatorFormat(const QTextCharFormat& format);
    void setLocalSymbolFormat(const QTextCharFormat& format);
    void setGlobalSymbolFormat(const QTextCharFormat& format);
    void setImageFormat(const QTextCharFormat& format);
    void setCommentFormat(const QTextCharFormat& format);
    void setDigitFormat(const QTextCharFormat& format);

    void setImageList(const std::vector<std::string>& image_list);
    void setSymbolMap(const std::map<std::string, double>& local_symbol_map,
                      const std::map<std::string, double>& global_symbol_map);

    QString getDocForSymbol(const QString& symbol);

    void initialize();

protected:
    friend class PixelMathEditControl;
    void highlightBlock(const QString& text) override;
    void persistFormat(int blockPosition, int start, int count, const QTextCharFormat& format);

    struct HighlightingRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    typedef std::shared_ptr<HighlightingRule> HighlightingRuleSPtr;
    std::vector<HighlightingRuleSPtr> m_HighlightingRules;
    std::vector<HighlightingRuleSPtr> m_SymbolsRules;
    std::vector<HighlightingRuleSPtr> m_ImagesRules;

    QTextCharFormat m_FunctionFormat;
    QTextCharFormat m_SpecialFunctionFormat;
    QTextCharFormat m_ParenFormat;
    QTextCharFormat m_ComaFormat;
    QTextCharFormat m_SemiColonFormat;
    QTextCharFormat m_BracketFormat;
    QTextCharFormat m_OperatorFormat;
    QTextCharFormat m_LocalSymbolFormat;
    QTextCharFormat m_GlobalSymbolFormat;
    QTextCharFormat m_ImageFormat;
    QTextCharFormat m_CommentFormat;
    QTextCharFormat m_DigitFormat;

    QRegularExpression m_MultiLineCommentStartPattern;
    QRegularExpression m_MultiLineCommentEndPattern;

    std::map<int, QTextCharFormat> m_PositionFormats; // Persistent storage for all formats

    std::map<QString, QString> m_SymbolsDoc;

private:
};

class AFTERNIGHTCONTROLS_API PixelMathEditControl : public QPlainTextEdit
{
public:
    PixelMathEditControl(QWidget* parent = nullptr);

    PixelMathCodeHighlighter* getHighlighter() const;

    // The true name of $T
    void setTargetImageRealName(const std::string& realName);

    static QString getDocForFunctionOrOperator(const QString& func);

    ~PixelMathEditControl();

private:
    void showTooltip();

private:
    void mouseMoveEvent(QMouseEvent* event) override;

    QString getDocForFunction(const QString& func);
    QString getDocForSpecFunction(const QString& func);
    QString getDocForSymbol(const QString& func);
    QString getDocForImage(const QString& imgname);

    static std::unordered_map<QString, QString> m_functions_doc;

    static std::unordered_map<QString, QString> m_global_functions_doc;

    static std::unordered_map<QString, QString> m_special_functions_doc;

    static std::unordered_map<QString, QString> m_operators_doc;

    static QSet<QString> m_formats_allowed_for_tooltips;

    struct TooltipJob
    {
        QPoint mousePos;
        QString word;
        QString property;
    };

    TooltipJob m_LastTooltip;
    QTimer* m_TooltipTimer;

    std::string m_target_image_real_name;

    PixelMathCodeHighlighter* m_Highlighter = nullptr;
};
