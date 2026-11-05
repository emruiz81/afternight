// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#pragma once

#include "AfterNightControlsExport.h"

#include <QWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QComboBox>

#include "DockableControl.h"

class QLabel;
class LogViewerSettingsControl;
class LogViewerFindTextControl;

class AFTERNIGHTCONTROLS_API LogViewerControl : public DockableControl
{
public:
    struct ThemeColorDefaults
    {
        QColor backgroundColor;
        QColor fontColor;
        QColor backgroundHighlightColor;
        QColor fontHighlightColor;
    };

    LogViewerControl(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());

    KDDockWidgets::QtWidgets::DockWidget* getDockWidget() override;

    void initialize();

    void shutdown();

    void setBackgroundColor(const QColor& color);

    QColor getBackgroundColor() const;

    void setBackgroundHighlightColor(const QColor& color);

    QColor getBackgroundHighlightColor() const;

    void setFontColor(const QColor& color);

    QColor getFontColor() const;

    void setFontHighlightColor(const QColor& color);

    QColor getFontHighlightColor() const;

    void setFontSize(const int size);

    int getOriginalFontSize() const;

    int getCurrentFontSize() const;

    void notifyLogViewerSettingsControlClosed();

    void notifyLogViewerFindTextControlClosed();

    void setMaxLines(const int maxLines);

    int getMaxLines() const;

    void saveSettings();

    void findText(const QString& text, bool caseSensitive, bool matchWholeWord, bool regularExpression, bool backward);

    void resetSearchCursor();

    void setFont(const QFont& font);

    void setFontItalic(bool italic);

    void setFontBold(bool bold);

    bool getFontItalic() const;

    bool getFontBold() const;

    QFont getFont() const;

    [[nodiscard]] ThemeColorDefaults currentThemeColorDefaults() const;
    [[nodiscard]] QColor logLevelColor(spdlog::level::level_enum level) const;

    void beginBatchUpdate();
    void endBatchUpdate();

    virtual ~LogViewerControl();

private:
    [[nodiscard]] spdlog::level::level_enum currentMinimumLevel() const;
    void updateFileLogLevelIndicator();
    void appendSessionEntry(const QString& text, spdlog::level::level_enum level, bool scrollToEnd = true);
    void trimVisibleLines();
    void scrollToBottom();
    void setAnsiColorFormattingEnabled(bool enabled);

    void on_level_currentTextChanged(const QString& text);

    void closeRequested();

    void isCurrentTabChanged(bool focused);
    void isFocusedChanged(bool focused);
    void isFloatingChanged(bool);
    void guestViewChanged();

    void zoomIn();
    void zoomOut();
    void zoom100();

    void wordWrap();
    void clearContent();
    void reloadContent();
    void settings();
    void search();
    void save();

private:
    void closeEvent(QCloseEvent* event) override;
    // void resizeEvent(QResizeEvent* event) override;

    void updateSettings();

    QVBoxLayout* m_CanvasLayout = nullptr;

    QPlainTextEdit* m_TextEdit = nullptr;

    QAction* m_WordWrapAction = nullptr;
    QAction* m_AnsiColorsAction = nullptr;
    QAction* m_SettingsAction = nullptr;
    QAction* m_SearchAction = nullptr;

    QColor m_BackgroundColor;
    QColor m_FontColor;
    QColor m_BackgroundHighlightColor;
    QColor m_FontHighlightColor;

    int m_OriginalFontSize = 12;
    int m_CurrentFontSize = 12;

    QLabel* m_FilterLabel = nullptr;
    std::unique_ptr<QComboBox> m_LevelComboBox;
    QLabel* m_FileLevelIndicator = nullptr;

    int m_MaxLines = 1000;

    LogViewerSettingsControl* m_LogViewerSettingsControl = nullptr;
    LogViewerFindTextControl* m_LogViewerFindTextControl = nullptr;

    QTextCursor currentSearchCursor;

    QFont m_Font;

    bool m_FontItalic = false;
    bool m_FontBold = false;
    bool m_IsShutdown = false;
    bool m_updatingSettings = false;
    bool m_autoScrollEnabled = true;
    bool m_AnsiColorFormattingEnabled = true;
};
